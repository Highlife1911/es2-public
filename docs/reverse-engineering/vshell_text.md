# VSHELL text drawing

What decides the text pixels in the reference `VER95\VSHELL.EXE` (SHA-256
`1534aa8f…a16e`, image base `0x00400000`): the font primitives, the label,
the button texts, the edit field and the report (text page) layout, with the
own code that implements them. Every address was read in the llvm-objdump
listing. Markers: (C) read from the listing, (T) confirmed in vitro by
`//shell:text_real_test` (last section). The font format, the corpus and which
font each screen loads are in [dfn.md](../formats/dfn.md); the frame, the
blitter and the redraw model in [vshell_presentation.md](vshell_presentation.md);
widget classes, their state words and rectangles in
[vshell_widgets.md](vshell_widgets.md).

Coordinates: a widget's rectangle is the four dwords at `+0x25` (l), `+0x29`
(t), `+0x2d` (r), `+0x31` (b); below, `w = r − l` and `h = b − t`. Draw handlers
start with `0x41fbd9`, which makes the display's drawing context current with
the widget's rectangle as clip rectangle (clip mode 1) and offset mode 1, so
every coordinate here is relative to the widget's own top-left corner.

The shell's four DFN fonts are `MAP`, `BLACK`, `FONT` and `FONT2`; `GREEN6X8`
and `TITLE` are DBSIM fonts (`SIMVOL0`, `SIMPATCH`), not shell members.

## The drawing context's text fields

The current drawing context is `[0x48e264]` (`[display+0x15]`, size `0x239`;
the viewport `[0x47c444]` is the context `+4`). Text uses:

| Field | Set by | Meaning |
|---|---|---|
| `+0x228` | label draw `0x40bafa` | pointer to the pen {kind dword, colour dword}: read by the line draw `0x455624` (as viewport `+0x224`) for the underline |
| `+0x22c` | label, edit field | pointer to the brush of the rectangle fill `0x4576a4` (viewport `+0x228`): kind 0 = solid colour (driver slot 2 `0x45abfc`), kind 6 = 256-byte translation table applied to every pixel of the rectangle (driver slot 3 `0x45ac84`), kind 4 = colour through a ramp table (not used by text) |
| `+0x230` | label draw (`+0xb9`) | underline byte |
| `+0x231` | label draw (`+0x45`), edit draw (0) | alignment dword: 0 left, 1 right, 2 centre |
| `+0x235` | label draw (`w`), edit draw (`w + 1`) | width for the alignment |

The context's constructor (`0x452da0`, tail `0x452e71..0x452e82`) sets the
last three to 0. The edit field does not set `+0x228`/`+0x230`, so it inherits
the last label's values; every one of the 147 label constructions passes
underline 0 and nothing else writes `+0xb9`, so the underline is never on in
VSHELL.

## Font primitives (`src32\GL_FONT.CPP`)

| Address | Function | Contract |
|---|---|---|
| `0x4010fd(font, c)` | getWidth | c is taken as a byte (`0x40110b..0x401111`); in range `F ≤ c < F + N`: the width byte of glyph `c − F`; out of range: severity-3 log and the same table read outside its bounds (dfn.md). A fixed-width font answers every byte, in range or not (`0x401186..0x4011a6`: the range test only logs); the CD fonts are all proportional (C) |
| `0x4011af(font, s)` | string width | sum of getWidth over the NUL-terminated string |
| `0x4011de`, `0x4011ea(font, s)` | height | the font height (`+0x0a` of the object); the string argument of `0x4011ea` is unused |
| `0x4011f6(font, &pt, c)` | glyph draw | fatal if the font is compressed; out-of-range c → severity-2 log and `?`; builds a bitmap header {H, width, depth `+0x12`, flags, `w × H`, data + offset} and draws it with `0x458ee4` at `(pt.x, pt.y − B)`; when the underline byte is set, draws a line `(pt.x, pt.y)`–`(pt.x + width, pt.y)` with the pen (`0x401309..0x40134b`) |
| `0x401355(font, &pt, s)` | string draw | fatal if compressed; x := `pt.x`, plus `width − textwidth` when alignment is 1, plus `(width − textwidth) >> 1` (arithmetic shift) when 2 (`0x4013ac..0x401404`); then for each byte: glyph draw at (x, `pt.y`), `x += getWidth(byte)` |
| `0x401438`, `0x4014a6`, `0x4014e3` | realize, colour table, remapColors | no caller and no pointer in the image: dead in VSHELL |

`pt.y` is the **pen (baseline) row**: the glyph's top row lands at `pt.y − B`
(B = 10 for FONT2/BLACK). The glyph header's flags byte has bits 0..4 cleared
(layout 0, bit `0x10` clear → index 0 skipped); bits 5..7 are an uninitialised
stack byte (dfn.md, open). The glyph goes through the same viewport, clip
rectangle and blitter as images: pixels outside the widget's rectangle are
clipped (inclusive clip rectangle, vshell_presentation.md). There is no
shadow: the glyphs carry one ink index (dfn.md) and no traced path draws a
string twice. Byte b selects glyph `b − 32`, as stored (DOS code page); advance
= glyph width, no kerning.

**Zero-width glyphs crash the blitter.** SHELL1's `FONT2` and `BLACK` have
glyphs of width 0 for 249, 250, 251, 253 and 254. The blitter skips a bitmap
only when `x + w ≤ l`; with w = 0 and x > l the span is 0 and the row loop
(`0x45a31d`: `dec ecx; jne`) runs 2^32 times. In vitro the worker faulted at
`0x45a320` in an edit draw of such a glyph (T). None of the 4,056 ESTEXT draws
reaches the case. The own glyph draw draws nothing for a zero-width glyph
(deviation).

**Bytes without a glyph** (out of `[F, F + N)`): VSHELL draws `?` and advances
by a width read outside its table (dfn.md). Not reachable in vitro: the worker
stops at the logging assertion `0x44e560`. ESTEXT of all three languages has no
such byte. Own choice: the advance of `?` (0 if the font has no `?`); a
fixed-width font keeps its fixed width as VSHELL does.

## Colour

The font primitives take no colour. Glyphs are drawn with their stored ink
(FONT2: index 41, BLACK: index 16), and the label and the edit field then
**recolour index 41** in their rectangle: a kind-6 brush whose table is the
identity except `table[0x29] = colour` (`0x40bb79..0x40bc3b`,
`0x40c856..0x40c91b`). This happens after the text is drawn and whether or not
there is text, and it also turns any other index-41 pixel inside the rectangle
(the background image under a transparent label) into the colour. Colour 41 is
therefore "as stored". Colours passed at the 147 label constructor sites: 41 at
91 (among them every button text), 26 at 26, 39 at 12, 23 at 12, 40 at 6 (the
text pages' line labels `0x40d37d` and five labels with a background in the
practice screen, `0x44b962..0x44bb9a`); the 143 `setText` sites (`0x40b8bb`)
can change them. (Argument values from a scan of the pushes before each call in
the listing.) The font's own remap `0x4014e3` is never called, and `0x4537a4`
is not the font colour remap.

## Rectangle fills (`0x4576a4`)

Add the offset, intersect with the clip rectangle (`0x457914`: nothing when
empty) and fill inclusively (C, T). The fills of the label, the edit field and
the report are normalised (min/max) except the edit caret (below). The kind-6
table is the identity except entry 41; solid fills use driver slot 2
`0x45abfc`, translation fills slot 3 `0x45ac84`.

## Label (`0x40b828`, `0xc9` bytes)

Constructor `0x40b828(this, parent, rect, font, ?, text, align, colour,
background, underline)`: font `+0xb1`, align `+0x45`, colour `+0xb5`,
background flag `+0xc1`, background colour `+0xc5` := 16, underline `+0xb9`,
then `setText`. `setText` `0x40b8bb(this, text, align, colour, redraw)` stores
align and colour, copies the text into `+0x4d` (null or empty → empty text); a
text longer than 100 bytes is a fatal assertion, and exactly 100 bytes write
the terminating NUL over the low byte of the font pointer `+0xb1` (quirk; the
own code refuses 100 bytes like longer texts and does not reproduce the
pointer damage). Redraw ≠ 0 draws at once with y = −1.

Draw `0x40b9e9(this, y)` (nothing when bit `0x02` of byte `+0x11` is set):

1. If `+0xc1`: solid fill, colour `+0xc5`, rectangle (0, 1)–(w − 1, h − 1).
2. If the text is not empty and a font is set: context width w, alignment
   `+0x45`, underline `+0xb9`, pen colour `+0xbd`; the pen point is
   (0, y − 1) when y ≠ −1, else (0, (h + 1) − ((h + 1 − H) / 2) − 3) with H the
   font height and `/` C division (`0x40bb14..0x40bb41`); string draw.
3. Always: recolour index 41 → `+0xb5` over (0, 1)–(w, h − 1).

## Buttons with text (`0x40a004`, `0x40a2c4`)

Both create a child label (`0x40a086`: rectangle (1, 0)–(w, h) of the button;
`0x40a341`: (0, 0)–(w, h)) with the button's font, align 2 (centre), colour 41,
no background. Their draw calls the label draw with
y = `(H + h + 1) / 2 + 1` when both state words `+0x45` and `+0x49` are set,
else `(H + h + 1) / 2 − 1` (`0x40a240..0x40a2b3`, `0x40a631..0x40a6a4`; h of
the button). So the pen row is `(H + h + 1)/2` (pressed) or
`(H + h + 1)/2 − 2`, i.e. a pressed button's text is two rows lower. The
service bay's button bar uses class `0x40a2c4` with BLACK. Its subclass
`0x40a6b0` (vtable `0x46ead8`) draws the same way (`0x40a86a..0x40a8b4`). Four
more labels are built at `0x40a925..0x40aa4b` (texts and alignments from the
caller, colour 41, no background), one more at `0x40b1a1` (centre, colour 41),
and `0x40ac70..0x40ac97` redraws four labels (`+0x55`, `+0x59`, `+0x5d`,
`+0x61`) with y = −1; the classes (including the button class `0x40a8c0`) are in
vshell_widgets.md. The meaning of
`+0x45`/`+0x49` (pressed) is also there. The button pen rows are confirmed
from the listing only (the label draw they call is in vitro).

## Edit field (`0x40c1a4`, `0xc3` bytes)

Constructor `0x40c1a4(this, parent, rect, font, …)`: font `+0xa3`, empty text
`+0x45` (90 bytes), accepted characters `+0x9f` := `0x46ee48` (`A`–`Z` and
space; null accepts everything, `0x40c237`), text width `+0xaf` := 0, caret
blink `+0xb3` := 1, protected prefix length `+0xb7` := 0, colour `+0xbb` := 41.
All four edit fields of VSHELL use FONT2 `[0x46de00]`: Saved games (`0x438f68`,
one per slot), the name-entry and registration screens (`0x43bac8`, `0x43c02c`)
and the DEBUG mission-file dialog (`0x44dfb2`).

- **setText** `0x40c294`: fatal unless the length is below 90; length `+0xab`
  := strlen, width `+0xaf` := string width.
- **Character-set test** `0x40c237`: compares the zero-extended 16-bit
  character word with each **sign-extended** set byte up to and including the
  terminator: 0 always passes, a word above 255 never matches a set, and a set
  byte ≥ 0x80 could never match (no VSHELL set has one). The null set accepts
  everything; the stored byte is the low byte (C, T).
- **Insert** `0x40c382(this, c)` (the width rule): accepted only if c passes
  the filter, `length + 1 < 90` and `width + getWidth(c) + 6 < w`
  (`0x40c39b..0x40c3c7`, signed compares), tested in that order, so a character
  outside the set is refused before its advance is read; then
  width += getWidth(c), append, redraw. The visible limit is therefore a pixel
  limit that depends on the characters typed and on the field's rectangle, not
  a character count.
- **Backspace** `0x40c406`: only while `length > +0xb7`; width −= getWidth.
- **Draw** `0x40c6ff`: solid fill colour 16 over (0, 0)–(w, h); context width
  `w + 1`, alignment 0, clip mode 1 (`+0x208` := 1); pen point
  (1, H/2 + (h + 1)/2) (C division, `0x40c7cb..0x40c7ec`); string draw; if
  `+0xa7` (focus, vshell_widgets.md) and `+0xb3`: solid fill colour 39 over
  (width, y − 2)–(width + 6, y), the caret right after the text (the text
  starts at x = 1, the caret at x = width); recolour 41 → `+0xbb` over
  (1, 1)–(w − 1, h − 1). The caret rectangle is passed without the min/max
  normalisation of the other rectangles and reuses the fill's solid brush
  record with its colour set to 39 (`0x40c824`) (C, T).

**Saved games row.** The name rectangle is l = 12 for slots 0 and 9, else 10,
r = parent width − 1 (`0x438ec7..0x438f23`), i.e. r − l = 325 for slots 0 and 9
and 327 otherwise. With FONT2 the narrowest character of the slot set is `i`
(advance 3); a 327-pixel row takes 85 of them, so the row reaches the length
limit (89 bytes) before the pixel limit, and the strcpy at `0x40e6ea` inside
`0x40e6cd` would write 90 bytes into the 80-byte text of the slot record (the
flag and nine bytes of the next record's file name). The own code refuses a row
of 80 bytes or more (below).

## Text pages (`src\ESREPORT.CPP`, `0x40d1bc..0x40d834`)

A report object (`0x36` bytes) lays a text out as one label per line and shows
one page at a time. Fields: `+0` parent, `+4..+0x13` rectangle (l, t, r, b),
`+0x14` font, `+0x18` visible, `+0x1a` text copy, `+0x1e` current page, `+0x20`
page count, `+0x22` lines per page, `+0x24` line count, `+0x26` line height,
`+0x28` width of `M`, `+0x2a` line pointers, `+0x2e` row rectangles, `+0x32`
line labels. The mission screen builds five, all with FONT2 `[0x46de00]` and
parent `[0x48d8bc]` (`0x443686..0x44383b`): `[0x48d8fc]` rectangle
(10, 21)–(613, 168) for the page-0 campaign text, and `[0x48d900]`,
`[0x48d904]`, `[0x48d908]`, `[0x48d90c]` with (10, 21)–(575, 115) for briefing,
debriefing, objectives and intelligence (vshell_ui.md §4.8).

`setText` `0x40d231(report, text)`:

1. Free the previous layout (`0x40d3db`), copy the text.
2. Line height := font height; `+0x28` := string width of `"M"`.
3. Layout `0x40d4e7`: fatal if `r − l` is 0; characters per line :=
   `(r − l) / width(M)`; line-array size := `strlen / that + count of '\n'`;
   lines per page := `(b − t) / line height`; row j of a page is the rectangle
   (l, t + j·H)–(r, t + j·H + H).
4. Line breaks `0x40d686` (below); line count := its result (overwriting the
   estimate).
5. Pages := `lines / perPage + ((lines + 1) ≠ perPage)` (`0x40d2d0..0x40d2f1`,
   as read; it is not a rounding-up: 11 lines on 11 rows give 2 pages, the
   second empty; 10 lines give 0 pages).
6. For every line i a label at row `i mod perPage`: the line's text, align 0,
   colour 40, background on (colour 16), underline off; hidden (`0x41fabd`).

Showing (`0x40d492`): line i is shown when the report is visible and
`i / perPage` is the current page, else hidden. Previous page `0x40d7ed` (when
current > 0), next page `0x40d80e` (when current < pages − 1).

**Line breaking `0x40d686`** works in place on the copy, with W = `r − l`:

- `lines[0]` := text; *last break* := text.
- For each byte p:
  - `'\n'`: p := NUL, last break := p, next line starts at p + 1.
  - another byte below `0x20` (signed compare, so `0x80..0xff` are not
    affected): replaced by a space, not treated as a break candidate.
  - a space: p := NUL temporarily; if the string width of the current line
    (up to p) is ≤ W, last break := p; otherwise the line is broken at the
    last break (its byte becomes NUL if it is a space), the next line starts
    after it, and a fatal assertion fires unless that next line, measured up
    to p, is narrower than W. Then p := space again.
- At the end: if the current line is wider than W, it is broken at the last
  break the same way, without the assertion.
- Result: number of lines.

Quirks that a reimplementation must keep or document: the last break is not
moved when a break happens, so a line that starts after a break and holds a
word plus a following word too wide for the line ends in the fatal assertion,
and the same case at the very end produces two line pointers to the same text
(the last line appears twice); a word wider than W is fatal; the line array is
sized from the `M` estimate and is not bounds-checked by the breaker. No CD
text reaches a fatal case, for every composition of variants the preparation
can produce (`tools/reference-game/scripts/domain_line_breaks.py`, FONT2
widths): the widest word is 111 px (campaign, W 603) and 177 px (mission
panels, W 565), the widest word pair 185 and 245 px. The longest line
measured is 97 bytes (`C1_09.ENG` debrief), near the 100-byte label limit.

## Own implementation

Code: `shell/src/gui/text.c`, `shell/include/es2/shell/gui/text.h`
(`es2/shell/gui/text.h`), `shell/src/slots.c`, `shell/src/menu.c`,
`shell/src/gui/widgets.c`.

| Own function | Original |
|---|---|
| `es2_shell_glyph_advance` | `0x4010fd` |
| `es2_shell_string_width` | `0x4011af` |
| `es2_shell_glyph_draw` | `0x4011f6` through the blitter: index 0 transparent, clipped to the widget rectangle and, beyond VSHELL, to the frame |
| `es2_shell_string_draw` | `0x401355` |
| `es2_shell_fill`, `es2_shell_recolour_ink` | `0x4576a4` kinds 0 and 6 |
| label | `0x40b828`, `0x40b8bb`, `0x40b9e9` |
| button texts | `0x40a004`, `0x40a2c4` |
| edit field | `0x40c1a4`, `0x40c294`, `0x40c382`, `0x40c406`, `0x40c6ff`; `es2_shell_edit_type` types a whole string |
| text pages | `es2_shell_report_draw` over `es2_shell_report_layout` (`texts.h`) |

Glyphs come from SHELL1's `FONT2.DFN` (all text but the bay bar) and
`BLACK.DFN` (bay bar) through `//formats:dfn` and VSHELL's VOL order, never
SHELL0's copies, `FONT.DFN` or `MAP.DFN`. The frame is the caller's:
coordinates are frame coordinates with the widget rectangle as VSHELL's
`+0x25..+0x31`; `shell/src/gui/widgets.c` draws labels, edit fields and text
panels into the shell frame through `es2_gui_frame_text_view`.

`es2_shell_slot_row_typed` (`slots.h`) applies the Saved games row's edit
field (r − l = 325 for slots 0 and 9, 327 otherwise, prefix `"%2d. "`, set
`0x475869`) instead of the 24-character fallback `ES2_SHELL_SLOT_NAME_MAX`. It
refuses a row of 80 bytes or more (`ES2_ERR_CAPACITY`), as
`es2_shell_slots_mark` refuses the text. The text menu applies it and the
registration field (`0x43c02c`: r − l = 206, set `0x476205`) with FONT2 of
`SHELL1.VOL` (from the game directory or menu.ini's `cd_root`); without that
file the fallback limits stay (documented deviation of a console without
fonts).

Deviations and choices: zero-width glyphs draw nothing; a byte without a glyph
advances by the width of `?`; a label text of 100 bytes is refused; the
underline (`+0x230`, the pen `+0x228`, the line draw `0x455624`) is not
implemented because every label passes 0.

## In-vitro acceptance

The worker's slot-mode op `text-draw` (`tools/reference-game/shell-invitro/main.c`)
registers the GL classes (`0x453048`, a static initializer the worker does not
run otherwise), loads the four fonts with VSHELL's `loadFont` `0x4088da`
(`dfn\font2`, `black`, `font`, `map`), binds a memory canvas as the screen (row
table `0x47c482`, stride `0x47c4a6`, driver table `0x47e69c` → `0x47e5dc`),
makes a zeroed context current with what `0x41fe84` sets (clip rectangle = the
widget's, clip and offset mode 1, origin 0; `0x41fbd9` patched to return) and
runs `0x401355`, `0x40b9e9`, `0x40c294`/`0x40c382`/`0x40c406`/`0x40c6ff` per
case. `//shell:text_real_test` compares the canvas hash and the edit state case
by case:

- ESTEXT of ENG, FRE and GER (342 entries each, 338 non-empty, every byte with
  a glyph in all four fonts) as labels in each font: 4,056 cases, all equal.
- A seeded sweep of 900 cases (string draws with every alignment and widths
  below and above the text, labels with y = −1 and explicit, backgrounds, any
  colour, rectangles down to 0 × 0, edit draws with and without the caret,
  typing with backspaces over the four sets including Saved games rows and the
  registration field typed past their limits): all equal.
- Mutations caught: 4 of 4 (label pen row, width rule, centre shift, recolour
  rectangle).

Listing ranges re-read for the in-vitro cases: `0x4010fd..0x401437`,
`0x40b9e9..0x40bc47`, `0x40a240..0x40a2c0`, `0x40c237..0x40c45e`,
`0x40c6ff..0x40c923`, the fill `0x4576a4..0x457972`, the driver fills
`0x45abfc`/`0x45ac84`, the blitter `0x45a09c..0x45a35d`, `0x41fe84`, `0x452da0`.

## Open

- Pixel comparison with captured frames of the original; the formulas are
  confirmed from the listing and in vitro only.
- The button texts' pen rows (`0x40a240..0x40a2b3`) are static only.
