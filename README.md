# EarthSiege II: behaviour-identical reimplementation

EarthSiege II (German GoldGames 1.11) on modern Windows and in the browser, from the user's
own CD, with behaviour identical to the original and every original executable replaced by
own code. Structure may differ, behaviour may not: the same input gives the same simulation
state, random sequence, mission events and files; presentation looks and sounds like the
original.

Rules: [CLAUDE.md](CLAUDE.md). Open work:
[docs/work_packages.md](docs/work_packages.md). Documentation index:
[docs/README.md](docs/README.md); everything the repository builds or runs:
[docs/overview.md](docs/overview.md).

## Repositories

| Repository | Visibility | Content |
|---|---|---|
| `es2` (this one) | public | Source, build, tests and documentation. No game data: nothing from the CD except the generated tables listed in [CLAUDE.md](CLAUDE.md) |
| `es2-data` | private | Oracle goldens, launch sets and the recorded inputs of tests and tools. Tests that need it read `ES2_DATA_REPO` and skip without it |
| `es2-dependencies` | local, not versioned | The pinned tools and the reference CD image ([docs/dependencies.md](docs/dependencies.md)) |

The game data comes from the user's copy of the reference CD
([docs/reference_version.md](docs/reference_version.md)); the setup scripts extract it into
`build/extracted/cd` (`ES2_DATA_ROOT`).

## Branches

| Branch | Content |
|---|---|
| `original` | The original game as a portable installation on modern Windows: installation from the CD, the `ddraw.dll` video proxy, the IV32 binding |
| `reconstructed` | `original` plus the behaviour-identical reimplementation with its documentation and tests |
| `main` | `reconstructed` plus the WebAssembly build and the open work |

New work lands on `main`. `es2-data` has a single `main` branch with the goldens and recorded inputs.

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

- The file hand-off between shell and simulation is the process boundary: each side can be
  original or own, as long as it reads and writes the same files and arguments.
- `core/` holds host-independent implementations of DBSIM functions, used by two hosts:
  the `ddraw.dll` proxy loaded by the original DBSIM, which detours functions after checking
  module hash and byte signatures (`hook/`, modes `original`/`replaced`/`compare`), and
  `es2sim`, which maps a guest image at the original addresses and binds the original call
  sites to own code through service rows (`sim/dispatch`).
- DBSIM and VSHELL data tables are compiled in as generated sources (`sim/data`,
  `shell/data`); no own executable reads an original executable.
- `formats/` reads the original files through a read-only VFS; `sim/oracle` provides virtual
  time, tick records, the input journal and memory watches to both hosts.
- Timing follows the original: `GetTickCount` pacing with a 40 ms minimum and elapsed-time
  scaling, no fixed tick ([timing.md](docs/reverse-engineering/timing.md)). The simulation
  never reads platform time. Randomness is the original lagged generator
  ([rng.md](docs/reverse-engineering/rng.md)).

## Status

The played chain `es2launch → es2shell → es2sim` executes no original instruction and loads
no game DLL. The same code runs in current Chrome and Firefox as WebAssembly
([web_build.md](docs/web_build.md)); the site is built locally from the user's CD and
nothing of it is committed.

Known differences, each documented next to its contract: the SDL audio flush
([audio.md](docs/reverse-engineering/audio.md)), the modal pump's timing and the dead key
([main_loop.md](docs/reverse-engineering/main_loop.md)), the environment copy
([rtl_imports.md](docs/reference-game/rtl_imports.md)), refusals instead of the original's
crashes ([own_process.md](docs/reference-game/own_process.md)), and the web build's
differences ([web_build.md](docs/web_build.md) §18). Original faults that look like bugs
(the Razor's pitch lock, the C1_01 debrief text shift, the broken gradients) are reproduced.

## Verification

| Check | What it proves |
|---|---|
| In-vitro differential | Each hook and service equals the original machine code on randomised cases |
| Mutations | Each hook has a mutation the differential catches |
| Oracle suites | Scripted missions in the original with virtual time match their goldens with all hooks replaced |
| Compare mode | Own and original code agree in the same running game |
| Replay and frame tests | `es2sim` reproduces every suite's tick log, watches, return files and frames |
| Shell in vitro | The own shell's files and screens equal VSHELL's |
| Wasm, site and browser tests | The web build reproduces the native runs under Node, over the built site and in the browsers |

Details: [oracle_regression.md](docs/reference-game/oracle_regression.md),
[web_build.md](docs/web_build.md) §16.

## Layout

| Directory | Contents |
|---|---|
| `core/` | Own implementations of DBSIM functions, C17, headless |
| `sim/` | The own DBSIM process `es2sim` |
| `shell/` | The own VSHELL: `es2shell`, `es2menu` |
| `launcher/` | The own `ES.EXE`, `es2launch` |
| `rtl/` | The own `CW3220.DLL` |
| `media/` | In-process Indeo 3.2 decoder binding |
| `formats/` | Readers for the original file formats and a read-only VFS |
| `hook/` | Signature-checked detours |
| `platform/` | Host OS layer for Windows and wasm |
| `web/`, `tools/web/` | The web build's page and workers; site builder and media conversion |
| `tools/reference-game/` | Proxy, in-vitro harness, installation and oracle tooling |
| `tools/` | Dependency setup, `check.ps1`, worker helpers, pre-commit guard |
| `bazel/` | Macros, platforms, toolchains |
| `data/` | Hash lists of the reference CD for the pre-commit guard |
| `docs/` | Formats, reverse-engineering notes, verification documents |
| `build/` | Ignored: Bazel output, extracted CD, installations |

## Build and test

Dependencies: [docs/dependencies.md](docs/dependencies.md). The system Bazel must match
`.bazelversion`. `es2-dependencies` and `es2-data` sit beside the checkout;
`tools/write-local-bazelrc.ps1` writes the ignored `local.bazelrc` with this machine's paths
(output in `build/bazel`, `ES2_DATA_ROOT`, `ES2_DATA_REPO`). Tests that need the data skip
without it.

```powershell
pwsh tools/write-local-bazelrc.ps1              # once per checkout (-NoData without the CD)
bazel test //...                                # quick tier
bazel test //tools:scripts                      # PowerShell and Python tooling
bazel test //sim:frames_all                     # es2sim frames against DBSIM's
bazel build //tools/reference-game/proxy:ddraw.dll
pwsh tools/check.ps1 -Live -OwnShell -OwnSim    # tests, oracle run and the own chain
pwsh tools/web/make-site.ps1                    # the web site in build/web/site
```

Installation, start and oracle runs: [setup.md](docs/reference-game/setup.md).
