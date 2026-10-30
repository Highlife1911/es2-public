# sim/: the own DBSIM process

`es2sim.exe` runs our own code for DBSIM without the original binary
executing. The design is [own_process.md](../docs/reference-game/own_process.md).
The host reproduces the original's behaviour under the oracle and adds nothing.

## Usage

```text
es2sim --headless [--config <ini>] [--tick-log <path>] [--environment <file>]
       [--arguments "<DBSIM arguments>"] [--frames <path>]
       [--dump-frames <list> --dump-dir <dir>] [--image-access <path>]
```

- DBSIM's command line: `--arguments`, else `[game] command_line`, else
  `-eggplant -X2 -R0 -G`, followed by `[game] arguments`. The working directory
  is the installation. `[run] time=<seconds since 1970>` fixes CW3220's `_time`.
- `--environment <file>` replaces the process environment for everything es2sim
  reads from it (the guest's environment copy, `ES2SIM_*` switches): an
  environment block as `GetEnvironmentStringsA` returns it (NUL-terminated
  `NAME=value` strings, then one NUL), at most 1 MiB; anything else is a usage
  error. Files, clocks, heap reservations, INI files and the process end go
  through `//platform:os` (`platform/include/es2/platform/os.h`).
- `--frames` writes the headless frame capture (`present.h`); its closing stats
  line is written at the normal end, not on `ExitProcess` paths. `--dump-frames`
  with `--dump-dir` writes ES2F dumps of the listed frames. `--image-access`
  writes the image-access map (VA ranges and counts) at the end of the run;
  `ES2SIM_PERF=1` or `=<file>` writes per-frame wall time and trap counts.
  `--image-access` and `ES2SIM_PERF` change no replay result.

Start-up: the process claims `0x00400000..0x006d7fff` before the CRT (as the
in-vitro harness does) and builds the guest image from the own tables
(`sim/data/dbsim_data.c`) at their original VAs without relocations
([own_process.md](../docs/reference-game/own_process.md), "es2sim without
DBSIM.EXE"). No `DBSIM.EXE` is read and `ES2_DATA_ROOT` is not consulted.
CODE is never executable, read-only on the seven table pages and no-access
elsewhere (a stray read is a named FAULT); the 134 import slots hold refusal
traps. A guest call without own code stops the run with a refusal (exit 70).

Without `--headless` (the `es2sim_window` build, and `es2sim_window_wasm` on the
web) the same code runs in a window with sound, CD music and joystick.
Windowed `[input] mode=replay` replays the journal as the message source,
drops the window's events and does not pace virtual time.

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
| `include/es2/sim/image_data.h`, `src/image_data.c`, `data/dbsim_data.c` | the runs (initialised DATA, CODE tables, RTTI counts) and their writer |
| `include/es2/sim/image_file.h`, `src/image_file.c` | testonly (`//sim:image_file`): hash gate, PE parse and placement of the user's `DBSIM.EXE`, for the tests that compare the own tables with the file |
| `include/es2/sim/memory.h`, `src/memory.c` | guest views: identity in the executable, an arena of host buffers in tests |
| `include/es2/sim/dispatch.h`, `src/dispatch.c` | rows (hook, service, entry), sealing, `es2_sim_call`, refusals |
| `include/es2/sim/machine_sim.h`, `src/machine_sim.c` | the sim backend of `es2_machine_call`: the guest stack, the FS:0 head, the guest-entry depth |
| `include/es2/sim/trap.h`, `src/dispatch_trap.c` | stage 1: the vectored handler, a guard that refuses an unconverted call (exit 70) |
| `include/es2/sim/heap.h`, `src/heap.c`, `src/heap_os.c` | the CW3220 heap model as the allocator, preferred base; CW3220's attach blocks and its own `_malloc`/`_free` on the same heap, reservations not adjoining |
| `dispatch/services.h`, `dispatch/services/*.h`, `include/es2/sim/services.h`, `src/services_rtl.c` | service rows (one fragment file per topic, included in file-name order by the generated `services_rows.h`) and the host services |
| `include/es2/sim/residue.h`, `src/residue.c` | the residue table |
| `include/es2/sim/adapters.h`, `src/adapters.c` | the proxy's adapters compiled in, one hook row per registry line |
| `include/es2/sim/host.h`, `src/host.c`, `src/main.c` | mapping, set-up, log, `es2sim.exe` |
| `oracle/` | pure C17 shared with the proxy: virtual time, the tick-record lines, the input journal, the after-load snapshot, the memory watches |
| `include/es2/sim/platform_window.h`, `audio_window.h`, `platform_sdl.c`, `audio_sdl.c`, `joystick_sdl.c` | `es2sim_window`'s platform: the window procedure's messages from a backend's events, loop seams, frame conversion, the SOS calls over a sink and MCI; the SDL3 backend, the SOS mixing sink, the opt-in joystick |
| `include/es2/sim/platform_web.h`, `audio_web.h`, `src/platform_web.c`, `audio_web.c`, `window_wasm_stubs.js` | the web platform and sink over the "es2sim" imports (`web/es2sim_imports.mjs`; [web_build.md](../docs/web_build.md) sections 9, 10 and 12) |
| `include/es2/sim/os_process.h`, `src/os_process_win32.c`, `os_process_wasm.{c,js}`, `dispatch_trap_wasm.c`, `host_fault_wasm.c`, `image_access_wasm.c` | es2sim's process on its OS: the guest-range claim or layout check, the simulation thread, the last-chance fault report, StackBase, the keyboard tables, the message box; in wasm the end of a run by a trap or a media read (web_build.md sections 5 and 6) and the counterparts of the Windows guards, which install nothing |
| `src/cpu_fault.c`, `src/image_access.c`, `src/perf.c` | the CPU divide fault on the original's operands (Windows; the portable link uses `//core:divide`); the image-access measurement (table sites in `src/image_sites.c`); the frame-time recorder |
| the service areas below | the service rows of one area each |
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
  (own_process.md 2.4), never by casting an address. Every call of other DBSIM
  code goes through the dispatch (`es2_sim_call`).
- Test executable names avoid `update`, `patch`, `setup`, `install`
  (Windows' installer heuristic, error 740): hence `table_test`, not
  `dispatch_test`.

## Tests

- Unit tests (`//sim:*_test`) run on synthetic images and recording services:
  they prove the own code against the model of the contract, not against the
  original. Per area in the table below; the host, heap, dispatch and image
  tests are `//sim:image_test`, `table_test`, `trap_calls_test`, `trap_refusal_test`, `guest_memory_test`
  (also `_wasm`), `heap_test`, `services_test`, `host_real_test`,
  `host_watch_real_test`, `divide_scenarios_test`; the web build's
  `es2sim_wasm_exits_test` and `es2sim_wasm_layout_test`.
- Each service area is also compared with the original's machine code in
  [`tools/reference-game/invitro`](../tools/reference-game/invitro/README.md):
  `differential_<stem>_test`, one per entry of `SIM_SUITES`
  (`tools/reference-game/invitro/suites.bzl`, flags and case headers there).
  The stems are in the table below.
- The replay tests and the frame compare prove the whole process against
  the oracle goldens.

### The replay test

`replay_tests.bzl` and `tests/replay_suite.py` run one oracle suite through
`es2sim.exe --headless` and compare it with its golden (`//sim:replay_<suite>_test`,
manual, one per registered suite; `//sim:replay_all`; the driver's own test is
`//sim:replay_suite_test`). The driver hands the host `--config`
(`es2-replay.ini`: the proxy's `[trace]`, `[time]`, `[input]`, `[arming]` plus
`[game]` and `[run]`, with `frame_limit`), `--tick-log` and `--environment`
(an environment block of the frame references' size, so the guest heap's
layout does not follow the test process's environment) in a prepared
install directory; the host's part is the journal, the watches, stopping at
`frame_limit`, the `exit` record and the return files in `DATA\`.
`[trace] heap_trace=<path>` writes the heap trace that the proxy's `[rtl] heap_trace`
writes for the original; `tools/reference-game/scripts/heap_trace_compare.py`
compares the two. `[snapshot] after_load=1` writes the after-load snapshot
(`oracle/snapshot.{h,c}`, ES2SNAP1 v2) before the first logic frame. How to run
it and read its result lines:
[oracle_regression.md](../docs/reference-game/oracle_regression.md) "The
own-process replay". The python files sit in `tests/` but the targets in this
package (a `sim/tests/BUILD.bazel` would cut `//sim`'s test sources off).

The same suites run with the wasm build under Node
(`//sim:replay_wasm_<suite>_test`, `replay_wasm_all`; `replay_wasm_tests.bzl`),
over the built site (`//sim:replay_site_<suite>_test`, `replay_site_all`;
`replay_site_tests.bzl`) and windowed, native against web
(`//sim:replay_window_wasm_<suite>_test`; `tests/replay_window_wasm.py`);
all manual.

### The frame compare and frame times

`frames_tests.bzl` and `tests/frames_suite.py`: `//sim:frames_<suite>_test`
(manual, `//sim:frames_all`; `//sim:frames_wasm_<suite>_test` for the wasm
build) runs a suite through `replay_suite.py --frames`, compares the capture
with the reference manifest and checks every kind of difference against
`tests/frames_known.py`. `tests/perf_suite.py` summarises and measures
`es2sim`'s frame times. Both are described in
[own_process.md](../docs/reference-game/own_process.md).

## The service areas

Each area is own code for the original functions its contract document lists
(addresses live there and in the headers). Sources are `include/es2/sim/<x>.h`
and `src/<x>.c`; most are compiled into `//sim:services`, `init` and `startup`
have libraries of their own. "Unit" is the `//sim:` test; "In-vitro" the stem
of `differential_<stem>_test`.

| Area | Sources | Unit | In-vitro | Contract |
|---|---|---|---|---|
| Static initialisation: c0w32 entry `0x401000`, 65 init and 26 exit records keyed by the user's record VAs; `CreateFileA` (-1 headless), `CloseHandle` (refused), `GetModuleHandleA` | `init`, `services_platform` | `init_test` (the reference file's record tables with `ES2_DATA_ROOT`) | `init_records` | [own_process.md](../docs/reference-game/own_process.md#the-static-initialisation-as-own-code) |
| Start-up, VOL registration, parsers, fatal report, `-eggplant` and memory checks; CW3220's file imports over its file model and the path helper `0x45ee00` | `startup`, `volumes`, `files`, `services_files` | `startup_test`, `es2sim_real_test` | `startup` | [launcher.md](../docs/reverse-engineering/launcher.md#dbsims-start-up-as-own-code), [own_process.md](../docs/reference-game/own_process.md#start-up-parsers-and-file-services) |
| Main loop, pump with the journal as message source, window procedure, limiter, clock under virtual time, exit; the DIB section `0x464d44` | `loop`, `frame_buffer`, `oracle/journal` | `loop_test`, `//sim/oracle:oracle_test` | `loop` | [main_loop.md](../docs/reverse-engineering/main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code), [own_process.md](../docs/reference-game/own_process.md#the-main-loop-the-journal-and-the-exit) |
| Input layer: set-up `0x45a5dc`, device poll `0x45baa8`, joystick, mouse queue, prefs streams | `input` | `input_test` | `input_layer` | [input.md](../docs/reverse-engineering/input.md#the-input-layer-as-own-code), [own_process.md](../docs/reference-game/own_process.md#the-input-layer) |
| Mission load: the functions the load root `0x461608` reaches, in call order; cockpit step, cursor sprite store, mono output, terrain shading pass | `load`, `oracle/snapshot` | `load_test`, `//sim/oracle:snapshot_test` | `load_rest` | [mission_load.md section 33](../docs/reverse-engineering/mission_load.md#33-the-mission-load-in-the-own-process), mission_load.md section 34, [own_process.md](../docs/reference-game/own_process.md#the-mission-load) |
| Audio: SOS driver load and init, sound table, SFX manager, voice start, CD audio over MCI, pause and resume | `audio` | `audio_test`, `sos_mixer_test` | `audio` | [audio.md](../docs/reverse-engineering/audio.md#audio-in-the-own-process), [own_process.md](../docs/reference-game/own_process.md#audio) |
| HUD and cockpit construction, display modes, panel draws, per-frame HUD state, headless | `hud`, `hud_load`, `hud_cockpit`, `hud_gauges`, `hud_display`, `hud_draw`, `hud_state` | `hud_test` (covers all HUD files below) | | [hud.md](../docs/reverse-engineering/hud.md#the-hud-line-in-the-own-process), [own_process.md](../docs/reference-game/own_process.md#the-hud-line) |
| per-frame row-1 walk and its leaves | `hud_frame` | | | [hud.md](../docs/reverse-engineering/hud.md#the-map-draws-tail-the-load-roots-last-steps-the-radar-draw-and-the-per-frame-row-1-walk) |
| HUD message queue, ports, gauge blink | `hud_widgets` | | `hud_widgets` | [hud.md](../docs/reverse-engineering/hud.md#the-message-queue-the-ports-and-the-gauge-blink) |
| Gunsights and gauges | `gadgets` | | `gadgets` | [hud.md](../docs/reverse-engineering/hud.md#the-gunsights-and-gauges-as-own-code) |
| MFD and HDD frames, pages and draws | `mfd` | | `mfd`, `mfd_rest2` | [hud.md](../docs/reverse-engineering/hud.md#the-mfd-and-hdd-frames-and-pages-as-own-code), [hud.md](../docs/reverse-engineering/hud.md#the-mfd-status-and-target-draws-the-hdd-status-body-the-radio-records-and-the-entry-steps) |
| Panel scroll zones, radar page and HUD radar | `panel` | | `panel` | [hud.md](../docs/reverse-engineering/hud.md#the-scroll-zones-and-the-radar-page) |
| Live widget leaves: mission clock, layout scale, hit test, widget, slider and root | `widget_leaves` | | `widget_leaves` | [hud.md](../docs/reverse-engineering/hud.md#the-live-widget-leaves) |
| Panel gadgets' method-table bodies | `hud_gadgets` | | `panel_gadgets` | [hud.md](../docs/reverse-engineering/hud.md#gauge-and-panel-gadget-constructors) |
| Radio port, HDD legend and buttons, throttle slider slots | `hud_radio` | | `hud_radio` | [hud.md](../docs/reverse-engineering/hud.md#the-radio-panel-and-the-hdd-leftovers) |
| Cockpit jolt and bump, mission-start drop sequence `0x45d85c` | `cockpit` | | `cockpit`, `cockpit_rest` | [display_modes.md](../docs/reverse-engineering/display_modes.md#the-jolt-and-the-bump-as-own-code), [cockpit.md](../docs/reverse-engineering/cockpit.md#drop-sequence-45d85c-and-panel-constructor-431bf8) |
| View transform `0x48c2e4`, projection leaves, matrix inverse, polygon and terrain lighting, light objects and table | `view` | `view_test` | `view` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#view-transform-projection-and-lights), [own_process.md](../docs/reference-game/own_process.md#the-view-transform-the-projection-and-the-lights) |
| Modal dialogs: mission-end alert, quit confirmation, options dialog | `dialogs` | `dialogs_test` | `dialogs` | [main_loop.md](../docs/reverse-engineering/main_loop.md#the-modal-dialogs), [main_loop.md](../docs/reverse-engineering/main_loop.md#the-modal-dialogs-in-the-own-process), [own_process.md](../docs/reference-game/own_process.md#the-modal-dialogs) |
| Graphics library: clip helpers, fills, ellipse, bitmap draw and grab, text, BMP_8 driver slots, scan converter | `gfx` | `gfx_test` | `gfx` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#graphics-library-primitives) |
| Frame sequence `0x45fba4`, palette cycler, fades, palette upload, present with the headless frame capture | `present` | `present_test` | `present` | [presentation.md](../docs/reverse-engineering/presentation.md#the-presentation-frame-in-the-own-process) |
| Affine fill family, screen cuts, span loops, perspective fill, shade lookup | `poly` | `poly_test` | `poly`, `poly_rest` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#software-polygon-fills) |
| 2D polygon fill `0x483eb8`, line `0x483a04`, BMP_8 line writer | `fill` | | `gl_fill` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#gl-fill-layer-and-line) |
| Gouraud fill `0x487acc`, its triangle and the terrain's flat fill | `gouraud` | `gouraud_test` | `gl_gouraud` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#gouraud-and-flat-fill) |
| Sky and horizon, terrain cells pass, binning, sort tree, per-object draw, scene finish | `scene` | `scene_test` | `scene` | [horizon.md](../docs/reverse-engineering/horizon.md), [terrain.md](../docs/reverse-engineering/terrain.md#binning-the-cell-callback-and-the-sort-tree-as-own-code) |
| Terrain renderer: set-up, walk, cell draw, height cache | `terrain_draw` | | `terrain_draw` | [terrain.md](../docs/reverse-engineering/terrain.md#the-terrain-renderer-as-own-code) |
| The terrain draw's view frustum library | `frustum` | | `frustum` | [terrain.md](../docs/reverse-engineering/terrain.md) |
| DTS shape draw, part kinds, polygons, BSP walk, view-space projection and shade ranges, actor draw slots, explosion binning | `shape_draw` | `shape_draw_test` | `shape_draw`, `shade_ranges`, `actor_draw` | [rasterizer.md](../docs/reverse-engineering/rasterizer.md#dts-shape-draw), [rasterizer.md](../docs/reverse-engineering/rasterizer.md#view-space-projection-and-shade-ranges), [rasterizer.md](../docs/reverse-engineering/rasterizer.md#actor-draw-slots-and-explosion-binning), [own_process.md](../docs/reference-game/own_process.md#the-dts-shape-draw-slots) |
| Display transitions `0x42a4c4`/`0x42a9c0` with the slides, retrace wait, shield palette | `display` | | `display` | [display_modes.md](../docs/reverse-engineering/display_modes.md#the-display-transitions-and-the-shield-palette-as-own-code) |

## Seams and traps

- **Providers.** Platform-dependent answers come through a seam, never from the
  area itself: `es2_sim_input_platform` (`GetKeyState`, joystick, cursor, client
  size: headless from the journal via `es2_sim_input_headless`, windowed from
  the backend), `es2_sim_load_platform` (headless palette answers, colour table,
  present count), `es2_sim_audio_device` (headless the silent
  `es2_sim_audio_silent`). `es2_sim_startup.dib_section`/`.after_load`/`.quit_pump`
  are how the start-up reaches the DIB section, the loop after the load and the
  pump.
- **Headless draws write no pixels** in `view`, `scene`, `shape_draw` and the HUD
  areas, and keep every other write exact. The HUD's coupling calls (clock,
  stream B, sound API) go through `hud_clock`/`hud_rng_*`/`hud_sound` and are
  recorded in `es2_sim_hud`'s trace.
- **Dialogs.** A modal pass's pump is the main loop's row, so the journal's
  pump-keyed entries answer the dialog; `es2_sim_dialogs` counts the passes and
  refuses a dialog past `pass_limit`.
- **Deviations from the original.** The retrace wait `0x45c638` does no port I/O;
  the mono output `0x4955c4` is refused when a DARKMONO.VXD handle is present;
  the palette set's GDI count is NUMCOLORS (`num_colors`); the flag path of
  `0x42e8e8` keeps an original bug whose words depend on the stack address.
- **No row.** `0x469dc8` and `0x471d08` have no static reference and get none.
- **Call-throughs.** `0x464a14` stays the main loop's row; the camera read
  `0x45edf8` stays the hook `camera_current`; the mech's part callee `0x4030d0`
  is the registry hook `part_shapes_bind` (core's
  `es2_part_shapes_bind_registers`); the terrain shading pass `0x46c2e0` is the
  row `terrain_shading` in `src/load.c`.
- **Not every in-vitro part is enabled.** `differential_gfx_test` enables the
  clip, fill and bitmap parts; the text and scan-converter parts are written but
  not enabled (`gfx_cases.h`).
