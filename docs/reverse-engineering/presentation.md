# Presentation contract (DBSIM)

What DBSIM's presentation does with its 8-bit frame, what an own presenter
must reproduce or leave alone, how the proxy captures and presents frames,
how frames are accepted, and the own process's presentation code. Related:
[display_modes.md](display_modes.md) (display object, keep-list items 1–4),
[flyer.md](flyer.md#function-table) (the presentation/platform function
table, `0x465e28`/`0x465a24`/`0x465c40`/`0x46548c`, MainWndProc),
[timing.md](timing.md) (virtual time, intra-frame clock) and
[vshell_presentation.md](vshell_presentation.md) (VSHELL's counterpart).

**Method.** Read from the bytes of the reference `VER95/DBSIM.EXE`
(image base `0x400000`, see
[reference_version.md](../reference_version.md)) with
`llvm-objdump -d --no-show-raw-insn -M intel`; `0x45fba4`, `0x4649d8` and
`0x464a14`, which the linear listing decodes out of step, are disassembled
from their entry with `--start-address`. Import thunks are named from the PE
import table; DATA tables (the graphics-driver descriptor `0x4a586c`, the
strings `BMP_8`, `data\prefs.cfg`) are read from the file. Live counts are
the saturated entry probes of
the DBSIM map (order
a60/c1s/c1c/c3_09/c4_08/c3_10). **live** = a live run shows it;
everything else is a **hypothesis**. "Listing" = the `llvm-objdump -d -M
intel` listing of a scratch copy of the verified file.

## Summary

1. **DBSIM never draws into a DirectDraw surface.** Renderer,
   HUD and text draw in software into one 8-bit top-down **DIB section**
   (bits `[0x4d3028]`, memory DC `[0x4d3034]`, created by `0x464d44`) larger
   than the screen: 1280 × 960 for 640 × 480. A present copies one
   screen-sized window of it at the *page origin* `S` = (`[0x4d3018]`,
   `[0x4d301c]`) into the destination rectangle `R` = `[0x4d2ff8..0x4d3004]`.
   The one present routine `0x465628(&S, &R)` (through `0x464a14`) has two
   branches selected by `[0x4d255e]`: **GDI** (`StretchBlt` from the DIB DC
   to the window, scaled to the client rectangle) and **DirectDraw** (lock
   primary or back buffer, copy rows, draw the cursor sprite, unlock, flip
   below 640 pixels). No `GetDC`/`ReleaseDC` on a surface, no
   `Blt`/`BltFast`, no clipper, no GDI text output.
2. **The oracle runs the GDI branch (live).** The perf report of
   `c1_01-success` (DirectDraw observer installed) counts one `directdraw` phase over 6,197
   frames, the observer's own final drain (`dd_finish`): no DirectDraw object
   was created. The hang snapshot of
   [input_journal.md](../reference-game/input_journal.md) shows `0x465628` in
   `GetClientRect`/`InvalidateRect`/`BeginPaint`. Every record of the
   40-suite frame capture has mode 0
   (every manifest `modes`
   `{"0": n}`). So the `ddraw` observer, `-DeferFlip` and
   `-PresentationEvery` do nothing in the oracle, and the oracle path
   already is a windowed, scaled presenter.
3. **Nothing DirectDraw or GDI returns reaches simulation state.** Return codes, `lpSurface`/`lPitch`,
   client rectangle, system palette and `NUMCOLORS` feed only the present
   and palette routines; DBSIM never reads surface memory and the present
   never writes the DIB. What touches compared state is in the
   [keep-list](#keep-list-for-a-presenter).
4. **Seam** ([The proxy seam](#the-proxy-seam)): in the oracle, DBSIM's
   GDI32/USER32 import slots used by `0x465628` and the palette sinks
   `SetDIBColorTable`/`AnimatePalette`; the DirectDraw observer only for the
   DirectDraw branch. The proxy's GDI presenter needs no SDL3.
5. **Acceptance** is defined on the indexed frame and its colour table at
   every present ([Acceptance](#acceptance)).

## The frame buffer

| Item | Address | Value (by video mode) | Evidence |
|---|---|---|---|
| video mode | `0x45e510(m)`: byte 4 of `data\prefs.cfg` (7 bytes read once, `0x45e535`/`0x45e54c`), or `-v<digit>` (`0x45e6cc`) | prefs byte 1 → low resolution, anything else → high |  |
| DirectDraw flag `[0x4d255e]` | byte 6 of `data\prefs.cfg` (`0x45e561`); `-Z1` sets 1, `-Z<other>` 0 (`0x45e706`); the toggle `0x4667c8` flips it | 0 = GDI, ≠ 0 = DirectDraw | 0 in every suite (live, [Summary](#summary) item 2) |
| screen W × H `[0x4d253e]`, `[0x4d2542]` | `0x45e5ac`, `0x45e612`, `0x45e679` | low 320 × 240, high 640 × 480 |  |
| DIB W × H `[0x4d2546]`, `[0x4d254a]` (= DIB stride) | same | low 640 × 480, high 1280 × 960 |  |
| `[0x4d253c]` coordinate shift, `[0x4d2537]` | same | low 0 / 0, high 1 / 3 |  |
| present-mode word `[0x4d2566]` | 1 in every video mode; 0 only through a case of the option parser `0x45e758` (`0x45e84a`) | selects the presentation-frame branch of `0x45fba4` |  |
| DIB section | `0x464d44` (from window creation `0x465158`): `CreateCompatibleDC(0)` → `[0x4d3034]`; BITMAPINFOHEADER at `0x4d304c`: size `0x28`, width = DIB W, **height = −DIB H (top-down)**, 1 plane, 8 bit, `BI_RGB`, image size W·H; colour table at `0x4d3074` = the system palette with red and blue swapped; `CreateDIBSection(memdc, bmi, 1, &[0x4d3028], 0, 0)` (usage 1 = `DIB_PAL_COLORS`), selected into the memory DC; failure → message box "Bad WinGBitmap" | — |  |
| software screen object | `0x464d44` names it `BMP_8` (`0x4a0841`); page origins `+0x48..+0x67` zeroed because the name is not `VGA_8` | single buffer, no hardware pages |  |
| graphics driver | `0x4893aa(3)` copies the descriptor `0x4a586c` (the only entry of the list `0x4a5840`) to `0x4a57ac` (`0x24` dwords): `[0x4a57e8]` (page origin) = `0x48990e` → `0x4649d8`; `[0x4a57ec]`, `[0x4a57f0]`, `[0x4a57f4]` (page selects) = `0x4898fc`, `0x489902`, `0x489908`, empty (`enter`/`leave`/`ret`); driver init `0x48a1d4` | — |  |
| R | `0x48a1d4`: (0, 0, `[0x4a03c8]` − 1, `[0x4a03cc]` − 1), where `0x464d44` stored the screen W, H | (0, 0, 639, 479) at high resolution |  |
| S | `0x48a1d4` sets (0, 0); `0x4649d8(page, point)` sets `[0x4d3018]`, `[0x4d301c]` and moves the renderer's pixel pointer `[[0x4d2ff0]+0xa]` and row base `[[0x4a03d0]]` to `[0x4d2ff4]` + x + y · DIB W | live 1000 in c3_09 only (jolts and panel slides, [display_modes.md](display_modes.md)), 0 elsewhere | counts live |

The "8-bit primary surface" an own presenter must show is the W × H window
of the DIB at S, with R = the whole screen in every main-loop present. The
page swaps of the presentation frame (`0x45fc3b..`, `+0x88`/`+0x8c`) and the
page selects of the display-mode code call the empty driver slots and change
nothing visible.

## Who presents, and when

- **Main loop.** `0x45fba4` (presentation frame, after rendering
  `0x45fca8`, before the frame-counter increment at `0x45f4ad`,
  [main_loop.md](main_loop.md)); step by step in
  [The frame sequence](#the-frame-sequence-0x45fba4). With `[0x4d2566]` ≠ 0
  it calls the transition animation `0x42a9c0(D)` (may present several times
  per frame through `0x464a14` without reading the clock), **the palette
  cycler `0x4306ac` (reads the clock at `0x4306b6`)**, the flush `0x43048c`,
  the present `0x464a14` and the transition start `0x42a4c4(D)`. Live:
  `0x45fba4`, `0x4306ac`, `0x42a9c0`, `0x42a4c4` 1000×5/936; `0x465628`
  1000×5/940.
- **Other callers of `0x464a14`** (29 `call 0x464a14` in the listing): the
  display-mode slides inside `0x42a9c0` (`0x42b135`…`0x42d040`, 21 calls), UI
  screens (`0x454906`, `0x454b60`, `0x457205`: modal loops such as
  `0x456000`), mission load (`0x4614a3`, `0x461e1c`), MainWndProc `WM_PAINT`
  (`0x4662a7`, GDI mode only) and the private message `0x812`. In modal loops
  the frame counter stands and the number of presents follows message
  traffic (hypothesis, [timing.md](timing.md) item 2; see
  [The original's own frame spread](#the-originals-own-frame-spread)).
- **Frame clear** `0x46548c` (flyer.md): WinMain after entering DirectDraw,
  `0x45f4da`, `0x4616a9`, `0x461a6f`, teardown `0x466730`. Live 0/1/0/1/1/1.

## GDI branch (the oracle path)

`0x464a14` (`push 0x4d2ff8; push 0x4d3018; call 0x465628; add esp, 8; ret`)
calls `0x465628(S, R)`. With `[0x4d255e]` = 0, and only when `[0x4d3034]` ≠ 0
(`0x46589a..0x4659e5`):

1. `GetClientRect(hwnd, &rc)`; `InvalidateRect(hwnd, &rc, TRUE)`;
   `hdc = BeginPaint(hwnd, &ps)` (`hwnd` = `[0x4d3610]`). The class brush
   is null (`0x4650e8`), so the erase draws nothing.
2. If `[0x4d302c]` (GDI palette): `old = SelectPalette(hdc, pal, FALSE)`,
   `RealizePalette(hdc)`.
3. `sx = (rc.right << 16) / W`, `sy = (rc.bottom << 16) / H` (signed
   `idiv`); `dx = (s16(R.left) · sx) >> 16`, `dy = (s16(R.top) · sy) >> 16`
   (arithmetic shifts); `w = R.right − R.left + 1`, `h = R.bottom − R.top + 1`;
   `dw = rc.right` if `w = W`, else `(s16(w) · sx) >> 16`; `dh = rc.bottom`
   if `h = H`, else `(s16(h) · sy) >> 16`.
4. `StretchBlt(hdc, dx, dy, dw, dh, [0x4d3034], S.x, S.y, w, h, SRCCOPY)`.
   No row clamp and no cursor sprite (the Windows cursor of
   `SetCursor([0x4d3720])` in the pump `0x464c78` is used).
5. If the palette is set (`[0x4d302c]` re-read at `0x4659b2`):
   `SelectPalette(hdc, old, TRUE)`, `RealizePalette(hdc)` again (`0x4659c8`).
   `EndPaint(hwnd, &ps)`.

For R = the screen this stretches the W × H source over the whole client
area. The window (`0x465158`) is created centred, client size W × H, style
`WS_VISIBLE | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX` (`0x100e0000`,
resizable, no caption bit), class `ES2`; `WM_GETMINMAXINFO` limits the track
size ([flyer.md](flyer.md#mainwndproc-0x466034)). The pixels of a scaled
`StretchBlt` depend on GDI's stretch mode of the paint DC (default, never
set by DBSIM) and on the 256COLOR emulation
([media_compatibility.md](../reference-game/media_compatibility.md)); only
1:1 is a plain copy (hypothesis for the exact GDI behaviour).

## DirectDraw branch

Never taken in the oracle ([Summary](#summary) item 2); the bytes define it
completely. Every DirectDraw call is in `0x46548c..0x4668f4` and
`0x465f48..0x465f72` (all references to `0x4d3710`/`0x4d3714`/`0x4d3718`/`0x4d371c`);
the only import is `DirectDrawCreate` (thunk `0x496a54`).

| Object | Method (slot, byte offset) | Sites | Arguments | Result used |
|---|---|---|---|---|
| — | `DirectDrawCreate` | `0x466595` (`0x46658c`) | (NULL, `&[0x4d3710]`, NULL) | ≠ 0 → fail |
| DD `[0x4d3710]` | `SetCooperativeLevel` (20, `+0x50`) | `0x4665b3` | (hwnd, `0x57` = FULLSCREEN, ALLOWREBOOT, NOWINDOWCHANGES, EXCLUSIVE, ALLOWMODEX) | ≠ 0 → fail |
| DD | `SetDisplayMode` (21, `+0x54`) | `0x4665e2` (`0x4665c4(W, H, 8)`) | (W, H, 8) | ≠ 0 → fail |
| DD | `CreateSurface` (6, `+0x18`) | `0x466687`, `0x4666a6`, `0x4666c9`, `0x4666e8` | descriptor below, out `&[0x4d3714]`, NULL | ≠ 0 after the retry → fail |
| primary | `GetAttachedSurface` (12, `+0x30`) | `0x466718` (W < 640 only) | (caps `DDSCAPS_BACKBUFFER` = 4, `&[0x4d3718]`) | ≠ 0 → fail |
| DD | `CreatePalette` (5, `+0x14`) | `0x4668de` (toggle), `0x465f5c` (`0x465e28`) | flags 4 (`DDPCAPS_8BIT`) resp. `0x4c` (`8BIT | INITIALIZE | ALLOW256`), entries `0x4d372c`, out `&[0x4d371c]`, NULL | = 0 → `SetPalette` |
| primary | `SetPalette` (31, `+0x7c`) | `0x4668f4`, `0x465f72` | (`[0x4d371c]`) | ignored |
| palette `[0x4d371c]` | `SetEntries` (6, `+0x18`) | `0x465e1e` (`0x465cc4`) | (0, 0, 256, entries) | ignored |
| primary | `IsLost` (24, `+0x60`) | `0x4654aa`, `0x46564f` | — | = `DDERR_SURFACELOST` (`0x887601c2`) → `Restore` |
| primary | `Restore` (27, `+0x6c`) | `0x4654bd`, `0x465662` | — | ignored |
| target | `Lock` (25, `+0x64`) | `0x4654ee`, `0x4656a0` | (NULL, desc with `dwSize` `0x6c`, `DDLOCK_WAIT`, NULL) | ≠ 0 → skip the frame (no unlock, no flip); `lpSurface`, `lPitch` read |
| target | `Unlock` (32, `+0x80`) | `0x465524`, `0x465870` | (NULL) | ignored |
| primary | `Flip` (11, `+0x2c`) | `0x465547`, `0x465892` (W < 640 only) | (NULL, `DDFLIP_WAIT`) | ignored |
| all | `Release` (2, `+0x8`) | `0x4665fc`, `0x466613`, `0x46662a` (before re-creating), `0x46674d`, `0x466764`, `0x46677b`, `0x466792` (teardown) | — | ignored |

Target = the primary when W ≥ 640, else the back buffer `[0x4d3718]`.

**Creation** is the toggle `0x4667c8` (GDI → DirectDraw, also from WinMain
`0x46543c` when `[0x4d255e]` is set at start), step by step in
[Contract](#contract). The surface descriptor: `dwSize` `0x6c`, caps
`DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY` (`0x4200`); W < 640: `dwFlags`
`0x20` (`DDSD_BACKBUFFERCOUNT`), one back buffer, caps
`| DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_MODEX` (`0x200018`); W ≥ 640:
`dwFlags` 0, no back buffer. Neither sets `DDSD_CAPS` (quirk; whether the
system DirectDraw accepts it is [open](#open-questions)). A failed
`CreateSurface` is retried once without `DDSCAPS_VIDEOMEMORY`. The last step,
`0x45f13c(mode)`, calls the cockpit widget `[H+0x1e9]`'s virtual `+4`, which
reaches the panel's full redraw `0x443758` ([cockpit.md](cockpit.md), slot 1
of the panel widget). **No clipper, no surface description query, no
`GetCaps`, no mode enumeration**.

**Per present** (`0x465628`, `0x465647..0x465895`): `IsLost` → `Restore`;
`Lock(target)`; source = `[0x4d3028]` + S.x + S.y · DIB W, destination =
`lpSurface` + R.left + R.top · `lPitch`; `w = R.right − R.left + 1`
columns, `h = min(R.bottom − R.top + 1, DIB H − S.y)` rows; a `memcpy` per
row when R.left = R.top = 0, else a byte loop (same result); the
[cursor sprite](#cursor-sprite); `Unlock(target)`; `Flip` when W < 640. In
high resolution this is the non-flipping primary `Lock(NULL, desc,
DDLOCK_WAIT, NULL)` / `Unlock(NULL)` pair that the observer's
`-PresentationEvery` retains; in low resolution it is the `Flip(NULL,
DDFLIP_WAIT)` that `-DeferFlip` defers.

**Teardown** (`0x466730`, through `0x4667c8` and at exit from WinMain
`0x46547a`): clear, release of back buffer, primary, palette, DD,
`ClipCursor` to the whole screen
([The help call and the DirectDraw release](#the-help-call-and-the-directdraw-release));
the toggle back to GDI is in [Contract](#contract). Callers of the toggle:
[The DirectDraw full-screen switch](#the-directdraw-full-screen-switch).

**Read back:** only the return codes in the table and
`lpSurface`/`lPitch` of the lock descriptor. Never surface memory, never
`GetSurfaceDesc`; nothing is stored outside `0x4d3710..0x4d371c`,
`0x4d255e`. A lost surface costs one `Restore` per present; a
failed lock drops that present silently.

## Palette contract

**Sources.** The game's current palette object is
`[0x49b020]` (setter `0x4303b0`); entries are 4-byte records at
`[P+8]` (bytes 0..2 = 6-bit red, green, blue, byte 3 flags). Changes reach
the screen only through the palette flush `0x43048c` (callers `0x429bc4`,
presentation frame `0x45fbda`/`0x45fc36`, mission load `0x461dec`/
`0x461e08`):

- `[0x49b020]` ≠ the last flushed `[0x49b024]` → `0x481fe0(P)`
  (renderer colour tables `0x48d99e`/`0x4820b7`, then the full install
  `0x465e28(P)`); live 52 in c3_09 only (the damage palette swap
  `0x42ea44`/`0x42ea70`/`0x42ea94`, [display_modes.md](display_modes.md));
- else, if the dirty byte `[0x49b030]` is set → **`0x465cc4(P)`**; live
  177/1000/738/865/601/436;
- then the dirty byte and the dirty range `[0x49b028]`/`[0x49b02c]` ← 0. The
  range (kept by the entry writer `0x4303c4`) is never used by the upload:
  every upload sends 256 entries.

Writers of the dirty byte: `0x4303c4` (range copy into `[P+8]`, live
≈ the upload count; called by the cycler `0x4306ac`, which rotates up to
five colour ranges `0x4cfd64` on clock deadlines `0x4cfd78`), and the fade
step `0x430b34` (reads the clock itself at `0x430b3a`; dirty writes at
`0x430cc1..0x430cf3`; callers `0x45daef` in the mission-start sequence
`0x45d85c` and `0x45dd30`/`0x45dd53` in the unreferenced `0x45dc50`).

**`0x465cc4(P)` (`0x465cc4..0x465e26`).**

1. `hdc = GetDC(0)`; `n = GetDeviceCaps(hdc, 24)`;
   `GetSystemPaletteEntries(hdc, 0, 256, E)`; `r = n / 2` (`sar` with the
   sign fix-up, towards zero). Index 24 (`0x18`) is **`NUMCOLORS`**, not
   `NUMRESERVED` (`0x465ceb`; `0x465a24` uses the same constant at
   `0x465ae9`). On a palette device it is the number of static colours
   (20 → r = 10, hypothesis for the 256COLOR emulation); a true-colour
   device answers −1 → r = 0.
2. `E[i]` for i < r and i ≥ 256 − r: the system entry with flags 0. For
   r ≤ i < 256 − r: `E[i]` = ((P.r << 2) & 255, (P.g << 2) & 255,
   (P.b << 2) & 255, flags 5 = `PC_RESERVED | PC_NOCOLLAPSE`), `[P+8]`
   re-read for every entry. The source flag byte is ignored (unlike
   `0x465e28`). r above 128 leaves the middle range empty (every flag 0);
   **n ≤ −2 or n ≥ 514 writes outside `E`** (trap). `ReleaseDC(0, hdc)`.
3. GDI mode: build RGBQUADs (blue, green, red from `E`; the fourth byte is
   never written, stack residue) and `SetDIBColorTable([0x4d3034], 0, 256,
   quads)`, then `AnimatePalette([0x4d302c], 0, 256, E)`; EAX =
   AnimatePalette's result. DirectDraw mode:
   `[0x4d371c]->SetEntries(0, 0, 256, E)`.

So indices 0..r−1 and 256−r..255 show the system's static colours, not the
game's (both modes). An own presenter must take the palette **as uploaded**
(the table passed to `SetDIBColorTable` resp. `SetEntries`), not recompute
it from `[P+8]`.

**`0x465e28(P)`** (full install;
[The palette set](#the-palette-set-0x481fe0-with-0x465e28-0x465c40-0x465a24)):
stores P in `[0x4d3b2c]`, converts with the flag rule (bit 0 → ×4, bit 1 →
unscaled, neither → the frame entry is not written), flushes the system
palette to black (`0x465c40`), creates a new logical palette `[0x4d302c]`
(`0x465a24`, never deleting the old one), realizes it on the screen DC and
calls `SetDIBColorTable`; DirectDraw mode additionally
`CreatePalette(0x4c)` + `SetPalette` (the old palette object is not
released). MainWndProc's `WM_QUERYNEWPALETTE`/`WM_PALETTECHANGED`
re-realize in GDI mode only.

**Fades** are ordinary entry writes plus flushes; no gamma ramp, no
`WaitForVerticalBlank`, no hardware fade (no such import or slot
is used).

## Cursor sprite

`0x465618(p)` stores the sprite object `[0x4d3724]` (caller `0x461ab8` in
the mission load, from the bitmap loader `0x45cdf4`). Only the DirectDraw
branch draws it (`0x46576a..0x465866`): `GetCursorPos(&pt)` (screen
coordinates, equal to surface coordinates only in exclusive full screen);
nothing unless 0 ≤ x < W and 0 ≤ y < H; bitmap header B = `*[p+8]`, pixels
at `B+0xe`, width = s16 `B+6`, height = s16 `B+4`; drawn with its top-left
corner at the pointer, clipped to W − x columns and H − y rows, source row
stride = the full width; **index 1 is transparent** (`movsx`, `dec`,
skip on zero). It is written into the locked surface only, never into the
DIB. With a sprite set, entering DirectDraw hides the Windows cursor and
leaving shows it. In GDI mode (the oracle) the sprite is never drawn, so it
is not part of any frame hash.

## Keep-list for a presenter

What a presenter must leave original or reproduce, because it reaches
compared state, the input journal or the clock. Items 1–4 restate
[display_modes.md "Simulation-relevant edges"](display_modes.md#simulation-relevant-edges).

1. Panel-off byte `H+0x20f` writes and their timing.
2. `0x45b68c` at the end of the 0↔1 transitions.
3. Display mode `D+0x14` as read by the key dispatchers.
4. Clock and stream-B calls of `0x434010`/`0x43408c`; the camera
   attachment-angle adds of `0x42a4c4`; no native port read `0x45c638`.
5. **The palette cycler `0x4306ac` reads the clock once per presentation
   frame (`0x4306b6`).** Under the intra-frame clock every read
   after the first in a frame advances the virtual clock by 1 ms
   ([timing.md](timing.md)), so removing, adding or reordering it changes
   the clock seen by later readers. The presenter replaces only the sinks
   (`0x465cc4`'s API calls, `0x465628`'s blit), never `0x45fba4`'s call
   sequence. Measured with every hook original
   ([hud.md](hud.md#the-coupling-set-measured)): 72,288 reads after the
   load in the 40 suites, exactly once per tick in 38 and 1.009 per tick in
   the two options-dialog suites, plus one read in each suite's load window.
6. **No `GetTickCount` through DBSIM's thunk** (`0x4968ec`) from presenter
   code, and no other call into DBSIM code. GDI, USER32 and SDL internals
   may use their own time sources (they do not pass through DBSIM's IAT).
7. **Message traffic of the game window.** The present validates the
   window inside itself (`InvalidateRect` + `BeginPaint`/`EndPaint`), so no
   `WM_PAINT` is left pending. A presenter must leave the update region
   in the same state, must not post or send messages to the game window,
   must not resize, show or hide it during a replay (resizes produce
   `WM_PAINT`, which the pump `0x464c78` retrieves and counts: pump-keyed
   journal entries shift, and `WM_PAINT` presents again through
   `0x464a14`), and must not pump messages itself on the main thread.
8. **Focus.** `WM_KILLFOCUS`/`WM_SETFOCUS` toggle the compared pause flag
   `0x4d2572` (flyer.md). The replay filters them, interactive play does
   not: an own top-level window that takes focus pauses the game. Present
   into the game's own window (or a child of it), not a separate
   top-level window, unless the input path routes input and focus.
9. **The window/full-screen toggle `0x4667c8`** writes `[0x4d255e]`,
   which MainWndProc (`WM_PAINT`, palette cases), `0x4669b4`, `0x465cc4`,
   `0x465e28` and `0x465628` read; keep its call sites and the
   `0x45f13c` widget call. A presenter may implement "full screen" as a
   borderless scaled window, but the flag must keep its value unless all
   readers are moved with it.
10. **`0x481fe0`'s renderer tables** (`0x48d99e`, `0x4820b7`) are
    renderer state: keep `0x481fe0` original; only `0x465e28`'s API sink
    may be replaced.

Not on the list (presentation only, may change freely): the DIB section's
creation as long as `[0x4d3028]`, `[0x4d3034]`, the stride and the
top-down layout stay; the GDI logical palettes; DirectDraw objects; the
blit; the cursor sprite drawing; window position and client size outside
replays.

## The proxy seam

- `ddraw.dll` is our proxy; `media_proxy.c` forwards all exports to the
  system DirectDraw. Only when `[perf]`, `[presentation] defer_flip` or
  `[presentation] every_n` > 1 is set (runner `-Perf`, `-DeferFlip`,
  `-PresentationEvery`, measurement INI `ES2_ORACLE_SPEED_CONFIG`) does it
  replace `DirectDrawCreate` with `dd_create`
  ([ddraw_observer.h](../../tools/reference-game/proxy/ddraw_observer.h)),
  which gives the DirectDraw object and every surface from
  `CreateSurface`, `DuplicateSurface` and `GetAttachedSurface` its own
  method table (v1 only). Palettes from `CreatePalette` are **not**
  wrapped, so `SetEntries` is invisible to the observer.
- In the oracle (GDI branch) the observer wraps nothing (live:
  `"deferred":0,"drained":0` and one `directdraw` count in the perf reports).
- The proxy patches DBSIM's import slots in memory for `GetTickCount`
  ([virtual_time.h](../../tools/reference-game/proxy/virtual_time.h)),
  the two MCI functions (`hook_executable_mci`) and the GDI seam below
  ([presenter.h](../../tools/reference-game/proxy/presenter.h)).

**What a presenter replaces.**

| Branch | Seam | Replace | Keep |
|---|---|---|---|
| GDI (oracle) | DBSIM's IAT slots of `StretchBlt` (2 sites: `0x4659ad` in `0x465628`, `0x4655e3` in the clear `0x46548c`), `SetDIBColorTable` (`0x465cc4`, `0x465e28`), `AnimatePalette` (`0x465cc4`) | `StretchBlt` from `[0x4d3034]` → own conversion of the source rectangle through the last uploaded colour table and an own scaler (integer/aspect-correct, nearest) drawn with `StretchDIBits`/`SetDIBitsToDevice` or SDL; `SetDIBColorTable` → record the table (and forward, so the DIB stays consistent); `AnimatePalette`/`RealizePalette` may become no-ops for a true-colour desktop | `GetClientRect`, `InvalidateRect`, `BeginPaint`, `EndPaint` (window validation, item 7); `GetSystemPaletteEntries`/`GetDeviceCaps` unless the presenter fixes r (then the colour table changes and the acceptance must say so) |
| GDI, alternative | an entry hook on `0x465628` (own code reads S, R, `[0x4d3028]`, the recorded table) | the whole function body | the window validation calls of item 7, done by the hook |
| DirectDraw | `dd_create` and the wrapped methods | `SetCooperativeLevel` (no exclusive mode), `SetDisplayMode` (no mode change), `CreateSurface` (own 8-bit system-memory buffer of W × H with a matching `lPitch`), `IsLost` → `DD_OK`, `Lock`/`Unlock` → own buffer (present at `Unlock` when W ≥ 640), `Flip` → present the back buffer (W < 640), `CreatePalette`/`SetEntries`/`SetPalette` → wrap the palette (slots 3–6) and record the entries | the return-code contract of the table above (all success); `GetCursorPos`-based sprite drawing stays inside DBSIM |

The proxy implements the first row and the DirectDraw row
([below](#capture-own-gdi-presenter-windowed-directdraw)) without SDL3.
SDL3 serves the own process's window
([own_process.md "The windowed platform layer"](../reference-game/own_process.md#the-windowed-platform-layer));
GPU scaling, filters and vsync control would need it in the proxy too.
Dropping the 256COLOR emulation for display does not drop it for
`0x465cc4`'s `NUMCOLORS`/system palette reads: without it r becomes 0 and
`GetSystemPaletteEntries` fills nothing, so the uploaded table changes
(hypothesis; the [visual check](#visual-check) saw no wrong colours).

## Acceptance

**What is compared.** At every present, the *indexed* frame and its
palette, not screen RGB: GDI's RGB result depends on the desktop
(256COLOR emulation, DWM, stretch mode) and is not reproducible across
hosts, while the indexed frame is what DBSIM produces and the presenter
consumes. The record is the [capture record](#capture-record) (a separate
file, never the tick log), keyed by the frame counter `[0x4d257b]` at the
present (the main-loop present of frame N happens before the increment to
N + 1, [main_loop.md](main_loop.md)), the site (return address of
`0x464a14`) and an ordinal per (`frame`, `site`).

Capture point: the hooked `StretchBlt` slot (GDI; arguments give the
source DC and rectangle, the caller's EBP gives `site`) or the windowed
`Unlock`/`Flip` (DirectDraw). The hash runs on the main thread inside the
present: wall time only, no clock through DBSIM, no message, no game memory
written, so the tick log is unchanged ([Live evidence](#live-evidence), pass A).

**Passes.**

1. **A (determinism and baseline).** Capture on, all hooks original, twice
   per suite: tick logs identical to the goldens and the two capture files
   identical for all records with a main-loop or slide `site`. Records from
   modal loops are compared as the set of distinct (`index_sha256`,
   `palette_sha256`) per `frame`, because their count follows message
   traffic.
2. **B (replaced mode).** The same with the replaced hook selection
   (`hooks.md`): goldens identical and capture identical to A (a difference
   localises a hook that changes presentation-visible state the tick log
   does not compare).
3. **C (presenter).** Presenter on, capture on: the same comparison against
   A, plus the goldens.
4. **Offline.** The presenter's conversion and scaler applied to frames
   dumped in A (ignored scratch only, never committed) match a reference
   implementation (palette lookup; at 1:1 the RGB equals the table lookup
   exactly).

The compare is `presenter_capture.py compare` (two reports) or
`dbsim_frames.py compare` ([Comparing against it](#comparing-against-it));
the offline pass is `presenter_capture.py check-dumps <dump dir> --offline
presenter_offline.exe --client WxH ...`.

**Fixed frames for the spot check** (dumps, besides the full sequences):
acceptance-60s frames 100, 750, 1,499 (no page-origin moves, 177 uploads);
c3_09-loadout-b frames 278, 279, 280, 290 (Esc at 279: the 0→1 transition
starts in presentation frame 279 and finishes in 280,
[display_modes.md](display_modes.md)), 308, 309, 310 (Esc back at 309),
the frames of the first jolt (the first frame with `S` ≠ the previous `S`
while `0x42d4a8` runs; read from the capture file), and one frame on each
side of each of the 52 palette installs; c4_08 and c3_10: the frame of the
death switch to the outside view (mode 4, border `0x42da08`) and the frame
after it.

## Capture, own GDI presenter, windowed DirectDraw

The proxy's per-present capture, the own GDI presenter and the DirectDraw
branch in a window. No SDL3.

Files: [presenter.h](../../tools/reference-game/proxy/presenter.h) (capture,
GDI presenter, installation), [presenter_frame.h](../../tools/reference-game/proxy/presenter_frame.h)
(layout, conversion and scaler: pure, shared by the DLL, the offline
tool and the fixtures; SHA-256 from the proxy's shared
[sha256.h](../../tools/reference-game/proxy/sha256.h)),
[ddraw_windowed.h](../../tools/reference-game/proxy/ddraw_windowed.h)
(own DirectDraw objects), [present_site.h](../../tools/reference-game/proxy/present_site.h)
(site announcements), [presenter_offline.c](../../tools/reference-game/proxy/presenter_offline.c)
(offline pass), [presenter_capture.py](../../tools/reference-game/scripts/presenter_capture.py)
(compare, summary, sessions, offline reference rendering), fixture
`proxy/tests/presenter_fixture.c`.

### Configuration

Keys of the section `[presentation]`, read once at the first top-level
`CreateWindowExA` (before the window and the DIB exist) from the first of:
the INI named by `ES2_PRESENTATION_CONFIG`, the measurement INI
`ES2_ORACLE_SPEED_CONFIG`, `es2-oracle.ini` beside the proxy (a player's
setting). `directdraw` is read from the same file when the proxy resolves
its first export.

| Key | Values | Effect |
|---|---|---|
| `capture` | 0 (default), 1 | per-present records ([Capture record](#capture-record)); needs `capture_report` |
| `capture_report` | a path, or `beside_tick_log` | the capture file, created `CREATE_NEW`; an existing or unusable file refuses the capture only (event `presentation_capture_refused`, [below](#capture-in-an-interactive-session)). A path may contain `{pid}`; `beside_tick_log` = `<[trace] tick_log of es2-oracle.ini>.capture.ndjson` (not `*.jsonl`: `check.ps1` and `tools/evidence_budget.py` treat a `*.jsonl` in a run directory as a raw tick log) |
| `capture_dump_frames` | frame numbers, comma or space separated (at most 64) | an ES2F dump of every hashed record of those frames |
| `capture_dump_range` | `<from>-<to>[/<step>]` | a regular series of frames dumped besides `capture_dump_frames` (at most 4,096 frames and 8,192 dump files per process); a malformed or too long range is refused with the event `presentation_dump_range_refused` and switches only the range off; the start line gets `"dump_range":[from,to,step]` only when a range is set |
| `capture_dump_dir` | a directory | dumps go to `<dir>\<capture file name>.frames\`; without it, `<capture file>.frames\`. Dumps are decoded game frames: ignored scratch only, never archived evidence |
| `capture_regions` | 0 (default), 1 | adds the display mode and the region hashes of `frame_regions.h` to every hashed record and `"regions":"<table id>"` to the start line ([own_process.md "Region hashes on both sides"](../reference-game/own_process.md#region-hashes-on-both-sides)) |
| `capture_qpc` | 0 (default), 1 | a QueryPerformanceCounter time per record (needs `capture=1`; [Capture record](#capture-record)) |
| `presenter` | `original` (default), `gdi` | the own GDI presenter; any other value refuses (event `presentation_presenter_unknown`) |
| `directdraw` | `original` (default), `windowed` | own DirectDraw objects instead of the system's (event `presentation_directdraw_windowed`; other values: `presentation_directdraw_unknown`) |

With `capture` and `presenter` absent no import slot changes; the proxy then
adds only one more wrapper in the `CreateWindowExA` chain (presenter, window
shim, input journal, USER32) and the INI reads at window creation. Without
`capture_dump_range`, `capture_regions` and `capture_qpc` every byte the
capture writes is the base format.

### Capture in an interactive session

An interactive session runs several DBSIM processes with one INI (VSHELL,
DBSIM, VSHELL, DBSIM, ...).

- **Report name.** Every `{pid}` in `capture_report` is replaced by the
  decimal process id (`GetCurrentProcessId()`) before the file is created,
  for example in a player's `es2-oracle.ini`:

  ```ini
  [presentation]
  presenter=gdi
  capture=1
  capture_report=captures\mission.{pid}.capture.ndjson
  ```

  Dumps go to `<report name>.frames` (or `<capture_dump_dir>\<report
  name>.frames`), so they are per process too. The file stays `CREATE_NEW`:
  a stale file of the same name (pid reuse) is never overwritten.
  `beside_tick_log` takes no token. A name that does not fit `MAX_PATH`
  after the expansion refuses the capture like an unusable path.
- **Start line.** It ends with `"pid":<n>`, the number in the file name
  (schema stays 1).
- **Presenter independent of the capture.** When the report cannot be
  created (existing file, missing directory, unusable name) the event
  `presentation_capture_refused` is logged and only the capture is off.
  With `presenter=gdi` the slots are replaced and the presenter runs (event
  `presentation_gdi`, no records, no dumps); with `presenter=original`
  nothing is installed.
- **Reader.** `presenter_capture.py sessions "<capture_report value>"`
  lists the per-process reports of a session (`{pid}` matches any text;
  shell globs work too): file, pid, presenter, record count, first and last
  frame, oldest first; exit 0 with at least one report, 1 with none.
  `summary` and `compare` take a single report.

Oracle runs (`beside_tick_log`, one DBSIM process per run directory) never
reach the refusal path, and no reader of oracle captures depends on the
`pid` field or a pid in a file name.

### Seam and installation

From the bytes:

- DBSIM's IAT slots `StretchBlt` `0x6cc4c4` (thunk `0x496a2a`) and
  `SetDIBColorTable` `0x6cc4c8` (thunk `0x496a30`). The only references in
  the reference code section: `call 0x496a2a` at `0x4659ad` (present) and
  `0x4655e3` (clear), `call 0x496a30` at `0x465de5` and `0x465f00`, one
  `jmp [slot]` each; 29 `call 0x464a14` (fixture mode `reference` on
  ES2_DATA_ROOT's DBSIM.EXE).
- Checked before anything is chained (DllMain, memory reads only): both
  thunks, the `CreateWindowExA` thunk, and the code bytes the frame layout
  depends on: `0x464a14` (no frame of its own, [GDI branch](#gdi-branch-the-oracle-path)),
  the prologue of `0x465628` (`push ebp; mov ebp, esp; add esp, -0x100`),
  `lea ecx, [ebp-0x100]` at `0x4658a7` (the RECT of `GetClientRect`), the
  four GDI calls above, and the DirectDraw calls `call [ecx+0x80]`
  `0x465870`, `call [edx+0x2c]` `0x465892` (present Unlock/Flip) and
  `call [edx+0x80]` `0x465524`, `call [eax+0x2c]` `0x465547` (clear). One
  changed byte refuses (fixture `mismatch` flips the last byte of each entry
  and of each thunk).
- Present detection: the return address of the `StretchBlt` call
  (`0x4659b2` present, `0x4655e8` clear, other callers counted as
  `other_blits`). EBP on entry is the frame pointer of `0x465628` (the thunk
  is a `jmp`): `site` = `[ebp+0x10]` (the return address of `0x464a14`,
  [Present sites from own code in the proxy capture](#present-sites-from-own-code-in-the-proxy-capture)),
  client RECT = `[ebp-0x100..-0xf4]`. Calls from another thread than the
  window's creator are forwarded and counted (`foreign_thread`).

### Capture record

One JSON line per present. Header
`{"event":"start","schema":1,"key":"frame_counter_0x4d257b","presenter":...,"dump_frames":n,"pid":p}`
(plus `"dump_range"` and `"regions"` when set); then per present of
`0x465628` (GDI: the `StretchBlt` at `0x4659ad`; DirectDraw: Unlock
`0x465870` or Flip `0x465892`):

| Field | Meaning |
|---|---|
| `frame` | dword `[0x4d257b]` at the present |
| `site` | reference VA of `[ebp+0x10]` in the frame of `0x465628`, e.g. `0x45fbe4` (main loop), `0x42b13a` and the other slide returns, `0x45490b` (UI screen) |
| `ordinal` | number of earlier records with the same (`frame`, `site`) |
| `mode` | dword `[0x4d255e]` |
| `S`, `R` | dwords `[0x4d3018..]` (x, y), `[0x4d2ff8..]` (left, top, right, bottom) |
| `client` | GDI: the `GetClientRect` result of `0x465628`; DirectDraw: the client rectangle the presenter drew into |
| `src`, `dst` | GDI: the source and destination rectangles of `StretchBlt` as DBSIM passed them; DirectDraw: the surface (0, 0, W, H) and the client size |
| `source` | `dib` (source DC = `[0x4d3034]`, `SRCCOPY`, rectangle inside the DIB), `dib_outside`, `other` (another DC or ROP), `surface` (DirectDraw) |
| `index_sha256` | SHA-256 of the `w x h` bytes at `[0x4d3028]` + S, row by row with stride `[0x4d2546]` (DirectDraw: the surface rows with the pitch as stride, after the cursor sprite DBSIM drew); `null` unless `source` is `dib` or `surface` |
| `palette_sha256` | SHA-256 of 256 (red, green, blue) triples, in that byte order, of the last `SetDIBColorTable` table for the DIB DC (reserved byte excluded), or of the DirectDraw palette set on the primary; `null` before the first upload |
| `uploads` | count of recorded `SetDIBColorTable` calls on the DIB DC (DirectDraw: `CreatePalette` + `SetEntries`) |
| `presented` | `original` (presenter off), `gdi` (drawn by the presenter), `forwarded` (presenter on, original `StretchBlt` ran), `windowed` (DirectDraw presenter) |
| `display`, `regions` | only with `capture_regions=1`: the display mode and the region hashes |
| `qpc` | only with `capture_qpc=1` (start `"schema":2` with `qpc_frequency`; [own_process.md "A time per DBSIM capture record"](../reference-game/own_process.md#a-time-per-dbsim-capture-record)), the last field: QueryPerformanceCounter on entry to the present. GDI: on entry to the `StretchBlt` hook, before the hash and the presenter's draw; DirectDraw: on entry to the present of `Unlock` or `Flip`, before the palette conversion, the windowed draw and the hash |

Records of a frame are buffered and written when the first present of a
later frame arrives (a stopped process loses at most its current frame; the
runner stops DBSIM with `Stop-Process`); every 500 frames and at process
detach an `{"event":"stats",...}` line carries the counters
(`announced_sites` and `unattributed_sites` among them) and the wall-time
cost (`capture_qpc`, `present_qpc`, totals and maxima, `qpc_frequency`). The
clear (`0x46548c`) is never recorded.

**ES2F dump:** `ES2F`, u32 LE version 1, frame, site, ordinal, width,
height, uploads; 768 bytes of RGB triples; the `width x height` index bytes
top-down. `es2sim --dump-frames` writes the same format.

Inside the present the capture reads game memory, hashes, appends to an own
buffer and writes the own file; no clock through DBSIM (QPC from kernel32
directly), no message, no game memory written (the fixture compares the
synthetic globals around every present). It leaves different stack residue
below DBSIM's ESP than GDI's `StretchBlt` would; pass A shows the tick log
unchanged.

### Present sites from own code in the proxy capture

With `mission_root=replaced` the core's `0x461608`
(`core/src/mission_root.c`, `mr_call(&s, 0x464a14u, 0x461e21u, ...)`) calls
the present through the adapter's stack bridge (`ai_behaviour_stack_call`,
or `logic_frame_machine_call` without the shim). The machine call
(`es2_machine_x86_enter`, `hook/machine_x86.S`) pushes its own return
address into the site slot `[ebp+0x10]`; the original's `0x461e21` is
written there only after the call returns, so the raw site is an address
inside `ddraw.dll`. Own callers of `0x464a14`: only `mission_root.c` (the
call at `0x461e1c`); no own code calls `0x465628` directly.

**The rule** (`presenter.h` `pr_present_site`, the same order as es2sim's
[`call_site()`](#present-sites-from-own-code)):

1. The raw return address lies in CODE (`[base+0x1000, base+0x97000)`, the
   `.text` section, checked against the PE header by the fixture's
   `reference` mode): its image VA.
2. An announcement is current whose target is `0x464a14` and whose return
   address is a CODE address right after an `e8` call of `0x464a14` (bytes
   checked in the loaded image): that VA.
3. The announcement's caller is a row of `pr_own_present_callers`
   (`0x461608 -> 0x461e21`) and the row's site is right after an `e8` call
   of `0x464a14`: the row's site.
4. Otherwise the raw address, counted in `unattributed_sites`. It stays an
   8-digit DLL address, which `dbsim_frames.py verify` and `compare` refuse
   ("bad site").

Rules 2 and 3 are counted in `announced_sites`; both counters are in the
stats line and in the manifest's `last_stats`.

The announcement is `proxy/present_site.h`: `es2_present_site_begin(target,
caller, ret)` / `es2_present_site_end(previous)`, nesting, main thread
only, always compiled (the in-vitro harness compiles them but never reads
them). The hook body of `0x461608` (`mission_root_hooks.h`, both
`mission_root_stack_call` and `mission_root_plain_call`) wraps each bridge
call with the callee, the entry `0x461608` and the core's absolute return
address; the capture uses it only when the callee is the present. A new own
caller of the present needs an announcement in its adapter, a
`pr_own_present_callers` row, or both.

### GDI presenter (`presenter=gdi`)

Takes over the present-site `StretchBlt` only in the full-screen case:
source = the DIB DC with `SRCCOPY` inside the DIB, `w x h` = screen
`[0x4d253e]` x `[0x4d2542]`, destination (0, 0, client.right,
client.bottom) with client.left = client.top = 0, and a recorded colour
table; otherwise (and for the clear and every other caller) the original
runs unchanged and `forwarded` counts it. Then:

1. Layout (`pr_compute_layout`): k = min(cw / w, ch / h); k >= 1: image
   `w*k x h*k`; k = 0 (client smaller than the screen): the largest
   aspect-correct size, width = cw and height = h*cw / w when cw*h <= ch*w,
   else height = ch and width = w*ch / h (at least 1); offsets
   ((cw - width) / 2, (ch - height) / 2), rounded down. Clients over
   4096 x 4096 pixels are refused (forwarded).
2. Conversion and scaling (`pr_render`): output pixel (x, y) inside the
   image shows source ((x - ox)*w / width, (y - oy)*h / height) through the
   table (0x00RRGGBB, i.e. blue, green, red, 0 in memory), everything else
   black. At 1:1 this is exactly the table lookup of the source window.
3. One `StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, cw, ch, pixels, 32-bit
   top-down BI_RGB, DIB_RGB_COLORS, SRCCOPY)` into the `BeginPaint` DC
   DBSIM passed; 0 or `GDI_ERROR`: the original `StretchBlt` runs instead
   (`draw_failures`).
4. Returns TRUE (DBSIM ignores the result), LastError as on entry.

Kept: `GetClientRect`, `InvalidateRect`, `BeginPaint`, `EndPaint`,
`SelectPalette`/`RealizePalette` (keep-list item 7), every
`SetDIBColorTable` and `AnimatePalette` (forwarded: the DIB and the logical
palette stay as in the original). The table used is the one DBSIM uploads
(`0x465cc4` with the static system colours at both ends, or `0x465e28`).
Buffers are on the process heap (never DBSIM's allocator).

### DirectDraw branch in a window (`directdraw=windowed`)

At the seam of the observer (the proxy's `DirectDrawCreate` export,
`initialize_forwarding`), `ddw_create` hands out own v1 objects; with the
observer also on (`-Perf`, `-DeferFlip`, `-PresentationEvery`) it wraps them
like the system objects. Every method DBSIM calls succeeds (the return-code
contract of the [table](#directdraw-branch)):

- `SetCooperativeLevel(hwnd, 0x57)`: records the window, no exclusive mode;
  `SetDisplayMode(W, H, 8)`: records the size, the desktop mode stays (other
  depths: `DDERR_INVALIDMODE`).
- `CreateSurface` with DBSIM's descriptor (caps without `DDSD_CAPS`): a
  primary of W x H, 8 bit, own memory, pitch W rounded up to 4; with
  `DDSCAPS_FLIP` and a back-buffer count one attached back buffer
  (`GetAttachedSurface(DDSCAPS_BACKBUFFER)`). `IsLost`, `Restore` succeed;
  `Lock` returns the memory (a second Lock: `DDERR_SURFACEBUSY`).
- `CreatePalette(DDPCAPS_8BIT, entries)`, `SetEntries`, `SetPalette`: the
  palette set on the primary is the colour table.
- Present: the Unlock of a non-flipping primary (W >= 640), or `Flip`
  (W < 640: the back buffer); converted and scaled as above into the
  window's client area through `GetDC`/`StretchDIBits`/`ReleaseDC` (no
  message). `Flip` does not swap the buffers (DBSIM rewrites every row
  before each Flip and never reads a surface).
- Every other method: `DDERR_UNSUPPORTED`, with the right stdcall argument
  count. `QueryInterface`: `E_NOINTERFACE`. No system DirectDraw object.

Limits (DBSIM's code, unchanged): the toggle `0x4667c8` still moves the
window `HWND_TOPMOST` to the `AdjustWindowRect` of (0, 0, W, H), clips the
cursor to the screen rectangle (0, 0, W, H) and hides the Windows cursor
when a sprite is set; the sprite is drawn at `GetCursorPos` screen
coordinates, so in a window it is offset by the window's client origin, and
the clip holds the pointer in the top-left W x H of the screen. A fix needs
the input path, not the presenter. Capturing DirectDraw presents needs the
observer off (with it, the return address of Unlock/Flip is in the
observer).

### Tests

`//tools/reference-game/proxy:presenter_<mode>_test` (fixture
`presenter_fixture.c`, synthetic image in DBSIM's call shapes) with the
modes `capture`, `capture_gdi`, `presenter`, `gdi_real` (real GDI32),
`beside`, `exists`, `unknown`, `noreport`, `off`, `mismatch`,
`journal_only`, `session` (two real processes, one INI with `{pid}`),
`exists_gdi`, `unusable_gdi`, `range`, `range_refused`, `own_site`, `sha`,
`scaler`, `cost`, `reference` (the seam bytes on the real `DBSIM.EXE`, with
`ES2_DATA_ROOT`), `ddraw` and `ddraw_off`;
`//tools/reference-game/scripts:presenter_capture_unit_test` (comparison
rules, summary, layout, `SessionsTest`, the offline pass). The announcing
hook body of `0x461608` is covered in vitro by
`//tools/reference-game/invitro:differential_mission_root_test`.

Mutations, each caught: `site` from `[ebp+0xc]` (capture), offset rounding
up (scaler), blue from the reserved byte in the palette hash (capture),
Unlock of a flipping primary presenting (ddraw), `{pid}` expansion without
replacement (`session`), the refusal that returns instead of continuing
(`exists_gdi`, `unusable_gdi`), the range's step test (`range`), site rule 2
returning the raw address (`own_site`).

**Cost** (fixture `cost`): capture 2.5 ms and presenter 4.6 ms per present
(640 x 480 into 1920 x 1080), bounded at 15 ms and 40 ms. Wall time only;
virtual time does not see it.

### Live evidence

- **Passes A, B, C and offline** (acceptance-60s, c1_01-collision, c3_09-loadout-b, c4_08-razor-plasma): in
  A1, A2, B (every hook replaced) and C (`presenter=gdi`) every suite is
  identical to its golden and `presenter_capture.py compare` against A1
  gives exit 0. **The capture leaves the tick log unchanged**; the replaced
  simulation draws the same indexed frames and palettes; in C every present
  goes through the own presenter (`forwarded` 0). Every record has mode 0
  and every main-loop frame exactly one record. Offline: every dump at five
  client sizes, no failures.
- **Sites from own code** (every hook replaced): all suites identical, every site a CODE
  address, `unattributed_sites` 0 and `announced_sites` 1 in every manifest.

### Visual check

Seen on a play clone with every hook original and no capture; the proxy
logs its events per process.

| `[presentation]` | 256COLOR layer | Proxy log | Result |
|---|---|---|---|
| `presenter=gdi` | on | `presentation_gdi` in both DBSIM processes | accepted: cockpit, map, damage palette swap, fades, flicker, resizing (larger: integer steps with black borders; smaller than 640 x 480: a fitted image) |
| `presenter=gdi` | off (`-UseNativeColors`, for ES, VSHELL and DBSIM) | `presentation_gdi` | accepted |
| `presenter=gdi`, `directdraw=windowed` | off | `presentation_gdi`, `presentation_directdraw_windowed` | accepted: Alt+Enter in a mission keeps the game in its window and running; the cursor sprite is offset by the window frame (the limit above) |

`presentation_directdraw_windowed` is logged when the proxy sets up its
exports, not at a DirectDraw call; that the branch ran is the visual
observation. Judged by eye, without a capture and without a side-by-side
comparison with `presenter=original`.

## Frame capture per suite

The reference for renderer, HUD and presenter work: for each of the 40
oracle suites the SHA-256 of the 8-bit frame and of the colour table after
every presented frame, produced by the original DBSIM with every hook
original. Tools: the capture of [presenter.h](#capture-own-gdi-presenter-windowed-directdraw),
[`capture-dbsim-frames.ps1`](../../tools/reference-game/setup/capture-dbsim-frames.ps1)
(the runner) and [`dbsim_frames.py`](../../tools/reference-game/scripts/dbsim_frames.py)
(`build`: capture report to table and manifest; `verify`: manifest against
table; `compare`: two tables or reports; `regions`: every kind of
difference, [own_process.md "The frame compare as tests"](../reference-game/own_process.md#the-frame-compare-as-tests)).

Every suite has status `ok` and every record mode 0; the repeated captures
are compared in [The original's own frame spread](#the-originals-own-frame-spread).

### The runner

```powershell
tools\reference-game\setup\capture-dbsim-frames.ps1 `
    -InstallName timing-r111,timing-r111-p1,timing-r111-p2,timing-r111-p4,timing-r111-p5 -HiddenWindow
```

It writes the INI `[presentation] capture=1 / capture_report=beside_tick_log`
(plus the dump keys with `-Dump`), sets `ES2_PRESENTATION_CONFIG` for the
duration of the call, runs `run-oracle-parallel.ps1` (check mode, no
`-Hooks` unless given, the suites named or all of the registry;
`-PreparedProxy`, `-HiddenWindow`, `-GoldenRoot`, `-SuitesFile` handed on)
into `<DataRoot>\logs\dbsim-frames-work-<stamp>`, and then, per suite, finds
`<suite>.jsonl.capture.ndjson` in the suite's `run` directory and calls
`dbsim_frames.py build`. The environment is given back as the caller had it
(the `Get-RunnerEnvironment` guard of the other runners). Lists may be given
as `a,b` (the form `pwsh -File` hands over).

`-WindowedDirectDraw` (needs a `-Label`) captures DBSIM's DirectDraw branch:
it adds `directdraw=windowed` to the INI and hands the game `-Z1`
([own_process.md "The DirectDraw branch in a capture run"](../reference-game/own_process.md#the-directdraw-branch-in-a-capture-run)).

Dumps (`-Dump` with `-DumpFrames 100,278` and/or `-DumpRange 100-400/4`):
ES2F files into `<ScratchRoot>\dumps\<capture name>.frames`, default
`<repository>\build\dbsim-frames-<date>[-<label>]` with a `.gitignore` of
`*`; never in the evidence directory or either repository. `-Dump` without
a selection is refused, as is a selection without `-Dump`; the manifest
names the directory and counts the files.

**Size and time.** About 43 bytes per record (c3_09-loadout-b, the longest
suite: 9,764 records, 420 KB, about 25 s of hashing); all 40 suites about
4 MB, every file far below the 5 MiB of `evidence_budget.py`. Run time is
the regression's ([oracle_regression.md](../reference-game/oracle_regression.md))
plus the prepared-proxy build when `-PreparedProxy` is not given.

### The frames table and manifest

The table keeps the [capture record](#capture-record)'s `frame, site,
ordinal, mode, S, source, index_sha256, palette_sha256` per record (the
hashes read inside the present at the `StretchBlt` call `0x4659ad`, before
the presenter or GDI converts anything; `-` for a non-DIB source and before
the first upload) in `frames.tsv.gz` (tab-separated, header line
`#frame...`, `-` for null, deterministic gzip with no name and no time; a
capture with region hashes adds six columns and a second header line naming
the region table). The report's other fields stay in the report, which the
run keeps only in the runner's work directory.

Result, in `<EvidenceRoot>\dbsim-frames-<date>\` (`EvidenceRoot` defaults to
`<DataRoot>\logs`, `<date>` to today in UTC, `-Date yyyyMMdd`):

| File | Content |
|---|---|
| `<suite>\manifest.json` | schema 1, kind `es2-dbsim-frames`: `suite`, `date`, `status`; `run` (result against the golden, the run's configuration, `frame_limit`); `capture` (report identity, start line, `closed`, last counters); `frames` (table identity with `table_sha256`, counts and per-site/mode statistics). |
| `<suite>\frames.tsv.gz` | the table above |
| `session-<stamp>.json` | the invocation: clones, hooks, dump selection, status per suite |

Nothing else enters the evidence directory: no capture report, no tick log,
no dump. A suite already present is never replaced (the run is refused
before anything starts); a later invocation may add suites to the same date.
`-Label <name>` puts a run into `dbsim-frames-<date>-<name>`; a run with
`-Hooks` needs one, so the baseline directory always holds the run with
every hook original.

`manifest.status`: `ok`; `run-different` (not identical to its golden: the
table is written but is not a reference); `no-capture` (no report: refused,
`presentation_capture_refused`, or no window reached); `unusable-capture`
(no schema-1 start line); `no-records`. The script exits 1 when any suite is
not `ok` and then keeps the work directory (`-KeepWork` always).
`capture.closed` is true when the proxy ended orderly (last line = the final
counter line, counting every record). **A runner that stops DBSIM leaves
`closed` false and loses the records of its current frame**: suites with a
`frame_limit` are stopped at the end of their window; this is not a
failure. The capture runs to the end of the replay, so it can contain frames
beyond `run.frame_limit`, the window the tick-log golden covers. A second
DBSIM process in one suite would have its report refused under
`beside_tick_log`; the manifest names `capture_candidates` and the start
line's `pid`.

### Comparing against it

1. Run the candidate with the same capture switch: the same INI
   (`capture=1`, `capture_report=...`) on the suite's replay, for the own
   `dbsim.exe` through the same proxy seam or `es2sim --frames`
   ([Headless frame capture](#headless-frame-capture)), or the oracle with
   the hooks replaced (`-Hooks ... -Label`).
2. `python -B tools\reference-game\scripts\dbsim_frames.py compare <...>\dbsim-frames-<date>\<suite>
   <candidate>`, the candidate a manifest directory, a `frames.tsv.gz` or a
   capture report. The rules are those of `presenter_capture.py compare`:
   main-loop and display-mode-slide records must agree one by one in
   `frame, site, ordinal, mode, S, source, index_sha256, palette_sha256`;
   records of every other site (modal screens, loading, `WM_PAINT`) are
   compared per frame as the set of distinct (index, palette) pairs, without
   the site; only frames up to the last frame both contain are compared (so
   `table_sha256` alone is not a determinism check). Exit 0 identical, 1
   different (the first sequence difference and the first differing frame
   set, with both sides), 2 unusable.
3. To look at a difference, dump the frames around it from both runs
   (`-Dump -DumpFrames <n>` or `-DumpRange <n-5>-<n+5>`); the ES2F files are
   read with `presenter_capture.read_dump`, and `frame_dumps.py compare`
   shows where two dumps differ.

`dbsim_frames.py verify <suite dir>` checks a stored manifest against its table.

### Tests

`//tools/reference-game/proxy:presenter_range_test` and
`presenter_range_refused_test`; `//tools/reference-game/scripts:dbsim_frames_unit_test`
(build, gzip, `closed`, torn lines, verify, compare, the refusal of an
8-digit site `0x10023387`); `//tools/reference-game/setup:capture_dbsim_frames_test`
(the runner under PowerShell 5.1 and 7 with a mock regression child).
Mutations caught: `capture_report=beside_tick_log`
replaced (runner test), the palette dropped from the comparison fields
(`dbsim_frames_unit_test`).

## The original's own frame spread

Three captures over the 40 suites: A1 and A2 with every hook original, R
with all hooks of `proxy/hook-names.txt` replaced. Every tick-log result
was `identical`.

- **The original against itself:** A1, A2 and a third capture of the original
  agree in 39 of 40 suites. In `c3_10-detail-options` the frame set of frame 600
  (the detail options dialog, 146 or 147 presents) differs between all
  three, and nothing before it: the original's frames are deterministic
  except inside a modal options dialog. Hypothesis: the asynchronous
  `0xa000`/`MM_MCINOTIFY` messages in the dialog pump
  ([main_loop.md "The message trace of the replay pump"](main_loop.md#the-message-trace-of-the-replay-pump)).
  The own frame compare treats that set as the original's spread
  (`sim/tests/frames_known.py`). `c1_01-options-dialog` agrees in all three.
- **Every hook replaced (R).** Without the site announcement the first
  frame's second present records a proxy address (`0x7xxx3257`) as its
  site, which `dbsim_frames.py` refuses ("bad site");
  [Present sites from own code in the proxy capture](#present-sites-from-own-code-in-the-proxy-capture)
  attributes it (A1: `0x461e21`). R's tables run on past A1's last frame
  while the suites stay identical (hypothesis: the frame limit's stop takes
  a longer path with the hooks replaced; [open](#open-questions)). With
  sites normalised and R cut at A1's last frame, R = A1 in 38 of 40 suites;
  `c3_10-detail-options` differs at frame 204 (same present count, other
  index content) and `c1_01-options-dialog` at frame 300 (site `0x45490b`
  instead of `0x45720a`): with every hook replaced the dialog frames leave
  the original's spread.

## The presentation frame in the own process

Own code in `sim/src/present.c` (`sim/include/es2/sim/present.h`) and
`sim/src/startup.c` (the teardown); rows in
`sim/dispatch/services/presentation_frame.h`, `presentation_loads.h`,
`presentation_roots.h`, and the present row in `mission_load.h`. "In vitro"
means `//tools/reference-game/invitro:differential_<stem>_test` (original
machine code against the own code, GDI/USER imports stubbed, `GetTickCount`
on the virtual clock): `present` (`present_cases.h`), `presentation_loads`
(`presentation_loads_cases.h`), `presentation_roots`
(`presentation_roots_cases.h`) and the frame part of `display`
(`display_cases.h`). "Unit" means `//sim:present_test` (synthetic image).

### The frame sequence `0x45fba4`

Listing `0x45fba4..0x45fca6`; row `present_frame`.

- **Present branch**, taken when word `[0x4d2566]` is nonzero:
  1. If the signed byte `[0x4d2537]` is 1, the surface `[0x4d2568]` is selected (`0x481224`).
  2. `0x42a9c0([0x4d24c0])`.
  3. The palette cycler `0x4306ac`.
  4. The palette flush `0x43048c`.
  5. The present `0x464a14`, return site `0x45fbe4`.
  6. `0x42a4c4([0x4d24c0])`.
  7. If the byte is still 1, the frame surface `[0x4d24bc]` is selected again.
- **Page-swap branch**, taken when the word is zero (not in the oracle):
  1. Word `[0x4d267e] = (word [0x4d24f2] == 0)`.
  2. `0x42a9c0`, `0x4306ac`, `0x43048c`.
  3. With `s = [0x4d24bc]`: `[s+0x34] = [s+0x8c]`. If `s` is the current surface `[0x6c5ed0]`, the value also goes to `[0x4a3682]` and to the driver slot `[0x4a57f4]`.
  4. `[s+0x3c] = [s+0x88]`. If `s` is current (`[0x6c5ed0]` is read again), the value also goes to `[0x4a368a]` and to the slot `[0x4a57f0]`.
  5. The dwords `+0x88` and `+0x8c` are swapped.
  6. `0x42a4c4`.
- **Registers:** the last callee's. None of the nine callers (`0x45d1c9`,
  `0x45d20d`, `0x45d3f5`, `0x45d43b`, `0x45daf9`, `0x45dd07`, `0x45dd49`,
  `0x45dd9c`, `0x45f4a0`) reads them.
- **Clock:** exactly one clock call per frame, the cycler's.
- **Design:** the own sequence calls the present as own code
  (`es2_sim_present_frame`), not through the dispatch; the row `0x464a14`
  serves the same `present()` for the guest callers. The surface switch is
  `es2_sim_gl_surface_select`.
- **Verification:** listing; unit (the call order `0x42a9c0`, clock, flush,
  present, `0x42a4c4` in the present branch; the page swap with the surface
  current and not current; the surface switch for byte 1 but not 2); in
  vitro, the frame part of `display` (`0x45fba4` around the rows
  `0x42a9c0`/`0x42a4c4`, with `[0x4d2537]` = 1 through `0x481224`).

### The palette cycler `0x4306ac`

Listing `0x4306ac..0x4307ad`; row `palette_cycle`.

- The clock (`0x4306b6`) is read once.
- **Per slot** `k = 0..4`, with record `R = [0x4cfd64+4k]`, deadline `[0x4cfd78+4k]` and index word `[0x4cfd8c+2k]`, the slot is processed when R is nonzero and the deadline is below T (unsigned `jae`):
  1. `first = sx [R]`, `n = sx [R+2]`, base = `[[0x49b020]+8] + 4·first`, end = base + 4n − 4.
  2. src = base + 4·sx index.
  3. n dwords are copied into a local 0x400-byte buffer. Before each read, src wraps to base when above end (unsigned).
  4. `0x4303c4(first, n, buffer)` is called, also when n ≤ 0.
  5. The index is incremented and reset to 0 when it equals n.
  6. The deadline becomes `sx [R+4] + T`.
- **Registers:** per slot, EDX = the slot address. A waiting slot leaves
  EAX = the deadline address and EDX = the deadline; a cycled slot leaves
  EAX = the new deadline, EDX = its address and ECX = the index address.
  EAX starts at 0.
- **Own deviation:** n > 256 is refused (it would overrun the original's
  stack buffer).
- **Verification:** listing; in vitro 1000 cases identical (EAX, EDX, DATA,
  palette memory) covering empty, due and waiting slots, the wrap, index
  past n, and n ≤ 0; unit: register values and the n > 256 refusal.

### The fade `0x4308fc` and `0x430b34`

Listing `0x4308fc..0x430b31`, `0x430b34..0x430d07`; rows
`palette_fade_setup`, `palette_fade_step`.

**Set-up `0x4308fc(A, B, d)`:**
- Writes A to `[0x4cfd98]`, d to `[0x4cfda0]`, and 0 to the byte `[0x49b03c]`.
- Fills three word tables of per-entry steps: byte 0 at `0x4cfda4`, byte 1 at `0x4cffa4`, byte 2 at `0x4d01a4`. Each step is a signed `idiv`:
  - Mode 2 (A and B): `((b&0xff)−(a&0xff))<<8`, `(b&0xff00)−(a&0xff00)`, `((b&0xff0000)>>8)−((a&0xff0000)>>8)`, each divided by d.
  - Mode 1 (A only): A's values divided by −d.
  - Mode 0 (B only): B's values divided by d.
- The mode goes to word `[0x4d03a4]`. Count n = sx `[A+4]` (B's in mode 0) goes to word `[0x4cfd9e]`.
- EAX = n in mode 1; in modes 0 and 2, n if n > 0, else 0.
- **Refused:** neither palette given (the count is an uninitialised local); d = 0 with n > 0 (#DE); n > 256.

**Step `0x430b34`:**
1. One clock read.
2. If the byte is 0, the deadline `[0x49b038]` = T + d and the byte = 1.
3. If the deadline is below T: the byte = 0, EAX = 0, and EDX/ECX are the clock call's. Otherwise:
   - From entry sx `[0x4cfd9c]` (a start word no code writes), for i < sx `[0x4cfd9e]` (re-read on each pass): the product `((word step·t) & 0xff00) >> 8`, where t = d − (deadline − T).
     - Mode 0 sets bytes 0..2 to it.
     - Modes 1 and 2 copy A's entry and add it bytewise.
     - Other modes write nothing.
   - The dirty range (start, start + count − 1) is set when the byte `[0x49b030]` is 0, else widened (signed min/max).
   - EAX = deadline − T, EDX = the range's last entry, ECX = the count.

**Verification:** listing; in vitro set-up 1000 and step 1500 cases
identical (EAX; EDX where defined; DATA; palette memory), covering every
mode, start, expiry, and setting and widening the dirty range; unit: worked
values.

### The palette upload `0x465cc4(P)`

Listing `0x465cc4..0x465e26`; row `palette_upload`. The steps are the
[Palette contract](#palette-contract)'s; NUMCOLORS is load.h's field
`reserved_entries`. Own rules:

- n ≤ −2 or n ≥ 514 (the original's overrun) is refused; so is the
  DirectDraw `SetEntries` through a null object.
- Headless, NUMCOLORS is −1, so all 256 entries come from P.
- The table goes to the load layer's platform record when one serves the
  loop (the same record `0x465e28` writes), so the capture hashes one "last
  table".
- Own deviations: the quad's fourth byte is written as 0 (the original
  leaves stack residue); entries GetSystemPaletteEntries does not fill are
  zero.

**Verification:** listing; in vitro 800 cases identical (DATA, the
AnimatePalette table, the SetDIBColorTable RGB bytes, handles, call counts)
with NUMCOLORS answers from −1 to 513 and the DIB DC set or unset; unit:
values and refusals.

### The palette set `0x481fe0` with `0x465e28`, `0x465c40`, `0x465a24`

Row `palette_set` (`load.c` `palette_apply`, `palette_entries`). The listing
facts behind the model are in load.c and in
[mission_load.md "The mission load in the own process"](mission_load.md#33-the-mission-load-in-the-own-process).
Further listing facts:

- `0x465e28` indexes the colours through `[P+8]`, re-read every iteration,
  plus 4i. Per entry, byte 3 bit 0 selects the shifted copy (`<< 2`, bytes
  2, 1, 0) and bit 1 the raw copy. An entry with neither bit, and byte 3
  (RGBQUAD's reserved byte) of every entry, are never written: the original
  keeps the stack's bytes there ([Deviations](#deviations)).
- `0x465a24`'s nudge reads the entry again after each match. Its source
  table is the local table, so a NUMCOLORS answer whose half is negative
  (below −1) reads before the table, in the original too.
- `0x4820b7` passes its arguments 2..5 to `0x486e10`, which copies `count`
  (unsigned `loop`) pairs of dwords to `0x4a4fa5 + 8·start`. `0x48d99e`'s
  result (argument 1) is not used.
- **Registers:** not modelled; the row leaves the entry registers. Neither
  caller reads them: after `0x43049a`: `pop ecx`, EDX loaded, EAX zeroed at
  `0x4304c4`; after `0x461722`: `pop ecx`, then `0x464c78`, which writes
  them before reading.
- **In vitro, `presentation_loads` part "palette": 1400 cases identical**
  (the original `0x481fe0` with `0x48d99e`, `0x4820b7`, `0x486e10` as
  machine code, GDI stubbed, `GetSystemPaletteEntries` writing nothing;
  compared DATA, the `SetDIBColorTable` table and ten call counts).
  Caught mutation: in load.c's nudge against the last r entries, `< 0xfcu`
  changed to `< 0xfdu` (case 19, 2 DATA bytes).

### The present row `0x464a14`

Row `present` (`mission_load.h`) → `es2_sim_service_present_capture`
(present.c), as [mission_load.md](mission_load.md#34-the-roots-last-callees-the-terrain-shading-pass)
proposes. It performs `present()` (listing `0x464a14..0x464a26`,
`0x46589a..0x4659e5`): the [GDI branch](#gdi-branch-the-oracle-path)
without guest writes; the load record's present count
(`table(c)->presents`); the frame record with the site of
[Present sites from own code](#present-sites-from-own-code). The DirectDraw
branch (`0x465647..`) is `present_dd()` on the modelled objects
([Platform choices](#platform-choices)).

Deviations: EAX = 1 where the original keeps the entry EAX without a DIB DC
(no caller reads it); a zero game size is refused (the original divides by
zero). Suites that replace the row after the seal (dialogs, display,
`//sim:dialogs_test`) do not depend on its function; dialogs and
`//sim:dialogs_test` replace the `font_load` row after the seal instead of
stubbing `0x430fb0`.

**Verification:** listing; in vitro `present` 1000 cases identical (every
GDI/USER call counted, StretchBlt's eleven arguments, the select-palette
flags), with random game sizes, client areas, rectangles (including 16-bit
truncation) and page origins, palette and DIB DC set or unset.

### Present sites from own code

The original calls the present `0x464a14` from 29 sites. The frame record's
`site` is the return address of the call. An own C caller (`es2_sim_call`)
has return address 0, and a call the trap enters from a core bridge has a
host address (`0x10014097` in the headless runs). The row picks the site in
this order (`call_site()` in present.c):

1. a site announced by `es2_sim_present_site(va)` just before the call (consumed by the next present);
2. a return address inside CODE `[0x401000, 0x497000)`, unchanged;
3. a call (trapped or a machine call) from a known machine caller (table `entry_sites`): when the
   innermost guest entry is the present `0x464a14` and the entry one level out is `0x461608`, the site is
   `0x461e21` (the return address of `0x461e1c call 0x464a14`); `es2_sim_guest_entry_va`
   (machine_sim.c, over the machine backend and the trap) gives the entries;
4. otherwise the raw address, counted in `es2_sim_present.unattributed`.

The own C callers and the original call they stand for (each `call 0x464a14` checked in the listing):

| Own caller | Original call | Site recorded |
|---|---|---|
| dialogs.c `dialog_close` | `0x454906` | `0x45490b` |
| dialogs.c `dialog_step` | `0x454b60` | `0x454b65` |
| dialogs.c `options_step` | `0x457205` | `0x45720a` |
| load.c `loading_screen` | `0x4614a3` | `0x4614a8` |
| loop.c WM_PAINT | `0x4662a7` | `0x4662ac` |

Raw sites that remain possible: `0x46644e` (not reached headless) and any
trapped core caller other than `0x461608`; a new caller needs a
`trapped_sites` row. `//sim:present_test` (`attribution`) checks the four
rules and the CODE bounds.

### The help call and the DirectDraw release

Rows `help_contents` (`0x4669c4`, `es2_sim_service_help_4669c4`) and
`dd_release` (`0x466730`, `es2_sim_service_dd_release_466730`), present.c.

**The help call `0x4669c4(file)`** (listing `0x4669c4..0x4669ea`): if
`[0x4d255e]` (DirectDraw) is nonzero, call `0x4667c8`; then
`WinHelpA([0x4d3610], file, 3, 0)` and return its EAX. In the row,
`WinHelpA` is a platform deviation (no WinHelp on current systems): the
call is counted in the present instance's platform record (`win_help`,
`win_help_args`) and EAX is `win_help_answer` (headless 0, "failed"). With
DirectDraw on, the row calls `0x4667c8` through the dispatch (row
`fullscreen_switch`). Callers: after `0x4345d1` comes `pop ecx` and the
function returns EAX; after `0x460827` comes `pop ecx` and a jump to
`0x46085c`, which writes EAX. Neither reads EDX or ECX.

**The release `0x466730()`** (listing `0x466730..0x4667c7`):

1. If `[0x4d3710]` (the DirectDraw object) is set, call the frame clear `0x46548c`.
2. For `0x4d3718`, `0x4d3714`, `0x4d371c`, `0x4d3710` in that order: if the dword is nonzero, call slot
   +8 (Release) of its vtable with the object as the only argument, then zero the dword.
3. `GetSystemMetrics(SM_CXSCREEN)`, `GetSystemMetrics(SM_CYSCREEN)`, then `ClipCursor` to
   `{0, 0, cx - 1, cy - 1}`; EAX = 1.

The row calls the frame clear and each Release through the dispatch. It
takes the screen size from the platform record (`screen_cx`, `screen_cy`; 0
means 640×480) and counts the two metrics calls and `ClipCursor` with its
rectangle. startup.c's teardown uses it: with no DirectDraw object (every
GDI and headless run) it is just `ClipCursor`; with an object it calls the
row through the dispatch.

**Registers:** both rows leave EDX and ECX as at entry, the original the
system calls'. No caller reads them: see above for `0x4669c4`; for
`0x466730`, WinMain after `0x46547a` and `0x4667c8` after `0x466944` write
them before reading.

**Verification.** In vitro `presentation_roots`: 2000 cases identical (EAX,
DATA with the heap, the stub log; every path: help with DirectDraw off and
on, clear and no clear, each object released and absent). Unit
`//sim:present_test` `help_release`. Caught mutation: swapping the release
order of `0x4d3718` and `0x4d3714` in present.c fails both (case 1, stub
log).

### The load wrappers `0x430f58` and `0x430fb0`

Rows `panel_load` and `font_load` (`presentation_loads.h`), CDECL, one
slot, both with `es2_sim_service_load_by_name` (load.c).

- **Listing:** both are byte for byte the load by name `0x45cdf4` (and
  `0x465f7c`); only the immediate of the exception record differs
  (`0x49b1ac`, `0x49b1d0`; `0x45cdf4` uses `0x49ef70`). The steps:
  1. `0x49630c` links the frame record (fs:0, and the construction counter is read into it).
  2. Word `[ebp-0x40] = 8`.
  3. `0x472f14(ebp-0x30, path)`.
  4. `0x47a144(S, 0)` gives X.
  5. `0x472f64(S, 2)`.
  6. EAX = X, EDX = `[ebp-0x50]`, the fs:0 head at entry, which is stored back to fs:0.
- **Registers:** ECX is the entry ECX in the own row; the original leaves
  `0x472f64`'s ECX. Every caller pops ECX right after the call
  (`0x431104`, `0x431145`, `0x454837`).
- **In vitro, `presentation_loads` part "load": 600 cases identical** (EAX,
  EDX, DATA, the stub log of `stream_open 0x4730e8`, `dts_load 0x47a144`,
  `stream_destroy 0x472f64`).

### Rows and verdicts

Callers from the listing: every `call`/immediate that names the VA. Data
tables were not scanned; [sim_gap.md](../reference-game/sim_gap.md)'s
"referenced only" column covers them.

| VA | Listing | Callers | Verdict |
|---|---|---|---|
| `0x430394` | `0x430394..0x4303ae` | `0x42e093` (the world loader `0x42e010`) | inlined in the row `world_body` (load.c `world_palette`: `[0x49b020] = 0x465f7c(name)`) |
| `0x430d08` | `0x430d08..0x430dff` | `0x42e3c1`, `0x42e41e` (`0x42e010`) | inlined in `world_body` (load.c `range_table`) |
| `0x430ea0` | `0x430ea0..0x430f54` | `0x431c4d` (`0x431bf8`, row `panel_ctor`) | inlined in `panel_ctor` (hud_cockpit.c `panel_canvas`) |
| `0x430f58` | `0x430f58..0x430faf` | `0x4310ff` (`0x431098`) | **row `panel_load`** (`es2_sim_service_load_by_name`); the own `0x431098` inlines it (hud_load.c `hud_shape_one`) |
| `0x430fb0` | `0x430fb0..0x431007` | `0x431140` (`0x431098`), `0x454832` (`0x45464c`, row `dialog_open`) | **row `font_load`** (`es2_sim_service_load_by_name`); dialogs.c calls it through the dispatch; closes the sim_gap called root (live 40/40) |
| `0x431008` | `0x431008..0x431094` | `0x429a94` (`0x429834`, own code in hud_cockpit.c without a row) | inlined there (hud_cockpit.c `panel_object`) |
| `0x431098` | `0x431098..0x4311dc` | `0x431046` (`0x431008`) | inlined in `panel_object` (hud_cockpit.c `panel_texts`); not dead (it has this caller) |
| `0x464a14` | `0x464a14..0x464a26` | 29 sites (the frame sequence, the dialogs, the loading screen, `0x42b135..0x42d040`, the window procedure) | **row `present`** → `es2_sim_service_present_capture` ([The present row](#the-present-row-0x464a14)) |
| `0x464a28` | — | none | a second present entry; not modelled |
| `0x464c10` | `0x464c10..0x464c62` | `0x4651a0` (the window creation `0x465158`) | inlined in startup.c `window` (headless: the screen metrics come from the platform record) |
| `0x465628` | `0x465628..0x4659e5` | `0x464a1e` (`0x464a14`), `0x464ac3` (`0x464a28`, no caller) | inlined in present.c `present()` (GDI branch) and `present_dd()` (the DirectDraw branch on the modelled objects) |
| `0x465a24` | `0x465a24..0x465c3c` | `0x465ec1` (`0x465e28`) | inlined in the row `palette_set` (load.c `palette_entries`) |
| `0x465c40` | `0x465c40..0x465cc0` | `0x465eac` (`0x465e28`) | inlined in `palette_set` (load.c `palette_apply`: counts only, no guest write) |
| `0x465e28` | `0x465e28..0x465f7a` | `0x482009` (`0x481fe0`, row `palette_set`), `0x466228` (the window procedure's palette messages) | inlined in `palette_set` (load.c `palette_apply`). The window procedure refuses messages `0x30f`/`0x311` headless (loop.c) |
| `0x466730` | `0x466730..0x4667c7` | `0x46547a` (WinMain), `0x466944` (`0x4667c8`) | **row `dd_release`**; startup.c's teardown uses it |
| `0x4669c4` | `0x4669c4..0x4669ea` | `0x4345d1`, `0x460827` | **row `help_contents`** |
| `0x4669b4` | `0x4669b4..0x4669c2` | the keyboard handler | `hud_mfd_leaves.h`'s row `display_restore`, reused: DirectDraw off is a `ret`, on calls `0x4667c8` |
| `0x4667c8` | `0x4667c8..0x4669b1` | `0x4345da`, `0x45fea9`, `0x46543c`, `0x4669bd`, `0x4669d0` | **row `fullscreen_switch`** ([The DirectDraw full-screen switch](#the-directdraw-full-screen-switch)); live 0 calls |
| `0x46658c` | `0x46658c..0x4665c2` | `0x466844` (`0x4667c8`) | inlined in `fullscreen_switch` (present.c `dd_create`) |
| `0x4665c4` | `0x4665c4..0x46672d` | `0x466851` (`0x4667c8`) | inlined in `fullscreen_switch` (present.c `dd_mode`) |
| `0x45f13c` | `0x45f13c..0x45f15f` | `0x4669a4` (`0x4667c8`) | inlined in `fullscreen_switch` |

No row exists for the inlined VAs: their only callers are own code that
inlines them, or code that is not reached (`0x464a28`). With DirectDraw off
(the default) `0x4669b4` is a `ret` and `0x4669c4` only calls WinHelpA.

### Headless frame capture

`es2sim --headless ... --frames <path>` writes one ndjson record per present
in the [capture record](#capture-record) format: start line, records, and a
closing `stats` line with `records`. The fields are those of the proxy
capture with presenter `es2sim-headless`, `presented` `headless`, `site`
the [attributed site](#present-sites-from-own-code), `ordinal` per (frame,
site) for up to 64 sites, `source` `dib` or `dib_outside`, the
`index_sha256` window bounded by height `[0x4d254a]`, and the display mode
and region hashes as `capture_regions=1` gives them.

The closing line is written at the normal end (atexit), not on the
`ExitProcess` refusal paths; a capture without it is "not closed" for
`dbsim_frames.py`. `es2sim --dump-frames LIST --dump-dir DIR` writes ES2F
dumps ([own_process.md](../reference-game/own_process.md#the-frame-compare-as-tests)).
Unit: five records on a synthetic 16×8 DIB read back with recomputed
hashes; the capture is `identical` to itself under `dbsim_frames.py compare`.

The windowed own process shows the frame of the GDI present through the
last uploaded colour table (`sim/src/platform_sdl.c`,
[own_process.md "The windowed platform layer"](../reference-game/own_process.md#the-windowed-platform-layer)).

### Frames against the original

The per-suite compare of `es2sim --headless --frames` with DBSIM's capture
is `//sim:frames_<suite>_test` ([own_process.md "The frame compare as tests"](../reference-game/own_process.md#the-frame-compare-as-tests),
all pass; the one known row is the original's spread of
`c3_10-detail-options`).

Site attribution against the original captures:
every site is an image VA, none unattributed, and `dbsim_frames.py compare`
is identical. Keyed by site, `c1_01-options-dialog` first differs at frame
500: the own run has two more `0x45720a` presents before the close at
`0x45490b`, showing the image of its last step, the options dialog's
step-count spread
([main_loop.md "The message trace of the replay pump"](main_loop.md#the-message-trace-of-the-replay-pump));
the compare does not show it because it ignores the site of other presents.

### Deviations

- **Headless GDI answers.** No window exists. The GDI/USER calls are counted
  and answered as a 32-bit desktop answers them: NUMCOLORS −1; no system
  entries; the client area is the game size unless the host sets one;
  palette use and the handle from the load record.
- **The palette upload's quad byte 3** is written as 0 (the original leaves
  stack residue there).
- **`0x465e28`'s local table:** for an entry whose colour has neither flag
  bit, and for byte 3 of every entry, the original passes the stack's bytes
  on: to `0x465a24` (hence DATA `0x4d372c..`) and to `SetDIBColorTable`. The
  own code uses 0.
  - In vitro the thunk zeroes 8 KiB below its stack pointer, so these bytes
    are 0 in the original too, except where `0x4820b7`/`0x486e10`'s frames
    lie (entries 232..255): no-flag entries are generated, and byte 3 is
    compared, below entry 200 only.
  - Live ([own_process.md](../reference-game/own_process.md), the residue
    table): every colour DBSIM installs has flag bit 0 (DPL records all
    `01`, [dpl.md](../formats/dpl.md); shield and intro entries
    `0x01000000 | rgb`; the cycler and the damage colours copy entries). So
    only byte 3 is residue (of earlier frames), and it reaches only
    `SetDIBColorTable`'s `rgbReserved`, which GDI ignores: `0x465a24` takes
    bytes 0..2 and sets its own flags, DBSIM imports no `GetDIBColorTable`,
    and both frame hashes take bytes 0..2. Only no-flag entries could
    change a colour.
- **DirectDraw** is modelled, not called ([Platform choices](#platform-choices)).
- **Refusals:** a DirectDraw method through a null object (`0x465628`,
  `0x465cc4`, `0x465e28`, `0x46548c`), `0x465e28` from the window procedure
  (`0x466228`, messages `0x30f`/`0x311`) headless, the overruns listed per
  function.

## Open questions

- The value of `NUMCOLORS` and of the system palette under the 256COLOR
  emulation (r = 10 is a hypothesis), and the uploaded table without it
  (r = 0); a capture's `palette_sha256` together with one dumped table
  answers it.
- `DIB_PAL_COLORS` (usage 1) in `CreateDIBSection` with a colour table
  later set by `SetDIBColorTable` as RGB quads: which GDI interpretation
  applies to the `StretchBlt` source on this host is not checked; it does
  not affect the indexed hashes.
- Whether the system DirectDraw accepts `CreateSurface` without
  `DDSD_CAPS`; the branch was never observed live with the system
  DirectDraw (`directdraw=windowed` is tested against DBSIM's call sequence
  from the bytes and in the visual check).
- Whether any code reads the DIB back for a simulation decision (pixel
  picking): not found in the presentation code; the renderer and HUD read
  it for their own drawing (strip save `0x488012`). A search over all
  readers of `[0x4d3028]`/`[0x4d2ff4]` is not done.
- Why the capture runs past the frame limit with every hook replaced
  ([The original's own frame spread](#the-originals-own-frame-spread)).
- `dbsim_frames.py compare` compares other-site presents per frame without
  the site; keying them by site would expose the options-dialog step-count
  spread ([Frames against the original](#frames-against-the-original)).
- The DirectDraw branches of `0x465628`, `0x465cc4`, `0x465e28` and the
  frame clear `0x46548c` are modelled from the listings and unit-tested
  (`//sim:present_test`), not in vitro; only the switch has an in-vitro
  suite. `0x465e28` from the window procedure is not modelled.
- The desktop full screen of es2sim_window is checked on the dummy video
  driver only; a visual check on a real display is open.

## The DirectDraw full-screen switch

Row `fullscreen_switch` (`0x4667c8`, `es2_sim_service_fullscreen_switch`,
[sim/dispatch/services/fullscreen.h](../../sim/dispatch/services/fullscreen.h)),
present.c. Its callees `0x46658c` (create), `0x4665c4` (mode and surfaces)
and `0x45f13c` (the panel call) have no other caller and are inlined.
Callers: WinMain `0x46543c` when `[0x4d255e]` is set at the
start (`-Z1`, or prefs byte 6, which the shell's Full Screen writes);
Alt+Enter in the command handler (`0x45fea9`, unless the tape word
`0x4d252c`, [input.md](input.md)); the cockpit system gadget's second item
(`0x4345da`); `0x4669b4` (`0x4669bd`, the keyboard handler's
Alt+Tab/Ctrl+Esc/Alt+Esc) and the help call `0x4669c4` (`0x4669d0`), both
only while DirectDraw is on. None is live in the suites (no map row; the
probes arm after WinMain).

### Contract

Listings `0x4667c8..0x4669b1`, `0x46658c..0x4665c2`, `0x4665c4..0x46672d`,
`0x45f13c..0x45f15f`. W = `[0x4d253e]`, H = `[0x4d2542]` are read at the entry.

1. `GetSystemMetrics(4)`, `(0x20)`, `(0x21)`; the results are dead.
2. **To full screen** (`[0x4d255e]` = 0):
   1. `[0x4d36fc]` ← 1 (the next WM_DISPLAYCHANGE is ignored).
   2. `[0x4d3700]` set: cleared (the start-up's rectangle in `0x4d36cc`
      stands). Otherwise `GetWindowRect(hwnd, 0x4d36cc)`.
   3. `[0x4d255e]` ← 1.
   4. `0x46658c`: `DirectDrawCreate(NULL, &[0x4d3710], NULL)`, then
      `SetCooperativeLevel(hwnd, 0x57)`. A nonzero answer fails.
   5. `0x4665c4(W, H, 8)`: `SetDisplayMode(W, H, 8)`; then the back buffer
      `0x4d3718`, the primary `0x4d3714` and the palette `0x4d371c` are
      released and zeroed, each when set. The descriptor is a zeroed
      `0x6c`-byte stack block (`_memset`): W < 640 (signed) flags `0x20`,
      one back buffer, caps `0x4200 | 0x200018`; otherwise flags 0, no
      back buffer, caps `0x4200`. `CreateSurface(desc, &[0x4d3714], NULL)`;
      on an error once more with caps bit `0x4000` cleared. W < 640:
      `GetAttachedSurface(&caps = 4, &[0x4d3718])`. 1, or 0 at the first
      error.
   6. **Failure path** (either returns 0): `0x4650d0` (the timer kill),
      `PostQuitMessage(0)`, return. `[0x4d255e]` stays 1, the objects
      written so far stay, EAX is PostQuitMessage's.
   7. `SetRect(rc, 0, 0, [0x4d253e], [0x4d2542])` (read again),
      `AdjustWindowRectEx(rc, 0xce0000, FALSE, 0)`,
      `SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, width, height, 0x108)`
      (`SWP_NOREDRAW | SWP_NOCOPYBITS`).
   8. `CreatePalette(4, 0x4d372c, &[0x4d371c], NULL)`; on 0
      `SetPalette(primary, palette)`.
   9. `ClipCursor({0, 0, W, H})`; `SetCursorPos(W / 2, H / 2)` (`sar` with
      the sign fix-up, toward zero); `ShowCursor(FALSE)` when the cursor
      sprite `[0x4d3724]` is set.
3. **Back to the window** (`[0x4d255e]` ≠ 0): `[0x4d36fc]` ← 1; the release
   `0x466730` (row `dd_release`); `[0x4d255e]` ← 0; `GetSystemMetrics(0)`,
   `(1)` (dead); `SetWindowPos(hwnd, HWND_NOTOPMOST, left, top,
   right − left, bottom − top, 0x10)` (`SWP_NOACTIVATE`) from `0x4d36cc`;
   `ShowCursor(TRUE)` when the sprite is set.
4. **Both** (not after the failure path): `0x45f13c` with the byte
   `[0x4d255e]` pushed (ignored). EAX = H = `[0x49b088]`; with H and
   G = `[H+0x1e9]` set, G's slot `+4` is called (cdecl, G); EAX is its
   answer.

EDX and ECX: the row leaves the entry's; no caller reads them.

### Platform choices

The original asks for exclusive 8-bit mode at the game size. This is not
available on current systems, so the own row reproduces every guest write
and answers the calls itself:

- **Modelled DirectDraw.** The objects are the platform record's
  `dd_object[0..3]` (headless the stand-ins `0xdd01..0xdd04`, released
  without a call). Every method answers DD_OK unless its bit is set in
  `dd_fail`; nothing headless sets a bit, so the failure path is reached
  only in vitro. Each call is counted in `dd_calls` and handed to `dd_note`.
  A method called through a null object faults in the original and is
  refused.
- **Display.** `SetWindowPos` to HWND_TOPMOST asks the platform for
  full screen, the way back asks for the window (`display_mode`).
  es2sim_window makes the window SDL desktop full screen and scales the
  640×480 frame into a letterbox (`SDL_SetRenderLogicalPresentation`); the
  way back restores SDL's windowed geometry. Topmost, the adjusted
  rectangle, `ClipCursor` and the metrics are counted, not applied. A
  refusal by the video driver is counted (`display_failures`) and never
  stops the process. Headless there is no window action.
- **Mouse.** In full screen the mouse events and `GetCursorPos` are mapped
  from the desktop through the letterbox into game coordinates,
  `SetCursorPos` the other way, and `ClientToScreen` leaves the point (the
  original's screen is the game's).
- **No WM_DISPLAYCHANGE.** No mode change happens, so `0x812` is never
  posted from it; `[0x4d36fc]` is still written.
- **Cursor.** `ShowCursor` is counted; the system cursor stays and the
  DirectDraw present does not draw the cursor sprite into a surface.
- **The DirectDraw paths after the switch** use the same model: the
  present (`IsLost`, `Lock`, `Unlock`, `Flip`; the frame shown is the DIB),
  the frame clear `0x46548c` (`0x4654a2..0x46554a`), the palette upload's
  `SetEntries` and `0x465e28`'s `CreatePalette`/`SetPalette`.

### Tests

- **In vitro, `fullscreen`: 3000 cases identical**
  (`//tools/reference-game/invitro:differential_fullscreen_test`): the four
  original functions as machine code against the row, DirectDraw a world of
  four recording-stub objects with a seeded failure mask; EAX, DATA with the
  heap and the stub log compared; every path taken (both directions, every
  DirectDraw call failing and answering, the failure path, below 640 and
  not, sprite, the panel's three cases). Caught mutation: dropping the
  retry's caps clear in present.c's `dd_mode` (case 8, stub log).
- **Unit:** `//sim:present_test` `fullscreen`, `//sim:platform_sdl_test`
  (the request on the dummy video driver, the cursor mapping, the way back).
- `//sim:es2sim_real_test` runs es2sim headless with `-Z1` to the same end
  as without it.

