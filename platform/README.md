# SDL3 platform smoke program

`smoke.c` is one C17 translation unit. It opens an 800 x 450 SDL window, uses logical coordinates when resized, and draws three colored rectangles, a moving frame marker, and SDL's built-in debug text. It reads no game data or other assets.

SDL's callback entry points own the event loop. Every initialization and drawing operation that is required for the result is checked. VSync is optional and reports a diagnostic if unavailable. Closing the window exits the interactive program. This is a build and rendering check, not the game implementation.

## Setup

Run the PowerShell commands below from `<es2>`. They use the existing Bazel and LLVM-MinGW installation and system 7-Zip at `C:\Program Files\7-Zip\7z.exe`. The approved SDL3 version is 3.4.18; its checksum is pinned in `tools/setup-platform-dependencies.ps1`.

One-time provisioning for a fresh dependency directory (the configured machine already has it; the setup refuses an existing destination directory):

```powershell
Set-Location '<es2>'
& 'tools\setup-dependencies.ps1' -Platform
```

`-Platform` downloads and verifies the official SDL MinGW development archive.

## Build, run and verify

```powershell
& bazel build //platform:smoke_windows --lockfile_mode=error
& bazel test //platform:smoke_windows_test --nocache_test_results --lockfile_mode=error
& bazel test //platform:smoke_windows_test --platforms=//bazel/platforms:windows_x86_64 --nocache_test_results --lockfile_mode=error
& bazel run //platform:smoke_windows -- --smoke-test
& bazel run //platform:smoke_windows
```

Windows x86 is the default. Add `--platforms=//bazel/platforms:windows_x86_64` before the argument separator to run the x64 executable. The test opens the program's own window, so it needs a usable local Windows graphics session; it is tagged local/no-remote.

Bazel outputs stay beneath `build\bazel`. `.bazelrc` disables convenience output symlinks; query the actual files instead of assuming a workspace `bazel-bin` directory:

```powershell
& bazel cquery //platform:smoke_windows --output=files
```

## Bounded smoke mode

Pass `--smoke-test` to the executable. It returns success after eight complete frames; a drawing error, initialization error, or early close returns failure. Standard output contains these machine-readable records:

```text
ES2_SMOKE_READY width=800 height=450 headers=<SDL_VERSION> runtime=<SDL_GetVersion()>
ES2_SMOKE_PASS frames=8
```

The PASS record is written after successful window/renderer creation, eight checked calls through the drawing path including `SDL_RenderPresent`, and destruction of the renderer and window. Standard error uses `ES2_SMOKE_FAIL` for a failure. A test must check both exit code zero and the exact PASS record. A normal interactive close writes `ES2_SMOKE_STOP`, which is not a test pass. Header and runtime version numbers are reported separately so a DLL version mismatch stays visible.

## Build integration contract

The target uses the approved SDL3 3.4.18 MinGW headers, import library and architecture-matched DLL with the LLVM-MinGW compiler. Its runtime target puts the DLL beside the executable. Compile as C17 with `-Wall -Wextra -Werror`. No additional SDLmain library is needed for SDL3.

## API references

API contracts used:

- [SDL callback lifecycle](https://wiki.libsdl.org/SDL3/README-main-functions).
- [Window and renderer creation](https://wiki.libsdl.org/SDL3/SDL_CreateWindowAndRenderer) and [logical presentation](https://wiki.libsdl.org/SDL3/SDL_SetRenderLogicalPresentation).
- [Debug text](https://wiki.libsdl.org/SDL3/SDL_RenderDebugText), [rectangle filling](https://wiki.libsdl.org/SDL3/SDL_RenderFillRect), and [presentation](https://wiki.libsdl.org/SDL3/SDL_RenderPresent).
