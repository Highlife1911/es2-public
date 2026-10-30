# platform

## Host OS layer (`//platform:os`)

The only place where es2sim, es2shell and the launcher meet the operating system for
files, clocks, memory, the environment, INI files and the process end. The functions keep
the Win32 semantics, `GetLastError` included, so callers see the same results in both
builds ([web_build.md](../docs/web_build.md) section 7).

| File | Role |
|---|---|
| `include/es2/platform/os.h`, `src/os_common.c` | The interface and the shared parts |
| `src/os_win32.c` | Windows: the Win32 and CRT calls |
| `src/os_wasm.c`, `src/os_stream.c` | wasm: the `es2os` imports and CRT-like buffered streams over them |
| `src/os_node.js` | The JS host of the `es2os` imports over `node:fs` (Node runs) or a given file system (the browser worker) |
| `src/os_wasm_stubs.js` | Link-time names of the imports; never run |
| `include/es2/platform/ini.h`, `src/ini.c` | INI reader with the rules of `GetPrivateProfileStringW/IntW` for the builds without Win32 |

Tests: `ini_test` (against the API), `os_test`, `os_stream_test` and its wasm twin
(against the Windows C runtime); `os_calls`, a fixed script of file calls that
`//web/node:host_test` runs natively and as wasm and compares.

## SDL3 smoke program (`//platform:smoke_windows`)

`smoke.c` opens an 800 × 450 SDL window and draws a few shapes and debug text; it reads no
game data. `--smoke-test` returns after eight checked frames and prints
`ES2_SMOKE_PASS frames=8` (failures `ES2_SMOKE_FAIL` on stderr); the test checks exit code
and record. `//platform:smoke_windows_test` opens a real window, so it needs a local
graphics session (`local`, `no-remote`).

```powershell
bazel test //platform:smoke_windows_test
bazel test //platform:smoke_windows_test --platforms=//bazel/platforms:windows_x86_64
bazel run //platform:smoke_windows
```

SDL3 3.4.18 is installed by `tools/setup-dependencies.ps1 -Platform`
([dependencies.md](../docs/dependencies.md)).
