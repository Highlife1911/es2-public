# Functionality overview

Everything the repository builds or runs, one line each. **Used by:** *game*
= the played chain; *test* = `bazel test //...`; *oracle* = runs against
the original game in a measurement clone; *owner* = started by hand;
*gen* = writes committed sources or docs; *diag* = diagnostics on demand.
A `?` marks an entry whose use is unclear and should be checked before
anything is removed.

## Executables and DLLs

| Target | Purpose | Used by |
|---|---|---|
| `//sim:es2sim_window` | Own `DBSIM.EXE`: the simulation process with window, SDL3 audio and joystick | game |
| `//sim:es2sim` | The same process headless (`--headless`), for replays and frame tests | test, oracle |
| `//shell:es2shell` | Own `VSHELL.EXE`: the graphical shell (menus, preparation, campaign) | game |
| `//shell:es2menu` | Text front end of the shell logic | owner, test |
| `//shell:es2movie`, `//shell:es2sound` | Manual tools: play one movie, play the shell sounds | owner |
| `//launcher:es2launch` | Own `ES.EXE`: starts shell and simulation | game |
| `//rtl:cw3220.dll` | Own `CW3220.DLL`: the Borland runtime imports of DBSIM | game, oracle |
| `//tools/reference-game/proxy:ddraw.dll` | Proxy loaded by the original DBSIM: hooks own code into it, media bridge, save/load guard, presenter, measurement probes | oracle, game (original chain) |
| `//media:iv32_bind` | In-process Indeo 3.2 decoder binding for the proxy and the shell | game |
| `//formats:es2fmt` | Command-line reader for the original file formats | diag |
| `//formats:player_mec_tool` | Reads and writes the player's mech file | diag |
| `//shell:es2prep_core` | Mission preparation as a command-line tool | test, oracle |
| `//tools/reference-game/shell-invitro:es2shellprep` | Runs VSHELL's preparation code in a private mapping and publishes the launch set | oracle |
| `//tools/reference-game/invitro:differential_harness` | Runs original machine code and own code side by side | test |
| `//tools/reference-game/proxy:presenter_offline` | Replays presenter captures without the game | diag |
| `//sim:joystick_probe` | Lists SDL3 joysticks | diag |
| `//tools/reference-game/helpers:*` | `capture_crash`, `hang_snapshot`, `probe_media`, `session_volume`: read-only process diagnostics, volume control during oracle runs | diag, oracle |
| `//platform:smoke_windows` | SDL3 smoke program for Windows | test |
| `//shell:*_tool` (draw, text, widgets, help, movies, mission_map, editors, stats, practice, texts, condition), `launch_prefs_tool` | Test drivers for single shell modules on real data | test |

## Libraries

| Package | Purpose |
|---|---|
| `core/` | Own implementations of DBSIM functions (122 source modules), host-independent |
| `sim/` | Host of the own DBSIM: guest image at the original addresses, heap, dispatch, services, renderer, HUD, audio, input |
| `sim/dispatch/services` | Service rows that bind original call sites to own code |
| `sim/oracle` | Virtual time, tick records, input journal, snapshots, memory watches; shared by the proxy and `es2sim` |
| `shell/` | Own VSHELL: preparation, loadouts, saves, campaign, GUI screens, movies, sounds |
| `formats/` | Readers for VOL, DBM/DBA/DPL, DTS, DAT, MSN, SAV, STR, CFG, DFN and a read-only VFS |
| `hook/` | x86 detours with `original`/`replaced`/`compare` modes, signature checks |
| `rtl/` | The runtime models behind `cw3220.dll` |
| `launcher/` | Launch logic of `es2launch` |
| `media/` | IV32 binding |

## Verification

| Function | What it shows | Entry | Used by |
|---|---|---|---|
| In-vitro differential, quick | Own code equals original code per hook group, randomised cases (100,000 per hook) | `//tools/reference-game/invitro:differential_test` | test |
| In-vitro differential, full | The same with about 1M cases per hook | `:differential_full` | owner |
| Runtime differentials | The own runtime imports, heap, start-up, I/O and `cw3220.dll` against the original | `:differential_rtl_*` | test |
| Curated mutations | Every hook has a mutation the harness catches | `invitro/check_mutations.ps1` | owner |
| Generated mutations | Survivor list from mutated core functions | `invitro/generate_mutations.py` | owner |
| Live coverage | Branch sites reached in real game runs | `setup/run-live-coverage.ps1`, `scripts/live_coverage_*.py` | oracle |
| Oracle suites | Scripted missions in the original with virtual time, compared with goldens | `oracle-suites.json`, `setup/run-oracle-parallel.ps1`, `run-oracle-regression.ps1` | oracle |
| Compare mode | Own and original code run in the same game, results compared | proxy `compare`, `scripts/compare_audit.py` | oracle |
| Replay suites | `es2sim --headless` plays an oracle suite and matches its golden | `//sim:replay_suite_test`, `sim/tests/replay_suite.py` | test |
| Frame tests | `es2sim` frames against DBSIM's captured frames | `//sim:frames_suite_test`, `setup/capture-dbsim-frames.ps1` | test, oracle |
| Frame times | `es2sim` and DBSIM frame pacing | `//sim:perf_suite_test`, `scripts/dbsim_present_times.py` | test, oracle |
| Tick log | Per-tick state log of DBSIM | proxy, `setup/configure-tick-log.ps1`, `run-tick-log-mission.ps1` | oracle |
| Input journal | Records and replays input deterministically | proxy, `input-scripts/`, `scripts/joystick_script.py`, `recording_compare.py` | oracle |
| Heap trace | Heap operations of DBSIM and `es2sim` compared | `scripts/heap_trace_compare.py` | oracle |
| Entry and VOL probes | Which original functions and files a run reaches (DBSIM map) | proxy, `scripts/dbsim_map.py`, `sim_gap.py` | oracle |
| Shell in vitro | VSHELL's preparation, slots and results against the own shell | `shell-invitro/` | test, oracle |
| VSHELL capture tour | Screens of the original shell for comparison | `setup/capture-vshell-screens.ps1` | oracle |
| Mission watches | Watched memory during missions | `watch-sets/`, `scripts/propose_watch_set.py`, `mission_watch_report.py` | oracle |
| Hook bisection | First hook that breaks a regression | `scripts/bisect_hooks.py` | diag |
| Script tests | The PowerShell and Python tooling itself | `//tools:scripts` | test |
| Evidence budget | Size and content rules of `es2-data` | `tools/evidence_budget.py` | test |

## Build configurations

| Configuration | Purpose | Used by |
|---|---|---|
| default (`windows_x86`) | The game's 32-bit binaries and all tests | build, test |
| `windows_x86_64` | Host platform of the build; 64-bit build of `formats` and `core` | build |

## Scripts

### Installation and start (`tools/reference-game/setup`)

| Script | Purpose |
|---|---|
| `prepare-native.ps1` | Builds a portable installation from the extracted CD |
| `verify-native-rebuild.ps1` | Checks an installation against its recorded hashes |
| `new-measurement-clone.ps1` | Creates the measurement clones `timing-r111`, `-p1` … `-p5` |
| `configure-runtime.ps1`, `configure-sim.ps1`, `configure-shell.ps1`, `configure-media.ps1`, `configure-tick-log.ps1` | Deploy own components or the measurement proxy into an installation |
| `retire-originals.ps1` | Removes `DBSIM.EXE` and `VSHELL.EXE` from a played installation, or restores them |
| `start-native.ps1`, `start-own-launcher.ps1`, `start-own-menu.ps1`, `start-timing-mission.ps1` | Start an installation in one of its configurations |
| `START_ES2.cmd`, `START_ES2_DIAGNOSE.cmd` (repository root) | Double-click start of the owner's installation |
| `CloneGuard.psm1`, `SuiteLaunch.psm1`, `Es2Bazel.psm1`, `pwsh_session.py` | Shared modules: clone locks, prepared-file transactions, Bazel calls, PowerShell sessions for tests |
| `extract_data_track.py` | Extracts the data track from the raw CD image |

### Oracle runs (`tools/reference-game/setup`)

`run-oracle-parallel.ps1`, `run-oracle-regression.ps1`, `run-prepared-suite.ps1`,
`run-mission-outcome.ps1`, `record-suite.ps1`, `capture-launch-set.ps1`,
`capture-dbsim-frames.ps1`, `capture-vshell-screens.ps1`, `run-live-coverage.ps1`,
`run-tick-log-mission.ps1`, `validate-media.ps1`, `test-hook-registry.ps1`,
`run-draft-coverage.ps1` with `run-draft-oracle-bridge.ps1` (live coverage of a
verified draft suite before it is registered).

### Analysis and generators (`tools/reference-game/scripts`)

| Kind | Scripts |
|---|---|
| gen: committed sources | `dbsim_data.py`, `dbsim_image_tables.py`, `vshell_store.py`, `vshell_layout.py`, `legacy_graph_counters.py`, `shell/tools/gen_layout_table.py`, `proxy/hook_groups.py`, `proxy/generate_exports.py` |
| gen: docs | `function_index.py`, `regenerate_function_docs.ps1`, `hooks_table.py`, `hook_candidates.py`, `live_coverage_column.py` |
| oracle reports | `oracle_regression.py`, `compare_tick_logs.py`, `expand_compact_ticks.py`, `dbsim_frames.py`, `frame_dumps.py`, `presenter_capture.py`, `vshell_present_times.py`, `modal_pump_keys.py`, `message_trace.py`, `after_load_snapshot.py`, `live_coverage_report.py`, `live_coverage_union.py`, `live_probe_report.py`, `suite_coverage_delta.py`, `branch_edges.py`, `probe_timing.py`, `OracleSpeed.psm1` |
| binary analysis | `areas.py`, `rtl_imports.py`, `probe_instruction_inventory.py`, `sim_gap.py`, `dbsim_map.py` |
| diag | `read_minidump.py` |

### Shell front ends (`shell/tools`)

`es2prep.py` (preparation), `es2loadout.py` (loadout → SAV → launch set),
`es2campaign.py` (campaign advance), `es2chain.py` (preparation, DBSIM, campaign
without menus), `es2menu_backend.py` (set-up of `es2menu`).

### Repository and workers (`tools/`)

| Script | Purpose |
|---|---|
| `setup-dependencies.ps1`, `setup-platform-dependencies.ps1` | Install and verify the pinned tools in `es2-dependencies` |
| `check.ps1`, `check_counters.py` | Acceptance run of a batch: tests and a parallel oracle run |
| `check-fresh-clone.ps1` | Verifies that a fresh clone builds and tests |
| `worker.ps1`, `clean-worker-bazel.ps1` | Worktrees and Bazel output bases of parallel workers |
| `python_tree_digest.py` | Digest of the pinned Python tree |
| `git-hooks/pre-commit` | Refuses files from the original CD (`data/iso_file_hashes.sha256`) |

## Checked-in data

| Path | Content |
|---|---|
| `data/` | Hash lists of the reference CD |
| `sim/data`, `shell/data` | Generated tables from `DBSIM.EXE` and `VSHELL.EXE` (see generators) |
| `tools/reference-game/oracle-suites.json`, `input-scripts/`, `watch-sets/` | Definitions of the oracle suites, their input and watches |
| `docs/` | Formats, reverse-engineering notes, function index, decisions |
