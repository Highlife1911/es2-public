# ES2 local dependencies

Install location: `<es2-dependencies>`, the `es2-dependencies` folder beside the
checkout (`ES2_DEPENDENCIES` overrides it). Never use tools, files or
dependencies from other projects. System tools are separate from other
project directories.

Pinned distributions and upstream SHA-256 checksums are recorded in
`tools/setup-dependencies.ps1`. This installer downloads official release
archives, verifies their checksums, and refuses to overwrite existing tools.
`manifest.json` in the dependency directory records what it installed.

| Tool | Distribution |
| --- | --- |
| LLVM-MinGW | 20260922, UCRT, Windows x64 host; Clang 23.1.2, supports x86 targets |
| Bazel | 9.3.0, system installation on PATH (see [Bazel](#bazel)) |
| SDL3 | 3.4.18, official MinGW binaries for x86/x64 (see [SDL3](#sdl3)) |

All seven reference-game build/test scripts default to
`<es2-dependencies>\llvm-mingw\bin`. Disassembly comes from
`llvm-objdump` of the pinned LLVM-MinGW.
No system PATH, registry or installed game files are changed.
Python is pinned under `python\` (below); PowerShell 7 is the system
installation; 7-Zip is the existing system installation.

## Python and PowerShell 7

- **Python 3.12.14** (Windows x64, `MSC v.1944`) in
  `<es2-dependencies>\python` is a copy of a Python folder (no
  download), standard library only: no `Scripts`, `tcl`, `Lib\site-packages`
  contents or `__pycache__` (our scripts import only the standard library).
  1,029 files, 43 MB. `tools/setup-dependencies.ps1
  -Python` verifies `python.exe` (SHA-256
  `10d845f50a2af64e3500bb2fcb348b5bc98a75d8ddada63e45ba1da6a1fc79d1`),
  `python312.dll` (`c19758a4dfccc35b5f4da513945dfab85e70bed8cdbf4bc25587662fa7f457d2`)
  and the tree digest of `tools/python_tree_digest.py`
  (`63c034f113f0013c8f86d4aa326d17442e3a45a29ed9724f4486c8853d23cd28`);
  with `-PythonSource <python folder>` it recreates the copy when it is
  missing.
- **PowerShell 7** is the system installation (Microsoft Store package
  7.6.6, app alias `%LOCALAPPDATA%\Microsoft\WindowsApps\pwsh.exe`; not
  copied). `START_ES2.cmd` uses that alias and falls
  back to `pwsh.exe` on PATH.

Bazel reads both paths from `bazel/platforms/host_tools.bzl` (`HOST_PYTHON`,
`HOST_PWSH`); `MODULE.bazel` repeats `HOST_PYTHON` for `@proxy_host`
because it cannot load Starlark files. The `-Python` defaults of the
reference-game scripts point to the pinned Python.

## REA MCP server (approved 2026-10-10)

[REA](https://github.com/morluto/rea) ("Reverse Engineer Anything") is a
Claude MCP server for static analysis. It is **not** part of
`es2-dependencies` and not a build or test input; the usage rules are in
[CLAUDE.md](../CLAUDE.md) §2. It uses Ghidra only as its analysis backend;
there is no Ghidra project, and `llvm-objdump` listings are canonical.

| Component | Version | Location (under the MCP folder) | Verification |
| --- | --- | --- | --- |
| `rea-agents` (npm) | 6.3.0 | `rea\node_modules\rea-agents` | npm integrity hashes in `rea\package-lock.json` |
| Node.js | 24.21.0, win-x64 zip | `rea_dependencies\node` | nodejs.org `SHASUMS256.txt` |
| Temurin JDK | 21.0.12.1+1, Windows x64 zip | `rea_dependencies\jdk` | SHA-256 `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` |
| Ghidra | 12.1.4 PUBLIC 20260921 | `rea_dependencies\ghidra` | SHA-256 `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` |

- Launcher: `rea.cmd` (`rea.cmd mcp` is the MCP entry; the
  Claude registration is `cmd /c rea.cmd mcp`). It sets
  `USERPROFILE`, `TEMP`/`TMP`, `GHIDRA_INSTALL_DIR`, `JAVA_HOME` and
  `GHIDRA_HEADLESS_MAXMEM=8G` for the REA process only, so REA's `~/.rea`
  state lives in `rea_dependencies\home`.
- Ghidra's settings, cache and temp directories point to
  `rea_dependencies\ghidra-userdata` (`support\launch.properties`, original
  kept as `launch.properties.orig`).
- No system PATH, registry or global npm change; the only change outside
  the MCP folder is the `rea` entry in `%USERPROFILE%\.claude.json`.
- Health check: `rea.cmd doctor --provider ghidra` (12.1.4, JDK 21, native
  decompiler, Windows native authority); a stdio `initialize` + `tools/list`
  returns 151 tools.
- `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, as in
  [reference_version.md](reference_version.md)) opens as x86 PE with the
  Ghidra provider; `procedure_pseudo_code`, `procedure_callers`, `read_bytes`
  and `close_binary` work on a scratch copy. The pseudocode drops call
  arguments and registers, so the listing stays the source for register
  contracts.
- Scope: REA's Windows Ghidra support is experimental and read-only. Its own
  limitation notes say it admits native x86/x86-64 PE **applications** on
  local NTFS and rejects DLLs, so `VSHELL.EXE` and `ES.EXE` should work like
  `DBSIM.EXE`, while `CW3220.DLL`, `BWCC32.DLL`, `SOS9503.DLL` and the 16-bit
  NE `SOSLIBS3.DLL` are expected not to be admitted (not tried). Annotations
  made through REA are not persisted.

## Bazel

The system Bazel 9.3.0 is `bazel.exe` on PATH (SHA-256
`64aff24f986df98291f4da2795fd4b3e831c0af3f74bd7aa8eb56c49cb28b1e3`; the
directory also holds Bazelisk, which the project does not use). The
repository's `.bazelversion` says 9.3.0; `Es2Bazel.psm1`
(`Resolve-Es2Bazel`) and `check_mutations.ps1` take `bazel` from PATH (or
`ES2_BAZEL`) and refuse a binary whose `--version` differs from
`.bazelversion`; `tools/setup-dependencies.ps1` does not download Bazel and
only reports the system version. The git-ignored `local.bazelrc` that `tools/write-local-bazelrc.ps1`
writes (`.bazelrc` imports it) sets `--output_user_root` to
`build/bazel` of the main checkout, so server, embedded JDK and
caches stay in the es2 checkout's ignored `build\bazel`.

The build covers Windows x86/x64 C17 targets; see
[the decision](decisions/0001-build-system.md) and
[platform commands](../platform/README.md).

**Bazel modules.** Bazel fetches modules from the Bazel Central Registry on
demand; no vendor directory. Pins: `rules_cc` 0.2.26, `platforms` 1.1.0,
`bazel_skylib` 1.9.2; transitive versions are recorded in
`MODULE.bazel.lock` (regenerate with `bazel mod deps --lockfile_mode=update`
when the Bazel version changes the built-in module versions). The repository
cache lives under the output root (`build\bazel\cache`). `rules_cc` 0.2.26
pulls a large module graph (protobuf, rules_java, rules_python, …); only
repositories needed by the requested targets are fetched (a minimal
`cc_library` needs about 250 MB of downloads), while an untargeted
`bazel vendor` fetches everything (JDKs, protoc for all platforms; over
19 GB). Never run `bazel vendor`/`bazel fetch` without target patterns.
`%USERPROFILE%\\_bazel_<user>` belongs to another project and is not used.

## SDL3

`tools/setup-dependencies.ps1 -Platform` invokes the separate pinned SDL installer
`tools/setup-platform-dependencies.ps1`. It verifies the official release hash
and refuses an existing installation directory; `platform-manifest.json`
records the archive. There is no global PATH change. The native files live in
`sdl3-3.4.18`; `bazel/toolchains/sdl3/repository.bzl` exposes them as `@sdl3`.

| Input | SHA-256 |
| --- | --- |
| `SDL3-devel-3.4.18-mingw.zip` | `cd98158c8d025a816f430a600b4e9526524e6219ee884ec72bd498b80fd3bf58` |

The hash matches the [official release asset metadata](https://github.com/libsdl-org/SDL/releases/expanded_assets/release-3.4.18).

## Fresh-clone check

`tools/check-fresh-clone.ps1` answers "does a new clone of `es2` build and pass
its tests on a machine that has the pinned dependencies but not the CD data?".
It runs `git clone --no-hardlinks` from the local repository (`-Source`, default
`<es2>`, never a URL; only committed files are cloned) into a temporary
directory, runs the clone's `tools/setup-dependencies.ps1 -VerifyOnly`, then
`bazel test //... --test_env=ES2_DATA_ROOT= --repo_env=ES2_DATA_ROOT= --keep_going`
with its own output base (default `<output root>\t-03-fresh`, plus `-contents`;
the shared repository and disk caches of `local.bazelrc` stay in use), writes
`report.md`, `summary.json`, the build event file and the logs to
`-ReportDirectory`, shuts the Bazel server down and deletes the clone, also
after errors. The output base stays for `tools\clean-worker-bazel.ps1
t-03-fresh`. Exit status 0 = dependencies verified and every test passed;
1 = something is missing or failed; 2 = refused.
`//tools:fresh_clone_script_test` covers the argument handling, the report and
the cleanup with a fake `bazel.cmd`.

`setup-dependencies.ps1 -VerifyOnly` (also `-Platform -VerifyOnly`) only reads
and hashes: system 7-Zip (advisory), Bazel against `.bazelversion`, the
PowerShell 7 (`ES2_PWSH`, else `pwsh.exe` on PATH), the pinned Python (hashes and tree
digest), LLVM-MinGW (`clang.exe` version, or the kept archive's hash),
`ddraw.dll` and the SDL3 tree. It prints
`[OK|MISSING|MISMATCH|WARN] item - detail`, exits 1 for MISSING/MISMATCH and
never downloads, copies or writes.

Without data, the tests that read `ES2_DATA_ROOT` skip (printed
`SKIP`/`skipped`), wholly or only their real-data cases: every
`differential_*_test` of `tools/reference-game/invitro`, and real-data targets
in `shell`, `tools/reference-game/proxy`, `formats` and `shell-invitro`. The
rest (core, formats, shell logic, hook adapters, fixtures, Python script tests)
needs no data. `//shell:chain_real_test` starts a stand-in `dbsim.EXE` even
without data.

What a fresh machine needs:

- Windows x64 with git, PowerShell 7 (`ES2_PWSH`, else `pwsh.exe` on PATH), the system Bazel named in `.bazelversion`
  (9.3.0) on PATH, `C:\Windows\SysWOW64\ddraw.dll`;
  7-Zip only for the installers.
- `<es2-dependencies>` with `python` (the pinned copy; there is no
  download, `setup-dependencies.ps1 -Python -PythonSource` needs a Python 3.12.14
  folder), `llvm-mingw` and `sdl3-3.4.18`. `tools/write-local-bazelrc.ps1` writes
  `local.bazelrc` with the output user root, repository cache and disk cache
  under `build\bazel`, the dependency directory, the compiler PATH and the data
  roots (`-NoData` leaves the data roots out).
- Network on the first build: Bazel fetches the BCR modules and its JDK,
  unless the repository cache already holds them.
- The LLVM-MinGW and SDL3 archives are not kept, so their pinned hashes cannot be
  re-verified after installation; `-VerifyOnly` checks the tool version instead.
- No CD data: `ES2_DATA_ROOT` may stay empty for the build and the tests above;
  the in-vitro differential tier, the signature check and everything live need
  `build\extracted\cd`.

## Real-data test inputs: `@es2_data`

The real-data tests read the extracted CD at `ES2_DATA_ROOT` (`--test_env` in
`local.bazelrc`); that path string alone is not the bytes behind it. The repository
rule `es2_data_inputs` in `tools/reference-game/data_inputs.bzl` (wired in
`MODULE.bazel` like `proxy_host_inputs`, as repository `@es2_data`) reads the
same root through `--repo_env=ES2_DATA_ROOT` (`local.bazelrc` sets both lines to
`build/extracted/cd`; keep them equal) and exposes two groups:

- `@es2_data//:reference_binaries`: the rows of `data/reference_hashes.txt`
  below `extracted/cd/` (`VER95/DBSIM.EXE`, `VER95/VSHELL.EXE`, `VER95/ES.EXE`,
  `VER95/SOS9503.DLL`, `CW3220.DLL`, `BWCC32.DLL`, `SOSLIBS3.DLL`,
  `SETUP.EXE`), each checked against its pinned SHA-256 by the helper
  `tools/reference-game/data_inputs.py` (pinned host Python) and copied into
  the repository, i.e. into Bazel's output base, never into the source tree;
  plus `status.txt`.
- `@es2_data//:cd`: the above plus `cd.sha256`, the SHA-256 of every CD file
  (865 files, about 1 s to hash).

The in-vitro tests (`differential_*`) and the proxy `*_reference_test` modes
list `reference_binaries`; the other tests that read `ES2_DATA_ROOT` list `cd`.
The tests still read the files at `ES2_DATA_ROOT`; the labels only put the
content digests into the action key. The rule watches the CD tree
(`watch_tree`), the pinned table and the helper, so it refetches only when
one of them or `ES2_DATA_ROOT` changes, not after a server restart.

- **No data**: with `ES2_DATA_ROOT` unset or empty, the path missing, or no
  `VER95/DBSIM.EXE` below it (or below its `extracted/cd`), the rule does not
  fail: both groups contain only `status.txt` (`absent: <reason>`) and an empty
  manifest, and the tests skip. A fresh clone without the default path
  therefore builds and skips; `check-fresh-clone.ps1` passes
  `--repo_env=ES2_DATA_ROOT=` next to `--test_env=ES2_DATA_ROOT=` so the check
  also simulates a machine without data where the path exists.
- **Wrong data**: a pinned binary with another hash fails the fetch with both
  hashes and the advice to fix or empty both flags.
- **Tags**: `formats:player_mec_real_test` and `formats:vfs_dbsim_real_test`
  enter the shared disk cache; the Python real-data tests
  (`shell:prepare_real`, `loadout_real`, `campaign_cli`,
  `shell-invitro:real`, `results`, `sweep`) keep the `local` default of
  `host_python_test`. `no-cache` because they read inputs `@es2_data` does
  not track: `formats:sav_real_test` (save snapshot under `es2-data/logs`),
  `shell:chain_real_test` and `shell:results_corpus_real_test` (recorded
  suite outputs under `es2-data`); the latter two and the script tests that
  read the CD (`setup:suite_scripts_test`, `proxy:entry_probes_scripts_test`)
  depend on the labels anyway.
  `//tools/reference-game:data_inputs_test` covers the helper on synthetic
  files.
- **Caching trap**: in Bazel 9.3 `no-cache` and `local` keep a test out of
  the disk cache, but the output base's own action cache still records it, so
  such a test shows `(cached)` on a second run in the same output base, also
  after `bazel shutdown`. It reruns only in a fresh output base or after a
  change of its dependencies.
