# Reference game setup

How the original EarthSiege II 1.11 is installed as a portable installation, started, made video-compatible,
rebuilt and verified, how the own launcher, shell, runtime and simulator are deployed into installations, and
the tooling around the scripts (worker helper, evidence budget, window lint, script tests).

Tools are never installed automatically, and a new dependency needs approval first. The four
original runtime libraries `CW3220.DLL`, `BWCC32.DLL`, `SOS9503.DLL` and `SOSLIBS3.DLL` are approved; that does
not cover other tools, wrappers, codecs or drivers. Tool versions and pins: [dependencies.md](../dependencies.md).
The game side runs on Windows 10 x64 (WoW64) with the system's DirectDraw, WinMM, Video for Windows and Indeo
wrapper; no codec package or rendering wrapper is added.

## Start the installation

Double-click `START_ES2.cmd`. It stays in the project root and locates
`tools\reference-game\setup\start-native.ps1` relative to itself. The console closes after a successful launch
and stays open on an error. `START_ES2.cmd -Preview` from a terminal checks without starting the game; other
arguments are passed through to the script. Direct invocation from PowerShell 7:

```powershell
& '.\tools\reference-game\setup\start-native.ps1'
```

The starter uses the system PowerShell 7 (the Microsoft Store app alias in `%LOCALAPPDATA%\Microsoft\WindowsApps`,
otherwise `pwsh.exe` from PATH) with `-NoProfile`, installs nothing and sets no execution policy; without a
PowerShell 7 it prints the path it tried. Windows PowerShell 5.1's default blocks the script; PowerShell 7 runs
it under the existing RemoteSigned policy.

`start-native.ps1` (default installation `native-r111`):

- verifies the hashes of the original EXEs/DLLs against `installation-manifest.json`, and of the media proxy
  `ddraw.dll` and the system `SysWOW64\ddraw.dll` against `oracle-media-manifest.json`;
- mounts `build\extracted\earthsiege2-data.iso` read-only when needed, finds the `EARTHSIEGE2` volume and
  rewrites `DATA\DRIVE.CFG` when the drive letter changed;
- launches `ES.EXE` (or `ES2LAUNCH.EXE`, see [Own shell as the default](#own-shell-as-the-default)) with the
  installation as working directory; the children inherit the Windows `256COLOR` layer, the parent environment
  and persistent compatibility settings stay unchanged;
- refuses a second start while a game process of the same installation runs;
- writes one JSON record per launch below the `es2-data` logs.

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
colour/video configuration; close any running session first. `-CaptureCrash` builds the own x86 collector
`//tools/reference-game/helpers:capture_crash`, copies it to `build\reference-game\capture_crash.exe` and starts
the game under it. Through the Windows debugging API it observes `ES.EXE` and its children, records the first
access violation and unhandled exceptions with registers and process memory dumps, and keeps normal exception
delivery; after 32 rapid repetitions of the same access violation it captures the fault and detaches without
terminating the game. Reports go to a new `crash-capture-*` directory below the `es2-data` logs; no executable,
registry entry or error-reporting setting changes, and saves are not touched. `-Preview` builds the helper
without starting the game. Test: `tools/reference-game/helpers/tests/test_capture_crash.ps1`. The repeated
exception is the VSHELL save/load fault
([media_compatibility.md](media_compatibility.md#mission-map-lifetime-guard)).

## Prepare the original media

1. Verify the raw CD image's SHA-256 against `data/reference_hashes.txt` and keep an unmodified copy in
   `es2-dependencies/archive`.
2. `tools/reference-game/setup/extract_data_track.py <raw source> build\extracted\earthsiege2-data.iso`
   validates the Mode-1 sectors and refuses to overwrite. The raw source keeps its audio tracks; the ISO holds
   only the data session.
3. Extract that ISO with 7-Zip (`7z.exe`) into `build\extracted\cd`.
4. Compare the original binaries with `data/reference_hashes.txt`. Every CD file's SHA-256 is in
   `data/iso_file_hashes.sha256`, so a source that matches holds the scanned bytes;
   no new virus scan is needed and `prepare-native.ps1` asks for none.

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
proxy (`//tools/reference-game/proxy:ddraw.dll`) and the media probe with Bazel, stages the DLL in
`build\reference-game\proxy` and runs the decode and registry checks
([media_compatibility.md](media_compatibility.md#validation)). `configure-media.ps1` deploys the staged DLL:

```powershell
.\tools\reference-game\setup\validate-media.ps1
.\tools\reference-game\setup\configure-media.ps1 -VshellMeasures -Apply
.\tools\reference-game\setup\start-native.ps1
```

The build forwards all 22 exports of this machine's system DirectDraw, regenerates them from the current system
DLL and disables PE timestamps, so equal source, toolchain, repository path and system exports give identical
bytes. Only the validated `ddraw.dll` is deployed; rebuild and revalidate after changing the proxy or after a
system DirectDraw update (`start-native.ps1` rejects a mismatched system DLL).

`configure-media.ps1` (default `native-r111`) previews without `-Apply`. `-Apply` requires the exact validated
DLL hash, an unchanged system DirectDraw and a closed game, and copies only the DLL, `es2-oracle.ini` and the
ownership/hash manifest `oracle-media-manifest.json` into the installation. Unknown or edited files are not
overwritten; the game EXEs and media stay unchanged. Switches:

| Switch | `es2-oracle.ini` | Manifest |
|---|---|---|
| (none) | `[video] log=` (events to `video-compat.jsonl` in the `es2-data` logs) | |
| `-VshellMeasures` | `[video] system_indeo=1`, `[compat] mission_map_lifetime=1`: the VSHELL-only measures, for an installation that plays the original `VSHELL.EXE` | `vshell_measures` |
| `-OwnPresenter` | `[presentation] presenter=gdi`, `directdraw=windowed`: the own GDI presenter, DBSIM's DirectDraw in a window | `own_presenter` |
| `-ReplaceHooks` | `[hooks]` with every hook of `proxy/hook-names.txt` set to `replaced` | `replaced_hooks` |

Without `system_indeo=1` the proxy does not bind the Indeo decoder. `verify-native-rebuild.ps1` rebuilds the
expected INI from the manifest. Mechanism and scope of the measures (MCI redirection, VfW binding, no codec added
to `Drivers32`): [media_compatibility.md](media_compatibility.md).

## A separate fresh rebuild

A second installation from the same sources, leaving `native-r111` and its saves untouched. Run in PowerShell 7
with an unused installation name; every command names the rebuild:

```powershell
$rebuildName = 'rebuild-r111' # Use another unused name when this directory exists.
$rebuildEvidence = Join-Path (Join-Path $env:ES2_DATA_REPO 'logs') ('rebuild-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $rebuildEvidence | Out-Null
$mediaProof = Join-Path $env:ES2_DATA_REPO 'logs\media-compat-validation.json'
if (Test-Path -LiteralPath $mediaProof) {
    Copy-Item -LiteralPath $mediaProof -Destination (Join-Path $rebuildEvidence 'previous-media-validation.json')
}
& '.\tools\reference-game\setup\prepare-native.ps1' -InstallName $rebuildName -Language German -InstallBundledRuntime
& '.\tools\reference-game\setup\validate-media.ps1'
Copy-Item -LiteralPath $mediaProof -Destination (Join-Path $rebuildEvidence 'media-compat-validation.json')
& '.\tools\reference-game\setup\configure-media.ps1' -InstallName $rebuildName -Apply
& '.\tools\reference-game\setup\verify-native-rebuild.ps1' -InstallName $rebuildName -Language German -OutputPath (Join-Path $rebuildEvidence 'fresh-install-verification.json')
& '.\tools\reference-game\setup\start-native.ps1' -InstallName $rebuildName -Preview
```

Run the verifier **before the first game start**: the game writes settings, mission state and saves, so a played
installation differs. It compares every copied original file against the extracted CD, checks the seven pinned
binary hashes, the drive/language and compatibility configuration, the fresh file inventory, the manifests and
the current validation proof, rejects read-only `DATA`/`SAV` files, accepts the own-shell, own-sim and
retired-original files against their manifests, and writes only a new report outside the installation (an
existing report is refused). Start it with `& '.\START_ES2.cmd' -InstallName $rebuildName`. Both
installations append compatibility events to `video-compat.jsonl` in the `es2-data` logs; correlate them by
process ID.

## What is established

- `native-r111` plays the cockpit, quick start, intro picture and sound, sound effects and speed, a session of at
  least ten minutes and a save/restart/load cycle. Not established: mission completion, CD music and
  joystick-specific behaviour. Visual results come from play and recordings, not from automated screenshots;
  automated video decoding does not prove visible in-game playback.
- A scripted fresh rebuild passes file verification, compatibility tests and startup with all 72 primary files
  unchanged.
- Timing and state capture, movement calibration and deterministic replay build on it:
  [timing.md](../reverse-engineering/timing.md), [main_loop.md](../reverse-engineering/main_loop.md),
  [player_state.md](../reverse-engineering/player_state.md), [input_journal.md](input_journal.md),
  [oracle regression](oracle_regression.md).

## Own launcher

`tools/reference-game/setup/start-own-launcher.ps1` (default clone `calibration-r111`) starts the game through
the `ES.EXE` replacement [`launcher/`](../../launcher/README.md) after the same checks as `start-native.ps1`. It
refuses `native-r111` and clones whose `es2-oracle.ini` carries oracle settings (input replay, virtual time, tick
log, hooks, coverage, probes, perf) unless `-AllowOracleConfig`, copies the launcher as `ES2LAUNCH.EXE` and logs
every VSHELL/DBSIM start to `own-launcher-*.log` in the `es2-data` logs. `-DryRun` changes nothing. Contract:
[launcher.md](../reverse-engineering/launcher.md).

## Own shell as the default

`tools/reference-game/setup/configure-shell.ps1 -InstallName <name> -Apply` deploys the own launcher and the own
graphical shell: `ES2LAUNCH.EXE` beside the original files and `ES2SHELL\` with `es2shell.exe`, `SDL3.dll` and
`menu.ini` (`game_dir`, the `cd_root` of `DATA\DRIVE.CFG`; no fixed seed), recorded in
`own-shell-manifest.json`. Nothing original is touched; `VSHELL.EXE` stays in place, unused. `-Remove` deletes
the deployment; without a switch the script previews. `native-r111` is refused unless `-AllowPrimary`.
`prepare-native.ps1 -InstallBundledRuntime -OwnShell` deploys it right after a fresh installation.

With the manifest present, `start-native.ps1` (and so `START_ES2.cmd`) checks the hashes and starts
`ES2LAUNCH.EXE` with `ES2_LAUNCH_SHELL` set to the deployed shell; `menu.ini`'s `cd_root` follows the mounted CD
letter like `DRIVE.CFG`. The launcher writes `logs\own-launcher-<stamp>.log`, the shell `ES2SHELL\shell.log`.
`-Shell vshell` starts `ES.EXE` with the original VSHELL; `-Shell own` refuses an installation without the
deployment. The `256COLOR` layer stays for the launcher, VSHELL and DBSIM; `es2launch` leaves it out of the own
shell's environment
([launcher.md](../reverse-engineering/launcher.md#the-replacement-shell-without-the-256color-layer)).
`verify-native-rebuild.ps1` checks the deployed files against the manifest. Test:
`//tools/reference-game/setup:configure_shell_test`.

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
`oracle-runtime-manifest.json`. Refused: `native-r111` unless `-AllowPrimary` (and then only with its own
installation manifest, no `measurement_clone_of`), an installation that is not a separate measurement clone,
linked paths, a running game process of the clone, a clone another session holds, and any unexpected state.
`start-native.ps1` and the scripts that call its preview accept a deployed clone and add `runtime` to their
records. In the clone DBSIM runs the own runtime; `ES.EXE` and `VSHELL.EXE` run the original through it. Design:
[rtl_imports.md](rtl_imports.md#the-own-cw3220dll). Test: `//tools/reference-game/setup:configure_runtime_test`.

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
the guest image from its own data. `es2launch.ini` holds the command inside one more quote pair,
`command = "<command>"`, because `GetPrivateProfileString` strips a quote pair around a whole value
([own_process.md](own_process.md) "es2sim without DBSIM.EXE").

Applying the same files again reports `applied: false`; another build or command replaces the deployment.
**`-Remove`** deletes `ES2SIM\`, `es2launch.ini` and the manifest; every `ES2LAUNCH.EXE` started there then runs
`DBSIM.EXE` again.

**Refusals.** No original file is opened for writing. Refused: `native-r111` without `-AllowPrimary`; an
installation without `installation-manifest.json`; unexpected own-sim files or files whose hashes differ from the
manifest; a running game process of the installation, es2sim included; a clone that another session holds
(CloneGuard; `ES2_CLONE_LOCK_TOKEN` lets a caller that holds the lock in); a `DBSIM.EXE` whose hash is not the
installation manifest's; sources that are not 32-bit PE images or that are original files; paths that are not
plain ASCII or that contain quotes. Test: `//tools/reference-game/setup:configure_sim_test` (tag `scripts`).

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
DBSIM's proxy); `-ConfigureSimScript`, `-SimExe` and `-SimSdl` need `-Sim own`. `-CdRoot <dir>` hands the chain
the CD root the own executables read, for example a copy without `VER95\DBSIM.EXE` and `VER95\VSHELL.EXE`. The
dry run shows the preview of `configure-sim.ps1`. The clone's process list includes `ES2SIM\es2sim.exe`, so the
clone guard and the restore see it. Test: `start_own_menu_test` with a fixture launcher and a fake
`configure-sim.ps1`.

### `check.ps1 -Live -OwnSim`

The stage `own-sim` runs every suite through es2sim with the frame compare, then the unattended chain above on
one clone; stage, options and verdict: [oracle_regression.md](oracle_regression.md#acceptance-script). With
`-OwnSim`, `sim/` and `rtl/` are source-stamp paths of the live stage. Test: `//tools:check_script_test` with
fake drivers (size `large`, about 240 s).

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
deletes the record. **Refused:** a measurement clone (`measurement_clone_of` in the manifest); `native-r111`
without `-AllowPrimary`; names that are not plain installation names; links; a clone another session holds
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
files).

## One-command acceptance of the own menu

`start-own-menu.ps1` (default clone `calibration-r111`) is the single command for the own menu: it backs up
`DATA` and `SAV` of the clone to `own-menu-<stamp>` in the `es2-data` logs, builds and sets up the menu, starts
it through `start-own-launcher.ps1` (menu console and game windows visible by design), waits for the session,
collects the evidence, restores the two folders in a `finally` and prints the compare checklist. `-Shell` is
`text` (default), `gui` or `vshell`. It refuses `native-r111` (unless `-AllowPrimary`), a running game process
of the clone, a clone another session holds and linked paths; `-DryRun` lists the steps and changes nothing;
`-RestoreOnly <session dir>` restores an interrupted session. It never stops a process. Routes, evidence layout,
exit codes and checklist: [launcher.md](../reverse-engineering/launcher.md#owner-acceptance-one-command). Test:
`//tools/reference-game/setup:start_own_menu_test` (tag `scripts`).

Interactive sessions warn when the clone has no proxy `ddraw.dll`, a proxy without the `[presentation]` options or
an `es2-oracle.ini` without `directdraw=windowed`, because DBSIM then crashes after the shell's Full Screen
(prefs byte 6 = 1; `0x4654a8`). `-WindowedDirectDraw` sets
`directdraw=windowed` and `presenter=gdi` for the session and restores the INI afterwards
([launcher.md](../reverse-engineering/launcher.md#dbsims-full-screen-in-interactive-sessions)).

## Worker helper

`tools\worker.ps1` sets up and closes a worker of the parallel protocol: one git worktree, one branch and one
Bazel output base per topic (the first argument, `-Package`). A topic name is lower case and contains a hyphen
(`doc-fix`), or is `wp<n>`: `^[a-z][a-z0-9]*(-[a-z0-9]+)*$`; `cache` and `install` are reserved.
`tools\clean-worker-bazel.ps1` applies the same rule.

```powershell
& .\tools\worker.ps1 doc-fix
```

It creates the worktree `es2-doc-fix` beside the repository on the new branch `claude/doc-fix` (from `main`, or from `-Base`) with a
copy of the main checkout's `local.bazelrc`, and the git-ignored scratch directory `build\claude\doc-fix` inside the worktree, and prints the commands to run in
PowerShell: `Set-Location` into the worktree, then

```
bazel --output_base=build\bazel\doc-fix test --repo_contents_cache=build\bazel\doc-fix-contents --local_resources=cpu=4 <targets>
bazel --output_base=build\bazel\doc-fix shutdown
```

It refuses an existing worktree directory, an existing branch, a missing base branch or a bad name, and starts no
Bazel.

```powershell
& .\tools\worker.ps1 doc-fix -Finish            # when the worker is done
& .\tools\worker.ps1 doc-fix -Finish -Archive   # ... and copy the evidence
```

`-Finish` shuts down the Bazel servers of the output bases `<topic>`, `<topic>-mut` and `<topic>-mutation` (those
with a server pid file) and prints the next steps; it removes nothing. The steps: copy `build\claude\<topic>`
to `<topic>-<yyyyMMdd>` in the `es2-data` logs, `git worktree remove ..\es2-<topic>` (deletes the ignored
files in it), `git branch -d claude/<topic>` after the merge, and `tools\clean-worker-bazel.ps1 <topic>`.

`-Archive` (only with `-Finish`) does the copy itself and nothing else in `es2-data`. It leaves out
`mutation-workspace-*` directories and `*.exe`, `*.dll`, `*.lst`, `*.asm`, `*.elf`, `*.pkl` at any depth (copies
of or derived from the original's bytes, or regenerable binaries) and follows no links. It copies into
`<target>.partial` and renames it, and refuses if the target or the staging directory exists, if the evidence
root does not exist (it is never created) or if `-Date`, the `yyyyMMdd` part of the target name (default: today),
is not eight digits.

`clean-worker-bazel.ps1 <topic>[,<topic>...]` deletes the output bases `<topic>` and, where they exist,
`<topic>-contents`, `<topic>-mut`, `<topic>-mut-contents`, `<topic>-mutation` and `<topic>-mutation-contents`
below `build\bazel`; `-DryRun` lists. It shuts down a running Bazel server of a base first, unlinks
every reparse point before it deletes anything (Bazel's execroot holds junctions into the source tree) and
stops if one cannot be unlinked. It refuses the reserved names, names that are not topic names and a topic whose
worktree `es2-<topic>` still exists.

The path parameters (`worker.ps1`: `-Repo`, `-WorktreeParent`, `-OutputRoot`, `-EvidenceRoot`, `-Base`, `-Date`;
`clean-worker-bazel.ps1`: `-OutputRoot`, `-Repo`, `-WorktreeParent`) default to the real locations and exist for
`//tools:worker_scripts_test`, which runs both scripts against a temporary git repository, output root and
evidence root with a fake `bazel.cmd`.

## Evidence budget

`tools/evidence_budget.py` guards the size and content of `es2-data` (policy:
[README.md](../../README.md#verification)). It only reads; it never stages, deletes or commits.
`tools/check.ps1` runs it as its last stage `evidence` (`-Es2Data`, `-SkipEvidence`). By hand before a commit of
`es2-data`:

```powershell
python -B tools\evidence_budget.py [--data-root <es2-data checkout>] [--older-than <yyyy-mm-dd>] [--max-bytes 5242880]
```

Exit 0 nothing refused, 1 something staged is refused (listed with the reason), 2 usage. Refused when staged: the
raw tick log of an identical run (`*.jsonl` directly in a `run` directory whose `*comparison*.json` says
`"identical": true`), an empty `*.hooks.jsonl`
([oracle_regression.md](oracle_regression.md#empty-hook-mismatch-logs)), anything inside `mutation-workspace*`,
`*.lst`, `*.asm`, `*.elf`, `*.pkl`, `*.dmp`, and a file over 5 MiB unless its path is in
`es2-data/.es2budget-allow` (size rule only). It also prints the sizes of `logs`, `.git` and `.git/lfs` and the
top-level `logs` directories older than `--older-than` that no document cites; those are candidates for a discard,
which is decided by hand. The script header has the full rules. Test: `//tools:evidence_budget_test`.

## Console windows

A console program started by a process that has **no console of its own** (a GUI program, a scheduled task, a
hidden launcher) gets a new console window. A child of a process that **has** a console shares it and opens
nothing, also when that console has no window: a Bazel test has such a console (`GetConsoleWindow()` is 0 inside a
`host_python_test`), and so does anything started with `CREATE_NO_WINDOW` / `CreateNoWindow = $true`.

`//tools:process_starts_test` (`tools/test_process_starts.py`) scans every tracked `*.ps1`, `*.psm1`, `*.py`,
`*.c`, `*.h`, `*.cmd` and `*.bat` file (never `bazel-*`, `build/`, `es2-data/`) and fails for a process start
with neither a no-window flag nor an allow comment. The flags per language (`CREATE_NO_WINDOW` /
`DETACHED_PROCESS`, `-WindowStyle Hidden` / `-NoNewWindow` / `CreateNoWindow = $true`, `creationflags=`,
`start /B`) are listed in the script header. The PowerShell call operator (`& program`) is not scanned: whether
it opens a window depends only on the caller's console. `START_ES2*.cmd` only call `pwsh` and are visible by
design.

A start without a flag carries `window-lint: allow <reason>` (at least eight characters) in a comment on its line,
on the line above or inside a multi-line call; `window-lint: allow-file <reason>` in the first 40 lines covers the
file. The reason names the rule that makes the start safe: `gui-child <exe>` (a GUI-subsystem child such as
`ES.EXE`, `VSHELL.EXE`, `DBSIM.EXE` or the `-mwindows` launcher), `inherits console: ...` (a console child whose
parent owns a console: Bazel tests, the `es2menu` console, scripts run from a console or from `check.ps1`) or
`visible on purpose` (an `-Interactive` game start, record mode). A marker without a start next to it is an error
("stale marker"). `python -B tools\test_process_starts.py --list` (the pinned Python) prints every start with its
disposition. A new script that starts a program passes the flag; an allow comment is only for one of the three
rules.

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
carry the tags `scripts` and `manual` and form the suite `//tools:scripts`, so `bazel test //...` skips them;
`tools/check.ps1 -Full` runs `//tools:scripts` with `differential_full` in its full stage. Run
`bazel test //tools:scripts` after a change to `worker.ps1`, `clean-worker-bazel.ps1`, `check.ps1`,
`check_counters.py`, `check-fresh-clone.ps1`, `setup-dependencies.ps1`, `setup-platform-dependencies.ps1`,
`setup-web-dependencies.ps1` or `python_tree_digest.py`. None of the three is `no-cache`: every file they read is
declared data and the rest is built in their temporary directory. The `no-cache` tag elsewhere marks tests that
read sibling scripts, registries or recorded suite outputs that are not declared data, or that measure timing.

To measure test wall time: `bazel test //... --nocache_test_results --profile=<file>` after a `bazel build //...`
that warms the disk cache; per-test durations come from the profile (the `TestRunner` wall time inside each
`Testing //x` action), not from the `PASSED in` lines. Bazel 9 has no `analyze-profile`.

### Sharding

Script tests with independent sections use Bazel's sharding variables: `shard_items()` in
`setup/pwsh_session.py` runs section or lane i in shard i modulo the shard count and fails when there are more
shards than sections; without the variables everything runs. The shard count is the target's `shard_count`.
Sharded: `oracle_speed_scripts_test` and `suite_scripts_test` (sections, lanes),
`//tools/reference-game/proxy:simulation_hook_configuration_test` (iteration k of the per-hook registry loop in
shard k modulo the count, the closing part in shard 0), the in-vitro hook groups and sim suites with `shards > 1`
([invitro/README.md](../../tools/reference-game/invitro/README.md)) and `differential_rtl_test` and `_full_test`
(function f in shard f modulo 2; every function and mode has its own random stream, `rtl_reseed` in
`rtl_cases.h`, so a sharded run executes the cases of an unsharded one). `suite_scripts_test` plays the
registered suites through `run-oracle-regression.ps1` in groups of at most `SUITES_PER_GROUP` = 10, one lane
each, so a step stays under the 120 s step timeout whatever the suite count; `lane_legacy_runner` keeps the
option checks and runs only the baseline suites `acceptance-60s` and `c3_09-loadout-b`.

The shell real tests `bay_real_test`, `slots_real_test`, `launch_real_test`, `texts_real_test` and
`stats_real_test` (`shell/BUILD.bazel`) are `cc_test` targets that run the pinned Python through `python_test.c`,
because `host_python_test` has no `shard_count`; `shell/tests/shard.py` is their data. `owns(n)` is true when case
n runs in this shard (n modulo the shard count); every shard draws the seeded streams in full, so a case is the
same case at any shard count, and only the run in the mapped original (one worker process per run) happens in
the owning shard. Counters that depend only on our side are tallied in every shard or in a reserved shard that
steps every sequence without a worker (`bay_real_test`: shard 0). Every compared case calls `shard.ran(id)`;
`shard.finish` writes the ids to `TEST_UNDECLARED_OUTPUTS_DIR/<test>-shard-<i>.txt`, and their union equals the
id list of an unsharded run (`--test_sharding_strategy=disabled`) with no id twice. The memory worker
(`shell-invitro`) is a one-shot process, so the tests batch what it allows (`restore:N` chunks of 16 to 150
states); `chain_real_test`, `menu_real_test`, `practice_real_test`, `editors_real_test`, `editors2_real_test`,
`draw_real_test` and `text_real_test` stay whole.

**Adding a shard to a real test.** Number the cases in a fixed order (a counter, never the position in a filtered
list), draw every random stream in every shard, skip only the worker run and the comparisons of cases the shard
does not own, call `shard.ran(id)` for the cases it compares, keep union-wide assertions on our side only, add
`tests/shard.py` to `data`, switch to a `cc_test` with `shard_count`, and compare the id lists.

### Process identity in the clone guard

PID, executable path and start time must all match, and nothing foreign is ever stopped. The path comes from
kernel32 `QueryFullProcessImageNameW` (`Get-CloneProcessImagePath` in `CloneGuard.psm1`), never from
`Get-Process -Id N` then `.Path`: for a process that is still starting or exiting that can answer
`C:\Windows\SYSTEM32\ntdll.dll` or nothing, the more often the busier the machine. `Get-CloneOwnedState` takes a
differing start time or path as final at once and looks again for about 2 s at a process whose path or start time
cannot be read; `Get-CloneGameProcess -Installation` falls back to the kernel32 path when CIM has no
`ExecutablePath` yet; `Stop-SuiteOwnedProcess` (`SuiteLaunch.psm1`) checks the path before and right before the
stop and refuses with "Process identity changed". `test_clone_guard.py` gives every lane its own game name
`ES2MOCK<random>.EXE` and filters every process enumeration by a directory of its own temporary root or by that
name, so parallel workers do not see each other's processes.
