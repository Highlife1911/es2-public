"""Clone guard (CloneGuard.psm1) with real processes on a synthetic clone.

Starts only PowerShell lock holders and the own mock game binary, copied into a
synthetic DataRoot under a fixture-only name ES2MOCK<random>.EXE that is new for
every lane of every run (the guard's game-name list is switched to it inside the
test harnesses, so the real runners' machine-wide DBSIM.EXE checks never see the
mock, and the guard checks of one lane, one test or one parallel worker never
see the mock games of another). No original
executable, installation or clone is touched.
"""
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import types
import uuid

GUARD = Path(sys.argv[1]).resolve()
MOCK_GAME = Path(sys.argv[2]).resolve()
SETUP = GUARD.parent
SCRIPTS = SETUP.parent / "scripts"
PWSH = Path(os.environ.get("ES2_PWSH") or shutil.which("pwsh") or "")
if not PWSH.name or not PWSH.exists():
    PWSH = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
WINPS = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
NO_WINDOW = 0x08000000
# How long the mock games that must still run at the end of a lane live (the test ends them itself); a lane on a
# busy machine (parallel workers) needs minutes, and the mock caps its sleep at 600000 ms.
LIFETIME_MS = 600000
# A mock game that must exit by itself while a step watches it: its countdown starts when the step creates the
# stop file, and the step must still find it running when its first look comes (about 1 s on a busy machine).
SHORT_LIFE_MS = 6000


def game_name():
    """The fixture game name of the running lane: ES2MOCK<random>.EXE, new for every lane of every run.

    The guard enumerates processes by name machine-wide. A name that only this lane uses means that the
    lane sees only the mock games it started itself, never another lane's, another test's or another
    worker's (parallel workers run this test and suite_scripts_test at the same time)."""
    return LOCAL.game_name


CLONE = "timing-r111-p9"
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SETUP))
from pwsh_session import PwshSession  # noqa: E402
import oracle_regression as oracle  # noqa: E402

checks = 0
count_lock = threading.Lock()
children = []


def check(value, detail=""):
    global checks
    with count_lock:
        checks += 1
    if not value:
        raise AssertionError(detail)


def q(value):
    return "'" + str(value).replace("'", "''") + "'"


# Each lane (a section of this test with its own synthetic DataRoot, clone and
# mock games) runs in a thread with one PowerShell 7 process and one Windows
# PowerShell 5.1 process; every step runs in a fresh runspace of such a process
# (pwsh_session.py). Lock holders stay separate processes (Holder): they are
# other sessions by design.
LOCAL = threading.local()


def session(shell):
    key = str(shell)
    if key not in LOCAL.sessions:
        LOCAL.sessions[key] = PwshSession(shell, scratch=LOCAL.root)
    return LOCAL.sessions[key]


def ps(code=None, file=None, args=(), ok=True, env=None, shell=None, timeout=120, isolated=False):
    cmd = [str(shell or PWSH), "-File", str(file), *map(str, args)] if file else [str(shell or PWSH), "-Command", code]
    target = session(shell or PWSH)
    if file:
        result = target.run(file=file, args=args, env=env, timeout=timeout, isolated=isolated)
    else:
        result = target.run(code=code, env=env, timeout=timeout, isolated=isolated)
    if (result.returncode == 0) != ok:
        print(json.dumps(dict(command=cmd, code=result.returncode, out=result.stdout, err=result.stderr)), flush=True)
    check((result.returncode == 0) == ok, (cmd, result.stdout, result.stderr))
    return result


class Squeezed(str):
    """Output without whitespace: PowerShell wraps long error lines anywhere,
    even inside words, so `needle in text(r)` compares without whitespace
    (and without case: Windows paths differ in drive-folder case)."""
    def __contains__(self, needle):
        return "".join(str(needle).split()).casefold() in self.casefold()


def text(result):
    # PowerShell 7 colors error records with ANSI sequences; drop them too.
    plain = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout + result.stderr)
    return Squeezed("".join(plain.split()))


def start_game(path, ms, stop=None, code=None):
    # With a stop file the countdown of MS starts only when the file appears,
    # so a slow PowerShell start cannot miss the still-running game. CODE is
    # the game's exit code; then a missing stop file is spelled "-".
    extra = [str(stop) if stop else "-", str(code)] if code is not None else ([str(stop)] if stop else [])
    p = subprocess.Popen([str(path), str(ms), *extra], creationflags=NO_WINDOW)
    children.append(p)
    return p


def wait_file(path, seconds=60):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if path.exists():
            try:
                return json.loads(path.read_text(encoding="utf-8-sig"))
            except ValueError:
                pass
        time.sleep(0.1)
    raise AssertionError(f"timeout waiting for {path}")


class Holder:
    """A separate PowerShell session holding the clone lock until released."""
    count = 0

    def __init__(self, root, data, clone=CLONE):
        Holder.count += 1
        self.ready = root / f"holder{Holder.count}.ready"
        self.release_file = root / f"holder{Holder.count}.release"
        cmd = [str(PWSH), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(root / "holder.ps1"),
               str(GUARD), str(data), clone, str(self.ready), str(self.release_file)]
        env = dict(os.environ)
        env.pop("ES2_CLONE_LOCK_TOKEN", None)
        env["ES2_SESSION_HINT"] = "fixture holder session"
        self.process = subprocess.Popen(cmd, env=env, creationflags=NO_WINDOW,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        children.append(self.process)
        self.info = wait_file(self.ready)

    def release(self):
        self.release_file.write_text("release")
        check(self.process.wait(timeout=60) == 0, "holder did not release cleanly")


HOLDER = r'''
param($Guard,$DataRoot,$Name,$Ready,$Release)
$ErrorActionPreference='Stop'
Import-Module $Guard -Force
$lock=Enter-CloneLock -DataRoot $DataRoot -InstallName $Name -Purpose 'fixture holder'
$info=@{token=$lock.Token;pid=$PID;path=$lock.Path;inherited=$lock.Inherited} | ConvertTo-Json
[IO.File]::WriteAllText($Ready+'.tmp',$info);Move-Item -LiteralPath ($Ready+'.tmp') -Destination $Ready
while(!(Test-Path -LiteralPath $Release)){Start-Sleep -Milliseconds 100}
Exit-CloneLock $lock
if(Test-Path -LiteralPath $lock.Path){exit 3}
'''

# Imports the guard with the fixture game name, then makes the scripts' own
# Import-Module of CloneGuard.psm1 a no-op so that setting survives.
SCRIPT_HARNESS = r'''
param($Guard,$Script)
$ErrorActionPreference='Stop'
Import-Module $Guard -Force -Global
& (Get-Module CloneGuard) { $script:GameNames=@('__GAME_NAME__') }
function global:Import-Module {
    param([Parameter(Position=0)][string]$Name,[switch]$Force)
    if ([IO.Path]::GetFileName($Name) -ieq 'CloneGuard.psm1') { return }
    Microsoft.PowerShell.Core\Import-Module $Name -Force:$Force -Global
}
if ($env:FIXTURE_STOP_FILE) { New-Item -ItemType File -Path $env:FIXTURE_STOP_FILE | Out-Null }
& $Script @args
'''


def guard_prelude():
    return (f"$ErrorActionPreference='Stop'; Import-Module {q(GUARD)} -Force; "
            f"& (Get-Module CloneGuard) {{ $script:GameNames=@('{game_name()}'); $script:ParallelGameName='{game_name()}' }}; ")


def tick_events(n=3):
    rows = [dict(event="start", schema=2, record_limit=1000000, actor_raw=0, watches=0)]
    for i in range(n):
        tick = dict(event="tick", frame_counter_before_increment=i, **{f: 0 for f in oracle.TICK_FIELDS})
        tick.update(completed_clock_ms=i*47, clock_raw_hex="00"*24, rng_a_hex="00"*114, rng_b_hex="00"*114,
                    position_raw_i32=[i, 2, 3], angles_raw_u16=[0, 0, 1], in_injected=i, in_fallbacks=4)
        rows.append(tick)
    return rows


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def fixture(root):
    """Synthetic DataRoot with the clone CLONE, a neighbour clone and a mock game in each."""
    data = root / "data"
    (data / ".es2-synthetic-fixture").parent.mkdir(parents=True)
    (data / ".es2-synthetic-fixture").write_text("synthetic suite fixtures only", encoding="ascii")
    install = data / "installs" / CLONE
    (install / "DATA").mkdir(parents=True)
    write_json(install / "installation-manifest.json",
               dict(destination=str(install), measurement_clone_of="synthetic-source", language="German"))
    write_json(install / "oracle-media-manifest.json", dict(proxy_sha256="synthetic-proxy"))
    game = install / game_name()
    shutil.copyfile(MOCK_GAME, game)
    other = data / "installs" / "timing-r111-p8"
    other.mkdir(parents=True)
    other_game = other / game_name()
    shutil.copyfile(MOCK_GAME, other_game)
    (root / "holder.ps1").write_text(HOLDER, encoding="utf-8")
    (root / "script-harness.ps1").write_text(SCRIPT_HARNESS.replace("__GAME_NAME__", game_name()), encoding="utf-8")
    lock_path = data / "installs/.clone-locks" / f"{CLONE}.json"
    return types.SimpleNamespace(data=data, install=install, game=game, other_game=other_game, lock_path=lock_path)


def lane_locks(root):
    fx = fixture(root)
    data, install, game, other_game, lock_path = fx.data, fx.install, fx.game, fx.other_game, fx.lock_path
    # Every changed script parses in Windows PowerShell 5.1 as well.
    for name in ("CloneGuard.psm1", "run-oracle-regression.ps1", "run-oracle-parallel.ps1",
                 "configure-tick-log.ps1", "start-timing-mission.ps1", "run-tick-log-mission.ps1",
                 "run-prepared-suite.ps1", "run-mission-outcome.ps1", "../scripts/OracleSpeed.psm1",
                 "SuiteLaunch.psm1"):
        r = ps(f"$e=$null;$t=$null;[void][Management.Automation.Language.Parser]::ParseFile({q(SETUP/name)},[ref]$t,[ref]$e);"
               "if($e.Count){$e|Out-String;exit 1}", shell=WINPS)

    # --- Get-/Restore-RunnerEnvironment hand the process environment back exactly --------------
    for shell in (PWSH, WINPS):
        ps(guard_prelude() +
           "[Environment]::SetEnvironmentVariable('ES2_T46_KEPT','before'); [Environment]::SetEnvironmentVariable('ES2_T46_GONE',[NullString]::Value); "
           "[Environment]::SetEnvironmentVariable('ES2_T46_LOST','lost'); "
           "$snapshot=Get-RunnerEnvironment; "
           "if($snapshot['ES2_T46_KEPT'] -cne 'before' -or $snapshot.ContainsKey('ES2_T46_GONE')){throw 'snapshot wrong'}; "
           "[Environment]::SetEnvironmentVariable('ES2_T46_KEPT','changed'); [Environment]::SetEnvironmentVariable('ES2_T46_GONE','added'); "
           "[Environment]::SetEnvironmentVariable('ES2_T46_LOST',[NullString]::Value); [Environment]::SetEnvironmentVariable('ES2_T46_NEW','new'); "
           "Restore-RunnerEnvironment $snapshot; "
           "$after=Get-RunnerEnvironment; "
           "if($after['ES2_T46_KEPT'] -cne 'before' -or $after['ES2_T46_LOST'] -cne 'lost' -or $after.ContainsKey('ES2_T46_GONE') -or $after.ContainsKey('ES2_T46_NEW')){throw 'restore wrong'}; "
           "if($after.Count -ne $snapshot.Count){throw 'variable count differs'}", shell=shell)

    # --- Lock: a second session is refused with the holder named --------------
    holder = Holder(root, data)
    check(lock_path.exists() and holder.info["path"] == str(lock_path) and not holder.info["inherited"])
    enter = f"$l=Enter-CloneLock -DataRoot {q(data)} -InstallName {CLONE}; $l | Select-Object Token,Inherited,Path | ConvertTo-Json"
    for shell in (PWSH, WINPS):
        r = ps(guard_prelude() + enter, ok=False, shell=shell)
        message = text(r)
        check(f"Clone {CLONE} is in use by another session" in message and f"holder PID {holder.info['pid']} (" in message
              and "fixture holder session" in message and "fixture holder" in message, message)
    r = ps(guard_prelude() + f"Assert-CloneLockAvailable -DataRoot {q(data)} -InstallName {CLONE}", ok=False)
    check(f"holder PID {holder.info['pid']} (" in text(r))
    # The holder's own children inherit the lock through the token.
    token = {"ES2_CLONE_LOCK_TOKEN": holder.info["token"]}
    r = ps(guard_prelude() + enter, env=token)
    inherited = json.loads(r.stdout)
    check(inherited["Inherited"] and inherited["Token"] == holder.info["token"])
    ps(guard_prelude() + f"Assert-CloneLockAvailable -DataRoot {q(data)} -InstallName {CLONE}", env=token)
    # A token for another clone does not open this one.
    r = ps(guard_prelude() + enter, ok=False, env={"ES2_CLONE_LOCK_TOKEN": "0"*32})
    # The lock content is readable beside the holder's handle.
    r = ps(guard_prelude() + f"Read-CloneLock {q(lock_path)} | ConvertTo-Json")
    content = json.loads(r.stdout)
    check(content["clone"] == CLONE and content["holder_pid"] == holder.info["pid"] and content["purpose"] == "fixture holder")
    holder.release()
    check(not lock_path.exists(), "lock left after release")
    ps(guard_prelude() + f"Assert-CloneLockAvailable -DataRoot {q(data)} -InstallName {CLONE}")

    # A killed holder leaves nothing behind (the handle closes with it).
    holder = Holder(root, data)
    holder.process.kill()
    holder.process.wait(timeout=30)
    deadline = time.time() + 10
    while lock_path.exists() and time.time() < deadline:
        time.sleep(0.1)
    check(not lock_path.exists(), "lock of a killed holder remained")
    # A stale lock file without a holder is reclaimed.
    stale = dict(schema=1, clone=CLONE, token="f"*32, holder_pid=4, holder_path="C:\\stale\\pwsh.exe",
                 holder_start_utc="2026-01-01T00:00:00.0000000Z", session="stale", purpose="stale", acquired_utc="2026-01-01T00:00:00Z")
    write_json(lock_path, stale)
    ps(guard_prelude() + f"Assert-CloneLockAvailable -DataRoot {q(data)} -InstallName {CLONE}")
    # Own process on purpose: the lock must vanish when the process that took it ends.
    r = ps(guard_prelude() + enter, isolated=True)
    reclaimed = json.loads(r.stdout)
    check(not reclaimed["Inherited"] and reclaimed["Token"] != stale["token"])
    check(not lock_path.exists(), "lock not released at process exit")


def lane_processes(root):
    fx = fixture(root)
    data, install, game, other_game, lock_path = fx.data, fx.install, fx.game, fx.other_game, fx.lock_path
    # --- Processes: listed with PID, path and start time; foreign ones left alone
    g1 = start_game(game, LIFETIME_MS)
    g_other = start_game(other_game, LIFETIME_MS)
    time.sleep(0.5)
    report = root / "g1-launch.json"
    r = ps(guard_prelude() + f"""
$p=@(Get-CloneGameProcess -Installation {q(install)})
if($p.Count -ne 1 -or $p[0].ProcessId -ne {g1.pid} -or $p[0].ExecutablePath -ne {q(game)} -or !$p[0].StartTimeUtc){{throw ('listing '+($p|ConvertTo-Json))}}
$all=@(Get-CloneGameProcess)
if(@($all | Where-Object {{$_.ProcessId -in @({g1.pid},{g_other.pid})}}).Count -ne 2){{throw 'machine-wide listing'}}
Format-CloneGameProcess $p[0]
""")
    check(f"PID {g1.pid} ({game}, started 20" in text(r), r.stdout)
    r = ps(guard_prelude() + f"Assert-CloneIdle {q(install)} 'testing'", ok=False)
    message = text(r)
    check("Close the game in this clone before testing" in message and f"PID {g1.pid} (" in message and str(game) in message
          and "leaves it alone" in message and f"PID {g_other.pid} (" not in message, message)

    def write_report(path, pid, exe, shift_seconds=0):
        return (f"$s=(Get-Process -Id {pid}).StartTime.ToUniversalTime().AddSeconds({shift_seconds});"
                f"@{{pid={pid};exe={q(exe)};process_start_utc=$s.ToString('o')}} | ConvertTo-Json | Set-Content -LiteralPath {q(path)};")
    wait = (f"$r=Wait-CloneOwnedProcessExit -Installation {q(install)} -LaunchReport {q(report)} "
            "-NotBefore ([DateTime]::UtcNow.AddMinutes(-10)) -GraceSeconds 1 -StopTimeoutSeconds 15; $r | ConvertTo-Json")
    # Same PID and path, different start time: a reused PID, never touched.
    r = ps(guard_prelude() + write_report(report, g1.pid, game, 2) + wait)
    check(json.loads(r.stdout)["status"] == "pid-reused" and g1.poll() is None)
    # A report from before this run, or for a process outside the clone, is refused.
    r = ps(guard_prelude() + write_report(report, g1.pid, game) +
           f"Wait-CloneOwnedProcessExit -Installation {q(install)} -LaunchReport {q(report)} -NotBefore ([DateTime]::UtcNow.AddHours(1)) -GraceSeconds 0",
           ok=False)
    check("does not name a process this run started" in text(r) and g1.poll() is None)
    r = ps(guard_prelude() + write_report(report, g_other.pid, other_game) + wait, ok=False)
    check("does not name a process this run started" in text(r) and g_other.poll() is None)
    # Missing report, or a report whose launch never started: nothing to do.
    r = ps(guard_prelude() + wait.replace(q(report), q(root / "missing.json")))
    check(json.loads(r.stdout)["status"] == "no-launch-report")
    # The own process that outlives its grace period is stopped and gone.
    r = ps(guard_prelude() + write_report(report, g1.pid, game) + wait)
    result = json.loads(r.stdout)
    check(result["status"] == "stopped" and result["pid"] == g1.pid, r.stdout)
    # A stopped process has no exit code of the game.
    check(result["exit_code"] is None and result["stopped_by_runner"] is True and result["exit_code_source"] == "none", r.stdout)
    check(g1.wait(timeout=10) != 0 and g_other.poll() is None, "own game not stopped or foreign game touched")
    # An own process that exits by itself within the grace period is only waited for.
    g2 = start_game(game, SHORT_LIFE_MS, root / "g2.stop")
    r = ps(guard_prelude() + write_report(report, g2.pid, game) + f"New-Item -ItemType File {q(root / 'g2.stop')} | Out-Null;"
           + wait.replace("-GraceSeconds 1", "-GraceSeconds 60"))
    check(json.loads(r.stdout)["status"] == "exited" and g2.wait(timeout=10) == 0, r.stdout)
    exited = json.loads(r.stdout)
    check(exited["exit_code"] == 0 and exited["exit_code_source"] == "opened-handle" and exited["stopped_by_runner"] is False, r.stdout)
    # The exit code is the game's own, not only zero (handle opened while the process runs).
    g6 = start_game(game, SHORT_LIFE_MS, root / "g6.stop", code=7)
    r = ps(guard_prelude() + write_report(report, g6.pid, game) + f"New-Item -ItemType File {q(root / 'g6.stop')} | Out-Null;"
           + wait.replace("-GraceSeconds 1", "-GraceSeconds 60"))
    exited = json.loads(r.stdout)
    check(exited["status"] == "exited" and exited["exit_code"] == 7 and exited["exit_code_source"] == "opened-handle" and g6.wait(timeout=10) == 7, r.stdout)
    # A process the launcher started in this PowerShell process (Register-CloneStartedProcess) keeps its code
    # although it ended before the wait began; an unregistered one that is gone before the first look has none.
    launcher = (f"$g=Start-Process -FilePath {q(game)} -ArgumentList '100','-','9' -PassThru -WindowStyle Hidden;"
                "$s=$g.StartTime.ToUniversalTime().ToString('o');"
                f"@{{pid=$g.Id;exe={q(game)};process_start_utc=$s}} | ConvertTo-Json | Set-Content -LiteralPath {q(report)};")
    r = ps(guard_prelude() + launcher + "Register-CloneStartedProcess $g; $g.WaitForExit();" + wait)
    exited = json.loads(r.stdout)
    check(exited["status"] == "exited" and exited["exit_code"] == 9 and exited["exit_code_source"] == "launcher-handle" and exited["waited_ms"] < 5000, r.stdout)
    r = ps(guard_prelude() + launcher + "$g.WaitForExit();" + wait)
    exited = json.loads(r.stdout)
    check(exited["status"] == "exited" and exited["exit_code"] is None and exited["exit_code_source"] == "none", r.stdout)
    # Assert-CloneIdle -WaitSeconds waits for an exiting game, never stops it.
    g3 = start_game(game, 1500, root / "g3.stop")
    ps(guard_prelude() +
       f"if(!@(Get-CloneGameProcess -Installation {q(install)}).Count){{throw 'game not running'}};"
       f"New-Item -ItemType File {q(root / 'g3.stop')} | Out-Null;"
       f"Assert-CloneIdle {q(install)} 'testing' -WaitSeconds 60")
    check(g3.wait(timeout=10) == 0)
    # The executable path comes from kernel32, and a process that has just been started is at once
    # the own one (System.Diagnostics.Process.Path answered ntdll.dll or nothing while a process was still
    # starting under CPU load, which made the own game 'foreign', status pid-reused). The loop starts one
    # fresh game per round and asks for its identity immediately.
    r = ps(guard_prelude() + f"Get-CloneProcessImagePath {g3.pid}")
    check(r.stdout.strip() == "", "a process that has exited has no image path")
    for _ in range(12):
        g = start_game(game, 20000)
        r = ps(guard_prelude() + f"$s=(Get-Process -Id {g.pid}).StartTime.ToUniversalTime(); "
               f"'path=' + (Get-CloneProcessImagePath {g.pid}); "
               f"'state=' + (& (Get-Module CloneGuard) {{ param($i,$e,$s) (Get-CloneOwnedState $i $e $s).State }} {g.pid} {q(game)} $s)")
        check(f"path={game}".lower() in r.stdout.lower() and "state=owned" in r.stdout, r.stdout)
        g.kill()
        g.wait(timeout=30)


def lane_scripts(root):
    fx = fixture(root)
    data, install, game, other_game, lock_path = fx.data, fx.install, fx.game, fx.other_game, fx.lock_path
    g_other = start_game(other_game, LIFETIME_MS)  # a foreign game that the scripts must leave alone
    # --- Scripts ---------------------------------------------------------------
    holder = Holder(root, data)
    hp = f"holder PID {holder.info['pid']} ("
    # configure-tick-log -Apply: refused before any build or deployment.
    configure = ["-DataRoot", data, "-InstallName", CLONE, "-TickLogPath", root / "tick.jsonl", "-Apply",
                 "-PreparedProxy", root / "missing-prepared.json"]
    r = ps(file=SETUP / "configure-tick-log.ps1", args=configure, ok=False)
    check("is in use by another session" in text(r) and hp in text(r), text(r))
    check(not (install / "ddraw.dll").exists() and not (install / "es2-oracle.ini").exists())
    # start-timing-mission: refused before the launch report or the game.
    r = ps(file=SETUP / "start-timing-mission.ps1",
           args=["-DataRoot", data, "-InstallName", CLONE, "-OutputPath", root / "launch.json"], ok=False)
    check("is in use by another session" in text(r) and hp in text(r) and not (root / "launch.json").exists(), text(r))
    # run-oracle-regression (mock suite): refused, nothing created; inherited token passes.
    suite = "guard-suite"
    journal = root / "input.jsonl"
    journal.write_text(json.dumps(dict(f=0, api="keydown", vk=32)) + "\n", encoding="utf-8")
    watches = root / "watches.txt"
    watches.write_text("", encoding="ascii")
    write_json(root / "suites.json", dict(format=2, suites=[dict(name=suite, script=str(journal), watches=str(watches), ticks=3, frame_limit=3)]))
    mock = root / "mock"
    (mock / suite).mkdir(parents=True)
    (mock / suite / "tick.jsonl").write_text("".join(json.dumps(e) + "\n" for e in tick_events()), encoding="utf-8")
    write_json(mock / suite / "report.json", dict(ticks_seen=3, stalled=False))
    golden = root / "golden"
    write_json(golden / suite / "normalized.json", oracle.normalize(mock / suite / "tick.jsonl", frame_limit=3))
    write_json(golden / suite / "manifest.json", dict(commit="synthetic-baseline"))

    def regression(out, ok, env=None):
        return ps(file=SETUP / "run-oracle-regression.ps1", ok=ok, env=env,
                  args=["-DataRoot", data, "-InstallName", CLONE, "-SuitesFile", root / "suites.json", "-MockDirectory", mock,
                        "-GoldenRoot", golden, "-OutputDirectory", out, "-Python", sys.executable])
    r = regression(root / "reg-refused", ok=False)
    check("is in use by another session" in text(r) and hp in text(r) and not (root / "reg-refused").exists(), text(r))
    regression(root / "reg-inherited", ok=True, env={"ES2_CLONE_LOCK_TOKEN": holder.info["token"]})
    check(lock_path.exists(), "inherited run released the holder's lock")
    holder.release()
    regression(root / "reg-free", ok=True)
    summary = json.loads((root / "reg-free/summary.json").read_text(encoding="utf-8-sig"))
    check(summary["results"] == [dict(suite=suite, result="identical")] and not lock_path.exists())

    # configure-tick-log -Apply with a game in the clone: waits, then names it;
    # a game that exits during the wait passes the guard (and is never stopped).
    g4 = start_game(game, LIFETIME_MS)
    time.sleep(0.3)
    harness = root / "script-harness.ps1"
    r = ps(file=harness, args=[GUARD, SETUP / "configure-tick-log.ps1", *configure, "-BusyWaitSeconds", 1], ok=False)
    message = text(r)
    check("Close the game in this clone before configuring it" in message and f"PID {g4.pid} (" in message
          and "still running after waiting 1 s" in message and g4.poll() is None, message)
    g4.kill(); g4.wait(timeout=30)
    g5 = start_game(game, 3000, root / "g5.stop")
    r = ps(file=harness, args=[GUARD, SETUP / "configure-tick-log.ps1", *configure, "-BusyWaitSeconds", 60], ok=False,
           env={"FIXTURE_STOP_FILE": str(root / "g5.stop")})
    check("Close the game" not in text(r) and "missing-prepared.json" in text(r) and g5.wait(timeout=10) == 0, text(r))
    check(not (install / "ddraw.dll").exists())

    # run-oracle-parallel (mock children): locks every clone, children inherit,
    # a held clone refuses the whole run before any child or output exists.
    pmock = root / "pmock"
    pmock.mkdir()
    (pmock / "run-oracle-regression.ps1").write_text(r'''
param($Mode,$Suite,$InstallName,$OutputDirectory,$DataRoot,$GoldenRoot,$RepositoryRoot,$Python,$Hooks='')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $RepositoryRoot 'tools/reference-game/setup/CloneGuard.psm1') -Force
$lock=Enter-CloneLock -DataRoot $DataRoot -InstallName $InstallName
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
@{inherited=$lock.Inherited;token=$env:ES2_CLONE_LOCK_TOKEN;clone=$InstallName} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'guard.json')
@{mode=$Mode;output=$OutputDirectory;commit='synthetic';dirty_tree=$false;hooks=$Hooks;results=@(@{suite=$Suite;result='identical'})} |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'summary.json')
''', encoding="utf-8")
    suites = [s["name"] for s in json.loads((SETUP.parent / "oracle-suites.json").read_text())["suites"]][:2]
    clones = ("timing-r111-p8", CLONE)

    def parallel(out, ok):
        return ps(f"& {q(SETUP / 'run-oracle-parallel.ps1')} -MockDirectory {q(pmock)} -DataRoot {q(data)} "
                  f"-InstallName @('{clones[0]}','{clones[1]}') -Suite @('{suites[0]}','{suites[1]}') -OutputDirectory {q(out)}", ok=ok)
    parallel(root / "par-free", ok=True)
    tokens = set()
    for name in suites:
        guard = json.loads((root / "par-free" / name / "run/guard.json").read_text(encoding="utf-8-sig"))
        check(guard["inherited"] and guard["token"], guard)
        tokens.add(guard["token"])
    check(len(tokens) == 2 and not any((data / "installs/.clone-locks" / f"{c}.json").exists() for c in clones))
    holder = Holder(root, data)
    r = parallel(root / "par-refused", ok=False)
    check("is in use by another session" in text(r) and f"holder PID {holder.info['pid']} (" in text(r)
          and not (root / "par-refused").exists(), text(r))
    check(not (data / "installs/.clone-locks" / f"{clones[0]}.json").exists(), "first clone lock kept after refusal")
    holder.release()
    check(g_other.poll() is None, "foreign mock game was touched")


def lane_machine(root):
    """The machine-wide rule of start-timing-mission.ps1 (Assert-CloneMachineIdle)."""
    fx = fixture(root)
    data, install, game, other_game = fx.data, fx.install, fx.game, fx.other_game
    outside = root / "outside-installs" / game_name()      # like the Bazel temp stand-in dbsim.EXE
    old = data / "installs-old" / "x" / game_name()         # starts with the installs prefix as a string only
    lookalike = data / "installs" / (CLONE + "-old") / game_name()  # starts with the target clone's name only
    for path in (outside, old, lookalike):
        path.parent.mkdir(parents=True)
        shutil.copyfile(MOCK_GAME, path)
    g_out, g_old = start_game(outside, LIFETIME_MS), start_game(old, LIFETIME_MS)
    time.sleep(0.5)
    other_dir = other_game.parent

    def machine(permitted=(), installation=install, processes="", ok=True, shell=None):
        permitted_arg = "@(" + ",".join(q(p) for p in permitted) + ")"
        return ps(guard_prelude() + f"Assert-CloneMachineIdle -DataRoot {q(data)} -Installation {q(installation)} "
                  f"-PermittedClones {permitted_arg} {processes}", ok=ok, shell=shell)

    # Processes outside DataRoot\installs are ignored, also the prefix look-alike 'installs-old'.
    for shell in (PWSH, WINPS):
        machine(shell=shell)
    r = ps(guard_prelude() + f"@(Get-CloneBlockingGame -DataRoot {q(data)} -Installation {q(install)}).Count")
    check(r.stdout.strip() == "0", r.stdout)
    # A game of another installation blocks, is named and left alone; the stand-in is not named.
    g_other = start_game(other_game, LIFETIME_MS)
    time.sleep(0.5)
    for shell in (PWSH, WINPS):
        message = text(machine(ok=False, shell=shell))
        check("outside the permitted" in message and f"PID {g_other.pid} (" in message and str(other_game) in message
              and "It is left alone." in message and f"PID {g_out.pid} (" not in message and f"PID {g_old.pid} (" not in message, message)
    # ... unless it is a DBSIM of a permitted parallel clone (the run's own neighbour).
    machine(permitted=[install, other_dir])
    # A clone whose name merely starts with the permitted one is not permitted.
    g_like = start_game(lookalike, LIFETIME_MS)
    time.sleep(0.5)
    message = text(machine(permitted=[install, other_dir], ok=False))
    check(f"PID {g_like.pid} (" in message and "outside the permitted" in message, message)
    g_like.kill(); g_like.wait(timeout=30)
    # A game in the target clone blocks first, with the old wording, whatever else runs.
    g_target = start_game(game, LIFETIME_MS)
    time.sleep(0.5)
    message = text(machine(permitted=[install, other_dir], ok=False))
    check("Close the game in this clone before starting another" in message and f"PID {g_target.pid} (" in message
          and "leaves it alone" not in message and "is left alone" in message, message)
    message = text(machine(ok=False))
    check("Close the game in this clone" in message and f"PID {g_target.pid} (" in message, message)
    # Paths are compared after normalisation: \?\ prefix, case, '..', trailing separators.
    bs = chr(92)
    forms = [bs * 2 + "?" + bs + str(other_game).upper(), str(other_dir) + bs + ".." + bs + other_dir.name + bs + game_name(),
             str(other_game).lower()]
    for form in forms:
        listing = f"$p=[pscustomobject]@{{ProcessId={g_other.pid};Name='{game_name()}';ExecutablePath={q(form)};StartTimeUtc=$null}};"
        r = ps(guard_prelude() + listing + f"(@(Get-CloneBlockingGame -DataRoot {q(data)} -Installation {q(old.parent)} "
               f"-Processes @($p))).Reason")
        check(r.stdout.strip() == "other-installation", (form, r.stdout))
        r = ps(guard_prelude() + listing + f"@(Get-CloneBlockingGame -DataRoot {q(data)} -Installation {q(install)} "
               f"-PermittedClones @({q(str(other_dir) + chr(92))}) -Processes @($p)).Count")
        check(r.stdout.strip() == "0", (form, r.stdout))
    r = ps(guard_prelude() + f"""
$bs=[string][char]92
$d={q(data)}
$i=Resolve-ClonePath (Join-Path $d 'installs')
$c=@(
  ((Test-ClonePathInside ($d+$bs+'installs-old'+$bs+'x'+$bs+'a.exe') $i) -eq $false),
  ((Test-ClonePathInside ($d+$bs+'installs'+$bs+'x'+$bs+'a.exe') ($i+$bs)) -eq $true),
  ((Test-ClonePathInside $i $i) -eq $false),
  ((Test-ClonePathInside '' $i) -eq $false),
  ((Resolve-ClonePath ($bs+$bs+'?'+$bs+$i+$bs)) -ceq $i))
$c -join ','
""")
    check(r.stdout.strip() == "True,True,True,True,True", r.stdout)
    # 8.3 names resolve to the long ones when the volume has them.
    r = ps(guard_prelude() + f"""
$fso=New-Object -ComObject Scripting.FileSystemObject
$short=$fso.GetFolder({q(other_dir)}).ShortPath
if ($short -ieq {q(other_dir)}) {{ 'no8dot3'; return }}
$long=Resolve-ClonePath ($short+[string][char]92+'{game_name()}')
if ($long -ine {q(other_game)}) {{ throw ('short name not expanded: '+$long) }}
'ok'
""")
    check(r.stdout.strip() in ("ok", "no8dot3"), r.stdout)
    # A process whose path cannot be read: blocks while it exists and its path stays unreadable (the
    # reader keeps answering nothing), ignored once it is gone.
    listing = f"$p=[pscustomobject]@{{ProcessId=%d;Name='{game_name()}';ExecutablePath=$null;StartTimeUtc=$null}};"
    never = "-PathReader { param($id) $null } -PathRetries 2"
    r = ps(guard_prelude() + listing % g_out.pid + f"Assert-CloneMachineIdle -DataRoot {q(data)} -Installation {q(install)} -Processes @($p) {never}", ok=False)
    message = text(r)
    check("path cannot be read" in message and f"PID {g_out.pid} (" in message and "path not readable" in message
          and "It is left alone" in message, message)
    gone = start_game(outside, 300)
    gone.wait(timeout=30)
    ps(guard_prelude() + listing % gone.pid + f"Assert-CloneMachineIdle -DataRoot {q(data)} -Installation {q(install)} -Processes @($p) {never}")
    # A path that becomes readable on a later read (a game just being started) is judged by that path:
    # outside the installations ignored, in another installation refused as such. The default reader
    # (Win32_Process) finds the real path of the running mock game.
    ps(guard_prelude() + listing % g_out.pid + f"Assert-CloneMachineIdle -DataRoot {q(data)} -Installation {q(install)} -Processes @($p)")
    later = f"-PathReader {{ param($id) {q(other_game)} }} -PathRetries 3"
    r = ps(guard_prelude() + listing % g_out.pid + f"Assert-CloneMachineIdle -DataRoot {q(data)} -Installation {q(install)} -Processes @($p) {later}", ok=False)
    message = text(r)
    check("outside the permitted" in message and "path cannot be read" not in message, message)
    # Nothing was stopped: every game that was running still runs.
    check(all(p.poll() is None for p in (g_out, g_old, g_other, g_target)), "the guard stopped a game")


def run_lane(lane, root, errors):
    LOCAL.sessions = {}
    LOCAL.root = root
    LOCAL.game_name = f"ES2MOCK{uuid.uuid4().hex[:12].upper()}.EXE"
    started = time.time()
    try:
        root.mkdir(parents=True)
        lane(root)
    except BaseException as error:  # reported by main() after all lanes ended
        errors.append((lane.__name__, error))
    finally:
        for open_session in LOCAL.sessions.values():
            open_session.dispose()
        print(f"lane {lane.__name__}: {time.time() - started:.1f} s", flush=True)


def main():
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as tmp:
        root = Path(tmp).resolve()
        errors = []
        try:
            # The sections do not share clones, locks or processes (each owns a
            # synthetic DataRoot), so they run side by side.
            lanes = (lane_locks, lane_processes, lane_scripts, lane_machine)
            threads = [threading.Thread(target=run_lane, args=(lane, root/lane.__name__, errors)) for lane in lanes]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
        finally:
            # Only processes this test started (its own Popen children).
            for p in children:
                if p.poll() is None:
                    p.kill()
                    p.wait(timeout=30)
        if errors:
            for name, error in errors[1:]:
                print(f"lane {name} also failed: {error!r}", flush=True)
            raise errors[0][1]
    print(f"clone guard: {checks} checks passed; only own mock processes were started")


if __name__ == "__main__":
    main()
