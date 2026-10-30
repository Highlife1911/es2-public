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

The Bazel toolchain `bazel/toolchains/llvm_mingw` uses
`<es2-dependencies>\llvm-mingw`. No system PATH, registry or installed
game files are changed. Python is pinned under `python\` (below);
PowerShell 7 and 7-Zip are the existing system installations.

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
  copied). `START_ES2.cmd` uses that alias and falls back to `pwsh.exe` on
  PATH.

Bazel reads the Python path from `bazel/platforms/host_tools.bzl`
(`HOST_PYTHON`); `MODULE.bazel` repeats it for `@proxy_host` because it
cannot load Starlark files.

## Bazel

The system Bazel 9.3.0 is `bazel.exe` on PATH (SHA-256
`64aff24f986df98291f4da2795fd4b3e831c0af3f74bd7aa8eb56c49cb28b1e3`; the
directory also holds Bazelisk, which the project does not use). The
repository's `.bazelversion` says 9.3.0; `Es2Bazel.psm1`
(`Resolve-Es2Bazel`) takes `bazel` from PATH (or `ES2_BAZEL`) and refuses a
binary whose `--version` differs from `.bazelversion`;
`tools/setup-dependencies.ps1` does not download Bazel and only reports the
system version. The git-ignored `local.bazelrc` that `tools/write-local-bazelrc.ps1`
writes (`.bazelrc` imports it) sets `--output_user_root` to
`build/bazel` of the main checkout, so server, embedded JDK and
caches stay in the es2 checkout's ignored `build\bazel`.

**Bazel modules.** Bazel fetches modules from the Bazel Central Registry on
demand; no vendor directory. Pins: `rules_cc` 0.2.26, `platforms` 1.1.0,
`bazel_skylib` 1.9.2; transitive versions are recorded in
`MODULE.bazel.lock` (regenerate with `bazel mod deps --lockfile_mode=update`
when the Bazel version changes the built-in module versions). The repository
cache lives under the output root (`build\bazel\cache`). `rules_cc` 0.2.26
pulls a large module graph; only repositories needed by the requested
targets are fetched, while an untargeted `bazel vendor` fetches everything
(over 19 GB). Never run `bazel vendor`/`bazel fetch` without target patterns.

## Dependency check

`setup-dependencies.ps1 -VerifyOnly` only reads and hashes: system 7-Zip
(advisory), Bazel against `.bazelversion`, the pinned Python (hashes and tree
digest), LLVM-MinGW (`clang.exe` version, or the kept archive's hash) and the
system `ddraw.dll` the proxy build reads. It prints
`[OK|MISSING|MISMATCH|WARN] item - detail`, exits 1 for MISSING/MISMATCH and
never downloads, copies or writes. The LLVM-MinGW archive is not kept, so its
pinned hash cannot be re-verified after installation; the check compares the
tool version instead.

The drive and paths are hard-coded in `.bazelrc`, `MODULE.bazel`,
`bazel/platforms/host_tools.bzl` and `bazel/toolchains/llvm_mingw/config.bzl`;
`-Destination` of the setup script only moves the installer, not the build.
The first build needs network access unless the repository cache already
holds the BCR modules and Bazel's JDK.
