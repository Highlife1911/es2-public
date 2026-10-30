# VSHELL presentation

How the reference `VER95/VSHELL.EXE` draws and presents: the frame buffer,
the GDI and DirectDraw present paths, the palette from DPL file to screen
and which scene selects which palette, the bitmap draw and blit primitives,
the redraw model, the cursor, what the 256COLOR layer changes, the present
sites the capture tour hooks, and what the own shell's presenter keeps. It
is the VSHELL counterpart of [presentation.md](presentation.md) for DBSIM;
both programs are built on the same engine library, and several structures
below (the `BMP_8` screen object, the copied graphics-driver table, the DIB
section) have a twin there.

**Method.** Read from the bytes of the reference `VER95/VSHELL.EXE`
(image base `0x400000`) with the pinned
`llvm-objdump -d --no-show-raw-insn -M intel`. Code that the linear listing
decodes out of step (the blitter `0x45a09c..0x45a670`, the ramp installer
`0x457d88`, the event drain entry `0x46848c`) was disassembled from its
entry with `--start-address`. Import thunks are named from the PE import
table (VSHELL's IAT is in `.idata` at `0x5de000`); DATA tables (palette
names, driver descriptor, vtables) were read from the file.
**Hypothesis** = every statement about what
Windows or the 256COLOR emulation does with the calls.

Related contracts: [vshell_process.md](vshell_process.md) (WinMain, window
procedure, main loop, the Alt+Tab crash), [vshell_widgets.md](vshell_widgets.md)
(widgets, layout table, draw handlers; input in its section 7),
[vshell_text.md](vshell_text.md) (fonts), [vshell_ui.md](vshell_ui.md)
section 19 (movies).

## Summary

1. **One 8-bit frame, 640 × 480, in a top-down DIB section.**
   `0x406434` creates it at window creation: memory DC `[0x48195c]`, bits
   `[0x481950]`, row stride 640, a 14-byte frame bitmap header
   `[0x481948]` that the graphics driver binds as the drawing destination
   and the present and clear read for the size. Every image, text and
   widget is drawn in software into this buffer; nothing is drawn into a
   window DC or a DirectDraw surface directly.
2. **There is one present primitive, `0x406bfb(P, R)`.** It
   copies a rectangle of the frame to the screen: GDI branch `BitBlt` (1:1,
   no scaling, from `[0x48195c]` to the `BeginPaint` DC), DirectDraw branch
   `Lock` / row copy / `Unlock` of the primary surface. It is not a repaint
   (nothing is drawn); it presents whatever rectangle its callers pass. The
   full-frame present is `0x406e87`, the clear is `0x4077ee`.
3. **Redraw model: dirty rectangles, flushed once per main-loop iteration.**
   A widget's draw handler first calls `0x41fbd9`, which asks
   the display object `[0x48118c]` (class `WinWin95Display`) to clip drawing
   to the widget's rectangle and to queue that rectangle (at most 100,
   contained rectangles merged); then it draws into the frame. The main
   loop presents the queue through `0x41fe49` → `0x41fe2c` → `0x406bfb`,
   right after the event drain and the deferred palette install. `WM_PAINT`
   only re-blits the whole frame (`0x406e87`); it redraws nothing.
4. **Palettes: one 20-entry name table, one deferred installer.**
   `0x407b62(n)` loads `dpl\<name>.dpl` from the table at
   `0x46de14` (`intr_pt1`, `palette`, `arming`, `cam_er`, `cam_moon`,
   `br_w1..5`, `db_w1..5`, `alph`, `delt`, `omic`, `brav`, `luna`) and marks
   it pending; `0x407c1b` installs it (ramps, then the screen palette
   `0x4071fe`). The DPL colours × 4 go **unchanged, as RGBQUADs**, into the
   DIB colour table (`SetDIBColorTable`, one call site `0x4072f3`); only the
   logical screen palette gets the 20 static system colours and a blue
   nudge for colliding entries. Of dbm.md's hypotheses `PALETTE`, `ARMING`,
   `CAM_*`, `BR_*`, `DB_*` hold; `BAY`, `MAP`, `MAP_PAL1`,
   `MAP_PAL2` are never named by VSHELL (refuted for the shell); the
   mission map uses `ALPH`/`DELT`/`OMIC`/`BRAV` by campaign group.
5. **DirectDraw is a real full-screen mode, not only a colour check.**
   With `prefs.cfg` byte 6 = 1 (or the Preferences' Full
   Screen) VSHELL creates a DirectDraw object, exclusive 640 × 480 × 8, one
   primary without back buffer, an 8-bit palette, and presents by locking
   the primary. The colour check is a separate GDI test (`GetDeviceCaps`
   `BITSPIXEL` ≠ 8 → warning box, `0x401c1f`).
6. **Present sites:** GDI `BitBlt` at `0x406e52`
   (IAT slot `0x5de3f0`, the only `BitBlt` call in the image), palette
   `SetDIBColorTable` at `0x4072f3` (slot `0x5de3f8`, the only call), clear
   `StretchBlt` at `0x40791f` (slot `0x5de3f4`, the only call); DirectDraw
   `Unlock` at `0x406d67` (present) and `0x407888` (clear). Pixels are
   always at `[0x481950]`, 640 × 480, stride 640. Details in
   [Present sites](#present-sites).
7. **The 256COLOR layer does not change the frames (live).** The
   capture tour draws the same index and colour-table hashes with and
   without it ([The tours](#the-tours)); only the conversion to screen
   colours on a palette device can differ.

## The frame buffer

| Item | Address | Value | Evidence |
|---|---|---|---|
| frame width, height | `[0x481968]`, `[0x48196c]` | 640, 480 (constants stored by `0x406389`, `0x4063af..0x4063b9`) | |
| DIB creation | `0x406434` (called from `0x4069fa` after `CreateWindowExA`) | `CreateCompatibleDC(0)` → `[0x48195c]`; BITMAPINFOHEADER at `0x481974`: size `0x28`, width 640, **height −480 (top-down)**, 1 plane, 8 bit, `BI_RGB`, image size 307,200; colour table at `0x48199c` = the 256 system palette entries with red and blue swapped (`0x4064ae..0x4064e3`); `CreateDIBSection(memdc, bmi, 1 = DIB_PAL_COLORS, &[0x481950], 0, 0)` → `[0x481960]`, selected into the memory DC (old object `[0x481964]`); failure → message box "Bad WinGBitmap" | |
| frame bitmap header | `[0x481948]` (14 bytes, `0x4065b0..0x40661c`) | +0 height 480 (s16), +2 width 640 (s16), +4 depth 8, +5 flags (low nibble kept, bit `0x10` set = opaque), +6 size 307,200, +0xa pixels = `[0x481950]` | |
| screen object | `[0x48194c]` (`0xa0` bytes, `0x4530f8` with the name `BMP_8` `0x46dd03`) | page origins `+0x48..+0x67` zeroed because the name is not `VGA_8` (`0x40671f..0x4067bf`), as in DBSIM | |
| graphics driver | descriptor `0x47e69c` (`0x24` dwords, copied to `0x47e5dc` by `0x459a96`) | slot 11 `0x42884f` binds the screen to the frame bitmap (row table `[0x46d874]`: entry 0 = pixel base, entries 1… = row offsets `i × 640`); slots 17/18 (`[0x47e620]`/`[0x47e624]`, called with the screen's `+0x3c`/`+0x34` like DBSIM's page selects) are empty (`enter`/`leave`/`ret`) | |
| blitter bounds | `[0x47c482]` row table, `[0x47c4a6]`, `[0x47c4aa]` | 640, 480 (copied from the screen object by `0x45335c`) | |
| page origin S | `[0x481940]`, `[0x481944]` | (0, 0): set to 0 at bind time (`0x4289ae`); its only writer `0x406218` is reached only through driver slot 15 (copy `[0x47e618]`), which nothing reads | no reader found |
| "screen size" of the page present | `[0x481920..0x48192c]` | (0, 0, w − 1, h − 1) of the last bind: 320 × 240 at start-up (`0x406805` passes width/2, height/2), 640 × 400 after `0x427e75` (the mission map build, [vshell_mission_map.md](vshell_mission_map.md)), the offscreen size after `0x427ae6` | only `0x406264` reads it |

So the 8-bit picture of VSHELL is always the 640 × 480 buffer at
`[0x481950]`; there is no larger virtual page and no scroll origin in use.
The frame bitmap's opaque flag means only that blits *from* the frame copy
index 0 as well.

## The present primitive `0x406bfb(P, R)`

Arguments: `P` = pointer to a point (x, y), `R` = pointer to a rectangle
(left, top, right, bottom, inclusive). `w = R.right − R.left + 1`,
`h = R.bottom − R.top + 1`. The branch is selected by the DirectDraw flag
`[0x481f4c]` (`0x406bfb..0x406e86`).

**GDI branch** (`[0x481f4c]` = 0, only when the memory DC `[0x48195c]` ≠ 0):

1. `GetClientRect(hwnd, &rc)`, `InvalidateRect(hwnd, &rc, TRUE)`,
   `hdc = BeginPaint(hwnd, &ps)` (`hwnd` = `[0x481f38]`).
2. If the logical palette `[0x481954]` ≠ 0: `old = SelectPalette(hdc, pal,
   FALSE)`, `RealizePalette(hdc)`.
3. Scale factors `(rc.right << 16) / 640` and `(rc.bottom << 16) / 480` are
   computed and never used; two comparisons whose flags are never read
   follow (dead code, `0x406dd6..0x406e2f`).
4. **`BitBlt(hdc, P.x, P.y, w + P.x, h + P.y, [0x48195c], R.left, R.top,
   SRCCOPY)`** at `0x406e52`. The destination is P and the source is
   R's corner; the width and height are each too large by P.x and P.y
   (quirk). For every caller that passes a real rectangle (the dirty flush)
   P = (R.left, R.top), so the call copies the frame 1:1 to the same client
   position, over a block that reaches from (R.left, R.top) to
   (R.right + 1, R.bottom + 1) measured from the client origin (hypothesis
   for the visible effect: GDI clips to the source bitmap and the client;
   the extra area shows frame pixels that are current anyway).
5. If a palette was selected: `SelectPalette(hdc, old, TRUE)`,
   `RealizePalette(hdc)`. `EndPaint`.

There is **no scaling**: the frame is shown 1:1 at the client origin
whatever the client size is. (The mouse path scales client coordinates to
the frame with factors computed at `0x405b19..0x405b66` and used at
`0x405b6c`; [vshell_widgets.md](vshell_widgets.md) section 7. A larger
client thus shows the frame unscaled while input is scaled: quirk,
hypothesis that it is visible only when the window is resized.)

**DirectDraw branch** (`[0x481f4c]` ≠ 0; primary `[0x481f40]`):

1. `IsLost` (`+0x60`) = `DDERR_SURFACELOST` → `Restore` (`+0x6c`).
2. `Lock(NULL, &desc, DDLOCK_WAIT, NULL)` (`+0x64`, desc size `0x6c`); a
   failed lock skips the present (no unlock).
3. Source = `[0x481950]` + P.x + P.y · 640; destination = `lpSurface` +
   R.left + R.top · `lPitch`. Rows = min(h, 480, 480 − P.y), columns =
   min(w, 640) (no clamp against 640 − P.x). One `memcpy` per row when
   R.left = R.top = 0, else a byte loop (same result).
4. `Unlock(NULL)` (`+0x80`) at `0x406d67`. No flip, no cursor sprite.

Here P is the source and R the destination, the opposite of the GDI branch;
the two agree for every caller in the image because P is either R's corner
or (0, 0) with R at (0, 0) (for the four call sites below).

### Callers

| Caller | Return address (site) | P, R | When |
|---|---|---|---|
| `0x41fe2c(display, rect)` | `0x41fe3d` | P = R = the queued rectangle | the dirty flush `0x41fe49`, once per main-loop iteration (`0x40209c`), in the settle loop `0x402266` (`0x4022a6`, up to 24 rounds) and at the end of a screen object's draw event (`0x40d193`) |
| `0x406264()` | `0x406273` | P = (0, 0) (`0x481940`), R = `0x481920` (see the frame table) | the three-space view loop `0x4019c6` (`0x401a31`) and the mission screen (`0x44514d`, `0x4455ed`, `0x4456bc`, `0x44578b`, `0x44585a`, `0x445929`, `0x4459f8`) when `[0x46f3a4]` ≠ 0 |
| `0x406e87()` | `0x406f0e` | P = (0, 0), R = (0, 0, 640, 480) (one column and row too many; clamped in the DirectDraw branch) | `WM_PAINT` (`0x4057bf`, when `[0x46c274]` ≠ 0 and no movie plays), `WM_SETFOCUS` during a movie (`0x405468`), the private message `0x812` after a display change (`0x405650`) |
| `0x406277(rect)` | `0x40631f` | P = rect corner + view offset + S, R = rect moved by `[0x481920]` | no static caller and no pointer to it (dead) |

`0x406e87` is the only "full" present. `WM_PAINT` falls through to
`DefWindowProcA` after it (`0x4057e1`), which validates nothing more because
the present already ran `BeginPaint`/`EndPaint`. `WM_SETFOCUS` outside a
movie calls `0x42f905` instead of presenting; message `0x812` is posted on
`WM_DISPLAYCHANGE` outside DirectDraw and resizes the window before the
present ([vshell_process.md](vshell_process.md) section 7).

### The clear `0x4077ee`

Callers `0x4018d0`, `0x40199a`, `0x406bc1`, `0x40f656`, `0x41ee5b` (after a
movie), `0x431966`, `0x431c18`, `0x431cef`, `0x431d34`, `0x431dcd`
(complete list). GDI: `GetClientRect`, `InvalidateRect`, `BeginPaint`,
**the whole frame zeroed** (`memset([B+0xa], 0, [B+6])`), `StretchBlt(hdc,
0, 0, rc.right, rc.bottom, [0x48195c], 0, 0, 10, 10, SRCCOPY)` at
`0x40791f`, then — quirk — `SelectPalette(hdc, esi, TRUE)` with an `esi`
this function never set (the caller's value) and `RealizePalette` when
`[0x481954]` ≠ 0, `EndPaint`. DirectDraw: lock the primary, zero 480 rows of
640 bytes, `Unlock` at `0x407888`; **the frame buffer is not zeroed** in
this branch (asymmetry; whatever was drawn stays in the frame and reappears
with the next dirty present).

## Redraw model

**The display object** `[0x48118c]` (class `WinWin95Display`, `0x65d`
bytes, constructed at `0x401d71` by `0x41fcb7`, vtable `0x471904` at
object `+1`):

| Slot | Function | What it does |
|---|---|---|
| 0 | `0x41fe84(display, window)` | invalidate a window: the drawing context `[display+0x15]` (`0x239` bytes, ctor `0x452da0`) gets the window's rectangle (`window+0x15`) as clip rectangle, clip mode 1 and offset mode 1, and becomes current (`0x452e98` → `[0x48e264]`, viewport pointer `[0x47c444]`); then the rectangle is queued: skipped if a queued rectangle contains it, queued rectangles it contains are removed (swap with the last), else appended (`0x41fef9..0x420042`) |
| 1 | `0x42004b(display, rect)` | invalidate a rectangle: clip rectangle and offset mode as slot 0, clip mode unchanged; appended without merging |
| 2, 3 | `0x4200fc`, `0x420107` | get / set the cursor (`+0x11`); set calls `SetCursor` only when the value changes |
| 4, 5 | `0x420120`, `0x420152` | `SetCursorPos` / `GetCursorPos` with client↔screen conversion |
| 6, 7 | `0x420184`, `0x420190` | `ShowCursor(TRUE)` / `ShowCursor(FALSE)` |
| — | `0x41fe49(display)` | flush: present every queued rectangle through `0x41fe2c`, then count := 0 |

The queue is 100 rectangles of 16 bytes at `+0x19`, the count at `+0x659`.
Both append paths log an assertion (`0x44e560`, `wsrc\winwin95.cpp` lines
`0x63`/`0x6d`) at 100 entries and then still store at index 100, which is
the count field itself (quirk; whether `0x44e560` returns at that severity
is not followed).

**Who draws.** `0x41f3ed(window)` posts a draw event (type 4, `0x468db0`
→ `0x468780`) to a visible window and recursively to its children; the
event drain (`0x46848c` = timer poll `0x46a508` + drain `0x469ee4`) runs the
handlers; every draw handler seen starts with `0x41fbd9(self)` (find the
display of the window, `0x468678`, then display slot 0, so drawing is in
window-relative coordinates through offset mode 1), then draws into the
frame with the bitmap and text primitives. The flush in the same main-loop
iteration presents the result. Main-loop order
([vshell_process.md](vshell_process.md) section 8, `0x402044..0x4020d0`):
input pump `0x41e0bf`, event drain `0x46848c`, deferred palette install
`0x407c1b`, flush `0x41fe49`, movie queue `0x41e98c(1)`, pending screen
switch `0x4019c6`; the whole body runs only while the focus flag
`[0x46c288]` is set (`WM_SETFOCUS` sets it, `WM_KILLFOCUS` clears it,
`0x405443`/`0x40546d`): without focus nothing is presented.

Whether every write into the frame is preceded by an invalidation of its
rectangle (so that the screen always equals the frame after a flush) is a
**hypothesis**: it holds for the draw handlers followed here (`0x40cfcb`,
`0x40d0f0`, the bitmap panel `0x40bd22`), not checked for all widgets of
[vshell_widgets.md](vshell_widgets.md).

**The animated main-menu background** (`0x430fc4`, window `[0x48d1a0]`,
class ctor `0x40ce0c`, handler `0x40ce63`): six frames
`dbm\bay2a_80`, `_81`, `_82`, `_83`, `_84`, `_84` (the last image twice,
names at `0x4736e5..0x47373a`); on open it registers a timer with the timer
service `[0x5ddcb4]` (`0x46a3f0(service, window, 500, 500, 0)`, at most 20
timers); the service counts `GetTickCount` deltas down (`0x46a51e..0x46a5a3`)
and posts an event `0x200`, whose handler advances the frame index `+0x6d`
modulo the count `+0x71` unless the window flag `+0x11` bit 1 is set, and
draws it (`0x40cf31..0x40cf4f`). So one image every 500 ms, 84 for 1,000 ms
(from the arguments; the exact phase depends on when the drain
polls the service). The live capture shows no cycle at start: frames 80 to
84 and then the finished menu, each presented once, and no further change
([The tours](#the-tours)), as the own shell's `main_menu.real_animation`
draws them. The background's callback also shows the menu buttons at frame
index 5 ([audio.md](audio.md), [vshell_state.md](vshell_state.md)
`0x47374c`).

## Palette

### Loading and selection

- `0x407b62(n)` (signed 16-bit index, **no bounds check**): destroy the
  current palette object `[0x46de08]` (virtual `+0x18`, flag 3), load
  `0x46de14[n]` through the resource loader `0x4073b4`, store it in
  `[0x46de08]`, set the pending byte `[0x46de10]` := 1. Every selection
  re-reads the file; there is no cache.
- `0x407c1b()`: if pending, `0x45414c([0x46de08])` and pending := 0.
  `0x45414c` installs the colour ramps (`0x454223` → `0x457d88(base,
  count, records, field +6)`) and then the screen palette `0x4071fe`.
- The **realization gate** `[0x46c274]`: `0x4071fe` returns at once while it
  is 0 (`0x40720b..0x407212`). It is 0 at start, set to 1 when the shell
  reaches its first interactive screen (`0x4018a1`, `0x4018d5`,
  `0x40199f`, `0x401bed` for start modes 3 and 4) and cleared around the
  movie sequence `0x431c7b` (`0x431cd8` / `0x431d2a`). A palette installed
  while the gate is 0 has its ramps installed but never reaches the screen,
  and the pending byte is cleared anyway. `WM_PAINT` presents
  only while the gate is set.
- GLPalette object (reader `0x454334`, [dpl.md](../formats/dpl.md)): +4
  colour count, +6 the unknown 16-bit field of the file, +8 colour records,
  +0xc ramp count, +0xe ramp base, +0x10 ramp records.

### Start-up palette in entry modes 3 and 4

WinMain sets the gate for entry modes 3 and 4 at `0x401bd4..0x401bed`
before `0x401d37` calls `0x40796c(1)`, whose branch `0x4079f2..0x4079fa`
selects and applies palette 1. So with `-X3`/`-X4` palette 1 reaches the
table before the results path runs; in the menu modes it only clears the
pending byte. The killed path (`0x40f475..0x40f4a9`: `0x44d0e7(3)` over
`dbm\bay2a_84`) selects no palette of its own (the won path does,
`0x40f5cf`), so palette 1 is the only palette of the killed path's Replay
box. Listing: `0x401bed` precedes `0x401d39`; no `0x407b62`/`0x45414c` call
between the entry dispatch `0x4018a1` and `0x44d0e7`.

Live: `VSHELL.EXE dummy -eggplant -X3 -G` on a clone with a
killed-results state (`results.dat` 96 B), `__COMPAT_LAYER=256COLOR`,
captured with `PrintWindow`, shows the hangar and the Replay box in
palette 1's colours. The own shell applies palette 1 first in
`es2_main_menu_enter` (`shell/src/gui/main_menu.c`,
`//shell:main_menu_test` "entry"); headless on the same state its frame
matches the capture: 91.7 % of the pixels differ by less than 8 per
channel, 98.7 % by at most 24 (the rest is probably the compatibility
layer's 256-colour quantisation, not verified); the box text is legible.

### Scene bindings

All `0x407b62` call sites:

| Index | File | Selected by |
|---:|---|---|
| 0 | `dpl\intr_pt1.dpl` | no constant call site; reachable only through a movie record's palette word (below) |
| 1 | `dpl\palette.dpl` | start-up `0x4079f2` (applied at once; it reaches the table only in start modes 3 and 4, [above](#start-up-palette-in-entry-modes-3-and-4)), main menu after the intro `0x4018e1`/`0x4019ab`, `0x40f5cf`, after the movie `0x54` `0x41ef56`, `0x41efe8`, the service bay `0x43b8e7` (`0x43b8cd` show), bay screen word 3 (REPAIR) `0x43b84b` |
| 2 | `dpl\arming.dpl` | bay screen words 2 (WEAPONS), 4 (BUILD), 5 (ARMORY), 6 (CREW) (`0x43b840`, `0x43b856`, `0x43b861`, `0x43b86c`); `0x4447b7` (mission screen, group 5) |
| 3, 4 | `dpl\cam_er.dpl`, `dpl\cam_moon.dpl` | bay screen word 7 (MISSION), page `0x481108` = 0: index 3 for campaign groups 1..4, 4 for group ≥ 5 (`0x43b877..0x43b896`) |
| 5..9 | `dpl\br_w1..5.dpl` | MISSION page 1: 4 + group (`0x43b8a9`); mission screen `0x445121` (4 + group, applied at once) |
| 10..14 | `dpl\db_w1..5.dpl` | MISSION page 4 (debrief): 9 + group (`0x43b8c0`) |
| 15..18 | `dpl\alph.dpl`, `delt`, `omic`, `brav` | mission screen zone map `0x44472f`: group g ≤ 4 loads `dba\alph2`, `delt1`, `omic1`, `brav1` (table `0x478068`) and palette 14 + g (`0x44482d`) |
| 19 | `dpl\luna.dpl` | no constant or derived call site found (group 5 takes the movie branch `0x44474f..0x4447c9` instead) |

The bay's selector is `0x43b7f2(screen word)` (jump table `0x43b80a`):
words 0 and 1 change nothing, 8 (bay) hides the frame `[0x48d520]`; the
palette goes to the bay background screen `[0x48d524]` through `0x43a430`,
which stores the index at `+0x45` and re-shows the screen, whose open event
(`0x40d067`, type 2) loads and applies it (`0x40d08e..0x40d099`). The
campaign group is `[0x46fc58]`, the mission-screen page `0x481108`
([vshell_ui.md](vshell_ui.md) §4.6, §4.8).

Movie records (`0x41e8c0`, ring `0x485748`, 28 bytes each) carry a palette
word at `+0x12`; the player loads it before the movie unless it is −1
(`0x41eaf7..0x41eb3d`). All constant enqueues pass −1; the mission-screen
enqueues (`0x444aa5`, `0x444d10`, `0x444e02`, `0x445527`, `0x44556a`) pass
computed values (not followed here; [vshell_ui.md](vshell_ui.md) §19).

**Never named by VSHELL** (no string, no format, the `dpl` extension pointer
`[0x46f118]` has no reader): `map`, `map_pal1`, `map_pal2`, `bay`, `alpha`,
`delta`, `omicron`, `bravo`, `br_mn`, `br_er`, `db_moon`, `db_er`, `esii`
(13 of the 33 SHELL0 DPLs; names from the VOL directory).

### From DPL to screen: `0x4071fe(P)`

`0x4071fe..0x4073b3`:

1. Gate check (above); a debug counter and log line ("… realize_palette").
2. For i = 0..255, record `[P+8] + 4i` = (r, g, b, flags): flags bit 0 →
   local RGBQUAD i = (b << 2, g << 2, r << 2) as bytes (`0x407248..0x407267`,
   blue first); else bit 1 → the unscaled bytes in the same order
   (`0x407270..0x407294`); else **the entry is not written**. The fourth
   byte is never written. The local buffer is uninitialised stack
   (`0x40716d`), so both are residue (all CD DPLs have flags 1, dpl.md).
3. `0x40716a`: a 256-entry logical palette of black `PC_RESERVED |
   PC_NOCOLLAPSE` entries (header copied from `0x46d8b4`) is selected and
   realized on the screen DC, the old one selected back, the black one
   deleted: the system palette is flushed to black before every install.
4. `0x406f40(quads, 256, P)` builds the LOGPALETTE at `0x481f50` (version
   `0x300`, 256 entries at `0x481f54`) on `GetDC(0)` and returns
   `CreatePalette` of it:
   - `GetSystemPaletteUse` = 2 (`SYSPAL_NOSTATIC`): entry i = (red, green,
     blue) from quad i, flags 5 (`PC_RESERVED | PC_NOCOLLAPSE`)
     (`0x406f80..0x406fa0`); then entry 255 := white, entry 0 := black,
     both flags 0.
   - otherwise: `n = GetDeviceCaps(NUMCOLORS)`, all 256 system entries
     read, `r = n / 2` (towards zero; −1 gives 0); entries 0..r−1 and
     256−r..255 keep the system colours with flags 0; entries r..255−r get
     the quad's colour, flags 5, and when such an entry equals one of the
     static entries in all three components its blue is moved by +3 (or −3
     from 252 up) and the comparison continues with the new value
     (`0x407042..0x407121`).
5. The new handle goes to `[0x481954]` (the previous one is never deleted:
   one GDI palette leaked per install), is selected and realized on
   `GetDC(0)` as foreground palette, and **`SetDIBColorTable([0x48195c], 0,
   256, quads)`** at `0x4072f3` gives the DIB the step-2 table, without
   static colours and without the blue nudge.
6. DirectDraw mode only: entries 10..245 of `0x481f54` := the quads in
   RGB order with flags 5, `CreatePalette(0x4c, 0x481f54, &[0x481f48])`
   (the previous DirectDraw palette is not released), on failure an
   assertion ("create_pal failed", `src\WINSTUFF.CPP`), on success
   `SetPalette` on the primary.

**What reaches the screen** (hypothesis, the GDI side is Windows'): on a
true-colour desktop the `BitBlt` from the DIB uses the DIB colour table, so
all 256 indices show DPL × 4. On a palette device (the 256COLOR emulation)
the DIB colours are matched to the realized logical palette, so indices
0..9 and 246..255 show the system's static colours and a colliding entry
shows its nudged blue. The capture tour hashes the DIB table only, which is
the same in both configurations ([The tours](#the-tours)); the logical
palette was not captured.

**Palette messages** (window procedure): `WM_QUERYNEWPALETTE`
(`0x405709`) and `WM_PALETTECHANGED` (`0x40565a`, ignored when the window
itself caused it) go to `DefWindowProcA` in DirectDraw mode or while a
movie plays; otherwise they set the flags of entries **10..245** of
`0x481f54` to 5 resp. 0 (fixed range, not the computed r), create a new
palette into `[0x481954]` (leak), select and realize it on the window DC,
select the old one back and realize, and `InvalidateRect(hwnd, NULL, TRUE)`
when entries were remapped. Nothing presents in reply except the resulting
`WM_PAINT`.

**Movies** (context only, [vshell_ui.md](vshell_ui.md) §19): `0x41e48c`
builds an MCI palette `[0x485744]` from entries 10..245 of `0x481f54`
(`0x406f15` copies them), `0x41e4cc` an alternative of explicit indices
10..245; the MCI child window gets it with "setvideo … palette handle"
(`0x41e708`). While a movie plays (`[0x470eb0]` = 1, `0x41e786`)
`WM_PAINT` presents nothing.

## Bitmaps and blit primitives

**Bitmap objects** (GLBitmap, reader `0x453ef8`, [dbm.md](../formats/dbm.md)):
`+4` the 14-byte blitter header (height, width, depth, flags, size, pixel
pointer at `+0xe`), `+0x12` the compression kind. The blitter header is the
same layout as the frame header `[0x481948]`.

**Draw entries** (each decompresses first when `+0x12` ≠ 0, draws, and
frees the buffer):

| Entry | Low level | Callers | Notes |
|---|---|---|---|
| `0x4537e8(bmp, &pt, mirror)` | `0x458ee4` | `0x40bd40` (bitmap panel, mirror 0), `0x40c033` (multi-layer panel, mirror from the layer record `+0x89`), `0x427385`, `0x427486`, `0x453e3a` (0) | the plain draw |
| `0x453874` | `0x458fd0` (draw a source sub-rectangle) | none | — |
| `0x453908` | `0x4591b8` → driver slot 29 `0x45b4c0` | `0x422580`, `0x427368` | scaled draw (divides the size by integer factors); not followed |
| `0x45399c` | `0x4592a8` | `0x425d91` | polygon/rotated draw; not followed |

**Decompression `0x453a50(bmp)`**: U = the first dword of the
stored data (`0x453a65..0x453a68`; `0x453a68` is the `mov` that faults on a
stale object, [media_compatibility.md](../reference-game/media_compatibility.md#mission-map-lifetime-guard)
and [vshell_process.md](vshell_process.md) section 11; it is reached only
when the object's kind word is nonzero), `[0x48e26c]` := a new U-byte
buffer, a stream over the remaining `P − 4` bytes, kind 1 RLE (`0x44f5d8`)
or kind 3 LZH (`0x44f908`), any other kind returns 0; `0x4549da` reads
exactly U bytes; the reader object is destroyed; returns U. The draw entry
swaps U and the buffer into the header for the draw and restores them. A
preset-buffer mode (`[0x48e268]` ≠ 0: return 0 at once and draw from
`[0x48e26c]`) exists, but its setter `0x453a38` has no caller. All CD
images are kind 0 (dbm.md), so on the CD data this path runs only for
objects whose kind word is not a real one.

**Viewport and clipping** (`[0x47c444]` = drawing context `+4`,
`0x458ee4..0x458fc8`): `+0x208` = 1 adds (`+0x20c` − `+0x21c`) to the
destination (window-relative coordinates); `+0x204` = 0 draws clipped to
the frame (driver slot 26 `0x45a09c`, no rectangle); 1 clips to the
rectangle at `+0x20c` (slot 28 `0x45a5ce` → `0x45a09c`); 2 walks a list of
clip rectangles or span lists (`0x45a5fc..0x45a668`). Layout nibble 1 of
the bitmap flags goes to `0x459508` instead (dbm.md: no CD image uses it).

**The blitter `0x45a09c(B, &pt, clip, mirror)`**:

- Clipping with an inclusive rectangle (l, t, r, b): nothing when x > r,
  y > b, x + w ≤ l or y + h ≤ t; drawn span x0 = max(x, l) … min(x + w,
  r + 1), likewise in y (`0x45a0b7..0x45a127`). Without a rectangle the
  bounds are [0, 640) × [0, 480) (`0x45a12c..0x45a19c`).
- `mirror & 3` through the jump table `0x45a1b3`: 0 normal
  (`0x45a1c3`), 1 vertical flip (`0x45a205`: destination rows written
  bottom-up), 2 horizontal mirror (`0x45a255`: source read right to left),
  3 both (`0x45a2a1`). The skipped margins are taken from the mirrored side
  of the source, so a clipped mirrored image is the clip of the mirrored
  image.
- Transparency: header flag `0x10` set → every byte copied (`rep movsd`/
  `movsb` for modes 0 and 1, byte loop for 2 and 3); clear → **index 0 is
  skipped** (`0x45a31d..0x45a336`, `0x45a33a..0x45a353`). There is no other
  colour key and no blending.
- Destination stride is `[0x47c4a6]` (640), source stride the bitmap width.

**Related primitives:** slot 27 `0x45a35e` is the reverse copy (frame →
bitmap, same clipping and mirror table, opaque), used by `0x459080`, which
first zeroes the bitmap; its only caller is `0x40b74d` (a widget saving the
area under itself, [vshell_widgets.md](vshell_widgets.md)).
`0x4537a4(B, table)` remaps a bitmap's pixels in place through a 256-byte
table (no direct caller found; it is not the font colour remap: text colour
is a rectangle fill, [vshell_text.md](vshell_text.md)). `0x454180` finds the
palette entry nearest to a packed 6-bit RGB colour; both depend on the
installed palette. Fonts draw through the same blitters
([vshell_text.md](vshell_text.md)).

## DirectDraw (the full-screen mode)

All DirectDraw use is in `0x406bfb`, `0x4071fe`, `0x407466..0x4077de` and
`0x4077ee`; the only import is `DirectDrawCreate` (thunk `0x46bc88`, IAT
`0x5de4dc`). Objects: DirectDraw `[0x481f3c]`, primary `[0x481f40]`, back
buffer `[0x481f44]` (never created), palette `[0x481f48]`.

| Call | Site | Arguments / result |
|---|---|---|
| `DirectDrawCreate` | `0x40746f` | (NULL, `&[0x481f3c]`, NULL); ≠ 0 → fail |
| `SetCooperativeLevel` (`+0x50`) | `0x40748c` | (hwnd, `0x17` = FULLSCREEN, ALLOWREBOOT, NOWINDOWCHANGES, EXCLUSIVE); no ALLOWMODEX, unlike DBSIM's `0x57` |
| `SetDisplayMode` (`+0x54`) | `0x4074b3` | (640, 480, 8) |
| `Release` (`+0x08`) | `0x4074d4..0x40750e`, teardown `0x4075c2` | back buffer, primary, palette before re-creation; all four at teardown |
| `CreateSurface` (`+0x18`) | `0x407566`, retry `0x407588` | `dwSize` `0x6c`, `dwFlags` 0 and back-buffer count 0 (both first set to `0x20`/2 and then overwritten, `0x40752f..0x40754f`), caps `0x4200` (PRIMARY, VIDEOMEMORY), no `DDSD_CAPS` (quirk, as in DBSIM); retry without VIDEOMEMORY; failure → assertion line `0x34d` |
| `CreatePalette` (`+0x14`) | `0x407719` (enter), `0x407363` (install) | (`0x4c`, `0x481f54`, `&[0x481f48]`, NULL) |
| `SetPalette` (`+0x7c`) | `0x40772e`, `0x4073ac` | (`[0x481f48]`) |
| `IsLost`/`Restore`/`Lock`/`Unlock` | present `0x406c23..0x406d67`, clear `0x407810..0x407888` | see above |

**Enter** (`0x407636` when `[0x481f4c]` = 0): flag := 1, create, mode,
surface; on failure `0x40683c` (DIB and timer freed) and
`PostQuitMessage(0)`; `SetWindowPos(HWND_TOPMOST)` to the frame-adjusted
rectangle of 640 × 480 at (−frame, −frame − caption), `SWP_NOREDRAW |
SWP_NOCOPYBITS`; flags of entries 0..9 and 246..255 := 5; palette;
`ClipCursor(0, 0, 640, 480)`, `SetCursorPos(320, 240)`. **Leave** (the same
function when the flag is 1): flag := 0, flags 0..9/246..255 := 0, release
everything, `SetWindowPos(HWND_NOTOPMOST)` centred, `SWP_NOACTIVATE`; the
cursor clip is **not** released (quirk; hypothesis that Windows drops it
on the next activation change). `0x4077df` leaves only when in DirectDraw.

**Who toggles** (callers): WinMain when `prefs.cfg` byte 6
(`[0x4825a2]`, copied to `[0x46d878]`) is set (`0x406bbc`; the `-d`/`/d`
argument that clears `[0x46d878]` at `0x406b1d` is overwritten by the
prefs byte at `0x406b34` before use, quirk); the Preferences' Window and
Full Screen controls and CANCEL's re-synchronisation (`0x4375b1`,
`0x437687`, `0x43725a`); message boxes are wrapped in leave/re-enter
(`0x401c40`/`0x401c5c` colour warning, `0x42f46d`/`0x42f48e`,
`0x4318af`/`0x4318cb`); leave before exiting (`0x401c77`, `0x401cec`,
`0x401f9d`); leave **and store prefs byte 6 := 0** (`0x40ddfe(6, 0, 1)`):
the keyboard handler's system-key combinations (`0x4052c5`, key words
`0x20f`, `0x401`, `0x201` = Alt or Ctrl plus a mapped key, presumably
Alt+Tab, Ctrl+Esc, Alt+Esc as DBSIM's `0x4669b4`;
[vshell_widgets.md](vshell_widgets.md) section 7), which also redraw a
visible Preferences screen `[0x48d388]`; the movie error box `0x41ed88`
and the help `0x431e82`, both of which also commit and write `prefs.cfg`.
`0x42f987` releases the objects when the preference is set; it lies in
`0x42f960`, which [vshell_process.md](vshell_process.md) sections 3 and
12 show to be a dead argv builder (no caller), so this release never runs.

## Cursor and busy cursor

- The visible cursor is always the Windows cursor: the window class has
  `IDC_ARROW` (`0x4068ad`), `WM_SETCURSOR` is left to `DefWindowProcA`,
  and no code draws a cursor into the frame or a surface (for the
  present paths; the `GLCursor` class of the engine library is not used by
  them). The cursor is never part of a frame hash.
- **Busy cursor** `0x408d2f(busy)` (stdcall, one argument):
  `SetCursor(LoadCursor(NULL, busy ? IDC_WAIT : IDC_ARROW))`, then always
  `ShowCursor(TRUE)` (the show count only grows). Callers: `0x41ece0`
  (busy) and `0x41e77b`, `0x41ee62` (arrow) around movies; `0x431a85`
  (busy) and `0x431ad8` (arrow) around CONTINUE GAME's load
  ([vshell_ui.md](vshell_ui.md) §4.1). Hypothesis: the next mouse move in
  the client lets `DefWindowProcA` restore the class arrow, so the wait
  cursor shows only while no messages are pumped.
- Start-up stores `GetCursor()` in two `WinWin95Cursor` objects
  `[0x481184]`, `[0x481188]` (`0x41fc98`) and passes the **object pointer**
  of the first to display slot 3 (`0x401e3c..0x401e4b`), which hands it to
  `SetCursor` as if it were a handle (quirk; hypothesis: rejected by
  USER32, no visible effect).
- DirectDraw mode clips the pointer to (0, 0)–(640, 480) and centres it on
  entry (above).

## What the 256COLOR layer changes

From the bytes, VSHELL's behaviour depends on the display through these
reads only (sites; the values are **hypotheses** for the two
configurations):

| Read | Site | Effect |
|---|---|---|
| `GetDeviceCaps(BITSPIXEL)` (called twice, the first result unused) | `0x401c1f`, `0x401c27` | ≠ 8 → the colour warning box (`error.str` message 3, caption 2, `0x401a81`), only for start-mode words 0 and 1 (`[0x482362]`, [vshell_process.md](vshell_process.md) section 4 step 11). With 256COLOR: 8, no box ([media_compatibility.md](../reference-game/media_compatibility.md)); without it the box is a warning only (live, [The tours](#the-tours)) |
| `GetSystemPaletteEntries` | `0x4064ae` | the DIB's initial colour table (until the first install) |
| `GetSystemPaletteUse`, `GetDeviceCaps(NUMCOLORS)`, `GetSystemPaletteEntries` | `0x406f6c`, `0x407006`, `0x40701c` | the logical palette: with 256COLOR presumably `SYSPAL_STATIC` and 20 static colours (r = 10: statics kept at 0..9/246..255, blue nudge); on a true-colour desktop `NUMCOLORS` = −1 → r = 0 (all 256 entries from the DPL, no nudge) |
| palette messages, `RealizePalette` | `0x405709`, `0x40565a`, the present | matter only on a palette device |

The DIB colour table and the indexed frame are the same in both
configurations (live: every snap of the capture tour has the same
index and colour-table hashes with and without the layer); only the
conversion to screen colours differs. DirectDraw mode sets its own 8-bit
display mode and does not depend on the layer (hypothesis). The launchers
set 256COLOR for the whole process tree (`setup/start-native.ps1`,
`-UseNativeColors` turns it off); the own launcher leaves it out of the
replacement shell's environment ([launcher.md](launcher.md) "The
replacement shell without the 256COLOR layer").

## Present sites

All addresses are code of the reference image; the seams are VSHELL's own
import slots (the proxy patches import slots of the executable as for MCI,
[media_compatibility.md](../reference-game/media_compatibility.md)).

| What | Call site (return) | Seam | State at the call |
|---|---|---|---|
| **present (GDI)** | `0x406e52` (`0x406e57`) in `0x406bfb` | IAT `BitBlt` `0x5de3f0` (thunk `0x46bb38`, `jmp [slot]`; the only `BitBlt` call) | stack: (hdc, P.x, P.y, w + P.x, h + P.y, `[0x48195c]`, R.left, R.top, `0xcc0020`); `esi` = hdc; EBP = frame of `0x406bfb`: `[ebp+4]` = site (`0x41fe3d` dirty flush, `0x406273` page present, `0x406f0e` full present), `[ebp+8]` = &P, `[ebp+0xc]` = &R, client RECT at `[ebp−0xd4]`; for the dirty flush the loop site is two frames up: `[[ebp]+4]` = `0x41fe6f` (in `0x41fe49`), `[[[ebp]]+4]` = `0x4020a1` (main loop), `0x4022ab` (settle loop) or `0x40d198` (screen draw event) |
| pixels | — | — | `[0x481950]`, 640 × 480, stride 640 (`s16 [[0x481948]+2]`), top-down; the frame is complete at every present |
| **palette (DIB table)** | `0x4072f3` (`0x4072f8`) in `0x4071fe` | IAT `SetDIBColorTable` `0x5de3f8` (thunk `0x46bb44`; the only call) | (`[0x48195c]`, 0, 256, quads): blue, green, red, **residue** — hash the three colour bytes only |
| palette (logical) | `0x40715f` in `0x406f40`; `0x4056a9`, `0x40574b` (palette messages) | IAT `CreatePalette` `0x5de3e4` | LOGPALETTE at `0x481f50`; the result becomes `[0x481954]` (also `0x4071c2`: the black flush palette, and `0x41e4ba`/`0x41e509`: movies — distinguish by return address) |
| **clear (GDI)** | `0x40791f` (`0x407924`) in `0x4077ee` | IAT `StretchBlt` `0x5de3f4` (thunk `0x46bb3e`; the only call) | the frame was zeroed just before |
| present / clear (DirectDraw) | `call [ecx+0x80]` `0x406d67`, `call [edx+0x80]` `0x407888` | the wrapped primary's `Unlock` (proxy observer) | the copied rectangle is in the surface; the frame buffer holds the same pixels for the present (not for the clear) |

**Keys.** VSHELL has no frame counter and its main loop runs as fast as
messages allow. A capture record carries an own sequence number, the site
and loop site above, P and R, the journal step (the number of input-pump
calls `0x41e0bf` so far) and an ordinal within that step, the upload count
of `SetDIBColorTable`, and the DirectDraw flag `[0x481f4c]`. Hashes:
SHA-256 of the 307,200 frame bytes (the whole frame, not only R: the screen
tests compare whole frames) and of the 768 colour bytes of the last DIB
table. One flush presents several rectangles; a record per *flush* (hash
once, at the last rectangle, or when the loop site changes) keeps the
volume low.

**Signatures to check before chaining** (bytes of the reference image):
thunks `0x46bb38` `ff 25 f0 e3 5d 00`, `0x46bb3e` `ff 25 f4 e3 5d 00`,
`0x46bb44` `ff 25 f8 e3 5d 00`, `0x46bb26` `ff 25 e4 e3 5d 00`; the
prologue of `0x406bfb` `55 8b ec 81 c4 2c ff ff ff 53 56 57` (frame size
`0xd4`, the client RECT offset depends on it); the calls `0x406e52`
`e8 e1 4c 06 00`, `0x4072f3` `e8 4c 48 06 00`, `0x40791f` `e8 1a 42 06 00`;
`0x41fe2c` `55 8b ec 53 8b 45 0c 8b 5d 08 50 50 e8 be 6d fe ff`; the
DirectDraw `Unlock` calls `0x406d67` `ff 91 80 00 00 00`, `0x407888`
`ff 92 80 00 00 00`; and the module hash, as
`mission_map_guard.h` does.

**What a capture must not do:** post or send messages to the shell
window, pump messages, resize or focus it (the focus flag `[0x46c288]`
stops the main loop body, a resize produces `WM_PAINT` and a full present),
or call into VSHELL code. Reading game memory and hashing on the main
thread inside the present costs wall time only.

## Keep-list for an own presenter

What an own shell must reproduce for the picture and behaviour to be
VSHELL's; everything else in this document is VSHELL's platform plumbing.

1. **The frame:** 640 × 480 indices, top-down, drawn with VSHELL's
   primitives: the clip semantics (inclusive rectangles, frame bounds when
   none), the four mirror modes, index 0 transparent unless the bitmap's
   flag `0x10` is set, decompression per draw (kinds 0, 1, 3).
2. **The colours: the DIB table as uploaded** (DPL × 4 per component,
   flags bit 0; bit 1 unscaled; other entries undefined). An own presenter
   converts index → RGB through that table. Showing the 20 static system
   colours and the blue nudge would be the 256COLOR/palette-device look;
   the own shell does not reproduce it (deviation 3 below).
3. **When a palette changes:** a selection takes effect at the next
   `0x407c1b` (main loop, before the flush) or at once where the caller
   applies it immediately (`0x4079fa`, `0x40d099`, `0x41eb3d`,
   `0x44513e`); not at all while the realization gate is 0. The scene
   bindings in the table above. The flush to black before an install
   matters only on palette devices (hypothesis).
4. **When the screen changes:** after each main-loop iteration (flush),
   on `WM_PAINT` (whole frame), on clear (black, frame zeroed in GDI mode),
   never while the window has no focus or while a movie plays. An own
   presenter may present the whole frame per iteration instead of the
   rectangles only if the frame never holds unflushed drawing at a flush
   (hypothesis above).
5. **Timing:** the main-menu background's 500 ms timer; the timer service
   reads `GetTickCount`.
6. **Cursor:** the system arrow, the wait cursor through `0x408d2f`'s
   callers, no software cursor.
7. **Geometry:** 1:1 in a 640 × 480 client. Integer scaling and centring
   in full screen are a platform deviation (VSHELL never scales the
   picture, although its mouse path scales input to the client size); the
   mouse must map to frame coordinates as `0x405b6c` does.
8. **Full screen:** VSHELL's full screen is an exclusive 640 × 480 × 8
   DirectDraw mode with the cursor clipped; an own shell can only offer a
   borderless full-screen window (platform deviation, like
   [presentation.md](presentation.md)'s keep-list item 9). Message boxes
   leave full screen and come back.

Not on the list: the GDI palette objects and their leaks, the DirectDraw
objects, the dead scale code, the BitBlt over-size, the stale `esi` in the
clear, the cursor object passed to `SetCursor`.

## Capture tour

The proxy's VSHELL capture records VSHELL's presents and colour tables and
replays an input journal. Code: `tools/reference-game/proxy/vshell_capture.h`
(hooks, records, guard), `vshell_journal.h` (journal parser and scheduler),
`setup/capture-vshell-screens.ps1` (the tour on a clone), journals in
`tools/reference-game/input-scripts/vshell/`. Tests:
`//tools/reference-game/proxy:vshell_capture_<mode>_test` (synthetic image,
no game) and the lane `lane_vshell_screens` of
`//tools/reference-game/setup:suite_scripts_test` (mocked clone, the clone
guard's mock game as `VSHELL.EXE`).

### Capture guard

The proxy changes nothing unless the environment variable
`ES2_VSHELL_CAPTURE_CONFIG` names an INI **and** the process is `VSHELL.EXE`;
the DBSIM oracle runs and every other use of `ddraw.dll` stay unchanged.
Then `DllMain` (kernel32 only, no file I/O) checks the image base
`0x400000`, the image size `0x1ea000`, the seven import thunks
(`ff 25 <slot>`), the code bytes of [Present sites](#present-sites), the
pump's `PeekMessageA` call (`0x40636e..0x406382`), the three `GetKeyState`
calls (`0x405230..0x405262`), the `GetCursorPos` call
(`0x42015c..0x420164`) and the three `SetCursorPos` calls (`0x405bef`,
`0x407757`, `0x420143`), and replaces seven import slots:

| Import | Slot | Thunk | Site (return) | Use |
|---|---|---|---|---|
| `BitBlt` | `0x5de3f0` | `0x46bb38` | `0x406e52` (`0x406e57`) | present record |
| `StretchBlt` | `0x5de3f4` | `0x46bb3e` | `0x40791f` (`0x407924`) | clear record |
| `SetDIBColorTable` | `0x5de3f8` | `0x46bb44` | `0x4072f3` (`0x4072f8`) | colour table (only for the memory DC `[0x48195c]`) |
| `PeekMessageA` | `0x5de414` | `0x46bb68` | `0x40637a` (`0x40637f`) | pump pass count and injection |
| `GetKeyState` | `0x5de440` | `0x46bbaa` | `0x405232`, `0x405243`, `0x405254` | journal modifiers |
| `GetCursorPos` | `0x5de448` | `0x46bbb6` | `0x420160` (`0x420165`) | journal pointer |
| `SetCursorPos` | `0x5de49c` | `0x46bc34` | `0x405bf0`, `0x407758`, `0x420149` | journal pointer warps |

Slots, thunks and the single call of each import are read from the import
table and the llvm-objdump listing of the reference `VSHELL.EXE`. The
globals the hooks read (`[0x481950]`, `[0x48195c]`, `[0x481f38]`,
`[0x481f4c]`, `[0x46c288]`, `[0x470eb0]`) are dwords in every access of the
listing; `[0x46c288]` is 1 in the image. At the first hooked call the INI
is read and the SHA-256 of the executable file is compared with
the reference hash; a mismatch, an unusable log file or a bad journal leaves
every hook forwarding (record `refused`). Each hook calls the previous slot
value. The executable file is never changed; game memory is only read.

### Configuration

Section `[vshell_capture]` of the INI:

| Key | Default | Meaning |
|---|---|---|
| `log` | (required) | JSONL record file, created new; an existing file refuses |
| `capture` | 1 | present, clear and palette records |
| `every` | 0 | 1: a record per present; 0: only when the frame or colour-table hash changed |
| `dump_dir` | empty | raw frames `<index_sha256>.idx` (307,200 bytes) and tables `<palette_sha256>.rgb` (768 bytes RGB), once each; only into an ignored directory |
| `journal` | empty | input journal to replay |
| `hold_focus` | 1 | with a journal: while the focus flag `[0x46c288]` is 0, inject `WM_SETFOCUS` (at most once per 50 passes) |
| `block_real_input` | 1 | with a journal: drop real `0x100..0x109` and `0x200..0x20e` messages (also the `WM_CHAR` that `TranslateMessage` makes of an injected key, and the mouse moves Windows posts after VSHELL's own `SetCursorPos`) |

### Journal

JSONL, one flat object per line. Ops: `mark` (screen/state labels for the
records), `wait` (`passes` and/or `ms`, both must be reached), `snap` (hash
the frame now), `click` (`x`, `y` in frame pixels, or `control` = a
`vshell_layout.json` id that the script resolves to the centre of its
`screen_rect`), `key` (`vk`, `mods` bit 0 Shift, 1 Ctrl, 2 Alt), `close`
(`WM_CLOSE`). A *pass* is one call of the pump `0x406328`, ended by
`PeekMessageA` returning 0; only calls from the pump's site are counted or
served. At the first `PeekMessageA` of a pass the scheduler hands out that
pass's messages (hwnd `[0x481f38]`):

- click: pass A `WM_MOUSEMOVE` to (639,479), (0,0), (x,y)
  ([vshell_widgets.md](vshell_widgets.md) §7.6 homing); two passes later
  `WM_LBUTTONDOWN` (`MK_LBUTTON`), two passes later `WM_LBUTTONUP`, both
  at (x,y);
- key: `WM_KEYDOWN` (`WM_SYSKEYDOWN` with Alt; lParam repeat 1 and the scan
  code), two passes later the key-up form (bits 30/31 set);
  `GetKeyState(VK_SHIFT / VK_CONTROL / VK_MENU)` returns `0x8000` for the
  entry's modifiers until the next `PeekMessageA`, 0 otherwise;
- click, key, snap and close wait for a pass with focus flag ≠ 0 and movie
  flag `[0x470eb0]` = 0.

**Pointer.** VSHELL posts every mouse event at the position `GetCursorPos`
returns in the device poll (display slot `0x420152`, called from
`0x4689c1`; [vshell_widgets.md](vshell_widgets.md#33-posting-0x468780-and-the-queue)),
not at the message's lParam. With a journal the proxy therefore keeps a
virtual pointer in client coordinates: an injected mouse message
(`0x200..0x209`) moves it to its lParam, the three `SetCursorPos` sites
move it (record `warp`, the real pointer stays), and the `GetCursorPos`
site reads it back mapped to the screen with `ClientToScreen`; before the
first injected move or warp the real pointer is read. Other callers of
either import are forwarded.

**Deviations from a person at the keyboard:** the messages never pass
through the system queue; real input is dropped; focus is held by an
injected `WM_SETFOCUS` (the window procedure's own handler sets the flag
and restarts the sound, `0x405455`), not by the window manager.

### Records

`capture.jsonl`: every record has `kind`, `seq`, `pass`, `step` (journal
entry index), `screen`, `state`. Kinds: `start` / `refused` (configuration,
`module_sha256_ok`, `journal_entries`: negative = the bad line), `palette`
(`start`, `count`, `uploads`, `palette_sha256`), `present` and `clear`
(`site`, `dst` = BitBlt or StretchBlt destination x, y, w, h, `src` =
source corner, `presents` since the previous record, `uploads`, `dd` =
`[0x481f4c]`, `focus`, `index_sha256`, `palette_sha256`), `snap` (the same
hashes), `pointer` (after a snap: the virtual pointer x, y, once one is
known), `warp` (`site`, x, y), `step` (op, journal line, x, y, vk, mods),
`focus_injected`, `journal_done`, `end` (totals, with `dropped`,
`pointer_reads`, `pointer_real_reads`, `pointer_warps`). `index_sha256` is
the SHA-256 of the whole 640 × 480 frame at `[0x481950]`; `palette_sha256`
of the 256 RGB triples of the last uploaded DIB table (null before the
first upload: the start-up table from the system palette is not uploaded
through `SetDIBColorTable`), the same form as DBSIM's capture
(`presenter.h`). Full-screen (DirectDraw) presents are not captured; `dd`
shows the mode.

### The tour script

```powershell
pwsh -File tools\reference-game\setup\capture-vshell-screens.ps1 -InstallName calibration-r111 [-Run game_r] [-Dump] [-Every] [-NativeColors] [-ConfirmNoticeSeconds N] [-RunTimeoutSeconds 600] [-DryRun]
pwsh -File tools\reference-game\setup\capture-vshell-screens.ps1 -RestoreOnly <session>
pwsh -File tools\reference-game\setup\capture-vshell-screens.ps1 -SummarizeOnly <session>
```

It refuses `native-r111`, a running game of the clone, another session's
clone lock, and a `VSHELL.EXE` that does not match the clone's
`installation-manifest.json`; the clone's `ddraw.dll` must be a proxy build
with the capture (deployed by `check.ps1 -Live` or `configure-media.ps1`;
a proxy without the capture writes no capture log and the run's status is
`no-capture-log`). Steps: back up `DATA` and `SAV` into
`<DataRoot>\logs\vshell-screens-<stamp>\backup` (ignored, digests in
`restore.json`); mount the CD image and write `DATA\DRIVE.CFG` as
`start-native.ps1` does; per run of `input-scripts/vshell/tour.json`: put
`DATA` and `SAV` back to the backup, copy the run's start-state files
(inside the clone, from the backup), write `runs\<run>\capture.ini` and the
resolved `journal.jsonl`, start `VSHELL.EXE <arguments>` (visible window,
256COLOR unless `-NativeColors`) with `ES2_VSHELL_CAPTURE_CONFIG`, wait for
it to end, and after the timeout stop only its own process (PID, path,
start time); finally restore `DATA` and `SAV` (always) and write
`frames.json`.

`-ConfirmNoticeSeconds N` looks up to N seconds for a dialog-class window
(`#32770`) of the run's own process and presses its button once
(`WM_COMMAND` with ID 2: an OK-only message box gives its one button that
ID, a posted ID 1 leaves it open; the mock game's box,
`clone_guard_mock_game box`, returns IDOK either way, which
`//tools/reference-game/setup:suite_scripts_test` checks), recorded as
`notice` in each `run.json`. It answers the colour-depth box that VSHELL
shows without the 256COLOR layer, whose modal loop does not run through
the pump and so cannot be answered by the journal.

Runs of `tour.json` (all with `dummy -eggplant -X1 -a`, i.e. no movies):
`game_r` (the clone's GAME_R: main menu, Preferences and Cancel, Saved
games and Exit, CONTINUE GAME → service bay, WEAPONS, MISSION with the
`opened` snap, back to WEAPONS and MISSION again for page 1 with
Objectives, Intelligence and Briefing ([vshell_ui.md](vshell_ui.md)
§41.3), MAIN MENU, QUIT), `game_t` (GAME_T and its slot-11 files as slot
10, CONTINUE), `new_career` (START NEW GAME → Registration, snapped 2750
ms after the click, in the middle of an off phase of the caret's 500/500
ms blink timer `0x40c45f`). The `results` run (debrief page, entry `-X3`)
is listed but disabled: it needs a results-path state from a finished
mission.

### `frames.json` (schema 1)

```
{ "schema": 1, "kind": "es2-vshell-screens", "created_utc", "session", "installation",
  "vshell_sha256", "proxy_sha256", "compatibility_layer", "tour",
  "runs": [ { "name", "start_state", "copy", "arguments", "journal", "journal_sha256",
              "sav_at_launch": {file: sha256}, "process": {status, pid, exit_code, stopped_by_runner, ...},
              "status": "complete" | "journal-incomplete" | "no-end-record" | "refused" | "no-capture-log",
              "capture_start", "capture_end", "counts": {present, clear, palette, snap, step, focus_injected},
              "screens": [ {run, screen, state, step, index_sha256, palette_sha256} ],
              "frames":  [ {seq, kind, pass, step, screen, state, site, dd, index_sha256, palette_sha256} ] } ],
  "screens": [ every snap of every run: the references of the screen tests ] }
```

Only hashes, labels and counts; frames only with `-Dump`, in the session's
ignored `private\dumps`. The session directory lies in `es2-data` (`frames.json`
is kept there, the dumps never).

### The tours

Live results on the clone `calibration-r111`:

| Layer | Setup | Result |
|---|---|---|
| 256COLOR | proxy without the pointer hooks | every snap the main menu, the clicks land at the real pointer |
| 256COLOR | proxy with the pointer hooks | 16 snaps, every screen as labelled, the same 16 `index_sha256`/`palette_sha256` pairs in repeated runs; `game_r` ends by its QUIT click (exit 0, status `journal-incomplete`: the `close` step never runs) |
| none | | every run stops at the timeout after `start` and the first `clear`: VSHELL waits in the colour-depth box (window title "Hinweis"; `0x401c1f` → `0x401a81(3, 2, …)`) |
| 256COLOR | tour with mission page 1 (A) | 17 snaps, all runs exit 0; with `-Dump` |
| none | `-ConfirmNoticeSeconds 90` (B) | the box found after 11 to 213 ms, pressed and closed; then the same 17 index and colour-table hashes as A |
| 256COLOR | `registration` `empty` snap at 3000 ms | the caret is on, so the frame differs from A's |
| 256COLOR | `mission` `page1` snap at 3000 ms | inside the animation, so the frame differs from A's |
| 256COLOR | page 1 after 8000 ms (G) | the reference of the page-1 frames |

Findings (live unless marked):

1. **The capture's integrity.** `start` with `module_sha256_ok` true in
   every run, no `refused`; `focus_injected` 0 and `dropped` 0;
   `pointer_real_reads` 99 per run (the polls before the journal's first
   move), `pointer_warps` 0 (no `SetCursorPos` site runs in the windowed
   tour). Every snap after a click shows its screen once the virtual
   pointer is in place; a journal that only posts messages clicks at the
   real pointer (every snap the main menu).
2. **Every `present` record has `site` `0x406e57`** (the dirty flush and
   the full present both go through the GDI `BitBlt`).
3. **The main menu at start is a one-shot sequence, not a cycle:** six
   full presents about 280,000 pump passes apart (the last is the menu),
   then no further change until the snap 1.4 million passes later. These
   are the own shell's `main_menu.real_animation` frames 0 to 4 and
   `real_continue_enabled`.
4. **The 256COLOR layer changes no frame:** the colour-depth notice
   (message 3, caption 2, [vshell_process.md](vshell_process.md)
   section 4 step 11) is a warning only, and after it every snap's index
   and colour-table hash equals the layered run's (A against B, 17 of 17).
5. **The snaps** (a dump of each looked at): `main_menu` start, after
   Preferences, after Saved games (all the same frame, the continue-enabled
   menu); `preferences`; `saved_games`; `service_bay` after CONTINUE GAME
   (the same in `game_t`); `weapons`; `mission` `opened` (page 0 of Alpha
   sector); `main_menu` from the bay (the menu over the bay with the
   Herc); `registration` empty (a snap on the caret's on phase differs).
   Page 1 (G): `page1` and `briefing` (the mission summary, the same
   frame), `objectives`, `intelligence`.
6. **The second MISSION opens page 1 with `-a`:** the flag `0x477946` is
   set by the page-0 open, not by a played movie. Page 1's first entry runs
   a map animation of 355 presents (G: from pass 16,336,812 to 16,338,776,
   all at `0x406e57`) whose last frame is the `page1` frame; a click during
   it is handled after it (A's Objectives click: its text appears on the
   frame after the last animation frame). A 3000 ms wait catches animation
   frames; 8000 ms waits past the end. The summary text is German
   although the buttons are English ([vshell_ui.md](vshell_ui.md) §42.1
   explains where it comes from).

The screen tests use these sessions as references: `//shell:screen_test`
reads the newest session by default, its real cases carry a reference key,
and `//shell:gui_tour_real_test` compares es2shell's frames along the
`game_r` path. Every difference the tour showed was the cases' setup, not
the shell's drawing (the frame history through the bay background's 371
index-0 pixels, the clone's settings, slot names and caret phase):
[vshell_ui.md](vshell_ui.md) §41. Page 1's animation and text language in
es2shell: [vshell_ui.md](vshell_ui.md) §42.

Not captured: the debrief state (`results` run), full-screen (DirectDraw)
presents, the Registration screen's name entry for a complete new career.

## The own shell's frame, presenter and window

The own shell implements keep-list items 1 to 8 above together with the
window procedure and main loop of
[vshell_process.md](vshell_process.md#13-the-own-shell-process-frame)
and the input path of
[vshell_widgets.md](vshell_widgets.md#8-input-and-event-queue-of-the-own-shell).
Code: `//shell:gui` (portable, no platform call, no file I/O: frame, dirty
queue, colour table, screen, layout, input, queue, loop, headless mode),
`//shell:gui_sdl` (SDL3 window, pump, presenter), `//shell:es2shell` (the
process). The drawing primitives are `shell/src/gui/draw.c`
(`es2/shell/draw.h`).

**What is modelled.**

| Item | Own code | VSHELL |
|---|---|---|
| frame | `es2_gui_frame.pixels`, 640 × 480 top-down indices | `[0x481950]` |
| colour table | `es2_gui_palette_select` (pending) / `es2_gui_palette_apply` (main loop step 3): flags bit 0 → × 4, bit 1 → unscaled, else the entry keeps its value; nothing while the realization gate is 0, pending cleared anyway | `0x407b62`, `0x407c1b`, `0x4071fe` (`0x40720b`, `0x407248..0x407294`) |
| dirty queue | `es2_gui_invalidate_window` (skip when contained, borders included; remove queued rectangles strictly inside, the last moves into the hole and is re-checked; else append), `es2_gui_invalidate_rect` (append) | display slots 0/1, `0x41fef9..0x420042`, `0x42004b` |
| present | `es2_gui_flush`: each queued rectangle in order through `es2_gui_screen_present_rect`, count := 0 | `0x41fe49` → `0x41fe2c` → `0x406bfb` |
| screen | `es2_gui_screen`: 0x00RRGGBB pixels converted **at the present** through the table of that moment; a rectangle present covers BitBlt's over-size block (`left` … `2·left + w`, `top` … `2·top + h`, clipped to 640 × 480) | GDI `BitBlt` at `0x406e52` on a true-colour desktop |
| full present | `es2_gui_screen_present_full` (WM_PAINT with the gate set and no movie; WM_SETFOCUS during a movie) | `0x406e87` |
| clear | frame zeroed, the whole screen in the colour of index 0 | `0x4077ee` GDI branch (`StretchBlt` of the zeroed frame) |
| busy cursor | `es2_gui_set_busy` → SDL wait / arrow cursor; the next mouse move restores the arrow (DefWindowProc's WM_SETCURSOR, hypothesis) | `0x408d2f` |

The window shows `es2_gui_screen`; WM_PAINT/`SDL_EVENT_WINDOW_EXPOSED`
re-present the existing frame and never redraw a widget, so the
stale-bitmap path of [vshell_process.md](vshell_process.md) section 11
cannot occur.

**Platform deviations.**

1. *Scaling.* VSHELL shows the frame 1:1 at the client origin. The own
   windowed shell keeps a fixed 640 × 480 client (VSHELL's
   `WM_GETMINMAXINFO` forbids resizing; the SDL window is not resizable),
   so it is 1:1 as well. Full screen shows the frame at the largest integer
   scale that fits the desktop, centred on black (`es2_gui_layout_compute`);
   the mouse maps back through the same layout (`es2_gui_layout_to_frame`),
   as `0x405b6c`'s scaling maps the client to the frame.
2. *Full screen.* VSHELL's full screen is an exclusive DirectDraw
   640 × 480 × 8 mode with the cursor clipped to 640 × 480; the own shell
   uses a borderless desktop-size SDL window, no mode change, no cursor
   clip.
3. *Colours.* The own screen converts through the uploaded table (the
   true-colour desktop case). The 256COLOR/palette-device look (static
   colours in 0..9/246..255, the blue nudge, the flush to black) is not
   reproduced.
   One part of it is: when an upload changes the table, the main-loop
   step (and the drain helper `0x402266`) converts the whole frame through
   the new table (`es2_gui_recolour`), as an 8-bit display recolours every
   pixel on the install. VSHELL's GDI path converts only the dirty
   rectangles, so on a true-colour desktop areas outside them keep the
   previous palette's colours: ESPal's draw `0x40d0f0` flushes the queue at
   once, before its hide selects the new palette, and the bay's button bar
   (y 4..27, outside ESPal's 0..639 × 30..479) is then shown with the old
   table; in `mnu_bttn.dba` frames 1..3 the indices `0xea` and `0x8e` are
   grey in `arming.dpl` but red in `br_w3.dpl` (`0xea`) and the
   `cam_*`/`br_w*`/`db_w*` palettes (`0x8e`).
4. *Desktop scaling.* `VSHELL.EXE` declares no DPI awareness, so on a
   scaled desktop Windows enlarges its 640 × 480 client by the desktop
   scale and smooths it (at 125 %: 800 × 600), which blends the dithered
   gradients of the images. SDL3 declares per-monitor awareness by default
   (`WIN_InitDPIAwareness`, SDL 3.4.18); the own window declares the same
   as VSHELL (`SDL_WINDOWS_DPI_AWARENESS` = `unaware`,
   `es2_gui_window_open`), so Windows does the same enlargement for both.
5. *The visible gradients are the art's own.* The dark gradients of the
   images are drawn with greys that differ by one or two DAC steps per
   channel, so neighbouring patches are slightly blue or green. A
   screenshot of the main menu's left edge shows exactly the palette
   entries the frame uses there ((24,24,24), (24,24,32), (24,32,32),
   (32,32,40), (16,24,24), (32,32,32), (32,40,40): indices 220, 219, 210,
   204, 224, 205, 192 of the main menu's palette; DPL values × 4, all
   multiples of 8), the other colours being blends of the 125 %
   enlargement. The original shows the same gradients, also on a clean
   installation of only the CD's files (no proxy, no INI, `ES.EXE`).
   Nothing to reproduce or correct.
6. *Colour-depth warning.* The `BITSPIXEL` ≠ 8 warning box (`0x401c1f`)
   does not apply: the own presenter converts the 8-bit frame itself.
7. *Dirty-queue overflow.* VSHELL raises a severity-4 assertion at 100
   rectangles (process exit 1); the own queue is replaced by one full-frame
   rectangle (counted in `dirty_overflows`).
8. *Unwritten table entries.* An entry whose flags have neither bit keeps
   the previous upload's colour; VSHELL's comes from an uninitialised stack
   buffer (all CD DPLs have flag 1, so the case does not occur with CD
   data).
9. *Idle.* VSHELL's loop spins; `es2shell` waits up to 1 ms for the next
   SDL event after each pass (`SDL_WaitEventTimeout`), which changes no
   event or present order.
10. *Initial DIB table.* VSHELL starts with the system palette; the own
    table starts black (no present happens before the first install with
    the gate set).
11. *Window icon.* VSHELL's class `shell` (`0x40687c`) carries
    `LoadIconA(hInstance, "es2icon")`, the 32 × 32 icon of its own
    resources (shown in the title bar and the task bar). SDL3 registers its
    own window class without an icon, so `es2_gui_window_icon` (window.c)
    sets the icon with `WM_SETICON` at the system's large and small icon
    sizes, built by `es2_gui_store_icon` from the span store's copy of the
    icon resources ([The VSHELL span store](#the-vshell-span-store)); on
    failure the window keeps the default icon and `shell.log` says
    `no window icon: ...`. `//shell:gui_sdl_test` checks it against the
    file's icon at 32 and 16 pixels. Live (`PrintWindow`, `WM_GETICON`):
    the mech in the title bar.
12. *Event-queue capacity.* VSHELL's queue is a growable vector
    (`0x469ea8` → `0x41da2d`); the own one holds `ES2_GUI_QUEUE_MAX` events
    and counts what it cannot hold in `queue_overflows`. One drain reaches
    814 events from BUILD to CREW on a new career; a dropped tail loses the
    controls shown last (CREW's lower order rows without fill, frame and
    captions until the next click), so the capacity stays far above that.

**Headless mode** (`es2/shell/gui/headless.h`): no window, window messages
from a script, the screen in memory, one report line per present with
SHA-256 of the 307,200 frame indices, of the 768-byte table (blue, green,
red per entry, the DIB table without its reserved byte, as the capture
hashes it) and of the screen. Every GUI test and the screen harness use it.

**Verification.** `//shell:gui_test` (synthetic): queue merge and removal
order, overflow, palette flags and gate, the over-size present,
present-time colours, clear, layout and its inverse, the headless report
(deterministic, hashes of known frames). `//shell:gui_tables_real_test`
compares the input tables and four code facts with the reference
`VSHELL.EXE` ([vshell_widgets.md](vshell_widgets.md)). The frames against
VSHELL's: [The tours](#the-tours).

## The VSHELL span store

es2shell and es2menu do not open `cd_root\VER95\VSHELL.EXE`; the bytes
they need from it are own generated data, as VSHELL always holds its own
image. menu.ini's `cd_root` names the CD for the VOLs (`VOL\ZONES.VOL`,
`SHELL0.VOL`, `LANG0.VOL`), the texts, the movies and the online manual.

**Store.** `tools/reference-game/scripts/vshell_store.py` reads the
reference file once and writes `shell/data/vshell_store_data.c`
(committed, the source of record; no build or test runs the generator). A
run is the union of the readers' static spans (the script's `SPANS`, from
the source), the pointer tables and their targets (`POINTERS`, the target
window clipped to the section's raw end and required to hold a NUL), the
91 layout literals of `shell/data/vshell_layout.json` (length plus NUL) and
the measured reads, merged where they overlap or touch: 39 runs, 7,613
bytes (engine and prefs 12 runs/980 bytes, practice 3/40, gui 6/6,402,
literals 18/191), each with its VA and the readers it serves; no run is
"measured" only, so the static list covers every measured read. The
layout table `gui/layout_table.h` keeps the literals' VAs (the bytes live
in the store). VSHELL's class icon: the `RT_GROUP_ICON` `ES2ICON` (20
bytes, `0x5e9398`) and its one `RT_ICON` image (id 1, 744 bytes,
`0x5e90b0`).

**Measured reads.** `shell/data/vshell_reads.txt` (VA, size, reader kind;
no bytes) is the union of every read of the shell real and screen tests
while they still read the file: 26 intervals, 7,539 bytes. Areas: the hot
bytes `0x46e5a9` (3,796), the map cosine `0x47b480` (2,050) and arctangent
`0x47aaf4` (513) tables, the preparation tables `0x47f850` (112),
`0x46f72c` (16), `0x46f868` (36), the results spans `0x46f9a8`…
`0x46fec4`, `0x46ffc0` (18), the two pointer tables `0x47006c` and
`0x47083c` (9 slots each) and the strings they name (`0x4703ed`, 168;
`0x470b4c`, 200: the readers take up to 32 and 64 bytes there), the band
and editor tables `0x47665c` (22), `0x476a4c` (54), the practice tables
`0x479c56` (24), `0x479f70` (8), `0x47396b` (8), and seven literal areas.
`gui/weapons.c` asks the literal reader for `0x4774c1` (item ID 0) outside
the layout table; the screen drivers (`tests/screen_practice.c`,
`screen_weapons.c`) read two bytes per literal, which is why the union
holds one byte more at `0x479c3e`. The measurement switch
`ES2_SHELL_SPAN_LOG=<directory>` (off by default; without it nothing is
written and nothing changes) appends `<reader> 0x<va> <size>` to
`<dir>/spans-<pid>.log` for every successful read through
`es2_shell_exe_span` (`practice.c`) and every literal the layout literal
reader (`literal()`, `gui/app.c`) returns (`es2_shell_span_log`,
`shell/src/vshell_store.c`).

**Readers.** `es2_shell_exe_span(ES2_VSHELL_STORE, va, size, ...)` answers
from the runs (binary search by VA); a read that a single run does not
hold whole is `ES2_ERR_NOT_FOUND` (never zero bytes). The engine tables
(`engine_io.c`), the practice tables and prefs defaults (`host.c`) and
every read of `gui/app.c` (literals, the mission map's tables, `0x46ffc0`,
the hot bytes, the band and editor tables) come from the store. A missing
table is the engine's error "the VSHELL tables of the span store could not
be read". `es2_gui_store_icon` (`window.c`) builds the icon from the stored
group and image (`LookupIconIdFromDirectoryEx`,
`CreateIconFromResourceEx`).

### es2shell and es2menu without VSHELL.EXE

The run-time path has no SHA-256 gate and no file read of `VSHELL.EXE`,
and no image fields in the engine I/O, `es2_host_practice` or the
graphical app; `sha_hex` serves the evidence log's hashes.
`es2_shell_exe_span` (`practice.c`) answers the store sentinel; any other
image goes to a file reader that only tests install
(`es2_shell_set_file_reader`); without one it is `ES2_ERR_ARGUMENT`. The
PE section walk is `shell/tests/vshell_file.c` (`//shell:vshell_file`,
testonly), installed by the tests that compare the store with the
reference file or use synthetic PE images: `vshell_store_real_test`,
`practice_test` (which first checks that, without the reader, a file image
is refused and the store still gives the practice tables), `engine_test`
and the screen drivers `tests/screen_practice.c`, `screen_weapons.c`.
`gui_tables_real_test`, `mission_map_tool.c` and `screen_boxes.c` keep
their own readers of the file, and `//shell:gui_sdl_test` its own
`file_icon` to compare the stored icon with the file's.

**Not part of the own chain.** The Python tools (`shell/tools/es2prep.py`,
`es2loadout.py`, `es2campaign.py`) read and hash-check the file; they are
reference and test tooling (es2menu starts no backend), not the own chain.
`launcher/` names `vshell.EXE` only for the original line (ES.EXE's child
names, the stand-in chain test); `setup/` names it nowhere.

### How the store is checked

- `//shell:vshell_store_real_test` (`ES2_DATA_ROOT`, SKIP without): every
  run equals the file at its VA through the section walk, runs sorted and
  not adjacent; the icon group and image equal the file at their VAs and
  what Windows' resource loader finds for `ES2ICON` and its id in the
  file; every line of `vshell_reads.txt` and every `ES2_UI_REF_EXE_STRING`
  of the layout table (with its NUL) gives the same bytes from the store as
  from the file; the engine and practice table readers give the same
  tables from both.
- `//shell:vshell_store_test` (no data): the lookup and its refusals on
  every run, the measurement switch's line format.
- `//shell:gui_sdl_test`: the stored icon without a file, and with
  `ES2_DATA_ROOT` equal in size and pixels to the file's at 32 and 16
  pixels.
- `//tools/reference-game/scripts:vshell_store_unit_test`: the generator
  on an own synthetic PE with a group icon (merge, provenance, pointer
  windows, refusals, `--check`), the committed reads inside the committed
  runs, and with `ES2_DATA_ROOT` a regeneration byte-identical to the
  committed file.
- `//shell:no_vshell_real_test` (`tests/test_no_vshell_real.py`,
  `ES2_DATA_ROOT`, SKIP without): a temporary mirror of the CD (hard links
  on the same volume, copies where a link fails) without
  `VER95\VSHELL.EXE`, removed afterwards. es2shell headless runs
  `gui_tour_real_test`'s career (exit 2) and tour (exit 0) and es2menu
  PRACTICE MISSIONS (`7|6|14|21|10`) and INSTANT ACTION (`8`), each exit 2,
  once against the full CD and once against the mirror: exit codes,
  reports and console output (paths replaced) and every DATA and SAV file
  are equal. A re-introduced read of `cd_root\VER95\VSHELL.EXE` in
  `eio_open` that fails without the file is caught.

## Open questions

- The colours on screen under the 256COLOR layer (static entries, blue
  nudge): the indexed frames and DIB tables are identical with and without
  the layer; the logical palette and the visible result on a palette
  device were not captured.
- `CreateDIBSection` with `DIB_PAL_COLORS` and a table later set as RGB
  quads: GDI's interpretation on this host is not checked (as for DBSIM in
  [presentation.md](presentation.md)); it does not affect the indexed
  hashes.
- Whether any widget draws into the frame without invalidating (screen ≠
  frame after a flush).
- The page present `0x406264`'s 320 × 240 / 640 × 400 rectangle and the
  offscreen rebinding `0x427800..0x427ec9`: purpose (three-space view of the
  Herc?) and visible effect not followed.
- Whether `0x44e560` returns after the dirty-queue overflow assertion.
- `0x45b4c0` (scaled draw), `0x4592a8` (polygon draw) and layout-1 bitmaps
  `0x459508` are not followed.
- The palette words of the mission-screen movie records (index 0
  `intr_pt1`, 19 `luna` possibly reachable only there).
