# VSHELL process and start-up

Subject: the process shell of the reference `VER95/VSHELL.EXE` (image
base `0x00400000`, [reference_version.md](../reference_version.md)):
WinMain, the command line, the start-up sequence, the window and its
procedure, the main loop, the exit, the present and draw sites other
contracts rely on, and the Alt+Tab crash `0x453a68`. It is the VSHELL
counterpart of DBSIM's [presentation.md](presentation.md) for the process
part; the drawing is in [vshell_presentation.md](vshell_presentation.md),
the widgets and events in [vshell_widgets.md](vshell_widgets.md).

**Method.** Static: the `llvm-objdump -d -M intel` listing of the CD file
with import thunks named from the PE import table, and DATA and `.rsrc`
bytes read by small scripts. Jump tables that the linear listing decodes as
code (`0x40162c`/`0x401664`, `0x40169c..0x4016c2`) are decoded from the file
bytes. Nothing in this document is checked in vitro or live. **C** =
static at the cited addresses; **H** = hypothesis. Facts already in
[vshell_ui.md](vshell_ui.md) §1/§2, [launcher.md](launcher.md#other-side-of-the-protocol),
[file_io.md](file_io.md#vshell-call-sites) and
[formats/vol.md](../formats/vol.md#original-registration-and-lookup-order)
are referenced, not repeated.

## Summary

1. **One window, one thread, one loop (C).** WinMain `0x406ab8` registers
   class `shell` with the exported window procedure `MainWndProc` at
   **`0x404fb8`** (`0x405289` lies inside its key branch), creates the only
   window of the process (`CreateWindowExA` has one call site, `0x4069b8`),
   builds an 8-bit 640×480 DIB section, starts a 15 ms timer whose procedure
   services the sound library, and calls the start-up `0x401aa4`, which
   contains the main loop and returns only at the end.
2. **`BWCC.DLL` is loaded and never used (C).** `0x40687c` calls
   `LoadLibraryA("BWCC.DLL")` between filling the window class and
   `RegisterClassA` and discards the handle. The process has no dialog, no
   menu resource and no other window; `.rsrc` holds only the icon
   `ES2ICON`. The CD has `BWCC.DLL` only in `VER31`, so in a Windows 95
   installation the load fails without effect (as DBSIM's unused
   `BWCC32.DLL`, [dll_imports.md](../reference-game/dll_imports.md)).
3. **The full repaint `0x406e87` is a re-present, not a redraw (C).**
   `0x406e87` copies the whole 640×480 frame buffer to the window through
   the present primitive `0x406bfb`; neither touches a widget or a bitmap.
   WM_PAINT, WM_SETFOCUS and the display-change message reach only this
   copy. Widgets are drawn only when the event drain of the main loop
   dispatches their queued paint events.
4. **VSHELL stops working while it has no focus (C).** The main loop and
   the drain helper `0x402266` skip input, event dispatch, present and
   movies while the focus flag `0x46c288` is 0; only Windows messages are
   pumped. Every show and repaint posted in that time is executed after
   focus returns, in queue order, without a visibility check.
5. **The Alt+Tab crash is a deferred paint of a freed image (H, from the
   bytes).** The crash instruction reads the data pointer of a freed
   `GLBitmap`. Two image panels keep their bitmap pointer after the bitmap
   array is freed: the Telecomm panel `[0x48d8c4]` (guarded by the proxy)
   and the full-screen theatre picture panel `[0x48d8c0]` (`0x44483e` hides
   it and frees `[0x478064]` without clearing `+0x5d`; not guarded). Section
   11 gives the sequence, the window in which an Alt+Tab triggers it, and
   what a crash dump must show to confirm it.
6. **Alt+Tab, Ctrl+Esc and Alt+Esc change `prefs.cfg` (C).** VSHELL sees
   them as its own keys (`0x4052aa`): it leaves full-screen mode, sets
   preference byte 6 (display) to 0 and writes `data\prefs.cfg`, so the next
   start is windowed. Alt+Enter (`0x405317`) toggles full screen on key-up
   and stores byte 6 accordingly; Alt+F4 opens the Quit box.

## 1. Image, imports and resources

| Item | Fact (C) |
|---|---|
| Sections | `CODE` `0x401000` (0x6ae00 bytes), `DATA` `0x46c000` (0x15200 raw, 0x172000 virtual, so `0x481200..0x5ddfff` is zero-initialised), `.idata`, `.edata`, `.reloc`, `.rsrc` (0x400 bytes) |
| Exports (`dbshell.exe`) | `__GetExceptDLLinfo` `0x40106e`, `sfxWndProc` `0x404e21` (a TimerProc, section 6), `MainWndProc` `0x404fb8` (the window procedure) |
| Imports | `cw3220.dll` (Borland RTL), KERNEL32, GDI32, USER32, WINMM (MCI strings and commands, joystick), DDRAW (`DirectDrawCreate` only); no `AllocConsole` |
| Resources | icon group `ES2ICON` and one icon; no menu, dialog, string table or accelerator |
| Version string | `0x47158c` → `0x471590` (`v1.0.0`, build date March 1996), printed only by `-v` |
| Initial DATA values | focus flag `0x46c288` = 1, full-screen flag `0x46d878` = 1 (WinMain overwrites it), arena size `0x46c284` = 4,000,000; all other flags 0 |

## 2. WinMain `0x406ab8`

`__stdcall`, returns with `ret 0x10` at `0x406bf8` (C).

| Step | Address | What (C) |
|---|---|---|
| 1 | `0x406acb` | `0x406a37(lpCmdLine, argv)`: argv\[0\] is the literal `dbsim`; the rest is split at single spaces (code 0x20 only, repeated spaces skipped, no quoting), each token copied into an allocation (`0x40870a`). The array is 27 pointers on the stack (`ebp-0x6c`) without a bound check. |
| 2 | `0x406ad8` | Only if `hPrevInstance` is 0 (always in Win32): `0x40687c` = window class and `BWCC.DLL` (section 6); a zero result returns 0. |
| 3 | `0x406aee..0x406b26` | Any argument starting with `-` or `/` and followed by lower-case `d` clears `0x46d878`. **Dead:** step 4 overwrites `0x46d878`. |
| 4 | `0x406b28..0x406b34` | `0x40dc44`: clear the 54 preference callbacks `0x482608` and load `data\prefs.cfg` into the block `0x48259c` ([vshell_preparation.md](vshell_preparation.md#shell-options)); then `0x46d878` := preference byte 6 (`0x4825a2`, display: 1 = full screen). |
| 5 | `0x406b3d` | `0x4068ea(hInstance, nCmdShow)` creates the window (section 6). On failure: two `LoadStringA` calls into `0x481788`/`0x4817a8` (no string table exists, the buffers stay empty), an "Init Error" message box with fixed text, return 0. |
| 6 | `0x406b87..0x406bad` | `joyGetNumDevs` → `0x481f34`; if nonzero `joyGetDevCapsA(0, 0x481d9c, 0x194)` and `joySetThreshold(0, 0x200)`. |
| 7 | `0x406bb2..0x406bbc` | If `0x46d878`: `0x407636` enters full-screen DirectDraw mode (section 6). |
| 8 | `0x406bc1` | `0x4077ee`: clear the screen to black ([vshell_presentation.md](vshell_presentation.md#the-clear-0x4077ee)). |
| 9 | `0x406bc6` | `0x46d87c` := 1 (window ready; read by WM_SETFOCUS). |
| 10 | `0x406bd5` | `0x401aa4(argc, argv)`: start-up, main loop and loop end (sections 4 to 9). |
| 11 | `0x406bdd..0x406be4` | `while (!0x406328()) ;` pump until WM_QUIT arrives (the loop end posted it). |
| 12 | `0x406be6` | `0x4075c2`: release the DirectDraw objects `0x481f44`, `0x481f40` (primary surface), `0x481f48` (palette), `0x481f3c` (DirectDraw) through their `Release` slot, each only if set. |
| 13 | `0x406beb..0x406bf0` | Return the sign-extended word `0x46e348` (getter `0x408d28`). |

## 3. Command line

### Option parser `0x4015d8(argc, argv)` (C)

Elements 1.. are examined in order (`0x4015ea..0x401800`). A token counts
only if it starts with `-` or `/`; the next character, minus `?`, indexes a
56-entry byte table at `0x40162c` and a jump table at `0x401664`. Letters
are case-insensitive except `X`. Any other character is ignored.

| Option | Effect | Address | Notes |
|---|---|---|---|
| `-a`/`-A` | movie flag `0x482359` := 0 | `0x40169c` | all AVIs off (`0x41e8c0`, `0x41e98c` test it); ES.EXE passes `-a` with its own flag A |
| `-@` | cheat byte `0x482368` := 1 | `0x4016a8` | [launcher.md](launcher.md#-sprunknown-debugcheat-mode), [vshell_ui.md](vshell_ui.md) §4.11 |
| `-f`/`-F` | language `0x48235e` := 1 (French) | `0x4016b4` | |
| `-g`/`-G` | language := 2 (German) | `0x4016c3` | |
| `-e…`/`-E…` | the exact token `-eggplant` sets `0x46c278` := 1; any other `-e…`/`-E…` (also `/eggplant`) sets language := 3 | `0x4016d2..0x4016f5` | language 3 has no directory prefix in `0x408bb5` (only 0/1/2 map to `eng\`/`fre\`/`ger\`); **H:** the text loads then fail |
| `-m`/`-M` | `0x482354` := 1 | `0x401704` | read only by the dead argv builder `0x42f960`: no effect |
| `-k`/`-K` | keyboard flag `0x482355` := 0 | `0x401710` | the keyboard device object is then not created (`0x401ed4`) |
| `-r`/`-R` | entry mode `0x482362` := 3 | `0x40171c` | **dead:** the start-up overwrites the entry mode at `0x401bc6` |
| `-s`/`-S` | sound flag `0x482356` := 0 | `0x40172a` | ES.EXE passes `-s` with its flag S |
| `-l`/`-L` | word `0x482364` := 0 | `0x401736` | read only by the dead `0x42f960`: no effect |
| `-v`/`-V` | `printf` of the version line, sound flag := 0, `0x40988c` | `0x401744` | **continues** the start-up; `0x40988c` (device and sound shutdown) finds nothing to shut down yet |
| `-X<n>` | exit code := `(short) atol(token + 2)` (`0x408d1a`) | `0x401768` | upper-case only; the start-up copies it into the entry mode |
| `-?` | usage text, sound flag := 0, `0x40988c` | `0x401783` | continues like `-v` |

The usage text (`0x401783..0x4017f1`, ten `printf` calls) names `-f`,
`-g`, `-m`, `-k`, `-r`, `-s`, `-v` and `-?`, each in the four spellings
`-x -X /x /X`, with one-line descriptions (French, German, no mouse, no
keyboard, returning from the simulation, no sound, version, options). It
omits `-a`, `-@`, `-e`/`-eggplant`, `-l` and `-X`. VSHELL is a GUI program
without a console, so the text and the version line go nowhere visible (C:
no console import).

### Defaults before the parser: `0x40796c(0)` (C)

Called at `0x401b15`, before the parser: mouse flag 0, keyboard 1, sound 1,
`0x482357`/`0x482358`/`0x48235a` := preference bytes +0/+1/+2, language
`0x48235e` := preference byte +0x2b, `0x482364` := 1, `0x482366` := 0, movies
1, cheat byte 0, `0x407caf`, exit code := 0 (`0x4079ea`). So the language is
the preference byte unless `-f`/`-g`/`-e…` overrides it. `0x40796c(1)` at
`0x401d39` is the second, resource part (palette 1, font `font2`, the
`estext.bin` table; [vshell_ui.md](vshell_ui.md) §1).

### What ES.EXE passes

`dummy -eggplant -X<state> [-s] [-a] [-@] [-<lang>]`
([launcher.md](launcher.md#start-loop)): `dummy` is argv\[1\] and
ignored (no dash); `-eggplant` sets `0x46c278`; `-X` the exit code/entry
mode; the language letter `-G`/`-F` (or `-E`, which ES.EXE never passes,
because it maps `E` to "no argument").

## 4. Start-up `0x401aa4(argc, argv)`

Ordered as executed (C). "Pump" is `0x406328` (section 8), called between
most steps so that the window stays responsive.

| # | Address | Step | Failure |
|---|---|---|---|
| 1 | `0x401aba..0x401afa` | allocate `[0x46c284]` = 4,000,000 bytes (`0x40870a`) and make it the allocator arena (`0x407d75(0x482480, size, block)`); `0x4086b7` (`new`) and `0x408760` (`delete`) use it | assertion severity 4 (`VSHELL.CPP` line 0x137) → `__exit(1)` |
| 2 | `0x401b02` | `0x40d8df`: `data\drive.cfg` → CD root `0x4823b9` = first token + `\`, install root `0x482369` empty ([file_io.md](file_io.md#the-stream-library-and-path-helpers)) | missing file: assertion severity 4 → `__exit(1)` |
| 3 | `0x401b07` | `0x46a7e4`: the debug monochrome screen (`DARKMONO.VXD`, [file_io.md](file_io.md#vshell-call-sites)); no effect without the VxD | — |
| 4 | `0x401b0c..0x401b15` | `0x48117c` := 1; defaults `0x40796c(0)` (section 3) | — |
| 5 | `0x401b1b` | `0x403c2b(0x200)`: `0x46d168` := max(n, 0x80); the group-manager constructor `0x403b68` sets it to 0x400 one step later (**H:** a volume cache size) | — |
| 6 | `0x401b3c` | VOL registration `0x40356f(0x100, ".vol", CD root, 0)`: creates the 30-slot manager `0x46cae4` (`0x403b68`) and enumerates through `0x403caf`: pass 1 `vol\*.vol` in the current directory (`0x403d29..0x403e15`), pass 2 `<CD>\vol\*.vol` (`0x403e1b..0x403f18`); a name already registered (`strcmp` of `vol\` + file name against the stored path without drive letter, `0x403db7`/`0x403eba`) is skipped, so a local copy wins over the CD's | full manager: assertion severity 3 (logged, continues); the full contract is [vol.md](../formats/vol.md#registration-filtering-and-ties) |
| 7 | `0x401b49..0x401b88` | `error.str` through the VOL-aware stream (`0x428521` into the reader `0x481190`); group 0 into the 12 pointers `0x4811a0`, group 1 into the 3 pointers `0x4811d0` (`0x4286e2`, which asserts unless the group's count is at most the slot count and then stores every string of the group) | missing: assertion severity 4 "Missing string resource" → `__exit(1)` |
| 8 | `0x401bb9` | option parser `0x4015d8` (section 3) | — |
| 9 | `0x401bc1..0x401bce` | entry mode `0x482362` := exit code (the `-X` value); exit code := 0 | — |
| 10 | `0x401bd4..0x401bed` | entry mode 3 or 4: `0x46c274` := 1 (section 7, WM_PAINT) | — |
| 11 | `0x401bf7..0x401c68` | **colour-depth check**, entry modes 0 and 1 only: `GetDeviceCaps(GetDC(window), BITSPIXEL)` (called twice, the second result is compared) ≠ 8 → leave full screen if active (`0x4077df`), message 3 with caption 2 (`0x401a81(3, 2, MB_ICONEXCLAMATION)`), pump, re-enter full screen if it was active (`0x407636`); then **continue** | a warning only |
| 12 | `0x401c6d..0x401ca7` | `-eggplant` check: `0x46c278` = 0 → leave full screen, message 4 with caption 0, `0x40683c` (DIB and timer release), `PostQuitMessage(0)`, return | VSHELL ends with exit code 0 |
| 13 | `0x401cac..0x401d28` | **CD check:** `sprintf(buf, "%s", 0x40d9e1("avi\pt1.avi"))` = CD root + `avi\pt1.avi`; open it with the VOL-aware read stream `0x403065` into the local stream `ebp-0x30`; error field `ebp-0x2c` ≠ 0 → leave full screen, message 5 with caption 0, `0x40683c`, `PostQuitMessage(0)`, close the stream, return. **On success the stream stays open until the loop end** (closed at `0x40224e`). | exit code 0 |
| 14 | `0x401d2d` | `0x40e0f0`: device objects `0x46f2c8`, `0x46f2cc` (`devices.cpp`) | assertion |
| 15 | `0x401d37` | `0x40796c(1)`: palette 1, font, `<lang>\estext.bin` | — |
| 16 | `0x401d44` | game init `0x40e736` ([vshell_ui.md](vshell_ui.md) §2: `GAMEFILE.STR`, `HERC_INF`, `DAMAGE`, slot statistics, mode word) | — |
| 17 | `0x401d4e` | WEAPONS `0x4125e8` (`gam\weapons.dat`, `<lang>\weapons.bin`) | — |
| 18 | `0x401d58` | CAREER `0x412f2a` (`gam\career.dat`, `<lang>\missions.bin`) | — |
| 19 | `0x401d62` | map tables `0x424408`: `dba\mis_icon.dba` → `0x471db8`, `dba\mapshp.dba` → `0x471dbc`, `dfn\map.dfn` → `0x471dc0` (each only if not yet loaded; freed only at the loop end by `0x4244d6`) | assertion severity 4 |
| 20 | `0x401d6c` | `0x4684a8`: event system ([vshell_widgets.md](vshell_widgets.md)) | — |
| 21 | `0x401d71..0x401da0` | the 0x65d-byte screen object `0x48118c` (`0x41fcb7`): the dirty-rectangle list (100 rectangles of 16 bytes at `+0x19`, count `+0x659`) used by the present step (section 8) | — |
| 22 | `0x401da6..0x401e7e` | two cursor objects `0x481184`, `0x481188` from `GetCursor()` (`0x41fc98`); the first is installed in the event system (`+0x35`, and through slot `+0xc` of `0x48118c`); the event system gets the callback `0x4022e9` (`+0x3d`, deletes an object) and mask bits 0x60 (`+0x39`) | — |
| 23 | `0x401e84..0x401f19` | input devices on the device list `0x4811dc`: a local object built on the list (`0x409964`, `ebp-0x40`, destroyed by `0x41dc68` at the end; **H:** a device), the mouse producer `0x408e08` (0x574 bytes, `WinMouseProducer`), and, if the keyboard flag is set, the keyboard device `0x40939c` (0x54 bytes) | — |
| 24 | `0x401f1f..0x401f89` | root window `0x481180` (0x45 bytes, `0x409f20`) with the screen rectangle of the event system (`+0x15..+0x24`) | — |
| 25 | `0x401f94` | sound `0x42f2f4` ([file_io.md](file_io.md#vshell-call-sites): `data\sound.cfg`, SOS) | zero → leave full screen, a fixed "could not create the sound manager" error box, `0x40683c`, `PostQuitMessage(0)`, return (exit code 0). The zero return comes only from the allocation of the sound object; driver failures continue silently ([audio.md](audio.md)) |
| 26 | `0x401ff8` | screens `0x40180c` (section 5) | — |
| 27 | `0x401ffd..0x402009` | show the root window (`0x41f93a([0x481180])`); `0x46c28c` := 1 (the shell is interactive: WM_CLOSE and the special keys act only when it is set) | — |
| 28 | `0x402013` | enter the main loop at its pump (section 8) | — |

**Message boxes.** `0x401a81(msg, caption, flags)` =
`MessageBoxA(NULL, group0[msg], group1[caption], flags)` (C). The English
`ERROR.STR` holds 6 + 3 strings, the German `ERROR.GER` 12 + 3, so the 12
slots fit both. Meaning of the IDs VSHELL uses (paraphrased from the
English file): message 3 "Windows is not in 256-colour mode" (caption 2,
"Notice"), message 4 "run ES.EXE instead" (caption 0, "Warning"), message 5
"insert the CD and restart" (caption 0). The German group 1 is ordered
differently (error, warning, notice), so the captions differ in meaning
between the languages. The movie player uses message 5 as well
(`0x41edae`, section 8).

**Assertions.** `0x44e560(ctx 0x48dd04, severity, value, file, line,
text)` is called by the sites only when their condition failed; severities
4, 5 and 6 report and end the process with `__exit(1)` (`0x44e572..0x44e5b2`),
lower ones only report (C). ES.EXE restarts VSHELL with `-X1` after exit
code 1 ([launcher.md](launcher.md#start-loop)).

## 5. Screens and the entry dispatch `0x40180c`

The 22 constructors `0x401812..0x40187b` and the dispatch at `0x40188a` are
in [vshell_ui.md](vshell_ui.md) §1/§2. Additions (C):

- Between the constructors and the dispatch: pump (`0x401880`) and
  `0x42f5d3` (sound focus: restart music and effects).
- `-X3`/`-X4` (`0x4018a1`): `0x46c274` := 1, load slot 10 (`0x40eaaa(10, 0)`),
  results path `0x40f09f`, music fade-in `0x42f894`.
- `-X6` (`0x4018c6`): music fade-in, pump, clear (`0x4077ee`), `0x46c274` := 1,
  palette 1 (`0x407b62(1)`, `0x407c1b`), show the main menu `[0x48d1a0]`.
- Others (`0x4018fd`): queue the intro movies 0x44 and 0x45 in the rectangle
  (32, 60, 576, 360) with no palette (`-1`) and no flags, play the queue now
  (`0x41e98c(0)`), pump, clear, `0x46c274` := 1, palette 1, show the main
  menu.

`0x46c274` is therefore set on every entry path once the first shell
screen is up; CREDITS clears it around `credits.avi` (`0x431cd8`,
`0x431d2a`). It gates the WM_PAINT re-present (section 7) and the palette
loader `0x4071fe` (`0x40720b`).

## 6. Window, graphics set-up and the timer

**Class (`0x40687c`, C).** `WNDCLASS` on the stack: style 0,
`lpfnWndProc` `0x404fb8`, no extra bytes, `hInstance`, icon `es2icon`
(`LoadIconA`), cursor `IDC_ARROW`, no background brush, menu name
`MAKEINTRESOURCE(1)` (no such resource exists), class name `shell`.
`LoadLibraryA("BWCC.DLL")` runs between filling the structure and
`RegisterClassA`; its result is dropped, no `GetProcAddress` or
`FreeLibrary` refers to it (the only `GetProcAddress` calls are the SOS
loader `0x46ab1d` and the RTL's `__GetExceptDLLinfo` lookup `0x46b6d3`).
**Use: none.** BWCC is Borland's dialog-control library; VSHELL has no
dialog, so the load is a leftover of the Borland project template (H for
the reason, C for the absence of use).

**Window (`0x4068ea`, C).** `0x406389` zeroes the graphics globals and sets
the frame size 640 × 480 (`0x481968`/`0x48196c`) and a BITMAPINFOHEADER at
`0x481974` (size 0x28, planes 1, 8 bits). Style
`WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU | WS_VISIBLE` (0x100e0000) on an
overlapped window (no `WS_POPUP`), title `EarthSiege II`, class `shell`, no
parent or menu. Windowed: outer size 640 + 2·`SM_CXFRAME` by
480 + 2·`SM_CYFRAME` + `SM_CYCAPTION` − 1, centred:
x = (`SM_CXSCREEN` − 640)/2 − `SM_CXFRAME`,
y = (`SM_CYSCREEN` − 480)/2 − `SM_CYFRAME` − `SM_CYCAPTION` (C division,
rounding toward zero). With preference byte 6 the frame size is the screen
size, `0x481f4c` is 1 during the creation, and `SetWindowPos(HWND_TOPMOST,
…, 0x108)` follows; `0x481f4c` is reset to 0 afterwards (`0x4069e7`), and
WinMain step 7 enters full screen. The handle is `0x481f38`. Then
`0x406434(hInstance)`, `ShowWindow(nCmdShow)`, and `UpdateWindow` only when
windowed.

**Graphics set-up `0x406434` (C; details in [vshell_presentation.md](vshell_presentation.md)).**
Library initialisers `0x453048`, `0x45fd44`, `0x463a4c`; `SetTimer(window, 1,
15, 0x404e21)` → timer id `0x481f30`; `CreateCompatibleDC(NULL)` →
`0x48195c`; the system palette (`GetSystemPaletteEntries`, 256) into
`0x48199c` with red and blue swapped; `CreateDIBSection` 640 × −480
(top-down), 8 bits, `DIB_PAL_COLORS`, bits pointer `0x481950` (failure: a
"Bad WinGBitmap" box, and the process continues with a null bitmap); the
bitmap selected into the memory DC (old object `0x481964`); a 14-byte
bitmap header `0x481948` (height, width, 8 bits, size, bits) and the
0xa0-byte `BMP_8` surface object `0x48194c` on it (`0x4530f8`,
`0x4289e4(surface, header, 320, 240)`, `0x45335c`, `0x453528(surface, 9,
0)`).

**Timer (C).** `sfxWndProc` `0x404e21` is the TimerProc: on `WM_TIMER`
(0x113) it calls the sound service `0x42d334([0x473338])` when the sound
manager exists. Timer messages are dispatched by every pump, also while the
main loop idles or a movie plays.

**Full screen `0x407636` (toggle, C).** Entering (`0x481f4c` was 0):
`0x481f4c` := 1, `0x407466` and `0x40749c(width, height, 8)` (DirectDraw
object, cooperative level and display mode; failure → `0x40683c` and
`PostQuitMessage`), a topmost window over the screen, palette entries 0..9
of the two logical palettes `0x481f54`/`0x48232c` flagged 5, a DirectDraw
palette (`CreatePalette` flags 0x4c) set on the primary surface
`0x481f40`, `ClipCursor` to 640 × 480 and the cursor centred. Leaving:
`0x481f4c` := 0, the flags cleared, `0x4075c2` releases the DirectDraw
objects, the window is put back centred as `HWND_NOTOPMOST`. `0x4077df`
leaves full screen if it is active (called before every message box).

## 7. Window procedure `MainWndProc` `0x404fb8`

`__stdcall (hwnd, msg, wParam, lParam)`, returns at `0x40586a` with
`ret 0x10`. Messages not listed go to `DefWindowProcA` (`0x405852`).
"Movie open" is `0x470eb0` ≠ 0 (set by the MCI open `0x41e786`/`0x41e80e`).

| Message | Branch | Behaviour (C) |
|---|---|---|
| `WM_DESTROY` (2) | `0x40548f` | `0x40683b` (empty), `0x40683c` (restore the DC object, delete the DIB, kill the timer), close an open movie (`0x41e5bb`), `PostQuitMessage(0)` |
| `WM_SETFOCUS` (7) | `0x405455` | if `0x46d87c` and a movie is open: `0x406e87` (re-present); focus flag `0x46c288` := 1; if no movie is open: `0x42f5d3` (sound regains focus, music and effects restart) |
| `WM_KILLFOCUS` (8) | `0x405443` | `0x46c288` := 0; `0x42f6a8` (stop the music and give up the sound focus) |
| `WM_PAINT` (0xf) | `0x4057ab` | if `0x46c274` and no movie is open: `0x406e87` (re-present the whole frame); then always `DefWindowProcA` (which validates the region) |
| `WM_CLOSE` (0x10) | `0x4050a3` | if `0x46c28c`: quit flag `0x46c268` := 1; returns 0 either way, so the window is never destroyed by WM_CLOSE |
| `WM_ENDSESSION` (0x16) | `0x405840` | if `wParam` = 1: `DestroyWindow` |
| `WM_GETMINMAXINFO` (0x24) | `0x4054c5` | windowed on a desktop that is neither 640 wide nor 480 high: maximum size, maximum and minimum track size all = the 640 × 480 client's outer size, maximum position (−200, −200): the window cannot be resized. Full screen or such a desktop: maximum size and track size (1300, 800) |
| `WM_DISPLAYCHANGE` (0x7e) | `0x405806` | store the new screen size (`0x481858`/`0x48185c`); if not in full screen: post 0x812 |
| `WM_KEYDOWN`/`UP`, `WM_SYSKEYDOWN`/`UP` (0x100/0x101/0x104/0x105) | `0x4051fa` | key path below |
| `WM_MOUSEMOVE`, `L`/`RBUTTON` down/up (0x200..0x205) | `0x4050bd..0x4051f5` | mouse path below |
| `WM_QUERYNEWPALETTE` (0x30f) | `0x405709` | full screen or movie open: `DefWindowProcA`; else entries 10..245 of `0x481f50` flagged 5, `CreatePalette` → `0x481954`, select and realize in the window DC, `InvalidateRect` if anything changed, return the count |
| `WM_PALETTECHANGED` (0x311) | `0x40565a` | the same with flags 0, skipped when `wParam` is this window |
| `MM_MCINOTIFY` (0x3b9) | `0x4057f2` | `wParam` = 1 (successful): movie-ended flag `0x470ea0` := 1 |
| 0x812 (private) | `0x405580` | after a display change: screen size from `GetSystemMetrics`; 640 × 480 desktop → window rect adjusted to the client (`AdjustWindowRectEx` style 0xce0000) and moved; otherwise moved to (50, 50) with size 0x294 × 0x212 and centred (`0x404ec9`); then `0x406e87` |

**Key path (C; the event side is in [vshell_widgets.md](vshell_widgets.md)).**
The virtual key is looked up in the 84-entry table `0x46d4bc`, which is
ordered by PC scan code (index 1 Esc, 2..0xb `1`..`0`, 0xe Backspace, 0xf
Tab, 0x1c Enter, 0x38 Alt, 0x39 Space, 0x3b..0x44 F1..F10, …). The code is the
index ORed with 0x800 for Shift, 0x200 for Alt (`GetKeyState(VK_MENU)`),
0x400 for Ctrl. Special codes, acted on only when no movie is open and
`0x46c28c` is set:

| Code | Keys | Effect |
|---|---|---|
| 0x23e | Alt+F4 | `0x46a01c([0x5ddcb0])` and show the Quit box `[0x48d1dc]` (`0x40b7e9`) |
| 0x20f, 0x401, 0x201 | Alt+Tab, Ctrl+Esc, Alt+Esc | `0x4052aa`: leave full screen (`0x4077df`); preference byte 6 := 0 (`0x40ddfe(6, 0, 1)`; the callbacks are all null, `0x40e0dd` has no caller); if the Preferences screen `[0x48d388]` is visible: refresh its controls (`0x436e5d`) and repaint it, else copy the block to the shadow `0x4825d2` (`0x40dd6e(0)`) and **write `data\prefs.cfg`** (`0x40dd0a`). Acts on key-down and key-up. |
| 0x21c | Alt+Enter | key-up only (`test ebx, ebx` / `je` after `0x40530c`, ebx = 1 for `0x101`/`0x105`): toggle full screen (`0x407636`), byte 6 := 1 or 0 by the new state, then the same refresh-or-write |

Other codes (`0x40539b`): key-up adds 0x80; the ten hot-key slots
`0x4816a8` (12 bytes: used, key string, callback; registration
`0x404d56`/`0x404db5`, removal `0x404d8b`/`0x404dea`) are offered the code
first; otherwise it goes into the 256-word ring `0x4814a0` (write index
`0x4816a0`, read index `0x4816a4`, reader `0x404d0d`) and bit 1 of
`[0x48e244]` is set. Keys outside the table are dropped. System keys never
reach `DefWindowProcA`, so the system menu cannot be opened by keyboard.

**Mouse path (C).** Every button message first sets the skip flags: during
an intro movie (`0x471124`) `0x471120` := 1, during any movie `0x470ea0`
:= 1, during the map animation (`0x46c26c`) `0x46c270` := 1. With a movie
open nothing else happens. Otherwise `0x405bfc(kind, lParam, buttons)`
scales x and y with `0x4510f1` by factors `0x46d740`/`0x46d744` (set
lazily by `0x405b19`), stores x, y and a button word (bit 0, bit 1) at
`0x4818dc..0x4818e4` and calls each of the ten listeners `0x481864` (12
bytes: used, kind mask, callback) whose mask contains `1 << kind`. Kinds:
0 move, 1 left down, 2 left up, 3 right down, 4 right up. Button values
passed: move `(wParam & 1) + 2·(wParam & 2)`, left down `1 + 2·(wParam & 2)`,
left up `2·(wParam & 2)`, right down `(wParam & 1) + 2`, right up
`wParam & 1`; only bits 0 and 1 survive into the word.

## 8. Main loop `0x402018..0x402191`

Entered at its Windows pump (`0x402013` → `0x402181`). One iteration (C):

| Step | Address | Call | Role |
|---|---|---|---|
| — | `0x402018` | focus flag `0x46c288` = 0 → a debug counter (`0x46a834`) and straight to the pump | **idle while unfocused** |
| 1 | `0x40205b` | `0x41e0bf(0x4811dc, 0xffff, 0xffff)` | input poll: each device on the list (`+0x1c`) whose masks match is polled through its slot `+4` (the keyboard device drains the key ring into events) |
| 2 | `0x402071` | `0x46848c` | event step: `0x46a508`, then drain `0x469ee4([0x5ddcb0])` |
| 3 | `0x402084` | `0x407c1b` | apply a pending palette (`0x46de10`, `0x45414c`) |
| 4 | `0x40209c` | `0x41fe49([0x48118c])` | **present**: for each queued dirty rectangle `0x41fe2c` → `0x406bfb`; count reset |
| 5 | `0x4020b2` | `0x41e98c(1)` | play the queued movies (blocking); `0x46c280` := 1 |
| 6 | `0x4020d0` | `0x4019c6` | run a pending map animation (`0x46c269`): optional palette `0x46c26a`, flush the key ring (`0x40586d`), then rounds of `0x4262cf` (step) and `0x406264` (present the map rectangle) with a pump each until the step returns 0; Esc, Space or a click (`0x46c270`, set by the mouse path while `0x46c26c` is 1) call `0x425a43` (**H:** finish at once) |
| 7 | `0x4020e2..0x402110` | `[0x48dd0c]` ≠ 0 → assertion severity 6 → `__exit(1)` | deferred fatal error (**H:** a pending-error field of the assertion context `0x48dd04`; no absolute writer) |
| 8 | `0x402113..0x402149` | `0x46e950` := `GetTickCount()`; debug prints | the shell clock |
| 9 | `0x40214c` | `0x402342` | frame-rate statistic (8-entry ring `0x481208`), debug text only |
| 10 | `0x402151..0x40217b` | Quit box `[0x48d1dc]` visible → every 51st iteration `0x41f3ed([0x48d1dc])` | repaint the Quit box |
| — | `0x402181` | `0x406328` | Windows pump; WM_QUIT → leave |
| — | `0x40218a` | quit flag `0x46c268` | leave if set |

The quit flag is also tested after steps 1 to 6 (`0x402063`, `0x402076`,
`0x402089`, `0x4020a2`, `0x4020c2`, `0x4020d5`), each leaving to `0x402197`.
There is no frame limiter: the loop spins (H: one present per iteration at
most).

**Windows pump `0x406328` (C).** `PeekMessageA(…, PM_REMOVE)` until the
queue is empty: WM_QUIT (0x12) → return 1; otherwise `TranslateMessage` and
`DispatchMessageA`; empty → return 0. It is the only message loop of the
process (25 call sites: `0x401880`, `0x4018cb`, `0x401995`, `0x401a6d`,
`0x401b26`, `0x401b44`, `0x401c53`, `0x401d32`, `0x401d3f`, `0x401d49`,
`0x401d53`, `0x401d5d`, `0x401d67`, `0x401f8f`, `0x402181`, `0x4022ba`,
`0x406bdd`, `0x41e7a5`, `0x41e7ec`, `0x41ee67`, `0x41ee6e`, `0x42f485`,
`0x42f86f`, `0x42f8e9`, `0x4318c2`).

**Drain helper `0x402266` (C).** Up to 24 rounds of: quit flag set → stop;
pump (WM_QUIT → stop); if focused: steps 1 to 4. Callers use it to make a
show or repaint visible before going on (results path `0x40f642`/`0x40f65b`,
movie player `0x41ec84`/`0x41ecc8`/`0x41edf0`/`0x41efbc`, main menu handlers
`0x43189b`…`0x431d39`). **When focus is lost it drains nothing.**

**Other waits (C).** The movie player `0x41e98c` pumps until focus is back
after a movie (`0x41ee6e..0x41ee99`); the music fades `0x42f894` (up) and
`0x42f7f0` (down) pump in 10 ms steps; `0x4022c5(n)` busy-waits n seconds
**without pumping**. The busy cursor is `0x408d2f(1/0)` (`IDC_WAIT`).

## 9. Loop end and exit

**Loop end `0x402197..0x402265` (C).** Save slot 10 (`0x40e933(10, 0)`,
slot 11 in mode 0, only with a loaded game; [vshell_ui.md](vshell_ui.md)
§1/§9); delete the mouse producer (`0x408f27(obj, 3)`) and the keyboard
device (`0x40943b`); delete the root window (`0x408760`); free the map
tables (`0x4244d6`); `0x40988c` = delete the device objects (`0x40e299`) and
shut the sound down (`0x42f7c2`); free the 4,000,000-byte arena block
(`0x4087a1`); `0x40683b`, `0x40683c`; `PostQuitMessage(0)`; destroy the
local device (`0x41dc68`); close the CD-check stream (`0x4030e4(stream, 2)`).
WinMain then pumps to WM_QUIT, releases DirectDraw and returns the exit
code.

**Exit code `0x46e348` (C).** Word, sign-extended on return. Writers:
`0x408d1a` only (from `-X`, the defaults `0x4079ea` = 0, the reset
`0x401bce` = 0, and the handlers 2/5/0 of
[launcher.md](launcher.md#other-side-of-the-protocol) and
[vshell_ui.md](vshell_ui.md) §6). Process endings:

| Ending | Code |
|---|---|
| normal: quit flag or WM_QUIT → loop end | the handler's 0, 2 or 5 |
| `-eggplant` missing, CD missing, no sound manager (the sound object cannot be allocated; driver failures do not end the start-up) (start-up returns early) | 0 (reset at `0x401bce` happened before) |
| `RegisterClassA` or window creation fails (WinMain) | 0 |
| any assertion of severity 4..6 (arena, `drive.cfg`, `error.str`, map tables, deferred error, …) | 1 (`__exit`) |
| killed (Task Manager) | 1 |

No path returns the `-X` value itself: nothing between the parser and the
reset at `0x401bce` can end the process except an assertion (code 1).

## 10. Present, draw and message sites

| Site | What (C) | Used by |
|---|---|---|
| `0x406bfb(dst point*, src rect*)` | **the only present primitive**: full screen → `IsLost`/`Restore`, `Lock` the primary surface, copy rows from the DIB bits `0x481950` (clipped to 640 × 480), `Unlock`; windowed → `GetClientRect`, `InvalidateRect`, `BeginPaint`, select/realize `0x481954`, `BitBlt` from the memory DC `0x48195c`, restore, `EndPaint` | presentation; the capture proxy (one call = one presented rectangle) |
| `0x41fe2c` ← `0x41fe49` | dirty-rectangle flush of `[0x48118c]` (main loop step 4, `0x402266`, `0x40d193`) | presentation, capture proxy |
| `0x406e87` | whole frame (0,0)–(640,480) → `0x406bfb` (WM_PAINT, WM_SETFOCUS with a movie, 0x812) | presentation, own shell frame |
| `0x406264`, `0x406277` | map rectangle `0x481920` at `0x481940` → `0x406bfb` (map animation `0x401a31`, `0x44514d`) | presentation, [vshell_mission_map.md](vshell_mission_map.md), capture proxy |
| `0x4077ee` | clear to black (full screen: lock and zero rows; windowed: zero the DIB and stretch it over the client) | presentation |
| `0x407b62(i)` / `0x407c1b` | palette load / apply (main loop step 3) | presentation |
| `0x406328` | Windows pump (all 25 sites) | capture proxy journal, own shell loop |
| `0x41e0bf` | input poll (step 1) | input, capture proxy (a pump-keyed journal step) |
| `0x46848c` → `0x469ee4` | event drain (step 2) | widgets, probes |
| `0x404fb8` | window procedure (section 7) | input, own shell, probes |
| `0x405bfc`, `0x46d4bc`, `0x4814a0` | mouse dispatch, key table, key ring | input, capture proxy |
| `0x41e98c`, `0x41e8c0` | movie player and queue (28-byte records at `0x485748`: index, rectangle, palette word `+0x12`, mission-teardown flag `+0x14`, flag `+0x18`) | movie playback |
| `0x402266`, `0x4022c5`, `0x408d2f` | drain helper, busy wait, busy cursor | own shell, capture proxy |
| `0x401aa4`, `0x40180c`, `0x402018`, `0x402197` | start-up, screens, loop, loop end | probe inventory |

## 11. The Alt+Tab crash `0x453a68`

### 11.1 The crash site (C)

`0x453a50` is the bitmap decompressor ([formats/dbm.md](../formats/dbm.md)):
`ebx` = the `GLBitmap`, `0x453a65 mov eax, [ebx+0xe]` (data pointer),
**`0x453a68 mov esi, [eax]`** (first word of the data). It is reached only
from the four draw variants `0x4537e8`, `0x453874` (no caller), `0x453908`,
`0x45399c` when the bitmap's kind word `+0x12` is nonzero. Callers of the
variants: the image panel paint `0x40bd22` and the multi-image widget
`0x40c033` (`0x4537e8`), the shell map (`0x427385`, `0x427486`, `0x427368`,
`0x425d91`), a scaled draw at `0x422580`, and the library at `0x453e3a`.
An access violation at `0x453a68` therefore means a `GLBitmap` whose header
no longer holds a valid data pointer: in the recorded dump the header had
been reused by event allocations ([media_compatibility.md](../reference-game/media_compatibility.md#mission-map-lifetime-guard)).

### 11.2 The window messages draw nothing

WM_SETFOCUS (`0x405455`) and WM_PAINT (`0x4057ab`) call the full repaint
`0x406e87` → `0x406bfb` (section 7), which only copy the existing frame
buffer to the window (section 10) and never call a widget, `0x4537e8` or
`0x453a50` (C). No message branch of `0x404fb8` draws a widget. The decoder
can only be reached from code that runs inside the main loop, a wait loop or
a handler.

### 11.3 How a widget paint reaches the decoder (C)

- Windows start hidden (base constructor sets `+0x11` bit 2 at `0x41f239`)
  and accept event types 1, 2, 4, 8, 0x10 (mask `+0x39` |= 0x1f); the image
  panel constructor `0x40bc48` stores the bitmap argument at `+0x5d` and
  hides the panel (`0x40bc77`).
- Show `0x41f93a` clears bit 2 and **posts** a type-1 and a type-4 event;
  repaint `0x41f3ed` posts a type-4 event for a visible window and recurses
  into its visible children; hide `0x41fabd` sets bit 2 and posts a type-2
  event. Nothing is drawn at these calls.
- The drain `0x469ee4` routes each event to the first window up the parent
  chain whose mask contains the type (`0x46a274`, `0x46a2f8`) and calls its
  handler; **no visibility test**. The image panel's handler `0x40bc8a`
  calls its paint `0x40bd22` for type 4; `0x40bd22` draws `+0x5d` with
  `0x4537e8` whenever `+0x5d` is not null.
- So a paint posted before an image is freed and drained after it draws
  the freed image, even if the panel was hidden in between.

### 11.4 Image panels that keep a freed bitmap (C)

Every store to an image panel's `+0x5d` and every free of a bitmap array
(`0x455230(array, 3)`, 13 sites) was checked:

| Panel | Bitmap from | Freed by | Reference cleared? |
|---|---|---|---|
| Telecomm panel `[0x48d8c4]` (built at `0x442d29..0x442d73` by `0x40bc48` with the click handler `0x4454b8`, child of the map frame `[0x48d8b4]`, rect (10, 20)–(249, 256); not the mission map, [vshell_mission_map.md](vshell_mission_map.md)) | `dba\terradef.dba` `[0x47805c]` element 0, `0x4445c3` (`0x44461a`) | `0x4446d1` ← teardown `0x445095` (`0x44509e`) | **no** (the proxy's `mission_map_lifetime` guard clears it before that call) |
| theatre picture `[0x48d8c0]` (a separate `0x40bc48` widget built at `0x442caa..0x442cf1`, child of the root `[0x481180]`, full screen) | `dba\alph2.dba`, `delt1.dba`, `omic1.dba` or `brav1.dba` by theatre `0x46fc58` − 1, `[0x478064]` element 0, `0x44472f` (`0x44481e`) | `0x44483e`: hide, then free `[0x478064]` | **no** (not guarded) |
| theatre list `[0x48d8f4]` (multi-image widget) | `dba\th_earth.dba`/`th_moon.dba` `[0x478060]` (`0x4446bf`) | `0x4446d1` | yes: `0x40c182([0x48d8f4])` runs first |
| crew panels `[0x48d834+4i]`, `[0x48d810/814/818]` | `[0x4774c8]`, `[0x4774cc]` (crew constructor) | never | — |
| shell map | `[0x471db8]`, `[0x471dbc]` | only at the loop end | — |

The REPAIR/BUILD arrays `[0x46ff28]`, `[0x46ff04+4i]`, `[0x46ff2c+4i]`,
`[0x46ff74]`, `[0x46ff50+4i]`, `[0x46ff78+4i]`, `[0x46ff9c+4i]` are freed by
`0x414bd9`/`0x415a17`/`0x415f66` and drawn by those screens' own code, not
through `+0x5d`; whether a paint can outlive them was not traced (open).

### 11.5 The theatre sequence after a movie (C)

The first opening of the MISSION page while the once-flag `0x477946` is
clear (`0x444a69`; set at `0x444add`, cleared at `0x4383c6` in the saved
games code and at `0x43c7a5` in the new-career code, so again after every
load) queues the campaign movie `c<n>.avi` (0x46 + n) and the theatre
movie (0x4b + n, `alph_th`, `delt_th`, `omic_th`, `brav_th`, `luna`) with the
record flag `+0x14` = 1 (`0x444aad`). After that movie the player
`0x41e98c` does, in this order:

1. `0x41ee3f..0x41ee99`: close the movie, pump until focus is back.
2. `0x41ee9b..0x41eee4`: the bay bar `[0x48d528]` is visible → repaint the
   mission trees `[0x48d8e0]` (page 1), `[0x48d8b4]` (map frame, so the
   visible Telecomm panel), `[0x48d8b8]`, and `0x445343(page)`: type-4 events
   queued.
3. `0x41ef75`: flag `+0x14` → teardown `0x445095`: hide the mission panels
   (type-2 events queued) and free `terradef` (`0x4446d1`).
4. `0x41ef92..0x41efa0`: screen word := −1, `0x42f5d3`, music fade-in
   `0x42f894` (with music on: volume steps every 10 ms up to 100, about
   one second, pumping each round).
5. `0x41efaa`: `0x44472f` loads the theatre picture into `[0x478064]`,
   assigns it to `[0x48d8c0]` and shows the panel (type-1 and type-4
   queued), palette 0xe + theatre (theatre 5 instead queues `dropship.avi`).
6. `0x41efbc`: `0x402266` drains the queue, **if focused**.
7. `0x41efc1..0x41efe6`: the panel is visible → `0x4022c5(2)` (two seconds,
   no pumping), `0x44483e` (hide, free the theatre picture, keep `+0x5d`),
   repaint the bay frame `[0x48d520]`, palette 1.

**Without the guard (the save/load crash).** In step 6 the Telecomm panel's
type-4 event from step 2 is drained after step 3 freed `terradef`; the
panel paints the freed bitmap even with focus. This matches
[media_compatibility.md](../reference-game/media_compatibility.md#mission-map-lifetime-guard)
("entering mission selection", stale Telecomm panel, header reused by event
allocations) and explains why it came right after a load: the load clears
the once-flag, so the next MISSION page plays the theatre movie, and this
path repaints the map frame and then tears the mission screen down before
the next drain (C for the order; H that this is the path of the recorded
dump, which showed the stale panel but not the call stack; the other
teardown caller, the bay's screen change `0x43a5b7`, was not traced for
pending paints).

**With the guard, after an Alt+Tab (H).** If the focus is lost after step 1
and before the first pump of step 6, i.e. while the music fades in or the
theatre picture loads (a window of roughly one to two seconds right after
the theatre movie ends), `0x402266` drains nothing, `0x44483e` frees the
picture, and the bay repaint posts new events into the freed memory. When
the player switches back, WM_SETFOCUS sets `0x46c288` and the next
main-loop drain dispatches the theatre panel's type-4 event: `0x40bd22` →
`0x4537e8` → `0x453a50` reads the overwritten header and faults at
`0x453a68`. The Telecomm panel's queued paint is harmless here because the
guard cleared its reference. This is the only path found in which the
outcome depends on focus and the guard does not help; it is consistent
with the observed crash (on coming back, guard log present, DBSIM never
ran). It is a **hypothesis**: no dump exists, and other deferred paints of
freed images (11.4) are not excluded.

**What a dump must show to confirm it.** A `-CaptureCrash` dump of the next
occurrence: `ebx` at `0x453a68` equal to the old element 0 of `[0x478064]`
(the global is 0 after the free), `[0x48d8c0]+0x5d` equal to `ebx`, the
panel's bit 2 set (hidden), and on the stack the chain `0x46848c` →
`0x469ee4` → `0x46a274` → `0x40bc8a` → `0x40bd22` → `0x4537e8`. If
`[0x48d8c4]+0x5d` is `ebx` instead, the guard missed a path. A proxy guard
for `[0x48d8c0]` analogous to `mission_map_lifetime` (clear `+0x5d` before
`0x44483e` frees `[0x478064]`) would close the candidate in the original
shell; the proxy has none.

**Consequence for the own shell.** It draws from current state, so it has
no deferred paint of a freed image by construction and does not reproduce
the crash. It reproduces the visible sequence: after the theatre movie the
theatre picture for two seconds, then the service bay.

## 12. Requirements on an own shell process

What an own shell must reproduce of the process part (C unless marked):

1. **Arguments.** The ES.EXE argv (`dummy -eggplant -X<n> [-s] [-a] [-@]
   [-<lang>]`), the parser's effects of section 3 (`-a` movies off, `-s`
   sound off, `-k` keyboard off, `-@` cheat, `-f`/`-g` language, `-X`), the
   default language from preference byte +0x2b, the dead options `-m`,
   `-l`, `-r`, `-d` as no-ops.
2. **Start checks in VSHELL's order, with its texts from `error.str`:**
   colour warning (message 3, caption 2; entry modes 0 and 1 only; continue),
   missing `-eggplant` (message 4, caption 0; end with 0), CD check on
   `<CD>\avi\pt1.avi` (message 5, caption 0; end with 0). The colour check
   differs on purpose because the own presenter converts the 8-bit frame
   itself (platform deviation 6 in
   [vshell_presentation.md](vshell_presentation.md#the-own-shells-frame-presenter-and-window)).
3. **Exit codes** of section 9 and the slot-10 save at every loop end with
   a loaded game.
4. **Window behaviour:** a fixed 640 × 480 client, windowed or full screen
   by preference byte 6; Alt+Enter toggles and stores byte 6; Alt+Tab,
   Ctrl+Esc, Alt+Esc leave full screen, store byte 6 = 0 and write
   `prefs.cfg` (or refresh the visible Preferences screen); Alt+F4 opens
   the Quit box; WM_CLOSE sets the quit flag only while the shell is
   interactive; no system menu by keyboard.
5. **Focus:** nothing advances while the window is unfocused (no input,
   events, present or movies); music stops on focus loss and restarts on
   return; a movie in progress waits for focus before continuing.
6. **Main-loop order** per iteration: input, events, palette, present,
   movies, map animation; the quit flag checked after each; the Quit box
   repainted every 51 iterations while open (visible only if it changes).
7. **Waits:** the two-second theatre picture without input, the music
   fades, the busy cursor around movies.

Not needed (C): `BWCC.DLL`, the debug monochrome screen and every
`0x46a834` debug print, the frame-rate statistic `0x402342`, the dead argv
builder `0x42f960` (no caller, never spawns), the joystick capability read
(`0x481d9c` and `0x481f34` are written at the start-up and never read), the
`printf` usage text, the 15 ms timer as such (the sound service it drives
is in [audio.md](audio.md), "The VSHELL shell audio contract").

## 13. The own shell process frame

`shell/include/es2/shell/gui/loop.h`, `shell/src/gui/loop.c`,
`shell/src/gui/window.c` and `shell/src/gui/main.c` (`es2shell.exe`)
implement sections 3, 6 (window), 7 and 8 for the own shell; the drawing
side is in
[vshell_presentation.md](vshell_presentation.md#the-own-shells-frame-presenter-and-window),
the input in
[vshell_widgets.md](vshell_widgets.md#8-input-and-event-queue-of-the-own-shell).

- **Command line** (`es2_gui_parse_args`): defaults of `0x40796c(0)`, then
  the option table of section 3 (case-insensitive except `-X`; the exact
  token `-eggplant`, any other `-e…`/`/eggplant` = language 3; `-r`, `-m`,
  `-l` without effect; `-X` = `(short) atol`, last one wins; entry mode :=
  that value). Language −1 means "preference byte +0x2b": `run_shell`
  reads it from `DATA\PREFS.CFG` (`0x4079b7`) unless `-f`, `-g` or `-e…`
  set it. Own options `--menu DIR`, `--headless`, `--script`, `--report`
  (`{X}` in the two paths = the entry mode) and `--fullscreen` (forces
  full screen over preference byte 6) are removed before VSHELL's parser
  sees the rest. VSHELL splits its command line at single spaces without
  quotes; `es2shell` gets the C runtime's argv, which is the same for
  ES.EXE's arguments (no quotes, no doubled spaces). `es2launch` starts
  `es2shell.exe` through `ES2_LAUNCH_SHELL` with ES.EXE's argv appended
  (`//shell:gui_launch_test`: `dummy -eggplant -X1 -s -G` → entry mode 1,
  German, sound off; exit code 0 ends the launcher loop).
- **Start checks.** The CD check of step 13 is implemented
  (`es2_host_cd_check`: `menu.ini` `cd_root`, policy `cd_check` original or
  skip; without a `cd_root` there is no CD to check). The `-eggplant`
  check (step 12) and the colour-depth check (step 11) are not implemented;
  `-eggplant` is parsed only. An assertion ends the process with exit
  code 1 (`es2_app_finish`).
- **Window**: title `EarthSiege II`, fixed 640 × 480 client, not resizable,
  centred by SDL; full screen as a borderless desktop window; the class
  icon `es2icon` is set from the span store. Alt+F4 does not close it
  (`SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` = 0) and the Alt key opens no system
  menu (SDL's default), as in VSHELL whose key branch never calls
  `DefWindowProcA`. The window closes only when the loop ends.
- **Window procedure** (`es2_gui_on_*`, called by the pump): WM_SETFOCUS
  (full present only while a movie is open and the window is ready; focus
  flag 1; sound-focus hook when no movie), WM_KILLFOCUS (flag 0, hook),
  WM_PAINT (full re-present only with the realization gate set and no
  movie), WM_CLOSE (quit flag only while interactive), the mouse and key
  branches (vshell_widgets.md 3.4 and 7.1); mouse messages reach the
  producer also while unfocused (Windows sends WM_MOUSEMOVE to an inactive
  window), but nothing is posted until focus returns. Alt+Enter toggles
  full screen on key-up; Alt+Tab, Ctrl+Esc, Alt+Esc leave it; both then
  call the shell's system hook (`on_system` in `app.c`), which stores
  preference byte 6 and writes `DATA\PREFS.CFG`; Alt+F4 calls it for the
  Quit box. SDL's end-of-session quit is WM_QUIT (VSHELL: WM_ENDSESSION →
  DestroyWindow → PostQuitMessage).
- **Main loop** (`es2_gui_step`, one pass from the pump): pump (WM_QUIT
  ends), quit flag; unfocused → nothing else; input poll; timer hook
  (`0x46a508`) and drain; palette apply; dirty flush; movie hook; map hook
  (which also repaints the Quit box every 51st pass, step 10:
  `es2_boxes_pass`); the quit flag after each step. Not reproduced: the
  deferred-error assertion (step 7), the debug clock and frame statistic
  (8, 9). The platform waits up to 1 ms for the next SDL event after each
  pass instead of spinning.
- **Exit**: `es2shell.exe` returns the flow's exit code
  (`es2_shell_flow_finish`: 0, 2 or 5) when the loop ends, 1 after an
  assertion, and 0 when the window cannot be created or the CD check fails.

## 14. Open points

1. The Alt+Tab crash is explained by a candidate path (11.5), a
   hypothesis; a dump is needed.
2. Deferred paints of other freed images: the REPAIR/BUILD arrays (11.4)
   and any other "show, `0x402266`, free" sequence are not inventoried
   (every caller of `0x402266` followed by a free).
3. `0x407466`/`0x40749c` (DirectDraw creation and display mode), the
   `BMP_8` surface and the palette tables are named only
   ([vshell_presentation.md](vshell_presentation.md)).
4. The key ring consumer (keyboard device `0x40939c`, slot `+4`), the hot
   keys registered at run time and the mouse listeners are described in
   [vshell_widgets.md](vshell_widgets.md).
5. `0x48117c`, `0x46d168` (0x200 → 0x400) and `[0x48dd0c]` have no
   known meaning.
6. The probes over the shell chain can confirm the order of sections 4, 5
   and 8.
