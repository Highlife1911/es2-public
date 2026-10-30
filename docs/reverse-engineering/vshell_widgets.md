# VSHELL widget framework, input and screen geometry

Contract of the window framework of the reference `VER95/VSHELL.EXE`
([reference_version.md](../reference_version.md),
image base `0x400000`): window objects and their classes, the event model,
the input path from window messages to events, and the generated control
table of its 22 screens,
[`shell/data/vshell_layout.json`](../../shell/data/vshell_layout.json).
Sections 8 and 9 describe the own shell's implementation (`shell/gui`).

Related contracts: the screens' behaviour (handlers, which field a button
writes) in [vshell_ui.md](vshell_ui.md); WinMain, window procedure, pump and
focus gates in [vshell_process.md](vshell_process.md); drawing, palettes,
cursor and present sites in [vshell_presentation.md](vshell_presentation.md);
the text drawing (glyph primitives, the index-41 text colour, label, button
and edit texts, the edit width rule) in [vshell_text.md](vshell_text.md),
with the font format in [../formats/dfn.md](../formats/dfn.md).

**Method.** The `llvm-objdump -d -M intel` listing of the pinned
LLVM-MinGW (never committed), the Borland RTTI type descriptors in the
executable (class names and object sizes), and the generator
[`vshell_layout.py`](../../tools/reference-game/scripts/vshell_layout.py),
which interprets the 22 screen constructors instruction by instruction
(§4.1). **C** = static: read from the listing at the cited
addresses (and, for the table, reproduced by the generator's interpreter).
**V** = also equal to VSHELL's own code in vitro (`//shell:widgets_real_test`,
§9.4). **H** = hypothesis. Class names (`ESButtonFont`, `WinButton`, …) are
the original's RTTI names, read through the vtables (§2.2). Labels are
ESTEXT IDs; texts of the game are not quoted.

## 1. Overview

- Every screen is built once at start-up: `0x40180c` calls the 22 screen
  constructors at `0x401812..0x40187b`, in the order of the table's
  `screens` list; screens are then only shown and hidden (C; vshell_ui.md §1).
- A control is a window object: `operator new` (`0x4086b7`) with the
  object size, then a class constructor with `(this, parent, rect*, …)`.
  The constructors are plain cdecl functions; arguments are listed in §2.3.
- Windows form a tree under two run-time roots: the display's root window
  (`0x468660` returns `[[0x47f940]+5]`) and VSHELL's root `[0x481180]`, an
  `ESWindow` the start-up code builds with the display root's rectangle
  (`0x401f1f..0x401f89`, C). Screen frames hang from `[0x481180]`; the
  full-display shields of the modal boxes hang from the display root (§2.5).
- Rectangles are relative to the parent and inclusive (§2.1). Screen
  coordinates in the table assume the display root at (0,0) (**H**; the
  logical screen is 640×480, which the frames' rectangles
  `(0,0,639,479)` and `(0,30,639,479)` agree with).
- Input arrives as event objects in one queue (`[0x5ddcb0]`), drained once
  per main-loop step (`0x469ee4`); the target window's class decides what a
  mouse code means (§3).

## 2. Window objects

### 2.1 Base window (`Window`, 0x45 bytes)

Objects are byte-packed (Borland, no alignment); the vtable pointer is at
**+1**. Constructor chain: `0x409f20` (ESWindow) clears +0x11 and +0x39,
stores the Window vtable, calls the base constructor `0x41f1b0(this, parent,
rect*, handler)` and stores its own vtable (C).

| Offset | Size | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x01 | 4 | vtable (slot 0 = `handle(this, ev)`, slot 1 = `draw(this)` where the class has one) | C, `0x409f32`, `0x46a2de`, `0x40a44c` |
| +0x05 | 4 | next sibling | C, `0x41f788` |
| +0x09 | 4 | previous sibling, or the parent for the first child (flag bit 0) | C, `0x41f788`, parent walk `0x41f8d7` |
| +0x0d | 4 | first child (the most recently linked) | C, `0x41f788` |
| +0x11 | 4 | flags: bit 0 first in its parent's list; bit 1 hidden; bit 2 hidden only because an ancestor was (shown again with it) | C, `0x41f93a`, `0x41fabd`, `0x41f239` |
| +0x15 | 16 | absolute rectangle (left, top, right, bottom): the parent's absolute left/top plus the own rectangle | C, `0x41f599` |
| +0x25 | 16 | rectangle relative to the parent, right/bottom inclusive | C, copy `0x41f1cc`; asserts left ≤ right, top ≤ bottom `0x41f67f`; hit test `0x41f906` |
| +0x35 | 4 | cursor of the window (0: ask the parent); set by `0x420108`, used on hover enter | C, `0x46a240..0x46a260` |
| +0x39 | 4 | event mask: the event types the window accepts; the base sets 0x1f | C, `0x41f214`, dispatch `0x46a2f8` |
| +0x3d | 4 | handler: `void handler(event*)`, the constructor's handler argument | C, `0x41f231`, `0x41fc28` |
| +0x41 | 4 | 0 | C, `0x41f236` |

Linking (`0x41f788`) puts a new window **in front** of its parent's list, so
the list runs from the newest to the oldest child; the hit test (§3.7)
walks it in that order, so a later control lies on top of an earlier one
it overlaps (C).

Every window starts hidden (`or [this+0x11], 2` at `0x41f239`, C).
`WinButton` (so every ESRect-derived class), `ESMessage` and `ESDialog`
show themselves at the end of their constructors (`0x409d7d`, `0x40b8ae`,
`0x40c1f4`): they appear together with their parent. `ESBitmap` and
`ESTitle` (so `ESAlert`) hide themselves again (`0x40bc77`, `0x40b1c0`),
and `ESWindow`, `ESAnim2` and `ESPal` never show themselves: these need a
show call of their own (C). The table's `after_construction` is the result
after the screen constructor ran.

### 2.2 Classes

From the vtable stores of the constructors and the Borland type descriptor
12 bytes before each vtable (pointer at `vtable−0xc`, size dword at +0,
name offset word at +6; C). Slot 0 is the event handler, slot 1 the draw
function; "—" = the class has no slot 1 of its own and draws on event type
4 in slot 0.

| Class | vtable | Size | Constructor (base) | Slot 0 | Slot 1 |
| --- | --- | --- | --- | --- | --- |
| Window | `0x4718f4` | 0x45 | `0x41f1b0` | `0x41fc28` | — |
| ESWindow | `0x46e960` | 0x45 | `0x409f20` (Window) | `0x41fc28` | — |
| WinButton | `0x46e948` | 0x4d | `0x409d38` (ESWindow) | `0x409d8a` | pure (`0x46b964`) |
| ESRect | `0x46ec08` | 0x55 | `0x40aca4` (WinButton) | `0x409d8a` | `0x40af09` |
| ESButtonFont | `0x46e9b0` | 0x5d | `0x40a004` (ESRect) | `0x40a0bf` | `0x40a129` |
| ESButtonBitmap | `0x46ea5c` | 0x69 | `0x40a2c4` (WinButton) | `0x40a3a2` | `0x40a60d` |
| ESRadioButton | `0x46ead8` | 0x6d | `0x40a6b0` (ESButtonBitmap) | `0x40a6e9` | `0x40a81d` |
| ESRegionFill | `0x46ec1c` | 0x5d | `0x40af1c` (ESRect) | `0x409d8a` | `0x40af56` |
| ESTitle | `0x46ec60` | 0x75 | `0x40b0fc` (ESRect) | `0x409d8a` | `0x40b1d7` |
| ESAlert | `0x46ed18` | 0x7d | `0x40b590` (ESTitle) | `0x40b6f3` | `0x40b7d1` |
| ESMessage | `0x46ed8c` | 0xc9 | `0x40b828` (ESWindow) | `0x40b957` | — |
| ESBitmap | `0x46edcc` | 0x61 | `0x40bc48` (ESRect) | `0x40bc8a` | `0x40bd22` |
| ESGrid | `0x46ee10` | 0x6e8 | `0x40bd90` (ESRect) | `0x409d8a` | `0x40bf2c` |
| ESDialog | `0x46ef00` | 0xc3 | `0x40c1a4` (ESWindow) | `0x40c45f` | — |
| ESArm | `0x46ef40` | 0x61 | `0x40c928` (ESRect) | `0x40c965` | `0x40cac3` |
| ESAnim2 | `0x46effc` | 0x75 | `0x40ce0c` (ESWindow) | `0x40ce63` | — |
| ESPal | `0x46f03c` | 0x47 | `0x40d01c` (ESWindow) | `0x40d067` | — |
| ESQuad | `0x479c4c` | 0x65 | none: built inline (ESRect, then the vtable store, then `0x40a8c0`) | `0x409d8a` | `0x40abb0` |

`ESAnim` (vtable `0x46ef84`, stored at `0x40cc9c`), `ESClock` and
`ESReport` also have type descriptors, but the 22 screen constructors build
none of them (C: the generator reaches no other vtable).

### 2.3 Constructor arguments and fields

The table's `args` use these names, in this order after `this` (C, from
the constructors' stores; a field written by a base constructor is listed
once).

| Class | Arguments | Fields set |
| --- | --- | --- |
| ESWindow | parent, rect, handler | Window fields |
| WinButton | parent, rect, handler | +0x45 pressed = 0, +0x49 enabled = 1, mask \|= 0x60, shown (`0x409d7d`) |
| ESRect | parent, rect, handler, colour | +0x4d frame colour, +0x51 frame on = 1 |
| ESButtonFont | parent, rect, font, handler, label, colour, arg8 | ESRect(colour); +0x55 label: an ESMessage (0xc9) at (1, 0, w, h) with font, label, align 2, colour 0x29; +0x59 = arg8, label +0x49 = arg8 (not read by the class's own code; **H** unused) |
| ESButtonBitmap | parent, rect, font, bitmap_normal, bitmap_active, bitmap_extra, handler, label | WinButton; +0x4d label ESMessage at (0, 0, w, h), align 2, colour 0x29; +0x51 bitmap drawn when not pressed, +0x55 when pressed, +0x59 bitmap_extra (no read in the class's code, C; **H** unused), +0x5d latch = 1, +0x61 auto-repeat = 0, +0x65 repeat count, mask \|= 0x200 |
| ESRadioButton | as ESButtonBitmap | +0x69 checked = 0; draws +0x55 when checked, else +0x51 (`0x40a82b`) |
| ESRegionFill | parent, rect, handler, fill, colour | ESRect(colour); +0x55 dither colour = 0x25, +0x59 fill = the argument, +0x49 enabled = 0 |
| ESTitle | parent, rect, font, handler, colour, label, title_height | ESRect(colour); +0x55 title-bar colour 0x24, +0x59 body fill 1, +0x5d body dither colour 0x10, +0x61 title height, +0x65 stripes 1, +0x6d/+0x71 title gap x-range 0/0; +0x69 title ESMessage at (0, 0, w, title_height), align 2, colour 0x29; +0x49 = 0; hidden (`0x40b1c0`) |
| ESAlert | parent, rect, font, handler, colour, label | ESTitle(…, title height 0x14); mask \|= 0x60; +0x75 bitmap header (`0x453698`), +0x79 a (w+1)·(h+1) byte buffer for the screen under the box |
| ESMessage | parent, rect, font, handler, text, align, colour, opaque, style | +0x4d text (copied by `0x40b8bb`, fewer than 100 characters, asserted `0x40b905`), +0x45 align, +0xb1 font, +0xb5 colour, +0xb9 style, +0xc1 opaque, +0xc5 background 0x10; mask &= ~0x18 (no enter/leave); shown |
| ESBitmap | parent, rect, bitmap, handler, colour | ESRect(colour); +0x5d bitmap, +0x55/+0x59 draw offset 0/0, +0x51 frame off; hidden (`0x40bc77`) |
| ESGrid | parent, rect, handler, colour | ESRect(colour); 30 layer records of 0x38 bytes at +0x55 (`0x46b898` with `0x40bd58`, then `0x40bdf4` per record: three dwords 0, ten words 0xe, ten words 0x10, a dword 0), +0x6e5 = 1, +0x6e6 = 0x22 (§2.8) |
| ESDialog | parent, rect, font, handler | §2.9 |
| ESArm | parent, rect, handler, colour | ESRect(colour); +0x55 = rect height (the split line), +0x59 lower colour 0x10, +0x5d = 0 |
| ESAnim2 | parent, rect, handler | +0x45.. ten frame slots, +0x6d frame index 0, +0x71 frame count 0, mask \|= 0x200; frames added with `0x40cfb6(this, bitmap)` |
| ESPal | parent, rect, handler | +0x45 palette word 0 |
| ESQuad | (ESRect) parent, rect, handler, colour; then `0x40a8c0(this, font, text0, unused, align0, text1, x1, align1, text2, x2, align2, text3, x3, align3)` | four ESMessage columns at +0x55/+0x59/+0x5d/+0x61 with rectangles (2…x1), (x1…x2), (x2…x3), (x3…w−1), colour 0x29, opaque (+0xc1 = 1); `0x40ab3b(this, column, colour)` sets a column's colour; the row is built disabled (`0x40aa73`: +0x49 := 0) and the screens enable it through their fields |

Many screens overwrite fields after construction (title colours, enabled,
handlers): the table lists those writes per control under `fields`
(offset → value), and the effective handler (+0x3d) under `handler`.

### 2.4 Text button, image button, check box: press and release

Mouse codes are the event byte +0x24 (§3.4): 1 left up, 2 left down, 3
right up, 4 right down. The WinButton family's handlers require the control enabled
(+0x49 ≠ 0) for mouse events. WinButton `0x409d8a` and ESButtonBitmap
additionally require the two global gates `[0x470eb0]` and `[0x470fb0]`
clear (written by the movie code `0x41e786..0x41ee10`; **H**: a movie or
the CD prompt is up); ESRadioButton and ESArm do not test the gates;
ESBitmap tests neither the gates nor enabled (C).

| Class | Press | Release | Pointer leaves (type 0x10) |
| --- | --- | --- | --- |
| WinButton, ESRect, ESRegionFill, ESTitle, ESGrid, ESQuad (`0x409d8a`, jump table `0x409e2f`) | codes 2 and 4: pressed := 1, draw | codes 1 and 3: if pressed, **fire**, then pressed := 0, draw (the handler sees +0x45 = 1) | pressed := 0, draw, no fire |
| ESButtonFont (`0x40a0bf`) | as WinButton; codes 2 and 4 first play the click sound (`0x42f547`) when the sound flag `[0x482356]` is set and the event is not synthetic (+0x25 = 0) | as WinButton | as WinButton |
| ESArm (`0x40c965`) | as WinButton (jump table `0x40c9ed`) | as WinButton | unpress and post a redraw of the subtree (`0x41f3ed`) instead of drawing |
| ESButtonBitmap (`0x40a3a2`) | code 2: click sound (no synthetic test), pressed := 1, draw, **fire** unless auto-repeat; code 4: WinButton's press | code 1: pressed := 0 **without a draw** (the button stays drawn pressed until something redraws it); with auto-repeat count := 0, draw, **fire**; code 3: WinButton's fire-on-release | unpress unless latched (+0x5d) |
| ESRadioButton (`0x40a6e9`) | code 2: click sound, pressed := 1, draw, **fire**; others to WinButton | code 1: dropped | unpress unless latched |
| ESBitmap (`0x40bc8a`) | — | code 1: **fire** (no press needed) | — |

WinButton's type 2 (hidden) erases the rectangle with palette index 0x10
(`0x409f61`) and type 4 draws; ESBitmap handles only type 4 and code 1 (no
erase when hidden). ESAlert `0x40b6f3` takes mouse events (mask 0x60) and
drops them: a click on the box body reaches neither its parent nor the
shield (C).

"Fire" is `0x41fc28(window, event)`: if the handling window's handler
(+0x3d of the window in event +0x1c) is set, it is called with the event as
its only argument and owns it; otherwise the event is deleted (C). Handlers
end by deleting it themselves, `call [vtbl+0x18](ev, 3)`: the event's
virtual destructor with the delete flag (C; event vtables sit at +0, not
+1).

Auto-repeat (+0x61, set by the MISSION screen's arrow buttons): such a
button adds a 500/500 timer of the list `[0x5ddcb4]` when shown (event 1,
`0x46a3f0(list, this, 500, 500, 0)`) and removes it when hidden (event 2,
`0x46a478`); each tick (type 0x200) of a shown, pressed, enabled button
increments +0x65, draws and fires, so holding the button fires once per
tick (C, `0x40a3e9`, `0x40a414`, `0x40a49b`; the timer's unit is **H** ms).
Without auto-repeat, shown and hidden add no timer.

A check box or radio pair is an ESRadioButton whose handler sets the
`checked` field (+0x69) of itself and its partner (Preferences
`0x437351`, `0x437758`, …; vshell_ui.md §4.5, §11.4). The bitmaps are
frames 0 and 1 of `dba\chk_box.dba` (table: `bitmap_active` frame 0,
`bitmap_normal` frame 1).

Enable/disable is the word +0x49 alone (e.g. the bay buttons by mode,
`0x43b758`); a disabled ESButtonFont/ESButtonBitmap draws its label one
pixel lower or higher depending on pressed and enabled (`0x40a24d..0x40a2af`,
`0x40a63e..0x40a6a0`, C).

### 2.5 Show, hide and the modal boxes

- `show(w)` `0x41f93a`: nothing if w is shown. If its parent (`0x41f8d7`)
  is hidden, w becomes hidden-pending (bits 1 and 2). Otherwise both bits
  are cleared, events type 1 (shown) and 4 (redraw) are posted for w, and
  every pending child is shown the same way, oldest first (C).
- `hide(w)` `0x41fabd`: if w is hidden, only its pending bit is cleared
  (it then stays hidden when the parent is shown again). Otherwise w is
  hidden, marked pending when its parent is hidden, every shown child is
  hidden newest first (and so marked pending), and event type 2 (hidden) is
  posted (C).
- Both first send the hover window its leave when w's rectangle contains
  the queue cursor, and end with `0x46a15c(queue, 0)` when the hover is
  empty (C).
- Modal boxes: each `ESAlert` (quit, CD prompt, end of game, launch
  warning, the two scrap warnings, the Preferences alert) sits on a
  full-display `ESWindow` whose parent is the display root, created just
  before the box. `0x40b7e9(box)` shows that shield and then the box;
  `0x40b808(box)` hides the box and then the shield (C). (REPLAY, DEBUG
  and registration are ordinary screens: a full-screen ESBitmap on the
  root with an ESTitle.) The shield is a sibling of `[0x481180]` and newer,
  so the hit test (§3.7) finds it first and every click outside the box
  goes to the shield, whose handler is 0: the click is deleted (C; that
  this is the intended modality is **H**).
- ESAlert saves the screen under itself when shown (event 1 →
  `0x459080`) and restores it when hidden (event 2 → `0x458ee4`) (C; the
  primitives are in vshell_presentation.md, the stale-bitmap crash in
  vshell_process.md).
- ESAnim2 `0x40ce63`: shown → a 500-tick timer, `[0x46c28c]` := 0; hidden
  → timer stopped, **every frame bitmap deleted** (the frame count is
  kept) and `[0x46c28c]` := 1; tick → next frame (`0x40cfcb`) and fire
  (the main menu's `0x431830`) (C). Whether the main menu reloads its
  frames after being hidden is **open** (its constructor adds them only
  once).
- ESPal `0x40d067`: hidden selects and applies its palette word
  (`0x407b62`, `0x407c1b`); redraw fills its rectangle with 0x10 and
  **flushes the display at once** (`0x41fe49` at `0x40d193`: an immediate
  present) (C, V).
- ESMessage passes event 2 (hidden) to its handler and redraws on 4
  (`0x40b957`, C).

### 2.6 ESMessage text and alignment

`0x40b8bb(this, text, align, colour, redraw)` copies the text and stores
align and colour; `redraw` ≠ 0 redraws (C). Drawing `0x40b9e9`: with
`opaque` (+0xc1) the rectangle is filled with +0xc5 (default 0x10); the
text is drawn with the font +0xb1 in a span of the rectangle's width with
alignment +0x45 and style byte +0xb9, at x = 0 and the line
y = (h + 1) − ⌊(h + 1 − H)/2⌋ − 3 (h = bottom − top, H = the font metric
`0x4011ea(font, 0x46ed77)`, `0x40bb05..0x40bb53`), or y − 1 for an
explicit line argument y; glyph pixels of palette index 0x29 are remapped to
`colour` (+0xb5) through a 256-entry identity table (`0x40bb79..0x40bb9a`)
(C). Alignment 0 is left, 1 right, 2 centred (the string draw `0x401355`;
titles and buttons use 2, the left column of value pairs uses 1); the font
routines `0x4011ea`, `0x401355`, `0x4010fd` are in
[vshell_text.md](vshell_text.md).

### 2.7 Frames and fills (ESRect family)

Drawn relative to the window, inclusive and clipped to it, with
w = right − left and h = bottom − top (C for the sequences `0x40acd6`,
`0x40af56`, `0x40b1d7`, `0x40cac3`; V for the pixels; the line, pixel and
fill primitives `0x455624`, `0x459cdc`, `0x4576a4` and the drawing state
`[0x48e264]` are in vshell_presentation.md):

- Pen colours are dwords: high byte 0 writes the low byte, 4 a ramp entry
  (table `0x47ddd5`), anything else draws nothing (`0x459cdc`, `0x455624`;
  C, V for 0 and "else").
- ESRect `0x40acd6(this, fill)`: nothing at all when the frame flag +0x51
  is 0; with `fill` the interior (1,1)–(w−1,h−1) is filled with index
  0x10; edges (1,0)–(w−1,0), (w,1)–(w,h−1), (1,h)–(w−1,h), (0,1)–(0,h−1)
  in +0x4d, and the corner pixels (1,1), (w−1,1), (w−1,h−1), (1,h−1).
- ESButtonFont adds the bevel (2,1)–(w−2,1), (w−1,2)–(w−1,h−2),
  (2,h−1)–(w−2,h−1), (1,2)–(1,h−2) in the frame colour (also with the
  frame off) before its label.
- ESRegionFill: fill per +0x59, frame, then a checkerboard dither of +0x55
  over rows 1…h−1 from x = 1 + phase (phase 0, 1, 0, …), corners in +0x4d.
- ESTitle: frame; the title bar rows 1…bar−1 in +0x55 (bar = +0x61); with
  +0x65 the stripes: 26 groups of **14** 45-degree lines
  (x, bar−1)–(bar+x−2, 1) in index 0x0d, groups 28 apart from x = −5; the
  bar edges (w,1)–(w,bar) and (0,1)–(0,bar) and the title gap
  +0x6d…+0x71 in +0x55; without +0x59 the body dither from row bar in
  +0x5d; then the corners and the separator (0,bar)–(w,bar) in +0x4d.
- ESArm: frame with fill, rows +0x55…h−1 in +0x59, the split line in the
  frame colour, the frame again and, with +0x5d, the bevel (2,1)–(w−1,1),
  (w−1,2)–(w−1,h−2), (w−1,h−1)–(2,h−1), (1,h−2)–(1,2).
- ESBitmap: the bitmap +0x5d drawn at its offset (`0x4537e8`), then the
  frame without fill.
- Button labels are child windows: the button's draw draws the label at
  the pressed/released line (vshell_text.md), and the label's own redraw
  event (posted when shown) draws it again at its centred line (C).

### 2.8 Lists: ESQuad rows and ESGrid

The list rows of the screens (practice entries, repair/build/armory/
weapon lists, squad rows) are ESQuads: an ESRect row with four ESMessage
columns (§2.3). A row's handler is set after construction (`fields`
+0x3d, e.g. practice `0x44caa3`…) or comes from a handler table
(REPAIR's `0x48d2d8..`, filled by `0x4325cc`). Selection is the screens'
own logic (vshell_ui.md).

ESGrid is a layered bitmap panel. Its 30 layer records of 0x38 bytes at
+0x55 hold a point, a bitmap, ten recolour pairs from → to (0x0e → 0x10 is
"off") and a mirror flag; set `0x40be7f`, clear `0x40c182`. The draw
`0x40bf2c` frames with fill, draws grid lines every 16 pixels in the word
+0x6e6 when byte +0x6e5 is set, then each layer's bitmap and, per pair with
to ≠ 0x10, a kind-6 recolour over (x, y)…(x + width, y + height) (C; grid
lines V). The screens fill the layers at run time.

### 2.9 Edit field (ESDialog)

| Offset | Meaning (C) |
| --- | --- |
| +0x45 | text, at most 89 characters and a terminator (`0x40c2d2`, `0x40c3a2`) |
| +0x9f | allowed characters: pointer to a NUL-terminated set, 0 = any (`0x40c237`); default `0x46ee48` = the 26 capital letters and space |
| +0xa3 | font |
| +0xa7 | active (has the mouse capture) |
| +0xab | length |
| +0xaf | text width in pixels: the sum of the glyph advances (`0x4010fd(font, ch)`) |
| +0xb3 | cursor blink phase (default 1) |
| +0xb7 | protected prefix: backspace stops at this length |
| +0xbb | colour (default 0x29) |
| +0xbf | editable (default 1) |

- Set text `0x40c294(this, text)` (asserts fewer than 90 characters,
  recomputes length and width); `0x40c355(this, text, redraw, colour)`.
- Insert `0x40c382(this, ch)`: only if `ch` is in the set (`0x40c237`:
  the set is searched up to its terminator), length + 1 < 90 and
  **width + advance(ch) + 6 < right − left** (`0x40c3a7..0x40c3c7`); then
  appended, width updated, redrawn (C). The advance comes from the font
  (vshell_text.md); the rule makes the field's capacity a pixel width, not
  a count.
- Backspace `0x40c406`: only if length > 0 and length > prefix (+0xb7)
  (C). Saved games rows use prefix 4 (the "NN. " number) and the set
  `0x475869` (`^`, digits, lower- and upper-case letters, space; C,
  `0x438f95..0x438fde`); the DEBUG dialog uses `0x47a4e1` (the same 64
  characters), pilot name and registration `0x4761c3`/`0x476205` (digits,
  letters, space).
- Events (`0x40c45f`, handler body `0x40c467`):
  - 4 redraw; 0x10 (leave) stops its timers;
  - 0x200 blinks the cursor while shown (blink := editable ? 1 − blink : 0);
  - key events 0x40 insert the character word +0x24 (`0x40c382`) whenever
    the field is editable (+0xbf), active or not, then fire;
  - command events 0x100 of an editable, active field: +0x24 = 1
    (Backspace) or 4 (Left) → backspace (`0x40c406`), 10 (Enter) →
    deactivate (release the capture `0x46a01c`, clear +0xa7, redraw);
    every command then fires (`0x41fc28`, to the handler);
  - mouse code 2: an inactive field starts the 500/500 blink timer
    (`0x46a3f0`), sets the queue's capture flag (`[[0x5ddcb0]+0x1f]` := 1),
    becomes active (+0xa7 = 1) and fires; an active field releases the
    capture, stops its timer, posts a leave, recomputes the window under
    the cursor (`0x46a15c(queue, 0)`) and **re-posts the same click**
    (`0x468780`) so that it reaches the control under the cursor
    (`0x40c542..0x40c6b7`, C).

## 3. Event model

### 3.1 Event object

| Offset | Meaning (C unless marked) |
| --- | --- |
| +0x00 | vtable (slot +0x18: virtual destructor; `(ev, 3)` deletes) |
| +0x0c | type (§3.2) |
| +0x10, +0x14 | x, y: the pointer position at posting time (§3.3); made relative to the target window by the dispatch (`0x46a324`) |
| +0x18 | the display `[0x47f940]`, stored by `0x4689c1` for every posted event |
| +0x1c | the window that handles it (set by the dispatch) |
| +0x20 | the target window: the hit-test result, or set by the poster |
| +0x24 | mouse code byte (0 move, 1 left up, 2 left down, 3 right up, 4 right down), the key/character word, the command code, or a timer's argument |
| +0x25 | dword: 1 for the synthetic clicks of `0x413305` (suppresses the click sound) |

Mouse events are `WinMouseEvent`s of 0x29 bytes (constructor
`0x46903c`); queue, timer and other events are 0x24-byte `WinEvent`s
(`0x468db0`).

### 3.2 Event types

| Type | Meaning | Produced by (C) |
| --- | --- | --- |
| 1 | shown | posted by `0x41f93a` |
| 2 | hidden | posted by `0x41fabd` |
| 4 | redraw | posted by `0x41f93a`, `0x41f3ed` (a window and its shown subtree) |
| 8 | pointer enters (hover window changed) | dispatched directly by `0x46a15c` (§3.7) |
| 0x10 | pointer leaves | dispatched directly by `0x46a0b4` (§3.7) |
| 0x20 | mouse | mouse producer `0x4091fe`, `0x413305`, ESDialog re-post, the `WinConsumer`'s Enter (§7.3) |
| 0x40 | `WinKeyEvent` (character word at +0x24) | constructor `0x469566..0x46956c`; §7.2 |
| 0x80 | `WinScrollEvent` | `0x4692de..0x4692e4`; no producer on the keyboard or mouse path |
| 0x100 | `WinCommandEvent` (command code at +0x24) | `0x4697ca..0x4697d0`; §7.2 |
| 0x200 | timer tick | timer list `[0x5ddcb4]` (§3.8) |

### 3.3 Posting `0x468780` and the queue

`0x468780(ev)`: for a mouse move (type 0x20, code 0) the motion is scaled
by the speed words `[mouse+9]`, `[mouse+0xd]` (fixed point /256, the
remainders kept in `[0x5ddcf8]`/`[0x5ddcfc]`), added to the cursor, and
clamped to the display root's rectangle (`0x4688c1..0x4689b0`; past an
edge `0x4689f8`/`0x468a50` may hand the motion to another display, **H**).
The speed is 0x100 (`0x41fdd6`, `0x41fddd`), i.e. 1.0. A move whose motion
is (0, 0) is deleted at once.

Every posted event, not only mouse events, then gets the pointer position
at posting time: `0x4689c1` stores the display `[0x47f940]` at +0x18 and
calls its slot +0x14 (`0x420152`, `GetCursorPos` + `ScreenToClient`) into
+0x10/+0x14, overwriting the result of the motion. In VSHELL's fixed
640 × 480 client that is the frame position. Since the producer posts in
the device poll, a button event carries the pointer of the poll, not of its
message. The motion therefore only decides whether a move is posted.

The event is appended to the queue `[0x5ddcb0]+0x11` (`0x469ea8` →
`0x41da2d`, a growable vector asserting on failure) (C).

### 3.4 Mouse input from Windows

The window procedure (`0x404fb8`, vshell_process.md) turns WM_MOUSEMOVE,
WM_LBUTTONDOWN/UP and WM_RBUTTONDOWN/UP into
`0x405bfc(kind, lParam, buttons)` (kind 0 move, 1 left down, 2 left up,
3 right down, 4 right up), which scales the position to the logical screen
(`0x4510f1`, §7.4) and calls the listeners registered at `0x481864` (ten
records of mask, callback).

The mouse producer's callback (`0x4090b3`) compares the button state with
the last one and records (dx, dy, code) in its 100-entry ring (`0x408ffa`):
a change of the left button gives code 2 (down) or 1 (up), of the right
button 4 or 3, no change 0 (move) (C, `0x40910d..0x4091d5`). If both
buttons changed it writes two records, left first, each with the same
motion (`0x409108..0x4091a8`). The motion is measured from a fixed centre,
not from the last position: `0x408f69` stores half the screen object's
+8/+0xc (`0x453480` cases 0/1) at producer +0x4a/+0x4e, and dx = x − that
(C; **H**: the screen object is the 640 × 480 frame, so the centre is
(320, 240), and a move to exactly the centre has motion (0, 0) and is
dropped by §3.3). The ring keeps the newest 99 records (when the write
index reaches the read index, the read index advances,
`0x40907d..0x4090a9`). The producer later posts one WinMouseEvent per
record (`0x4091fe`) with +0x24 = code; button records are posted with
+0x10/+0x14 = 0 before posting (`0x409287`), move records with the motion.

Only WM_LBUTTONDOWN/WM_RBUTTONDOWN set the movie skip flags (`0x4050e0`,
`0x405145`); while `[0x470eb0]` (a movie) is set every button message
returns early and posts nothing (`0x405124`, `0x405189`, …), moves never
do (C).

### 3.5 Drain `0x469ee4` and dispatch `0x46a274`

The drain pops events until the queue is empty (`0x469fd7..0x469fe5`), so
events posted by a handler run in the same drain (C; vshell_ui.md §1).
Per event:

1. A mouse move: if the queue's capture flag (+0x1f) is set, the target is
   the capture window (+1); otherwise the hit test (§3.7) from the device's
   root at the cursor. When the hit window differs from the hover window or
   the queue has no display, the old hover gets a leave (type 0x10) and the
   new one an enter (type 8, and its cursor is set), both dispatched at
   once (§3.7); then the move's position is stored at queue +9, so enter
   and leave carry the previous position (C, `0x469efe..0x469f6f`).
2. Any other event without a target (+0x20 = 0), real button clicks,
   key and command events included, goes to the hover/capture window
   (queue +1) found by the last move. **Clicks and keyboard events
   therefore go to the window under the mouse, or to the field holding the
   capture** (C).
3. With a target, `0x46a274` picks the handling window (`0x46a2f8`: from
   the target up through the parents to the first whose event mask +0x39
   shares a bit with the type) and stores it at +0x1c, subtracts the
   **target's** absolute origin from +0x10/+0x14 (`0x46a324`) and calls the
   handling window's slot 0 `handle(window, ev)`; with no such window the
   event is deleted (C).

Posted clicks with a target (the synthetic clicks, the ESDialog re-post)
skip the hit test: they reach their target even if another window lies
above it; visibility is not checked on that path (C, `0x469f71..0x469f87`).

### 3.6 Synthetic clicks (`0x413305`)

Unless the cheat byte `[0x482368]` is set, `0x413305` shows the DEBUG
dialog (`0x44e1b5`) and posts two WinMouseEvents of type 0x20 with target
`[0x48dcfc]` (the dialog's ESButtonFont "Use Default"), +0x25 = 1, first
**+0x24 = 2 (left down)**, then **+0x24 = 1 (left up)**
(`0x41335d..0x4133d0`). This is press then release (C, §3.4's codes); the
button fires on the release (§2.4), silently (+0x25), in the same or the
next drain (vshell_ui.md §4.11).

### 3.7 Hit test, hover and capture

`0x46a05c(queue, window, point)`: a hidden window (bit 1) is not hit;
the point must lie in the absolute rectangle, borders included
(`0x41f906`); then the children are tried from the first (newest) in list
order and the first hit child's result is returned, else the window itself
(C). Hidden subtrees are skipped entirely. The queue keeps the hover
window (+1), its display (+5) and the cursor position (+9); the capture
flag (+0x1f) is set only by an active edit field (§2.9) and cleared by
`0x46a01c`.

Enter and leave are dispatched at once, not queued: `0x46a0b4` builds a
type-0x10 event for the hover window, calls `0x46a274` directly, then
clears hover and display; `0x46a15c(queue, w)` (w = 0: hit test at the
queue cursor) sets the new hover and dispatches type 8 directly. Both do
nothing while the capture flag is set (C, `0x46a0b4..0x46a26c`).

### 3.8 Timer list

`[0x5ddcb4]` holds 20 records (window, remaining, period, arg; `0x46a3f0`
asserts on a full list). The poll `0x46a508` subtracts the time since the
last poll, posts one type-0x200 event (`0x469a18`, target the window, +0x24
the arg) per due record through `0x468780` and reloads remaining := period;
a record of period 0 is removed at its first poll, due or not; removal
(`0x46a478`, `0x46a4ac`) moves the last record into the hole and the loop
then skips it (C).

## 4. Screen geometry: the layout table

### 4.1 Generator

[`tools/reference-game/scripts/vshell_layout.py`](../../tools/reference-game/scripts/vshell_layout.py)
`--exe <VER95/VSHELL.EXE> --out shell/data/vshell_layout.json` checks the
SHA-256 first, decodes the code with the pinned llvm-objdump (bytes piped,
so the hashed bytes are the decoded bytes; or `--listing` a scratch
listing written by `--write-listing`; every instruction used is compared
with the executable's bytes) and interprets the 22 screen constructors in
the original order:

- Values are numbers or symbols with their formula. A global is run-time
  state when it lies in uninitialised data or some instruction writes it by
  absolute address; otherwise its initial bytes are read (listed under
  `constant_reads`: the 27-word weapon ID table `0x476a4c` and one ESQuad
  vtable slot).
- The ES constructors and a few helpers (`INTERPRET`: parent walk, grid
  records, modal show/hide, REPAIR's handler table `0x4325cc`, the skill
  label setters `0x43c370`/`0x43c671`) are interpreted. Modelled with their
  documented effect: `operator new`, the base window constructor (link,
  rectangle, hidden), show/hide (§2.5 flags, without events), the text
  setter, the ESTEXT lookup `0x4087f0` (→ ESTEXT ID), the DBM/DBA loaders
  `0x408992`/`0x408aa1` (→ file name; DBA frames are +8[index]), the file
  stream `0x403065`/`0x403985`/vtable +0x18 (→ file offsets). Other calls
  are listed in `calls_not_interpreted` with their arguments; `sprintf`
  into a stack buffer is kept as format address and values.
- The two roots are seeded (§1). A conditional branch on a run-time value
  stops the run unless `BRANCH_ASSUMPTIONS` names it; the table repeats
  every assumption used (§4.4).
- The output is deterministic (one control per line).

### 4.2 Table format (`schema` 1)

Top level: `source` (file, hash, image base, generator, this contract),
`classes` (per class: vtable, RTTI size, constructors seen), `resources`
(the run-time globals of fonts, background and text table, §5),
`assumptions`, `screens` (22, in construction order: name, constructor,
controls), `constant_reads`, `calls_not_interpreted`.

Per control (one line each):

| Key | Content |
| --- | --- |
| `id` | `<screen>.<NN>`, in construction order within the screen |
| `class`, `ctor`, `site` | RTTI class (§2.2), the constructor called first on the object, its call site |
| `owner` | the control whose constructor or column method created this one (button labels, title labels, ESQuad columns) |
| `globals` | where the screen keeps the pointer: an address, or the formula of a run-time address |
| `parent` | `{"control": id}`, with `root` = `[0x481180]` and `display` = the display root |
| `rect` | relative rectangle (§2.1); a number or `{"formula"}` / `{"file"}` per edge |
| `screen_rect` | absolute rectangle when the parents' left/top are numbers (root at 0,0), else null |
| `args` | the constructor arguments by name (§2.3): numbers, handlers as `"0x…"`, fonts and bitmaps as `{"global"}`, `{"dba": file, "frame": n}` or `{"dbm": file}`, labels as `{"estext": id}`, literals of the executable as `{"exe_string": address, "length": n}` (never their characters), stack buffers as `{"local_buffer": …}` |
| `text` | an ESMessage's text when the screen set it after construction |
| `after_construction` | `shown`, `pending` (shown with its parent) or `hidden` (needs its own show) |
| `handler` | the effective handler: field +0x3d if the screen wrote it, else the argument |
| `methods` | ESQuad columns (`quad_columns`, `quad_set_colour`), ESAnim2 frames (`anim_add_frame`) |
| `fields` | fields the screen constructor wrote after construction: offset → size, value |
| `screen_show_hide` | show/hide calls made directly by the screen constructor; calls inside interpreted helpers are not listed (the CD prompt's and Preferences' modal show `0x40b7e9` at `0x4323c9` and `0x435739`, after which they hide the shield); `after_construction` includes them |
| `per_record` | the control is the template of a loop over run-time records (§4.4) |

### 4.3 Screens

| Screen | Constructor | Controls | Top windows (global) | Notes |
| --- | --- | --- | --- | --- |
| main_menu | `0x430fc4` | 25 | ESWindow `0x48d1a4` (display), ESAnim2 `0x48d1a0`, ESBitmap `0x48d1a8` | ten ESButtonFonts in ESTitle `0x48d1ac`; animation frames `dbm\bay2a_80..84.dbm` (84 twice) |
| practice | `0x44b310` | 76 | ESBitmap `0x48dbfc` | eight ESQuad entries `0x48dc5c+4i`, handlers set as fields; parameter labels with run-time ESTEXT IDs (`[0x4825c1..5]` + base) |
| saved_games | `0x438c40` | 49 | ESBitmap `0x48d428` | ten ESDialog slot rows `0x48d434+4i` (prefix 4, set `0x475869`) |
| preferences | `0x435598` | 56 | ESBitmap `0x48d384`, ESWindow `0x48d3bc` (alert shield) | eleven ESRadioButtons on `dba\chk_box.dba`; the full-screen alert `0x48d3c0` |
| debug_dialog | `0x44dd38` | 13 | ESBitmap `0x48dcdc` | edit field `0x48dcf8`, Use Default `0x48dcfc` (§3.6) |
| cd_prompt | `0x4322a8` | 7 | ESWindow `0x48d1e8` | ESAlert `0x48d1ec` |
| service_bay | `0x43afd4` | 21 | ESBitmap `0x48d520`, ESPal `0x48d524` | nine ESButtonBitmaps on `dba\mnu_bttn.dba` frames 1/2 (online button `dba\online.dba` 0/1), font `[0x46de04]` |
| squad_panel | `0x43d029` | 57 | nine ESGrids `0x48d59c..0x48d5bc` and labels on the root | eight ESQuad squad rows with `sprintf` numbers; hot-spot tables loaded by `0x41508f` (not interpreted, §4.4) |
| repair | `0x4326c7` | 168 | eight ESGrids `0x48d1f8..`, ESTitle `0x48d218` | 25 ESQuad rows with handlers from `0x48d2d8..` |
| quit_box | `0x431fa8` | 7 | ESWindow `0x48d1d8` | ESAlert `0x48d1dc` |
| end_of_game_box | `0x44d2bc` | 7 | ESWindow `0x48dcb4` | texts set when shown (ESTEXT 0 at construction) |
| launch_warning_box | `0x44d66c` | 7 | ESWindow `0x48dcc8` | as above |
| pilot_name | `0x43b8f0` | 12 | (inside saved_games.48) | the dead name-entry screen (vshell_ui.md §3) |
| registration | `0x43bd2e` | 16 | ESBitmap `0x48d574` | skill label set by `0x43c671` |
| replay_box | `0x44cdac` | 9 | ESBitmap `0x48dc9c` | |
| weapons | `0x43ebbc` | 161 | ESTitle `0x48d674` | two templates over `gam\arm_weap.dat` (§4.4), 27 weapon rows, enabled per availability |
| build | `0x445de8` | 80 | ESTitle `0x48d968`, ESRegionFill `0x48d9bc` | nine ESGrids, nine ESQuad rows |
| armory | `0x4484c4` | 163 | ESTitle `0x48da58` | one template over `gam\arm_weap.dat`; 27 ESQuad rows whose name column points into the run-time weapon table |
| crew | `0x441548` | 40 | ESTitle `0x48d80c` | `dba\c_pilots.dba`, `dba\star.dba` |
| mission | `0x442bc4` | 55 | ESTitles `0x48d8b4`/`0x48d8b8`/`0x48d8bc`, ESBitmap `0x48d8c0`, ESRect `0x48d8e0` | eight auto-repeat arrow ESButtonBitmaps on `dba\miss_arw.dba`; frame rectangle per display mode (§4.4) |
| herc_scrap_box | `0x4479b8` | 11 | ESWindow `0x48da10` | |
| weapon_scrap_box | `0x447e3e` | 11 | ESWindow `0x48da30` | |

1051 controls, 548 of them created inside another control (labels, titles,
columns).

### 4.4 Not resolved statically

These are recorded as formulas or assumptions, never guessed:

1. **Rectangles from a game file.** WEAPONS builds one ESBitmap per record
   of `gam\arm_weap.dat` in two lists (`0x43ee4c..0x43ef57`,
   `0x43ef7f..0x43f082`), ARMORY one list (`0x448748..0x448853`). The
   layout read from the code: a word count, then per record a word item
   ID, a dword x and a dword y (8 bytes, `0x403985`) and a word frame
   index; the second list follows the first. The control: parent the
   ESArm, rectangle (x, y, x + width(frame), y + height(frame)) with the
   frame of `dba\arm_weap.dba`, colour 0x27, frame off, kept at
   `0x48d680 + 4·slot(ID)` (list 1; `slot` = `0x43fd87`, the index of the
   ID in the table `0x476a4c`), `0x48d6e8 + 4·ID` (list 2), `0x48da64 +
   4·slot(ID)` (ARMORY). The table holds one template control per list
   (`per_record`); the own WEAPONS and ARMORY screens read the records from
   the CD at run time.
2. **The MISSION frame** `0x48d8b8` depends on the display-mode flag
   `[0x481f4c]` (`0x442d80`): top 0x2b in a window (the table's value),
   0x2a in full screen.
3. **Enabled state of the WEAPONS rows** (`0x43f86e`, `0x43f9d0`): the
   availability byte `0x483cde + 0x1d·item` at construction; the table
   gives the formula.
4. **Run-time texts and handlers**: labels with run-time ESTEXT IDs
   (practice parameter buttons, `[0x4825c1..0x4825c5]` + base, i.e. the
   prefs bytes, see vshell_ui.md §4.4), ARMORY name columns pointing into
   the weapon table (`[0x483cf5+0x1d·i]`), boxes whose texts are ESTEXT 0
   until shown, `sprintf` numbers in the squad rows (format `0x47664f`,
   values 1…8).
5. **Fonts and the background bitmap** are run-time globals (§5); the
   root's size is symbolic (`display_right`, `display_bottom`).
6. **Not interpreted**: the squad panel's hot-spot loader `0x41508f` and
   `0x414340` (data, not controls), the text tables `0x408bb5`
   (`wpn_desc.bin`, `wpn_info.bin`), ESDialog's draw `0x40c6ff` and ESQuad's
   draw `0x40abb0` called by constructors, ESAlert's bitmap header
   `0x453698`, the redraw `0x41f3ed`, `sprintf`. They create no window
   (C: no window or ES constructor among their direct call targets down to
   three call levels, checked on the listing; indirect calls not followed).

### 4.5 Using the table

- Screens draw from `screen_rect` (or `rect` + parent) and the class rules
  of §2; `after_construction` plus the screen's own show calls
  (vshell_ui.md) give what is visible.
- The capture journal clicks at the centre of a control's `screen_rect`;
  the owner-less controls with a handler are the clickable ones.
- A correction goes through the generator: change the profile or an
  assumption, regenerate, and
  `//tools/reference-game/scripts:vshell_layout_unit_test` compares the
  committed file with a regeneration from the CD (byte-identical; skipped
  without `ES2_DATA_ROOT`); its synthetic cases test the interpreter, the
  hash check and the listing check without data.

## 5. Resource globals

Filled by the start-up loader `0x40796c` (vshell_process.md) before the
screens are built; the table's `resources` names them from the executable
(C):

| Global | Content | Loaded at |
| --- | --- | --- |
| `0x46ddfc` | `dfn\font2` (first load) — button font | `0x407a13` (`0x4088da`) |
| `0x46de00` | `dfn\font2` (second load) — titles, labels, edit fields | `0x407a60` |
| `0x46de04` | `dfn\black` — service-bay button labels | `0x407aac` |
| `0x46de0c` | `dbm\bay2a_84` — the full-screen background bitmap of the frames | `0x407af9` (`0x408992`) |
| `0x46ddf8` | `<lang>\estext.bin` — the ESTEXT table of `0x4087f0` | `0x407b37` (`0x408bb5`) |

Why the same font file is loaded twice is **open**.

## 6. Open questions

- ESAnim2 deletes its frames when hidden; whether the main menu ever
  reloads them (and what VSHELL draws instead) is not traced.
- `bitmap_extra` of ESButtonBitmap and `arg8` of ESButtonFont are stored
  but never read by the classes' own code (C); whether a screen reads them
  is not checked.
- The squad hot-spot tables (`gam\arm_hots.dat`, `gam\rpr_hots.dat`,
  `0x41508f`).
- The global gates `[0x470eb0]`/`[0x470fb0]`: which movie or prompt state
  sets which (vshell_ui.md §19).
- The call path from the device poll `0x41e0bf` to the keyboard poll
  `0x409544` and to the `WinConsumer` (consumer list `0x41dc20`), and the
  hand-off from the device event vectors to `0x468780`.
- Tab (`0x12`/`0x25`), Esc (`0x00`/`0x13`), Ins, Del, Home, End, PgUp,
  PgDn: no consumer besides logging was found (§7.3).
- The centre (320, 240) of the mouse producer (§3.4) and the visible
  effect of the right-button quirk (§7.4).
- `WinScrollEvent` (0x80) has no producer on the keyboard or mouse path.
- The image-button and ESAnim2 bitmap draw (GLBitmap vtable +0x1c) is
  taken as a blit at the window's corner (H); text and image buttons with
  their labels are not compared in vitro (the labels are compared in
  vshell_text.md).
- The event handlers of §2.4, §2.5 and §2.9 are read from the bytes and
  unit-tested in the own toolkit, not run in vitro (they need VSHELL's
  event objects and queue).
- The ramp pens (high byte 4, §2.7) and the per-window cursor (+0x35):
  no traced screen uses them.

## 7. Input

The keyboard path, the command codes, the `WinConsumer`, mouse scaling,
cursor shapes and the journal forms of the capture tour. §3.4–§3.7 (mouse
codes, routing, capture) and vshell_process.md (window procedure, pump,
focus gates, Alt combinations) are not repeated.

### 7.1 Keyboard: window procedure to key word

WM_KEYDOWN/UP and WM_SYSKEYDOWN/UP (`0x100`/`0x101`/`0x104`/`0x105`)
reach `0x4051fa` (C):

1. The virtual-key code is looked up in the 84-entry dword table
   `0x46d4bc`; its index is the **key index**, which follows the PC
   set-1 scan-code order: 1 Esc, 2–11 the digits `1`…`0`, 14 Backspace,
   15 Tab, 16–25 `Q`…`P`, 28 Enter, 29 Ctrl, 30–38 `A`…`L`, 44–50
   `Z`…`M`, 56 Alt, 57 Space, 58 Caps Lock, 59–68 F1–F10, 69 Num Lock,
   70 Scroll Lock, 71 Home, 72 Up, 73 PgUp, 74 numpad `-`, 75 Left,
   76 numpad 5 (`VK_CLEAR`), 77 Right, 78 numpad `+`, 79 End, 80 Down,
   81 PgDn, 82 Ins, 83 Del. Zero entries (Shift, the punctuation keys)
   never match, nor do keys missing from the table (F11, F12, …); such
   keys are ignored (loop `0x40521c..0x40543d`, C). The table has no
   virtual-key code for index 12 (minus) although the hot-key list names
   it, nor for any punctuation key, so the punctuation characters of both
   character tables (§7.2) can never be typed (C from the table bytes).
2. Modifiers from `GetKeyState` (thunk `0x46bbaa`; this branch is its
   only caller): Shift → `0x800`, Alt (`VK_MENU`) → `0x200`, Ctrl →
   `0x400` (`0x405230..0x40525e`). The **key word** is index | modifiers,
   plus `0x80` on key-up (`0x40539b`).
3. Before the up flag is added, the system combinations are checked
   (`0x405263..0x40539a`): `0x23e` Alt+F4, `0x20f` Alt+Tab, `0x401`
   Ctrl+Esc, `0x201` Alt+Esc, `0x21c` Alt+Enter, i.e. the actions of
   vshell_process.md (C; tested on key-down and key-up alike, except that
   Alt+Enter acts only on the key-up message: `test ebx, ebx` / `je` after
   `0x40530c`, with ebx = 1 for `0x101`/`0x105`). After opening the Quit
   box the word 0x23e (and 0x2be on key-up) continues to the hooks and the
   ring (`0x405294` falls through to the 0x20f compare), where it produces
   no character. Alt+Tab, Ctrl+Esc, Alt+Esc and Alt+Enter never reach the
   ring, also when they do not act (movie open or not interactive:
   `jmp 0x405863`) (C).
4. The key word is offered to up to ten key hooks at `0x4816a8` (records
   of active flag, byte list, callback; registered by `0x404d56` /
   `0x404db5`, removed by `0x404d88` / `0x404de9`). A hook whose list
   contains the low byte of the key word (index | `0x80` for up) is
   called with the word; a return of 1 consumes the key
   (`0x4053b3..0x4053ef`, C).
5. Otherwise the word goes into the 256-word ring `0x4814a0` (write index
   `[0x4816a0]`, read index `[0x4816a4]`; a full ring drops the key) and
   bit 1 of `[[0x48e244]]` is set; `0x404d0d` pops one word (0 = empty)
   and clears that bit when the ring runs empty (C).

`WM_CHAR` is not used: the window procedure has no `0x102` branch (C,
compare chain `0x404fb8..0x40505f`).

### 7.2 The keyboard object and its two event forms

The keyboard object (global `[0x48249c]`, constructor code around
`0x4093b0..0x409430`) registers the hot-key hook `0x4094ee` with the byte
list at `0x46e588`, and a second hook `0x404c03` that returns 0 and so
never consumes anything (C). The hot-key list holds Esc, Backspace, Tab,
Enter, Home, Up, PgUp, numpad 5, Left, Right, End, Down, PgDn, Ins, Del,
the minus key (index 12), and the key-up forms of these. `0x4094ee`
stores (key word, time stamp from `0x46baf0`) in a ten-entry queue inside
the object (+0x14, six-byte records, read index +0x50, write index +0x52;
`0x409491`; it keeps the newest nine) and returns 1, so **hot keys never
reach the ring** (C).

The object's poll `0x409544` (reached through its vtable; that the device
poll `0x41e0bf` calls it is **H**) posts to its device (`[obj+8]`,
`0x41e0aa`), all queued hot keys first, then the ring:

1. **Command events** (type `0x100`, `WinCommandEvent`, constructor
   `0x4697bc`) for the queued hot keys: +0x24 = byte
   `0x46e5a9[key word]`; `0xff` means no event (`0x409560..0x40962e`, C):

   | Key | down | up |
   |---|---:|---:|
   | Esc | 0x00 | 0x13 |
   | Backspace | 0x01 | 0x14 |
   | Ins | 0x02 | 0x15 |
   | Del | 0x03 | 0x16 |
   | Left | 0x04 | 0x17 |
   | Up | 0x05 | 0x18 |
   | Right | 0x06 | 0x19 |
   | Down | 0x07 | 0x1a |
   | PgUp | 0x08 | 0x1b |
   | PgDn | 0x09 | 0x1c |
   | Enter | 0x0a | 0x1d |
   | Home | 0x0b | 0x1e |
   | End | 0x0c | 0x1f |
   | Tab | 0x12 | 0x25 |
   | numpad 5, minus | swallowed (`0xff`) | swallowed |

   The table is indexed with the full key word including modifier bits
   (`movzx edx, di` before `0x409589`): a hot key pressed with Shift,
   Ctrl or Alt reads past the 256-byte table into unrelated data (e.g.
   Shift+Esc 0x801 → 5 "Up", Shift+Tab 0x80f → 0 "Esc"); 202 words are
   reachable, three of them observable (vshell_ui.md section 40).
2. Then the ring is drained (`0x4097b1`): key-up words are dropped;
   **Ctrl+Q** (word `0x410`) posts command `0x11` (`0x409632..0x4096e1`)
   and **ends the poll** (`0x4096e1` → `0x4097c2`): the rest of the ring
   waits for the next pass. Any other word is translated to a character
   through the 84-byte tables `0x46e6a9` (plain, read without sign
   extension) or `0x46e6fd` (Shift, read with sign extension; both hold
   only values below 0x80), index = word & 0x7f; 0 means no event; lower
   case would be upper-cased (`0x46b9a0`), but both tables hold upper-case
   letters already. Result: a **key event** (type `0x40`, `WinKeyEvent`,
   constructor `0x469558`) with the character word at +0x24
   (`0x4096e6..0x40979e`, C). The plain table gives digits, upper-case
   letters, Space and the US punctuation of the main block; the Shift
   table the shifted US symbols. Function keys, cursor keys and Ctrl/Alt
   combinations other than Ctrl+Q produce nothing (Ctrl is not tested on
   this path: Ctrl+A gives `A`, **H** from the code shape).

Both event kinds have no target (+0x20 = 0) and therefore go to the
hover window or the capturing edit field (§3.5 rule 2).

The device list appends at the tail (`0x41e445`); the poll walks it from
the head (`0x41e0e1..0x41e12c`), so mouse events of a pass precede its key
events (C; **H**: that both devices' event vectors reach `0x468780` in that
order is not traced).

### 7.3 Who uses the codes

- **ESDialog edit field** (handler `0x40c467`, §2.9): characters, command
  1 (Backspace) and 4 (Left) as backspace, command 0x0a (Enter) ends the
  edit.
- **The `WinConsumer` handler `0x409996`** (the address `0x409c6c` is only
  the tail of its command `0x11` case). The object is built on WinMain's
  stack at `0x401e8d` (constructor `0x409964`, vtable `0x46e908`, slot 0 =
  `0x409996`) for the device `0x4811dc`; it receives each device event,
  logs it through the debug print `0x46a834`, handles command events by
  the jump tables `0x409a5e`/`0x409a7c` (`0x4099ec..0x409c9e`) and **posts
  the event itself** at the end (`0x409c9e`) (C; **H**: that the device
  routes its events through it, the consumer list `0x41dc20` is not
  traced):
  - `0x04`/`0x05`/`0x06`/`0x07` (Left/Up/Right/Down): moves the cursor by
    8 logical pixels (`0x46863c` get, `0x468564` set);
  - `0x0a` (Enter down) posts a mouse event with code 2 (left down),
    `0x1d` (Enter up) one with code 1 (left up), target
    `[[0x5ddcb0]+1]` (the hover window), constructor `0x46903c` (type
    0x20): **Enter is a left click at the cursor**;
  - `0x11` (Ctrl+Q): `0x46a01c([0x5ddcb0])` (drops the capture), then
    `0x40b7e9([0x48d1dc])`, the same Quit-box call as Alt+F4
    (`0x405283..0x40528f`);
  - `0x00`, `0x01`, `0x13`: logged only; other codes log "unknown".

  The click is posted before the command, so Enter over an inactive edit
  field starts the edit and its command 10 ends it again, and Enter over a
  capturing field with the pointer elsewhere ends the edit and re-posts the
  press to the window under the pointer while the consumer's release
  already went to the field (C for the order; the visible effect **H**).
  With §3.5's rule that clicks go to the hover window, the keyboard can
  drive every screen: arrows to move, Enter to click (**H**).
- **Movies** (`0x41e760..0x41e83b`): the movie loop pops the ring itself;
  Esc (index 1) or Space (index 0x39) sets the skip flag `[0x470ea0]`, as
  does any button press in the window procedure (`0x4050fd`, `0x405162`)
  (C). The movie sets `SetCapture` (`0x41e781`) and releases it
  (`0x41e807`).
- No consumer of commands `0x12`/`0x25` (Tab) was found. There is no
  keyboard focus apart from the edit field's capture (§3.7): keys go where
  the mouse is.

### 7.4 Mouse scaling and button state

`0x405bfc(kind, lParam, buttons)` (§3.4) computes on first use
(`0x405b19`) the 16.16 factors `[0x46d740] = (word [[0x481948]+2] << 16)
/ client_width` and `[0x46d744] = (word [[0x481948]+0] << 16) /
client_height` (`GetClientRect([0x481f38])` into `0x4818e6`), then
`x = (LOWORD(lParam) * [0x46d740]) >> 16` and `y` likewise from
`HIWORD(lParam)` (`0x4510f1`: `imul`, `shrd 16`) (C). The two words at
`[0x481948]` are the frame's width 640 and height 480 (the frame bitmap
header, vshell_presentation.md). The button word `[0x4818e4]` takes bit 0
from `buttons & 1` and bit 1 from `buttons & 2`, but WM_MOUSEMOVE,
WM_LBUTTONDOWN and WM_LBUTTONUP pass the right button as
`(wParam & MK_RBUTTON) * 2`, i.e. bit 2 (`0x4050bd`, `0x405180`,
`0x4051d1`), which is dropped: a move or a left-button message while the
right button is held reads as "right released", so right down, a move,
right up gives the codes 4, 3, 0 (C for the codes; **H** that a screen
reacts to the early 3). `0x405b6c` is the inverse (logical to screen,
`SetCursorPos`), called from `0x408ff0`.

### 7.5 Cursor shapes

`0x408d2f(busy)`: busy ≠ 0 → `SetCursor(LoadCursorA(0, IDC_WAIT))`,
else `IDC_ARROW`; then `ShowCursor(1)` (C). Callers: `0x41e77b` (0,
movie start), `0x41ecde` (1) / `0x41ee60` (0) around movies, `0x431a83`
(1) / `0x431ad8` (0) around CONTINUE GAME's load (vshell_presentation.md,
"Cursor and busy cursor"). The window class cursor is `IDC_ARROW`
(`0x4068b4`). `0x420108` sets a per-window cursor handle (used on hover
enter, §3.5); `ShowCursor(1/0)` at `0x420189`/`0x420195`; `ClipCursor`
only at `0x407743` (mode change). VSHELL draws no cursor sprite: the
visible cursor is the Windows cursor (vshell_presentation.md).

### 7.6 Journal forms

Input is read only from window messages in the pump `0x406328`
(`PeekMessageA`, thunk `0x46bb68`) plus `GetKeyState` for the three
modifiers; no DirectInput, no `GetAsyncKeyState`, `GetCursorPos` only at
`0x420160` (C, import thunks and their callers). Per main-loop iteration
the pump runs first, then the device poll `0x41e0bf` turns ring and
queues into events, then the drain (§3.5; main loop: vshell_process.md §8). The forms
the capture journal injects
([`vshell_journal.h`](../../tools/reference-game/proxy/vshell_journal.h),
tour runs in
[vshell_presentation.md](vshell_presentation.md#the-tours)):

- **key**: `WM_KEYDOWN` (`WM_SYSKEYDOWN` with Alt) then, a few pump
  passes later, `WM_KEYUP` with the virtual-key code of the wanted key
  index (§7.1); modifiers by answering `GetKeyState(VK_SHIFT / VK_CONTROL /
  VK_MENU)` with bit 15 set while the key message is handled. Hot keys
  give command events, others characters (§7.2).
- **click at a logical point**: every posted event carries the
  `GetCursorPos` of the poll (§3.3), so lParam alone moves nothing; the
  capture proxy answers `GetCursorPos` with the last injected point. In
  one pass `WM_MOUSEMOVE` to the far client corner (639, 479), to (0, 0)
  and to the target (homing: the first two keep the motion from the
  centre non-zero, so a target at (320, 240) is still posted, §3.4); in
  later passes `WM_LBUTTONDOWN`, then `WM_LBUTTONUP` at the target. The
  move must be drained before the buttons, since clicks go to the hover
  window of the last move (§3.5).
- **click by keyboard**: arrows (8 px per press) and Enter through §7.3's
  `WinConsumer` (**H**).
- While `[0x470eb0]` (movie) is set no button events are posted; while
  the window is unfocused nothing is read (vshell_process.md).

## 8. Input and event queue of the own shell

[`shell/include/es2/shell/gui/events.h`](../../shell/include/es2/shell/gui/events.h)
(`events.c`) implements §3.1 to §3.4, §3.6 and §7.1 to §7.4: the queue,
posting, the mouse producer, the key path, the device poll and the
synthetic click. Hit test, hover, capture, dispatch to windows (§3.5 rules
1 to 3, §3.7) and the `WinConsumer` of §7.3 are the widget toolkit's
(§9). The platform layers (`window.c`, `headless.c`) deliver window
messages in VSHELL's form (frame position + `MK_` bits; virtual-key code +
the three `GetKeyState` modifiers), so SDL's own semantics never reach the
handlers.

**Own choices.**

- The event queue is a ring of 1,024 events (VSHELL's is a growable vector
  that asserts on failure); posts beyond it are counted and dropped.
- Hot keys with modifiers (§7.2) post the same bytes as VSHELL: the shell
  host reads the 0xed4 bytes at `0x46e5a9` from the hash-checked
  `VSHELL.EXE` at run time and hands them to `es2_gui_input` (nothing is
  stored in the source; vshell_ui.md section 40). Without the executable
  they post nothing.

**Verification.** `//shell:gui_test`: mouse sequences (codes, positions,
the centre drop, both-button records, movie gate and skip flags, ring of
99), keys (characters, Shift/Ctrl/Alt, Ctrl+Q ending the poll, hot-key
commands down/up, numpad 5, system keys with and without the interactive
flag and a movie, full ring, hot queue of nine, `-k`), the drain running
events posted by a handler, the synthetic click; `//shell:gui_sdl_test`:
the SDL key event → virtual key → key index mapping (layout letters, the
digit row on every layout, the keypad with and without Num Lock);
`//shell:gui_tables_real_test`: the key table `0x46d4bc` (84 entries), the
hot-key list `0x46e588` (32 entries), the command bytes `0x46e5a9` for all
256 words, the character tables `0x46e6a9`/`0x46e6fd`, and the code bytes
at `0x41fdd6`, `0x41fddd`, `0x4689c1`, `0x41ffeb` equal to the own tables
and the facts above (reference file hash checked first).

## 9. Widget toolkit of the own shell

### 9.1 Implementation

[`shell/include/es2/shell/gui/widgets.h`](../../shell/include/es2/shell/gui/widgets.h)
(`widgets.c`) implements §2, §3 and §7.3 over the frame and queue of §8;
`gui/canvas.h` makes `es2_gui_frame` the one frame (`es2_frame` and
`es2_shell_text_frame` are views of it); `gui/layout.h` compiles §4's
table into C and registers the screens.

The screens are built under a hidden root (`es2_ui_init` leaves it hidden;
the flow shows it), and the builder applies the table's
`after_construction` state directly, since `screen_show_hide` omits the
modal shows inside helpers (§4.2).

### 9.2 Own choices

- Refused instead of VSHELL's assertion or crash, counted in
  `ui->refused`: a full pool (1,536 windows) or timer list, a bad
  rectangle (`0x41f67f`), image buttons and ESAnim2 frames without a
  bitmap, ESMessage texts of 100 bytes or more, ESGrid recolour sources
  outside 0…255, ramp pens (high byte 4; no widget uses one).
- Lines of slopes other than 0, 90 and 45 degrees (never drawn by a
  widget) use Bresenham's steps (H).
- The pointer moved by the arrow keys is clamped to the 640 × 480 frame
  and, without a platform warp callback, reaches the queue as a
  WM_MOUSEMOVE (what SetCursorPos causes, H).
- The per-window cursor (+0x35, `0x420108`) is not kept: no traced screen
  sets one.
- The text panel is one window drawing the text page (vshell_text.md);
  VSHELL builds one label per line: the same pixels, one dirty rectangle
  instead of one per line.
- The input layer (§8) posts device events in its poll; the toolkit runs
  the consumer over them between the poll and the drain and keeps
  VSHELL's order (each event after the clicks the consumer posts for it;
  H for the device path, §6).
- Button labels are drawn twice, as the tree does (§2.7).

### 9.3 Building a screen from the table

1. In the screen's own file `shell/src/gui/<screen>.c`, write
   `int es2_screen_<name>_build(es2_ui *ui, void *context)`: call
   `es2_ui_layout_build(ui, ES2_UI_SCREEN_<NAME>, &resolver, ids, n)` with an
   `es2_ui_resolver` that returns the fonts for the globals `0x46ddfc`,
   `0x46de00`, `0x46de04`, the bitmaps for `ES2_UI_REF_DBA`/`DBM`/`GLOBAL`
   references (`es2_ui_layout_file` names the file), the label texts
   (ESTEXT IDs, executable literals by address, globals) and the handler for
   each VSHELL handler address.
2. Control `<screen>.NN` is `ids[NN]` (also `ui->layout_ids[first + NN]`,
   `first` from `es2_ui_layout_screen_of`). Build what the table leaves to
   run time yourself: record lists (`per_record`), rectangles with
   `ES2_UI_EDGE_FORMULA`/`FILE` edges, fields whose value is a formula
   (enabled by availability, run-time ESTEXT IDs), ESGrid layers.
3. Show the screen with `es2_ui_show` (modal boxes with
   `es2_ui_modal_show`/`hide`; set `ui->quit_box` for Ctrl+Q and call
   `es2_ui_quit_box` from the shell's Alt+F4 system hook); handlers receive
   `(ui, window, event)` and may show, hide, post and redraw.
4. Append one line to `es2_ui_screens[]` in `shell/src/gui/screens.c`
   before the terminator, `{ES2_UI_SCREEN_<NAME>, es2_screen_<name>_build},`,
   with the build function's declaration in the screen's header;
   the screen's library depends on `//shell:screens`.
5. Test it on the headless mode (`es2_gui_headless_run` with hooks that
   call `es2_ui_on_timer`/`es2_ui_on_event`) and record frame hashes; the
   capture tour's frames are the reference for the full screen.

### 9.4 Verification

`//shell:widgets_test` (255 checks): tree, flags and events of show/hide,
hit test, enter/leave and masks, relative positions, press/release and fire
order per class, the disabled rule for every class with a mouse rule, the
gates, sounds, synthetic clicks, a handler posting events in the same drain,
auto-repeat and the timer list quirks, the edit field (capture, keys,
backspace and prefix, blink, the re-posted click, Enter), the consumer
keys, the Quit box and its shield, ESAlert's save-under, the class draws,
the layout builder over all 22 screens (classes and states equal to the
table, the ten Saved games rows with prefix 4 and set `0x475869`), and a
synthetic screen through the headless script runner (deterministic present
hashes). `//shell:widgets_real_test`: 2,084 cases (284 fixed, 1,800 seeded,
canvas 200 x 120) of the erase, ESRect, ESButtonFont, ESRegionFill, ESTitle,
ESArm, ESGrid, ESQuad and ESPal draws equal to VSHELL's own in the worker
(`--op widget-draw`); five mutations of the draw code (stripe count, dither
phase, a corner pixel, the pen rule, the split line) each make it fail.
`//shell:layout_table_test`: the generated header equals a regeneration
from the committed JSON.
