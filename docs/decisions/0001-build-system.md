# 0001: Bazel as the build system

Bazel builds every target of the repository for Windows x86 (default) and
x86_64 with the local LLVM-MinGW toolchain. No CMake fallback is kept.

## Configuration

- `MODULE.bazel` pins three Bazel Central Registry modules (`rules_cc`,
  `platforms`, `bazel_skylib`); `MODULE.bazel.lock` is committed and
  `.bazelrc` enforces it (`--lockfile_mode=error`).
- `@llvm_mingw` exposes the LLVM-MinGW SDK from `es2-dependencies`. Compiler
  wrappers, headers, import libraries and compiler-rt are declared action
  inputs; tools are SDK paths, not PATH lookups. Two toolchains select the
  i686 and x86_64 drivers; `.bazelrc` selects `//bazel/platforms:windows_x86`
  as target and `windows_x86_64` as host.
- `BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1`: MSVC never compiles or links. The
  strict action environment sets PATH to the SDK's `bin` and `System32`
  (an empty PATH is rejected by Bazel's Windows test launcher); CC and CXX are empty.
- C compile actions use `-std=c17 -Wall -Wextra -Werror -O2`; archives use
  LLVM's deterministic `rcsD`.
- `@sdl3` imports the SDL3 MinGW development archive. `cc_import` rejects a
  `.dll.a` name, so the repository rule creates `.lib` aliases of the unchanged
  COFF archives; the matching DLL is copied beside each executable.
- All server state, caches and outputs live under `build\bazel`;
  `--symlink_prefix=/` avoids convenience symlinks, so products are found with
  `bazel cquery <target> --output=files` or `bazel info bazel-bin`.

## Host inputs that are not hermetic

- `//tools/reference-game/proxy:system_exports` runs `generate_exports.py` on
  the host's `C:\Windows\SysWOW64\ddraw.dll` with the shared Python
  (`local`, `no-remote`, `no-sandbox`). The proxy links the generated
  definition with `--no-insert-timestamp`, image base `0x10000000`.
- `@es2_data` and `@es2_recorded` track the extracted CD under `ES2_DATA_ROOT`
  and the recorded suite outputs in `ES2_DATA_REPO` by content; without them
  the groups hold only a status file and the real-data tests skip.
- The SDK paths are absolute and Windows supplies its runtime; no remote or
  arbitrary-host hermeticity is claimed.

Commands: [README.md](../../README.md), [platform/README.md](../../platform/README.md);
dependency pins: [dependencies.md](../dependencies.md).
