# Documentation index

Start with [README.md](../README.md) and [CLAUDE.md](../CLAUDE.md); all functionality is in
[overview.md](overview.md). Addresses refer to the binaries of
[reference_version.md](reference_version.md) at image base `0x00400000`.

## Project

| Document | Content |
|---|---|
| [overview.md](overview.md) | Every executable, library, check, build configuration and script |
| [reference_version.md](reference_version.md) | The reference CD image and its three executables |
| [dependencies.md](dependencies.md) | The CD image and the pinned tools in `es2-dependencies` |
| [data_inventory.md](data_inventory.md) | Every installed file and what reads it |
| [build_system.md](build_system.md) | Bazel as the build system |
| [web_build.md](web_build.md) | Design of the WebAssembly build |
| [work_packages.md](work_packages.md) | Open work |

## Formats (`formats/`)

| Document | Format |
|---|---|
| [formats/vol.md](formats/vol.md) | VOL archives (`VOLN`), DBSIM's registration order |
| [formats/dbm.md](formats/dbm.md) | DBM/DBA/HBA images, palettes, the unused RLE/LZH decompressors |
| [formats/dpl.md](formats/dpl.md) | DPL palettes |
| [formats/dts.md](formats/dts.md) | DTS models |
| [formats/dat.md](formats/dat.md) | The DAT tables |
| [formats/mission.md](formats/mission.md) | MSN mission lists, `script.dat`, `mission.var`, SAV, the runtime operation selectors |
| [formats/collision.md](formats/collision.md) | COL/DMG/GL/PDG collision data |
| [formats/bnd.md](formats/bnd.md) | BND (never loaded by the retail game) |
| [formats/str.md](formats/str.md) | STR packed string files |
| [formats/cfg.md](formats/cfg.md) | `prefs.cfg`, `sound.cfg`, `keyjoy.cfg` |
| [formats/dfn.md](formats/dfn.md) | DFN/HFN bitmap fonts |
| [formats/herc_pictures.md](formats/herc_pictures.md) | The squad panel's Herc picture tables |

## DBSIM (`reverse-engineering/`)

| Document | Subsystem |
|---|---|
| [main_loop.md](reverse-engineering/main_loop.md) | Outer loop, the logic frame `0x45f570` and its call order, the death sequence, the dialog pump |
| [timing.md](reverse-engineering/timing.md) | Limiter, elapsed-time scaling, virtual time |
| [rng.md](reverse-engineering/rng.md) | The lagged generator, streams A and B |
| [player_state.md](reverse-engineering/player_state.md), [player_movement.md](reverse-engineering/player_movement.md) | Actor layout, movement, matrix, pose and track |
| [camera.md](reverse-engineering/camera.md) | Camera update `0x4011a0` |
| [terrain.md](reverse-engineering/terrain.md) | Terrain draw pass and segment walk |
| [weapons.md](reverse-engineering/weapons.md), [weapon_classes.md](reverse-engineering/weapon_classes.md) | Weapons, damage path, swept query, building turret |
| [vehicle.md](reverse-engineering/vehicle.md), [convoy.md](reverse-engineering/convoy.md) | Vehicle updates, the convoy chain |
| [ai.md](reverse-engineering/ai.md), [mech_order.md](reverse-engineering/mech_order.md) | Steering, vehicle control, AI behaviours, orders |
| [flyer.md](reverse-engineering/flyer.md) | Flyer flight model, controls and AI motion |
| [mission_runtime.md](reverse-engineering/mission_runtime.md) | Triggers, objectives, routes, timers, groups |
| [mission_load.md](reverse-engineering/mission_load.md) | Mission load `0x461608` up to the first frame, the arming point |
| [asset_loading.md](reverse-engineering/asset_loading.md) | Shape and bitmap loading, the persistent-object loader |
| [input.md](reverse-engineering/input.md) | Input dispatcher `0x460870`, torso servos, weapon keys |
| [vtables.md](reverse-engineering/vtables.md), [hit_tests.md](reverse-engineering/hit_tests.md), [horizon.md](reverse-engineering/horizon.md) | Actor classes and dispatch, hit tests and COL traversal, the horizon painter |
| [hud.md](reverse-engineering/hud.md), [cockpit.md](reverse-engineering/cockpit.md), [display_modes.md](reverse-engineering/display_modes.md) | HUD, cockpit, display modes, MFD and HDD |
| [rasterizer.md](reverse-engineering/rasterizer.md) | Software rasterizer and graphics library |
| [presentation.md](reverse-engineering/presentation.md) | The 8-bit frame, GDI and DirectDraw present, palette, cursor |
| [audio.md](reverse-engineering/audio.md) | The SOS audio boundary, the mixer, the shell's sounds |
| [file_io.md](reverse-engineering/file_io.md) | Every file call site of DBSIM and VSHELL |

## VSHELL and ES (`reverse-engineering/`)

| Document | Subject |
|---|---|
| [launcher.md](reverse-engineering/launcher.md) | The `ES.EXE` contract, the own launcher and text menu, the shell state engine |
| [vshell_process.md](reverse-engineering/vshell_process.md) | WinMain, options, start-up, window, main loop, exit codes |
| [vshell_preparation.md](reverse-engineering/vshell_preparation.md) | Mission preparation, campaign advancement, debrief |
| [vshell_ui.md](reverse-engineering/vshell_ui.md) | Screens, handlers, which screen writes which SAV/CFG/launch field |
| [vshell_presentation.md](reverse-engineering/vshell_presentation.md) | Frame, present primitive `0x406bfb`, palettes, blitter |
| [vshell_widgets.md](reverse-engineering/vshell_widgets.md) | Widget classes, event model, the generated layout table |
| [vshell_text.md](reverse-engineering/vshell_text.md) | Glyphs, labels, edit fields, the mission text pages |
| [vshell_mission_map.md](reverse-engineering/vshell_mission_map.md) | The mission map: inputs, projection, draw order, fly-over |
| [vshell_state.md](reverse-engineering/vshell_state.md) | Global state outside SAV/CFG and its reset matrix |

## Own code and verification (`reference-game/`)

| Document | Content |
|---|---|
| [hooks.md](reference-game/hooks.md) | The hook registry, configuration, validation, how to add a group |
| [own_process.md](reference-game/own_process.md) | The `es2sim` host: guest image, heap, dispatch, stack residue, services, acceptance |
| [dbsim_gap.md](reference-game/dbsim_gap.md), [sim_gap.md](reference-game/sim_gap.md) | Original code still running inside the original DBSIM; calls of `es2sim` without a service row |
| [dbsim_map.md](reference-game/dbsim_map.md), [hook_frontier.md](reference-game/hook_frontier.md), [function_verdicts.md](reference-game/function_verdicts.md) | DBSIM function map, the bridged-call inventory, verdicts |
| [functions/](functions/README.md) | Generated DBSIM function index |
| [rtl_imports.md](reference-game/rtl_imports.md), [dll_imports.md](reference-game/dll_imports.md) | DBSIM's `CW3220.DLL` imports and the other DLLs |
| [compare_mode.md](reference-game/compare_mode.md) | Which hooks accept compare mode and why |
| [oracle_regression.md](reference-game/oracle_regression.md), [oracle_suites.md](reference-game/oracle_suites.md) | Running regressions, recording and registering suites |
| [tick_log.md](reference-game/tick_log.md), [input_journal.md](reference-game/input_journal.md) | Log formats and the input journal |
| [live_coverage.md](reference-game/live_coverage.md) | Live branch coverage of the original |
| [shell_invitro.md](reference-game/shell_invitro.md) | VSHELL in the in-vitro harness |
| [vshell_map.md](reference-game/vshell_map.md) | VSHELL entry probes |
| [setup.md](reference-game/setup.md) | Installation and its layout, start scripts, worker helper |
| [media_compatibility.md](reference-game/media_compatibility.md) | The proxy's media bridge and the mission-map guard |

Component READMEs: [core](../core/README.md), [sim](../sim/README.md),
[shell](../shell/README.md), [launcher](../launcher/README.md),
[formats](../formats/README.md), [hook](../hook/README.md),
[platform](../platform/README.md),
[reference-game tooling](../tools/reference-game/README.md),
[in-vitro harness](../tools/reference-game/invitro/README.md),
[shell in vitro](../tools/reference-game/shell-invitro/README.md).

## Recipes

- **A hook** (a DBSIM function as own code inside the original): contract in the
  subsystem document from the `llvm-objdump` listing; the `core/` function, its adapter
  and registry entry (`/* Original: DBSIM.EXE 0x... */`); in-vitro cases against the
  original with every reachable branch both ways; one caught mutation; one oracle
  regression in `replaced` mode ([hooks.md](reference-game/hooks.md#how-to-add-a-hook-group)).
- **A service row** (own code in `es2sim`): contract section from the listing; rows in
  `sim/dispatch/services/<name>.h` (no duplicate VA, none that is a hook); an in-vitro
  suite against the original's machine code; one caught mutation; acceptance by the
  headless replay against the golden and, for drawing, by the frame hash against the
  original's capture.
- **A shell step:** contract, library, in-vitro harness against VSHELL, flow, screen.
