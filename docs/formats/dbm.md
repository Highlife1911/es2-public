# EarthSiege II DBM bitmaps and DBA/HBA arrays

Status: **all 314 DBM/DBA/HBA members inspected and all 3,453 declared
bitmaps decoded**. **313 members consume exactly; one original
DBA has a 606-byte trailer.** Do not report the corpus as entirely free of
size anomalies.
All integers are little-endian. File offsets refer to the VOL payload,
excluding its nine-byte prefix and repeated-byte suffix ([vol.md](vol.md)).
Executable addresses are VAs at image base `0x00400000`.

## Evidence and scope

An independent verifier reads all 16 CD VOLN archives, including the three
voice archives under `VER31`; there are 13 under `VOL`. It matched archive and
inspected-member hashes against
[iso_file_hashes.sha256](../../data/iso_file_hashes.sha256).
The archive hash list is described in [vol.md](vol.md#cd-archive-hashes).
Decoded bitmaps are decoded original artwork and are never committed.

**Binary identities**: `VER95/VSHELL.EXE` and `VER95/DBSIM.EXE`,
also recorded in [reference_version.md](../reference_version.md).

Addresses inside
jump tables must not be interpreted as instructions merely because objdump
prints them. The separately decoded block starting at `0x45a1c3` resolves
one such boundary in the bitmap blitter.

## DBM record

**On every declared bitmap:** for a record starting at `B`, let
`S` be its body size and `P` its stored pixel-data length.

| Relative offset | Width | Meaning / observed value |
| --- | --- | --- |
| `B + 0x00` | 4 | Tag `0e 00 28 00`. |
| `B + 0x04` | 4 | Body size `S`, excluding these first eight bytes and final alignment. |
| `B + 0x08` | 2 | Height in rows. |
| `B + 0x0a` | 2 | Width in pixels, also the raw row stride in bytes here. |
| `B + 0x0c` | 1 | Bits per pixel; always 8. |
| `B + 0x0d` | 1 | Bitmap flags; always zero here. Low nibble selects a layout; bit `0x10` selects opaque copying in the examined blitter. |
| `B + 0x0e` | 1 | Compression kind; always zero here. Original dispatch also handles 1 and 3. |
| `B + 0x0f` | 4 | `P`, stored pixel-data bytes; always `width * height` here. |
| `B + 0x13` | 2 | Embedded colour count `C`; always zero here. |
| `B + 0x15` | `P` | Stored pixels/stream. For kind zero, one palette index per byte. |
| `B + 0x15 + P` | `4*C` | Optional embedded four-byte colours; absent from this corpus. |
| `B + 8 + S` | `S & 1` | Zero alignment byte if the body size is odd. |

**Reader:** VSHELL `0x453ef8` reads the two dimensions, then three
separate bytes, then the four-byte data length and two-byte colour count.
In particular, `0x453f63–0x453f75` widens the single compression byte into
the in-memory word at object `+0x12`. `0x453fc7–0x453ffa` reads `4*C`
embedded colour bytes after the pixel data. `0x453ec8–0x453ede` computes
`S = 13 + P + 4*C`. The optional-colour layout is static;
its nonzero case has no DBM/DBA/HBA example on the CD.

**Example:** `SHELL0:DBM\BAY2.DBM`, VOL payload offset `0x554a87`,
length 307,222.
Its 21-byte header is:

`0e 00 28 00 0d b0 04 00 e0 01 80 02 08 00 00 00 b0 04 00 00 00`.

This gives height 480, width 640, `P = 307200`, `S = 307213`, then one
zero alignment byte. That byte is inside the VOL payload and separate
from the VOL suffix. All ten standalone DBMs are 640 by 480 with this layout.

## Rows and transparency

Pixels are successive left-to-right rows, top row first for
the unflipped draw path. There is no per-row prefix, offset table or padding.
The source-address calculation at VSHELL `0x45a1c3–0x45a1d0` is
`pixels + y * width + x`; row advancement at `0x45a32d` uses the remaining
width after the drawn span. Height and width participate in the respective
vertical/horizontal clipping checks at `0x45a0ee–0x45a127`.
Of 3,453 declared bitmaps, 350 have widths not divisible by four, yet their
pixel lengths are still exactly `width * height`. Record alignment is not
scanline alignment. Observed dimensions range from 1 to 640 wide and 1 to
480 high; these are corpus limits, not proven format maxima.

**In the examined blitter:** file flag bit `0x10` becomes bit
`0x10` of the drawing descriptor at `+5`. VSHELL `0x45a1f5` tests it.
When clear, `0x45a320–0x45a326` skips index zero and copies other indices;
when set, `0x45a2e3–0x45a2f5` copies all indices. Thus index zero is a colour
key in the zero-flag path, not palette alpha. All audited records store
zero flags. Runtime callers can change flags and request flipped draws;
the on-disk value alone does not establish every final screen composite.
The blitter `0x45a09c` takes a mirror argument (`& 3`, jump table
`0x45a1b3`): 1 vertical flip, 2 horizontal mirror, 3 both; the
source-address formula above (`0x45a1c3`) is case 0. The opaque flag `0x10`
and the index-0 skip apply in all four cases (see [images and palettes on the frame](#images-and-palettes-on-the-frame)).

**Unverified variants:** low-nibble layout 1 is dispatched separately at
`0x458f40–0x458f50`. No requested CD image uses it. Other depths, layouts
and embedded palettes are not validated by the scratch decoder.

## DBA and HBA

DBA and HBA share the following serialized format in all
304 array members. HBA is not a different compression scheme.

| Offset | Width | Meaning |
| --- | --- | --- |
| `0x00` | 4 | Tag `01 00 28 00`. |
| `0x04` | 4 | Array body length, including count and aligned child records. |
| `0x08` | 4 | Number of bitmaps `N`. This is one 32-bit count, not a row/column pair. |
| `0x0c` | variable | `N` complete DBM records in sequence. |

There is **no serialized per-bitmap offset table**. Derive offsets:

`B[0] = 12; B[i+1] = B[i] + 8 + S[i] + (S[i] & 1)`.

The same rule includes the final child's alignment. All child padding
bytes are zero. Array counts and child bounds agree in every member;
the declared array end is `8 + array_body_length`. VSHELL
`0x454c28–0x454c32` reads the 32-bit count, allocates an in-memory pointer
array, then `0x454c51–0x454c66` invokes the general object reader `N` times.
Pointers in that runtime array must not be mistaken for file offsets.

**Example:** `SHELL0:DBA\APOC_OUT.DBA`, payload `0xc2d`, length
55,632,
contains 58 images. Its first DBM at `0x0c` is 24 by 23, with `S = 565`;
the next starts at `0x24a`, including the zero alignment byte.

**Size comparison / hypothesis about naming:** `SIMVOL0`
`dba\LD_HERC1.DBA` starts with a 320 by 240 bitmap, whereas
`hba\LD_HERC1.HBA` starts with 640 by 480. Higher-resolution usage is
plausible, but this does not prove a universal doubling rule or runtime
resolution-selection policy.

## Compression in the original

**Static dispatch, not a claim of compressed CD images:** the draw
routine `0x4537e8` calls `0x453a50` only when the bitmap object's compression
word at `+0x12` is nonzero (`0x4537fa–0x453802`). The dispatcher reads a
little-endian uncompressed byte count `U` from the start of stored data at
`0x453a65–0x453a68`, allocates that much output, and creates an input stream
over the remaining `P - 4` bytes at `0x453a98–0x453aaa`.

| Kind | Original behavior |
| --- | --- |
| 0 | Direct stored bytes; no `U` prefix. The path used by all audited images. |
| 1 | RLE reader constructed at `0x453ad9` via `0x44f5d8`; read method `0x44f79c`. |
| 3 | LZSS plus adaptive Huffman reader constructed at `0x453b1d` via `0x44f908`; read method `0x44fb48`. |
| Other nonzero | The examined dispatcher returns zero through `0x453b3d–0x453b60`. |

At `0x453b62–0x453b6a`, the selected stream is asked for exactly `U` output
bytes. The RTTI names `RLERStream` and `LZHRStream` occur at `0x453c8c`
and `0x453c38`; the vtables at `0x47a7a8` and `0x47a800` point to the read
methods above. This identifies two algorithms, not a single universal DBA
decompressor.

**RLE byte grammar:** read control byte `c`. If bit 7 is clear,
copy the following `c` bytes literally. If bit 7 is set, read one value
byte and emit it `c & 0x7f` times. Counts have no plus-one bias. A zero
count makes no output; it is not an end marker. Runs operate on the byte
stream and need not end at row boundaries. Stop after the requested output
length. Evidence: control read `0x44f848–0x44f855`, high-bit test/mask and
repeat-value read `0x44f86d–0x44f884`, literal count `0x44f8a7–0x44f8ad`,
repeat emission `0x44f7cc–0x44f7e4`, literal copy `0x44f7e8–0x44f83a`.

**Kind-3 algorithm properties:** a 4,096-byte history ring, with
initial write position 4,036 and the first 4,036 bytes filled with `0x20`
(`0x44fa26–0x44fa37`). A Huffman-decoded symbol below 256 is literal;
symbols 256–313 represent match lengths 3–60 (`symbol - 253`). A decoded
distance `d` selects `(write_position - d - 1) & 0xfff`; overlapping copies
update the ring as bytes are emitted (`0x44fb76–0x44fc2c`).
The Huffman initializer builds 314 unit-frequency leaves at
`0x44fd96–0x44fdfb`; `0x44feb4` decodes and updates the tree.
`0x4501f8–0x450204` rebuilds at root frequency `0x8000`.
Bits are consumed most-significant first (`0x44ff88–0x44ffdf`);
distance decoding uses tables at `0x47a89c` and `0x47a99c` via `0x44ff04`.
This establishes an LZHUF-style adaptive-Huffman/LZSS algorithm.

`0x453a50` runs on every draw of a compressed bitmap and frees its buffer
afterwards; a preset-buffer mode (`[0x48e268]`) that returns 0 without
decompressing exists, but its setter `0x453a38` has no caller
([vshell_presentation.md](../reverse-engineering/vshell_presentation.md)).

**Open:** bit-for-bit compatibility with any named external LZH implementation,
complete distance-table specification, compressed-stream trailing-bit rules,
and malformed-stream behavior. No compressed member was found in the requested
corpus, so no corpus or runtime validation of either compressed mode is claimed.
The scratch verifier deliberately accepts only the raw mode.

**Against an inference from the crash anchor:**
`SHELL0:DBA\TERRADEF.DBA` contains one raw 260 by 230 image; its compression
byte at payload offset `0x1a` is zero. Payload offset in VOL is `0x25fc89`,
length 59,834.
The load at `0x4445c3` and later stale-pointer call to `0x453a50` documented
in [media_compatibility.md](../reference-game/media_compatibility.md#mission-map-lifetime-guard) do not imply
that this on-disk asset is compressed.

## Verification results and the CORNERS exception

**Totals**, counting archive members rather than unique resources:

| Archive | DBM members / images | DBA members / images | HBA members / images |
| --- | ---: | ---: | ---: |
| `VOL/SHELL0.VOL` | 10 / 10 | 83 / 1,275 | 0 / 0 |
| `VOL/SIMALERT.VOL` | 0 / 0 | 5 / 40 | 5 / 40 |
| `VOL/SIMPATCH.VOL` | 0 / 0 | 0 / 0 | 1 / 1 |
| `VOL/SIMVOL0.VOL` | 0 / 0 | 126 / 1,722 | 45 / 326 |
| `VOL/ZONES.VOL` | 0 / 0 | 39 / 39 | 0 / 0 |
| Other 11 VOLN archives | 0 / 0 | 0 / 0 | 0 / 0 |
| **Total** | **10 / 10** | **253 / 3,076** | **51 / 367** |

All declared images have matching dimensions/data sizes, valid bounded rows,
zero compression, zero embedded colours, and no out-of-bounds reads or writes
in the verifier. They produce **25,699,737 pixel bytes**. The 3,360 alignment
bytes were separately checked. Exact member consumption passes for 10 DBM,
252 DBA and 51 HBA members. All 65 DPL members pass ([dpl.md](dpl.md)).

**Exception:** `SIMVOL0:dba\CORNERS.DBA`, VOL payload `0x10e24cd`,
length 2,330.
Its array header declares 24 children and a body of 1,716 bytes. The 24
children and their padding end exactly at `0x6bc` (1,724). The remaining
606 bytes are outside that declared object, inside the VOL payload.

The trailer has complete raw DBM records at `0x768`, `0x81e`, `0x8a4`,
respectively 16 by 10, 16 by 6, 16 by 6. Those three were also decoded
and checked, but are **not** added to the declared array count.
Ranges `0x6bc–0x768` (172 bytes) and `0x894–0x8a4` (16 bytes) remain
uninterpreted. **Hypothesis:** residue of an older/partly overwritten image
array. Its origin and runtime treatment are not proven. Consequently the
requested “all data consumed, all sizes consistent” condition cannot be
truthfully asserted for the whole CD corpus. Preserve/report this anomaly;
do not silently extend the array count or discard its trailer.

## Palette selection

No audited bitmap has an embedded palette or palette filename.

**Static, VSHELL; [vshell_presentation.md](../reverse-engineering/vshell_presentation.md)):**
VSHELL names DPL files only through the 20-entry table `0x46de14` (loader
`0x407b62`, installer `0x407c1b`). `PALETTE`, `ARMING`, `CAM_*`, `BR_*` and
`DB_*` are shell palettes (service bay screens and the mission pages,
`0x43b7f2`); `BAY`, `MAP`, `MAP_PAL1` and `MAP_PAL2` are never named by
VSHELL, so no shell image is drawn with them. The mission screen's zone
images `ALPH2`, `DELT1`, `OMIC1`, `BRAV1.DBA` are drawn with `ALPH`, `DELT`,
`OMIC`, `BRAV.DPL` (`0x44472f`). Per-scene selection: [images and palettes on the frame](#images-and-palettes-on-the-frame).

The simulator bindings below are **hypotheses**, based on names, resource
placement and limited previews, not proven loader-to-draw dataflow.

| Images | Plausible palette source / limitation |
| --- | --- |
| `SIMVOL0:dba\LD_HERC*`, `LD_FLYR*` and HBA counterparts | Same-stem `SIMVOL0:dpl\LD_*.DPL`. The LD_HERC1 HBA preview produces readable upright “LOADING / STAND BY” artwork. |
| Simulator cockpit/HUD images, `SIMALERT` DBA/HBA, patch HBA | `SIMVOL0:dpl\COCKPIT.DPL` / `IMPACTCP.DPL` are candidates. Alert/patch archives contain no DPL of their own. |
| Simulator world/material imagery | Current `WORLD0..9.DPL` / `IMPACT0..9.DPL` are candidates; the mission/world selection and image bindings remain untraced. |

Scratch previews of BAY2, TERRADEF and LD_HERC1 confirm recognizable row
ordering and geometry; visual plausibility is not proof of palette
selection or of the original final composite. Previews use RGB times four and show every
index as a colour; they do not emulate platform palette overrides or blending.

## Appendix: DFN/HFN scope

**Family relationship:** all 36 DFN and 26 HFN members have tag
`05 00 28 00`, and each body's declared size equals its member length minus
eight. VSHELL's registration table at `0x47bda8` associates this tag with
the `GLFont` factory `0x454578`; the font tag accessor is `0x454610`.
They are separate font objects, not DBA arrays of tagged DBMs. The layout,
glyph metrics, character mapping and VSHELL's use of the fonts are in
[dfn.md](dfn.md). The registration table's entry for tag `03 00 28 00`
("Old GLFont") points to `0x454608`, which returns no object.

## Evidence and open points

The original readers and corpus checks establish the separate
flag/compression bytes, embedded colour count, 32-bit array count and
alignment rules above.

The verifier reports one strict-consumption failure (CORNERS). Open: the
CORNERS trailer, compressed-mode oracle cases, unused bitmap variants and the
simulator palette bindings.

## Images and palettes on the frame

Code: `shell/include/es2/shell/draw.h`,
`shell/src/gui/draw.c` (library `//shell:draw`, no Win32, no file I/O, no
allocation); decoding stays in `formats/src/dbm.c` (`es2_dbm_decode`, kinds 0
and 1). Tests: `//shell:draw_test` (synthetic), `//formats:dbm_real_test`
(SHELL0 counts), `//shell:draw_real_test` (in vitro against the original).
Read from the llvm-objdump listing of the reference `VSHELL.EXE`; the blitter and the RLE path are additionally
**in vitro** (below). Everything else here is a static reading.

**Which kinds the shell's images use (on the CD).**
`//formats:dbm_real_test` parses every DBA and DBM member of `VOL/SHELL0.VOL`
strictly and decodes every image: 83 DBA members with 1,275 images and 10
DBMs, 1,285 images, all compression kind 0, flags 0, depth 8, no embedded
colours, no DBA trailer, every record's stored bytes consumed exactly
(decoded-pixel FNV-1a 64 `5bf7bb2e3d5513c9`). So the decompressor `0x453a50`
never runs for a CD image of the shell. The library decodes kind 0 (borrowed
stored bytes) and kind 1 (RLE, the grammar above; equal to the original in
vitro). **Kind 3 (LZH) is not implemented** and refused
(`ES2_DRAW_UNSUPPORTED`): no shell image uses it, and its distance tables
(`0x47a89c`, `0x47a99c`) and end-of-stream behaviour are not specified here;
the `unpack` harness op below can verify an implementation against the
original. Layout nibble 1 (`0x459508`) and depths other
than 8 are refused as well.

**The blitter (`es2_draw_blit`, original `0x45a09c(B, &pt, clip, mirror)`,
cdecl, reads `[0x47c4a6]` width, `[0x47c4aa]` height and the row table
`[0x47c482]`: entry 0 = pixel base, entry 1 + y = row offset).** With a clip
rectangle (l, t, r, b, inclusive, signed compares): nothing when x > r, y > b,
x + w ≤ l or y + h ≤ t (checked in this order, `0x45a0b7..0x45a127`); without
one the bounds are [0, W) × [0, H) (`0x45a12c..0x45a19c`). The drawn span is
x0 = max(x, l) … min(x + w, r + 1) − 1, likewise in y. Mirror `& 3` (jump table
`0x45a1b3` = `0x45a1c3`, `0x45a205`, `0x45a255`, `0x45a2a1`): drawn image
position (c, r) reads source column c (w − 1 − c with bit 1) of source row r
(h − 1 − r with bit 0); the listing's start pointers and steps per mode give
exactly this mapping (mode 1 writes destination rows bottom-up, mode 2 reads
source rows bottom-up; the order does not change the bytes). Index 0 is
skipped unless flag `0x10` (`0x45a1f5`, all four modes). Other flag and
mirror bits are ignored.

**Deviations of the library (documented, platform/safety only):**

- A clip rectangle with l > r or t > b, or not inside the frame, is refused
  (`ES2_DRAW_BAD_CLIP`). The original has no such check: an inverted clip
  gives a negative span count (`rep movsd`/`dec ecx` with a huge count, a
  fault), a clip beyond the frame writes outside it (row offsets continue
  linearly). No VSHELL caller is known to pass either.
- A bitmap of width or height 0 draws nothing (`ES2_DRAW_EMPTY`); in the
  original a zero span count makes the byte loops of the keyed and mirrored
  modes run 2^32 times.
- Coordinates are computed in 64 bits; the original's 32-bit `x + w` would
  wrap only within 65,535 of `INT32_MAX`.
- A compressed record whose `U` differs from width × height is refused
  (`ES2_DRAW_BAD_IMAGE`, `es2_dbm_decode`); the original draws width × height
  bytes from a `U`-byte buffer.

**The DPL conversion (`es2_palette_convert`, original `0x407236..0x4072a1`).**
For i = 0..255 the original reads record i (`[P+8] + 4i`, no check against
the colour count): flags bit 0 → each component `<< 2` (32-bit shift, low
byte stored: a component above 63 wraps), bit 1 only → the components
unchanged, neither → **not written**. The buffer is blue, green, red, (fourth
byte never written) and goes unchanged into the DIB table. The library
returns the table as 256 RGB triples (the frame captures' `palette_sha256`
form); entries
that are not written (neither flag bit, or i ≥ the DPL's colour count) keep the
value the caller's table had (**deviation**: the original leaves
uninitialised stack, which may hold the previous install's values). All 20
DPLs of the table have 256 colours, all with flags bit 0 (counted by
`draw_real_test`), so on CD data the deviation is never reached.

**Palette per scene.** `es2_palette_file(n)` is the table `0x46de14` (names
at `0x46de98..`); `es2_palette_for_bay_screen(word, page, group)` is
`0x43b7f2` (jump table `0x43b80a`: words 0, 1, 8 and anything above 8 as an
unsigned value select nothing; 2, 4, 5, 6 → 2; 3 → 1; 7: page 0 → 3, or 4 when
the 16-bit `group − 1` ≥ 4; page 1 → `group − 1 + 5`; page 4 → `group − 1 +
10`; pages 2, 3 nothing; the index travels as a 16-bit word through
`0x43a430` into the bay background screen's `+0x45`). The other bindings
(start-up and main menu 1, mission screen `4 + group`, zone map `14 + g`) are
in [vshell_presentation.md](../reverse-engineering/vshell_presentation.md)
and belong to the screens that make them.

**The animated main-menu background.** `es2_menu_background_file(0..5)` =
`dbm\bay2a_80`, `_81`, `_82`, `_83`, `_84`, `_84` (`0x430fc4`, strings
`0x4736e5..0x47373a`; each loaded separately), period 500 ms;
`es2_menu_background_next` is `0x40cf31..0x40cf49`: index + 1, 0 when it
reaches the count, unchanged while the window's `+0x11` bit 1 is set (then
nothing is drawn either). All six are 640 × 480 kind 0 on the CD; the zone
images `dba\alph2`, `delt1`, `omic1`, `brav1` are one-image DBAs in SHELL0.

**In vitro.** The memory worker's slot mode has two ops
(`tools/reference-game/shell-invitro/main.c`, `s26_op`): `blit` runs the
original `0x45a09c` on every record of the memory-disk file `blit.in` into a
memory frame of the record's size (the three globals point at it for the
call), `unpack` runs `0x453a50` on a stand-in bitmap object and frees the
buffer with `0x408760` as `0x4537e8` does. `//shell:draw_real_test` compares
them byte for byte with `draw_tool` (our library): 4,000 seeded synthetic
blits (frames up to 96 × 72, bitmaps up to 80 × 80, all mirror modes incl.
extra bits, keyed and opaque, no clip / clip inside the frame incl. single
rows and columns, positions over every edge; about 82 % draw), 37 SHELL0
images into 640 × 480 frames, and 200 seeded kind-1 streams (zero counts,
runs and literals cut at U, trailing bytes, U up to 60,000): all equal.
Four mutations (row mirror bit, inclusive right edge, index-0 key, RLE count
mask) are caught.

**Open.** A pixel comparison of whole screens (which compose these
primitives) against captured frames of the original;
the DPL install `0x4071fe` itself in vitro (it needs GDI; its arithmetic is
four instructions per component and covered by the static reading only);
kind 3; the scaled and polygon draws `0x4591b8`, `0x4592a8` and the reverse
copy `0x45a35e` (not needed for the shell's plain bitmaps).
