# Dependencies

Everything the build, the tests and the setup scripts need lives in the local folder
`es2-dependencies`, outside the repository and not versioned. Tools, files or build outputs
of other projects are never used; no system PATH, registry or installed game file is changed.
New dependencies need the owner's approval ([CLAUDE.md](../CLAUDE.md) §3).

The setup scripts pin every distribution with its upstream SHA-256, download only official
release archives, verify them, refuse to overwrite an existing tool and record what they
installed in a manifest. `-VerifyOnly` only reads and re-hashes.

| Dependency | Version | Installed by |
|---|---|---|
| Reference CD image | EarthSiege II GoldGames 1.11, raw image with audio ([reference_version.md](reference_version.md)) | provided by the user ([CD image](#cd-image)) |
| LLVM-MinGW | 20260922, UCRT, Windows x64 host; Clang 23.1.2, x86 targets | `tools/setup-dependencies.ps1` |
| Python | 3.12.14, standard library only | `tools/setup-dependencies.ps1 -Python` (copy of a local Python) |
| SDL3 | 3.4.18, official MinGW binaries for x86/x64 | `tools/setup-platform-dependencies.ps1` |
| emsdk | 6.0.12 with Emscripten 6.0.12, Node.js 24.19.0, Python 3.13.3 | `tools/setup-web-dependencies.ps1` |
| FFmpeg | 9.0.2, gyan.dev full build | `tools/setup-web-dependencies.ps1` |
| Bazel | 9.3.0, system installation on PATH | by hand |
| PowerShell 7, 7-Zip, git | system installations | by hand |
| Bazel modules | `rules_cc`, `platforms`, `bazel_skylib`, `zstd` from the Bazel Central Registry | Bazel, pinned in `MODULE.bazel` and `MODULE.bazel.lock` |

Disassembly is `llvm-objdump` of the pinned LLVM-MinGW. Bazel reads the tool paths from
`bazel/platforms/host_tools.bzl` (`HOST_PYTHON`, `HOST_PWSH`, `HOST_NODE`, `HOST_FFMPEG`),
which takes them from the `@es2_host` repository (`bazel/platforms/host_repository.bzl`).

## Local paths

The repository holds no machine paths. `es2-dependencies` and `es2-data` sit beside the main
checkout; `ES2_DEPENDENCIES` and `ES2_DATA_REPO` override them, `ES2_PWSH` the PowerShell 7
otherwise found on PATH. The scripts find the main checkout through git (also from a worktree;
`Get-Es2Layout` in `tools/reference-game/setup/CloneGuard.psm1`). Bazel gets the paths from the
git-ignored `local.bazelrc`, which `.bazelrc` imports and `tools/write-local-bazelrc.ps1`
writes: output user root, repository cache and disk cache in the main checkout's `build/bazel`,
`ES2_DEPENDENCIES`, `ES2_PWSH`, the compiler PATH, and `ES2_DATA_ROOT` / `ES2_DATA_REPO` (not with
`-NoData`). `tools/worker.ps1` copies it into a new worktree; another worktree or clone runs
the script or copies the file.

## CD image

The game data comes only from the user's copy of the reference image
`EarthSiege2_Freeware_GoldGames_1r11_withAudio.iso`
([reference_version.md](reference_version.md)), kept in `es2-dependencies/archive`.
`tools/reference-game/setup/extract_data_track.py` writes its data track to
`build/extracted/earthsiege2-data.iso`, 7-Zip extracts the files to `build/extracted/cd`
(`ES2_DATA_ROOT`), and the web build cuts the CD music from the raw image
([setup.md](reference-game/setup.md#prepare-the-original-media)). The hash lists in `data/`
identify every file; nothing from the CD is committed.

## Bazel

The system Bazel must match `.bazelversion`; `Es2Bazel.psm1` and `check_mutations.ps1` take
`bazel` from PATH (or `ES2_BAZEL`) and refuse another version. `local.bazelrc` puts the
output user root, repository cache and disk cache under `build/bazel`; `.bazelrc` enforces the
lock file
(regenerate with `bazel mod deps --lockfile_mode=update` after a Bazel update). Only the
repositories the requested targets need are fetched; never run `bazel vendor` or
`bazel fetch` without target patterns. Build set-up:
[build_system.md](build_system.md).

**Caching trap:** `no-cache` and `local` keep a test out of the disk cache, but the output
base's action cache still records it: a second run in the same output base shows `(cached)`.
It reruns only in a fresh output base or after a dependency changes.

## Web build

`tools/setup-web-dependencies.ps1` (also `setup-dependencies.ps1 -Web`) keeps the verified
archives in `downloads/`, feeds them to `emsdk install/activate` so emsdk fetches nothing, and
checks the result by linking test programs against the frozen cache. The Windows build does
not need any of this.

- **Frozen cache.** The system libraries ship prebuilt; with `EM_FROZEN_CACHE=1` (set by the
  Bazel wrappers) emcc writes nothing under `emsdk` and needs no network. emcc needs
  `EM_CONFIG`, `EMSDK_PYTHON` and `EMSDK_NODE`.
- **Traps of emcc 6.0.12.** `-sSTACK_FIRST` is an error on the command line;
  `-sGLOBAL_BASE` takes a decimal number (`8388608`, not `0x800000`).
- **FFmpeg** must have libdav1d among the decoders and libopus and libsvtav1 or libaom-av1
  among the encoders.

## Real-data test inputs

Tests read the extracted CD at `ES2_DATA_ROOT` and the `es2-data` checkout at
`ES2_DATA_REPO`; `local.bazelrc` sets both as `--test_env` and `--repo_env` (keep them equal).
The repository rules in `tools/reference-game/data_inputs.bzl` put their content digests into
the action keys:

- `@es2_data//:reference_binaries`: the executables and DLLs of `data/reference_hashes.txt`,
  each checked against its hash; used by the in-vitro tests and the proxy reference tests.
- `@es2_data//:cd`: the above plus a digest over every CD file; the other real-data tests.
- `@es2_recorded//:outputs`: digests of `oracle/golden` and `oracle/launch` in `es2-data`.

Without data the groups hold only `status.txt` and the tests skip; a pinned binary with
another hash fails the fetch. Tests that also read untracked inputs under `es2-data` are
tagged `no-cache`.

## Fresh-clone check

`tools/check-fresh-clone.ps1` clones the local repository into a temporary directory, gives it a
`local.bazelrc` without data, runs
`setup-dependencies.ps1 -VerifyOnly` and `bazel test //... --keep_going` with
`ES2_DATA_ROOT` emptied in its own output base, and writes a report. A fresh machine needs
Windows x64 with git, PowerShell 7, Bazel on PATH, `ddraw.dll` in `SysWOW64` and
`es2-dependencies` with `python`, `llvm-mingw` and `sdl3-3.4.18`; the first build needs
network for the Bazel modules. Without CD data the real-data tests skip.

## REA MCP server

[REA](https://github.com/morluto/rea) is an MCP server for static analysis with Ghidra as its
backend, used as an analysis aid only ([CLAUDE.md](../CLAUDE.md) §3). It is not part of
`es2-dependencies` and not a build or test input; `llvm-objdump` listings stay canonical. It
opens the CD's executables as x86 PE (not DLLs); its pseudocode drops call arguments and
registers, so the listing stays the source for register contracts.
