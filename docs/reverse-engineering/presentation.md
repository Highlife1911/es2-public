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
(SHA-256 `8561aa47…c4ad`, image base `0x400000`) with the pinned
`llvm-objdump -d --no-show-raw-insn -M intel`; functions that the linear
listing decodes out of step (`0x45fba4`, `0x4649d8`, `0x464a14`) were
re-disassembled from their entry with `--start-address`. Import thunks are
named from the PE import table. DATA tables (the graphics-driver descriptor
`0x4a586c`, the strings `BMP_8`, `data\prefs.cfg`) were read from the file.
Live counts are the saturated entry probes of
`<es2-data>\logs\dbsim-map-20261010\map.json` (order
a60/c1s/c1c/c3_09/c4_08/c3_10). **Confirmed** = read from the instructions
or data (address given); **live** = a live log shows it (path given);
everything else is a **hypothesis**. "Listing" means the `llvm-objdump -d -M
intel` listing of a hash-checked scratch copy.

## Summary

1. **DBSIM never draws into a DirectDraw surface (confirmed).** Every
   renderer, HUD and text routine draws in software into one 8-bit
   top-down **DIB section** (bits `[0x4d3028]`, memory DC `[0x4d3034]`,
   created by `0x464d44`) that is larger than the screen: 1280 × 960 for a
   640 × 480 screen. A present copies one screen-sized window of it, at the
   *page origin* `S` = (`[0x4d3018]`, `[0x4d301c]`), into the destination
   rectangle `R` = `[0x4d2ff8..0x4d3004]`. There is exactly one present
   routine, `0x465628(&S, &R)` (reached through `0x464a14`), with two
   branches selected by `[0x4d255e]`: **GDI** (`StretchBlt` from the DIB DC
   to the window, scaled to the client rectangle) and **DirectDraw**
   (lock the primary or the back buffer, copy rows, draw the cursor sprite,
   unlock, flip below 640 pixels). DBSIM uses no `GetDC`/`ReleaseDC` on a
   surface, no `Blt`/`BltFast`, no clipper and no GDI text output.
2. **The oracle runs the GDI branch (live).** The perf report of
   `c1_01-success` (`<es2-data>\logs\speed-perf-20261007T140045Z\c1_01-success\perf.json`)
   was taken with the DirectDraw observer installed and counts **one**
   `directdraw` phase over 6,197 frames, which is the observer's own final
   drain (`dd_finish`): no DirectDraw object was ever created. The hang
   snapshot of [input_journal.md](../reference-game/input_journal.md)
   shows `0x465628` in `GetClientRect`/`InvalidateRect`/`BeginPaint`, the
   GDI branch. Every record of the 40-suite frame capture has mode 0
   (`<es2-data>\logs\dbsim-frames-20261014`, every manifest
   `modes` `{"0": n}`). Consequences: the `ddraw` observer sees nothing in
   the oracle, `-DeferFlip` and `-PresentationEvery` are no-ops there, and
   the original oracle path already *is* a windowed, scaled presenter (a
   resizable window; `StretchBlt` to the client size).
3. **Nothing DirectDraw or GDI returns reaches simulation state
   (confirmed for the data flow).** The values read back (return codes,
   `lpSurface`/`lPitch`, client rectangle, system palette, `NUMCOLORS`) feed
   only the present and palette routines themselves; DBSIM never reads
   surface memory and the present never writes the DIB. What *does* touch
   compared state around presentation is not the present but its
   neighbours, listed in the [keep-list](#keep-list-for-a-presenter): the
   clock read of the palette cycler `0x4306ac` in every presentation frame,
   the display-mode edges of [display_modes.md](display_modes.md), the
   window's message traffic (WM_PAINT and focus), and the toggle
   `0x4667c8`.
4. **Seam.** In the oracle configuration the seam is not the DirectDraw
   observer but DBSIM's GDI32/USER32 import slots used by `0x465628`
   (`StretchBlt`, `BeginPaint`, `EndPaint`, `InvalidateRect`,
   `GetClientRect`, `SelectPalette`, `RealizePalette`) and the palette
   sinks `SetDIBColorTable`/`AnimatePalette` (`0x465cc4`, `0x465e28`). The
   observer is the seam only for the DirectDraw branch. A GDI presenter
   (own scaler, `StretchDIBits` of a converted 32-bit image into the game's
   own window) needs no SDL3; the proxy's presenter is one
   ([below](#capture-own-gdi-presenter-windowed-directdraw)).
5. **Acceptance** is defined on the indexed frame: SHA-256 of the presented
   `w × h` bytes of the DIB plus SHA-256 of the 256-entry colour table at
   every present, keyed by the frame counter, captured without touching the
   tick log; the presenter's RGB conversion and scaling are tested
   separately on captured frames ([Acceptance](#acceptance)).

## The frame buffer

| Item | Address | Value (by video mode) | Evidence |
|---|---|---|---|
| video mode | `0x45e510(m)`: byte 4 of `data\prefs.cfg` (7 bytes read once, `0x45e535`/`0x45e54c`), or `-v<digit>` (`0x45e6cc`) | prefs byte 1 → low resolution, anything else → high | confirmed |
| DirectDraw flag `[0x4d255e]` | byte 6 of `data\prefs.cfg` (`0x45e561`); `-Z1` sets 1, `-Z<other>` 0 (`0x45e706`); the toggle `0x4667c8` flips it | 0 = GDI, ≠ 0 = DirectDraw | confirmed; oracle value 0 in every suite (live, [Summary](#summary) item 2) |
| screen W × H `[0x4d253e]`, `[0x4d2542]` | `0x45e5ac`, `0x45e612`, `0x45e679` | low 320 × 240, high 640 × 480 | confirmed |
| DIB W × H `[0x4d2546]`, `[0x4d254a]` (= DIB stride) | same | low 640 × 480, high 1280 × 960 | confirmed |
| `[0x4d253c]` coordinate shift, `[0x4d2537]` | same | low 0 / 0, high 1 / 3 | confirmed |
| present-mode word `[0x4d2566]` | 1 in every video mode; 0 only through a case of the option parser `0x45e758` (`0x45e84a`) | selects the presentation-frame branch of `0x45fba4` | confirmed |
| DIB section | `0x464d44` (from window creation `0x465158`): `CreateCompatibleDC(0)` → `[0x4d3034]`; BITMAPINFOHEADER at `0x4d304c`: size `0x28`, width = DIB W, **height = −DIB H (top-down)**, 1 plane, 8 bit, `BI_RGB`, image size W·H; colour table at `0x4d3074` = the system palette with red and blue swapped; `CreateDIBSection(memdc, bmi, 1, &[0x4d3028], 0, 0)` (usage 1 = `DIB_PAL_COLORS`), selected into the memory DC; failure → message box "Bad WinGBitmap" | — | confirmed |
| software screen object | `0x464d44` names it `BMP_8` (`0x4a0841`); page origins `+0x48..+0x67` zeroed because the name is not `VGA_8` | single buffer, no hardware pages | confirmed |
| graphics driver | `0x4893aa(3)` copies the descriptor `0x4a586c` (the only entry of the list `0x4a5840`) to `0x4a57ac` (`0x24` dwords). So `[0x4a57e8]` (page origin) = `0x48990e` → `0x4649d8`; `[0x4a57ec]`, `[0x4a57f0]`, `[0x4a57f4]` (page selects) = `0x4898fc`, `0x489902`, `0x489908`, which are empty (`enter`/`leave`/`ret`); driver init `0x48a1d4` | — | confirmed |
| R | `0x48a1d4`: (0, 0, `[0x4a03c8]` − 1, `[0x4a03cc]` − 1), where `0x464d44` stored the screen W, H there | (0, 0, 639, 479) at high resolution | confirmed |
| S | `0x48a1d4` sets (0, 0); `0x4649d8(page, point)` sets `[0x4d3018]`, `[0x4d301c]` and moves the renderer's pixel pointer `[[0x4d2ff0]+0xa]` and row base `[[0x4a03d0]]` to `[0x4d2ff4]` + x + y · DIB W | live 1000 in c3_09 only (jolts and panel slides, [display_modes.md](display_modes.md)), 0 elsewhere | confirmed; counts live |

So the "8-bit primary surface" an own presenter must show is the
W × H window of the DIB at S, with R = the whole screen in every present the
main loop makes. The page swaps of the presentation frame (`0x45fc3b..`,
`+0x88`/`+0x8c`) and the page selects of the display-mode code call the
empty driver slots and change nothing visible (confirmed).

## Who presents, and when

- **Main loop.** `0x45fba4` (presentation frame, after rendering
  `0x45fca8`, before the frame-counter increment at `0x45f4ad`,
  [main_loop.md](main_loop.md)). With `[0x4d2566]` ≠ 0 (the oracle):
  `0x481224([0x4d2568])` when byte `0x4d2537` = 1, `0x42a9c0(D)`
  (transition animation, may present several times per frame through
  `0x464a14` without reading the clock), **`0x4306ac()` palette cycling
  (reads the clock at `0x4306b6`)**, `0x43048c()` palette flush,
  `0x464a14()` present, `0x42a4c4(D)` transition start. The other branch
  (`[0x4d2566]` = 0) skips the present and swaps pages instead (not taken
  in the oracle). Step by step in
  [The frame sequence](#the-frame-sequence-0x45fba4). Live: `0x45fba4`,
  `0x4306ac`, `0x42a9c0`, `0x42a4c4` 1000×5/936; `0x465628` 1000×5/940.
- **Other callers of `0x464a14`** (29 `call 0x464a14` in the listing,
  [flyer.md](flyer.md#function-table)): the display-mode slides inside
  `0x42a9c0` (`0x42b135`…`0x42d040`, 21 calls), UI screens (`0x454906`,
  `0x454b60`, `0x457205`: modal loops such as `0x456000`), mission load
  (`0x4614a3`, `0x461e1c`), MainWndProc `WM_PAINT` (`0x4662a7`, GDI mode
  only) and the private message `0x812`. In modal loops the frame counter
  stands and the number of presents depends on message traffic (hypothesis
  from [timing.md](timing.md) item 2: pump counts are not exact; see
  [The original's own frame spread](#the-originals-own-frame-spread)).
- **Frame clear** `0x46548c` (flyer.md): WinMain after entering DirectDraw,
  `0x45f4da`, `0x4616a9`, `0x461a6f`, teardown `0x466730`. Live 0/1/0/1/1/1.

## GDI branch (the oracle path)

`0x465628(S, R)`, `[0x4d255e]` = 0, and only when `[0x4d3034]` ≠ 0
(confirmed, `0x46589a..0x4659da`):

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
   No row clamp and no cursor sprite in this branch (the Windows cursor of
   `SetCursor([0x4d3720])` in the pump `0x464c78` is used).
5. If the palette is set (`[0x4d302c]` re-read at `0x4659b2`):
   `SelectPalette(hdc, old, TRUE)`, `RealizePalette(hdc)` (`0x4659c8`).
   `EndPaint(hwnd, &ps)`.

For R = the screen this stretches the whole W × H source over the whole
client area. The window (`0x465158`) is created centred, client size
W × H, style `WS_VISIBLE | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX`
(`0x100e0000`, resizable, no caption bit), class `ES2`; `WM_GETMINMAXINFO`
limits the track size ([flyer.md](flyer.md#mainwndproc-0x466034)).
The pixel result of a scaled `StretchBlt` depends on GDI's stretch mode of
the paint DC (default, never set by DBSIM) and on the 256COLOR emulation
([media_compatibility.md](../reference-game/media_compatibility.md)); only
the 1:1 case is a plain copy (hypothesis for the exact GDI behaviour).

## DirectDraw branch

Never taken in the oracle ([Summary](#summary) item 2), but the bytes
define it completely. Every DirectDraw call of the image is in
`0x46548c..0x4668f4` and `0x465f48..0x465f72` (all references to
`0x4d3710`/`0x4d3714`/`0x4d3718`/`0x4d371c`); the only import is
`DirectDrawCreate` (thunk `0x496a54`).

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

**Creation** (`0x4667c8`, GDI → DirectDraw; also WinMain `0x46543c` when
`[0x4d255e]` is set at start): `[0x4d36fc]` ← 1 (WM_DISPLAYCHANGE is
ignored once), window rectangle saved to `0x4d36cc` unless `[0x4d3700]`,
`[0x4d255e]` ← 1, `0x46658c`, `0x4665c4(W, H, 8)`; on failure `0x4650d0`
and `PostQuitMessage(0)`. `0x4665c4` releases an existing back buffer,
primary and palette, then builds the descriptor: `dwSize` `0x6c`, caps
`DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY` (`0x4200`); W < 640:
`dwFlags` `0x20` (`DDSD_BACKBUFFERCOUNT`), one back buffer, caps
`| DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_MODEX` (`0x200018`); W ≥ 640:
`dwFlags` 0, no back buffer. Neither sets `DDSD_CAPS` (quirk; whether the
system DirectDraw accepts it was never observed). A failed `CreateSurface`
is retried once without `DDSCAPS_VIDEOMEMORY`. Then `SetWindowPos(hwnd,
HWND_TOPMOST, adjusted rectangle of W × H, SWP_NOREDRAW |
SWP_NOCOPYBITS)`, `CreatePalette(4, 0x4d372c)` + `SetPalette`,
`ClipCursor(0, 0, W, H)`, `SetCursorPos(W/2, H/2)`, `ShowCursor(FALSE)`
when a cursor sprite is set. Last, `0x45f13c(mode)` calls the cockpit
widget `[H+0x1e9]`'s virtual `+4`, which reaches the panel's full redraw
`0x443758` ([cockpit.md](cockpit.md), slot 1 of the panel widget).
**No clipper, no surface description query, no `GetCaps`, no mode
enumeration** (confirmed).

**Per present** (`0x465628`, `0x465647..0x465895`): `IsLost` → `Restore`;
`Lock(target)`; source = `[0x4d3028]` + S.x + S.y · DIB W, destination =
`lpSurface` + R.left + R.top · `lPitch`; `w = R.right − R.left + 1`
columns, `h = min(R.bottom − R.top + 1, DIB H − S.y)` rows; the copy is a
`memcpy` per row when R.left = R.top = 0, else a byte loop (same result);
the [cursor sprite](#cursor-sprite); `Unlock(target)`; `Flip` when
W < 640. In high resolution this is exactly the non-flipping primary
`Lock(NULL, desc, DDLOCK_WAIT, NULL)` / `Unlock(NULL)` pair that the
observer's `-PresentationEvery` retains; in low resolution it is the
`Flip(NULL, DDFLIP_WAIT)` that `-DeferFlip` defers.

**Teardown** (`0x466730`, DirectDraw → GDI through `0x4667c8`, and at
exit from WinMain `0x46547a`): `0x46548c` (clear) when DD exists; release
back buffer, primary, palette, DD; `ClipCursor` to the whole screen
(`GetSystemMetrics` − 1 at both ends; step by step in
[The help call and the DirectDraw release](#the-help-call-and-the-directdraw-release)).
The toggle back to GDI then restores the saved rectangle with
`HWND_NOTOPMOST`/`SWP_NOACTIVATE`, `ShowCursor(TRUE)` when a sprite is
set, `[0x4d255e]` ← 0, `0x45f13c`.

**Who toggles** (confirmed callers of `0x4667c8`): WinMain (`0x46543c`);
Alt+Enter in the command handler (`0x45fea9`, unless the tape word
`0x4d252c`, [input.md](input.md)); a cockpit widget (`0x4345da`);
`0x4669b4` (`0x4669bd`, the keyboard handler's Alt+Tab/Ctrl+Esc/Alt+Esc,
only while in DirectDraw) and `0x4669c4` (`0x4669d0`, help, leaves
DirectDraw first). None is live in the suites (no map row; the probes arm
after WinMain).

**What DBSIM reads back from DirectDraw:** the return codes listed in the
table, and `lpSurface`/`lPitch` of the lock descriptor. It never reads
surface memory, never calls `GetSurfaceDesc`, and nothing it reads is
stored outside the presentation globals `0x4d3710..0x4d371c`,
`0x4d255e` (confirmed). A lost surface costs one `Restore` per present; a
failed lock drops that present silently.

## Palette contract

**Sources (confirmed).** The game's current palette object is
`[0x49b020]` (setter `0x4303b0`); entries are 4-byte records at
`[P+8]` (bytes 0..2 = 6-bit red, green, blue, byte 3 flags). Changes reach
the screen only through the palette flush `0x43048c` (callers `0x429bc4`,
presentation frame `0x45fbda`/`0x45fc36`, mission load `0x461dec`/
`0x461e08`):

- `[0x49b020]` ≠ the last flushed `[0x49b024]` → `0x481fe0(P)`
  (renderer colour tables `0x48d99e`/`0x4820b7`, then the full install
  `0x465e28(P)`, [flyer.md](flyer.md)); live 52 in c3_09 only (the damage
  palette swap `0x42ea44`/`0x42ea70`/`0x42ea94`, [display_modes.md](display_modes.md));
- else, if the dirty byte `[0x49b030]` is set → **`0x465cc4(P)`**; live
  177/1000/738/865/601/436;
- then dirty byte and the dirty range `[0x49b028]`/`[0x49b02c]` ← 0. The
  range (kept by the entry writer `0x4303c4`) is never used by the upload:
  every upload sends 256 entries.

Writers of the dirty byte: `0x4303c4` (range copy into `[P+8]`, live
≈ the upload count; called by the cycler `0x4306ac`, which rotates up to
five colour ranges `0x4cfd64` on clock deadlines `0x4cfd78`), and the fade
step `0x430b34` (reads the clock itself at `0x430b3a`; dirty writes at
`0x430cc1..0x430cf3`; callers `0x45daef` in the mission-start sequence
`0x45d85c` and `0x45dd30`/`0x45dd53` in the unreferenced `0x45dc50`).

**`0x465cc4(P)` (confirmed, `0x465cc4..0x465e26`).**

1. `hdc = GetDC(0)`; `n = GetDeviceCaps(hdc, 24)`; `GetSystemPaletteEntries(hdc,
   0, 256, E)`; `r = n / 2` (signed, towards zero). Index 24 (`0x18`) is
   **`NUMCOLORS`**, not `NUMRESERVED` (`0x465ceb`; `0x465a24` uses the same
   constant at `0x465ae9`). On a palette device it is the number of static
   colours (20 → r = 10, hypothesis for the 256COLOR emulation); on a
   true-colour device −1 → r = 0.
2. `E[i]` for i < r and i ≥ 256 − r: the system entry with flags 0.
   For r ≤ i < 256 − r: `E[i]` = ((P.r << 2) & 255, (P.g << 2) & 255,
   (P.b << 2) & 255, flags 5 = `PC_RESERVED | PC_NOCOLLAPSE`). The source
   flag byte is ignored (unlike `0x465e28`). `ReleaseDC(0, hdc)`.
3. GDI mode: build RGBQUADs (blue, green, red from `E`; the fourth byte is
   never written, stack residue) and `SetDIBColorTable([0x4d3034], 0, 256,
   quads)`, then `AnimatePalette([0x4d302c], 0, 256, E)`.
   DirectDraw mode: `[0x4d371c]->SetEntries(0, 0, 256, E)`.

So the colours the player sees for indices 0..r−1 and 256−r..255 are the
system's static colours, not the game's (in both modes). An own presenter
must take the palette **as uploaded** (the table passed to
`SetDIBColorTable` resp. `SetEntries`), not recompute it from `[P+8]`.

**`0x465e28(P)`** (full install, flyer.md;
[The palette set](#the-palette-set-0x481fe0-with-0x465e28-0x465c40-0x465a24)):
stores P in `[0x4d3b2c]`, converts with the flag rule (bit 0 → ×4, bit 1 →
unscaled, neither → the frame entry is not written), flushes the system
palette to black (`0x465c40`), creates a new logical palette `[0x4d302c]`
(`0x465a24`, never deleting the old one), realizes it on the screen DC and
calls `SetDIBColorTable`; DirectDraw mode additionally
`CreatePalette(0x4c)` + `SetPalette` (the old palette object is not
released). MainWndProc's `WM_QUERYNEWPALETTE`/`WM_PALETTECHANGED`
re-realize in GDI mode only.

**Fades** are ordinary entry writes plus flushes; there is no gamma ramp,
no `WaitForVerticalBlank` and no hardware fade (confirmed: no such import
or slot is used).

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
leaving shows it. In GDI mode (the oracle) the Windows cursor is used and
the sprite is never drawn, so it is not part of any frame hash.

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
   frame (`0x4306b6`, confirmed).** Under the intra-frame clock every read
   after the first in a frame advances the virtual clock by 1 ms
   ([timing.md](timing.md)), so removing, adding or reordering it changes
   the clock seen by later readers. The presenter replaces only the sinks
   (`0x465cc4`'s API calls, `0x465628`'s blit), never `0x45fba4`'s call
   sequence. Measured with every hook original
   ([hud.md](hud.md#the-coupling-set-measured)): 72,288 reads after the
   load in the 40 suites, exactly once per tick in 38 of them and 1.009 per
   tick in the two options-dialog suites, plus one read in each suite's
   load window.
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
- In the oracle (GDI branch) no DirectDraw object exists, so the observer
  wraps nothing, `-DeferFlip` defers nothing and `-PresentationEvery`
  retains no lock (live: `"deferred":0,"drained":0` and one `directdraw`
  count in the perf reports of `speed-perf-20261007T140045Z`).
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
palette, not screen RGB: the RGB result of GDI depends on the desktop
(256COLOR emulation, DWM, stretch mode) and is not reproducible across
hosts, while the indexed frame is what DBSIM produces and the presenter
consumes. The record is the [capture record](#capture-record) (a separate
file, never the tick log); its key is the frame counter `[0x4d257b]` at the
present (the main-loop present of frame N happens before the increment to
N + 1, [main_loop.md](main_loop.md)) plus the site (the return address of
`0x464a14`) and an ordinal per (`frame`, `site`).

Capture point: the hooked `StretchBlt` slot (GDI; arguments give the
source DC and rectangle, the caller's EBP gives `site`) or the windowed
`Unlock`/`Flip` (DirectDraw). The hash runs on the main thread inside the
present: it costs wall time only, reads no clock through DBSIM, sends no
message and writes no game memory, so the tick log is unchanged (live:
[Live evidence](#live-evidence), pass A).

**Passes.**

1. **A (determinism and baseline).** Capture on, all hooks original, twice
   per suite: tick logs identical to the goldens and the two capture files
   identical for all records with a main-loop or slide `site`
   (determinism of the indexed frames under virtual time). Records from
   modal loops are compared as the set of distinct (`index_sha256`,
   `palette_sha256`) per `frame`, because their count follows message
   traffic.
2. **B (replaced mode).** The same with the replaced hook selection
   (`hooks.md`): goldens identical and capture identical to A (the
   replaced simulation must also render identical frames; a difference
   localises a hook that changes presentation-visible state that the tick
   log does not compare).
3. **C (presenter).** Presenter on, capture on: the same comparison against
   A, plus the goldens. This proves the presenter does not perturb the
   game and is fed the same frames.
4. **Offline.** The presenter's conversion and scaler applied to frames
   dumped in A (ignored scratch only, never committed) match a reference
   implementation (palette lookup; at 1:1 the RGB must equal the table
   lookup exactly).

The compare is `presenter_capture.py compare` (two reports) or
`dbsim_frames.py compare` ([Comparing against it](#comparing-against-it));
the offline pass is `presenter_capture.py check-dumps <dump dir> --offline
presenter_offline.exe --client WxH ...`.

**Fixed frames for the spot check** (dumps, besides the full sequences):
acceptance-60s frames 100, 750, 1,499 (a60: no page-origin moves, 177
uploads); c3_09-loadout-b frames 278, 279, 280, 290 (Esc at 279: the 0→1
transition starts in presentation frame 279 and finishes in 280,
[display_modes.md](display_modes.md)), 308, 309, 310 (Esc back at 309),
and the frames of the first jolt (the first frame with `S` ≠ the previous
`S` while `0x42d4a8` runs; read from the capture file), plus one frame on
each side of each of the 52 palette installs; c4_08 and c3_10: the frame
of the death switch to the outside view (mode 4, border `0x42da08`) and
the frame after it.

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
`CreateWindowExA` (before the window and the DIB exist) from the first of: the
INI named by `ES2_PRESENTATION_CONFIG`, the measurement INI
`ES2_ORACLE_SPEED_CONFIG`, `es2-oracle.ini` beside the proxy (a player's
setting). `directdraw` is read from the same file when the proxy resolves its
first export.

| Key | Values | Effect |
|---|---|---|
| `capture` | 0 (default), 1 | per-present records ([Capture record](#capture-record)); needs `capture_report` |
| `capture_report` | a path, or `beside_tick_log` | the capture file, created new (`CREATE_NEW`; an existing or unusable file refuses the capture only: event `presentation_capture_refused`; with `presenter=gdi` the presenter is installed anyway, without it no slot changes). A path may contain `{pid}` ([Capture in an interactive session](#capture-in-an-interactive-session)); `beside_tick_log` = `<[trace] tick_log of es2-oracle.ini>.capture.ndjson` (not `*.jsonl`: `check.ps1` and `tools/evidence_budget.py` treat a `*.jsonl` in a run directory as a raw tick log) |
| `capture_dump_frames` | frame numbers, comma or space separated (at most 64) | an ES2F dump of every hashed record of those frames |
| `capture_dump_range` | `<from>-<to>[/<step>]` | a regular series of frames dumped besides `capture_dump_frames` (at most 4,096 frames and 8,192 dump files per process); a malformed or too long range is refused with the event `presentation_dump_range_refused` and switches only the range off; the start line gets `"dump_range":[from,to,step]` only when a range is set |
| `capture_dump_dir` | a directory | dumps go to `<dir>\<capture file name>.frames\`; without it, `<capture file>.frames\`. Dumps are decoded game frames: keep them in ignored scratch, never in archived evidence |
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

- **Report name.** `capture_report` may contain the token `{pid}`; every
  occurrence is replaced by the decimal process id of the DBSIM process
  (`GetCurrentProcessId()`) before the file is created. Example for a
  player's `es2-oracle.ini`:

  ```ini
  [presentation]
  presenter=gdi
  capture=1
  capture_report=D:\es2-captures\mission.{pid}.capture.ndjson
  ```

  Every process of the session then writes `mission.<pid>.capture.ndjson`;
  with `capture_dump_frames` its dumps go to `<report name>.frames` (or
  `<capture_dump_dir>\<report name>.frames`), so dumps are per process too.
  The file stays `CREATE_NEW`: a stale file with the same name (pid reuse) is
  never overwritten. `beside_tick_log` takes no token (one process per run
  directory). A name that does not fit into `MAX_PATH` after the expansion
  refuses the capture like an unusable path.
- **Start line.** It ends with `"pid":<n>`, the same number as in the file
  name (schema stays 1).
- **Presenter independent of the capture.** When the report cannot be
  created (existing file, missing directory, unusable name) the event
  `presentation_capture_refused` is logged and only the capture is switched
  off. With `presenter=gdi` the slots are replaced and the presenter runs
  (event `presentation_gdi`, no capture records, no dumps); with
  `presenter=original` nothing is installed and no slot changes.
- **Reader.** `presenter_capture.py sessions "<capture_report value>"`
  lists the per-process reports of a session (`{pid}` matches any text;
  shell globs work too): file, pid, presenter, record count, first and last
  frame, oldest first; exit 0 with at least one report, 1 with none.
  `summary` and `compare` take a single report.

The oracle runners set `capture_report=beside_tick_log`; each run has its own
run directory and exactly one DBSIM process, so the file never exists and the
refusal path is not reached there. None of the readers of oracle captures
(`presenter_capture.py summary|compare`, `check.ps1`, `evidence_budget.py`)
depends on the start line's last field or on a pid in a file name.

### Seam and installation

Confirmed from the bytes:

- DBSIM's IAT slots `StretchBlt` `0x6cc4c4` (thunk `0x496a2a`) and
  `SetDIBColorTable` `0x6cc4c8` (thunk `0x496a30`), names from the PE import
  table. In the reference code section the only references are: `call
  0x496a2a` at `0x4659ad` (present) and `0x4655e3` (clear), `call 0x496a30`
  at `0x465de5` and `0x465f00`, one `jmp [slot]` each; 29 `call 0x464a14`
  (fixture mode `reference` on ES2_DATA_ROOT's DBSIM.EXE).
- Checked before anything is chained (DllMain, memory reads only): both thunks,
  the `CreateWindowExA` thunk, and the code bytes the frame layout depends on:
  `0x464a14` (`push 0x4d2ff8; push 0x4d3018; call 0x465628; add esp, 8; ret`,
  no frame of its own), the prologue of `0x465628` (`push ebp; mov ebp, esp;
  add esp, -0x100`), `lea ecx, [ebp-0x100]` at `0x4658a7` (the RECT of
  `GetClientRect`), the four GDI calls above, and the DirectDraw calls
  `call [ecx+0x80]` `0x465870`, `call [edx+0x2c]` `0x465892` (present
  Unlock/Flip) and `call [edx+0x80]` `0x465524`, `call [eax+0x2c]`
  `0x465547` (clear). One changed byte refuses (fixture `mismatch` flips the
  last byte of each entry and of each thunk).
- Present detection: the return address of the `StretchBlt` call
  (`0x4659b2` present, `0x4655e8` clear, other callers counted as
  `other_blits`). EBP on entry is the frame pointer of `0x465628` (the thunk
  is a `jmp`): `site` = `[ebp+0x10]` (the return address of `0x464a14`,
  [Present sites from own code in the proxy capture](#present-sites-from-own-code-in-the-proxy-capture)),
  client RECT = `[ebp-0x100..-0xf4]`. Calls from another thread than the one
  that created the window are forwarded and counted (`foreign_thread`).

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

What the capture does inside the present: reads game memory, hashes, appends
to an own buffer, writes the own file; no clock through DBSIM (QPC from
kernel32 directly), no message, no game memory written (the fixture compares
the synthetic globals around every present). It leaves different stack
residue below DBSIM's ESP than GDI's `StretchBlt` would; pass A shows the
tick log unchanged.

### Present sites from own code in the proxy capture

With `mission_root=replaced` the core's `0x461608`
(`core/src/mission_root.c`, `mr_call(&s, 0x464a14u, 0x461e21u, ...)`) calls
the present through the adapter's stack bridge (`ai_behaviour_stack_call`, or
`logic_frame_machine_call` without the shim). The bridge's `call
*_ai_behaviour_guest_target` leaves its own return address in the site slot
`[ebp+0x10]`; the original's `0x461e21` is written there only after the call
returns. In `ddraw.dll` (image base `0x10000000`) the instruction after that
call is at `0x10023387`, recorded live as `0x7xxx3387` (64K-aligned load, low
16 bits equal). Own callers of `0x464a14`: only `mission_root.c` (the call at
`0x461e1c`); no own code calls `0x465628` directly.

**The rule** (`presenter.h` `pr_present_site`, the same order as es2sim's
[`call_site()`](#present-sites-from-own-code)):

1. The raw return address lies in CODE (`[base+0x1000, base+0x97000)`, the `.text` section, checked
   against the PE header by the fixture's `reference` mode): its image VA.
2. An announcement is current whose target is `0x464a14` and whose return address is a CODE address
   right after an `e8` call of `0x464a14` (the bytes are checked in the loaded image): that VA.
3. The announcement's caller is a row of `pr_own_present_callers` (`0x461608 -> 0x461e21`) and the row's
   site is right after an `e8` call of `0x464a14`: the row's site.
4. Otherwise the raw address, counted in `unattributed_sites`. It stays an 8-digit DLL address, which
   `dbsim_frames.py verify` and `compare` refuse ("bad site"), so it is never silently a site.

Rules 2 and 3 are counted in `announced_sites`; both counters are in the
stats line and in the manifest's `last_stats`.

The announcement is `proxy/present_site.h`: `es2_present_site_begin(target,
caller, ret)` / `es2_present_site_end(previous)`. These nest, are main thread
only, and are always compiled; the in-vitro harness compiles them too but
never reads them. The hook body of `0x461608` (`mission_root_hooks.h`, both
`mission_root_stack_call` and `mission_root_plain_call`) wraps each bridge
call with the callee, the entry `0x461608` and the core's absolute return
address. The announcement covers every guest call of `0x461608`; the capture
uses it only when the callee is the present. A new own caller of the present
(another replaced function that calls `0x464a14`) needs an announcement in
its adapter, a `pr_own_present_callers` row, or both.

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
2. Conversion and scaling (`pr_render`): output pixel (x, y) inside the image
   shows source ((x - ox)*w / width, (y - oy)*h / height) through the table
   (0x00RRGGBB, i.e. blue, green, red, 0 in memory), everything else black.
   At 1:1 this is exactly the table lookup of the source window.
3. One `StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, cw, ch, pixels, 32-bit
   top-down BI_RGB, DIB_RGB_COLORS, SRCCOPY)` into the `BeginPaint` DC DBSIM
   passed; 0 or `GDI_ERROR`: the original `StretchBlt` runs instead
   (`draw_failures`).
4. Returns TRUE (DBSIM ignores the result), LastError as on entry.

Kept: `GetClientRect`, `InvalidateRect`, `BeginPaint`, `EndPaint`,
`SelectPalette`/`RealizePalette` (the own calls of DBSIM around the blit,
keep-list item 7), every `SetDIBColorTable` and `AnimatePalette` (forwarded:
the DIB and the logical palette stay as in the original). The table used is
the one DBSIM uploads (`0x465cc4` with the static system colours at both ends,
or `0x465e28`), not a recomputation from the game palette. Buffers are on the
process heap (never the allocator of DBSIM).

### DirectDraw branch in a window (`directdraw=windowed`)

At the seam of the observer (the `DirectDrawCreate` export of the proxy,
`initialize_forwarding`), `ddw_create` hands out own v1 objects; when the
observer is also on (`-Perf`, `-DeferFlip`, `-PresentationEvery`) it wraps
them like the system objects. Every method DBSIM calls succeeds (the
return-code contract of the [table](#directdraw-branch)):

- `SetCooperativeLevel(hwnd, 0x57)`: records the window, no exclusive mode;
  `SetDisplayMode(W, H, 8)`: records the size, the desktop mode stays (other
  depths: `DDERR_INVALIDMODE`).
- `CreateSurface` with the descriptor of DBSIM (caps without `DDSD_CAPS`): a
  primary of W x H, 8 bit, own memory, pitch W rounded up to 4; with
  `DDSCAPS_FLIP` and a back-buffer count one attached back buffer
  (`GetAttachedSurface(DDSCAPS_BACKBUFFER)`). `IsLost`, `Restore` succeed;
  `Lock` returns the memory (a second Lock: `DDERR_SURFACEBUSY`).
- `CreatePalette(DDPCAPS_8BIT, entries)`, `SetEntries`, `SetPalette`: the
  palette set on the primary is the colour table.
- Present: the Unlock of a non-flipping primary (W >= 640), or `Flip` (W <
  640: the back buffer); converted and scaled as above into the client area
  of the window through `GetDC`/`StretchDIBits`/`ReleaseDC` (no message).
  `Flip` does not swap the buffers (DBSIM rewrites every row before each
  Flip and never reads a surface).
- Every other method: `DDERR_UNSUPPORTED`, with the right stdcall argument
  count. `QueryInterface`: `E_NOINTERFACE`. No system DirectDraw object.

Limits (code of DBSIM, unchanged): the toggle `0x4667c8` still moves the
window `HWND_TOPMOST` to the `AdjustWindowRect` of (0, 0, W, H), clips the
cursor to the screen rectangle (0, 0, W, H) and hides the Windows cursor when
a sprite is set; the sprite is drawn at `GetCursorPos` screen coordinates, so
in a window it is offset by the client origin of the window, and the clip
holds the pointer in the top-left W x H of the screen. A fix needs the input
path, not the presenter. Capture of DirectDraw presents needs the observer
off (with it, the return address of Unlock/Flip is in the observer).

### Tests

`//tools/reference-game/proxy:presenter_<mode>_test` (fixture
`presenter_fixture.c`, synthetic image with generated routines in DBSIM's
call shapes, the real GDI32 for `gdi_real`, the reference DBSIM.EXE bytes
for `reference`):

- `capture` / `capture_gdi`: records of the main loop, a slide, a UI
  screen, another source DC, an outside rectangle and a foreign thread;
  flush rule, stats, ES2F dumps; exact lines with SHA-256 from CryptoAPI;
  LastError of the original preserved; no synthetic game global written.
- `presenter`: six client sizes against the definition, 1:1 = table lookup,
  every forwarding reason. `gdi_real`: the real GDI32 `StretchDIBits` into a
  32-bit DIB section (1:1, 2x with borders, 3x).
- `beside` (`beside_tick_log`, `capture_dump_dir`), `exists`, `unknown`,
  `noreport`, `off` (precedence of `ES2_PRESENTATION_CONFIG`), `mismatch`,
  `journal_only` (chain without the window shim).
- `session` (two real processes with the same INI and
  `capture_report=...session.{pid}.ndjson`, `presenter=gdi`: own report and
  pid each, both presenting, exactly two files, the token never a file
  name), `exists_gdi` and `unusable_gdi` (report refused, presenter draws,
  the existing file untouched, no dumps). `exists` (capture only, no
  presenter): no slot changes.
- `range`, `range_refused` (`capture_dump_range`: parser spellings, the
  selection with its caps, the dump files, the start line).
- `own_site` (the site rule: `call 0x464a14` at `0x461e1c`, another call at
  `0x461e30`, a stub outside the image that calls the present; ten records,
  `announced_sites` 3 and `unattributed_sites` 5: an announced return
  address and return address 0 with the table give `0x461e21`, CODE return
  addresses stay, four unattributed cases keep the raw 8-digit site, nesting
  restores the outer announcement). The announcing hook body of `0x461608`
  is covered in vitro by
  `//tools/reference-game/invitro:differential_mission_root_test`.
- `sha` (FIPS vectors, 300 random lengths and chunkings against CryptoAPI,
  `pr_hash_window` with strides, `pr_rgb_hash`), `scaler` (20,000 random
  layouts: fit, centring, maximal integer scale, aspect within a pixel; 400
  random renders against the definition), `cost`.
- `reference` (with `ES2_DATA_ROOT`): the seam bytes on the real
  `DBSIM.EXE`; every `pr_own_present_callers` row follows an `e8` call of
  `0x464a14` (29 calls listed), the entry's prologue is `55 8b ec`, the CODE
  bounds equal `.text`.
- `ddraw` (the creation, present and clear sequences of DBSIM in both
  resolutions, palette changes, teardown reference counts, capture
  records), `ddraw_off`.

`//tools/reference-game/scripts:presenter_capture_unit_test` (comparison
rules, summary, layout, `SessionsTest`, the offline pass through
`presenter_offline` on synthetic dumps for scales 0, 1, 2, 4, 10).

Mutations, each caught: `site` from `[ebp+0xc]` (capture), offset rounding
up (scaler), blue from the reserved byte in the palette hash (capture),
Unlock of a flipping primary presenting (ddraw), `{pid}` expansion without
replacement (`session`), the refusal that returns instead of continuing
(`exists_gdi`, `unusable_gdi`), the range's step test (`range`), the site
rule 2 returning the raw address (`own_site`).

**Cost** (fixture `cost`, 640 x 480 window of a 1280 x 960 DIB into
1920 x 1080, mean of 30 on the 16-core host): capture 2.5 ms per present
(SHA-256 of 307,200 bytes), presenter 4.6 ms (conversion, 2x scaling, a
recording stand-in for `StretchDIBits`); the test bounds them at 15 ms and
40 ms. Wall time only; virtual time does not see it.

### Live evidence

**Passes A, B, C and the offline pass**
(`<es2-data>\logs\p-01-capture-20261012`; four
clones, one prepared proxy, the hidden replay window; kept: the A1 capture
files as the baseline). Suites acceptance-60s, c1_01-collision,
c3_09-loadout-b, c4_08-razor-plasma; INI `capture=1`,
`capture_report=beside_tick_log`, `capture_dump_frames=100 278 279 280 290
308 309 310 750 1499`.

- **A1, A2** (capture on, every hook original, `-Perf`): the four suites
  are identical to their goldens both times. `presenter_capture.py compare`
  of A1 against A2 gives exit 0 for every suite, through frame 1,828
  (acceptance-60s), 2,437 (c1_01-collision), 8,886 (c3_09-loadout-b) and
  2,094 (c4_08-razor-plasma). In every record `"mode"` is 0, every
  main-loop frame has exactly one record (`main_frames_not_one_count` 0),
  and perf `directdraw` is 1 in every run. In c3_09-loadout-b the page
  origin moves in 1,711 of 9,764 records, with 381 palette changes (233
  distinct tables). The slide sites `0x42b294`, `0x42b33e`, `0x42b720`,
  `0x42b81c`, `0x42b87f`, `0x42b917` and the dialog sites `0x45490b` and
  `0x454b65` present besides the main loop `0x45fbe4`. **The capture
  leaves the tick log unchanged.**
- **B** (every hook replaced; the RTL config of `check-h67`: 44 imports
  replaced, the heap included): the goldens are identical, and `compare`
  against A1 gives exit 0 in all four suites. The replaced simulation draws
  the same indexed frames and palettes, not only the same tick fields.
- **C** (A's configuration plus `presenter=gdi`): the goldens are
  identical, and `compare` against A1 gives exit 0 in all four suites.
  Every present goes through the own presenter (`presented` gdi 1,758 /
  9,764 …, `forwarded` 0), also with the hidden window.
- **Offline** (`check-dumps` with `presenter_offline.exe`, five client
  sizes: 640×480, 1920×1080, 1366×768, 800×600 and 2560×1440): 137 dumps
  and 685 checks, 0 failures.

**Sites from own code** (`<es2-data>\logs\dbsim-frames-20261022-pr03b`,
789 hooks replaced, five clones, hidden window): 40 of 40
suites identical; `dbsim_frames.py verify` accepts all 40 tables; every site
is a CODE address (19 distinct, among them `0x45fbe4`, `0x4614a8` and
`0x461e21` in every suite); frame 0 has the presents `0x4614a8`,
`0x461e21`, `0x45fbe4` in this order in all 40 suites; every manifest's
`last_stats` has `unattributed_sites` 0 and `announced_sites` 1. Without
the announcement each table had one site outside CODE at frame 0's second
present (`dbsim-frames-20261021-pr03`).

### Visual check

On the play clone `rebuild-r111` (never `native-r111`): the proxy (SHA-256 `a1b151de…f2d3`, validated by `validate-media.ps1`, 8 of 8
decodes, deployed with `configure-media.ps1 -Apply`), every hook original,
no RTL config, `es2-oracle.ini` beside the proxy with `[presentation]`, no
capture, started with `start-native.ps1`. Evidence:
`<es2-data>\logs\p-01-visual\proxy-events.jsonl` (the proxy's events
per process).

| `[presentation]` | 256COLOR layer | Missions | Proxy log | Result |
|---|---|---|---|---|
| `presenter=gdi` | on | 2 | `presentation_gdi` in both DBSIM processes | accepted: cockpit, map, damage palette swap, fades, flicker, resizing (larger: integer steps with black borders; smaller than 640 x 480: a fitted image) |
| `presenter=gdi` | off (`-UseNativeColors`, for ES, VSHELL and DBSIM) | 1 | `presentation_gdi` | accepted (the mission judged) |
| `presenter=gdi`, `directdraw=windowed` | off | 1 | `presentation_gdi`, `presentation_directdraw_windowed` | accepted: Alt+Enter in a mission keeps the game in its window and running; the cursor sprite is offset by the window frame (the limit above) |

`presentation_directdraw_windowed` is logged when the proxy sets up its
exports, not at a DirectDraw call; that the branch ran is the visual
observation. Not done: a side-by-side comparison with `presenter=original`;
the picture was judged by eye, without a capture.

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

Evidence: `<es2-data>\logs\dbsim-frames-20261014` (40 suites, every
status `ok`, every record mode 0); `dbsim-frames-20261021`, `-a1_retry`,
`-a2`, `-replaced` ([The original's own frame spread](#the-originals-own-frame-spread));
`dbsim-frames-20261022-pr03b`. `dbsim_frames.py build` over the four
`p-01-capture-20261012\A1` captures reproduces the
[live evidence](#live-evidence) figures.

**Does it change anything?** With capture on the proxy replaces two GDI import
slots of DBSIM in memory (`StretchBlt`, `SetDIBColorTable`); it reads game memory,
hashes, writes its own file, reads no clock through DBSIM and writes no game memory
(fixture check on every present). It does not touch virtual time or the tick log
(pass A), and the runner requires every suite to end identical to its golden (else
the manifest status `run-different`). It costs wall time (about 2.5 ms per present,
fixture `cost`).

### The runner

```powershell
& tools\reference-game\setup\capture-dbsim-frames.ps1 `
    -InstallName timing-r111,timing-r111-p1,timing-r111-p2,timing-r111-p4,timing-r111-p5 -HiddenWindow
```

It writes the INI `[presentation] capture=1 / capture_report=beside_tick_log` (plus
the dump keys with `-Dump`), sets `ES2_PRESENTATION_CONFIG` for the duration of the
call, runs `run-oracle-parallel.ps1` (check mode, no `-Hooks` unless given, the suites
named or all of the registry; `-PreparedProxy`, `-HiddenWindow`, `-GoldenRoot`,
`-SuitesFile` handed on) into `<DataRoot>\logs\dbsim-frames-work-<stamp>`, and then,
per suite, finds `<suite>.jsonl.capture.ndjson` in the suite's `run` directory and calls
`dbsim_frames.py build`. The environment is given back as the caller had it (the same
`Get-RunnerEnvironment` guard as the other runners). Lists may be given as `a,b` (the
form `pwsh -File` hands over).

`-WindowedDirectDraw` (needs a `-Label`) captures DBSIM's DirectDraw branch: it
adds `directdraw=windowed` to the INI and hands the game `-Z1`
([own_process.md "The DirectDraw branch in a capture run"](../reference-game/own_process.md#the-directdraw-branch-in-a-capture-run)).

Dumps (`-Dump` with `-DumpFrames 100,278` and/or `-DumpRange 100-400/4`): ES2F files
into `<ScratchRoot>\dumps\<capture name>.frames`, default
`<repository>\build\dbsim-frames-<date>[-<label>]` with a `.gitignore` of `*`.
They are decoded game frames: never in the evidence directory, never in either
repository. `-Dump` without a selection is refused, as is a selection without
`-Dump`; the manifest names the directory and counts the files.

**Size and time.** c3_09-loadout-b (9,764 records, the longest suite) gives a 420 KB
table (43 bytes per record); the 40 goldens have about 80,600 ticks
(`launch.ticks_seen`), so about 90,000 records and 4 MB of tables in all, every file
far below the 5 MiB of `evidence_budget.py`. Run time is that of the regression
(about 200 s on five clones, [oracle_regression.md](../reference-game/oracle_regression.md);
the longest suite bounds it) plus the prepared-proxy build if `-PreparedProxy` is not
given, and about 25 s of hashing in the longest suite.

### The frames table and manifest

| Field | Meaning |
|---|---|
| `index_sha256` | SHA-256 of the `w x h` bytes of the DIB at `[0x4d3028]` + the page origin `S`, row by row with the DIB width as stride: the indices as DBSIM's renderer and HUD wrote them, read inside the present (the `StretchBlt` call `0x4659ad` of `0x465628`), before the presenter or GDI converts anything. `-` when the source was not the DIB window (record `source` `other` or `dib_outside`) |
| `palette_sha256` | SHA-256 of the 256 red, green, blue triples of the last table DBSIM passed to `SetDIBColorTable` for the DIB (reserved byte left out); `-` before the first upload |
| `frame` | `[0x4d257b]` at the present (the number of the presentation frame; the main-loop present of frame N comes before the increment to N + 1) |
| `site`, `ordinal` | the caller of `0x464a14` (`0x45fbe4` main loop, the display-mode slide sites, `0x45490b` and `0x454b65` dialogs, ...) and the count of earlier records of the same frame and site |
| `mode`, `S` | `[0x4d255e]` (0 = GDI) and the page origin |

The table keeps `frame, site, ordinal, mode, S, source, index_sha256, palette_sha256`
per record (`frames.tsv.gz`: tab-separated, header line `#frame...`, `-` for null,
deterministic gzip with no name and no time; a capture with region hashes adds six
columns and a second header line naming the region table). The capture report's other
fields (client rectangle, source and destination rectangles, upload count,
`presented`) stay in the report, which the run keeps only in the runner's work
directory.

Result, in `<EvidenceRoot>\dbsim-frames-<date>\` (`EvidenceRoot` defaults to
`<DataRoot>\logs`, `<date>` to today in UTC, `-Date yyyyMMdd`):

| File | Content |
|---|---|
| `<suite>\manifest.json` | schema 1, kind `es2-dbsim-frames`: `suite`, `date`, `status`; `run` (result against the golden, hooks, label, clone, commit, dirty tree, proxy SHA-256, hidden window, game arguments, ticks and `frame_limit` of the registry, golden root, UTC start and end, the exit-code record of the own DBSIM when the runner has one, the dump summary); `capture` (report name, size and SHA-256, start line, `closed`, torn lines, last counters); `frames` (table name, size, SHA-256 of the gzip and `table_sha256` of the table, record count, first and last frame, main-loop frame count and the frames without exactly one main-loop record, records per site and mode, page-origin moves, palette changes, distinct tables and frames, records without a hash) |
| `<suite>\frames.tsv.gz` | the table above |
| `session-<stamp>.json` | the invocation: clones, hooks, dump selection, status per suite |

Nothing else enters the evidence directory: no capture report, no tick log, no
dump. A suite already present in the directory is never replaced (the run is refused
before anything starts); a later invocation may add suites to the same date.
`-Label <name>` puts a run into `dbsim-frames-<date>-<name>`; a run with `-Hooks`
needs one, so the baseline directory always holds the run with every hook original.

`manifest.status`: `ok`; `run-different` (the suite did not end identical to its golden:
the table is written but is not a reference); `no-capture` (no report found: the proxy
refused it, `presentation_capture_refused`, or the suite never reached a window);
`unusable-capture` (no schema-1 start line); `no-records`. The script exits 1 when any
suite is not `ok` and keeps the work directory then (`-KeepWork` keeps it always).
`capture.closed` is true when the proxy ended orderly (its last line is the final counter
line and counts every record). **A runner that stops DBSIM leaves `closed` false and loses
the records of its current frame**: the suites with a `frame_limit` are stopped at the end
of their window (the baseline of `p-01-capture-20261012` shows it for acceptance-60s and
c1_01-collision); this is not a failure. The capture runs to the end of the replay, so it
can contain frames beyond `run.frame_limit`, the window the tick-log golden covers. A
suite that started a second DBSIM process would have that process's report refused under
`beside_tick_log`; the manifest names `capture_candidates` and the start line's `pid`.

### Comparing against it

1. Run the candidate with the same capture switch: the same INI (`capture=1`,
   `capture_report=...`) on the suite's replay, for the own `dbsim.exe` through the same
   proxy seam or `es2sim --frames` ([Headless frame capture](#headless-frame-capture)),
   or the oracle with the hooks replaced (`-Hooks ... -Label`).
2. `python -B tools\reference-game\scripts\dbsim_frames.py compare <...>\dbsim-frames-<date>\<suite>
   <candidate>` with the candidate a manifest directory, a `frames.tsv.gz` or a capture report.
   The rules are those of `presenter_capture.py compare`: the records of the main loop and of
   the display-mode slides must agree one by one in `frame, site, ordinal, mode, S, source,
   index_sha256, palette_sha256`; the records of every other site (modal screens, loading,
   `WM_PAINT`) are compared per frame as the set of distinct (index, palette) pairs, without
   the site, because their count follows message traffic; only frames up to the last frame
   both contain are compared (a stopped process loses its current frame, so `table_sha256`
   alone is not a determinism check). Exit 0 identical, 1 different (the first sequence
   difference and the first differing frame set, with both sides), 2 unusable.
3. To look at a difference, dump the frames around it from the reference run (`-Dump
   -DumpFrames <n>` or `-DumpRange <n-5>-<n+5>` for that suite) and from the candidate
   the same way; the ES2F files are read with `presenter_capture.read_dump`, and
   `frame_dumps.py compare` shows where two dumps differ.

`dbsim_frames.py verify <suite dir>` checks a stored manifest against its table.

### Tests

`//tools/reference-game/proxy:presenter_range_test` and `presenter_range_refused_test`;
`//tools/reference-game/scripts:dbsim_frames_unit_test` (build, deterministic gzip, the `closed` flag,
torn lines, missing and unusable reports, verify, compare forms and rules, CLI,
`announced_sites`/`unattributed_sites` in `last_stats`, the refusal of an 8-digit site
`0x10023387`); `//tools/reference-game/setup:capture_dbsim_frames_test` (the runner under Windows
PowerShell 5.1 and, when `ES2_PWSH` exists, PowerShell 7, with a mock regression child on a
synthetic DataRoot: the INI handed down and the environment given back, the manifests of an ok run,
a differing suite and a missing capture, the dump selection and scratch, refusals, labelled runs,
partial reruns). Mutations caught: `capture_report=beside_tick_log` replaced (runner test), the
palette dropped from the comparison fields (`dbsim_frames_unit_test`).

## The original's own frame spread

Three captures over the 40 registered suites (`capture-dbsim-frames.ps1
-HiddenWindow`, one prepared proxy for all runs, five clones `timing-r111`,
`-p1`, `-p2`, `-p4`, `-p5`): **A1** and **A2** with every hook original,
**R** with all 782 hooks of `proxy/hook-names.txt` replaced (original
`CW3220.DLL`). Evidence in `es2-data\logs`: `dbsim-frames-20261021` (A1),
`-a1_retry` (A1's `c3_09-loadout-b` again: its first start failed the CD
volume check, not a frame difference), `-a2`, `-replaced`; the pairwise
results in `t67-compare-original-20261021.json` and
`t67-compare-r-20261021.json`. Every suite's tick-log result was
`identical` in all three runs.

**The original against itself** (`dbsim_frames.py compare`, through the
shorter run's last frame): A1 = A2, `dbsim-frames-20261014` = A1 and
`dbsim-frames-20261014` = A2 for 39 of 40 suites. The one exception is
`c3_10-detail-options`: the frame set of frame 600 (the detail options
dialog, 146 or 147 presents) differs between all three runs, and nothing
before it. So the original's frames are deterministic everywhere the suites
go except inside a modal options dialog, where the number and content of
the presents depends on the run. Hypothesis: the asynchronous
`0xa000`/`MM_MCINOTIFY` messages in the dialog pump
([main_loop.md "The message trace of the replay pump"](main_loop.md#the-message-trace-of-the-replay-pump)).
The own process's frame compare treats frame 600's set of
`c3_10-detail-options` as the original's spread (`sim/tests/frames_known.py`).
`c1_01-options-dialog` was identical in all three original runs.

**Every hook replaced (R).**

- **Sites from own code.** Without the site announcement a present called
  from own code records the proxy's return address (`0x7xxx3257`, the
  DLL's load address varies) as its site instead of an image VA;
  `dbsim_frames.py` refuses such a table ("bad site"). In R it is the first
  frame's second present (A1: `0x461e21`).
  [Present sites from own code in the proxy capture](#present-sites-from-own-code-in-the-proxy-capture)
  attributes it.
- **The capture runs past the frame limit.** R's tables go on after A1's
  last frame (last frame 4310 against A1's 1675 in `acceptance-60s`, 3091
  against 1366 in `c1_01-cheat-keys`; `dbsim-frames-20261021-pr03`: 6648
  and 3665); the suites' results are still identical, so the extra presents
  come after the comparison window (hypothesis: the frame limit's stop takes
  a longer path with the hooks replaced; not checked).

With own-code sites normalised and R cut at A1's last frame, R = A1 in 38 of
40 suites. The two options-dialog suites differ: `c3_10-detail-options`
already at frame 204 (the first dialog, same present count, other index
content) and `c1_01-options-dialog` at frame 300 (another present site,
`0x45490b` instead of `0x45720a`). With every hook replaced the dialog
frames leave the original's spread.

## The presentation frame in the own process

Own code in `sim/src/present.c` (`sim/include/es2/sim/present.h`) and
`sim/src/startup.c` (the teardown); rows in `sim/dispatch/services/presentation_frame.h`,
`presentation_loads.h`, `presentation_roots.h`, and the present row in `mission_load.h`. "In vitro" means
the differential suites `//tools/reference-game/invitro:differential_<stem>_test`
(original machine code against the own code, GDI/USER imports stubbed,
`GetTickCount` on the virtual clock): `present` (`present_cases.h`),
`presentation_loads` (`presentation_loads_cases.h`), `presentation_roots` (`presentation_roots_cases.h`) and the frame part of
`display` (`display_cases.h`). "Unit" means `//sim:present_test`
(synthetic image).

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
- **Page-swap branch**, taken when the word is zero:
  1. Word `[0x4d267e] = (word [0x4d24f2] == 0)`.
  2. `0x42a9c0`, `0x4306ac`, `0x43048c`.
  3. With `s = [0x4d24bc]`: `[s+0x34] = [s+0x8c]`. If `s` is the current surface `[0x6c5ed0]`, the value also goes to `[0x4a3682]` and to the driver slot `[0x4a57f4]`.
  4. `[s+0x3c] = [s+0x88]`. If `s` is current (`[0x6c5ed0]` is read again), the value also goes to `[0x4a368a]` and to the slot `[0x4a57f0]`.
  5. The dwords `+0x88` and `+0x8c` are swapped.
  6. `0x42a4c4`.
- **Registers:** they are the last callee's. None of the nine callers
  (`0x45d1c9`, `0x45d20d`, `0x45d3f5`, `0x45d43b`, `0x45daf9`, `0x45dd07`,
  `0x45dd49`, `0x45dd9c`, `0x45f4a0`) reads them.
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
- **Registers:**
  - Per slot, EDX = the slot address. A waiting slot leaves EAX = the deadline address and EDX = the deadline.
  - A cycled slot leaves EAX = the new deadline, EDX = its address and ECX = the index address.
  - EAX starts at 0.
- **Own deviation:** n > 256 is refused. It would overrun the original's stack buffer.
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

Listing `0x465cc4..0x465e26`; row `palette_upload`.

1. `GetDC(0)`.
2. n = `GetDeviceCaps(dc, 24)`. Index 24 is NUMCOLORS: the device's colour count (load.h's field `reserved_entries` holds it); a 32-bit desktop answers −1.
3. `GetSystemPaletteEntries(dc, 0, 256, E)`.
4. r = n/2 (`sar` with the sign fix-up: towards zero).
5. The flags of E[0..r−1] are set to 0.
6. E[r..255−r] = the bytes 0, 1, 2 of `[P+8]`'s entry, each shifted left by 2 (8 bits), with flags 5. `[P+8]` is re-read for every entry.
7. The flags of the rest are set to 0.
8. `ReleaseDC`.
9. **GDI** (`[0x4d255e]` = 0):
   - The RGBQUADs are built: blue, green and red from E. The fourth byte is never written, so it is stack residue.
   - `SetDIBColorTable([0x4d3034], 0, 256, quads)`, then `AnimatePalette([0x4d302c], 0, 256, E)`.
   - EAX = AnimatePalette's result.
10. **DirectDraw** (`[0x4d371c]->SetEntries`): refused.

Further rules:
- r above 128 leaves the middle range empty (every flag 0).
- n ≤ −2 or n ≥ 514 writes outside E in the original: refused.
- Headless, NUMCOLORS is −1, so all 256 entries come from P.
- The table goes to the load layer's platform record when one serves the loop (the same record `0x465e28` writes), so the capture hashes one "last table".
- Own deviations: the quad's fourth byte is written as 0 (the original leaves stack residue there); entries GetSystemPaletteEntries does not fill are zero.

**Verification:** listing; in vitro 800 cases identical (DATA, the
AnimatePalette table, the SetDIBColorTable RGB bytes, handles, call counts)
with NUMCOLORS answers from −1 to 513 and the DIB DC set or unset; unit:
values and refusals.

### The palette set `0x481fe0` with `0x465e28`, `0x465c40`, `0x465a24`

Row `palette_set` (`load.c` `palette_apply`, `palette_entries`). The listing
facts behind the model are in load.c and in
[mission_load.md "The mission load in the own process"](mission_load.md#33-the-mission-load-in-the-own-process).
Further listing facts:

- `0x465e28` indexes the colours through `[P+8]`, re-read every iteration, plus 4i. Per entry, byte 3 bit 0
  selects the shifted copy (`<< 2`, bytes 2, 1, 0) and bit 1 the raw copy. An entry with neither bit, and
  byte 3 (RGBQUAD's reserved byte) of every entry, are never written: the original keeps the stack's bytes
  there ([Deviations](#deviations)).
- `0x465a24`'s nudge reads the entry again after each match. Its source table is the local table, so a
  NUMCOLORS answer whose half is negative (below −1) reads before the table, in the original too.
- `0x4820b7` passes its arguments 2..5 to `0x486e10`, which copies `count` (unsigned `loop`) pairs of
  dwords to `0x4a4fa5 + 8·start`. `0x48d99e`'s result (argument 1) is not used.
- **Registers:** they are not modelled; the row leaves the entry registers. Neither caller reads them:
  - after `0x43049a`: `pop ecx`, EDX loaded, EAX zeroed at `0x4304c4`;
  - after `0x461722`: `pop ecx`, then `0x464c78`, which writes them before reading.
- **In vitro, `presentation_loads` part "palette": 1400 cases identical.**
  - The original `0x481fe0` runs with `0x48d99e`, `0x4820b7` and `0x486e10` as machine code. `GetDC`,
    `ReleaseDC`, `SelectPalette`, `RealizePalette`, `CreatePalette`, `DeleteObject`,
    `GetSystemPaletteUse`, `GetDeviceCaps`, `GetSystemPaletteEntries` and `SetDIBColorTable` are stubbed;
    `GetSystemPaletteEntries` writes nothing (the headless answer).
  - Random inputs: range start −4..40 and count 0..12; colour bytes from a small set, so that the nudges
    +3 and −3 happen; DATA's entries from the same set; palette use 2 or not; NUMCOLORS −1, 0, 1, 2, 20,
    256 or 0..512.
  - Compared: DATA (LOGPALETTE `0x4d3728..`, `0x4d302c`, `0x4d3b2c`, the ranges); the colour table
    `SetDIBColorTable` received, against the load record's `color_table`; the ten call counts against the
    record's counts.
  - Coverage: both palette-use paths, r = 0 and r > 0, the three entry kinds, both nudges, a count and no
    count.
  - **Caught mutation:** in load.c's nudge against the last r entries, `< 0xfcu` changed to `< 0xfdu`
    fails the palette part (case 19, 2 DATA bytes).

### The present `0x464a14` → `0x465628`, GDI branch

Listing `0x464a14..0x464a26`, `0x46589a..0x4659e5`; `present.c` `present()`.

- `0x464a14` calls `0x465628(S = 0x4d3018, R = 0x4d2ff8)`. DirectDraw (`[0x4d255e]`, `0x465647..`) is refused.
- Without the DIB DC `[0x4d3034]`, nothing happens. Otherwise:
  1. `GetClientRect`, `InvalidateRect(client, TRUE)`, `BeginPaint`.
  2. If there is a palette `[0x4d302c]`: `SelectPalette(FALSE)` and `RealizePalette`.
  3. sx = (client.right<<16) / `[0x4d253e]`, sy likewise with the height (signed `idiv`).
  4. dx = (sx16 R.left · sx) >> 16, dy likewise.
  5. w = R.right − R.left + 1, h likewise.
  6. dw = client.right when w equals the game width, else (sx16 w · sx) >> 16; dh likewise.
  7. `StretchBlt(dc, dx, dy, dw, dh, [0x4d3034], S.x, S.y, w, h, SRCCOPY)`.
  8. If there is a palette (`[0x4d302c]` re-read at `0x4659b2`): the old palette is selected back (TRUE) and `RealizePalette` is called again (`0x4659c8`).
  9. `EndPaint`.
- No guest write.
- **Verification:** listing; in vitro 1000 cases identical (every GDI/USER
  call counted, StretchBlt's eleven arguments, the select-palette flags),
  with random game sizes, client areas, rectangles (including 16-bit
  truncation) and page origins, palette and DIB DC set or unset. The second
  RealizePalette was found this way.

### The present row `0x464a14`

Row `present` (`mission_load.h`) → `es2_sim_service_present_capture` (present.c),
as [mission_load.md](mission_load.md#34-the-roots-last-callees-the-terrain-shading-pass)
proposes. It performs `present()`:

- the GDI branch compared in vitro (1000 cases, above);
- the load record's present count (`table(c)->presents`);
- the frame record written with the site of [Present sites from own code](#present-sites-from-own-code).

Deviations: EAX = 1 where the original keeps the entry EAX without a DIB DC
(no caller reads it); a zero game size is refused (the original divides by
zero). Suites that replace the row after the seal (dialogs, display,
`//sim:dialogs_test`) do not depend on its function; dialogs and
`//sim:dialogs_test` replace the `font_load` row after the seal instead of
stubbing `0x430fb0`.

### Present sites from own code

The original calls the present `0x464a14` from 29 sites (listing: every
`call 0x464a14`). The frame record's `site` is the return address of the
call. An own C caller (`es2_sim_call`) has return address 0, and a call the
trap enters from a core bridge has a host address (`0x10014097` in the
headless runs). The row picks the site in this order (`call_site()` in
present.c):

1. a site announced by `es2_sim_present_site(va)` just before the call (consumed by the next present);
2. a return address inside CODE `[0x401000, 0x497000)`, unchanged;
3. a trapped call from a known machine caller (table `trapped_sites`): when the innermost trapped entry is
   the present `0x464a14` and the entry one level out is `0x461608`, the site is `0x461e21` (the return
   address of `0x461e1c call 0x464a14`); `es2_sim_trap_entry_va` (dispatch_trap.c) gives the entries;
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
`WinHelpA` is a platform deviation (no WinHelp on current systems): the call
is counted in the present instance's platform record (`win_help`,
`win_help_args`) and EAX is `win_help_answer` (headless 0, "failed"). With DirectDraw on, the row calls `0x4667c8` through the dispatch (row
`fullscreen_switch`, [The DirectDraw full-screen switch](#the-directdraw-full-screen-switch)). Callers: after `0x4345d1` comes `pop ecx` and the function
returns EAX; after `0x460827` comes `pop ecx` and a jump to `0x46085c`,
which writes EAX. Neither reads EDX or ECX.

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

**Registers:** both rows leave EDX and ECX as they were at entry, while the
original leaves the system calls'. No caller reads them: see above for
`0x4669c4`; for `0x466730`, WinMain after `0x46547a` and `0x4667c8` after
`0x466944` do not read them before writing them.

**In vitro, `presentation_roots`: 2000 cases identical**, each with 1..6 calls of the two rows.

- **Random per call:** `[0x4d255e]` (0, 1 or random), `[0x4d3610]`, each of the four object dwords (0 or
  one of four objects), the file argument and the entry registers. The screen size is random per case.
- **Original side:** runs `0x4669c4`/`0x466730` as machine code. `WinHelpA`, `GetSystemMetrics` and
  `ClipCursor` are import stubs. `0x4667c8`, `0x46548c` and Release are recording stubs (own side: dispatch
  services).
- **Compared:** EAX of every call, DATA with the heap, and the stub log (own side: built from the platform
  record).
- **Coverage, every path taken:** help with DirectDraw off and on; frame clear and no clear; each of the
  four objects released and absent.
- **Caught mutation:** swapping the release order of `0x4d3718` and `0x4d3714` in present.c fails the
  suite (case 1, stub log) and `//sim:present_test` (`help_release`).

`//sim:present_test` `help_release` covers help with DirectDraw off and on
(on: the switch back to the window), and release with none, all and one object, plus a
refused Release.

### The load wrappers `0x430f58` and `0x430fb0`

Rows `panel_load` and `font_load` (`presentation_loads.h`), CDECL, one slot, both with
`es2_sim_service_load_by_name` (load.c).

- **Listing:** both are byte for byte the load by name `0x45cdf4` (and `0x465f7c`). Only the immediate of
  the exception record differs (`0x49b1ac`, `0x49b1d0`; `0x45cdf4` uses `0x49ef70`). The steps:
  1. `0x49630c` links the frame record (fs:0, and the construction counter is read into it).
  2. Word `[ebp-0x40] = 8`.
  3. `0x472f14(ebp-0x30, path)`.
  4. `0x47a144(S, 0)` gives X.
  5. `0x472f64(S, 2)`.
  6. EAX = X, EDX = `[ebp-0x50]`, the fs:0 head at entry, which is stored back to fs:0.
- **Registers:** ECX is the entry ECX in the own row; the original leaves `0x472f64`'s ECX. Every caller pops
  ECX right after the call (`0x431104`, `0x431145`, `0x454837`), so this does not matter.
- **In vitro, `presentation_loads` part "load": 600 cases identical**, each with 1..4 calls.
  - The original side runs `0x430f58`/`0x430fb0`, `0x49630c` and `0x472f14` as machine code. The own side
    goes through the dispatch.
  - `stream_open 0x4730e8`, `dts_load 0x47a144` and `stream_destroy 0x472f64` are recording stubs on both
    sides. They log the stream's fields, whether the stream is the one opened, the path and the flags, and
    dts_load answers a random value.
  - Compared: EAX, EDX, DATA (the counter included) and the stub log.

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

- `es2sim --headless ... --frames <path>` writes one ndjson record per present, in the
  [capture record](#capture-record) format: start line, records, and a closing `stats` line with `records`.
- Field values:
  - The key is `frame_counter_0x4d257b`; the presenter is `es2sim-headless`.
  - `site` is the [attributed site](#present-sites-from-own-code) (`0x45fbe4` from the frame sequence).
  - `ordinal` counts per (frame, site), up to 64 sites.
  - `S`, `R`, `client`, `src`, `dst` are as StretchBlt receives them.
  - `source` is `dib`, or `dib_outside` when the window is not inside the DIB.
  - `index_sha256` is the SHA-256 of the w×h window at bits + S.y·stride + S.x (stride `[0x4d2546]`, height `[0x4d254a]`, bits `[0x4d3028]`).
  - `palette_sha256` is the SHA-256 of the 768 RGB bytes of the last uploaded table, null before the first upload.
  - `uploads` is the table's upload count; `presented` is `headless`.
  - The display mode and region hashes as `capture_regions=1` gives them.
- The closing line is written at the normal end (atexit), not on the `ExitProcess` refusal paths. A capture without it is "not closed" for `dbsim_frames.py`.
- `es2sim --dump-frames LIST --dump-dir DIR` writes ES2F dumps
  ([own_process.md](../reference-game/own_process.md#the-frame-compare-as-tests)).
- Unit: five records on a synthetic 16×8 DIB read back, with the hashes recomputed, ordinals, `dib_outside`, a new frame and the stats line; the unit test's capture is `identical` to itself under `dbsim_frames.py compare`.

The windowed own process shows the frame of the GDI present through the last
uploaded colour table (`sim/src/platform_sdl.c`,
[own_process.md "The windowed platform layer"](../reference-game/own_process.md#the-windowed-platform-layer)).

### Frames against the original

The per-suite compare of `es2sim --headless --frames` with DBSIM's capture is
`//sim:frames_<suite>_test` ([own_process.md "The frame compare as tests"](../reference-game/own_process.md#the-frame-compare-as-tests),
40/40 pass; the one known row is the original's spread of
`c3_10-detail-options`).

Site attribution, checked against `dbsim-frames-20261014`,
`dbsim-frames-20261021` and `-a2` for `acceptance-60s`,
`c1_01-cockpit-keys`, `c1_01-quit-dialog` and `c1_01-options-dialog`:
`dbsim_frames.py compare` (sequence sites one by one, other presents per
frame as a set without the site) is identical in all 12 comparisons; every
site is an image VA, none unattributed. Keyed by site, `acceptance-60s`,
`c1_01-cockpit-keys` and `c1_01-quit-dialog` match all three references.
`c1_01-options-dialog` keyed by site first differs at frame 500: the
original runs 296 consecutive presents at `0x45720a` and then the close at
`0x45490b`; the own run has 298 before the close, which shows the image of
its last step (590 against 592 `0x45720a` presents in the whole run): the
options dialog's step-count spread
([main_loop.md "The message trace of the replay pump"](main_loop.md#the-message-trace-of-the-replay-pump)).
The compare does not show it because it ignores the site of other presents.

### Deviations

- **Headless GDI answers.** No window exists. The GDI/USER calls are counted
  and answered as a 32-bit desktop answers them: NUMCOLORS −1; no system
  entries; the client area is the game size unless the host sets one;
  palette use and the handle from the load record.
- **The palette upload's quad byte 3** is written as 0 (the original leaves
  stack residue there).
- **`0x465e28`'s local table:** for an entry whose colour has neither flag bit, and for byte 3 of every entry,
  the original passes the stack's bytes on: to `0x465a24` (hence DATA `0x4d372c..`) and to
  `SetDIBColorTable`. The own code uses 0.
  - In vitro, the thunk zeroes 8 KiB below its stack pointer, so that these bytes are 0 in the original too,
    except where `0x4820b7`/`0x486e10`'s frames lie (entries 232..255). Entries without a flag are therefore
    generated below entry 200 only, and byte 3 is compared there only.
  - Live ([own_process.md](../reference-game/own_process.md), the residue table): every colour DBSIM
    installs has flag bit 0. The DPL records are all `01` ([dpl.md](../formats/dpl.md)), the shield and
    intro entries are `0x01000000 | rgb`, and the cycler and the damage colours copy entries. So only byte 3
    is residue. It reaches only `SetDIBColorTable`'s `rgbReserved`, because `0x465a24` takes bytes 0..2 and
    sets its own flags. DBSIM imports no `GetDIBColorTable`, and both frame hashes take bytes 0..2.
  - In the game, the bytes are the residue of earlier frames. GDI ignores the reserved byte of a DIB colour
    table, so only the no-flag entries can change a colour.
- **DirectDraw** is modelled, not called
  ([The DirectDraw full-screen switch](#the-directdraw-full-screen-switch), platform choices).
- **Refusals:** a DirectDraw method through a null object (`0x465628`,
  `0x465cc4`, `0x465e28`, `0x46548c`), `0x465e28` from the window procedure (`0x466228`, messages
  `0x30f`/`0x311`) headless, the overruns listed per function.

## Open questions

- The value of `NUMCOLORS` and of the system palette under the 256COLOR
  emulation (r = 10 is a hypothesis), and the uploaded table without it
  (r = 0); a capture's `palette_sha256` together with one dumped table
  answers it.
- `DIB_PAL_COLORS` (usage 1) in `CreateDIBSection` with a colour table
  later set by `SetDIBColorTable` as RGB quads: which GDI interpretation
  applies to the `StretchBlt` source on this host was not checked; it does
  not affect the indexed hashes.
- Whether the system DirectDraw accepts `CreateSurface` without
  `DDSD_CAPS`; the DirectDraw branch was never observed live with the
  system DirectDraw (`directdraw=windowed` is tested against DBSIM's call
  sequence from the bytes and in the visual check).
- Whether any code reads the DIB back for a simulation decision (pixel
  picking): not found in the presentation code; the renderer and HUD
  read it for their own drawing (strip save `0x488012`). A search over all
  readers of `[0x4d3028]`/`[0x4d2ff4]` was not done.
- With every hook replaced the capture runs past the frame limit
  ([The original's own frame spread](#the-originals-own-frame-spread)); the cause is not checked.
- `dbsim_frames.py compare` compares other-site presents per frame without
  the site; keying them by site would expose the options-dialog step-count
  spread ([Frames against the original](#frames-against-the-original)).
- The DirectDraw branches of `0x465628`, `0x465cc4`, `0x465e28` and the
  frame clear `0x46548c` are modelled from the listings and unit-tested
  (`//sim:present_test`), not in vitro; only the switch has an in-vitro
  suite. `0x465e28` from the window procedure is not modelled.
- The desktop full screen of es2sim_window was checked on the dummy video
  driver only; a visual check on a real display is open.

## The DirectDraw full-screen switch

Row `fullscreen_switch` (`0x4667c8`, `es2_sim_service_fullscreen_switch`,
[sim/dispatch/services/fullscreen.h](../../sim/dispatch/services/fullscreen.h)),
present.c. Its callees `0x46658c` (create), `0x4665c4` (mode and surfaces)
and `0x45f13c` (the panel call) have no other caller and are inlined.
Callers: WinMain `0x46543c` when `[0x4d255e]` is set at the start (`-Z1`,
or prefs byte 6, which the shell's Full Screen writes), Alt+Enter
(`0x45fea9`), the cockpit system gadget's second item (`0x4345da`),
`0x4669b4` and the help call `0x4669c4` (both only while DirectDraw is on).

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
      `SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, width, height, 0x108)`.
   8. `CreatePalette(4, 0x4d372c, &[0x4d371c], NULL)`; on 0
      `SetPalette(primary, palette)`.
   9. `ClipCursor({0, 0, W, H})`; `SetCursorPos(W / 2, H / 2)` (`sar` with
      the sign fix-up, toward zero); `ShowCursor(FALSE)` when the cursor
      sprite `[0x4d3724]` is set.
3. **Back to the window** (`[0x4d255e]` ≠ 0): `[0x4d36fc]` ← 1; the release
   `0x466730` (row `dd_release`); `[0x4d255e]` ← 0; `GetSystemMetrics(0)`,
   `(1)` (dead); `SetWindowPos(hwnd, HWND_NOTOPMOST, left, top,
   right − left, bottom − top, 0x10)` from `0x4d36cc`; `ShowCursor(TRUE)`
   when the sprite is set.
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

**In vitro, `fullscreen`: 3000 cases identical**
(`//tools/reference-game/invitro:differential_fullscreen_test`), each with
1..6 calls of the row.

- **Original side:** `0x4667c8`, `0x46658c`, `0x4665c4` and `0x45f13c` run
  as machine code. DirectDraw is a world of four objects whose vtable
  methods are recording stubs honouring a seeded failure mask.
  `DirectDrawCreate`, the nine USER32 calls and `_memset` are import stubs;
  `0x466730` and `0x4650d0` are patched with stubs.
- **Own side:** the platform record carries the same objects, mask and
  `AdjustWindowRectEx` margins; `dd_note` writes the same log.
- **Random per call:** the mode, `[0x4d3700]`, the sprite, the window
  handle, the game size (640×480, 320×200, random, negative and odd
  included), `0x4d36cc`, the four object dwords and the panel (none,
  without widget, with widget).
- **Compared:** EAX of every call, DATA with the heap, the stub log.
- **Coverage, every path taken:** both directions; rectangle kept and read;
  every DirectDraw call failing and answering; each release present and
  absent; below 640 and not; the failure path; the sprite on and off in
  both directions; the panel's three cases.
- **Caught mutation:** dropping the retry's caps clear in present.c's
  `dd_mode` fails the suite (case 8, stub log).

`//sim:present_test` `fullscreen` covers both directions, 320 and 640, the
failure path, the frame clear's and the palette's DirectDraw tails and the
null refusal. `//sim:platform_sdl_test` checks the request on the dummy
video driver (the driver's answer is counted), the cursor mapping and the
way back. `//sim:es2sim_real_test` runs es2sim headless with `-Z1` to the
same end as without it.
