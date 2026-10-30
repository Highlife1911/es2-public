# EarthSiege II: the original game on modern Windows

The original EarthSiege II (German GoldGames 1.11) as a portable installation
on Windows 10 x64, built from the original CD. No original file is changed and
nothing is installed into the system. A small own `ddraw.dll` proxy forwards
DirectDraw, lets the Indeo intro movies play inside the game's process and
guards a mission-map fault of the original `VSHELL.EXE`. Rules:
[CLAUDE.md](CLAUDE.md).

This branch is complete. The own reimplementation is on `reconstructed`, the
current plan on `main`.

No game data lives in this repository. The reference CD image and the pinned
tools are in `es2-dependencies` ([docs/dependencies.md](docs/dependencies.md)).
Launch records and media logs go to the sibling repository `es2-data`. The
evidence paths `es2-data\logs\…` named in the docs are on its `reconstructed`
branch.

## Layout

| Directory | Contents |
|---|---|
| `tools/reference-game/setup/` | Installation, media validation and start scripts |
| `tools/reference-game/proxy/` | The `ddraw.dll` proxy: DirectDraw forwarding, MCI redirection, mission-map lifetime guard |
| `tools/reference-game/helpers/` | `probe_media`, the decode probe of `validate-media.ps1` |
| `media/` | In-process IV32 (Indeo 3.2) decoder binding ([docs](docs/reference-game/media_compatibility.md#the-iv32-binding-library)) |
| `tools/` | Dependency setup and the pre-commit guard (`git-hooks/`) |
| `bazel/` | Target platforms and the LLVM-MinGW toolchain |
| `data/` | Hash lists of the reference CD for the pre-commit guard |
| `docs/` | Reference version, dependencies, installation, media compatibility |
| `build/` | Ignored, generated: Bazel output root and caches (`bazel/`), extracted CD and data ISO (`extracted/`), game installations (`installs/`) |

## Install and start

Details: [setup.md](docs/reference-game/setup.md).

```powershell
# Data track of the verified CD image, then its files with 7-Zip into build\extracted\cd
& <es2-dependencies>\python\python.exe -I tools\reference-game\setup\extract_data_track.py <raw image> build\extracted\earthsiege2-data.iso
.\tools\reference-game\setup\prepare-native.ps1 -InstallBundledRuntime -Language German
.\tools\reference-game\setup\validate-media.ps1
.\tools\reference-game\setup\configure-media.ps1 -VshellMeasures -Apply
.\START_ES2.cmd
```

## Build and test

The system Bazel must match `.bazelversion`. `.bazelrc` enforces the lock file and
imports the git-ignored `local.bazelrc`, which `tools/write-local-bazelrc.ps1`
writes: the output root in `build/bazel`, the dependency directory and
`ES2_DATA_ROOT` (the extracted CD; the decode test that needs it skips without
it).

```powershell
& tools\write-local-bazelrc.ps1                      # once per checkout
bazel test //...                                     # IV32 binding and mission-map guard fixtures
bazel build //tools/reference-game/proxy:ddraw.dll   # the proxy
```
