# Reference game setup

How the original EarthSiege II 1.11 is installed as a portable installation, started, made video-compatible,
rebuilt and verified, how the own launcher, shell, runtime and simulator are deployed into installations, and
the tooling around the scripts (worker helper, evidence budget, window lint, script tests).

Every new dependency installation needs the owner's approval first. The four original runtime libraries
`CW3220.DLL`, `BWCC32.DLL`, `SOS9503.DLL` and `SOSLIBS3.DLL` are approved; that does not cover other tools,
wrappers, codecs or drivers. Tools are never installed automatically when missing.

## Start the installation

Double-click `START_ES2.cmd`. It stays in the project root and locates
`tools\reference-game\setup\start-native.ps1` relative to itself. The console closes after a successful launch
and stays open on an error so the message can be read. `START_ES2.cmd -Preview` from a terminal checks without
starting the game; other arguments are passed through to the script. Direct invocation from PowerShell 7:

```powershell
& 'tools\reference-game\setup\start-native.ps1'
```

The starter uses the system PowerShell 7 (`%LOCALAPPDATA%\Microsoft\WindowsApps\pwsh.exe`, the Microsoft Store
app alias; otherwise `pwsh.exe` from PATH) with `-NoProfile`, installs nothing and sets no execution policy. If
no PowerShell 7 is found it prints the path it tried. Windows PowerShell 5.1's default blocks script execution;
PowerShell 7 allows the local script under the existing RemoteSigned policy.

`start-native.ps1` (default installation `native-r111`):

- verifies the hashes of the original EXEs/DLLs against `installation-manifest.json`, and of the media proxy
  `ddraw.dll` and the system `SysWOW64\ddraw.dll` against `oracle-media-manifest.json`;
- mounts `build\extracted\earthsiege2-data.iso` read-only when needed, finds the `EARTHSIEGE2` volume and
  rewrites `DATA\DRIVE.CFG` when the drive letter changed;
- launches `ES.EXE` (or `ES2LAUNCH.EXE`, see [Own shell as the default](#own-shell-as-the-default)) with the
  installation as working directory; the children inherit the Windows `256COLOR` layer, the parent environment
  and persistent compatibility settings stay unchanged;
- refuses a second start while a game process of the same installation runs;
- writes one JSON record per launch below `es2-data\logs`.

| Option | Effect |
|---|---|
| `-Preview` | verifies the files and prints the launch configuration; mounts and starts nothing |
| `-InstallName <name>` | another installation below the install root |
| `-Shell own\|vshell` | own shell through `ES2LAUNCH.EXE`, or `ES.EXE` with the original `VSHELL.EXE`; default `own` when the own shell is deployed |
| `-UseNativeColors` | diagnostic: omits the `256COLOR` layer |
| `-CaptureCrash` | starts under the crash collector (below) |

Installations live in `build\installs\<name>` (`Get-CloneInstallRoot` of `CloneGuard.psm1`).

## Diagnostic start with the crash collector

`START_ES2_DIAGNOSE.cmd` is `START_ES2.cmd -CaptureCrash`: same installation, CD and
colour/video configuration. Close any running session first and keep its console open. It does not repair or
rewrite saves; normal game actions can still update them.

`-CaptureCrash` builds the own x86 collector `//tools/reference-game/helpers:capture_crash` with Bazel, copies it
to `build\reference-game\capture_crash.exe` and starts the game under it. Through the Windows debugging API it
observes `ES.EXE` and its children, records the first access violation and unhandled exceptions with registers
and process memory dumps, and keeps normal exception delivery. After 32 rapid repetitions of the same access
violation it captures the fault if needed and detaches without terminating the game. Log and dump counts are
bounded. Reports go to a new `<es2-data>\logs\crash-capture-*` directory. It installs nothing and does
not change executables, the registry or Windows error reporting. `START_ES2_DIAGNOSE.cmd -Preview` checks the
installation and builds the helper without starting the game. Test:
`tools/reference-game/helpers/tests/test_capture_crash.ps1`.

The repeated-exception case comes from the VSHELL save/load fault, a freed mission-map bitmap the game's handler
kept retrying; cause and guard: [media_compatibility.md](media_compatibility.md#mission-map-lifetime-guard).

## Tools on the host

- Windows 10 Pro x64, 10.0.19045, WoW64.
- 7-Zip 25.01: `C:\Program Files\7-Zip\7z.exe` (system installation).
- Python 3.12.14: `<es2-dependencies>\python\python.exe`.
- PowerShell 7: system installation (`pwsh`).
- LLVM-MinGW, Clang 23.1.2: `<es2-dependencies>\llvm-mingw\bin`.
- Windows DirectDraw, WinMM, Video for Windows and the Indeo wrapper as present on the system; no codec package
  or rendering wrapper is added.

Versions and pins: [dependencies.md](../dependencies.md).

## Prepare the original media

1. Verify the raw CD image's SHA-256 against `data/reference_hashes.txt` and keep an unmodified copy in
   `es2-dependencies\archive`.
2. `tools/reference-game/setup/extract_data_track.py <raw source> build\extracted\earthsiege2-data.iso`
   validates the Mode-1 sectors and refuses to overwrite. The raw source keeps its audio tracks; the ISO holds
   only the data session.
3. Extract that ISO with 7-Zip into `build\extracted\cd`.
4. Compare the original binaries with `data/reference_hashes.txt`. Every CD file's SHA-256 is in
   `data/iso_file_hashes.sha256`, so a source that matches holds the scanned bytes
   (`es2-data/logs/defender-scan.json`); no new virus scan is needed and `prepare-native.ps1` asks for none.

## Create a portable installation

Preview, then the installation after approval of the four bundled runtimes:

```powershell
.\tools\reference-game\setup\prepare-native.ps1
.\tools\reference-game\setup\prepare-native.ps1 -InstallBundledRuntime -Language German
```

This creates `build\installs\native-r111` (or `-InstallName`), copies the approved local files,
writes the original-derived configuration and `installation-manifest.json`, and verifies the original
executable and runtime hashes. No registry write, system DLL replacement, codec installation or download. An
existing destination is refused; a second installation is a [fresh rebuild](#a-separate-fresh-rebuild).
`-Language` is `German` (default), `English` or `French`. `-OwnShell`, `-OwnSim` and `-RetireOriginals` deploy
the own chain right after the copy (sections below).

### The original installer and the portable layout

The original installer is not run: `SETUP.EXE` is a 16-bit NE program and `SIERRA.INF` rejects Windows NT. The
layout follows `SIERRA.INF` and the listing of `BATCH.EXE`:

- The maximum Win95 installation copies `DATA`, `SAV`, `TAPES` and selected VOL files, and puts `ES.EXE`,
  `VSHELL.EXE`, `DBSIM.EXE` and `SOS9503.DLL` from `VER95` into the installation root. `prepare-native.ps1`
  copies the whole `VOL` folder (no dependence on the minimum/medium split) and the selected language folder;
  videos stay on the CD.
- Bundled root files: `CW3220.DLL`, `BWCC32.DLL`, `SOSLIBS3.DLL`, `HMIDRV.WIN`, `HMIMDRV.WIN`, `SOSLIB.INI`,
  `ESII.ICO`. The CD's 1996 `DDRAW.DLL` and DirectX drivers are never copied.
- The localized `ERROR.ENG/FRE/GER` becomes the root `ERROR.STR`, `MISSION.ENG/FRE/GER` becomes
  `DATA\MISSION.STR`.
- `BATCH.EXE` `0x40126f`–`0x401335` writes `DATA\DRIVE.CFG` (the CD root, then the installation root, each
  ending in CRLF) and `DATA\LANGUAGE.CFG` (one character, `E`, `F` or `G`). `prepare-native.ps1` writes the same
  bytes; the installer's optional codec registration is not needed for them.
- The installer also writes `SIERRA.INI` and may register codecs and DirectX; the portable installation writes
  no registry or system file. The missing IV32 decoder is bound inside the process instead
  ([media_compatibility.md](media_compatibility.md)).

## Video compatibility: the media proxy

The proxy is own code in `tools/reference-game/proxy`. `validate-media.ps1` runs the fixture tests, builds the
proxy (`//tools/reference-game/proxy:ddraw.dll`) and the media probe with Bazel and stages the DLL in
`build\reference-game\proxy`, from where `configure-media.ps1` deploys it:

```powershell
.\tools\reference-game\setup\validate-media.ps1
.\tools\reference-game\setup\configure-media.ps1 -VshellMeasures -Apply
.\tools\reference-game\setup\start-native.ps1
```

The validator builds a `ddraw.dll` that forwards all 22 exports of this machine's system DirectDraw, tests
German and English intro parts 1 and 2 in native and 256-colour modes, checks decoder availability before any
DirectDraw creation, verifies the disabled configuration, runs the mission-map guard tests and compares the
HKLM and HKCU 32-bit codec mappings before and after. Evidence: `es2-data/logs/media-compat-validation.json`.

The build regenerates the DirectDraw exports from the current system DLL and disables PE timestamps; two builds
with identical source, toolchain, repository path and system exports give identical bytes. Only the validated
`ddraw.dll` is deployed. Rebuild and revalidate after changing the proxy or after a system DirectDraw update;
`start-native.ps1` rejects a mismatched system DLL.

`configure-media.ps1` (default `native-r111`) previews without `-Apply`. `-Apply` requires the exact validated
DLL hash, an unchanged system DirectDraw and a closed game, and copies only the DLL, `es2-oracle.ini` and the
ownership/hash manifest `oracle-media-manifest.json` into the installation. Unknown or edited files are not
overwritten; the game EXEs and media stay unchanged. Switches:

| Switch | `es2-oracle.ini` | Manifest |
|---|---|---|
| (none) | `[video] log=` (events to `es2-data\logs\video-compat.jsonl`) | |
| `-VshellMeasures` | `[video] system_indeo=1`, `[compat] mission_map_lifetime=1`: the VSHELL-only measures, for an installation that plays the original `VSHELL.EXE` | `vshell_measures` |
| `-OwnPresenter` | `[presentation] presenter=gdi`, `directdraw=windowed`: the own GDI presenter, DBSIM's DirectDraw in a window | `own_presenter` |
| `-ReplaceHooks` | `[hooks]` with every hook of `proxy/hook-names.txt` set to `replaced` | `replaced_hooks` |

Without `system_indeo=1` the proxy does not bind the decoder. `verify-native-rebuild.ps1` rebuilds the expected
INI from the manifest. The owner's play deployment on `native-r111`:
`configure-shell.ps1 -InstallName native-r111 -AllowPrimary -Apply`, `configure-media.ps1 -VshellMeasures
-OwnPresenter -ReplaceHooks -Apply` (it plays the original `VSHELL.EXE`), `configure-runtime.ps1 -InstallName native-r111 -AllowPrimary -Apply`.

With the measures on, the proxy redirects the main executable's two MCI import slots in memory; the first video
call initializes the present Windows Indeo wrapper through `OpenDriver` and a process-local VfW binding, with a
DirectDraw initialization fallback for other call orders. No codec is added to `Drivers32`; system rendering
functions are forwarded unchanged. Scope and evidence: [media_compatibility.md](media_compatibility.md).

## A separate fresh rebuild

A second installation from the same sources, leaving `native-r111` and its saves untouched. Run in PowerShell 7
with an unused installation name; every command names the rebuild:

```powershell
$rebuildName = 'rebuild-r111' # Use another unused name when this directory exists.
$rebuildEvidence = Join-Path '<es2-data>\logs' ('rebuild-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $rebuildEvidence | Out-Null
$mediaProof = '<es2-data>\logs\media-compat-validation.json'
if (Test-Path -LiteralPath $mediaProof) {
    Copy-Item -LiteralPath $mediaProof -Destination (Join-Path $rebuildEvidence 'previous-media-validation.json')
}
& 'tools\reference-game\setup\prepare-native.ps1' -InstallName $rebuildName -Language German -InstallBundledRuntime
& 'tools\reference-game\setup\validate-media.ps1'
Copy-Item -LiteralPath $mediaProof -Destination (Join-Path $rebuildEvidence 'media-compat-validation.json')
& 'tools\reference-game\setup\configure-media.ps1' -InstallName $rebuildName -Apply
& 'tools\reference-game\setup\verify-native-rebuild.ps1' -InstallName $rebuildName -Language German -OutputPath (Join-Path $rebuildEvidence 'fresh-install-verification.json')
& 'tools\reference-game\setup\start-native.ps1' -InstallName $rebuildName -Preview
```

Run the verifier **before the first game start**. It compares every copied original file against the extracted
CD, checks the seven pinned binary hashes, the exact drive/language and compatibility configuration, the fresh
file inventory, the manifests and the current validation proof, records each comparison and the source/script
hashes, rejects read-only `DATA`/`SAV` files, and writes only a new report outside the installation (an existing
report is refused). A played installation differs, because the game writes settings, mission state and saves.
It accepts the own-shell, own-sim and retired-original files against their manifests.

Then start it with `& 'START_ES2.cmd' -InstallName $rebuildName`. Both installations append
compatibility events to `<es2-data>\logs\video-compat.jsonl`; correlate them by process ID.

## What the installation is confirmed to do

- `native-r111`: the owner confirmed a correct cockpit, quick start, intro picture and sound, sound effects,
  plausible speed, at least ten minutes of play and a save/restart/load cycle. Mission completion, CD music and
  joystick-specific behaviour are not established.
- A scripted fresh rebuild passed file verification, compatibility tests and startup with all 72 primary files
  unchanged: `es2-data\logs\rebuild-20261005`.
- Read-only timing and state capture, movement calibration and deterministic replay build on it:
  [timing.md](../reverse-engineering/timing.md), [main_loop.md](../reverse-engineering/main_loop.md),
  [player_state.md](../reverse-engineering/player_state.md), [input_journal.md](input_journal.md),
  [oracle regression](oracle_regression.md).

Visual results come from the owner, not from automated screenshots; automated video decoding does not prove
visible in-game playback.

## Own launcher

`tools/reference-game/setup/start-own-launcher.ps1` (default clone `calibration-r111`) starts the game through
the `ES.EXE` replacement [`launcher/`](../../launcher/README.md) after the same checks as `start-native.ps1`. It
refuses `native-r111` and clones whose `es2-oracle.ini` carries oracle settings (input replay, virtual time, tick
log, hooks, coverage, probes, perf) unless `-AllowOracleConfig`, copies the launcher as `ES2LAUNCH.EXE` and logs
every VSHELL/DBSIM start to `<es2-data>\logs\own-launcher-*.log`. `-DryRun` changes nothing. Contract:
[launcher.md](../reverse-engineering/launcher.md).

## Own shell as the default

`tools/reference-game/setup/configure-shell.ps1 -InstallName <name> -Apply` deploys the own launcher and the own
graphical shell: `ES2LAUNCH.EXE` beside the original files and `ES2SHELL\` with `es2shell.exe`, `SDL3.dll` and
`menu.ini` (`game_dir`, the `cd_root` of `DATA\DRIVE.CFG`; no evidence log, no fixed seed), recorded in
`own-shell-manifest.json`. Nothing original is touched; `VSHELL.EXE` stays in place, unused. `-Remove` deletes
the deployment; without a switch the script previews. `native-r111` is refused unless `-AllowPrimary` (only on
the owner's word). `prepare-native.ps1 -InstallBundledRuntime -OwnShell` deploys it right after a fresh
installation.

With the manifest present, `start-native.ps1` (and so `START_ES2.cmd`) checks the hashes and starts
`ES2LAUNCH.EXE` with `ES2_LAUNCH_SHELL` set to the deployed shell; `menu.ini`'s `cd_root` follows the mounted CD
letter like `DRIVE.CFG`. The launcher writes `logs\own-launcher-<stamp>.log`, the shell `ES2SHELL\shell.log`.
`-Shell vshell` starts `ES.EXE` with the original VSHELL; `-Shell own` refuses an installation without the
deployment. The `256COLOR` layer stays for the launcher, VSHELL and DBSIM; `es2launch` leaves it out of the own
shell's environment
([launcher.md](../reverse-engineering/launcher.md#the-replacement-shell-without-the-256color-layer)).
`verify-native-rebuild.ps1` checks the deployed files against the manifest. Test:
`//tools/reference-game/setup:configure_shell_test` (synthetic installation, stand-in PE images,
`start-native -Preview`).

Deployed on `rebuild-r111` (the owner's play clone) and `native-r111`.

## Own runtime in a measurement clone

`configure-runtime.ps1` puts the own `cw3220.dll` (`//rtl:cw3220.dll`) into a measurement clone in place of
`CW3220.DLL` and takes it out again; the original stays beside it as `CW3220-ORIGINAL.DLL` and is never changed.
Without a switch it previews the state (`original` or `own`):

```powershell
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone>          # preview
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone> -Apply   # deploy
pwsh -File tools\reference-game\setup\configure-runtime.ps1 -InstallName <clone> -Remove  # restore
```

`-Apply` builds the DLL with Bazel (or takes `-Source`) and checks that it exports `es2_own_runtime`. Both
directions check both files' hashes against `installation-manifest.json` and the clone's
`oracle-runtime-manifest.json`. Refused: `native-r111` unless `-AllowPrimary` (only on the owner's word, and then
only with its own installation manifest, no `measurement_clone_of`), an installation that is not a separate
measurement clone, linked paths, a running game process of the clone, a clone another session holds, and any
unexpected state. `start-native.ps1` and the scripts that call its preview accept a deployed clone and add
`runtime` to their records. In the clone DBSIM runs the own runtime; `ES.EXE` and `VSHELL.EXE` run the original
through it. Design and tests: [rtl_imports.md](rtl_imports.md#the-own-cw3220dll). Test:
`//tools/reference-game/setup:configure_runtime_test`.

## The own DBSIM process in an installation

`es2launch` starts es2sim in place of `DBSIM.EXE`
([launcher.md](../reverse-engineering/launcher.md#the-own-dbsim-process-behind-the-launcher)).
`configure-sim.ps1` deploys es2sim into an installation; `start-own-menu.ps1 -Sim own` and
`check.ps1 -Live -OwnSim` run it unattended. The scripts are tested on synthetic installations.

### `configure-sim.ps1`

```powershell
& tools\reference-game\setup\configure-sim.ps1 -InstallName timing-r111                     # preview (JSON)
& tools\reference-game\setup\configure-sim.ps1 -InstallName timing-r111 -Apply [-Headless] [-Config es2sim.ini] [-TickLog <file>] [-Output <file>] [-Sim <exe> -Sdl <dll>]
& tools\reference-game\setup\configure-sim.ps1 -InstallName timing-r111 -Remove
```

**`-Apply`** writes only new files beside the original ones (the Bazel outputs are copied to
`build/reference-game/sim` first):

- `ES2SIM\es2sim.exe`, from `//sim:es2sim_window` unless `-Sim` is given
- `ES2SIM\SDL3.dll`, from `//sim:sdl_runtime` unless `-Sdl` is given
- `ES2SIM\es2sim.ini`, a copy of `-Config`, when that is given
- `es2launch.ini`, with `[sim] command` and, with `-Output`, `[sim] output`
- `own-sim-manifest.json` (kind `es2-own-sim`), with the hashes and the command

**The command** is `"<inst>\ES2SIM\es2sim.exe" [--headless] [--config "<inst>\ES2SIM\es2sim.ini"]
[--tick-log "<file>"]`; es2launch appends ` --arguments "<DBSIM argv>"`. It names no DBSIM.EXE: es2sim builds
the guest image from its own data. `es2launch.ini` holds the command
inside one more quote pair, `command = "<command>"`, because `GetPrivateProfileString` strips a quote pair around
a whole value ([own_process.md](own_process.md) "es2sim without DBSIM.EXE").

Applying the same files again reports `applied: false`; another build or command replaces the deployment.
**`-Remove`** deletes `ES2SIM\`, `es2launch.ini` and the manifest; every `ES2LAUNCH.EXE` started there then runs
`DBSIM.EXE` again.

**Refusals.** No original file is opened for writing. Refused:

- `native-r111` without `-AllowPrimary`
- an installation without `installation-manifest.json`
- unexpected own-sim files, or files whose hashes differ from the manifest
- a running game process of the installation, es2sim included
- a clone that another session holds (CloneGuard; `ES2_CLONE_LOCK_TOKEN` lets a caller that holds the lock in)
- a `DBSIM.EXE` whose hash is not the installation manifest's
- sources that are not 32-bit PE images, or that are original files
- paths that are not plain ASCII, or that contain quotes

**Test.** `//tools/reference-game/setup:configure_sim_test` (tag `scripts`) on a synthetic installation,
including a real `es2launch` dry run that reads the deployed INI.

### `start-own-menu.ps1 -Unattended -Sim own`

`-Sim dbsim|own` defaults to `dbsim`. With `own`, the unattended run of `c1_01-success`:

1. **Writes the configuration** `run\es2sim.ini` (UTF-16 LE with a BOM, the sections of `replay_suite.py`'s
   `config_text`): the watches, 47 ms virtual time, the replay journal of the suite's input script, early
   arming, `ticks`, and `outcome=1`.
2. **Deploys es2sim** with `configure-sim.ps1 -Apply -Headless -Config ... -TickLog <run tick log>
   -Output run\es2sim-output.txt` under its own clone lock token. `-SimExe` and `-SimSdl` take prebuilt files.
3. **Runs the chain** es2launch, es2shell or es2menu, es2sim, the shell again. The restore always calls
   `configure-sim.ps1 -Remove` and records it in `restore.json` as `own_sim`, so `-RestoreOnly` removes es2sim
   too.
4. **Judges the run** as for DBSIM (launcher events, pinned launch files, tick log and outputs against the golden,
   the chain verify), with these launcher events:

```
start shell-replacement -X1 / exit shell-replacement 2
start sim-replacement --arguments "-eggplant -X2 -R0 -G" / exit sim-replacement 3
start shell-replacement -X3 / exit shell-replacement 0 / end 0
```

`unattended.json` records `sim`. `-Sim own` cannot be combined with `-Hooks` or `-PreparedProxy` (they configure
DBSIM's proxy); `-ConfigureSimScript`, `-SimExe` and `-SimSdl` need `-Sim own`. The dry run shows the preview of
`configure-sim.ps1`. The clone's process list includes `ES2SIM\es2sim.exe`, so the clone guard and the restore
see it. Test: `start_own_menu_test` with a fixture launcher and a fake `configure-sim.ps1`.

### `check.ps1 -Live -OwnSim`

The stage `own-sim` runs after the live stage, once the clones' runtime is restored. It builds
`//sim:es2sim_window` and `//sim:sdl_runtime` into `<EvidenceRoot>\own-sim\bin`, deletes the binaries afterwards
and keeps their hashes in the details.

- **(a) The suites.** Every registry suite (with `-Suite`, the named ones) runs through
  `sim/tests/replay_suite.py --frames`, `-OwnSimJobs` at a time (default: the clone count), in working
  directories prepared from the CD like the clones' files; `ES2_ORACLE_DIR` follows `-OracleDataRoot`. Each
  IDENTICAL suite then runs `sim/tests/frames_suite.py --capture` against the frame references
  (`-OwnSimFramesReference` sets `ES2_FRAMES_REFERENCE`). A passing suite keeps `result.json` and its outputs but
  not its raw tick log or capture.
- **(b) The chain.** `start-own-menu.ps1 -Unattended -Shell gui -Sim own` on `-OwnSimInstall` (default: the
  first clone), session `own-sim-chain`. `-OwnCdRoot <dir>` hands it (and the own-shell chain)
  `start-own-menu.ps1 -CdRoot`, e.g. a CD copy without `VER95\DBSIM.EXE` and `VER95\VSHELL.EXE`.

PASSED needs every replay IDENTICAL or SKIP with at least one IDENTICAL, every frame compare PASS or SKIP, and the
chain's verdict PASSED with `sim` = `own` and exit 0. With `-OwnSim`, `sim/` and `rtl/` are source-stamp paths;
`-Overlap` discards a passing own-sim stage like a passing live stage. Only the chain runs on a clone through
es2launch; a per-suite run on the clones would need `run-oracle-parallel.ps1` to deploy es2sim (open). Test:
`//tools:check_script_test` with fake drivers (size `large`, about 240 s).

### Live run

```powershell
& tools\reference-game\setup\start-own-menu.ps1 -Unattended -Shell gui -Sim own -InstallName timing-r111
& tools\check.ps1 -Live -OwnSim -SkipQuick
```

Expected:

- `UNATTENDED PASSED`, with `DBSIM (es2sim) identical to the c1_01-success golden`, launcher sequence `expected`
  (the events above), and launch files pinned
- `configure-sim.ps1 -Remove: removed True, state none` in the restore
- in `check.ps1`, `own-sim PASSED` with `replay of N suites: IDENTICAL N` over every registered suite (or SKIP for suites without a
  golden), `frames: PASS ...` (or SKIP without a reference), and `chain PASSED`

The run's hypotheses (the working directory, exit code 3) are in launcher.md.

## Played installations without `DBSIM.EXE` and `VSHELL.EXE`

Installations people play run without both files; the oracle clones (`timing-r111`, its `-pN` copies,
`calibration-r111`) keep them, because the oracle runs the originals. es2launch, es2shell and es2sim read
neither file.

```powershell
& tools\reference-game\setup\retire-originals.ps1 -InstallName rebuild-r111            # preview (JSON)
& tools\reference-game\setup\retire-originals.ps1 -InstallName rebuild-r111 -Apply
& tools\reference-game\setup\retire-originals.ps1 -InstallName rebuild-r111 -Restore
```

**States.** `present`: both files there, with the hashes of `installation-manifest.json`. `retired`:
`retired-originals.json` there and neither file. Anything else is unexpected and refused without a change. A
repeated `-Apply` or `-Restore` reports `changed: false`.

**`-Apply`** needs the own chain (`configure-shell.ps1 -Apply` and `configure-sim.ps1 -Apply` first), writes
`retired-originals.json` (kind `es2-retired-originals`, with both hashes), then deletes both files.
**`-Restore`** copies both files from `<ExtractedRoot>\cd\VER95`, checks each hash against the record and
deletes the record.

**Refusals:** a measurement clone (`measurement_clone_of` in the manifest); `native-r111` without
`-AllowPrimary`; names that are not plain installation names; links; a clone another session holds
(CloneGuard); a running game process of the installation; a restore source without the recorded hash.

**Other scripts with retired files.**

- `start-native.ps1` starts only the own shell, lists `retired_originals` in the preview, refuses
  `-Shell vshell`, and refuses a retired file that came back without `-Restore`.
- `configure-shell.ps1 -Remove` and `configure-sim.ps1 -Remove` refuse until `-Restore` has run.
- `verify-native-rebuild.ps1` accepts the record, the missing files and the own-sim files, and reports a retired
  file that is still present.
- `prepare-native.ps1 -InstallBundledRuntime -OwnShell -OwnSim -RetireOriginals` sets up a played installation in
  one step; the files are copied and hashed first, so the manifest holds what `-Restore` checks.

Test: `//tools/reference-game/setup:retire_originals_test` (tag `scripts`, synthetic installation with stand-in
files). `rebuild-r111` is retired (`start-native.ps1 -Preview` shows `shell: own` and
`retired_originals: DBSIM.EXE, VSHELL.EXE`); `timing-r111` keeps both files.

## One-command acceptance of the own menu

`start-own-menu.ps1` (default clone `calibration-r111`) is the owner's single command for the own menu: it backs
up `DATA` and `SAV` of the clone to `<es2-data>\logs\own-menu-<stamp>`, builds and sets up the menu, starts
it through `start-own-launcher.ps1` (menu console and game windows visible by design), waits for the session,
collects the evidence, restores the two folders in a `finally` and prints the compare checklist. `-Shell` is
`text` (default), `gui` or `vshell`. It refuses `native-r111` (unless `-AllowPrimary`), a running game process
of the clone, a clone another session holds and linked paths; `-DryRun` lists the steps and changes nothing;
`-RestoreOnly <session dir>` restores an interrupted session. It never stops a process. Routes, evidence layout,
exit codes and checklist: [launcher.md](../reverse-engineering/launcher.md#owner-acceptance-one-command). Test:
`//tools/reference-game/setup:start_own_menu_test` (tag `scripts`; synthetic installations, a fixture launcher,
the stand-in DBSIM of `//shell:chain_real_test` and a mock game process).

Interactive sessions warn when the clone's proxy lacks the `[presentation]` options or its `es2-oracle.ini` lacks
`[presentation] directdraw=windowed`, because DBSIM then crashes after the shell's Full Screen (prefs byte 6 = 1;
`0x4654a8`). Play on `rebuild-r111`, or add `-WindowedDirectDraw` (sets `directdraw=windowed` and
`presenter=gdi` for the session and restores the INI afterwards). Details:
[launcher.md](../reverse-engineering/launcher.md#dbsims-full-screen-in-interactive-sessions).

## Worker helper

`tools\worker.ps1` sets up and closes a worker package of the parallel protocol, one worktree per
package. Names are lower case with a hyphen (`tools-comments`, `t-05`) or
`wp<n>`, the same rule as `tools\clean-worker-bazel.ps1`:

```powershell
& tools\worker.ps1 tools-comments
```

It creates `es2-tools-comments` beside the checkout on the new branch `claude/tools-comments` (from `main`) and the ignored
scratch directory `build\claude\tools-comments`, and prints the commands: `Set-Location` into the worktree, then

```
bazel --output_base=build\bazel\tools-comments test --repo_contents_cache=build\bazel\tools-comments-contents --local_resources=cpu=4 <targets>
bazel --output_base=build\bazel\tools-comments shutdown
```

It refuses an existing worktree directory, an existing branch or a bad name, and starts no Bazel.

```powershell
& tools\worker.ps1 tools-comments -Finish            # when the worker is done
& tools\worker.ps1 tools-comments -Finish -Archive   # ... and copy the evidence
```

`-Finish` shuts down the Bazel servers of the output bases `<pkg>`, `<pkg>-mut` and `<pkg>-mutation` (those with a
server pid file) and prints the coordinator's next steps; it removes nothing. The steps: copy `build\claude\<pkg>`
to `<es2-data>\logs\<pkg>-<yyyyMMdd>`, `git worktree remove ..\es2-<pkg>` (deletes the ignored files
in it), `git branch -d claude/<pkg>` after the merge, and `tools\clean-worker-bazel.ps1 <pkg>` (which also removes
the `-contents`, `-mut` and `-mutation` variants; `-DryRun` lists).

`-Archive` does the copy itself and nothing else in `es2-data`. It leaves out `mutation-workspace-*` directories
and `*.exe`, `*.dll`, `*.lst`, `*.asm`, `*.elf`, `*.pkl` at any depth (copies of or derived from the original's
bytes, or regenerable binaries) and follows no links. It copies into `<target>.partial` and renames it, and
refuses if the target or the staging directory exists, if the evidence root does not exist (it is never created)
or if `-Date` is not `yyyyMMdd` (default: today).

The path parameters (`-Repo`, `-WorktreeParent`, `-OutputRoot`, `-EvidenceRoot`, `-Base`, `-Date`;
`clean-worker-bazel.ps1` has `-OutputRoot`, `-Repo`, `-WorktreeParent`) default to the real locations and exist
for `//tools:worker_scripts_test`, which runs both scripts against a temporary git repository, output root and
evidence root with a fake `bazel.cmd`.

## Evidence budget

`tools/evidence_budget.py` guards the size and content of `es2-data` (evidence policy in
[README.md](../../README.md#verification)). It only reads: it never stages, unstages, deletes or commits.
`tools/check.ps1` runs it as its last stage `evidence` (`-Es2Data`, `-SkipEvidence`), so a refusal fails the
check. By hand before a commit of `es2-data`:

```powershell
<es2-dependencies>\python\python.exe -B tools\evidence_budget.py [--data-root <es2-data>] [--older-than <yyyy-mm-dd>] [--max-bytes 5242880]
```

Exit 0 nothing refused, 1 something staged is refused (listed with the reason), 2 usage. Refused when staged:

- a raw tick log of an identical run: `*.jsonl` or `*.jsonl.hooks.jsonl` directly in a directory named `run`
  whose `*comparison*.json` says `"identical": true` (what `check.ps1` deletes). Input recordings
  (`*-input.jsonl`), tick logs of runs that differ or have no comparison, and small shared logs outside a `run`
  directory (`logs/video-compat.jsonl`) are allowed;
- an empty `*.hooks.jsonl` (the zero-byte mismatch log of a compare-mode run without mismatches; the runner and
  `check.ps1` delete it, [oracle_regression.md](oracle_regression.md#empty-hook-mismatch-logs)); one with content
  is evidence and allowed unless it lies in the `run` directory of an identical run;
- anything inside `mutation-workspace*`; `*.lst`, `*.asm`, `*.elf`, `*.pkl`, `*.dmp`;
- a file over 5 MiB (`--max-bytes`), unless its path is in `es2-data/.es2budget-allow` (size rule only).

It also prints the size of `logs`, `.git` and `.git/lfs`, and the top-level `logs` directories whose newest file
is older than `--older-than` and that no document cites (`logs\<name>` or `logs/<name>` in `docs/**/*.md`,
`PLAN.md` where present, `CLAUDE.md`, `README.md`, `tools/**/README.md`; `*` and a trailing `<placeholder>` match a suffix).
Without `--older-than` the date is that of the es2 repository's first commit; without a commit (`--repo` is no
repository of its own) the list is skipped. These are only candidates for a discard, which stays the owner's decision. Test: `//tools:evidence_budget_test`;
the stage in `//tools:check_script_test`.

## Console windows

A console program started by a process that has **no console of its own** (a GUI program, a scheduled task, a
launcher started hidden) gets a new console window; the GUI launcher and the console stand-in
(`launcher/BUILD.bazel`) avoid that. A child of a process that **has** a console shares it and opens nothing, also
when that console has no window: a Bazel test runs with such a windowless console (inside a `host_python_test`
`GetConsoleWindow()` is 0, two processes on the console), and so does anything started with `CREATE_NO_WINDOW` /
`CreateNoWindow = $true`.

`//tools:process_starts_test` (`tools/test_process_starts.py`) scans every tracked `*.ps1`, `*.psm1`, `*.py`,
`*.c`, `*.h`, `*.cmd`, `*.bat` file (never `bazel-*`, `build/`, `es2-data/`) for process starts and fails for one
that has neither a no-window flag nor an allow comment:

| Language | Starts that are listed | Accepted flag |
|---|---|---|
| C | `CreateProcess*`, `_spawn*`/`_exec*`, `ShellExecute*`, `WinExec`, `system`, `popen` | `CREATE_NO_WINDOW` or `DETACHED_PROCESS` in the call or the 8 lines before it; `STARTF_USESHOWWINDOW` with `SW_HIDE`; `SW_HIDE` in a `ShellExecute` call. `_spawn*`, `system` and `popen` take no flag and always need the comment |
| PowerShell | `Start-Process`, `ProcessStartInfo`, `[Diagnostics.Process]::Start('file')` (also inside PowerShell held in Python strings) | `-WindowStyle Hidden` (also `WindowStyle='Hidden'` in a splatted hashtable), `-NoNewWindow` (shares the caller's console), `CreateNoWindow = $true` within 15 lines |
| Python | `subprocess.run/Popen/call/check_*`, `os.system/popen/startfile/spawn*/exec*`, `asyncio.create_subprocess_*` | `creationflags=` that names `NO_WINDOW`, `CREATE_NO_WINDOW`, `DETACHED_PROCESS` or `0x08000000` |
| cmd | `start` | `start /B` |

The PowerShell call operator (`& program`) is **not** listed: whether it opens a window depends only on the
caller's console, which a text scan cannot know, and the scripts run in the console of their caller.

**Allow comment.** A start without a flag carries `window-lint: allow <reason>` (reason of at least eight
characters) in a comment on its line, on the line above, or on a line inside a multi-line call: `# ...` in Python
and PowerShell, `/* ... */` in C, `rem ...` in cmd. The reason names the rule that makes the start safe:

- `gui-child <exe>`: the child is a GUI-subsystem program (`ES.EXE`, `VSHELL.EXE`, `DBSIM.EXE`, the `-mwindows`
  launcher, `launcher/BUILD.bazel`), which is never given a console;
- `inherits console: ...`: the child is a console program and the parent owns a console it shares (Bazel tests,
  the interactive `es2menu` console, scripts run from a console or from `check.ps1`/the runners, whose children
  are started with `CreateNoWindow`);
- `visible on purpose`: the window is wanted (the owner's `-Interactive` game start, `START_ES2*.cmd`, record
  mode).

`window-lint: allow-file <reason>` in the first 40 lines covers every start in the file (the Bazel-only test files
and the developer CLIs). A marker without a start next to it is an error ("stale marker"). Run
`<es2-dependencies>\python\python.exe -B tools\test_process_starts.py --list` to print every start with
its disposition. A new script that starts a program passes the flag; an allow comment is only for one of the
three rules above. Not covered by the scan: `START_ES2*.cmd`, which only call `pwsh` and are visible by design.

## PowerShell script tests

`suite_scripts_test`, `clone_guard_test`, `configure_*_test`, `retire_originals_test`, `start_own_menu_test`
(all in `tools/reference-game/setup`) and `oracle_speed_scripts_test` (in `tools/reference-game/proxy`) run the
real setup scripts against synthetic fixtures. They carry the Bazel tag `scripts`, so during development

```
bazel test //... --test_tag_filters=-scripts
```

skips them; run them before a commit and after any change to the scripts or modules they exercise. Each test
starts PowerShell once per independent section ("lane") and runs every step in a fresh runspace of that process
(`setup/pwsh_session.py`); environment and current directory are restored after each step. Steps that need
another session or process (lock holders, Windows PowerShell 5.1 next to PowerShell 7, the check that a lock
vanishes when its process ends) stay separate processes.

The tests of the scripts in `tools/` (`worker_scripts_test`, `check_script_test`, `fresh_clone_script_test`)
carry the tag `manual` and form the suite `//tools:scripts`, so `bazel test //...` skips them;
`tools/check.ps1 -Full` runs `//tools:scripts` with `differential_full` in its full stage (`test_check.py` asserts
that the quick stage does not). Run `bazel test //tools:scripts` after a change to `worker.ps1`,
`clean-worker-bazel.ps1`, `check.ps1`, `check_counters.py`, `check-fresh-clone.ps1`, `setup-dependencies.ps1`,
`setup-platform-dependencies.ps1` or `python_tree_digest.py`. `fresh_clone` and `worker_scripts` are size
`medium`. None of the four is `no-cache`: every file they read is declared data and the rest is built in their
temporary directory.

### Sharded script tests

The sections of these tests are independent (each builds its own synthetic tree), so Bazel's sharding variables
select them: `shard_items()` in `setup/pwsh_session.py` runs section or lane i in shard i modulo the shard count;
without the variables everything runs.

| Target | Shards | Unit |
|---|---|---|
| `//tools/reference-game/proxy:oracle_speed_scripts_test` | 4 | eight sections |
| `//tools/reference-game/setup:suite_scripts_test` | 7 | lanes; `lane_dry` in a shard of its own |
| `//tools/reference-game/proxy:simulation_hook_configuration_test` | 4 | iteration k of the per-hook registry loop in shard k modulo 4, the closing part in shard 0 |
| `//tools/reference-game/invitro:differential_hud_keys_test` | 3 | in-vitro group (`invitro/groups.bzl`) |
| `//tools/reference-game/invitro:differential_mission_objectives_test` | 2 | in-vitro group |
| `//tools/reference-game/invitro:differential_rtl_test`, `_full_test` | 2 | function f in shard f modulo 2 |

`suite_scripts_test` runs the registered suites through `run-oracle-regression.ps1` in groups of at most
`SUITES_PER_GROUP` = 10 (`legacy_group_lanes()`, one lane and fixture each, `lane_legacy_group_<i>`), so a step is
bounded by the group size and stays under the 120 s step timeout whatever the suite count; `lane_legacy_runner`
keeps the option checks and runs only the two baseline suites `acceptance-60s` and `c3_09-loadout-b`. Every
registered suite is still required `identical`.

In `differential_rtl_test` every function (direct phase) and every function and mode (installed phase) has its
own random stream (`rtl_reseed` in `rtl_cases.h`), so a sharded run executes exactly the cases of an unsharded
one; the `rtl_<function>` mutation runs use the same streams.

Tests that stay `no-cache`: `oracle_speed_scripts`, `suite_scripts`, `clone_guard`, `live_coverage_scripts`,
`entry_probes_scripts` read sibling scripts and registries of the source tree (`oracle-suites.json`,
`scripts/*`, the git state) that are not declared data; `chain_real`, `menu_real`, `results_corpus_real`,
`sav_real`, `str_cfg_real` read recorded suite outputs under es2-data that `@es2_data` does not track; the cost
benchmarks (`*_benchmark`, `*_cost`) measure timing. A rerun in the same output base reuses their results anyway.

To measure test wall time: `bazel test //... --nocache_test_results --profile=<file>` after a `bazel build //...`
that warms the disk cache; per-test durations come from the profile (the `TestRunner` wall time inside each
`Testing //x` action), not from the `PASSED in` lines. Bazel 9 has no `analyze-profile`.

### Sharded shell real tests

`bay_real_test` (4 shards), `slots_real_test`, `launch_real_test`, `texts_real_test` and `stats_real_test`
(3 shards each) in `shell/BUILD.bazel` are `cc_test` targets that run the pinned Python through `python_test.c`
like `host_python_test` (which has no `shard_count`), with `tests/shard.py` as data. `shard.py` reads
`TEST_TOTAL_SHARDS` / `TEST_SHARD_INDEX`, touches `TEST_SHARD_STATUS_FILE` and answers `owns(n)`: case or unit n
runs in shard n modulo the shard count; without the variables everything runs. Every shard draws the seeded
streams in full, so a case is the same case at any shard count; only the expensive part (the run in the mapped
original, one worker process per run) happens in the owning shard.

| Test | Unit of sharding (numbered in a fixed order) | Shards |
|---|---|---|
| `bay_real_test` | ARM vehicle (9), launch-file state (57; the original runs on 8 of them), seeded sequence (74) | 4: shard 0 is the coverage shard, 1..3 own the comparisons |
| `slots_real_test` | save case (12 + 2 fault/not-loaded), load per corpus SAV (4 + 1), new career (6), preparation (82) | 3 |
| `launch_real_test` | worker chunk of 150 states (corpus, sweep), preference sequence (40) | 3 |
| `texts_real_test` | per language: pages 0 and 1, then each SAV of page 4 | 3 |
| `stats_real_test` | case (3 corpus + 60 seeded) | 3 |

*Coverage assertions are over the union.* `bay_real_test` asserts that the seeded sequences hit every operation
kind (clicks applied and refused, empty lists, unmount, guidance, both rebalance options, bank changed, launches).
The counters depend only on our side, so shard 0 steps and tallies every sequence without a worker, using the
states of the step-by-step run, and the other shards compare the sequences they own (sequence k in work shard k
modulo 3). `launch_real_test` tallies its coverage counters and preference control kinds from our side in every
shard.

*No case dropped or duplicated.* Every compared case calls `shard.ran(id)`; `shard.finish` writes the ids to
`TEST_UNDECLARED_OUTPUTS_DIR/<test>-shard-<i>.txt` and prints their count. The union of the shards' ids equals the
id list of an unsharded run (`--test_sharding_strategy=disabled`), with no id twice.

The memory worker (`shell-invitro`) is a one-shot process (operations on its command line `--op`, the memory disk
as `--file`, the whole disk back in one packet); the tests batch what it allows (`restore:N` chunks of 16 to 150
states in `editors`, `launch` and `practice`). `chain_real_test`, `menu_real_test`, `practice_real_test`,
`editors_real_test`, `editors2_real_test`, `draw_real_test` and `text_real_test` stay whole;
`chain_real_test`'s closing facts need the rows of every recorded suite.

**Adding a shard to a real test.** Number the cases in a fixed order (a counter, never the position in a filtered
list), draw every random stream in every shard, skip only the worker run and the comparisons of cases the shard
does not own, call `shard.ran(id)` for the cases it compares, keep union-wide assertions on our side only (tallied
in every shard, or in a reserved shard as `bay_real_test` does), add `tests/shard.py` to `data`, switch to a
`cc_test` with `shard_count`, and compare the id lists.

### Process identity in the clone guard

The guard's rule: PID, executable path and start time must all match, and nothing foreign is ever stopped.
`Get-Process -Id N` then `.Path` (`MainModule`) is wrong for a process that is still starting or exiting: it can
answer `C:\Windows\SYSTEM32\ntdll.dll` (the first module of the loader's incomplete list) or nothing, while
kernel32 `QueryFullProcessImageNameW` answers right. Under load from parallel workers that window widens.

- `CloneGuard.psm1` `Get-CloneProcessImagePath` (exported) reads the path from kernel32.
  `Get-CloneOwnedState` takes a differing start time or kernel32 path as final at once; a process whose path or
  start time cannot be read right now is looked at again for about 2 s and is then owned, gone or foreign.
  `Get-CloneGameProcess -Installation` falls back to the kernel32 path when CIM has no `ExecutablePath` yet (a CIM
  snapshot right after a start can lack it; inferred, not logged), so `Assert-CloneIdle` sees a game that has just
  started.
- `SuiteLaunch.psm1` `Stop-SuiteOwnedProcess` reads the image path through `Get-CloneProcessImagePath` in both
  identity checks (before and right before the stop); an unreadable path refuses with "Process identity changed".
- `test_clone_guard.py` gives every lane its own game name `ES2MOCK<random>.EXE`, so a lane sees only its own
  mock games; mock games that must outlive the lane run 600 s (the test kills them), those that must exit by
  themselves end 6 s after their stop file; a loop of twelve fresh games checks `Get-CloneOwnedState` right after
  the start. Every process enumeration of these tests is filtered by a directory inside the test's own temporary
  root or by a fixture game name, so parallel workers do not see each other's processes.
