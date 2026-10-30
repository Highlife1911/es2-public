# sim/: the own DBSIM process

`es2sim.exe` runs our own code for DBSIM without the original binary
executing. The design is [own_process.md](../docs/reference-game/own_process.md).
The host reproduces the original's behaviour under the oracle and adds nothing.

## Usage

```text
es2sim --headless [--config <ini>] [--tick-log <path>]
       [--arguments "<DBSIM arguments>"] [--frames <path>]
```

Start-up: the process claims `0x00400000..0x006d7fff` before the CRT (as the
in-vitro harness does) and builds the guest image from the own tables (layout
and import table, the runs of initialised DATA and the seven CODE tables in
`sim/data/dbsim_data.c`), written at their original VAs without relocations
([own_process.md](../docs/reference-game/own_process.md), "es2sim without
DBSIM.EXE"). No `DBSIM.EXE` is read and `ES2_DATA_ROOT` is not consulted.
CODE is never executable, read-only on the seven table pages and
no-access elsewhere (a stray read is a named FAULT); the 134 import slots hold
refusal traps.

It then sets up the own heap, virtual time, the dispatch (the registry's
adapters plus the service rows of `dispatch/services.h`) and the stage-1 trap
on the simulation thread (x87 control word `0x1332`, DEP on); builds CW3220's
attach blocks on the heap; runs the c0w32 entry and the init records, WinMain's
start-up, the mission load with the input, audio, HUD and cockpit construction,
and the main loop with one tick record per frame. A guest call without own code
stops the run with a refusal (exit 70).

Without `--headless` (the `es2sim_window` build) the same code runs in an SDL3
window with sound, CD music and joystick.

Exit codes of its own: 64 usage, 65 image refused, 66 range not claimable, 67
set-up failed, 70 a refused call, 71 an unhandled fault in es2sim's own code;
otherwise the guest's: the dword `0x4d27b8`, 1 after the fatal report's
`__exit`, 3 after `_abort`, or the exception code (`0xc0000094`, `0xc0000095`)
where the original faults
([own_process.md](../docs/reference-game/own_process.md#the-originals-fault-paths)).

The gap roots without a row: [sim_gap.md](../docs/reference-game/sim_gap.md); the known rows of the
frame compare: `sim/tests/frames_known.py`.

## Layout

| Path | Seam |
|---|---|
| `include/es2/sim/image.h`, `src/image.c` | the image layout, imports and resources from the own tables |
| `include/es2/sim/image_data.h`, `src/image_data.c`, `data/dbsim_data.c` | the runs (initialised DATA, CODE tables, RTTI counts) and their writer; `es2_sim_dbsim_rtti_count` |
| `include/es2/sim/image_file.h`, `src/image_file.c` | testonly (`//sim:image_file`): hash gate, PE parse and placement of the user's `DBSIM.EXE`, for the tests that compare the own tables with the file |
| `include/es2/sim/memory.h`, `src/memory.c` | guest views: identity in the executable, an arena of host buffers in tests |
| `include/es2/sim/dispatch.h`, `src/dispatch.c` | entries (hook, service), sealing, `es2_sim_call`, refusals |
| `include/es2/sim/trap.h`, `src/dispatch_trap.c`, `src/dispatch_trap.S` | stage 1: the vectored handler and the trampoline |
| `include/es2/sim/heap.h`, `src/heap.c`, `src/heap_win32.c` | the CW3220 heap model as the allocator, preferred base; CW3220's attach blocks and its own `_malloc`/`_free` on the same heap, reservations not adjoining |
| `dispatch/services.h`, `dispatch/services/*.h`, `include/es2/sim/services.h`, `src/services_rtl.c` | service rows (one fragment file per topic, included in file-name order by the generated `services_rows.h`) and the host services |
| `include/es2/sim/residue.h`, `src/residue.c` | the residue table |
| `include/es2/sim/adapters.h`, `src/adapters.c` | the proxy's adapters compiled in, one hook row per registry line |
| `include/es2/sim/host.h`, `src/host.c`, `src/main.c` | mapping, set-up, log, `es2sim.exe` |
| `oracle/` | pure C17 shared with the proxy: virtual time, the tick-record lines, the input journal, the after-load snapshot, the memory watches |
| `include/es2/sim/loop.h`, `src/loop.c`, `src/frame_buffer.c` | the main loop, pump, window procedure, limiter and clock, exit; the DIB section's objects |
| `src/platform_sdl.c`, `src/audio_sdl.c`, `src/joystick_sdl.c` | the SDL3 platform of `es2sim_window`: window messages, key states and present; the SOS mixing device sink; the opt-in joystick |
| `src/cpu_fault.c`, `src/image_access.c`, `src/perf.c` | the CPU divide fault on the original's operands; the image-access measurement; the frame-time recorder |
| the other `src/` files | the service rows of one area each (start-up, files, input, load, audio, HUD, dialogs, drawing, presentation, cockpit); see the sections below |
| `tests/` | unit tests on synthetic images; the real-data tests skip without `ES2_DATA_ROOT` |

## Adding to it

- A **service**: add a row to the area's fragment `dispatch/services/<topic>.h`
  (a new area adds a new file; the order of rows does not matter, the table is
  sorted when sealed) and define the function
  (`void f(es2_sim_service_call *, void *user)`) in the area's `src/` file;
  state the registers it leaves and the listing range they were read from.
  `//sim:table_test` refuses a VA that is also a hook and checks every VA
  against CODE and its entry evidence.
- **New code** reads and writes guest state only through `es2_guest_memory`
  (own_process.md 2.4), never by casting an address.
- Test executable names avoid `update`, `patch`, `setup`, `install`
  (Windows' installer heuristic, error 740): hence `table_test`, not
  `dispatch_test`.

## Tests

- Unit tests (`//sim:*_test`) run on synthetic images and recording services:
  they prove the own code against the model of the contract, not against the
  original.
- Each service area is also compared with the original's machine code in
  [`tools/reference-game/invitro`](../tools/reference-game/invitro/README.md)
  (`differential_<stem>_test`, named per area below).
- The replay tests and the frame compare below prove the whole process against
  the oracle goldens.

### The replay test

`replay_tests.bzl` and `tests/replay_suite.py` run one oracle suite through
`es2sim.exe --headless` and compare it with its golden (`//sim:replay_<suite>_test`,
manual, one per registered suite; `//sim:replay_all`; the driver's own test is
`//sim:replay_suite_test`). The driver hands the host `--config`
(`es2-replay.ini`: the proxy's `[trace]`, `[time]`, `[input]`, `[arming]` plus
`[game]` and `[run]`, with `frame_limit`) and `--tick-log` in a prepared
install directory; the host's part is the journal, the watches, stopping at
`frame_limit`, the `exit` record and the return files in `DATA\`.
`[trace] heap_trace=<path>` writes the heap trace that the proxy's `[rtl] heap_trace`
writes for the original; `tools/reference-game/scripts/heap_trace_compare.py`
compares the two. How to run it and read its result lines:
[oracle_regression.md](../docs/reference-game/oracle_regression.md) "The
own-process replay". The python files sit in `tests/` but the targets in this
package (a `sim/tests/BUILD.bazel` would cut `//sim`'s test sources off).

### The frame compare and frame times

`frames_tests.bzl` and `tests/frames_suite.py`: `//sim:frames_<suite>_test`
(manual, `//sim:frames_all`) runs a suite through `replay_suite.py --frames`,
compares the capture with the reference manifest and checks every kind of
difference against `tests/frames_known.py`. `tests/perf_suite.py` summarises
and measures `es2sim`'s frame times. Both are described in
[own_process.md](../docs/reference-game/own_process.md).

## The static initialisation

`include/es2/sim/init.h`, `src/init.c`: the c0w32 entry `0x401000`, the 65
init records (CW3220's selector order) and the 26 exit records as own
functions keyed by the record VAs of the user's file; their calls to other code
go through `es2_sim_call`. `src/services_platform.c` adds three service rows:
`CreateFileA` (-1 headless), `CloseHandle` (refused), `GetModuleHandleA`
(`0x400000` for NULL). The VxD open is logged as a `platform` event. Tests:
`//sim:init_test` (synthetic image, recording services; the reference file's
record tables with `ES2_DATA_ROOT`) and
`//tools/reference-game/invitro:differential_init_records_test`. Contract:
[own_process.md "The static initialisation as own code"](../docs/reference-game/own_process.md#the-static-initialisation-as-own-code).

## The start-up and the file services

`include/es2/sim/startup.h`, `src/startup.c`, `src/volumes.c`: CW3220's call
of WinMain and DBSIM's start-up up to the mission load (WinMain, the
command-line split, both option parsers, `drive.cfg`, the VOL registration
with its read and buffered streams, error.str, the `-eggplant` and memory
checks, the fatal report with `assert.log`, WinMain's tail). The original's
stack locals live in a frame area the host provides; every call of other
DBSIM code goes through the dispatch.
`include/es2/sim/files.h`, `src/services_files.c`: CW3220's file imports as
service rows over the CW3220 file model (its variables in a region of their
own, its blocks on the game's heap, as in the original:
[own_process.md](../docs/reference-game/own_process.md#the-heap-trace))
and the path helper `0x45ee00`.
`es2sim --headless` takes DBSIM's command line from `--arguments`, `[game]
command_line` or `-eggplant -X2 -R0 -G` plus `[game] arguments`, and runs in
the installation directory (the working directory). Tests:
`//sim:startup_test` (synthetic image and installations),
`//sim:es2sim_real_test` (the start-up's exit codes on the built-in image) and
`//tools/reference-game/invitro:differential_startup_test`. Contract:
[launcher.md "DBSIM's start-up as own code"](../docs/reverse-engineering/launcher.md#dbsims-start-up-as-own-code),
[own_process.md "Start-up, parsers and file services"](../docs/reference-game/own_process.md#start-up-parsers-and-file-services).

## The main loop, the pump, the exit and the DIB section's objects

`include/es2/sim/loop.h`, `src/loop.c`, `src/frame_buffer.c` (in
`:services`): the game main after the mission load (the loop with
`stage_dispatch`, the limiter, the pause path, the frame counter and its
tick record, the loop tail with the mission end), the limiter, clock and
timing init under virtual time, the pump with the journal as its message
source, the window procedure with its handler list, pause and resume,
CW3220's atexit and exit, and the DIB section `0x464d44` with the graphics
library's surface objects (the bits a host region). The start-up calls the DIB
section and, after the load, the loop through
`es2_sim_startup.dib_section`/`.after_load`/`.quit_pump`.
`oracle/journal.{h,c}` is the input journal, shared with the proxy's
`input_journal.h`. `es2sim --headless` reads `[input]` and `[run] frame_limit`,
writes one tick record per frame and exits through `es2_sim_exit`. Tests:
`//sim:loop_test`, `//sim/oracle:oracle_test`,
`//tools/reference-game/invitro:differential_loop_test`. Contract:
[main_loop.md "The main loop, pump, window procedure, limiter and clock as own code"](../docs/reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code),
[own_process.md "The main loop, the journal and the exit"](../docs/reference-game/own_process.md#the-main-loop-the-journal-and-the-exit).

## The input layer

`include/es2/sim/input.h`, `src/input.c` (in `:services`): the input set-up
`0x45a5dc` that the mission-load root calls first (keyboard and mouse
devices, hot keys, the flight-key capture, the prefs read with its
callbacks, the voice-archive test and the joystick device, keyjoy.cfg
through the hook), the device poll `0x45baa8` and its readers, the joystick
poll and summary, the mouse window handler with its event and queue
callback, the mouse queue set-up, bounds and key flush, and the read and
write streams as rows for the prefs adapters. The provider seam
`es2_sim_input_platform` answers `GetKeyState`, the joystick, the cursor and
the client size: headless from the journal (`es2_sim_input_headless`), windowed
from SDL3. Tests: `//sim:input_test` and
`//tools/reference-game/invitro:differential_input_layer_test`. Contract:
[input.md "The input layer as own code"](../docs/reverse-engineering/input.md#the-input-layer-as-own-code),
[own_process.md "The input layer"](../docs/reference-game/own_process.md#the-input-layer).

## The mission load

`include/es2/sim/load.h`, `src/load.c` (in `:services`): the guest
functions the mission-load root `0x461608` reaches, as own code in its call
order (graphics context, palette and loading screen, arena, sound init up
to the SOS driver load, renderer reset and the TS/AN classes, shape
loaders, stage-2 callbacks, the world and actor load callees, the memory
stream, trivial bodies). The display seam `es2_sim_load_platform` holds the
headless palette answers, the colour table and the present count.
`oracle/snapshot.{h,c}`: the ES2SNAP1 v2 writer; `es2sim` writes the after-load
snapshot before the first logic frame with `[snapshot] after_load=1`.

The last callees of the load root are in `src/load.c` too: the cockpit step
`0x433f7c`, the cursor sprite store `0x465618` and the mono output `0x4955c4`
(refused when a DARKMONO.VXD handle is present), and the terrain shading pass
`0x46c2e0` with the cell slopes `0x46bfdc` and the cell shade `0x46c3f0`. The
shading pass calls the hook `normalize`, the object select and the terrain
lighting of the view area through the dispatch. The palette set's GDI count is
NUMCOLORS (`num_colors`).

Tests: `//sim:load_test`, `//sim/oracle:snapshot_test`,
`//tools/reference-game/invitro:differential_load_rest_test` (`--load-rest`).
Contract:
[mission_load.md section 33](../docs/reverse-engineering/mission_load.md#33-the-mission-load-in-the-own-process),
[mission_load.md section 34](../docs/reverse-engineering/mission_load.md),
[own_process.md "The mission load"](../docs/reference-game/own_process.md#the-mission-load).

## Audio

`include/es2/sim/audio.h`, `src/audio.c` (in `:services`): the SOS driver
load and init (no DLL: the device seam `es2_sim_audio_device`, headless the
silent device `es2_sim_audio_silent`), the sound table, the SFX manager with
its glue below the sfx hooks, the voice start and the lip pass, CD audio over
MCI (headless: no CD audio), the window handler, pause and resume, the prefs
sound groups and the close. Tests: `//sim:audio_test` and
`//tools/reference-game/invitro:differential_audio_test`. Contract:
[audio.md "Audio in the own process"](../docs/reverse-engineering/audio.md#audio-in-the-own-process),
[own_process.md "Audio"](../docs/reference-game/own_process.md#audio).

## The HUD line

`include/es2/sim/hud.h`, `src/hud_load.c`, `src/hud_cockpit.c`,
`src/hud_gauges.c`, `src/hud_display.c`, `src/hud_draw.c`,
`src/hud_state.c` (private helpers `src/hud_internal.h`; in `:services`):
the HUD and cockpit construction of the mission load, the display modes,
the panel elements' full draws and the per-frame HUD state, headless (no
pixels, every other write kept), with the coupling calls (clock, stream B,
sound API) made through `hud_clock`/`hud_rng_*`/`hud_sound` and recorded
in `es2_sim_hud`'s trace. Test: `//sim:hud_test` (synthetic image, against the
coupling table of the HUD document). Contract:
[hud.md "The HUD line in the own process"](../docs/reverse-engineering/hud.md#the-hud-line-in-the-own-process),
[own_process.md "The HUD line"](../docs/reference-game/own_process.md#the-hud-line).

Further HUD files; `//sim:hud_test` covers them all:

| File | Functions | In-vitro test | Contract |
|---|---|---|---|
| `src/hud_frame.c` | the per-frame row-1 walk (`0x432924`, `0x43408c`, `0x434194`, `0x4349ac`) and its leaves; the HDD map's tail, the radar draw and the load root's detail rows (in `hud_draw.c`, `hud_load.c`) | none of its own | [hud.md "The map draw's tail, the load root's last steps, the radar draw and the per-frame row-1 walk"](../docs/reverse-engineering/hud.md#the-map-draws-tail-the-load-roots-last-steps-the-radar-draw-and-the-per-frame-row-1-walk) |
| `src/hud_widgets.c` | the HUD message queue (insert `0x434e8c`, head, entry, remove, drop, expire, remove by key), the port step `0x435610`, pick, cancel and done, the DialogPort receive, hide and frame, the EventPort receive and frame, the port redraw `0x437164`, the gauge blink `0x438bc0` with `0x438d6c` | `differential_hud_widgets_test` (`--hud-widgets`) | [hud.md "The message queue, the ports and the gauge blink"](../docs/reverse-engineering/hud.md#the-message-queue-the-ports-and-the-gauge-blink) |
| `src/gadgets.c` (`gadgets_internal.h`) | the fixed and locking gunsights `0x43b808`/`0x43b978`, the roving gunsight `0x43c204` with the cursor shape `0x433d90` and the mouse position `0x486ece`, the fixed rotation gauge `0x43b4cc`, the reverse gauge's step `0x43b67c` and redraw `0x43b7cc`, the nav gauge `0x43c40c` with its marker point `0x43495c`; the draw rows in `hud_draw.c` delegate to it | `differential_gadgets_test` (`--gadgets`) | [hud.md "The gunsights and gauges as own code"](../docs/reverse-engineering/hud.md#the-gunsights-and-gauges-as-own-code) |
| `src/mfd.c` | the MFD frame `0x446350` with its page dispatch, damage alert, power-up animation and alert page; the helpers `0x4342e0`, `0x446ddc`, `0x44718c`, `0x434310`, `0x440a3c`, `0x440a64`; the animation start, step and state `0x471e10`/`0x471e88`/`0x471f24` and `0x45caa4`; the status page frame `0x43b238`, target `0x440314` and orders page (`0x43f7cc`, `0x43f8a0`, `0x43fa5c`); the HDD frame `0x449bf8` with `0x449f64`, `0x44a704`, `0x44a174`, the map scroll `0x44cff8`/`0x44d05c` and `0x450b54`/`0x450b74`; the status page draw `0x43a5a0`, the target page draw `0x43fe44`, the HDD status page body `0x450c7c`, the radio record draw, frame, line and talk `0x44aea0`/`0x44b3dc`/`0x44b148`/`0x44b344`, the MFD entry steps and order key `0x44703c`/`0x447070`/`0x447158` | `differential_mfd_test` (`--mfd`), `differential_mfd_rest2_test` (`--mfd-rest2`) | [hud.md "The MFD and HDD frames and pages as own code"](../docs/reverse-engineering/hud.md#the-mfd-and-hdd-frames-and-pages-as-own-code), [hud.md "The MFD status and target draws"](../docs/reverse-engineering/hud.md#the-mfd-status-and-target-draws-the-hdd-status-body-the-radio-records-and-the-entry-steps) |
| `src/panel.c` | the panel scroll zones `0x433770`, the radar page draw `0x43eef4`, the HUD radar `0x43f2d8`, the radar update `0x43eedc`; the rotated bitmap `0x45caa4` and the line `0x483a04` are called through the dispatch | `differential_panel_test` (`--panel`) | [hud.md "The scroll zones and the radar page"](../docs/reverse-engineering/hud.md#the-scroll-zones-and-the-radar-page) |
| `src/widget_leaves.c` | the panel rows `0x434520`, the three-shape destructor `0x438e70`, `0x4390b8`, the mission clock `0x4520c8`/`0x4520d8`/`0x45216c`/`0x452240`, the layout scale `0x45229c`, the hit test `0x4523b0`, the rectangle place `0x452454`, the widget, slider and root constructors `0x4524a0`/`0x4524d0`/`0x4526ec`, the slider clamps `0x452520`/`0x452600`, the root's press, release, move, find, draw, redraw and add (`0x4527c8`..`0x452c6c`); the only coupling site is the clock at `0x45213d` | `differential_widget_leaves_test` (`--widget-leaves`) | [hud.md "The live widget leaves"](../docs/reverse-engineering/hud.md#the-live-widget-leaves) |
| `src/hud_gadgets.c` | the panel gadgets' method-table bodies: the range gauge's items `0x4420a4` and key `0x4421c8`, the redraw passes `0x44244c`/`0x4428fc`/`0x442c10`/`0x442dbc`, the change bodies `0x442480`/`0x442948`/`0x442df0`, the twelve event-base entries `0x4433c7`..`0x443435`; no coupling sites | `differential_panel_gadgets_test` | [hud.md "Gauge and panel gadget constructors"](../docs/reverse-engineering/hud.md#gauge-and-panel-gadget-constructors) |
| `src/hud_radio.c` | the radio port's start and end callbacks `0x44b514`/`0x44b5e8` with the voice start `0x44aff0`; the radio record helpers `0x44ae1c`/`0x44b864`/`0x44b920`/`0x44b928`; the HDD legend `0x44ba54` and the HDD button slots `0x44be18`/`0x44be78`; the MFD select click `0x447658`; the throttle slider slots `0x4481c4`/`0x448738`/`0x448b58`; the only coupling site is the clock at `0x44b559` | `differential_hud_radio_test` | [hud.md "The radio panel and the HDD leftovers"](../docs/reverse-engineering/hud.md#the-radio-panel-and-the-hdd-leftovers) |
| `src/cockpit.c` | the cockpit jolt and bump: the hit entries `0x434010` and `0x434144`, the jolt start `0x42d2f8`, the jolt step `0x42d4a8`, the bump reset `0x42d820`; the mission-start drop sequence `0x45d85c` (begin, descent pass, landing and release pieces); the cockpit panel constructor `0x431bf8` is a row of its own in `hud_cockpit.c`; the jolt end (`hud_display.c`) calls the strip restore `0x488143` | `differential_cockpit_test` (`--cockpit`), `differential_cockpit_rest_test` (`--cockpit-rest`) | [display_modes.md "The jolt and the bump as own code"](../docs/reverse-engineering/display_modes.md#the-jolt-and-the-bump-as-own-code), [cockpit.md "Drop sequence 45d85c and panel constructor 431bf8"](../docs/reverse-engineering/cockpit.md#drop-sequence-45d85c-and-panel-constructor-431bf8) |

## The view transform, the projection and the lights

`include/es2/sim/view.h`, `src/view.c` (in `:services`): the R3D per-frame
view set-up `0x48c2e4`, the view and object selection, the projection
leaves, the saved view, the matrix inverse, the polygon and terrain
lighting, the active light list, the light objects (spot and point) and
the light table with its per-object update; exact headless (the module
writes no pixels). The MATH hooks and the load rows are called through the
dispatch. Tests: `//sim:view_test` (synthetic image, stub hooks) and
`//tools/reference-game/invitro:differential_view_test` (`view_cases.h`).
Contract:
[rasterizer.md "View transform, projection and lights"](../docs/reverse-engineering/rasterizer.md#view-transform-projection-and-lights),
[own_process.md "The view transform, the projection and the lights"](../docs/reference-game/own_process.md#the-view-transform-the-projection-and-the-lights).

## The modal dialogs

`include/es2/sim/dialogs.h`, `src/dialogs.c` (in `:services`; the helpers
of `src/hud_internal.h`): the modal dialogs headless, without a window:
the mission-end alert and the quit confirmation (`0x456000`, `0x457b00`),
the options dialog (`0x456d68`), the open and close with the ports' pause
and resume, the step with its clock sites, the key handler, the focus, the
presses and the release list, the selects, the prefs toggles and cycles. A
modal pass's pump is the main loop's row, so the journal's pump-keyed entries
answer the dialog. `es2_sim_dialogs` counts the passes and refuses a dialog
past `pass_limit`. Tests: `//sim:dialogs_test`,
`//tools/reference-game/invitro:differential_dialogs_test`. Contract:
[main_loop.md "The modal dialogs"](../docs/reverse-engineering/main_loop.md#the-modal-dialogs),
[main_loop.md "The modal dialogs in the own process"](../docs/reverse-engineering/main_loop.md#the-modal-dialogs-in-the-own-process),
[own_process.md "The modal dialogs"](../docs/reference-game/own_process.md#the-modal-dialogs).

## Graphics, rasterizer and presentation

| File | Functions | Tests | Contract |
|---|---|---|---|
| `src/gfx.c` (`gfx.h`) | the clip helpers (`0x48699c`, `0x4869fc`, `0x486c00`, `0x486d15`), rect, span and point fills, the ellipse `0x48817c`, the bitmap draw and grab chain (`0x4887d8`, `0x4816a8`, `0x488974`, `0x4827bb`), save/size/restore, scroll `0x487e60`, the text functions (`0x482498..0x482634`), the BMP_8 driver slots 0-6, 19, 20, 26-28 and the scan converter (`0x4930c1`, `0x493141`, `0x493192`) | `//sim:gfx_test`; `differential_gfx_test` (`--gfx`): the clip, fill and bitmap parts are enabled, the text and scan parts are written but not enabled (see `gfx_cases.h`) | [rasterizer.md "Graphics library primitives"](../docs/reverse-engineering/rasterizer.md#graphics-library-primitives) |
| `src/present.c` (`present.h`) | the frame sequence `0x45fba4`, the palette cycler `0x4306ac` (the frame's one clock call), the fade set-up `0x4308fc` and step `0x430b34`, the palette upload `0x465cc4` and the present `0x464a14` -> `0x465628` (GDI branch) with the headless frame capture (`es2sim --headless ... --frames <path>`); `0x464a14` stays the main loop's row | `//sim:present_test`; `differential_present_test` (`--present`) | [presentation.md "The presentation frame in the own process"](../docs/reverse-engineering/presentation.md#the-presentation-frame-in-the-own-process) |
| `src/poly.c` (`poly.h`) | the affine fill family: haze `0x4680e0`/`0x4680f0`/`0x468158`; the 3D fill `0x46817c` with its near clip and the 2D fill `0x468414`; the screen cuts and clippers `0x469894..0x469c64`; the page set-up `0x469dc8`/`0x469ddc`; the span loops `0x46ac14`/`0x46ad4c`/`0x46aeb1`; the edge tables `0x477d48`; the run clippers `0x48b524`/`0x48b5af`/`0x48b63a` and the depth runs `0x48b854`; the fixed-point helpers `0x47e068..0x47e08d` and `0x471cd8..0x471d61`; the perspective fill `0x468760` (one divide per `1 << s` pixels); the shade lookup `0x430e34` (computes the q8 product of `0x47e0a0` inline). `0x469dc8` and `0x471d08` have no static reference and get no row | `//sim:poly_test`; `differential_poly_test` (`--poly`), `differential_poly_rest_test` (`--poly-rest`) | [rasterizer.md "Software polygon fills"](../docs/reverse-engineering/rasterizer.md#software-polygon-fills) |
| `src/fill.c` (`fill.h`) | the 2D polygon fill `0x483eb8` with its reversed and orientation entries `0x484222`/`0x4842bb`, the line `0x483a04` and the BMP_8 line writer (slot 34, `0x48a3b0`); the clipper `0x484338`, the span builder `0x484720` and the line's clip and span helpers `0x483c6c`/`0x483d6c` are called directly; the Gouraud fill `0x487acc` and slot 33 go through the dispatch | `differential_gl_fill_test` (`--gl-fill`) | [rasterizer.md "GL fill layer and line"](../docs/reverse-engineering/rasterizer.md#gl-fill-layer-and-line) |
| `src/gouraud.c` (`gouraud.h`) | the Gouraud fill `0x487acc` (fill kind 1, a fan of triangles), its triangle `0x486ee8` (bands of one ramp colour filled as quads through `0x483eb8`) and the terrain's flat fill `0x46bc44`; the projection `0x48cce4` and `0x42e8e8` go through the dispatch | `//sim:gouraud_test`; `differential_gl_gouraud_test` (`--gl-gouraud`) | [rasterizer.md "Gouraud and flat fill"](../docs/reverse-engineering/rasterizer.md#gouraud-and-flat-fill) |
| `src/scene.c` (`scene.h`) | the sky and horizon entry `0x42e700` with the line, the bands and the ground (`0x42ebe8..0x42f0b0`), the terrain cells pass `0x428140`, the jolt palette (`0x42ea44..0x42ea94`), the binning `0x42841c` with the cell table, the cell callback `0x428c60`, the distance cull, the sort tree (`0x429460..0x429620`), the per-object draw `0x42876c`, the scene finish `0x42883c` and the pick save/restore; the fills, the terrain draw, the comparators and the shape lighting are called through the dispatch | `//sim:scene_test`; `differential_scene_test` (`--scene`) | [horizon.md "The sky and the horizon as own code"](../docs/reverse-engineering/horizon.md), [terrain.md "Binning, the cell callback and the sort tree as own code"](../docs/reverse-engineering/terrain.md#binning-the-cell-callback-and-the-sort-tree-as-own-code) |
| `src/terrain_draw.c` (`terrain_draw.h`) | the terrain set-up `0x46cb9c`, the walk `0x46d1a8` with the row and column walkers (`0x471f44`, `0x472504` and their helpers `0x471f30`, `0x4724b8`, `0x4724e0`), the line callbacks `0x46dddc`/`0x46dfa8`, the cell draw `0x46d448` with the texture selection, the cell object event `0x46e5a4`, the height cache lookup `0x49534c`, the mark read `0x470d94` and the facing test `0x46be9c`; the fills, the scene's `0x428bc0` and the frustum library are called through the dispatch; the shading pass `0x46c2e0` is the row `terrain_shading` in `src/load.c` | `differential_terrain_draw_test` (`--terrain_draw`), synthetic grids | [terrain.md "The terrain renderer as own code"](../docs/reverse-engineering/terrain.md#the-terrain-renderer-as-own-code) |
| `src/frustum.c` (`frustum.h`) | the terrain draw's view frustum library | `differential_frustum_test` | [terrain.md](../docs/reverse-engineering/terrain.md) |
| `src/shape_draw.c` (`shape_draw.h`) | the shape instance slot `0x478da4` with its matrix push and pop, the shape draws `0x490ab8`/`0x490c1c`, the detail test `0x476248`, the part transforms and saved views, the part kinds (list, polygon group, cell animation, detail, BSP), the flat, lit and Gouraud polygons with back-face test, projection, near-plane clips and fill dispatch, the actor draws; the dword projection `0x48ca70` with its UV clip `0x48d0f8`, the polygons `0x475cd0`/`0x475d8c`/`0x475eec` (plain, lit, Gouraud through the thunk), the textured polygon `0x474fa8`, the BSP walk `0x475b2c`, the BSP group `0x475c04`, the sprite part `0x4763f4`, the segment clip `0x40bb4c`; the view-space projection `0x48cce4` and the shade ranges `0x42e8e8` with the level index `0x42e878`, the default ranges `0x42ea0c` and the range writers `0x430518`/`0x430590` (`0x486e10` inlined; the flag path of `0x42e8e8` keeps an original bug whose words depend on the stack address); the beam draw `0x40bc14`, the actor draw slots `0x4174c8` (mech), `0x4215cc` (flyer), `0x408e6c` (debris), `0x409cd0` (meteor), the configuration part `0x40316c` with its offset slot `0x40ded8`, the explosion binning callees `0x408228`/`0x408240`. The mech's part callee `0x4030d0` is the registry hook `part_shapes_bind` (core's `es2_part_shapes_bind_registers`). Exact headless (nothing here writes pixels); hooks, view rows, the fill layer, the haze table, the shade lookup and every part's virtual slot are called through the dispatch | `//sim:shape_draw_test`; `differential_shape_draw_test` (`shape_draw_cases.h`, including the textures part), `differential_shade_ranges_test` (`--shade-ranges`), `differential_actor_draw_test` | [rasterizer.md "DTS shape draw"](../docs/reverse-engineering/rasterizer.md#dts-shape-draw), [rasterizer.md "View-space projection and shade ranges"](../docs/reverse-engineering/rasterizer.md#view-space-projection-and-shade-ranges), [rasterizer.md "Actor draw slots and explosion binning"](../docs/reverse-engineering/rasterizer.md#actor-draw-slots-and-explosion-binning), [own_process.md "The DTS shape draw slots"](../docs/reference-game/own_process.md#the-dts-shape-draw-slots) |
| `src/display.c` | the display transitions `0x42a4c4` (leave) and `0x42a9c0` (enter, with the slides), their small callees (`0x42d9e8`, `0x434604`, `0x434634`, `0x434928`, `0x434944`), the retrace wait `0x45c638` (a platform deviation: no port I/O) and the shield palette `0x443918`; the camera read `0x45edf8` stays the hook `camera_current` | `differential_display_test` (`--display`) | [display_modes.md "The display transitions and the shield palette as own code"](../docs/reverse-engineering/display_modes.md#the-display-transitions-and-the-shield-palette-as-own-code) |
