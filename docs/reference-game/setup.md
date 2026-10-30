# Reference game setup

How the original EarthSiege II 1.11 is installed as a portable installation, made video-compatible and
started.

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
- launches `ES.EXE` with the installation as working directory; its children inherit the Windows `256COLOR`
  layer, the parent environment and persistent compatibility settings stay unchanged;
- refuses a second start while a game process of the same installation runs;
- writes one JSON record per launch below `es2-data\logs`.

| Option | Effect |
|---|---|
| `-Preview` | verifies the files and prints the launch configuration; mounts and starts nothing |
| `-InstallName <name>` | another installation below the install root |
| `-UseNativeColors` | diagnostic: omits the `256COLOR` layer |

Installations live in `build\installs\<name>` (`Get-CloneInstallRoot` of `CloneGuard.psm1`).

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
   `data/iso_file_hashes.sha256`.

## Create a portable installation

Preview, then the installation after approval of the four bundled runtimes:

```powershell
.\tools\reference-game\setup\prepare-native.ps1
.\tools\reference-game\setup\prepare-native.ps1 -InstallBundledRuntime -Language German
```

This creates `build\installs\native-r111` (or `-InstallName`), copies the approved local files,
writes the original-derived configuration and `installation-manifest.json`, and verifies the original
executable and runtime hashes. No registry write, system DLL replacement, codec installation or download. An
existing destination is refused; a second installation takes another `-InstallName`
([A second installation](#a-second-installation)). `-Language` is `German` (default), `English` or `French`.

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
overwritten; the game EXEs and media stay unchanged.

| Switch | `es2-oracle.ini` | Manifest |
|---|---|---|
| (none) | `[video] log=` (events to `es2-data\logs\video-compat.jsonl`) | `vshell_measures` false |
| `-VshellMeasures` | `[video] system_indeo=1`, `[compat] mission_map_lifetime=1` | `vshell_measures` true |

Without `system_indeo=1` the proxy does not bind the decoder and the intro stays black; an installation that is
played needs `-VshellMeasures`.

With the measures on, the proxy redirects the main executable's two MCI import slots in memory; the first video
call initializes the present Windows Indeo wrapper through `OpenDriver` and a process-local VfW binding, with a
DirectDraw initialization fallback for other call orders. No codec is added to `Drivers32`; system rendering
functions are forwarded unchanged. Scope and evidence: [media_compatibility.md](media_compatibility.md).

## A second installation

A second installation from the same sources leaves `native-r111` and its saves untouched. Run in PowerShell 7
with an unused installation name:

```powershell
$name = 'original-r111' # Use another unused name when this directory exists.
& 'tools\reference-game\setup\prepare-native.ps1' -InstallName $name -Language German -InstallBundledRuntime
& 'tools\reference-game\setup\validate-media.ps1'
& 'tools\reference-game\setup\configure-media.ps1' -InstallName $name -VshellMeasures -Apply
& 'tools\reference-game\setup\start-native.ps1' -InstallName $name -Preview
& 'START_ES2.cmd' -InstallName $name
```

All installations append compatibility events to `<es2-data>\logs\video-compat.jsonl`; correlate them by
process ID.

## What the installation is confirmed to do

- `native-r111` with the original `VSHELL.EXE` and the VSHELL measures: the owner confirmed a correct cockpit,
  quick start, intro picture and sound, sound effects, plausible speed, at least ten minutes of play and a
  save/restart/load cycle. Mission completion, CD music and joystick-specific behaviour are not established.
- This branch's proxy keeps only the forwarding, the MCI redirection, the IV32 binding and the guard, with the
  same code; a live run with this build is not recorded.

Visual results come from the owner, not from automated screenshots; automated video decoding does not prove
visible in-game playback.
