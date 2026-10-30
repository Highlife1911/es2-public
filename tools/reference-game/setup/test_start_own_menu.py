# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""start-own-menu.ps1 (one-command owner acceptance of the own menu) on synthetic installations.

Everything lives in a temporary DataRoot: installations of stand-in files of our own (no game file), the real
es2menu_backend.py `setup` (it only writes menu.ini), a fixture launcher script that stands in for
start-own-launcher.ps1 and plays the owner's session (it changes DATA and SAV, runs the stand-in DBSIM of
//shell:chain_real_test, leaves a campaign directory), and a mock game process (clone_guard_mock_game). The real game,
the real menu and every real installation stay untouched.

Usage: test_start_own_menu.py <start-own-menu.ps1> <clone_guard_mock_game.exe> <stand_in_child.exe> <es2menu_backend.py>
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time

SCRIPT = Path(sys.argv[1]).resolve()
MOCK_GAME = Path(sys.argv[2]).resolve()
STAND_IN_DBSIM = Path(sys.argv[3]).resolve()
BACKEND = Path(sys.argv[4]).resolve()
SETUP = SCRIPT.parent
PWSH = os.environ.get("ES2_PWSH") or shutil.which("pwsh")
NO_WINDOW = 0x08000000
sys.path.insert(0, str(SETUP))
from pwsh_session import PwshSession  # noqa: E402
SESSION = None
SCRATCH = None
checks = 0

# The fixture launcher script: stands in for start-own-launcher.ps1. FAKE_MODE selects what the "owner" does.
FAKE_LAUNCHER = r'''
[CmdletBinding()]
param([string]$DataRoot, [string]$InstallName, [switch]$DryRun, [switch]$AllowOracleConfig)
$ErrorActionPreference = 'Stop'
$installation = Join-Path $DataRoot "installs\$InstallName"
if ($DryRun) { @{dry_run = $true; installation = $installation} | ConvertTo-Json; return }
$shell = if ($env:FAKE_SHELL) { $env:FAKE_SHELL } else { 'text' }
$menu = $null
$unattended = $env:FAKE_UNATTENDED -eq '1'
if ($shell -eq 'vshell') {
    if ($env:ES2_LAUNCH_SHELL) { throw 'ES2_LAUNCH_SHELL is set for the original VSHELL' }
} else {
    if (!$env:ES2_LAUNCH_SHELL -or $env:ES2_LAUNCH_SHELL -notmatch '--menu "([^"]+)"') { throw 'ES2_LAUNCH_SHELL is not set like the owner session needs it' }
    $menu = $Matches[1]
    if ($unattended) {
        # No menu console, no message boxes, the oracle settings accepted, the menu reads its script.
        if ($null -ne $env:ES2_LAUNCH_SHELL_CONSOLE) { throw 'ES2_LAUNCH_SHELL_CONSOLE is set in an unattended run' }
        if ($env:ES2_LAUNCH_NO_UI -ne '1') { throw 'ES2_LAUNCH_NO_UI is not 1' }
        if (!$AllowOracleConfig) { throw '-AllowOracleConfig is missing' }
        if ($shell -eq 'gui') {
            # es2shell headless with one script and report per start ({X} = the -X entry mode).
            if ($env:ES2_LAUNCH_SHELL -notmatch '--headless --script "([^"]+x\{X\}\.txt)" --report "([^"]+x\{X\}\.txt)"$') { throw 'no --headless --script/--report with {X} at the end of ES2_LAUNCH_SHELL' }
            $scriptPattern = $Matches[1]; $reportPattern = $Matches[2]
            $inputFile = $scriptPattern.Replace('{X}', '1')
            foreach ($x in @('1', '3')) { if (!(Test-Path -LiteralPath $scriptPattern.Replace('{X}', $x))) { throw "no script for -X$x" } }
        } else {
            if ($env:ES2_LAUNCH_SHELL -notmatch '--input "([^"]+)"$' -or !(Test-Path -LiteralPath $Matches[1])) { throw 'no --input file at the end of ES2_LAUNCH_SHELL' }
            $inputFile = $Matches[1]
        }
    } elseif ($shell -eq 'text' -and $env:ES2_LAUNCH_SHELL_CONSOLE -ne '1') { throw 'ES2_LAUNCH_SHELL_CONSOLE is not 1' }
    if ($shell -eq 'gui' -and ($env:ES2_LAUNCH_SHELL_CONSOLE -or $env:ES2_LAUNCH_SHELL -notmatch 'es2shell\.exe" --menu')) { throw 'the graphical shell needs ES2_LAUNCH_SHELL = es2shell.exe --menu and no console' }
}
$mode = $env:FAKE_MODE
$index = 0
while (Test-Path (Join-Path $DataRoot "logs\fake-launcher-$index.log")) { $index++ }
$log = Join-Path $DataRoot "logs\fake-launcher-$index.log"
$data = Join-Path $installation 'DATA'; $sav = Join-Path $installation 'SAV'
# What the owner's session does to the installation.
[IO.File]::WriteAllBytes((Join-Path $sav 'GAME_R.SAV'), [byte[]](1..200))
[IO.File]::WriteAllText((Join-Path $data 'PREFS.CFG'), "changed by the session $index")
Remove-Item -LiteralPath (Join-Path $sav 'GAME_T.SAV') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $data 'newdir') | Out-Null
[IO.File]::WriteAllText((Join-Path $data 'newdir\launch.bin'), 'written by the menu')
[IO.File]::WriteAllText((Join-Path $sav 'GAME_3.SAV'), 'slot 3')
Remove-Item -LiteralPath (Join-Path $data 'sub') -Recurse -Force -ErrorAction SilentlyContinue
# The stand-in DBSIM of chain_real_test (codes.txt gives its exit code) runs as the mission.
[IO.File]::WriteAllText((Join-Path $data 'codes.txt'), "3`n")
$mission = Start-Process -FilePath (Join-Path $installation 'DBSIM.EXE') -WorkingDirectory $data -WindowStyle Hidden -PassThru -Wait
if ($menu) {
    $campaign = Join-Path $menu 'campaign-000'
    New-Item -ItemType Directory -Force -Path $campaign | Out-Null
    [IO.File]::WriteAllText((Join-Path $menu 'current'), "campaign-000`n")
    if ($shell -eq 'gui') { [IO.File]::WriteAllText((Join-Path $menu 'shell.log'), "shell log, run $index`n") }
    else { [IO.File]::WriteAllText((Join-Path $menu 'menu.log'), "menu transcript, run $index`n") }
    [IO.File]::WriteAllText((Join-Path $campaign 'chain.log'), "chain log`n")
    $missions = '[]'
    if ($unattended) {
        # The proxy's tick log (the path the fake configure script recorded) and what DBSIM returned, as the chain keeps it.
        $tickPath = (Get-Content -LiteralPath (Join-Path $installation 'fake-tick-log-path.txt') -Raw).Trim()
        if ($mode -ne 'no-ticklog') { Copy-Item -LiteralPath $env:FAKE_TICK_TEMPLATE -Destination $tickPath }
        New-Item -ItemType Directory -Force -Path (Join-Path $campaign 'mission-000\dbsim') | Out-Null
        foreach ($n in @('results.dat', 'mission.var')) { Copy-Item -LiteralPath (Join-Path $env:FAKE_OUTPUTS $n) -Destination (Join-Path $campaign "mission-000\dbsim\$n") }
        $missions = '[{"index": 0, ' + $env:FAKE_FIRST_EXTRA + '"files": ' + $env:FAKE_FILES + '}]'
        [IO.File]::WriteAllText((Join-Path $DataRoot 'logs\fake-input-seen.txt'), [IO.File]::ReadAllText($inputFile))
        if ($shell -eq 'gui') {
            # What es2shell's headless mode writes at the end of each start (main.c's end line).
            $x3errors = if ($mode -eq 'gui-error') { 1 } else { 0 }
            [IO.File]::WriteAllText($reportPattern.Replace('{X}', '1'), "argv dummy -eggplant -X1 -G`nend passes=900 presents=40 errors=0 exit=2 screen=7 quit=1 exit=2`n")
            if ($mode -ne 'gui-no-x3') {
                $errorLine = if ($x3errors) { "error 2 expect page=4: page=0`n" } else { '' }
                [IO.File]::WriteAllText($reportPattern.Replace('{X}', '3'), "argv dummy -eggplant -X3 -G`n${errorLine}end passes=300 presents=20 errors=$x3errors exit=0 screen=0 quit=1 exit=0`n")
            }
        }
    }
    [IO.File]::WriteAllText((Join-Path $campaign 'chain.json'), '{"schema": 1, "finished": null, "completed": 1, "save": "save-001.sav", "pending": null, "missions": ' + $missions + '}')
}
$dbsimCode = if ($mode -eq 'dbsim-1') { 1 } else { $mission.ExitCode }
# With the own simulator deployed (es2launch.ini of the fake configure-sim) es2launch starts es2sim in its place.
$simName = 'dbsim.EXE'; $simArgs = 'dummy -eggplant -X2 -R0 -G'
if ($env:FAKE_SIM -eq 'own') {
    if (!(Test-Path -LiteralPath (Join-Path $installation 'es2launch.ini'))) { throw 'no es2launch.ini: the own simulator is not deployed' }
    if ($null -ne $env:ES2_LAUNCH_SIM -or $null -ne $env:ES2_LAUNCH_SIM_OUTPUT) { throw 'ES2_LAUNCH_SIM or ES2_LAUNCH_SIM_OUTPUT is set' }
    $simName = 'sim-replacement'; $simArgs = "`"$installation\ES2SIM\es2sim.exe`" --headless --arguments `"-eggplant -X2 -R0 -G`""
}
[IO.File]::WriteAllText($log, ((@(
    "10:00:00.000 launcher",
    "10:00:00.001 start shell-replacement $env:ES2_LAUNCH_SHELL dummy -eggplant -X1 -G",
    "10:00:01.000 exit shell-replacement 2",
    "10:00:01.001 start $simName $simArgs",
    "10:00:02.000 exit $simName $dbsimCode",
    "10:00:02.001 start shell-replacement $env:ES2_LAUNCH_SHELL dummy -eggplant -X$dbsimCode -G",
    "10:00:03.000 exit shell-replacement 0",
    "10:00:03.001 end 0") -join "`n") + "`n"))
if ($mode -eq 'throw') { throw 'the fixture launcher failed after changing the installation' }
if ($mode -eq 'crash') { [Environment]::Exit(7) }
# The "launcher" process of the session: a mock game that exits by itself after a moment.
Copy-Item -LiteralPath $env:FAKE_MOCK -Destination (Join-Path $installation 'ES2LAUNCH.EXE') -Force
$launcherMs = if ($mode -eq 'hang') { '600000' } else { '1200' }
$launcher = Start-Process -FilePath (Join-Path $installation 'ES2LAUNCH.EXE') -ArgumentList $launcherMs -WorkingDirectory $installation -WindowStyle Hidden -PassThru
if ($mode -eq 'hang') {
    # A launcher and a DBSIM that never end (a stalled mission); the unattended timeout stops both.
    Copy-Item -LiteralPath $env:FAKE_MOCK -Destination (Join-Path $installation 'DBSIM.EXE') -Force
    $null = Start-Process -FilePath (Join-Path $installation 'DBSIM.EXE') -ArgumentList '600000' -WorkingDirectory $installation -WindowStyle Hidden -PassThru
}
if ($mode -eq 'leftover') {
    # A DBSIM that outlives the launcher until the test creates the stop file.
    Copy-Item -LiteralPath $env:FAKE_MOCK -Destination (Join-Path $installation 'VSHELL.EXE') -Force
    $null = Start-Process -FilePath (Join-Path $installation 'VSHELL.EXE') -ArgumentList @('100', "`"$env:FAKE_STOPFILE`"") -WorkingDirectory $installation -WindowStyle Hidden -PassThru
}
@{launcher = (Join-Path $installation 'ES2LAUNCH.EXE'); launcher_pid = $launcher.Id; launch_log = $log; dry_run = $false} | ConvertTo-Json
Write-Output "EarthSiege II started through our launcher (PID $($launcher.Id)). Log: $log"
'''

# Stands in for configure-tick-log.ps1. Records its arguments (and whether the clone lock token it inherits is
# the lock holder's) and where the "proxy" is to write the tick log; deploys nothing.
FAKE_CONFIGURE = r'''
[CmdletBinding()]
param([string]$DataRoot, [string]$InstallName, [string]$TickLogPath, [int]$VirtualTimeStepMs, [int]$VirtualTimePace,
      [int]$VirtualTimeIntraFrame, [string]$InputMode, [string]$InputJournal, [string[]]$Watch, [int]$TickTarget, [string]$Hooks,
      [switch]$HiddenWindow, [string]$PreparedProxy, [switch]$Apply)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $env:FAKE_SETUP 'CloneGuard.psm1') -Force
$installation = Join-Path $DataRoot "installs\$InstallName"
if (Test-Path -LiteralPath $TickLogPath) { throw 'the tick log exists' }
if (!(Test-Path -LiteralPath (Split-Path -Parent $TickLogPath) -PathType Container)) { throw 'no tick log directory' }
$holder = Read-CloneLock (Get-CloneLockPath $DataRoot $InstallName)
[ordered]@{InstallName = $InstallName; TickLogPath = $TickLogPath; VirtualTimeStepMs = $VirtualTimeStepMs; VirtualTimePace = $VirtualTimePace
    VirtualTimeIntraFrame = $VirtualTimeIntraFrame; InputMode = $InputMode; InputJournal = $InputJournal; Watch = @($Watch); TickTarget = $TickTarget
    Hooks = $Hooks; HiddenWindow = [bool]$HiddenWindow; PreparedProxy = $PreparedProxy; Apply = [bool]$Apply
    TokenIsHolders = [bool]($holder -and $env:ES2_CLONE_LOCK_TOKEN -and $holder.token -ceq $env:ES2_CLONE_LOCK_TOKEN)} |
    ConvertTo-Json | Set-Content -LiteralPath $env:FAKE_CONFIGURE_LOG -Encoding UTF8
Set-Content -LiteralPath (Join-Path $installation 'fake-tick-log-path.txt') -Value $TickLogPath
Write-Output 'fake configure: done'
'''

# Stands in for configure-sim.ps1. Logs each call (one JSON line: arguments, whether the clone lock token it
# inherits is the holder's, the es2sim configuration it was given); -Apply writes a marker es2launch.ini and records the
# tick log path for the fake launcher, -Remove deletes the marker; the output is configure-sim's JSON plan.
FAKE_CONFIGURE_SIM = r'''
[CmdletBinding()]
param([string]$DataRoot, [string]$InstallName, [string]$Sim, [string]$Sdl, [string]$Config, [switch]$Headless, [string]$TickLog,
      [string]$Output, [switch]$Apply, [switch]$Remove, [switch]$AllowPrimary)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $env:FAKE_SETUP 'CloneGuard.psm1') -Force
$installation = Join-Path $DataRoot "installs\$InstallName"
$ini = Join-Path $installation 'es2launch.ini'
$holder = Read-CloneLock (Get-CloneLockPath $DataRoot $InstallName)
$state = if (Test-Path -LiteralPath $ini) { 'own' } else { 'none' }
$entry = [ordered]@{Apply = [bool]$Apply; Remove = [bool]$Remove; Headless = [bool]$Headless; Config = $Config; TickLog = $TickLog; Output = $Output
    Sim = $Sim; Sdl = $Sdl; State = $state; TokenIsHolders = [bool]($holder -and $env:ES2_CLONE_LOCK_TOKEN -and $holder.token -ceq $env:ES2_CLONE_LOCK_TOKEN)}
if ($Apply) {
    $bytes = [IO.File]::ReadAllBytes($Config)
    $entry.ConfigBom = $bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE
    $entry.ConfigText = [Text.Encoding]::Unicode.GetString($bytes, 2, $bytes.Length - 2)
}
Add-Content -LiteralPath $env:FAKE_CONFIGURE_SIM_LOG -Value ($entry | ConvertTo-Json -Compress)
if ($Apply -and $env:FAKE_SIM_MODE -eq 'refuse') { throw 'fake configure-sim: refused' }
if ($Apply) {
    Set-Content -LiteralPath $ini -Value '[sim]'
    Set-Content -LiteralPath (Join-Path $installation 'fake-tick-log-path.txt') -Value $TickLog
}
if ($Remove -and (Test-Path -LiteralPath $ini)) { Remove-Item -LiteralPath $ini }
[ordered]@{state = $(if (Test-Path -LiteralPath $ini) { 'own' } else { 'none' }); applied = [bool]$Apply; removed = [bool]($Remove -and $state -eq 'own')} | ConvertTo-Json
'''

# Stands in for bazel in the chain verify: answers --version, logs its arguments, prints the canned test output.
FAKE_BAZEL = ('@if "%1"=="--version" (echo bazel {version}& exit /b 0)\r\n'
              '@echo %* >> "%FAKE_VERIFY_LOG%"\r\n@type "%FAKE_VERIFY_OUT%"\r\n@exit /b %FAKE_VERIFY_CODE%\r\n')

# What the golden's DBSIM ran (reference_pins.py); start-own-menu.ps1 compares missions[0].files with these.
PINS = {"script.dat": (12286, "61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb"),
        "MISSION.STR": (939, "4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9"),
        "mission.var": (2000, "20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc"),
        "player.mec": (215, "f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984")}


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def tree(installation):
    """{folder\\relative: sha256} of DATA and SAV (directories as folder\\relative\\)."""
    result = {}
    for folder in ("DATA", "SAV"):
        root = Path(installation) / folder
        for path in sorted(root.rglob("*")):
            key = folder + "\\" + str(path.relative_to(root))
            result[key + ("\\" if path.is_dir() else "")] = None if path.is_dir() else hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def make_data_root(base, name, mock_as=None):
    root = Path(tempfile.mkdtemp(prefix="own-menu-", dir=base))
    data = root / "DataRoot"
    clone = data / "installs" / name
    (clone / "DATA" / "sub").mkdir(parents=True)
    (clone / "SAV").mkdir()
    extracted = root / "extracted"   # -ExtractedRoot (outside DataRoot)
    (extracted / "cd").mkdir(parents=True)
    (data / "logs").mkdir()
    for exe in ("ES.EXE", "VSHELL.EXE"):
        (clone / exe).write_bytes(("stand-in for " + exe + "\n").encode() * 20)
    shutil.copyfile(STAND_IN_DBSIM, clone / "DBSIM.EXE")
    (clone / "DATA" / "DRIVE.CFG").write_bytes(b"E:\r\nC:\\x\r\n")
    (clone / "DATA" / "PREFS.CFG").write_bytes(b"prefs stand-in " * 5)
    (clone / "DATA" / "mission.var").write_bytes(bytes(range(64)))
    (clone / "DATA" / "sub" / "inner.bin").write_bytes(b"inner")
    (clone / "SAV" / "GAME_R.SAV").write_bytes(bytes(range(256)) * 3)
    (clone / "SAV" / "GAME_T.SAV").write_bytes(b"T" * 99)
    (clone / "SAV" / "GAMEFILE.STR").write_bytes(b"gamefile stand-in\r\n" * 3)
    launcher = root / "fake-launcher.ps1"
    launcher.write_text(FAKE_LAUNCHER, encoding="utf-8-sig", newline="\n")
    return dict(root=root, data=data, extracted=extracted, clone=clone, launcher=launcher, stop=root / "stop")


def run(env_data, *args, mode="normal", ok=True, isolated=False, name=None, timeout=300, verify=False, extra_env=None,
        shell="text"):
    fixture = env_data
    exe = "es2shell.exe" if shell == "gui" else "es2menu.exe"
    command = ["-DataRoot", fixture["data"], "-ExtractedRoot", fixture["extracted"], "-InstallName", name or fixture["clone"].name,
               "-Python", sys.executable,
               "-BackendScript", BACKEND, "-LauncherScript", fixture["launcher"], "-MenuExe", fixture["root"] / exe,
               "-CoreExe", fixture["root"] / "es2prep_core.exe", *([] if verify else ["-NoVerify"]), *args]
    if shell != "text":
        command += ["-Shell", shell]
    env = dict(FAKE_MODE=mode, FAKE_MOCK=str(MOCK_GAME), FAKE_STOPFILE=str(fixture["stop"]), FAKE_SHELL=shell,
               **(extra_env or {}))
    result = SESSION.run(file=SCRIPT, args=[str(a) for a in command], env=env, timeout=timeout, isolated=isolated)
    if ok is not None and (result.returncode == 0) != ok:
        print(json.dumps(dict(command=[str(c) for c in command], code=result.returncode, out=result.stdout, err=result.stderr)), flush=True)
    if ok is not None:
        check((result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr))
    return result


def sessions(fixture):
    return sorted((fixture["data"] / "logs").glob("own-menu-*"))


def scratch_base():
    """Outside the execroot (its .git link makes the backend refuse menu directories, as in menu_real_test)."""
    base = Path(os.environ.get("TEST_TMPDIR") or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == "execroot":
            return parent.parent
    return base


def remove_tree(path):
    """The scratch lies outside TEST_TMPDIR, so nothing else removes it. The stand-in executables keep the read-only
    attribute of their Bazel outputs, which rmtree cannot delete on Windows."""
    def writable(function, name, _):
        try:
            os.chmod(name, stat.S_IWRITE)
            function(name)
        except OSError:
            pass
    shutil.rmtree(path, onexc=writable)


def text(result):
    return result.stdout + result.stderr


def main():
    if not PWSH:
        print("SKIP: no PowerShell 7 (ES2_PWSH)")
        return 0
    base = scratch_base()
    global SESSION, SCRATCH
    scratch = Path(tempfile.mkdtemp(prefix="own-menu-session-", dir=base))
    SCRATCH = scratch
    SESSION = PwshSession(PWSH, scratch=scratch)
    try:
        sections()
        unattended_sections()
        own_sim_sections()
    finally:
        SESSION.dispose()
        remove_tree(scratch)
    print("OK: %d checks" % checks)
    return 0


def sections():
    base = SCRATCH

    # ---- -DryRun: every step listed, nothing created, started or changed -------------------------------------
    f = make_data_root(base, "calibration-r111")
    before = tree(f["clone"])
    result = run(f, "-DryRun")
    out = text(result)
    check("DRY RUN" in out and "BACKUP first" in out and "RESTORE in a finally" in out and "ES2_LAUNCH_SHELL" in out, out)
    check("launcher plan" in out and "Route A" in out, out)
    check(tree(f["clone"]) == before and not sessions(f) and not (f["data"] / "installs" / ".clone-locks").exists()
          and not (f["data"] / "oracle").exists(), "dry run touched something")
    check(not (f["clone"] / "ES2LAUNCH.EXE").exists())

    # ---- refusals ------------------------------------------------------------------------------------------------
    result = run(f, ok=False, name="native-r111")  # no such installation, but the primary rule comes first
    check("primary installation" in text(result), text(result))
    for bad in ("..\\x", "a b"):
        result = run(f, ok=False, name=bad)
        check("plain installation name" in text(result) or "must stay within" in text(result), (bad, text(result)))
    result = run(f, ok=False, name="missing-clone")
    check("No such installation" in text(result), text(result))
    check(not sessions(f) and tree(f["clone"]) == before)

    p = make_data_root(base, "native-r111")
    primary_before = tree(p["clone"])
    result = run(p, ok=False)
    check("primary installation" in text(result) and "-AllowPrimary" in text(result), text(result))
    check(not sessions(p) and tree(p["clone"]) == primary_before and not (p["data"] / "oracle").exists())
    result = run(p, "-AllowPrimary", "-DryRun")
    check("owner asked for it" in text(result) and "DRY RUN" in text(result), text(result))
    check(tree(p["clone"]) == primary_before and not sessions(p))
    # With -AllowPrimary the script goes on, the backend refuses the primary installation itself, the restore runs.
    result = run(p, "-AllowPrimary", ok=False)
    out = text(result)
    check("owner asked for it" in out and "primary installation" in out and "RESTORED" in out, out)
    check(tree(p["clone"]) == primary_before and len(sessions(p)) == 1)

    # A game process of the installation: refused, nothing created.
    busy = make_data_root(base, "calibration-r111")
    shutil.copyfile(MOCK_GAME, busy["clone"] / "VSHELL.EXE")
    process = subprocess.Popen([str(busy["clone"] / "VSHELL.EXE"), "60000"], creationflags=NO_WINDOW)
    try:
        time.sleep(1.0)
        busy_before = tree(busy["clone"])
        for extra in ((), ("-DryRun",)):
            result = run(busy, *extra, ok=False)
            check("Close the game in this clone" in text(result) and "VSHELL.EXE" in text(result), text(result))
        check(not sessions(busy) and tree(busy["clone"]) == busy_before and not (busy["data"] / "installs" / ".clone-locks" / "calibration-r111.json").exists())
    finally:
        process.kill()
        process.wait()

    # A lock another session holds.
    held = make_data_root(base, "calibration-r111")
    lock_dir = held["data"] / "installs" / ".clone-locks"
    lock_dir.mkdir(parents=True)
    lock_file = lock_dir / "calibration-r111.json"
    with open(lock_file, "w") as stream:
        json.dump(dict(schema=1, clone="calibration-r111", token="not-ours", holder_pid=1, holder_path="x", holder_start_utc="2026-01-01T00:00:00Z",
                       session="another", purpose="test", acquired_utc="2026-01-01T00:00:00Z"), stream)
        stream.flush()
        result = run(held, ok=False)
        check("in use by another session" in text(result), text(result))
    check(not sessions(held))

    # ---- a whole session: backup, launcher, evidence, restore, checklist -------------------------------------------
    f = make_data_root(base, "calibration-r111")
    before = tree(f["clone"])
    result = run(f)
    out = text(result)
    check(tree(f["clone"]) == before, "the installation is not what it was before the session")
    (session,) = sessions(f)
    for line in ("backup DATA:", "backup SAV:", "RESTORED: DATA and SAV equal the state before the session.", "COMPARE CHECKLIST",
                 "SAV after the session: 1 added, 1 removed, 1 changed", "added   DATA\\newdir", "removed SAV\\GAME_T.SAV",
                 "changed SAV\\GAME_R.SAV", "tree "):
        check(line in out, (line, out))
    check("Route A" in out and "Route B" in out and "1 DBSIM start(s)" in out and 'ends with "end 0" as expected' in out, out)
    record = json.loads((session / "restore.json").read_text(encoding="utf-8"))
    check(record["installation"] == str(f["clone"]) and record["primary"] is False and record["seed"] == 71 and record["manifest"]["DATA"]["files"], record)
    check((session / "backup" / "SAV" / "GAME_R.SAV").read_bytes() == bytes(range(256)) * 3)
    for ignored in ("backup", "after-session", "private"):
        check((session / ignored / ".gitignore").read_text() == "*\n", ignored)
    check((session / "after-session" / "SAV" / "GAME_R.SAV").read_bytes() == bytes(range(1, 201)))
    evidence = session / "evidence"
    for name in ("before-session-hashes.txt", "after-session-hashes.txt", "changes.txt", "launcher-run1.log", "chain.json", "chain.log",
                 "status.json", "sav-compare.txt", "summary.txt", "session.json", "checklist.txt", "restore-report.txt", "menu-launch-shell.txt"):
        check((evidence / name).is_file(), (name, sorted(p.name for p in evidence.iterdir()), out))
    check((session / "private" / "menu.log").read_text().startswith("menu transcript"))
    check("GAME_R.SAV" in (evidence / "before-session-hashes.txt").read_text() and hashlib.sha256(bytes(range(1, 201))).hexdigest().upper() in (evidence / "after-session-hashes.txt").read_text())
    summary = json.loads((evidence / "session.json").read_text(encoding="utf-8"))
    check(summary["failure"] is None and summary["runs"][0]["exit_code"] == 0 and summary["game_processes_left"] == [], summary)
    menu = Path(summary["menu"])
    check(menu.parent == f["data"] / "oracle" / "launch" and "seed = 71" in (menu / "menu.ini").read_text() and "game_dir = " + str(f["clone"]) in (menu / "menu.ini").read_text())
    check((f["clone"] / "ES2LAUNCH.EXE").exists())      # the launcher itself stays, like after start-own-launcher.ps1
    check(not (f["data"] / "installs" / ".clone-locks" / "calibration-r111.json").exists(), "the clone lock was not released")

    # ---- two runs in one session ------------------------------------------------------------------------------------
    check("cd_root = " + str(f["extracted"] / "cd") in (menu / "menu.ini").read_text(), "default cd_root")
    f2 = make_data_root(base, "calibration-r111")
    before2 = tree(f2["clone"])
    cd_copy = f2["root"] / "cd-copy"      # -CdRoot names the CD root the own executables read
    cd_copy.mkdir()
    result = run(f2, "-Runs", "2", "-CdRoot", cd_copy)
    (session2,) = sessions(f2)
    check(tree(f2["clone"]) == before2 and (session2 / "evidence" / "launcher-run2.log").is_file() and "Run 2 of 2" in text(result), text(result))
    menu2 = Path(json.loads((session2 / "evidence" / "session.json").read_text(encoding="utf-8"))["menu"])
    check("cd_root = " + str(cd_copy) in (menu2 / "menu.ini").read_text(), (menu2 / "menu.ini").read_text())

    # ---- -Shell gui (es2shell.exe, no console) and -Shell vshell (the original, no menu directory) --------------------
    g = make_data_root(base, "calibration-r111")
    out = text(run(g, "-DryRun", shell="gui"))
    check("es2shell.exe --menu" in out and "no console" in out and "Screen checklist" in out and "Route A" in out, out)
    out = text(run(g, "-DryRun", shell="vshell"))
    check("original VSHELL.EXE" in out and "no menu directory" in out and "Screen checklist" in out, out)
    check(not sessions(g), "a dry run created a session")
    before_g = tree(g["clone"])
    out = text(run(g, shell="gui"))
    (session_g,) = sessions(g)
    check(tree(g["clone"]) == before_g and "RESTORED" in out and "-Shell gui" in out and "graphical shell opens" in out, out)
    check((session_g / "private" / "shell.log").read_text().startswith("shell log"), "gui: shell.log was not collected")
    summary_g = json.loads((session_g / "evidence" / "session.json").read_text(encoding="utf-8"))
    check(summary_g["shell"] == "gui" and summary_g["failure"] is None, summary_g)
    check('es2shell.exe" --menu' in (session_g / "evidence" / "menu-launch-shell.txt").read_text(), "gui: ES2_LAUNCH_SHELL")
    v = make_data_root(base, "calibration-r111")
    before_v = tree(v["clone"])
    out = text(run(v, shell="vshell"))
    (session_v,) = sessions(v)
    check(tree(v["clone"]) == before_v and "RESTORED" in out and "-Shell vshell" in out and "original VSHELL opens" in out, out)
    summary_v = json.loads((session_v / "evidence" / "session.json").read_text(encoding="utf-8"))
    check(summary_v["shell"] == "vshell" and summary_v["menu"] is None and
          not (session_v / "evidence" / "menu-launch-shell.txt").exists(), summary_v)

    # ---- the launcher fails after changing the installation: restored all the same ------------------------------------
    f3 = make_data_root(base, "calibration-r111")
    before3 = tree(f3["clone"])
    result = run(f3, mode="throw", ok=False)
    out = text(result)
    check("the fixture launcher failed" in out and "RESTORED" in out, out)
    check(tree(f3["clone"]) == before3)
    (session3,) = sessions(f3)
    check((session3 / "evidence" / "restore-report.txt").is_file() and json.loads((session3 / "evidence" / "session.json").read_text())["failure"])

    # ---- the session is killed (no finally): -RestoreOnly --------------------------------------------------------------
    f4 = make_data_root(base, "calibration-r111")
    before4 = tree(f4["clone"])
    result = run(f4, mode="crash", isolated=True, ok=None)
    check(result.returncode == 7, (result.returncode, text(result)))
    check(tree(f4["clone"]) != before4, "the crash left the installation unchanged")
    (session4,) = sessions(f4)
    check((session4 / "restore.json").is_file() and not (session4 / "evidence" / "restore-report.txt").exists())
    changed_state = tree(f4["clone"])
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4), "-DryRun"])
    out = text(result)
    check(result.returncode == 0 and "DRY RUN" in out and "would restore" in out and "remove file newdir\\launch.bin" in out and "overwrite GAME_R.SAV" in out
          and "recreate GAME_T.SAV" in out and "recreate sub\\inner.bin" in out and "remove directory newdir" in out, out)
    check(tree(f4["clone"]) == changed_state, "-RestoreOnly -DryRun changed the installation")
    # A damaged backup is refused and nothing is restored.
    damaged = session4 / "backup" / "SAV" / "GAMEFILE.STR"
    saved = damaged.read_bytes()
    damaged.write_bytes(saved + b"x")
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4)])
    check(result.returncode != 0 and "does not match restore.json" in text(result) and tree(f4["clone"]) == changed_state, text(result))
    damaged.write_bytes(saved)
    # A restore.json that names a path outside DataRoot\installs is refused.
    record_path = session4 / "restore.json"
    original_record = record_path.read_text(encoding="utf-8")
    forged = json.loads(original_record)
    forged["installation"] = str(f4["root"])
    record_path.write_text(json.dumps(forged), encoding="utf-8")
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4)])
    check(result.returncode != 0 and tree(f4["clone"]) == changed_state, text(result))
    record_path.write_text(original_record, encoding="utf-8")
    # A running game process of the installation blocks the restore.
    shutil.copyfile(MOCK_GAME, f4["clone"] / "DBSIM.EXE")
    process = subprocess.Popen([str(f4["clone"] / "DBSIM.EXE"), "60000"], cwd=str(f4["clone"]), creationflags=NO_WINDOW)
    try:
        time.sleep(1.0)
        result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4)])
        check(result.returncode != 0 and "Close the game in this clone" in text(result) and "DBSIM.EXE" in text(result), text(result))
        check(tree(f4["clone"]) == changed_state, "a restore ran under a game process")
    finally:
        process.kill()
        process.wait()
    shutil.copyfile(STAND_IN_DBSIM, f4["clone"] / "DBSIM.EXE")
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4)])
    out = text(result)
    check(result.returncode == 0 and "RESTORED: DATA and SAV equal the state before the session." in out, out)
    check(tree(f4["clone"]) == before4)
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f4["data"]), "-RestoreOnly", str(session4 / "backup")])   # idempotent; the backup subdirectory is accepted
    check(result.returncode == 0 and "0 action(s)" in text(result) and tree(f4["clone"]) == before4, text(result))
    check(not (f4["data"] / "installs" / ".clone-locks" / "calibration-r111.json").exists())

    # ---- a game process outlives the launcher: the restore is deferred, never forced ----------------------------------
    f5 = make_data_root(base, "calibration-r111")
    before5 = tree(f5["clone"])
    result = run(f5, "-SettleSeconds", "1", mode="leftover", ok=False)
    out = text(result)
    check(result.returncode == 2 and "NOT RESTORED" in out and "-RestoreOnly" in out and "VSHELL.EXE" in out, (result.returncode, out))
    (session5,) = sessions(f5)
    check(tree(f5["clone"]) != before5)
    deferred_state = tree(f5["clone"])
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f5["data"]), "-RestoreOnly", str(session5)])     # the game still runs
    check(result.returncode != 0 and "Close the game in this clone" in text(result) and tree(f5["clone"]) == deferred_state, text(result))
    f5["stop"].write_bytes(b"")
    # The mock exits 100 ms after the stop file appears; the restore is refused until it has.
    result = None
    for _ in range(100):
        result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(f5["data"]), "-RestoreOnly", str(session5)])
        if result.returncode == 0:
            break
        time.sleep(0.3)
    check(result.returncode == 0 and "RESTORED" in text(result), text(result))
    after5 = tree(f5["clone"])
    # VSHELL.EXE was replaced by the fixture (outside DATA and SAV); DATA and SAV are as before.
    check({k: v for k, v in after5.items()} == before5, "DATA and SAV differ after the deferred restore")

    for fixture in (f, f2, f3, f4, f5, p, busy, held, g, v):
        remove_tree(fixture["root"])
    full_screen_sections()


# ---- DBSIM's full screen (prefs byte 6) and the clone's proxy ------------------------------------------------------
OLD_PROXY = b"MZ" + b"\0" * 300 + b"es2-oracle.ini video compat" + b"\0" * 64            # an older build: no presentation strings
P01_PROXY = b"MZ" + b"\0" * 300 + b"presentation\0presenter\0gdi\0directdraw\0windowed\0" + b"\0" * 64
CALIBRATION_INI = "[video]\r\nsystem_indeo=1\r\n[compat]\r\nmission_map_lifetime=1\r\n"


def utf16(text_value):
    return b"\xff\xfe" + text_value.encode("utf-16-le")


def full_screen_sections():
    base = SCRATCH
    warning = "crashes DBSIM on this machine"
    # No proxy at all (the other fixtures): warned, rebuild-r111 named.
    n = make_data_root(base, "calibration-r111")
    out = text(run(n, "-DryRun", shell="gui"))
    check(warning in out and "no proxy ddraw.dll" in out and "rebuild-r111" in out and "0x4654a8" in out, out)
    check("-InstallName rebuild-r111 -Shell gui" in out, "the play guide names rebuild-r111")
    # A proxy without the [presentation] options: warned; -WindowedDirectDraw refused before anything is created.
    o = make_data_root(base, "calibration-r111")
    (o["clone"] / "ddraw.dll").write_bytes(OLD_PROXY)
    (o["clone"] / "es2-oracle.ini").write_bytes(utf16(CALIBRATION_INI))
    out = text(run(o, "-DryRun", shell="gui"))
    check(warning in out and "predates P-01" in out and "-WindowedDirectDraw" not in out, out)
    before_o = tree(o["clone"])
    ini_o = (o["clone"] / "es2-oracle.ini").read_bytes()
    out = text(run(o, "-WindowedDirectDraw", shell="gui", ok=False))
    check("needs a proxy with P-01" in out and not sessions(o) and tree(o["clone"]) == before_o
          and (o["clone"] / "es2-oracle.ini").read_bytes() == ini_o, out)
    # A proxy with the options but without the setting: warned with the switch named; with directdraw=windowed: no warning.
    w = make_data_root(base, "rebuild-r111")
    (w["clone"] / "ddraw.dll").write_bytes(P01_PROXY)
    (w["clone"] / "es2-oracle.ini").write_bytes(utf16(CALIBRATION_INI))
    out = text(run(w, "-DryRun", shell="gui"))
    check(warning in out and "directdraw=<unset: original>" in out and "-WindowedDirectDraw" in out, out)
    out = text(run(w, "-DryRun", "-WindowedDirectDraw", shell="gui"))
    check(warning not in out and "set [presentation] directdraw=windowed and presenter=gdi" in out, out)
    (w["clone"] / "es2-oracle.ini").write_bytes(utf16(CALIBRATION_INI + "[presentation]\r\npresenter=gdi\r\ndirectdraw=windowed\r\n"))
    out = text(run(w, "-DryRun", shell="gui"))
    check(warning not in out, out)
    check("-WindowedDirectDraw belongs to interactive" in text(run(w, "-DryRun", "-Unattended", "-WindowedDirectDraw", ok=False)))

    # -WindowedDirectDraw in a whole session: the ini holds the setting while the session runs, then it is byte-equal again.
    original = utf16(CALIBRATION_INI + "[presentation]\r\ndirectdraw=original\r\ncapture=0\r\n[video2]\r\nx=1\r\n")
    (w["clone"] / "es2-oracle.ini").write_bytes(original)
    before_w = tree(w["clone"])
    run(w, "-WindowedDirectDraw", mode="crash", isolated=True, ok=None, shell="gui")
    (session_w,) = sessions(w)
    changed = (w["clone"] / "es2-oracle.ini").read_bytes()
    check(changed[:2] == b"\xff\xfe", "the ini lost its UTF-16 encoding")
    lines = changed[2:].decode("utf-16-le").split("\r\n")
    section = lines[lines.index("[presentation]"):lines.index("[video2]")]
    check(section == ["[presentation]", "directdraw=windowed", "capture=0", "presenter=gdi"] and "x=1" in lines
          and "mission_map_lifetime=1" in lines, lines)
    record = json.loads((session_w / "restore.json").read_text(encoding="utf-8"))
    check(record["oracle_ini"]["existed"] is True and (session_w / "backup" / "es2-oracle.ini").read_bytes() == original, record)
    out = text(SESSION.run(file=SCRIPT, args=["-DataRoot", str(w["data"]), "-RestoreOnly", str(session_w), "-DryRun"]))
    check("es2-oracle.ini: would restore" in out and (w["clone"] / "es2-oracle.ini").read_bytes() == changed, out)
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(w["data"]), "-RestoreOnly", str(session_w)])
    check(result.returncode == 0 and "es2-oracle.ini: restored == before-session" in text(result), text(result))
    check((w["clone"] / "es2-oracle.ini").read_bytes() == original and tree(w["clone"]) == before_w)
    # A normal session restores it in its finally; a clone without an ini gets one for the session and loses it after.
    out = text(run(w, "-WindowedDirectDraw", shell="gui"))
    check("directdraw=windowed, presenter=gdi for this session" in out and "es2-oracle.ini: restored == before-session" in out
          and (w["clone"] / "es2-oracle.ini").read_bytes() == original, out)
    (w["clone"] / "es2-oracle.ini").unlink()
    run(w, "-WindowedDirectDraw", mode="crash", isolated=True, ok=None, shell="gui")
    created = (w["clone"] / "es2-oracle.ini").read_bytes()
    check(created == utf16("[presentation]\r\ndirectdraw=windowed\r\npresenter=gdi\r\n"), created)
    latest = sessions(w)[-1]
    check(json.loads((latest / "restore.json").read_text(encoding="utf-8"))["oracle_ini"]["existed"] is False)
    result = SESSION.run(file=SCRIPT, args=["-DataRoot", str(w["data"]), "-RestoreOnly", str(latest)])
    check(result.returncode == 0 and "es2-oracle.ini: removed == before-session" in text(result)
          and not (w["clone"] / "es2-oracle.ini").exists(), text(result))
    # A whole session without the switch warns, records the warning and never touches the ini.
    out = text(run(o, shell="gui"))
    (session_o,) = sessions(o)
    check(warning in out and (o["clone"] / "es2-oracle.ini").read_bytes() == ini_o
          and "es2-oracle.ini" not in (session_o / "evidence" / "restore-report.txt").read_text()
          and (session_o / "evidence" / "full-screen-warning.txt").is_file(), out)
    # The VSHELL-only proxy measures are off by default; only -Shell vshell on a clone with a proxy notes it.
    note = "lacks the VSHELL-only proxy measures"
    (w["clone"] / "es2-oracle.ini").write_bytes(utf16("[video]\r\nlog=x\r\n[presentation]\r\ndirectdraw=windowed\r\npresenter=gdi\r\n"))
    out = text(run(w, "-DryRun", shell="vshell"))
    check(note in out and "system_indeo=1" in out and "mission_map_lifetime=1" in out and "-VshellMeasures" in out, out)
    check(note not in text(run(w, "-DryRun", shell="gui")) and note not in text(run(w, "-DryRun")), "a note outside -Shell vshell")
    (w["clone"] / "es2-oracle.ini").write_bytes(utf16("[video]\r\nsystem_indeo=1\r\n[compat]\r\nmission_map_lifetime=0\r\n"))
    out = text(run(w, "-DryRun", shell="vshell"))
    check(note in out and "system_indeo=1 (" not in out and "mission_map_lifetime=1 (" in out, out)
    (w["clone"] / "es2-oracle.ini").write_bytes(utf16(CALIBRATION_INI))
    check(note not in text(run(w, "-DryRun", shell="vshell")), "a note although both measures are on")
    (w["clone"] / "es2-oracle.ini").unlink()
    check(note in text(run(w, "-DryRun", shell="vshell")), "no note without an ini")
    (w["clone"] / "ddraw.dll").unlink()
    check(note not in text(run(w, "-DryRun", shell="vshell")), "a note on a clone without a proxy")
    for fixture in (n, o, w):
        remove_tree(fixture["root"])


# ---- -Unattended ----------------------------------------------------------------------------------------------------
# Every process of these cases (the mock ES2LAUNCH.EXE and DBSIM.EXE, the stand-in DBSIM) lives in the case's own
# temporary installation, and the script looks only at processes whose executable lies directly in that installation:
# stand-ins of parallel tests or workers elsewhere on the machine are invisible to it, and nothing outside is stopped.

def tick_log(frames, rng="0a"):
    rows = [{"event": "start", "schema": 2, "record_limit": 1000000, "actor_raw": False, "watches": []}]
    rows += [{"event": "tick", "sequence": n, "frame_counter_before_increment": n, "completed_clock_ms": 47 * n, "rng_a_hex": rng,
              "position_raw_i32": [n, 2, 3]} for n in range(frames)]
    return "".join(json.dumps(r) + "\n" for r in rows)


def golden_from(directory, tick_text, outputs):
    """normalized.json of the real oracle_regression.py for a tick log and the two outputs (bytes)."""
    sys.path.insert(0, str(SETUP.parent / "scripts"))
    import oracle_regression
    directory.mkdir(parents=True)
    (directory / "tick.jsonl").write_text(tick_text, encoding="utf-8")
    out = directory / "out"
    out.mkdir()
    for name, data in outputs.items():
        (out / name).write_bytes(data)
    (directory / "normalized.json").write_text(json.dumps(oracle_regression.normalize(directory / "tick.jsonl", out)), encoding="utf-8")
    shutil.rmtree(out)
    (directory / "tick.jsonl").unlink()


def make_unattended(base, name="timing-r111", measurement=True):
    f = make_data_root(base, name)
    if measurement:
        (f["clone"] / "installation-manifest.json").write_text(json.dumps(dict(destination=str(f["clone"]), measurement_clone_of="synthetic-source")))
    root = f["root"]
    (root / "fake-configure.ps1").write_text(FAKE_CONFIGURE, encoding="utf-8-sig", newline="\n")
    version = (SETUP.parents[2] / ".bazelversion").read_text(encoding="ascii").strip()
    (root / "bazel.cmd").write_bytes(FAKE_BAZEL.format(version=version).encode("ascii"))
    outputs = root / "dbsim-out"
    outputs.mkdir()
    (outputs / "results.dat").write_bytes(b"synthetic results " * 4)
    (outputs / "mission.var").write_bytes(bytes(range(200)))
    (root / "tick-template.jsonl").write_text(tick_log(6), encoding="utf-8")
    golden_from(root / "golden" / "c1_01-success", tick_log(6), {"results.dat": b"synthetic results " * 4, "mission.var": bytes(range(200))})
    f.update(configure=root / "fake-configure.ps1", golden=root / "golden", bazel=root / "bazel.cmd")
    return f


def unattended_env(f, pins=None, verify_code=0, verify_line=None, first_extra=""):
    files = {k: dict(size=v[0], sha256=v[1]) for k, v in (pins or PINS).items()}
    out = f["root"] / "verify-out.txt"
    line = verify_line if verify_line is not None else json.dumps(dict(directory="x", results_steps=1, preparations=2, preparations_equal=2, bay=None))
    out.write_text("INFO: test output\n" + line + "\n//shell:chain_real_test PASSED in 3.0s\n", encoding="ascii")
    return dict(FAKE_UNATTENDED="1", FAKE_TICK_TEMPLATE=str(f["root"] / "tick-template.jsonl"), FAKE_OUTPUTS=str(f["root"] / "dbsim-out"),
                FAKE_FILES=json.dumps(files), FAKE_CONFIGURE_LOG=str(f["root"] / "configure.json"), FAKE_SETUP=str(SETUP),
                ES2_BAZEL=str(f["bazel"]), PATHEXT=".COM;.EXE;.BAT;.CMD", FAKE_VERIFY_LOG=str(f["root"] / "verify.log"), FAKE_VERIFY_OUT=str(out), FAKE_VERIFY_CODE=str(verify_code),
                FAKE_FIRST_EXTRA=first_extra)


def unattended_args(f, *extra):
    return ("-Unattended", "-ConfigureScript", f["configure"], "-GoldenRoot", f["golden"], "-VolumePercent", "100", *extra)


def verdict(session):
    return json.loads((session / "evidence" / "unattended.json").read_text(encoding="utf-8"))


def unattended_sections():
    base = SCRATCH

    # ---- -DryRun: the unattended steps, nothing created ------------------------------------------------------------
    f = make_unattended(base)
    before = tree(f["clone"])
    result = run(f, *unattended_args(f, "-DryRun"), extra_env=unattended_env(f))
    out = text(result)
    for needle in ("configure DBSIM with the oracle settings of c1_01-success", "-VirtualTimeStepMs 47 -VirtualTimePace 0", "-TickTarget 8000",
                   "-HiddenWindow", "--input <CONTINUE, MISSION, HAUPTMENUE, QUIT>", "ES2_LAUNCH_NO_UI=1", "-AllowOracleConfig", "at most 900 s",
                   "judge (evidence\\unattended.json)", "c1_01-success.jsonl", "c1_01-mission-hostiles.txt"):
        check(needle in out, (needle, out))
    check("Route A" not in out, "the play guide is for the owner's session")
    check(tree(f["clone"]) == before and not sessions(f) and not (f["root"] / "configure.json").exists()
          and not (f["data"] / "installs" / ".clone-locks").exists() and not (f["data"] / "oracle").exists(), "the dry run touched something")

    # ---- refusals before anything is created -----------------------------------------------------------------------------
    plain = make_unattended(base, "calibration-r111", measurement=False)
    result = run(plain, *unattended_args(plain), ok=False, extra_env=unattended_env(plain))
    check("is not a measurement clone" in text(result), text(result))
    for extra, needle in ((("-Runs", "2"), "-Unattended runs one launcher run"), (("-Seed", "5"), "runs with the seed of its golden"),
                          (("-SessionDirectory", str(f["root"])), "-SessionDirectory exists already"),
                          (("-MenuInput", str(f["root"] / "absent.txt")), "No menu input file")):
        result = run(f, *unattended_args(f, *extra), ok=False, extra_env=unattended_env(f))
        check(needle in text(result), (needle, text(result)))
    result = run(f, "-Hooks", "a=original", ok=False)
    check("belongs to the unattended run: add -Unattended" in text(result), text(result))
    result = run(f, "-Unattended", "-ConfigureScript", f["configure"], "-GoldenRoot", f["root"] / "no-golden", ok=False, extra_env=unattended_env(f))
    check("No golden for c1_01-success" in text(result), text(result))
    check(tree(f["clone"]) == before and not sessions(f) and not (f["root"] / "configure.json").exists() and not sessions(plain))

    # ---- a whole unattended run: equal to the golden, chain verify N of N -------------------------------------------------
    session_dir = f["root"] / "check-evidence" / "own-shell"
    proxy = f["root"] / "prepared-proxy.json"
    proxy.write_text("{}")
    result = run(f, *unattended_args(f, "-SessionDirectory", session_dir, "-Hooks", "alpha=replaced,beta=compare", "-PreparedProxy", proxy,
                                     "-BazelOutputBase", f["root"] / "bazel-base"), verify=True, extra_env=unattended_env(f))
    out = text(result)
    check(result.returncode == 0, (result.returncode, out))
    check(tree(f["clone"]) == before and "RESTORED: DATA and SAV equal the state before the session." in out, out)
    check("UNATTENDED PASSED: launcher sequence expected; launch files pinned; DBSIM identical to the c1_01-success golden; "
          "chain verify 2 of 2 preparations equal, 1 results step(s)" in out, out)
    check("COMPARE CHECKLIST" not in out and "Route A" not in out, "owner text in an unattended run")
    v = verdict(session_dir)
    check(v["verdict"] == "PASSED" and v["problems"] == [] and v["timed_out"] is False and v["dbsim"] == "identical", v)
    check(v["comparison"]["identical"] is True and v["comparison"]["common_ticks"] == 6 and v["chain_verify"]["preparations_equal"] == 2, v)
    check(v["hooks"] == "alpha=replaced,beta=compare" and v["prepared_proxy"] == str(proxy) and v["raw_tick_log_bytes_removed"] > 0, v)
    run_dir = session_dir / "run"
    check(not (run_dir / "c1_01-success.jsonl").exists() and (run_dir / "c1_01-success-normalized.json").is_file()
          and (run_dir / "c1_01-success-comparison.json").is_file(), sorted(p.name for p in run_dir.iterdir()))
    check((run_dir / "c1_01-success-output" / "results.dat").read_bytes() == b"synthetic results " * 4)
    configure = json.loads((f["root"] / "configure.json").read_text(encoding="utf-8-sig"))
    want = dict(InstallName="timing-r111", TickLogPath=str(run_dir / "c1_01-success.jsonl"), VirtualTimeStepMs=47, VirtualTimePace=0,
                VirtualTimeIntraFrame=1, InputMode="replay", TickTarget=8000, Hooks="alpha=replaced,beta=compare", HiddenWindow=True,
                PreparedProxy=str(proxy), Apply=True, TokenIsHolders=True)
    check(all(configure[k] == w for k, w in want.items()), configure)
    check(configure["InputJournal"].endswith("c1_01-success.jsonl") and len(configure["Watch"]) > 0, configure)
    check((f["data"] / "logs" / "fake-input-seen.txt").read_text() == "2\n4\n1\n5\n", "the default menu script")
    verify_args = (f["root"] / "verify.log").read_text(encoding="ascii", errors="replace")
    check("--output_base=%s" % (f["root"] / "bazel-base") in verify_args and "//shell:chain_real_test" in verify_args
          and "ES2_CHAIN_VERIFY=" in verify_args, verify_args)
    check(not (f["data"] / "installs" / ".clone-locks" / "timing-r111.json").exists(), "the clone lock was not released")
    session_json = json.loads((session_dir / "evidence" / "session.json").read_text(encoding="utf-8"))
    check(session_json["runs"][0]["timed_out"] is False and session_json["seed"] == 71, session_json)

    # ---- differences: another golden, other launch files, -NoVerify; a given menu script ------------------------------
    g = make_unattended(base)
    shutil.rmtree(g["golden"])
    golden_from(g["golden"] / "c1_01-success", tick_log(6, rng="0b"), {"results.dat": b"other", "mission.var": bytes(range(200))})
    wrong = dict(PINS, **{"player.mec": (215, "00" * 32)})
    script = g["root"] / "menu-script.txt"
    script.write_bytes(b"2\n4\n1\n5\n")
    g_before = tree(g["clone"])
    result = run(g, *unattended_args(g, "-MenuInput", script), ok=False, extra_env=unattended_env(g, pins=wrong))
    out = text(result)
    check(result.returncode == 6 and tree(g["clone"]) == g_before and "UNATTENDED FAILED" in out, (result.returncode, out))
    (session,) = sessions(g)
    v = verdict(session)
    check(v["dbsim"] == "DIFFERENT" and v["launch_files"] == "DIFFERENT" and v["launcher_sequence"] == "expected", v)
    check(set(v["comparison"]["problems"]) == {"tick_field", "outputs"} and v["chain_verify"] == "skipped (-NoVerify)", v)
    check(any("missions[0].files.player.mec" in p for p in v["problems"]) and (session / "run" / "c1_01-success.jsonl").is_file(), v)

    # ---- DBSIM ended with 1 and the chain verify failed -------------------------------------------------------------------
    h = make_unattended(base)
    bad = json.dumps(dict(directory="x", results_steps=1, preparations=2, preparations_equal=1, bay=None))
    result = run(h, *unattended_args(h), mode="dbsim-1", verify=True, ok=False, extra_env=unattended_env(h, verify_code=1, verify_line=bad))
    out = text(result)
    check(result.returncode == 4 and "UNATTENDED FAILED" in out, (result.returncode, out))
    v = verdict(sessions(h)[0])
    check(v["launcher_sequence"] == "DIFFERENT" and "exit dbsim.EXE 1" in v["launcher_events"] and v["chain_verify"]["preparations_equal"] == 1, v)
    check(any(p.startswith("chain verify: bazel exit 1") for p in v["problems"]) and any(p.startswith("launcher events differ") for p in v["problems"]), v)
    # No verify line at all.
    h2 = make_unattended(base)
    result = run(h2, *unattended_args(h2), verify=True, ok=False, extra_env=unattended_env(h2, verify_line="no json here"))
    check(result.returncode == 6 and any("no verify line" in p for p in verdict(sessions(h2)[0])["problems"]), text(result))
    # A verify that crashed after the campaign line (seen in a live run): that line is no verify line.
    h3 = make_unattended(base)
    campaign_line = json.dumps(dict(finished="stop-return-1", completed=50, results_steps=50, preparations_equal=50, preparations=50))
    result = run(h3, *unattended_args(h3), verify=True, ok=False, extra_env=unattended_env(h3, verify_code=3, verify_line=campaign_line))
    v = verdict(sessions(h3)[0])
    check(result.returncode == 4 and v["chain_verify"]["results_steps"] is None and any("no verify line" in p for p in v["problems"]), (result.returncode, v))

    # ---- The clone's slot 10 has its copies: a from_slot first mission (VSHELL's CONTINUE, no preparation) ----------------
    pinned = {k: dict(size=v[0], sha256=v[1]) for k, v in PINS.items()}
    slot_copy = {k: (v["size"], v["sha256"]) for k, v in pinned.items()}
    slot_copy["player.mec"] = (215, "11" * 32)   # the slot's own copy in 'files', not 0x40f68c's
    def first_extra(launch=pinned):
        extra = '"from_slot": true, '
        if launch is not None:
            extra += '"launch_files": ' + json.dumps(launch) + ', '
        return extra
    s = make_unattended(base)
    s_before = tree(s["clone"])
    result = run(s, *unattended_args(s), verify=True, extra_env=unattended_env(s, pins=slot_copy, first_extra=first_extra()))
    out = text(result)
    check(result.returncode == 0 and tree(s["clone"]) == s_before, (result.returncode, out))
    check("UNATTENDED PASSED: launcher sequence expected; launch files pinned (slot 10's copies); DBSIM identical" in out, out)
    v = verdict(sessions(s)[0])
    check(v["verdict"] == "PASSED" and v["launch_kind"] == "from_slot" and v["launch_files"] == "pinned" and v["problems"] == [], v)
    check(verdict(session_dir)["launch_kind"] == "prepared", "the prepared run's kind")
    check(verdict(session_dir)["shell"] == "text" and "shell_reports" not in verdict(session_dir), "T-55: the text verdict has no headless part")
    # A from_slot record without launch_files, and one whose launched player.mec is not the pinned one.
    for label, extra, needle in (("without", first_extra(None), "from_slot record without launch_files"),
                                 ("other", first_extra(dict(pinned, **{"player.mec": dict(size=215, sha256="22" * 32)})),
                                  "missions[0].launch_files.player.mec = " + "22" * 32)):
        u = make_unattended(base)
        result = run(u, *unattended_args(u), ok=False, extra_env=unattended_env(u, first_extra=extra))
        v = verdict(sessions(u)[0])
        check(result.returncode == 6 and v["launch_kind"] == "from_slot" and v["launch_files"] == "DIFFERENT"
              and any(needle in p for p in v["problems"]), (label, result.returncode, v))
        remove_tree(u["root"])
    for fixture in (h3, s):
        remove_tree(fixture["root"])

    # ---- a stalled run: the timeout stops only this run's ES2LAUNCH.EXE and DBSIM, then the restore runs -------------------
    t = make_unattended(base)
    t_before = tree(t["clone"])
    result = run(t, *unattended_args(t, "-TimeoutSeconds", "5", "-SettleSeconds", "1"), mode="hang", ok=False, extra_env=unattended_env(t))
    out = text(result)
    check(result.returncode == 5 and "TIMEOUT after 5 s" in out and "RESTORED" in out and tree(t["clone"]) == t_before, (result.returncode, out))
    (session,) = sessions(t)
    stops = (session / "evidence" / "timeout-stops.txt").read_text(encoding="utf-8")
    check("stopped after the timeout: ES2LAUNCH.EXE" in stops and "stopped after the timeout: DBSIM.EXE" in stops, stops)
    check(stops.index("ES2LAUNCH.EXE") < stops.index("DBSIM.EXE"), "the launcher is stopped first")
    v = verdict(session)
    check(v["timed_out"] is True and v["verdict"] == "FAILED" and any("did not end within 5 s" in p for p in v["problems"]), v)

    for fixture in (f, g, h, h2, t, plain):
        remove_tree(fixture["root"])

    # ---- -Unattended -Shell gui: es2shell headless under es2launch, one script and report per start ---------------
    gd = make_unattended(base)
    gd_before = tree(gd["clone"])
    result = run(gd, *unattended_args(gd, "-DryRun"), shell="gui", extra_env=unattended_env(gd))
    out = text(result)
    check("--headless --script <evidence\\shell-script-x{X}.txt>" in out and "X3: MAIN MENU, QUIT" in out
          and "X1 ends with exit 2, X3 with exit 0" in out and tree(gd["clone"]) == gd_before and not sessions(gd), out)
    result = run(gd, *unattended_args(gd, "-MenuInput", str(gd["root"] / "x.txt")), shell="gui", ok=False, extra_env=unattended_env(gd))
    check("-Shell gui runs the fixed headless scripts" in text(result) and not sessions(gd), text(result))
    result = run(gd, *unattended_args(gd), shell="vshell", ok=False, extra_env=unattended_env(gd))
    check("not the original VSHELL" in text(result) and not sessions(gd), text(result))
    # A whole gui run: PASSED with the headless reports' end lines; the scripts and reports are evidence.
    result = run(gd, *unattended_args(gd), shell="gui", verify=True, extra_env=unattended_env(gd))
    out = text(result)
    check(result.returncode == 0 and tree(gd["clone"]) == gd_before and "RESTORED" in out, (result.returncode, out))
    check("UNATTENDED PASSED: launcher sequence expected; launch files pinned; DBSIM identical to the c1_01-success golden; "
          "chain verify 2 of 2 preparations equal, 1 results step(s); es2shell headless X1 exit 2, X3 exit 0, 0 script error(s)" in out, out)
    (session,) = sessions(gd)
    v = verdict(session)
    check(v["verdict"] == "PASSED" and v["shell"] == "gui" and v["shell_reports"]["x1"]["exit"] == 2 and v["shell_reports"]["x3"]["exit"] == 0, v)
    check(v["shell_final"].startswith("end passes=300") and v["problems"] == [], v)
    ev = session / "evidence"
    x1 = (ev / "shell-script-x1.txt").read_text()
    check(x1.startswith("step 200\nexpect screen=0\nclick main_menu.09\n") and "click mission.27" in x1, x1)
    check("click service_bay.05" in (ev / "shell-script-x3.txt").read_text() and "click main_menu.13" in (ev / "shell-script-x3.txt").read_text())
    check((gd["data"] / "logs" / "fake-input-seen.txt").read_text() == x1, "the -X1 script")
    check((session / "private" / "shell.log").is_file() and 'es2shell.exe" --menu' in (ev / "menu-launch-shell.txt").read_text(), "shell.log collected")
    check(json.loads((ev / "session.json").read_text(encoding="utf-8"))["shell"] == "gui")
    # A script error in the -X3 start, and no -X3 report at all: FAILED (exit 6), restored.
    for mode, needle in (("gui-error", "headless report X3: exit 0 (expected 0), 1 script error(s)"), ("gui-no-x3", "headless report X3: no end line")):
        ge = make_unattended(base)
        ge_before = tree(ge["clone"])
        result = run(ge, *unattended_args(ge), shell="gui", mode=mode, ok=False, extra_env=unattended_env(ge))
        out = text(result)
        v = verdict(sessions(ge)[0])
        check(result.returncode == 6 and tree(ge["clone"]) == ge_before and "UNATTENDED FAILED" in out
              and any(needle in p for p in v["problems"]) and v["dbsim"] == "identical", (mode, result.returncode, v))
        if mode == "gui-error":
            check(v["shell_reports"]["x3"]["error_lines"] == ["error 2 expect page=4: page=0"], v)
        remove_tree(ge["root"])
    remove_tree(gd["root"])


# ---- -Sim own: es2sim instead of DBSIM.EXE in the unattended chain ------------------------------------------------

def own_sim(f, mode=""):
    fake = f["root"] / "fake-configure-sim.ps1"
    fake.write_text(FAKE_CONFIGURE_SIM, encoding="utf-8-sig", newline="\n")
    log = f["root"] / "configure-sim.jsonl"
    env = dict(unattended_env(f), FAKE_SIM="own", FAKE_CONFIGURE_SIM_LOG=str(log), FAKE_SIM_MODE=mode)
    args = ("-Sim", "own", "-ConfigureSimScript", fake, "-SimExe", f["root"] / "es2sim.exe", "-SimSdl", f["root"] / "SDL3.dll")
    return args, env, log


def sim_calls(log):
    return [json.loads(line) for line in log.read_text(encoding="utf-8-sig").splitlines() if line.strip()] if log.exists() else []


def own_sim_sections():
    base = SCRATCH
    # ---- -DryRun: the deployment step and configure-sim's preview, nothing changed -------------------------------------
    f = make_unattended(base)
    before = tree(f["clone"])
    args, env, log = own_sim(f)
    out = text(run(f, *unattended_args(f, "-DryRun", *args), extra_env=env))
    for needle in ("deploy the own DBSIM process: write", "-Apply -Headless -Config", "es2sim.ini", "es2sim-output.txt",
                   "ticks 8000, outcome 1", "the restore runs configure-sim.ps1 -Remove", "own simulator (configure-sim.ps1 preview): state none",
                   "start es2sim (sim-replacement) where DBSIM.EXE was started"):
        check(needle in out, (needle, out))
    check("configure DBSIM with the oracle settings" not in out and "audio session" not in out, out)
    calls = sim_calls(log)
    check(len(calls) == 1 and not calls[0]["Apply"] and not calls[0]["Remove"], calls)
    check(tree(f["clone"]) == before and not sessions(f) and not (f["clone"] / "es2launch.ini").exists(), "the dry run changed something")
    log.unlink()

    # ---- refusals --------------------------------------------------------------------------------------------------------
    for extra, needle in ((("-Sim", "own"), "-Sim belongs to the unattended run"),):
        check(needle in text(run(f, *extra, ok=False)), needle)
    check("-SimExe belongs to the own DBSIM process" in text(run(f, *unattended_args(f, "-SimExe", "x.exe"), ok=False, extra_env=env)))
    check("configure the proxy of DBSIM.EXE" in text(run(f, *unattended_args(f, "-Hooks", "a=original", *args), ok=False, extra_env=env)))
    check(not sim_calls(log) and not sessions(f))

    # ---- a whole run through es2sim: PASSED, es2launch.ini removed in the restore ------------------------------------------
    session_dir = f["root"] / "check-evidence" / "own-sim"
    result = run(f, *unattended_args(f, "-SessionDirectory", session_dir, *args), extra_env=env)
    out = text(result)
    check(result.returncode == 0 and tree(f["clone"]) == before and "RESTORED" in out, (result.returncode, out))
    check("UNATTENDED PASSED: launcher sequence expected; launch files pinned; DBSIM (es2sim) identical to the c1_01-success golden" in out, out)
    v = verdict(session_dir)
    check(v["sim"] == "own" and v["verdict"] == "PASSED" and v["dbsim"] == "identical", v)
    check('start sim-replacement --arguments "-eggplant -X2 -R0 -G"' in v["launcher_events"] and "exit sim-replacement 3" in v["launcher_events"]
          and not any("dbsim.EXE" in e for e in v["launcher_events"]), v["launcher_events"])
    run_dir = session_dir / "run"
    apply, remove = sim_calls(log)
    check(apply["Apply"] and apply["Headless"] and apply["TokenIsHolders"] and apply["ConfigBom"] and apply["State"] == "none", apply)
    check(apply["Config"] == str(run_dir / "es2sim.ini") and apply["TickLog"] == str(run_dir / "c1_01-success.jsonl")
          and apply["Output"] == str(run_dir / "es2sim-output.txt") and apply["Sim"] == str(f["root"] / "es2sim.exe")
          and apply["Sdl"] == str(f["root"] / "SDL3.dll"), apply)
    config = apply["ConfigText"]
    check(config.startswith("[trace]\r\ntick_log_max=0\r\ntick_log_actor_raw=0\r\nwatch_count=") and "\r\nwatch1=" in config
          and "[time]\r\nvirtual_time_step_ms=47\r\nvirtual_time_start_ms=16777216\r\nvirtual_time_pace=0\r\nvirtual_time_intra_frame=1\r\n" in config
          and "[input]\r\nmode=replay\r\njournal=" in config and "c1_01-success.jsonl\r\n[arming]\r\nearly=1\r\n" in config
          and config.endswith("[run]\r\nticks=8000\r\noutcome=1\r\n") and "[game]" not in config, config)
    check(remove["Remove"] and remove["TokenIsHolders"] and remove["State"] == "own", remove)
    check(not (f["clone"] / "es2launch.ini").exists() and not (f["root"] / "configure.json").exists(), "configure-tick-log ran or es2launch.ini stayed")
    record = json.loads((session_dir / "restore.json").read_text(encoding="utf-8"))
    check(record["own_sim"]["configure_script"] == str(f["root"] / "fake-configure-sim.ps1"), record)
    report = (session_dir / "evidence" / "restore-report.txt").read_text(encoding="utf-8")
    check("own simulator: configure-sim.ps1 -Remove: removed True, state none" in report, report)
    check("es2sim start(s)" in (session_dir / "evidence" / "summary.txt").read_text(encoding="utf-8"))

    # -RestoreOnly of that session: configure-sim.ps1 runs again (nothing left to remove), DATA and SAV equal.
    log.unlink()
    out = text(run(f, "-RestoreOnly", session_dir, extra_env=env))
    check("own simulator: configure-sim.ps1 -Remove: removed False, state none" in out and "RESTORED" in out, out)
    (call,) = sim_calls(log)
    check(call["Remove"] and call["TokenIsHolders"], call)

    # ---- the launcher started DBSIM.EXE after all: the sequence differs ----------------------------------------------------
    g = make_unattended(base)
    args, env, log = own_sim(g)
    env["FAKE_SIM"] = ""
    result = run(g, *unattended_args(g, *args), ok=False, extra_env=env)
    v = verdict(sessions(g)[0])
    check(result.returncode == 6 and v["launcher_sequence"] == "DIFFERENT" and "start dbsim.EXE dummy -eggplant -X2 -R0 -G" in v["launcher_events"], v)
    check(not (g["clone"] / "es2launch.ini").exists())

    # ---- configure-sim refuses: nothing is started, the restore still runs -Remove ------------------------------------------
    h = make_unattended(base)
    h_before = tree(h["clone"])
    args, env, log = own_sim(h, mode="refuse")
    result = run(h, *unattended_args(h, *args), ok=False, extra_env=env)
    out = text(result)
    check("fake configure-sim: refused" in out and "RESTORED" in out and tree(h["clone"]) == h_before, out)
    calls = sim_calls(log)
    check([c["Apply"] for c in calls] == [True, False] and calls[1]["Remove"], calls)
    check(not (h["data"] / "logs" / "fake-launcher-0.log").exists(), "the launcher was started after a refused deployment")
    for fixture in (f, g, h):
        remove_tree(fixture["root"])


if __name__ == "__main__":
    raise SystemExit(main())
