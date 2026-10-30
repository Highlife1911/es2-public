# Native color and intro compatibility

The proxy `ddraw.dll` (`tools/reference-game/proxy/media_proxy.c`) and the
static library `//media:iv32_bind` make the original game's colour mode and
intro movies work on the current Windows without touching a system file or an
original executable.

## Failures addressed

- **Colour warning.** VSHELL shows a colour warning box when
  `GetDeviceCaps(BITSPIXEL)` is not 8
  ([vshell_presentation.md](../reverse-engineering/vshell_presentation.md)).
  A 32-bit diagnostic measured BITSPIXEL=32, SIZEPALETTE=0 by default and
  BITSPIXEL=8, SIZEPALETTE=256 under the Windows 256COLOR compatibility
  layer; under the layer the warning is gone (confirmed by the owner).
- **Black intro.** `AVI/INTR_PT1.AVI`, `AVI/INTR_PT2.AVI` and the German
  `AVG` equivalents use IV32 (Indeo 3.2). `AVIFileOpen` succeeds, but
  `ICOpen(IV32)` and `AVIStreamGetFrameOpen` fail: the machine's 32-bit
  Drivers32 registry has no IV32 mapping.
- **Stale mission-map bitmap.** The original `VSHELL.EXE` faults after a saved
  game is loaded ([Mission-map lifetime guard](#mission-map-lifetime-guard)).

## Components and switches

| Measure | Needed by | Where it lives | Default | Switch |
|---|---|---|---|---|
| 256-colour layer (`__COMPAT_LAYER=256COLOR`) | VSHELL (colour warning box unless `BITSPIXEL` is 8) and, as recorded for the oracle goldens, original DBSIM | `start-native.ps1`, `start-own-launcher.ps1`, `start-timing-mission.ps1` | on | `start-native.ps1 -UseNativeColors` |
| DirectDraw forwarding | every process that loads the proxy | `media_proxy.c` | on | none |
| MCI import redirection (`hook_executable_mci`, `hook_mci_command`, `hook_mci_string`) | first-call trigger of `initialize_forwarding`; with `system_indeo` off it only forwards | `media_proxy.c` | on | none |
| IV32 binding in the proxy (`initialize_codec`) | `VSHELL.EXE`'s MCI `avivideo` movies; DBSIM's AVI code is not reachable ([file_io.md](../reverse-engineering/file_io.md)) | `media_proxy.c` over `//media:iv32_bind` | off | `[video] system_indeo=1` in `es2-oracle.ini` |
| Mission-map lifetime guard | `VSHELL.EXE` only (module name, image size and five code signatures are checked) | `proxy/mission_map_guard.h` | off (`GetPrivateProfileIntW(..., 0, ...)`) | `[compat] mission_map_lifetime=1` |

The two `[video]`/`[compat]` keys are the VSHELL-only measures; see
[VSHELL-only measures](#vshell-only-measures) for the scripts that write them.
The own shell and the own launcher run neither `ES.EXE` nor `VSHELL.EXE`
([shell/README.md](../../shell/README.md#the-process-boundary)); the own shell
binds IV32 in its own process (`shell/src/win/mci_player.c`).

## 256-colour layer

`start-native.ps1` sets `__COMPAT_LAYER=256COLOR` only in the environment
inherited by `es2launch` and its children (`-UseNativeColors` leaves it
empty). `es2launch` removes the layer from the environment of the replacement
shell ([launcher.md](../reverse-engineering/launcher.md), "The replacement
shell without the 256COLOR layer"), so the own shell never sees it. It also
removes it from es2sim's environment and passes the value as
`ES2SIM_COMPAT_LAYER`; es2sim puts it back into the guest's copy of the
environment only ([rtl_imports.md](rtl_imports.md), "The environment copy"),
so that copy matches DBSIM's. DBSIM
still starts under the layer from the start scripts; the oracle goldens were
recorded that way. DBSIM also works without it
([presentation.md](../reverse-engineering/presentation.md), "Visual check").
Dropping it for DBSIM would change the player's display path; it is not a
VSHELL measure and is left to the owner (`-UseNativeColors` exists).

## DirectDraw forwarding and MCI redirection

- The proxy `ddraw.dll` forwards all 22 exports to the exact existing 32-bit
  system DirectDraw DLL (`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`
  of `<system dir>\ddraw.dll`; a module equal to the proxy itself is refused).
  Rendering is not reimplemented. The optional windowed and observer wrappers
  (`ddraw_windowed.h`, `ddraw_observer.h`; off unless configured) are
  described in [presentation.md](../reverse-engineering/presentation.md) and
  [setup.md](setup.md).
- At process initialization `hook_executable_mci` redirects only the
  `mciSendCommandA` and `mciSendStringA` import slots (winmm.dll) of the main
  executable, keeping the original import targets. Original executable files
  are not patched.
- `DllMain` does no DLL loading, codec work or file I/O. Forwarding and codec
  initialization happen at the first MCI call or at the first
  `DirectDrawCreate`/`DirectDrawCreateEx` export call, whichever comes first;
  the order matters because the intro can precede `DirectDrawCreate`. Two
  exceptions, both off by default: with `[arming] early=1` the initialization
  moves to the call of the mission load (`0x45f42b`, `early_arming.h`), and
  while `ES2_LIVE_COVERAGE_CONFIG` is set `DllMain` reads `[vol_probes]` and
  may reserve a report file.
- DBSIM's first `mciSendCommandA` inside the mission load is the original
  arming point that the oracle path uses when `[arming] early=1` is absent
  ([rng.md](../reverse-engineering/rng.md),
  [mission_load.md](../reverse-engineering/mission_load.md) section 13). The
  redirection therefore stays in every proxy.
- Entry probes and frame capture are tools, not measures: `vshell_probes.h`
  (off unless the INI named by `ES2_LIVE_COVERAGE_CONFIG` has
  `[vshell_probes] enabled=1`) and `vshell_capture.h` (off unless
  `ES2_VSHELL_CAPTURE_CONFIG` is set). Both act only in `VSHELL.EXE` and are
  used by `capture-vshell-screens.ps1` and the probe runs.
- The own `cw3220.dll`'s delegate mode is unused by the own shell chain
  ([rtl_imports.md](rtl_imports.md#the-delegate-mode-is-unused)).

## The IV32 binding library

`//media:iv32_bind` ([iv32_bind.h](../../media/include/es2/media/iv32_bind.h),
[iv32_bind.c](../../media/src/iv32_bind.c)) makes the system's own Indeo
decoder usable in the calling process only.

Users: the proxy (`media_proxy.c`, `initialize_codec`) and the own shell's
movie player (`shell/src/win/mci_player.c`, through `es2_iv32_system_drivers`
and `es2_iv32_bind`). Proxy-only: the redirection of the two MCI import
slots, the `[video] system_indeo` switch, the INI and the log file.

**Mechanism.**

- The bridge opens the existing system `ir32_32.dll` wrapper through
  `OpenDriver`, which keeps the wrapper's normal checks and provides the driver
  handle it needs. The wrapper's `*original.dll` implementation is never loaded
  directly.
- `ICInstall(vidc, IV32, callback, NULL, ICINSTALL_FUNCTION)` (value 1) adds
  the callback to the current process's VfW function table. The
  registry-writing `ICINSTALL_DRIVER` branch (value 2) is never used. On the
  observed Windows 10 implementation the function branch updates in-memory
  records only; the HKLM and HKCU mappings stay unchanged. The generic
  Microsoft installation documentation covers the registry path only; the
  function-flag behaviour rests on local implementation inspection and
  runtime checks.
- No system file is changed, no codec is enabled globally, no dependency is
  installed. Verified on the current Windows build (10 Pro 19045, 32-bit); no
  claim for other Windows versions. The existing wrapper retains the
  [Windows Indeo mitigations](https://learn.microsoft.com/en-us/security-updates/securityadvisories/2009/954157).

Microsoft API references:
[OpenDriver](https://learn.microsoft.com/en-us/windows/win32/api/mmiscapi/nf-mmiscapi-opendriver),
[ICInstall](https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-icinstall),
[codec functions](https://learn.microsoft.com/en-us/windows/win32/multimedia/compressor-and-decompressor-installation-and-removal).

**Call sequence of `es2_iv32_bind(NULL, drivers)`.** The wrapper path from
`GetSystemDirectoryW` and `GetFileAttributesW`; the `msvfw32.dll` path and
`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`; `GetProcAddress` of
`ICOpen`, `ICClose`, `ICInstall`; `ICOpen(vidc, IV32, decompress)` (closed
again when it opens); then `ICInstall(vidc, IV32, callback, NULL, 1)`. The
callback answers `DRV_LOAD`/`ENABLE`/`DISABLE`/`FREE` with 1, opens the
wrapper on `DRV_OPEN` and forwards everything else to that instance through
the `OpenDriver`/`CloseDriver`/`SendDriverMessage` entry points passed in
(the proxy resolves them from the system `winmm.dll`). VfW and winmm are
resolved at run time, so the proxy's import table lists only ADVAPI32, GDI32,
USER32, KERNEL32 and the CRT sets (llvm-readobj).

**Log events** (`es2_iv32_event`, written to `video-compat.jsonl` by the
proxy): `system_indeo_missing`, `system_vfw_missing`, `vfw_exports_missing`,
`existing_indeo_available`, `system_indeo_process_function_ready` and
`system_indeo_process_function_failed`. A too-long `msvfw32.dll` path logs
nothing.

**Further entry points.** `es2_iv32_system_drivers` resolves the three driver
entry points itself. `es2_iv32_release` calls `ICRemove(vidc, IV32, 0)` and
only for an entry this process installed, so it can never reach a registry
mapping; the proxy does not call it.

**Refusals** (one fixture process each, own directories in `TEST_TMPDIR`): an
incomplete driver table is refused before any call and does not stick; a
missing wrapper sticks (a later system bind returns the same status without
calls until a release); a `msvfw32.dll` that is no image gives
`system_vfw_missing`; a loadable DLL without the VfW exports gives
`vfw_exports_missing`.

**Within one process** (`//media:iv32_bind_bind_test`): after the release a
new `ICOpen` fails, a decompressor opened before the release keeps working
until `ICClose`, a second bind succeeds, and `vidc.IV32` in HKLM and HKCU
(32-bit view) is unchanged.

### Two processes

The binding is process-local. Measured with two of our own processes
(`//media:iv32_bind_two_process_test`, `//media:iv32_bind_two_decode_test`;
SysWOW64 `ir32_32.dll` SHA-256 `6110e326…c99105`, `msvfw32.dll`
`a521d6b2…a7af9d`; evidence `es2-data/logs/s-23-2026-10-13`):

| First process | Second process | Second gets IV32 |
|---|---|---|
| no binding | own binding | yes |
| bound | own binding | yes |
| bound | no binding | no (isolation) |
| bound, decompressor open | own binding | yes |
| bound, decompressor open | no binding | no (isolation) |
| released | own binding | yes |
| decoding `AVI\INTR_PT1.AVI` (frames 0–9 done) | own binding, decodes 30 frames of `AVG\INTR_PT1.AVI` | yes, 30/30 |
| same | own binding, 30 frames of the same file | yes, 30/30 (first then decodes frames 10–19) |

"Gets IV32" means `ICOpen` succeeds and `ICDecompressQuery` accepts a
synthetic IV32 format (320×240, 24 bit), or, for the decode rows, that
`AVIStreamGetFrame` returns every frame. A binding in one process does not
prevent another process from binding and decoding at the same time. The shell
does not have to release its binding before it starts DBSIM (whose proxy
binds its own). The diagnostic `probe_media --system-indeo` showed only the
isolation rows: its child process never installed a binding of its own.

Mutations that fail the tests: making `ICInstall` count as failed fails the
bind, two-process and decode tests; removing the sticky status fails the
bad-argument and bind tests
([shell/mutation_witnesses.md](../../shell/mutation_witnesses.md)).

## Mission-map lifetime guard

**The original's fault (confirmed: live dump and listing).** In the original
`VSHELL.EXE` (SHA-256 `1534aa8f…a16e`), loading a saved game and then entering
mission selection faults with an access violation at `0x453a68`. The panel at
`[0x48d8c4]` (the Telecomm panel, click handler `0x4454b8` inert;
[vshell_mission_map.md](../reverse-engineering/vshell_mission_map.md)) keeps
its bitmap in field `+0x5d`; initialization at `0x4445c3` loads
`dba\terradef.dba` and assigns its first bitmap to that panel. The mission
teardown `0x445095` calls the bitmap clean-up `0x4446d1` from `0x44509e`,
which deletes the bitmap array and clears its owner global `[0x47805c]` but
leaves the panel's reference. A later paint through `0x40bd22` passes the
stale bitmap to `0x4537e8` and the decompressor `0x453a50`. In the dump the
owner global is zero, the stale bitmap's memory is reused by 56-byte event
allocations, its data pointer (`0x4c4b0000`) crosses the next block's `ABLK`
marker, and all 2,273 allocator blocks of the 4,000,000-byte arena parse
consistently: a stale reference, not general arena corruption. The
original's exception handler retries the same first-chance exception, so the
fault shows as a freeze. Evidence: `es2-data\logs\save-load-live-20261005-01`
(`live.dmp`, `analysis.json`, `resource-lifetime-evidence.json`,
`allocator-evidence.json`; `tools/reference-game/scripts/read_minidump.py`
reads the dump).

**The guard** (`proxy/mission_map_guard.h`, only in `VSHELL.EXE`) redirects
the single clean-up call at `0x44509e` in memory: it clears the panel's
bitmap reference and then calls the original clean-up exactly once; the next
mission initialization fills the reference again. It installs only after
checking the module name, the image size and five code signatures, and is
switched by `[compat] mission_map_lifetime=1` in `es2-oracle.ini` (off by
default). When it runs, `video-compat.jsonl` records
`mission_map_guard_installed` and `mission_map_reference_cleared`. Fixtures
(`mission_map_guard_*_test`, run by `validate-media.ps1`): signature
mismatch, wrong image size, failed instruction-cache flush, failed
page-protection restore, and the redirection with clear-before-clean-up
order, the preserved calling convention and a repeated initialization. The
production entry does not install outside `VSHELL.EXE`. The own shell draws
from current state and has no deferred paint of a freed image, so it needs no
guard.

## VSHELL-only measures

With the own shell and the own launcher, neither `ES.EXE` nor `VSHELL.EXE`
runs, so the IV32 binding and the mission-map guard have no job in a default
session. `native-r111` keeps VSHELL; the measures stay available behind their
switches and are written into `es2-oracle.ini` only on request.

**Scripts.** `configure-media.ps1` and `configure-tick-log.ps1` take
`-VshellMeasures` (the latter also `ES2_VSHELL_MEASURES=1`). Without it the
generated `es2-oracle.ini` holds only the log path (media) or the oracle
sections (tick log), neither `system_indeo` nor `mission_map_lifetime`, and
the ownership manifest records `mission_map_guard` and `vshell_measures` as
false. For an installation whose original VSHELL is still played, pass
`-VshellMeasures` when its proxy is deployed, e.g.
`configure-media.ps1 -InstallName native-r111 -VshellMeasures -Apply`.
`verify-native-rebuild.ps1` derives the expected INI from the manifest's
`mission_map_guard` flag (an older manifest saying true keeps the old
expectation). `start-own-menu.ps1 -Shell vshell` prints a note when the clone
has a proxy but its INI lacks one of the two keys. A configure script never
rewrites an INI of an installation it is not run on. `validate-media.ps1`
exercises the measures themselves (guard fixtures, eight decodes with both
keys on, the disabled fallback).

**Tests (offline).**
`//tools/reference-game/proxy:oracle_speed_scripts_test` (`configure-tick-log.ps1`:
the default INI has no `system_indeo`, `mission_map_lifetime` or `[compat]`
and the manifest says false; with `-VshellMeasures` and with
`ES2_VSHELL_MEASURES=1` both keys are present and the manifest says true) and
`//tools/reference-game/setup:start_own_menu_test` (the `-Shell vshell` note
is shown when a key is missing or the INI is absent, not shown when both are
on, without a proxy or for the other shells). Not run offline: the scripts
against a real installation.

**Expected live behaviour (hypothesis, not confirmed live):** with the keys
off, the oracle suites' tick logs equal their goldens and
`video-compat.jsonl` shows neither `system_indeo_process_function_ready` nor
`mission_map_guard_installed`; with `ES2_VSHELL_MEASURES=1` the old INI and
log lines return. A difference in the tick logs would point at the IV32
binding in DBSIM (contrary to the static reading of `media_proxy.c`) and is
answered by running the same check with `ES2_VSHELL_MEASURES=1`
(`tools\check.ps1 -Live -OwnShell`).

## Validation

`tools/reference-game/setup/validate-media.ps1` (parameter `-LogRoot`,
default `<DataRoot>\logs`) first runs `//media/...`, then exercises the
mission-map guard with own-code fixtures (including rollback failures and
calling-convention/lifetime checks), then performs eight complete decodes:

| Files | Frames per file | Modes |
|---|---:|---|
| German and English intro part 1 | 604 | Native colors and 256COLOR |
| German and English intro part 2 | 706 | Native colors and 256COLOR |

All frames decode in each run, with one
`system_indeo_process_function_ready` per run. The decoder is explicitly
opened after MCI and before the first DirectDraw call, testing the original
startup order. The script also exercises forwarded DirectDraw creation plus
existing MCI, time, wave-device and joystick queries, confirms the local
proxy module path, checks that disabling the binding restores the baseline
missing-decoder behaviour (exit 1), and compares 32-bit codec mappings before
and after (unchanged). The proxy's source stamp (`Get-Es2SourceStamp`,
`check.ps1`'s stamp paths) includes `media/`.

Logs live outside the repository in `es2-data/logs`; the latest full result is
`media-compat-validation.json`, and video activation in the actual game is
appended to `video-compat.jsonl`. Full frame decoding alone does not prove the
visible game image or sound; the owner confirmed intro picture and sound in
the game with the original-shell proxy. A live run of the game with the
library-based proxy (`check.ps1 -Live`) is not recorded.
