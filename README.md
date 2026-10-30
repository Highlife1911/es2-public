# EarthSiege II: behaviour-identical reimplementation

EarthSiege II (German GoldGames 1.11) on modern Windows from the original game
data, with behaviour identical to the original and every original executable
replaced by own code. This branch holds the finished reimplementation; the plan
for further work is on `main`. Rules: [CLAUDE.md](CLAUDE.md); documentation index:
[docs/README.md](docs/README.md); all functionality: [docs/overview.md](docs/overview.md).

No game data lives in this repository. Evidence is in the sibling repository
`es2-data`, the pinned tools and the reference CD image in `es2-dependencies`
([docs/dependencies.md](docs/dependencies.md)).

## Goal

EarthSiege II (German GoldGames 1.11, [reference_version.md](docs/reference_version.md))
runs on modern Windows from the user's original data with behaviour identical to
the original. Every original executable and DLL is replaced by own code; structure
may differ, behaviour may not. Changes forced by the platform (a window instead of
8-bit full screen, no WinHelp) are documented next to the contract they touch.

"Identical" means: the same input gives the same simulation state, random sequence,
mission events and files; presentation looks and sounds like the original.

## Architecture

```text
 Original                          Own replacement
 ES.EXE      launcher         →    es2launch      launcher/
 VSHELL.EXE  menus, campaign  →    es2shell       shell/
   │ DATA/script.dat, player.mec, mission.var, mission.str, arguments
 DBSIM.EXE   simulation, 3D   →    es2sim         sim/ + core/
 CW3220.DLL  Borland RTL      →    cw3220.dll     rtl/
 BWCC32, SOS9503/SOSLIBS3,    →    not loaded by the own chain
 DDRAW (8-bit full screen)         (own presenter, SDL3 window, audio, input)
```

- The file hand-off between shell and simulation is the process boundary: each side
  can be original or own, as long as it reads and writes the same files and arguments.
- `core/` holds own implementations of DBSIM functions, host-independent. Two hosts use it:
  - the `ddraw.dll` proxy (`tools/reference-game/proxy`) loaded by the original DBSIM,
    which detours each function after checking module hash and byte signatures
    (`hook/`, modes `original`/`replaced`/`compare`; registry in [hooks.md](docs/reference-game/hooks.md));
  - `es2sim`, which maps a guest image at the original addresses and binds the original
    call sites to own code through service rows (`sim/dispatch`).
- DBSIM and VSHELL data tables are compiled in as generated sources (`sim/data`, `shell/data`);
  no own executable reads an original executable.
- `formats/` reads the original files (VOL, DBM/DBA/DPL, DTS, DAT, MSN, SAV, STR, CFG, DFN)
  through a read-only VFS.
- `sim/oracle` provides virtual time, tick records, the input journal and memory watches
  to both hosts.

Timing: the loop is paced by `GetTickCount` with a 40 ms minimum and scales by the measured
elapsed time (`elapsed·256/125`, clamped to [64, 450]), so there is no fixed tick
([timing.md](docs/reverse-engineering/timing.md)). The simulation never reads platform
time; time enters as the per-iteration elapsed value, which the oracle chooses.
Randomness goes through the original lagged generator, two streams, fixed seed
([rng.md](docs/reverse-engineering/rng.md)).

## Status

| Component | Own code | Still original |
|---|---|---|
| Launcher | `es2launch`: options, checks, shell↔simulation loop | — |
| Shell | `es2shell`, `es2menu`: every screen, mission map, movies, sounds; every state change byte-equal to VSHELL in vitro | — |
| Simulation, own process | `es2sim`: 1,430 service rows; all 42 suites replay headless with identical tick logs; frames 40/40; DirectDraw full screen as SDL desktop full screen; the three `-D` demos | none open: every gap root, function behind one and virtual target in [sim_gap.md](docs/reference-game/sim_gap.md) has a row or a documented refusal (`sim_gap_verdicts.json`) |
| Simulation, in the original | 789 hooks from `core/`: the logic frame, the render root and every simulation call in them | drawing, SOS driver, heap, start-up and load before arming ([dbsim_gap.md](docs/reference-game/dbsim_gap.md)) |
| Runtime | `cw3220.dll`: all 53 imports of DBSIM | — |
| Presentation | GDI presenter, windowed DirectDraw, SDL3 window, audio and joystick in `es2sim_window` | — |
| Media | in-process Indeo 3.2 binding (`media/`) | the system codec in the original chain |
| Installation | `prepare-native.ps1` builds a portable installation from the extracted CD; `retire-originals.ps1` removes `DBSIM.EXE` and `VSHELL.EXE` from played installations | the measurement clones keep both for the oracle |

The played chain `es2launch → es2shell → es2sim` executes no original instruction and
loads no game DLL.

Known differences in the own code, each documented next to its contract:

- `frames_all` excludes frames 204 and 600 of `c3_10-detail-options`, where the original
  itself varies with the SOS timer's wall-time notices (`sim/tests/frames_known.py`).
- The SDL audio sink is flushed after a buffer mixed with no voice listed: SDL's
  resampler would otherwise hold the last frames back; the flushed buffer is silence
  ([audio.md](docs/reverse-engineering/audio.md) "Forced deviations and guards").
- `es2sim` is slower per modal pass than the original, so the `0xa000` sound notices in
  `c3_10` fall one dialog frame earlier; no pacing model removes the shift, the count
  matches the paced original ([main_loop.md](docs/reverse-engineering/main_loop.md)
  "The pump accounting").
- `es2sim` copies its environment as CW3220 does, without padding; the size is process
  input, so the frame tests give it one of the original's size, because a read past a
  block end (`c1_08`) depends on it ([rtl_imports.md](docs/reference-game/rtl_imports.md)
  "The environment copy").
- The GeneralAlert's button release list `0x4530bc` holds eight entries; where the
  original overruns it and faults at `0x453127` (DEMO1, tape playback), `es2sim` ends
  with a refusal (exit 70). An access violation in own code under the guest's frame
  records ends with exit 71 ([own_process.md](docs/reference-game/own_process.md)
  "Host faults under guest frame records").
- The character messages of the dead key `^` (`c1_01-mouse`, German layout) differ: Windows
  keeps the pending dead key per session, not per process, so they depend on what other
  processes typed (the original's run started with a `^` pending: two `WM_CHAR` where
  `es2sim` gets one `WM_DEADCHAR`); `es2sim` also leaves lParam bit 25 clear on the accent
  of two characters. No handler reads them; both replays are identical to the golden
  ([main_loop.md](docs/reverse-engineering/main_loop.md) "The dead key as a known difference").

Original behaviour that looks like a fault but is reproduced: the Razor locks at pitch
16,384 after a long vertical climb with roll 0; the German C1_01 debrief shows the
praise video with the failure text (data shift in `MISSION.STR`); the broken gradients
are the original art.

### Installations

Under `build\installs`, created by `prepare-native.ps1` and
`new-measurement-clone.ps1`:

| Installation | Role |
|---|---|
| `native-r111` | The owner's installation: own shell, own presenter, replaced hooks, own `cw3220.dll`. Never used for experiments |
| `rebuild-r111` | The owner's play clone: `es2launch` → `es2shell` → `es2sim_window`, without `DBSIM.EXE` and `VSHELL.EXE` (`retire-originals.ps1 -Restore` brings them back); `-Shell vshell` starts the original shell |
| `timing-r111`, `-p1`, `-p2`, `-p4`, `-p5` | Measurement clones for oracle runs; each run deploys proxy, INI and runtime and restores the prepared files afterwards |
| `calibration-r111` | Calibration clone with an old proxy (DBSIM crashes after the shell's Full Screen) |

`<es2-data>` holds the goldens of the 42 suites (`oracle\golden\<suite>`), the
sealed launch sets (`oracle\launch`, ignored, regenerable with `es2loadout.py`) and the
evidence (`logs\`). `ES2_DATA_ROOT` is `build\extracted\cd`; `local.bazelrc`
sets `ES2_DATA_REPO=<es2-data>`.

## Verification

| Check | What it proves |
|---|---|
| In-vitro differential (`invitro:differential_test`, `differential_full`) | Each hook equals the original machine code on randomised cases |
| Mutations (`check_mutations.ps1`) | Each hook has a mutation the differential catches |
| Oracle suites (`run-oracle-parallel.ps1`) | 42 scripted missions in the original with virtual time match their goldens with all hooks replaced |
| Compare mode | Own and original code agree in the same running game, for the hooks that support it |
| Replay suites (`//sim:replay_suite_test`) | `es2sim` reproduces every suite's tick log, watches and return files |
| Frame tests (`//sim:frames_all`) | `es2sim` frames equal DBSIM's captured frames |
| Shell in vitro (`shell-invitro/`) and capture tour | The own shell's files and screens equal VSHELL's |
| `tools/check.ps1 -Live -OwnShell -OwnSim` | Tests, a parallel oracle run and the own chain end to end |

`bazel test //...` runs the quick tier; tests on real data read `ES2_DATA_ROOT` and skip
without it. Evidence goes to `es2-data\logs\<topic>-<date>` as small files: `check.ps1`
deletes raw tick logs of identical runs, captured frames stay ignored and only their hash
manifests are committed; `evidence_budget.py` checks the rules
([setup.md](docs/reference-game/setup.md#evidence-budget)). Reverse-engineering claims stay
hypotheses until an oracle run, an in-vitro case or the `llvm-objdump` listing confirms them.

## Layout

| Directory | Contents |
|---|---|
| `core/` | Own implementations of DBSIM functions, C17, headless ([README](core/README.md)) |
| `sim/` | The own DBSIM process `es2sim`: guest image at the original addresses, heap, dispatch, services; `sim/oracle/` shared with the proxy ([README](sim/README.md)) |
| `shell/` | The own VSHELL: `es2shell`, `es2menu` and their libraries ([README](shell/README.md)) |
| `launcher/` | The own `ES.EXE`, `es2launch` ([README](launcher/README.md)) |
| `rtl/` | The own `CW3220.DLL` on `core/`'s runtime models ([docs](docs/reference-game/rtl_imports.md#the-own-cw3220dll)) |
| `media/` | In-process IV32 (Indeo 3.2) decoder binding ([docs](docs/reference-game/media_compatibility.md#the-iv32-binding-library)) |
| `formats/` | Readers for the original file formats and a read-only VFS ([README](formats/README.md)) |
| `hook/` | Signature-checked detours with `original`/`replaced`/`compare` modes ([README](hook/README.md)) |
| `platform/` | SDL3 smoke targets ([README](platform/README.md)) |
| `tools/reference-game/proxy/` | The `ddraw.dll` proxy loaded by the original DBSIM: hook adapters for `core/`, media bridge, save/load guard, tick log, virtual time, input journal |
| `tools/reference-game/invitro/` | Differential harness: original machine code against own code, mutations, coverage ([README](tools/reference-game/invitro/README.md)) |
| `tools/reference-game/{setup,scripts,helpers,input-scripts,watch-sets}` | Installation, oracle suites, regressions, measurements ([README](tools/reference-game/README.md)) |
| `tools/` | Dependency setup, `check.ps1`, worker helpers, the pre-commit guard (`git-hooks/`) |
| `bazel/` | Shared macros (`defs/`), target platforms (`platforms/`), toolchains (`toolchains/`: LLVM-MinGW, SDL3) |
| `data/` | Hash lists of the reference CD for the pre-commit guard |
| `docs/` | Formats, reverse-engineering notes, reference-game documents |
| `build/` | Ignored, generated: Bazel output root and caches (`bazel/`), extracted CD and data ISO (`extracted/`), game installations (`installs/`) |

## Build and test

The system Bazel must match `.bazelversion`. `.bazelrc` enforces the lock file;
`local.bazelrc` (`tools/write-local-bazelrc.ps1`) keeps the output root in
`build/bazel` and sets `ES2_DATA_ROOT` (the extracted CD;
tests that need it skip without it) and `ES2_DATA_REPO` (the `es2-data` checkout
for goldens and frame references).

```powershell
bazel test //...                                              # quick tier
bazel test //tools:scripts                                    # PowerShell and Python tooling
bazel test //sim:frames_all                                   # es2sim frames against DBSIM's
bazel test //tools/reference-game/invitro:differential_full   # in-vitro tier with about 1M cases per hook
bazel build //tools/reference-game/proxy:ddraw.dll            # the proxy
pwsh tools/check.ps1 -Live -OwnShell -OwnSim                  # tests, oracle run and the own chain
```

While working, run only the targets a change touches. Starting the game and the
oracle runs: [setup.md](docs/reference-game/setup.md),
[oracle_regression.md](docs/reference-game/oracle_regression.md).
