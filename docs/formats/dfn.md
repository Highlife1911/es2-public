# EarthSiege II DFN/HFN bitmap fonts

Layout checked on all 62 font members of the CD (36 DFN, 26
HFN) and against VSHELL's reader. Static reading of the
reference `VER95\VSHELL.EXE` (llvm-objdump listing,
image base `0x00400000`) plus the real-data test `//formats:dfn_real_test`.
How VSHELL draws text with these fonts (label, button, edit
field, the mission screen's text pages, line breaks, colours) is in
[vshell_text.md](../reverse-engineering/vshell_text.md).

All integers are little-endian. Offsets are member offsets (the VOL payload,
without the nine-byte block prefix and the repeated-byte suffix of
[vol.md](vol.md)). "Object +n" is the in-memory `GLFont` field the original
reader fills; for the header it is always the file offset minus 4.

## Evidence

| What | Where | How verified |
|---|---|---|
| Tag and class | registration table `0x47bda8` (entries {tag, factory, name}): tag `0x00280005` → factory `0x454578` "GLFont"; tag `0x00280003` → `0x454608` "Old GLFont" | listing and DATA dump; `0x454608` is `xor eax, eax; ret` (no object) |
| Vtable | `0x47c004`: slot 0 `0x454610` (returns the tag), 1 `0x45461c`, 2 `0x454658` (size queries), 3 `0x4547b4` (read), 4 `0x454694` (write), 5 `0x4046ac`, 6 `0x401074` (destructor) | DATA dump; the factory calls slot 3 (`0x4545f0`) |
| Envelope | object loader `0x4041ab` → `0x404818` reads tag and body size, `0x404a68` closes the item | listing |
| Body | `0x4547b4..0x454974` | listing; every CD member parses with exactly this order and is consumed exactly (`dfn_real_test`) |
| Glyph access | `getWidth` `0x4010fd`, height `0x4011de`/`0x4011ea`, glyph draw `0x4011f6` (`src32\GL_FONT.CPP`, assertion strings `0x46c070..0x46c230`) | listing |
| DBSIM | read method `0x482a14` reads the same eleven words and the data size (`0x482a14..0x482ab4`); its factory is `0x4827d4` ([dbsim map](../functions/dbsim-480000.md)) | DBSIM listing, header part only; the rest of DBSIM's reader was not followed |

## Layout

| Offset | Width | Object | Meaning |
|---|---|---|---|
| `0x00` | 4 | — | Tag `05 00 28 00`. |
| `0x04` | 4 | — | Body size S: bytes after these eight. |
| `0x08` | 2 | `+0x04` | N, the glyph count (signed word; the reader allocates `N × 4` for the offset table when N ≠ 0). |
| `0x0a` | 2 | `+0x06` | Character-map word count M; used only when the first code is −1. |
| `0x0c` | 2 | `+0x08` | First code F: glyph i draws byte value `F + i`. −1 means "a character map is stored". |
| `0x0e` | 2 | `+0x0a` | Height H in rows, of every glyph bitmap; returned by `0x4011de`/`0x4011ea`. |
| `0x10` | 2 | `+0x0c` | Fixed width; −1 means "per-glyph widths are stored". |
| `0x12` | 2 | `+0x0e` | Not read by VSHELL's text code; equals H in all 62 members. |
| `0x14` | 2 | `+0x10` | Not read by VSHELL's text code; equals the widest glyph in 60 members, 16 in the two shell `FONT.DFN` (widest 14). **H:** nominal maximum width. |
| `0x16` | 2 | `+0x12` | Bits per pixel (8). Copied into the glyph bitmap header (`0x4012b3`). |
| `0x18` | 2 | `+0x14` | Compression (0). The glyph draw and the string draw stop with a fatal assertion when it is nonzero ("Only uncompressed font currently supported", `0x40120e`, `0x401377`). |
| `0x1a` | 2 | `+0x16` | Baseline B: the glyph's top row is drawn at `y − B` for pen position y (`0x4012ee`). |
| `0x1c` | 2 | `+0x18` | Colour count C. |
| `0x1e` | 4 | `+0x1a` | Pixel data size D. |
| `0x22` | `4C` | `+0x1e` | C colour records of four bytes (read when C ≠ 0). |
| … | D | `+0x22` | Glyph pixel data (read when D ≠ 0). |
| … | `4N` | `+0x26` | N 32-bit offsets into the pixel data (read when N ≠ 0). |
| … | N | `+0x2a` | One width byte per glyph, only when the fixed width is −1. |
| … | `2M` | `+0x2e` | M words of the character map, only when F is −1. |

The read order is exactly this (`0x454859` colours, `0x45488a` data,
`0x4548f5` offsets, `0x45492a` widths, `0x454961` map). The counts are read
as signed words (`movsx`) and only tested for zero; the reader validates
nothing else. The size queries in slots 1 and 2 count `4N` for the width table
and `4M` for the map where the reader reads N and `2M` bytes; they are not used
by any load path traced here.

**Glyph bitmaps.** Glyph i is `width_i × H` bytes at
`data + offset_i`, rows top first, one palette index per byte, no row padding;
`width_i` is the width byte (or the fixed width). The glyph draw builds a
bitmap header from exactly these values (`0x40129e..0x4012e3`: height, width,
depth, flags, `w × H` length, data pointer) and passes it to the bitmap draw
`0x458ee4`, the same path as images (the blitter `0x45a09c` of
[vshell_presentation.md](../reverse-engineering/vshell_presentation.md)):
**index 0 is transparent**, every other index is written as stored. The width
is also the advance; there is no kerning and no inter-character gap. In all
62 members the offsets are contiguous (`offset_{i+1} = offset_i + width_i × H`,
the last bitmap ends at D); the format does not require it.

**Character mapping.** A byte b of a string is glyph `b − F`;
there is no translation table in use. VSHELL's strings are stored in the DOS
code page of the game's texts and drawn as stored: the German `ESTEXT.BIN`
uses `0x81 0x84 0x94 0x99 0x9a 0xe1` (ü ä ö Ö Ü ß), the French one
`0x82 0x83 0x85 0x87 0x88 0x8a 0x8c` (é â à ç ê è î), and every such byte has a
glyph in the shell font (`dfn_real_test`: 3,809 / 4,318 / 4,070 bytes of the
English, French and German `ESTEXT.BIN`, none without a glyph). The character
map (F = −1) is read and freed but never consulted: `getWidth` and the glyph
draw use F arithmetically, so a font with a map would be drawn wrongly by
VSHELL. No CD font has one.

**Codes without a glyph.** `getWidth` (`0x4010fd`) tests `F ≤ b < F + N` on
the byte value (zero-extended, `0x40110b..0x401111`); outside, it logs a
severity-3 assertion ("Character %d is not in font", non-fatal: `0x44e560`
returns for severities below 4) and still reads `widths[b − F]`, outside the
table. The glyph draw logs a severity-2 assertion and draws `?` (`0x3f`)
instead (`0x401293`). The string draw advances with `getWidth` of the original
byte. So an out-of-range byte draws a `?` and advances by an undefined amount.
In the shell fonts these are the bytes `0x00..0x1f` and `0xff` (FONT2 and BLACK
of SHELL0 also `0xf9..0xfe`).

## Envelope, padding and errors of the original

The object loader reads the tag and the body size, remembers the end of the
item as `position + S` rounded up to even, and looks the tag up in the
registration table (`0x404818`). An unknown tag is a fatal assertion
(`0x4041fd`); the "Old GLFont" tag 3 creates no object, so `loadFont`
(`0x4088da`) returns null and its callers stop with a fatal assertion. After
the read method the loader closes the item (`0x404a68`): if more than the
declared size was read it reports "Have read more than …"; otherwise it skips
the remaining bytes up to the rounded end one by one, stopping at the end of
the stream. The CD members have odd body sizes without the pad byte (for
example `FONT.DFN`, S = 18,241, member length 18,249); the skip then simply
reaches the end of the member.

## The corpus

All 62 members are proportional (fixed width −1), uncompressed 8-bit, F = 32,
no colours, no character map, N = 217 or 223 (codes 32..248 or 32..254),
13,670 glyphs in all (`dfn_real_test`).

| Archive | Members | N | H | B | Word `0x14` | Pixel indices used (besides 0) |
|---|---|---|---|---|---|---|
| `SHELL0.VOL` | `DFN\MAP.DFN` | 223 | 8 | 7 | 6 | 58, 59 |
| | `DFN\BLACK.DFN`, `DFN\FONT2.DFN` | 217 | 13 | 10 | 9 | 16 (BLACK), 41 (FONT2) |
| | `DFN\FONT.DFN` | 223 | 18 | 0 | 16 | 2 |
| `SHELL1.VOL` (priority `0x0a`) | the same four names | 223 | as SHELL0 | as SHELL0 | as SHELL0 | as SHELL0 |
| `SIMPATCH.VOL` | `DFN\CPYLW`, `GREEN6X8` | 223 | 10 | 8, 7 | 8 | — |
| | `hfn\CPYLW`, `GREEN6X8` | 223 | 13 | 11 | 9 | — |
| `SIMVOL0.VOL` | 26 `dfn\*.DFN` (`TITLE`, `ACTIVE`, `PUSHED`, `INACTIVE`, `GREEN6X8`, `RED6X8`, `CP*`, `WHITE`, `GRAY`, `DARK`, `GREEN`, `RED`, `HUD1..3`, `CPDARK`, `FONT`) | 223 | 10 | 8 (`GREEN6X8` 7) | 8 | — |
| | 24 `hfn\*.HFN` | 217 | 13 | 11 | 9 | — |

FONT2 and BLACK are the same glyph shapes with different ink: every glyph
pixel of `FONT2.DFN` is 0 or 41, of `BLACK.DFN` 0 or 16 (counted over all
glyphs). The shell's text colour is applied afterwards by recolouring index
41 ([vshell_text.md](../reverse-engineering/vshell_text.md#colour)).
`SHELL1.VOL` is a four-member font patch: its `FONT2`/`BLACK` add the codes
249..254 (249, 250, 251, 253, 254 with width 0), and all four differ from the
SHELL0 copies in some glyphs (member lengths SHELL0 / SHELL1: MAP 5,917 /
5,941, BLACK and FONT2 22,361 / 22,456, FONT 18,249 / 18,357). VSHELL's VOL
order takes the SHELL1 copies ([vol.md](vol.md), checked again by
`dfn_real_test` with the VSHELL mask `0x100`).

## Which fonts VSHELL uses

| Global | Loaded | File | Used by |
|---|---|---|---|
| `[0x46ddfc]` | `0x407a13` (`0x40796c(1)`, start-up step 15 of [vshell_process.md](../reverse-engineering/vshell_process.md)) | `dfn\font2.dfn` | 78 references: main menu, Quit box, CD prompt, Preferences, Saved games, name entry, registration, Crew, mission screen, Build, both scrap warnings, Practice, Replay, End of game, Launch warning, DEBUG mission file |
| `[0x46de00]` | `0x407a60` | `dfn\font2.dfn` again (a second, separate object) | 212 references: Repair, Preferences, Saved games, squad panel, Weapons, Crew, mission screen (also its five text pages), Build, Armory, Practice, the dialogs; every edit field |
| `[0x46de04]` | `0x407aac` | `dfn\black.dfn` | 9 references, all in the service-bay constructor `0x43afd4`: the labels of the bay's button bar (class `0x40a2c4`) |
| `[0x471dc0]` | `0x424490..0x42449b` (map tables `0x424408`) | `dfn\map.dfn` (literal path) | none: freed at `0x424538`, never passed to a draw |

The names come from `0x40d89f(name, "dfn")` → `dfn\<name>.dfn`; the strings
`font2` (twice), `black` and `dfn\map.dfn` are the only font names in the
executable, so **`FONT.DFN` is never loaded by VSHELL** and `MAP.DFN` is loaded
but not drawn. The two FONT2 objects have identical content and nothing in
VSHELL changes glyph pixels (the font's own `realize` `0x401438`,
`remapColors` `0x4014e3` and their helper `0x4014a6` have no caller and no
pointer to them exists in the image), so they are interchangeable for drawing.
The per-screen attribution counts references by the function that contains
them (screen addresses from [vshell_ui.md](../reverse-engineering/vshell_ui.md)
§3); which widget each reference belongs to is in the layout table of
[vshell_widgets.md](../reverse-engineering/vshell_widgets.md).

## HFN

HFN members use the same tag and, as far as checked, the same layout: all 26
parse with the reader above and are consumed exactly; DBSIM's read method
reads the same header. They are the 13-row (baseline 11) counterparts of the
10-row simulator DFNs; **H:** the high-resolution variant, as with DBA/HBA.
VSHELL never names an HFN. DBSIM's choice between DFN and HFN and its text
drawing are not traced (DBSIM side, not part of the shell).

## Reader (`//formats:dfn`)

[`dfn.h`](../../formats/include/es2/formats/dfn.h) parses one member without
allocating and returns borrowed spans of the colour records, pixel data,
offsets, widths and map; `es2_dfn_advance` (`0x4010fd`), `es2_dfn_glyph_at`
(the bitmap of `0x4011f6`) and `es2_dfn_text_width` (`0x4011af`) answer per
byte value. Deviations from the original reader, all on inputs the CD does
not contain: it refuses a wrong tag (also tag 3), a body that does not fit the
input, bytes after the body other than one zero pad byte after an odd body,
unread bytes inside the body (the original skips them), negative counts, a
negative height or a fixed width below −1, and in uncompressed 8-bit fonts
without a map any glyph bitmap outside the pixel data. Codes outside the font
are `ES2_ERR_NOT_FOUND` (the original reads outside its table, see above);
a font with a map, or compressed or non-8-bit glyphs, is
`ES2_ERR_UNSUPPORTED` for glyph access.

Tests: `//formats:dfn_test` (synthetic fixtures, every refusal, the pad and
range rules, fixed-width, colour, map and compressed variants);
`//formats:dfn_real_test` (reads the 13 VOL archives from `ES2_DATA_ROOT`,
skips without it): parses all 62 members, checks the corpus properties above,
resolves the four shell fonts with VSHELL's VOL order and checks their
metrics, ink indices and empty codes, and the `ESTEXT.BIN` coverage. It keeps
counts and metrics only, no glyph bytes.

## Open

- The meaning of the header words `0x12` and `0x14` (not read by VSHELL's text
  code; see the table).
- Whether bits 5..7 of the glyph bitmap's flags byte matter: the glyph draw
  clears bits 0..4 of a stack byte it never initialises (`0x4012b9`,
  `0x4012bd`); no reader of bits 5..7 is known in the blit path.
- DBSIM's use of DFN/HFN (beyond the shared header reader).
