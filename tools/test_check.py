"""tools/check.ps1 and tools/check_counters.py on synthetic inputs.

Everything happens below one temporary directory: a throwaway git repository
with the files check.ps1 reads (hook-names.txt, docs), fake `bazel.cmd` and
`python.cmd` commands that log their arguments and print canned results, a
fake Es2Bazel module (New-Es2PreparedProxy) and a mock live runner that copies
a prepared run directory (the shapes of a real run-oracle-parallel.ps1 output:
summary.json, <suite>/runner.log, <suite>/run/<suite>-comparison.json and
<suite>.jsonl with tick and exit_counters lines). No Bazel, no game, no clone
and none of the real locations (the real es2-data, the real runner) is used.

usage: test_check.py <check.ps1> <check_counters.py> [<configure-runtime.ps1> <cw3220.dll>]
(With the last two, one case deploys the own DLL with the real script on a synthetic clone of
stand-in files in a temporary DataRoot.)
The own-sim stage (-OwnSim) runs fake replay_suite.py / frames_suite.py drivers (FAKE_PY) and the
start-own-menu.ps1 stand-in with -Sim own; nothing of es2sim is built or started.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

CHECK = Path(sys.argv[1]).resolve()
COUNTERS = Path(sys.argv[2]).resolve()
REAL_RUNTIME = Path(sys.argv[3]).resolve() if len(sys.argv) > 4 else None
OWN_DLL = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
PWSH = Path(os.environ.get("ES2_PWSH") or shutil.which("pwsh") or "")
if not PWSH.name or not PWSH.exists():
    PWSH = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
GIT = shutil.which("git") or "C:/Program Files/Git/cmd/git.exe"
NO_WINDOW = 0x08000000
HOOKS = ["alpha", "beta", "gamma"]
SIM_SUITES = ["s-long", "s-mid", "s-short"]

checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def git(repo, *args):
    r = subprocess.run([GIT, "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", creationflags=NO_WINDOW, timeout=60)
    check(r.returncode == 0, (args, r.stdout, r.stderr))
    return r.stdout.strip()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def write_lf(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


# ---- Synthetic run directory (shape of a run-oracle-parallel.ps1 output) ------------------------

def tick(sequence, scale, skipped=0, bypassed=0):
    record = {"event": "tick", "sequence": sequence, "qpc": sequence * 1000, "completed_clock_ms": sequence * 47,
              "limiter_bypassed": False, "rng_a_hex": "01", "position_raw_i32": [1, 2, 3]}
    for hook in HOOKS:
        record["hk_%s_calls" % hook] = scale
        record["hk_%s_mismatches" % hook] = 0
        record["hk_%s_skipped" % hook] = skipped if hook == "beta" else 0
        record["hk_%s_bypassed" % hook] = bypassed if hook == "gamma" else 0
        record["hk_%s_call_410000" % hook] = scale * 2
    return record


def make_suite(live, name, ticks, common, result="identical", exit_scale=None, skipped=0, bypassed=0, runner_log=None,
               with_jsonl=True, rtl_last=None, rtl_exit=None, hook_log=None):
    base = live / name
    run = base / "run"
    run.mkdir(parents=True)
    write(base / "runner.log", runner_log if runner_log is not None else '{"mode":"check","results":[{"suite":"%s","result":"%s"}]}\n' % (name, result))
    comparison = {"identical": result == "identical", "common_ticks": common, "golden_ticks": ticks, "run_ticks": ticks,
                  "golden_watch_changes": 2, "run_watch_changes": 2}
    write(run / ("%s-comparison.json" % name), json.dumps(comparison))
    write(run / ("%s-input.jsonl" % name), '{"event":"input"}\n')
    rows = [{"event": "start", "schema": 2, "record_limit": 1000000}]
    for n in range(1, ticks + 1):
        rows.append(tick(n, n * 10, skipped if n == ticks else 0, bypassed if n == ticks else 0))
        if n == ticks and rtl_last:
            rows[-1].update(rtl_last)
        if n == 2:
            rows.append({"event": "watch", "sequence": 2, "name": "bank", "hex": "abcd"})
    if exit_scale is not None:
        rows.append(dict({"event": "exit_counters", "reason": "shutdown", "last_sequence": ticks, "frame_counter": ticks},
                         **{k: v for k, v in tick(ticks, exit_scale, skipped, bypassed).items() if k.startswith("hk_")}))
        if rtl_exit:
            rows[-1].update(rtl_exit)
    if hook_log is not None:
        # The mismatch log the proxy opens in compare mode (zero bytes when nothing mismatched).
        (run / ("%s.jsonl.hooks.jsonl" % name)).write_bytes(hook_log)
    if with_jsonl:
        write(run / ("%s.jsonl" % name), "".join(json.dumps(row, separators=(",", ":")) + "\r\n" for row in rows))


def make_live(root, suites):
    """suites: list of (name, kwargs of make_suite)."""
    live = root
    live.mkdir(parents=True)
    results = []
    for name, kw in suites:
        kw = dict(kw)
        extra = kw.pop("summary", {})  # More keys of the suite's result item (exit_code, exit_code_state, ...)
        make_suite(live, name, **kw)
        results.append(dict({"suite": name, "result": kw.get("result", "identical")}, **extra))
    write(live / "summary.json", json.dumps({"mode": "check", "output": str(live), "commit": "x", "dirty_tree": False,
                                              "hooks": "alpha=replaced", "results": results}))


# ---- Fixture ---------------------------------------------------------------------------------

MOCK_RUNNER = r"""[CmdletBinding()]
param([string[]]$InstallName, [string[]]$Suite = @(), [string]$PreparedProxy, [string]$Hooks, [string]$OutputDirectory, [string]$DataRoot = '')
$ErrorActionPreference = 'Stop'
# FAKE_RUNNER_WAIT_BAZEL: note whether the fake bazel had been called when the runner started and wait
# (60 s at most) until it has been: with -Overlap it is called while the runner runs.
$atStart = $null; $during = $null
if ($env:FAKE_RUNNER_WAIT_BAZEL) {
    $atStart = [bool](Test-Path -LiteralPath $env:FAKE_BAZEL_LOG)
    if ($env:FAKE_RUNNER_STARTED) { Set-Content -LiteralPath $env:FAKE_RUNNER_STARTED 'started' }
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (!(Test-Path -LiteralPath $env:FAKE_BAZEL_LOG) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    $during = [bool](Test-Path -LiteralPath $env:FAKE_BAZEL_LOG)
}
if ($env:FAKE_RUNNER_STARTED) { Set-Content -LiteralPath $env:FAKE_RUNNER_STARTED 'started' }
[ordered]@{InstallName=@($InstallName);Suite=@($Suite);PreparedProxy=$PreparedProxy;Hooks=$Hooks;OutputDirectory=$OutputDirectory
    DataRoot=$DataRoot;ES2_RTL_CONFIG=$env:ES2_RTL_CONFIG;ES2_ORACLE_HIDDEN_WINDOW=$env:ES2_ORACLE_HIDDEN_WINDOW;ES2_BWCC32=$env:ES2_BWCC32;ES2_NEW_FAIL=$env:ES2_NEW_FAIL;ES2_NEW_FAIL_LOG=$env:ES2_NEW_FAIL_LOG
    BazelAtStart=$atStart;BazelDuringRun=$during
    CloneDll=$(if ($env:FAKE_RUNNER_CLONE_DLL) { (Get-FileHash -LiteralPath $env:FAKE_RUNNER_CLONE_DLL -Algorithm SHA256).Hash } else { $null })} | ConvertTo-Json | Set-Content -LiteralPath $env:FAKE_RUNNER_LOG -Encoding UTF8
if ($env:FAKE_RUNNER_THROW) { throw 'the runner broke' }
# FAKE_NEW_FAIL_WRITE=<suite> stands for the proxy writing that suite's ES2_NEW_FAIL_LOG file.
if ($env:FAKE_NEW_FAIL_WRITE -and $env:ES2_NEW_FAIL_LOG) { Set-Content -LiteralPath $env:ES2_NEW_FAIL_LOG.Replace('{suite}', $env:FAKE_NEW_FAIL_WRITE) -Value '{"ev":"x"}' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
if ($env:FAKE_RUNNER_TEMPLATE) { Copy-Item -LiteralPath $env:FAKE_RUNNER_TEMPLATE -Destination $OutputDirectory -Recurse }
Write-Output '{"mode":"check"}'
exit ([int]$env:FAKE_RUNNER_EXIT)
"""

FAKE_MODULE = r"""function New-Es2PreparedProxy {
    param([Parameter(Mandatory)][string]$Directory)
    if (Test-Path -LiteralPath $Directory) { throw 'Prepared build directory must be fresh.' }
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $manifest = Join-Path $Directory 'prepared-proxy.json'
    '{"schema":1}' | Set-Content -LiteralPath $manifest
    Write-Output 'fake build line'
    return $manifest
}
# The own runtime build (a read-only file, as Bazel's outputs are).
function Copy-Es2BazelOutput {
    param([Parameter(Mandatory)][string]$Target, [Parameter(Mandatory)][string]$Destination)
    Add-Content -LiteralPath $env:FAKE_RUNTIME_LOG -Value "build $Target into $Destination"
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    # es2sim and SDL3.dll for the own-sim stage; FAKE_SIM_BUILD_FAIL returns a path that does not exist.
    if ($Target -eq '//sim:es2sim_window' -and $env:FAKE_SIM_BUILD_FAIL) { return (Join-Path $Destination 'missing.exe') }
    $leaf = switch ($Target) { '//sim:es2sim_window' { 'es2sim_window.exe' } '//sim:sdl_runtime' { 'SDL3.dll' } default { 'cw3220.dll' } }
    $copy = Join-Path $Destination $leaf
    [IO.File]::WriteAllText($copy, 'own runtime build')
    (Get-Item -LiteralPath $copy).IsReadOnly = $true
    return $copy
}
Export-ModuleMember -Function New-Es2PreparedProxy, Copy-Es2BazelOutput
"""

# setup/configure-runtime.ps1 stand-in. One state file per clone in FAKE_RUNTIME_DIR ("own <sha256>" or
# absent = original); every call is logged with whether the mock runner had started; FAKE_RUNTIME_FAIL =
# "<action>:<clone>" makes that call throw (a refusal of the real script).
MOCK_RUNTIME = r"""[CmdletBinding()]
param([string]$DataRoot = '', [Parameter(Mandatory)][string]$InstallName, [string]$Source, [switch]$Apply, [switch]$Remove)
$ErrorActionPreference = 'Stop'
$action = if ($Apply) { 'apply' } elseif ($Remove) { 'remove' } else { 'preview' }
Add-Content -LiteralPath $env:FAKE_RUNTIME_LOG -Value ("$action $InstallName source=$Source data=$DataRoot runner=" + [bool](Test-Path -LiteralPath $env:FAKE_RUNNER_LOG))
if ($env:FAKE_RUNTIME_FAIL -eq "${action}:$InstallName") { throw "fake refusal of $action on $InstallName" }
$file = Join-Path $env:FAKE_RUNTIME_DIR $InstallName
$state = 'original'; $own = ''
if (Test-Path -LiteralPath $file) { $state, $own = (Get-Content -LiteralPath $file -Raw).Trim() -split ' ' }
if ($Apply) {
    $own = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ($env:FAKE_RUNTIME_WRONG -eq $InstallName) { $own = 'WRONG' }
    Set-Content -LiteralPath $file -Value "own $own"; $state = 'own'
} elseif ($Remove -and $state -eq 'own') {
    Remove-Item -LiteralPath $file; $state = 'original'; $own = ''
}
$record = [ordered]@{installation=$InstallName; state=$state; original_sha256='ORIG'}
if ($own) { $record.own_sha256 = $own }
$record | ConvertTo-Json
"""

# setup/start-own-menu.ps1 -Unattended stand-in. Logs its arguments, whether the prepared proxy manifest and the
# runner's log existed when it started and the clone's runtime state (FAKE_RUNTIME_DIR), writes FAKE_OWN_VERDICT (a
# JSON file) as <SessionDirectory>/evidence/unattended.json, exits FAKE_OWN_EXIT. -Shell gui logs to
# FAKE_OWN_LOG with -gui before .json and takes FAKE_OWN_VERDICT_GUI / FAKE_OWN_EXIT_GUI when they are set.
# -Sim own (the own-sim stage's chain) logs with -sim before .json and takes FAKE_OWN_VERDICT_SIM / FAKE_OWN_EXIT_SIM.
MOCK_OWN_SHELL = r"""[CmdletBinding()]
param([switch]$Unattended, [string]$Shell = 'text', [string]$InstallName, [string]$PreparedProxy = '', [string]$SessionDirectory, [string]$Hooks = '',
      [string]$DataRoot = '', [string]$BazelOutputBase = '', [string]$Python = '', [string]$Sim = 'dbsim', [string]$SimExe = '', [string]$SimSdl = '')
$ErrorActionPreference = 'Stop'
$runtime = Join-Path $env:FAKE_RUNTIME_DIR $InstallName
$log = if ($Sim -eq 'own') { $env:FAKE_OWN_LOG -replace '\.json$', '-sim.json' } elseif ($Shell -eq 'gui') { $env:FAKE_OWN_LOG -replace '\.json$', '-gui.json' } else { $env:FAKE_OWN_LOG }
$verdict = if ($Sim -eq 'own') { $env:FAKE_OWN_VERDICT_SIM } elseif ($Shell -eq 'gui' -and $null -ne $env:FAKE_OWN_VERDICT_GUI) { $env:FAKE_OWN_VERDICT_GUI } else { $env:FAKE_OWN_VERDICT }
$code = if ($Sim -eq 'own') { if ($env:FAKE_OWN_EXIT_SIM) { $env:FAKE_OWN_EXIT_SIM } else { '0' } } elseif ($Shell -eq 'gui' -and $env:FAKE_OWN_EXIT_GUI) { $env:FAKE_OWN_EXIT_GUI } else { $env:FAKE_OWN_EXIT }
[ordered]@{Unattended=[bool]$Unattended; Shell=$Shell; InstallName=$InstallName; PreparedProxy=$PreparedProxy; ProxyExisted=$(if ($PreparedProxy) { [bool](Test-Path -LiteralPath $PreparedProxy) } else { $false })
    Sim=$Sim; SimExe=$SimExe; SimExeExisted=$(if ($SimExe) { [bool](Test-Path -LiteralPath $SimExe) } else { $false }); SimSdl=$SimSdl
    SessionDirectory=$SessionDirectory; Hooks=$Hooks; DataRoot=$DataRoot; BazelOutputBase=$BazelOutputBase; Python=$Python
    RunnerDone=[bool](Test-Path -LiteralPath $env:FAKE_RUNNER_LOG)
    Runtime=$(if (Test-Path -LiteralPath $runtime) { (Get-Content -LiteralPath $runtime -Raw).Trim().Split(' ')[0] } else { 'original' })} |
    ConvertTo-Json | Set-Content -LiteralPath $log -Encoding UTF8
if ($verdict) {
    New-Item -ItemType Directory -Force -Path (Join-Path $SessionDirectory 'evidence') | Out-Null
    Copy-Item -LiteralPath $verdict -Destination (Join-Path $SessionDirectory 'evidence\unattended.json')
}
Write-Output "own shell mock: done ($Shell)"
exit ([int]$code)
"""

FAKE_PY = r"""import os, runpy, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ["FAKE_PY_LOG"], "a", encoding="utf-8") as log:
    log.write(" ".join(args) + "\n")
# The New-fail variables each call sees (the live stage must have restored them by the evidence stage).
with open(os.environ["FAKE_PY_LOG"] + ".env", "a", encoding="utf-8") as log:
    log.write("ES2_NEW_FAIL=%s;ES2_NEW_FAIL_LOG=%s\n" % (os.environ.get("ES2_NEW_FAIL"), os.environ.get("ES2_NEW_FAIL_LOG")))
script = Path(args[1]).name if len(args) > 1 else ""
if script == "check_counters.py":
    sys.argv = args[1:]
    runpy.run_path(args[1], run_name="__main__")
elif script == "check_signatures.py":
    mode = os.environ.get("FAKE_SIG", "pass")
    if mode == "pass":
        print("PASS: 5/5 registry signatures, hash gate verified")
    elif mode == "skip":
        print("SKIP: ES2_DATA_ROOT is not set")
    else:
        print("PASS: 4/5 registry signatures, hash gate verified")
        sys.exit(1)
    print("ES2_DATA_ROOT=" + os.environ.get("ES2_DATA_ROOT", ""))
elif script == "evidence_budget.py":
    mode = os.environ.get("FAKE_BUDGET", "pass")
    print("staged files: 3, refused: " + ("1" if mode == "fail" else "0"))
    if mode == "fail":
        print("REFUSED logs/x/run/s.jsonl: raw tick log of an identical run")
    print("evidence directories older than 2026-10-11 that no document cites: 2 (1.0 KB)")
    print(("REFUSED" if mode == "fail" else "OK") + ": staged 3, refused " + ("1" if mode == "fail" else "0") + "; logs 1.0 KB")
    sys.exit(1 if mode == "fail" else 0)
elif script == "compare_audit.py":
    problems = int(os.environ.get("FAKE_AUDIT_PROBLEMS", "0"))
    for n in range(problems):
        print("docs/x.md: row %d is wrong" % n)
    print("3 hooks checked, %d problems" % problems)
    sys.exit(1 if problems else 0)
elif script in ("replay_suite.py", "frames_suite.py"):
    # The own-sim stage's drivers. FAKE_REPLAY / FAKE_FRAMES map a suite to its result word (default IDENTICAL,
    # PASS); FAKE_NO_CAPTURE lists suites without a frame capture; every call is one JSON line in FAKE_SIM_LOG.
    import json, time
    opts, flags, rest, i = {}, [], args[2:], 0
    while i < len(rest):
        if rest[i].startswith("--") and i + 1 < len(rest) and not rest[i + 1].startswith("--"):
            opts[rest[i]] = rest[i + 1]; i += 2
        else:
            flags.append(rest[i]); i += 1
    name = opts["--suite"]
    record = dict(script=script, suite=name, opts=opts, flags=flags, data_root=os.environ.get("ES2_DATA_ROOT", ""),
                  frames_reference=os.environ.get("ES2_FRAMES_REFERENCE", ""), oracle_dir=os.environ.get("ES2_ORACLE_DIR", ""))
    if script == "replay_suite.py":
        sim = opts.get("--sim", "")
        record.update(sim_exists=os.path.isfile(sim), sdl_beside=os.path.isfile(os.path.join(os.path.dirname(sim), "SDL3.dll")))
        running = Path(os.environ["FAKE_SIM_RUNNING"])
        running.mkdir(exist_ok=True)
        (running / name).write_text("x")
        time.sleep(0.4)
        record["concurrent"] = len(list(running.iterdir()))
        (running / name).unlink()
        word = json.loads(os.environ.get("FAKE_REPLAY") or "{}").get(name, "IDENTICAL")
        out = Path(opts["--output-dir"]) / name
        out.mkdir(parents=True, exist_ok=True)
        (out / "result.json").write_text("{}")
        (out / "tick-log.jsonl").write_text("{}\n")
        if "--frames" in flags and name not in os.environ.get("FAKE_NO_CAPTURE", "").split(","):
            (out / "frames.ndjson").write_text("{}\n")
        print("replay %s: %s (fake)" % (name, word))
        code = 0 if word in ("IDENTICAL", "SKIP") else 1
    else:
        record["capture_exists"] = os.path.isfile(opts.get("--capture", ""))
        word = json.loads(os.environ.get("FAKE_FRAMES") or "{}").get(name, "PASS")
        print("frames %s: compared through frame 9 (fake)" % name)
        print("frames %s: %s" % (name, word))
        code = 0 if word in ("PASS", "SKIP") else 1
    with open(os.environ["FAKE_SIM_LOG"], "a", encoding="utf-8") as log:
        log.write(json.dumps(record) + "\n")
    sys.exit(code)
else:
    print("unexpected script " + script)
    sys.exit(9)
"""

# FAKE_BAZEL_WAIT_RUNNER (the -Overlap runs): wait up to about 60 s for the mock runner's start marker
# FAKE_RUNNER_STARTED before logging, so the order "runner started, then bazel called" is not a race.
BAZEL_CMD = ('@if defined FAKE_BAZEL_WAIT_RUNNER call :wait\r\n'
             '@echo %CD% %* >> "%FAKE_BAZEL_LOG%"\r\n@type "%FAKE_BAZEL_OUT%" 1>&2\r\n@exit /b %FAKE_BAZEL_CODE%\r\n'
             ':wait\r\n@for /l %%i in (1,1,60) do @if not exist "%FAKE_RUNNER_STARTED%" ping -n 2 127.0.0.1 >nul\r\n@exit /b 0\r\n')
PY_CMD = '@"%ES2_REAL_PYTHON%" -B "%~dp0fakepy.py" %*\r\n'

BAZEL_PASS = """INFO: Build completed successfully, 12 total actions
//core:a_test                                                   (cached) PASSED in 0.1s
//core:b_test                                                            PASSED in 1.2s
//tools:c_test                                                           PASSED in 0.3s

Executed 2 out of 3 tests: 3 tests pass.
"""
BAZEL_FAIL = """//core:a_test                                                   (cached) PASSED in 0.1s
//core:b_test                                                            FAILED in 1.2s
//tools:c_test                                                           TIMEOUT in 3 out of 3 in 60.0s

Executed 2 out of 3 tests: 1 test passes and 2 fail locally.
"""


class Fixture:
    def __init__(self, tmp):
        self.tmp = tmp
        self.repo = tmp / "repo"
        self.fake = tmp / "fake"
        self.logs = tmp / "logs"
        self.n = 0
        self.es2data = tmp / "es2data"
        for d in (self.repo, self.fake, self.logs, self.es2data):
            d.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        write_lf(self.repo / "tools/reference-game/proxy/hook-names.txt", "# comment\n" + "\n".join(HOOKS) + "\n")
        # alpha's entry is the function of the -NewFail site 46162b, beta's that of 480b23; gamma contains none.
        write_lf(self.repo / "tools/reference-game/proxy/hook_registry.h",
                 "/* name, RVA, descriptor, extension, group. */\n"
                 "ES2_HOOK(alpha, 0x61608, a_descriptors[0], NULL, a)\n"
                 "ES2_HOOK(beta, 0x80af0, b_descriptors[0], NULL, b)\n"
                 "ES2_HOOK(gamma, 0x0250c, c_descriptors[0], NULL, c)\n")
        for name in ("core/x.c", "hook/y.c", "docs/reference-game/compare_mode.md", "docs/reference-game/hooks.md",
                     "tools/reference-game/invitro/check_signatures.py", "tools/reference-game/scripts/compare_audit.py"):
            write_lf(self.repo / name, "x\n")
        # The registry the own-sim stage reads, and a file below sim/ (a source-stamp path with -OwnSim).
        write_lf(self.repo / "tools/reference-game/oracle-suites.json",
                 json.dumps({"format": 3, "suites": [{"name": n} for n in SIM_SUITES]}) + "\n")
        write_lf(self.repo / "sim/src/main.c", "x\n")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "init")
        (self.fake / "bazel.cmd").write_bytes(BAZEL_CMD.encode())
        (self.fake / "python.cmd").write_bytes(PY_CMD.encode())
        write_lf(self.fake / "fakepy.py", FAKE_PY)
        write_lf(self.fake / "mock-runner.ps1", MOCK_RUNNER)
        write_lf(self.fake / "FakeEs2Bazel.psm1", FAKE_MODULE)
        write_lf(self.fake / "mock-runtime.ps1", MOCK_RUNTIME)
        write_lf(self.fake / "mock-own-shell.ps1", MOCK_OWN_SHELL)
        for name in ("replay_suite.py", "frames_suite.py"):
            write_lf(self.fake / name, "# H5-27: stand-in, dispatched by fakepy.py\n")
        self.runtime_dir = tmp / "runtime-state"
        self.runtime_dir.mkdir()
        self.bazel_out = self.fake / "bazel-out.txt"
        write_lf(self.bazel_out, BAZEL_PASS)
        self.env = dict(os.environ)
        self.env["PATH"] = os.pathsep.join([os.path.normpath(os.path.dirname(GIT)), self.env.get("PATH", "")])
        self.env.update(FAKE_BAZEL_LOG=str(self.logs / "bazel.log"), FAKE_BAZEL_OUT=str(self.bazel_out), FAKE_BAZEL_CODE="0",
                        FAKE_PY_LOG=str(self.logs / "py.log"), ES2_REAL_PYTHON=sys.executable,
                        FAKE_RUNNER_LOG=str(self.logs / "runner.json"), FAKE_RUNNER_EXIT="0",
                        FAKE_RUNTIME_LOG=str(self.logs / "runtime.log"), FAKE_RUNTIME_DIR=str(self.runtime_dir),
                        FAKE_OWN_LOG=str(self.logs / "own-shell.json"), FAKE_OWN_EXIT="0",
                        FAKE_SIM_LOG=str(self.logs / "sim.jsonl"), FAKE_SIM_RUNNING=str(tmp / "sim-running"))
        for key in ("FAKE_RUNNER_TEMPLATE", "FAKE_RUNNER_THROW", "FAKE_RUNTIME_FAIL", "FAKE_RUNTIME_WRONG", "ES2_BWCC32", "ES2_NEW_FAIL", "ES2_NEW_FAIL_LOG", "FAKE_NEW_FAIL_WRITE", "FAKE_OWN_VERDICT",
                    "FAKE_OWN_VERDICT_GUI", "FAKE_OWN_EXIT_GUI", "FAKE_REPLAY", "FAKE_FRAMES", "FAKE_NO_CAPTURE", "FAKE_SIM_BUILD_FAIL",
                    "FAKE_OWN_VERDICT_SIM", "FAKE_OWN_EXIT_SIM", "ES2_FRAMES_REFERENCE", "ES2_ORACLE_DIR"):
            self.env.pop(key, None)
        self.env.setdefault("PATHEXT", ".COM;.EXE;.BAT;.CMD")
        self.env.pop("ES2_DATA_ROOT", None)
        self.env.pop("ES2_RTL_CONFIG", None)
        self.env.pop("ES2_ORACLE_HIDDEN_WINDOW", None)

    def evidence(self):
        self.n += 1
        return self.tmp / ("evidence-%d" % self.n)

    def run(self, *args, evidence=None, env=None, with_defaults=True):
        evidence = evidence or self.evidence()
        for stale in self.logs.iterdir():
            stale.unlink()
        cmd = [str(PWSH), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(CHECK),
               "-Repo", str(self.repo), "-Bazel", str(self.fake / "bazel.cmd"), "-Python", str(self.fake / "python.cmd"),
               "-DataRoot", str(self.tmp / "data"), "-EvidenceRoot", str(evidence),
               "-LiveRunner", str(self.fake / "mock-runner.ps1"), "-SetupModule", str(self.fake / "FakeEs2Bazel.psm1"),
               *([] if "-RuntimeScript" in args else ["-RuntimeScript", str(self.fake / "mock-runtime.ps1")]),
               "-OwnShellScript", str(self.fake / "mock-own-shell.ps1"),
               "-OwnSimReplayScript", str(self.fake / "replay_suite.py"), "-OwnSimFramesScript", str(self.fake / "frames_suite.py"),
               *([] if "-Es2Data" in args else ["-Es2Data", str(self.es2data)]), *map(str, args)]
        full_env = dict(self.env, **(env or {}))
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=full_env,
                           creationflags=NO_WINDOW, timeout=240)
        return r.returncode, r.stdout + r.stderr, evidence

    def runtime_states(self):
        return {p.name: p.read_text(encoding="utf-8-sig").split()[0] for p in self.runtime_dir.iterdir()}

    def runtime_calls(self):
        return [line.split()[:2] + [line.rsplit("runner=", 1)[1]] for line in self.log("runtime.log").splitlines()
                if line.split() and line.split()[0] in ("preview", "apply", "remove")]

    def runner_json(self):
        return json.loads((self.logs / "runner.json").read_text(encoding="utf-8-sig"))

    def log(self, name):
        path = self.logs / name
        return path.read_text(encoding="utf-8", errors="replace").replace(chr(34) + " " + chr(34), " ").replace(chr(34), "") if path.exists() else ""


def summary(evidence):
    return (evidence / "summary.md").read_text(encoding="utf-8")


def stage_rows(evidence):
    data = json.loads((evidence / "summary.json").read_text(encoding="utf-8-sig"))
    return {s["name"]: s for s in data["stages"]}, data


# ---- Tests -----------------------------------------------------------------------------------

def test_default_stages(fx):
    code, out, ev = fx.run()
    check(code == 0, out)
    stages, data = stage_rows(ev)
    check(data["result"] == "PASSED", data)
    check([stages[n]["result"] for n in ("quick", "full", "signatures", "audit", "live")] ==
          ["PASSED", "SKIPPED", "PASSED", "PASSED", "SKIPPED"], stages)
    text = summary(ev)
    check("tests passed 3, failed 0" in text, text)
    check("5 of 5 registry signatures" in text, text)
    check("3 hooks checked, 0 problems" in text, text)
    check("Executed 2 out of 3 tests" in text, text)
    for name in ("quick.log", "signatures.log", "audit.log"):
        check((ev / name).is_file(), name)
    bazel = fx.log("bazel.log")
    check(" test //..." in bazel and "differential_full" not in bazel and "//tools:scripts" not in bazel, bazel)
    check(bazel.count("\n") == 1, bazel)
    py = fx.log("py.log")
    check("check_signatures.py" in py and "compare_audit.py --check docs/reference-game/compare_mode.md --hooks-doc docs/reference-game/hooks.md" in py, py)
    check("ES2_DATA_ROOT=" in (ev / "signatures.log").read_text(encoding="utf-8") and str(fx.tmp / "data") in (ev / "signatures.log").read_text(encoding="utf-8"))
    check(not (ev / "live").exists())


def test_evidence_stage(fx):
    # The evidence budget runs last, after the live stage, and fails only on a refusal.
    code, out, ev = fx.run()
    check(code == 0, out)
    stages, data = stage_rows(ev)
    check(stages["evidence"]["result"] == "PASSED" and stages["evidence"]["numbers"] == "staged 3, refused 0; logs 1.0 KB", stages["evidence"])
    check([s["name"] for s in data["stages"]][-1] == "evidence", data["stages"])
    check("evidence_budget.py --data-root %s --repo %s" % (fx.es2data, fx.repo) in fx.log("py.log").replace("\\\\", "\\"), fx.log("py.log"))
    check("older than 2026-10-11" in summary(ev), summary(ev))
    code, out, ev = fx.run(env={"FAKE_BUDGET": "fail"})
    stages, data = stage_rows(ev)
    check(code == 1 and data["result"] == "FAILED" and stages["evidence"]["result"] == "FAILED", (code, out))
    check("REFUSED logs/x/run/s.jsonl" in summary(ev) and stages["quick"]["result"] == "PASSED", summary(ev))
    code, out, ev = fx.run("-SkipEvidence", env={"FAKE_BUDGET": "fail"})
    stages, _ = stage_rows(ev)
    check(code == 0 and stages["evidence"]["result"] == "SKIPPED" and "evidence_budget" not in fx.log("py.log"), (code, out))
    code, out, ev = fx.run("-Es2Data", str(fx.tmp / "absent"), env={"FAKE_BUDGET": "fail"})
    stages, _ = stage_rows(ev)
    check(code == 0 and stages["evidence"]["result"] == "SKIPPED" and "no es2-data" in stages["evidence"]["numbers"], (code, out))
    # Evidence alone never counts as a selected stage.
    code, out, ev = fx.run("-SkipQuick", "-SkipSignatures", "-SkipAudit")
    check(code == 2 and "no stage selected" in out, (code, out))


def test_stage_selection(fx):
    code, out, ev = fx.run("-Full", "-SkipQuick", "-SkipAudit")
    check(code == 0, out)
    stages, _ = stage_rows(ev)
    check([stages[n]["result"] for n in ("quick", "full", "signatures", "audit")] == ["SKIPPED", "PASSED", "PASSED", "SKIPPED"], stages)
    bazel = fx.log("bazel.log")
    # -Full runs the full in-vitro tier and the manual script tests of the PowerShell tools (//tools:scripts).
    check("differential_full" in bazel and "//tools:scripts" in bazel and "//..." not in bazel, bazel)
    check(bazel.count(" test ") == 1 and bazel.count("\n") == 1, bazel)
    check("compare_audit" not in fx.log("py.log"))
    # Bazel options reach the command.
    code, out, ev = fx.run("-SkipSignatures", "-SkipAudit", "-SkipEvidence", "-BazelOutputBase", str(fx.tmp / "ob"),
                           "-BazelContentsCache", str(fx.tmp / "oc"), "-BazelArgs", "--local_resources=cpu=4")
    check(code == 0, out)
    bazel = fx.log("bazel.log")
    check("--output_base=%s test" % (fx.tmp / "ob") in bazel and "--repo_contents_cache=%s" % (fx.tmp / "oc") in bazel
          and "--local_resources=cpu=4" in bazel and bazel.rstrip().endswith("//..."), bazel)
    check(not fx.log("py.log"))
    # Nothing selected: refused (2), no evidence.
    code, out, ev = fx.run("-SkipQuick", "-SkipSignatures", "-SkipAudit")
    check(code == 2 and "no stage selected" in out and not ev.exists(), (code, out))
    # Live-only parameters need -Live.
    code, out, ev = fx.run("-HookModes", str(fx.fake / "bazel-out.txt"))
    check(code == 2 and "-Live" in out and not ev.exists(), (code, out))


def test_exit_codes(fx):
    write_lf(fx.bazel_out, BAZEL_FAIL)
    code, out, ev = fx.run(env={"FAKE_BAZEL_CODE": "3"})
    check(code == 1, out)
    stages, data = stage_rows(ev)
    check(data["result"] == "FAILED" and stages["quick"]["result"] == "FAILED", data)
    check(stages["signatures"]["result"] == "PASSED" and stages["audit"]["result"] == "PASSED", "later stages still run")
    text = summary(ev)
    check("tests passed 1, failed 2; bazel exit 3" in text and "failed: //core:b_test" in text and "failed: //tools:c_test" in text, text)
    write_lf(fx.bazel_out, BAZEL_PASS)
    code, out, ev = fx.run("-SkipQuick", env={"FAKE_SIG": "fail", "FAKE_AUDIT_PROBLEMS": "2"})
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["signatures"]["result"] == "FAILED" and stages["audit"]["result"] == "FAILED", (code, out))
    text = summary(ev)
    check("4 of 5 registry signatures" in text and "3 hooks checked, 2 problems" in text and "row 1 is wrong" in text, text)
    code, out, ev = fx.run("-SkipQuick", "-SkipAudit", env={"FAKE_SIG": "skip"})
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["signatures"]["result"] == "FAILED" and "skipped by the script" in summary(ev), (code, out))
    # A refused call keeps its evidence target untouched.
    target = fx.tmp / "existing"
    target.mkdir()
    (target / "keep.txt").write_text("keep")
    code, out, _ = fx.run(evidence=target)
    check(code == 2 and "evidence directory exists" in out and [p.name for p in target.iterdir()] == ["keep.txt"], (code, out))
    check(not fx.log("bazel.log"))


def live_template(fx, name, suites):
    template = fx.tmp / name
    make_live(template, suites)
    return {"FAKE_RUNNER_TEMPLATE": str(template)}


def all_identical_suites():
    return [("a60", dict(ticks=4, common=3)),
            ("c1s", dict(ticks=5, common=5, exit_scale=777, skipped=3, bypassed=2)),
            ("c4", dict(ticks=3, common=2, exit_scale=99))]


def test_live_identical(fx):
    env = live_template(fx, "t-ident", all_identical_suites())
    code, out, ev = fx.run("-Live", env=env)
    check(code == 0, out)
    stages, data = stage_rows(ev)
    check([stages[n]["result"] for n in ("quick", "signatures", "audit", "live")] == ["PASSED"] * 4, stages)
    runner = fx.runner_json()
    check(runner["InstallName"] == ["timing-r111", "timing-r111-p1", "timing-r111-p2", "timing-r111-p4", "timing-r111-p5"], runner)
    check(runner["Hooks"] == "alpha=replaced,beta=replaced,gamma=replaced", runner)
    check(runner["PreparedProxy"] == str(ev / "prepared" / "prepared-proxy.json"), runner)
    check(runner["OutputDirectory"] == str(ev / "live") and not runner["ES2_RTL_CONFIG"] and not runner["DataRoot"], runner)
    # hook-counters.json: compared tick = tick number common_ticks, last record, exit record.
    counters = json.loads((ev / "hook-counters.json").read_text(encoding="utf-8"))["suites"]
    check(sorted(counters) == ["a60", "c1s", "c4"], counters.keys())
    a60 = counters["a60"]
    check(a60["common_ticks"] == 3 and a60["compared_tick"]["hk_alpha_calls"] == 30 and a60["last_record"]["hk_alpha_calls"] == 40, a60["compared_tick"])
    check(a60["exit_counters"] is None, a60)
    check(all(k.startswith("hk_") for k in a60["compared_tick"]) and "hk_gamma_call_410000" in a60["last_record"], a60["last_record"].keys())
    check(counters["c1s"]["exit_counters"]["hk_alpha_calls"] == 777 and counters["c1s"]["compared_tick"]["hk_alpha_calls"] == 50, counters["c1s"])
    check(counters["c4"]["compared_tick"]["hk_beta_calls"] == 20 and counters["c4"]["last_record"]["hk_beta_calls"] == 30, counters["c4"])
    # Nonzero skipped/bypassed counters are reported, zero ones are not.
    text = summary(ev)
    check("NONZERO c1s: hk_beta_skipped = 3" in text and "NONZERO c1s: hk_gamma_bypassed = 2" in text, text)
    check("NONZERO a60" not in text and "NONZERO c4" not in text, text)
    check("suites identical 3 of 3" in text and "nonzero skipped/bypassed/mismatch counters 2" in text, text)
    check("counters c1s: 5 ticks, exit record" in text and "counters a60: 4 ticks, no exit record" in text, text)
    # Raw tick logs deleted (all identical), the small files stay.
    for suite in ("a60", "c1s", "c4"):
        run = ev / "live" / suite / "run"
        check(not (run / (suite + ".jsonl")).exists(), suite)
        check((run / (suite + "-input.jsonl")).exists() and (run / (suite + "-comparison.json")).exists() and (ev / "live" / suite / "runner.log").exists(), suite)
    check("deleted 3 raw tick log(s)" in text, text)
    check((ev / "hooks-argument.txt").read_text(encoding="utf-8") == runner["Hooks"])
    # The proxy was built from the fake module; with -PreparedProxy the build is skipped.
    check("fake build line" in (ev / "live.log").read_text(encoding="utf-8"))
    manifest = fx.tmp / "given-prepared.json"
    manifest.write_text("{}")
    code, out, ev2 = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", "-PreparedProxy", str(manifest),
                            "-OracleDataRoot", str(fx.tmp / "oracle-data"), env=env)
    runner = fx.runner_json()
    check(code == 0 and runner["PreparedProxy"] == str(manifest) and runner["DataRoot"] == str(fx.tmp / "oracle-data") and not (ev2 / "prepared").exists(), (code, out, runner))


def test_live_modes_and_rtl(fx):
    env = live_template(fx, "t-modes", all_identical_suites())
    modes = fx.tmp / "modes.txt"
    write_lf(modes, "# overrides\nbeta=compare\n\ngamma = Original\n")
    rtl = fx.tmp / "rtl.ini"
    write_lf(rtl, "[rtl]\nenabled=1\n")
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", "-HookModes", str(modes), "-RtlConfig", str(rtl), env=env)
    check(code == 0, out)
    runner = fx.runner_json()
    check(runner["Hooks"] == "alpha=replaced,beta=compare,gamma=original", runner)
    check(runner["ES2_RTL_CONFIG"] == str(rtl), runner)
    check((ev / "hook-modes.txt").is_file() and (ev / "rtl-config.ini").is_file())
    check("hooks: 1 compare, 1 original, 1 replaced" in summary(ev), summary(ev))
    # Bad override files refuse before any stage.
    for text, needle in (("delta=compare\n", "unknown hook"), ("alpha=fast\n", "is not original, replaced or compare"), ("alpha\n", "expected name=mode")):
        write_lf(modes, text)
        code, out, ev = fx.run("-Live", "-HookModes", str(modes), env=env)
        check(code == 2 and needle in out and not ev.exists(), (code, out))
    check(not fx.log("bazel.log"))


def test_live_hidden_window(fx):
    """-HiddenWindow reaches the runner as ES2_ORACLE_HIDDEN_WINDOW ('1', or '0' for
    -HiddenWindow:$false); not given, the variable stays unset (the runner's default holds)."""
    env = live_template(fx, "t-hidden", all_identical_suites())
    base = ("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit")
    cases = [(("-HiddenWindow",), "1", "window: hidden (-HiddenWindow)"), ((), None, None)]
    # "-File" binds "-Switch:$false" in PowerShell 7 only (Windows PowerShell 5.1 refuses the string).
    if PWSH.name.lower().startswith("pwsh"):
        cases.append((("-HiddenWindow:$false",), "0", "window: visible (-HiddenWindow)"))
    for extra, value, line in cases:
        code, out, ev = fx.run(*base, *extra, env=env)
        check(code == 0, out)
        check(fx.runner_json()["ES2_ORACLE_HIDDEN_WINDOW"] == value, (extra, fx.runner_json()))
        if line:
            check(line in summary(ev), summary(ev))
        else:
            check("window:" not in summary(ev), summary(ev))
    # The runner's summary key is reported when present.
    template = fx.tmp / "t-hidden-summary"
    make_live(template, all_identical_suites())
    data = json.loads((template / "summary.json").read_text(encoding="utf-8"))
    data["hidden_window"] = True
    write(template / "summary.json", json.dumps(data))
    code, out, ev = fx.run(*base, "-HiddenWindow", env=dict(env, FAKE_RUNNER_TEMPLATE=str(template)))
    check(code == 0 and "hidden window: yes (runner summary)" in summary(ev), (out, summary(ev)))


def test_live_install_names(fx):
    """One to six distinct measurement clones reach the live runner; anything else is refused
    (exit 2) before a stage runs; a comma-separated list is one -File argument and is split."""
    env = live_template(fx, "t-clones", all_identical_suites())
    base = ("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit")
    six = ["timing-r111"] + ["timing-r111-p%d" % n for n in (1, 2, 3, 4, 5)]
    for given, wanted in ((",".join(six), six), ("timing-r111", ["timing-r111"]), ("timing-r111, timing-r111-p4", ["timing-r111", "timing-r111-p4"])):
        code, out, ev = fx.run(*base, "-InstallName", given, env=env)
        check(code == 0 and fx.runner_json()["InstallName"] == wanted, (given, out, fx.runner_json()))
        check("clones: " + ", ".join(wanted) in summary(ev), summary(ev))
    for bad in (",".join(six + ["timing-r111-p6"]), "timing-r111,timing-r111", "native-r111", "a b", ","):
        code, out, ev = fx.run(*base, "-InstallName", bad, env=env)
        check(code == 2 and "-InstallName" in out and not ev.exists(), (bad, code, out))
        check(not fx.log("runner.json"), "nothing may run")


def test_live_exit_codes_and_hook_logs(fx):
    # The runner's exit-code record is reported per suite, a difference counts like any other DIFFERENT,
    # and zero-byte hook mismatch logs are removed (logs with content stay).
    suites = [("a60", dict(ticks=4, common=3, hook_log=b"", summary=dict(exit_code=0, exit_code_state="exited", exit_code_check="same"))),
              ("c1s", dict(ticks=5, common=5, exit_scale=777, hook_log=b'{"hook":"x"}\n',
                           summary=dict(exit_code=None, exit_code_state="none", exit_code_check="no golden exit code"))),
              ("c4", dict(ticks=3, common=2, exit_scale=99))]
    env = live_template(fx, "t-exit", suites)
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env=env)
    check(code == 0, out)
    text = summary(ev)
    check("exit code a60: exited 0 (same)" in text and "exit code c1s: none (no golden exit code)" in text and "exit code c4" not in text, text)
    check("exit code same 1, no golden exit code 1" in text and "suites identical 3 of 3" in text, text)
    check(not (ev / "live" / "a60" / "run" / "a60.jsonl.hooks.jsonl").exists() and "removed 1 empty hook mismatch log(s)" in text, text)
    check((ev / "live" / "c1s" / "run" / "c1s.jsonl.hooks.jsonl").read_bytes() == b'{"hook":"x"}\n')
    # A different exit code: the runner marked the suite DIFFERENT; check.ps1 names it and counts it.
    suites = [("a60", dict(ticks=4, common=3, summary=dict(exit_code=3, exit_code_state="exited", exit_code_check="different"), result="DIFFERENT")),
              ("c4", dict(ticks=3, common=2, exit_scale=99, summary=dict(exit_code=0, exit_code_state="exited", exit_code_check="same")))]
    env = live_template(fx, "t-exit-diff", suites)
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env=dict(env, FAKE_RUNNER_EXIT="1"))
    text = summary(ev)
    check(code == 1 and "suite a60: DIFFERENT" in text and "exit code a60: exited 3 (different)" in text, text)
    check("exit code same 1, no golden exit code 0, DIFFERENT 1" in text, text)


def test_live_different(fx):
    suites = [("a60", dict(ticks=4, common=3)),
              ("c1s", dict(ticks=5, common=2, result="DIFFERENT",
                           runner_log="line one\nsecond line\n" + "\n".join("tail %d" % i for i in range(30)) + "\n")),
              ("c4", dict(ticks=3, common=2, exit_scale=99))]
    env = live_template(fx, "t-diff", suites)
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env=dict(env, FAKE_RUNNER_EXIT="1"))
    check(code == 1, out)
    text = summary(ev)
    check("suite c1s: DIFFERENT" in text and "suite a60: identical" in text and "DIFFERENT 1" in text, text)
    check("first lines of c1s\\runner.log" in text and "- line one" in text and "- tail 12" in text and "tail 20" not in text, text)
    check("raw tick logs kept" in text and "refused" not in text.replace("refused 0", ""), text)
    for suite in ("a60", "c1s", "c4"):
        check((ev / "live" / suite / "run" / (suite + ".jsonl")).exists(), suite)
    check((ev / "hook-counters.json").is_file())
    stages, _ = stage_rows(ev)
    check(stages["live"]["result"] == "FAILED")


def test_live_refused(fx):
    refusal = ('{"mode":"check"}\nClose the game in this clone before suite c1s. Running in D:\\x : dbsim.EXE PID 4 (D:\\y\\dbsim.EXE, '
               "started 2026-10-11). This run did not start it and leaves it alone; if another session owns it, wait.\n")
    suites = [("a60", dict(ticks=4, common=3)),
              ("c1s", dict(ticks=0, common=0, result="DIFFERENT", runner_log=refusal, with_jsonl=False)),
              ("c4", dict(ticks=3, common=2, result="DIFFERENT", runner_log="comparison failed\n"))]
    env = live_template(fx, "t-refused", suites)
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env=dict(env, FAKE_RUNNER_EXIT="1"))
    check(code == 1, out)
    text = summary(ev)
    check("suite c1s: refused, rerun" in text and "suite c4: DIFFERENT" in text and "refused (rerun) 1" in text, text)
    check("refusal: Close the game in this clone" in text, text)
    check("raw tick logs kept" in text, text)
    # The runner dying before it wrote a summary is a plain failure with the log head.
    code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env={"FAKE_RUNNER_EXIT": "1", "FAKE_RUNNER_TEMPLATE": ""})
    check(code == 1 and "no readable summary.json" in summary(ev), (code, out))


def test_live_refused_other_installation(fx):
    # Messages of the machine-wide rule are "refused, rerun" as well.
    for name, message in (
            ("t-refused2", "An existing game of another installation is running, outside the permitted, separate parallel clones: "
                           "DBSIM.EXE PID 4 (D:\\x\\DBSIM.EXE, started 2026-10-11). It is left alone.\n"),
            ("t-refused3", "A game process whose path cannot be read is running and may belong to an installation: DBSIM.EXE PID 4 "
                           "(path not readable, started unknown). It is left alone; start the run again when it has exited.\n")):
        suites = [("a60", dict(ticks=4, common=3)),
                  ("c1s", dict(ticks=0, common=0, result="DIFFERENT", runner_log=message, with_jsonl=False))]
        env = live_template(fx, name, suites)
        code, out, ev = fx.run("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit", env=dict(env, FAKE_RUNNER_EXIT="1"))
        text = summary(ev)
        check(code == 1 and "suite c1s: refused, rerun" in text and "refused (rerun) 1" in text and "DIFFERENT" not in text.replace("DIFFERENT 0", ""), (code, text))


def test_overlap(fx):
    waits = {"FAKE_RUNNER_WAIT_BAZEL": "1"}
    env = dict(live_template(fx, "t-overlap", all_identical_suites()), **waits)
    # Default: sequential, the quick tier has run before the runner starts.
    code, out, ev = fx.run("-Live", env=env)
    runner = fx.runner_json()
    check(code == 0 and runner["BazelAtStart"] is True and runner["BazelDuringRun"] is True, (code, out, runner))
    # -Overlap: the runner starts first, the quick tier is called while it runs (the fake bazel waits for
    # the runner's start marker, see BAZEL_CMD).
    overlap = dict(waits, FAKE_BAZEL_WAIT_RUNNER="1", FAKE_RUNNER_STARTED=str(fx.logs / "runner-started"))
    env = dict(env, **overlap)
    code, out, ev = fx.run("-Live", "-Overlap", env=env)
    check(code == 0, out)
    runner = fx.runner_json()
    check(runner["BazelAtStart"] is False and runner["BazelDuringRun"] is True, runner)
    stages, data = stage_rows(ev)
    check(list(stages) == ["quick", "full", "signatures", "audit", "live", "evidence"], list(stages))
    check([stages[n]["result"] for n in ("quick", "full", "signatures", "audit", "live")] == ["PASSED", "SKIPPED", "PASSED", "PASSED", "PASSED"], stages)
    check(data["result"] == "PASSED", data)
    text = summary(ev)
    check("tests passed 3, failed 0" in text and "suites identical 3 of 3" in text and "DISCARDED" not in text, text)
    check(text.index("| quick |") < text.index("| signatures |") < text.index("| live |"), text)
    check("test //..." in fx.log("bazel.log"), fx.log("bazel.log"))
    check((ev / "quick.log").is_file() and (ev / "hook-counters.json").is_file(), "evidence")
    # With a given proxy the quick tier starts at once; the result is the same.
    manifest = fx.tmp / "overlap-prepared.json"
    manifest.write_text("{}")
    code, out, ev = fx.run("-Live", "-Overlap", "-PreparedProxy", str(manifest), env=env)
    check(code == 0 and fx.runner_json()["BazelAtStart"] is False, (code, out))
    # A failing quick tier discards a passing live result; the exit status comes from quick.
    write_lf(fx.bazel_out, BAZEL_FAIL)
    code, out, ev = fx.run("-Live", "-Overlap", env=dict(env, FAKE_BAZEL_CODE="3"))
    stages, data = stage_rows(ev)
    check(code == 1 and stages["quick"]["result"] == "FAILED" and stages["live"]["result"] == "DISCARDED", (code, stages))
    text = summary(ev)
    check("discarded, failed beside it: quick; was: suites identical 3 of 3" in text and "| live | DISCARDED |" in text, text)
    check("suite a60: identical" in text and (ev / "hook-counters.json").is_file(), text)
    check(stages["signatures"]["result"] == "PASSED" and stages["audit"]["result"] == "PASSED", "later stages still run")
    # A failing audit discards it as well; a live stage that itself failed stays FAILED.
    write_lf(fx.bazel_out, BAZEL_PASS)
    code, out, ev = fx.run("-Live", "-Overlap", env=dict(env, FAKE_AUDIT_PROBLEMS="1"))
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["audit"]["result"] == "FAILED" and stages["live"]["result"] == "DISCARDED", (code, stages))
    suites = [("a60", dict(ticks=4, common=3)), ("c1s", dict(ticks=5, common=2, result="DIFFERENT", runner_log="x\n"))]
    env2 = dict(live_template(fx, "t-overlap-diff", suites), **overlap)
    code, out, ev = fx.run("-Live", "-Overlap", env=dict(env2, FAKE_RUNNER_EXIT="1", FAKE_AUDIT_PROBLEMS="1"))
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "FAILED" and stages["audit"]["result"] == "FAILED", (code, stages))
    # A runner that dies before its summary: quick still completes and is reported.
    code, out, ev = fx.run("-Live", "-Overlap", env={"FAKE_RUNNER_EXIT": "1", "FAKE_RUNNER_TEMPLATE": ""})
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "FAILED" and stages["quick"]["result"] == "PASSED", (code, stages))
    # Refusals: nothing runs.
    for args, needle in ((("-Overlap",), "add -Live"), (("-Live", "-Overlap", "-SkipQuick"), "-SkipQuick")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists() and not fx.log("bazel.log") and not fx.log("runner.json"), (code, out))


def test_live_not_run_after_failure(fx):
    env = live_template(fx, "t-notrun", all_identical_suites())
    code, out, ev = fx.run("-Live", env=dict(env, FAKE_AUDIT_PROBLEMS="1"))
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["audit"]["result"] == "FAILED" and stages["live"]["result"] == "NOT RUN", (code, stages["live"]))
    check(not fx.log("runner.json") and not (ev / "live").exists(), "the runner must not start")
    check("| live | NOT RUN | - | an earlier stage failed |" in summary(ev), summary(ev))


def test_dirty_tree(fx):
    env = live_template(fx, "t-dirty", all_identical_suites())
    # A change outside the source-stamp paths does not matter.
    write_lf(fx.repo / "docs/reference-game/hooks.md", "changed\n")
    code, out, ev = fx.run("-Live", env=env)
    check(code == 0, out)
    git(fx.repo, "checkout", "--", "docs")
    # Modified tracked file under core/: -Live refused before any stage, the plain run still works.
    write_lf(fx.repo / "core/x.c", "changed\n")
    code, out, ev = fx.run("-Live", env=env)
    check(code == 2 and "source-stamp" in out and "core/x.c" in out and not ev.exists(), (code, out))
    check(not fx.log("bazel.log") and not fx.log("runner.json"), "nothing may run")
    code, out, ev = fx.run(env=env)
    check(code == 0, out)
    git(fx.repo, "checkout", "--", "core")
    # An untracked file under hook/ counts as well (the stamp hashes every file).
    write_lf(fx.repo / "hook/new.c", "new\n")
    code, out, ev = fx.run("-Live", env=env)
    check(code == 2 and "hook/new.c" in out and not ev.exists(), (code, out))
    (fx.repo / "hook/new.c").unlink()
    code, out, ev = fx.run("-Live", env=env)
    check(code == 0, out)


CLONES = ["timing-r111", "timing-r111-p1", "timing-r111-p2", "timing-r111-p4", "timing-r111-p5"]
LIVE_ONLY = ("-Live", "-SkipQuick", "-SkipSignatures", "-SkipAudit")


def test_runtime_own_default(fx):
    """The own cw3220.dll is the default runtime: previewed on every clone before any stage, built once
    after the proxy, deployed on every clone before the runner starts, removed after it; the summary names it."""
    env = live_template(fx, "t-runtime", all_identical_suites())
    code, out, ev = fx.run("-Live", env=env)
    check(code == 0, out)
    calls = fx.runtime_calls()
    previews = [["preview", c, "False"] for c in CLONES]
    applies = [["apply", c, "False"] for c in CLONES]
    restores = [["preview", c, "True"] for c in CLONES]   # each restore looks first; state own, so -Remove follows
    check(calls[:5] == previews and calls[5:10] == applies, calls)
    check([c for c in calls[10:] if c[0] == "remove"] == [["remove", c, "True"] for c in CLONES], calls)
    check([c for c in calls[10:] if c[0] == "preview"] == restores and len(calls) == 20, calls)
    check(fx.runtime_states() == {}, fx.runtime_states())   # every clone back to the original
    log = fx.log("runtime.log")
    built = ev / "runtime" / "cw3220.dll"
    check("build //rtl:cw3220.dll into %s" % (ev / "runtime") in log and "source=%s" % built in log, log)
    check(not built.exists(), "the built runtime is deleted after the run")
    import hashlib
    sha = hashlib.sha256(b"own runtime build").hexdigest().upper()
    text = summary(ev)
    check("- runtime: own cw3220.dll %s; deployed on 5 of 5 clone(s), restored on 5" % sha in text, text)
    check("runtime: own cw3220.dll %s (//rtl:cw3220.dll)" % sha in text and "runtime deployed on timing-r111-p4 (found original)" in text, text)
    check("runtime restored to the original CW3220.DLL on 5 of 5 clone(s)" in text and "; runtime own" in text, text)
    stages, data = stage_rows(ev)
    rt = data["runtime"]
    check(rt["runtime"] == "own" and rt["sha256"] == sha and rt["deployed"] == CLONES and rt["restored"] == CLONES and
          rt["not_restored"] == [] and rt["found"] == {c: "original" for c in CLONES} and rt["bwcc32"] == "refuse", rt)
    check(fx.runner_json()["ES2_BWCC32"] == "refuse", fx.runner_json())   # the default
    # A given own DLL is deployed as it is (no build); the runner's data root reaches the runtime script.
    given = fx.tmp / "given-cw3220.dll"
    given.write_bytes(b"given runtime")
    code, out, ev = fx.run(*LIVE_ONLY, "-RuntimeSource", str(given), "-OracleDataRoot", str(fx.tmp / "od"), env=env)
    log = fx.log("runtime.log")
    check(code == 0 and "build " not in log and "source=%s" % given in log and "data=%s" % (fx.tmp / "od") in log, (code, out, log))
    check(given.exists() and "(%s)" % given in summary(ev), summary(ev))
    # Without -Live there is no runtime line.
    code, out, ev = fx.run()
    check(code == 0 and "- runtime:" not in summary(ev) and stage_rows(ev)[1]["runtime"] is None, summary(ev))


def test_runtime_original(fx):
    """-Runtime original deploys nothing; a clone that still has the own runtime is refused before any stage."""
    env = live_template(fx, "t-runtime-orig", all_identical_suites())
    code, out, ev = fx.run(*LIVE_ONLY, "-Runtime", "original", env=env)
    check(code == 0, out)
    check(fx.runtime_calls() == [["preview", c, "False"] for c in CLONES], fx.runtime_calls())
    text = summary(ev)
    check("- runtime: original CW3220.DLL (nothing deployed)" in text and "; runtime original" in text, text)
    check("build " not in fx.log("runtime.log") and not (ev / "runtime").exists(), fx.log("runtime.log"))
    (fx.runtime_dir / "timing-r111-p2").write_text("own ABC")
    code, out, ev = fx.run(*LIVE_ONLY, "-Runtime", "original", env=env)
    check(code == 2 and "timing-r111-p2 has the own runtime deployed" in out and not ev.exists(), (code, out))
    check(not fx.log("runner.json"), "nothing may run")
    # -Runtime own takes it over: redeployed with this build, then the original again.
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 0 and fx.runtime_states() == {}, (code, out, fx.runtime_states()))
    check("runtime deployed on timing-r111-p2 (found own)" in summary(ev) and stage_rows(ev)[1]["runtime"]["found"]["timing-r111-p2"] == "own", summary(ev))


def test_runtime_refusals(fx):
    env = live_template(fx, "t-runtime-refuse", all_identical_suites())
    for args, needle in ((("-Live", "-Runtime", "cw3220"), "-Runtime is own or original"),
                         (("-Runtime", "own"), "-Runtime is a parameter of the live stage"),
                         (("-Bwcc32", "refuse"), "-Bwcc32 is a parameter of the live stage"),
                         (("-Live", "-Bwcc32", "maybe"), "-Bwcc32 is load or refuse"),
                         (("-Live", "-Runtime", "original", "-RuntimeSource", str(fx.fake / "bazel-out.txt")), "needs -Runtime own"),
                         (("-Live", "-RuntimeSource", str(fx.tmp / "absent.dll")), "own runtime not found")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists(), (args, code, out))
    # The preview refuses a clone (not a measurement clone, unexpected runtime files): exit 2, nothing changed.
    code, out, ev = fx.run("-Live", env=dict(env, FAKE_RUNTIME_FAIL="preview:timing-r111-p2"))
    check(code == 2 and "clone timing-r111-p2 refused by configure-runtime.ps1: fake refusal of preview" in out and not ev.exists(), (code, out))
    check([c[0] for c in fx.runtime_calls()] == ["preview"] * 3 and not fx.log("bazel.log") and not fx.log("runner.json"), fx.runtime_calls())
    # rtl/ belongs to the stamp of the own runtime: dirty rtl/ refuses -Runtime own, not -Runtime original.
    write_lf(fx.repo / "rtl/new.c", "x\n")
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 2 and "source-stamp" in out and "rtl/new.c" in out and not ev.exists(), (code, out))
    code, out, ev = fx.run(*LIVE_ONLY, "-Runtime", "original", env=env)
    check(code == 0, out)
    (fx.repo / "rtl/new.c").unlink()
    (fx.repo / "rtl").rmdir()


def test_runtime_restored_on_failure(fx):
    """The restore runs in a finally: after a failing runner, a runner that throws, and a deployment that fails
    half-way; a failed restore fails the stage and is named."""
    env = live_template(fx, "t-runtime-fail", all_identical_suites())
    # The runner dies before a summary: FAILED, every clone restored after it.
    code, out, ev = fx.run(*LIVE_ONLY, env={"FAKE_RUNNER_EXIT": "1", "FAKE_RUNNER_TEMPLATE": ""})
    check(code == 1 and fx.runtime_states() == {} and "no readable summary.json" in summary(ev), (code, out))
    check([c[1] for c in fx.runtime_calls() if c[0] == "remove"] == CLONES, fx.runtime_calls())
    code, out, ev = fx.run(*LIVE_ONLY, env=dict(env, FAKE_RUNNER_THROW="1"))
    check(code == 1 and fx.runtime_states() == {} and "runner failed: the runner broke" in (ev / "live.log").read_text(encoding="utf-8"), (code, out))
    # The third deployment is refused: the runner never starts, the two deployed clones and the refused one are
    # looked at again (the refused one is still original), clones 4 and 5 were never touched.
    code, out, ev = fx.run(*LIVE_ONLY, env=dict(env, FAKE_RUNTIME_FAIL="apply:timing-r111-p2"))
    stages, data = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "FAILED" and not fx.log("runner.json"), (code, stages["live"]))
    calls = fx.runtime_calls()
    check(calls[5:8] == [["apply", c, "False"] for c in CLONES[:3]], calls)
    check([c[1] for c in calls[8:] if c[0] == "remove"] == CLONES[:2] and len([c for c in calls[8:] if c[0] == "preview"]) == 3, calls)
    check(fx.runtime_states() == {} and data["runtime"]["restored"] == CLONES[:3], data["runtime"])
    check("error: fake refusal of apply on timing-r111-p2" in summary(ev), summary(ev))
    # A deployment whose hash is not the build's is a failure too.
    code, out, ev = fx.run(*LIVE_ONLY, env=dict(env, FAKE_RUNTIME_WRONG="timing-r111"))
    check(code == 1 and "did not deploy the own runtime on timing-r111" in summary(ev) and fx.runtime_states() == {}, (code, summary(ev)))
    # A restore that fails: the run is identical but the stage fails and names the clone.
    code, out, ev = fx.run(*LIVE_ONLY, env=dict(env, FAKE_RUNTIME_FAIL="remove:timing-r111-p4"))
    stages, data = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "FAILED", (code, stages["live"]))
    text = summary(ev)
    check("runtime NOT restored on timing-r111-p4: fake refusal of remove" in text and "runtime NOT restored on 1 clone(s)" in text, text)
    check("restored on 4, NOT restored on timing-r111-p4" in text and data["runtime"]["not_restored"] == ["timing-r111-p4"], text)
    check(fx.runtime_states() == {"timing-r111-p4": "own"}, fx.runtime_states())
    (fx.runtime_dir / "timing-r111-p4").unlink()


def test_bwcc32_switch(fx):
    """refuse (the default) reaches the runner as ES2_BWCC32=refuse and is named in the summary;
    -Bwcc32 load clears the variable for the run."""
    env = live_template(fx, "t-bwcc", all_identical_suites())
    for given in ((), ("-Bwcc32", "refuse")):
        code, out, ev = fx.run(*LIVE_ONLY, *given, env=env)
        check(code == 0 and fx.runner_json()["ES2_BWCC32"] == "refuse", (given, code, out, fx.runner_json()))
        text = summary(ev)
        check("- BWCC32: load answered with failure in DBSIM (-Bwcc32 refuse, the default)" in text and 'LoadLibraryA("BWCC32.DLL") answered with failure' in text, text)
        check(stage_rows(ev)[1]["runtime"]["bwcc32"] == "refuse")
    # -Bwcc32 load: a variable left in the environment does not leak into the run.
    code, out, ev = fx.run(*LIVE_ONLY, "-Bwcc32", "load", env=dict(env, ES2_BWCC32="refuse"))
    text = summary(ev)
    check(code == 0 and fx.runner_json()["ES2_BWCC32"] is None and "- BWCC32: loaded in DBSIM (-Bwcc32 load)" in text, fx.runner_json())
    check(stage_rows(ev)[1]["runtime"]["bwcc32"] == "load")


def test_new_fail_switch(fx):
    """-NewFail <spec> reaches the runner as ES2_NEW_FAIL=<spec> and ES2_NEW_FAIL_LOG=<evidence dir>/new-fail-{suite}.ndjson,
    is named in the summary, replaces a value left in the environment, is restored afterwards, and without the
    parameter the script touches neither."""
    env = live_template(fx, "t-newfail", all_identical_suites())
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "46162B:1", env=env)
    run = fx.runner_json()
    log = str(ev / "new-fail-{suite}.ndjson")
    check(code == 0 and run["ES2_NEW_FAIL"] == "46162b:1" and run["ES2_NEW_FAIL_LOG"] == log, (code, out, run))
    text = summary(ev)
    check("ES2_NEW_FAIL=46162b:1 (-NewFail; proxy/new_fail_switch.h), ES2_NEW_FAIL_LOG=" + log in text, text)
    check("- new fail: ES2_NEW_FAIL=46162b:1, logs " + log + " (-NewFail)" in text and "new fail logs: none written" in text, text)
    stages, data = stage_rows(ev)
    check(data["new_fail"]["spec"] == "46162b:1" and data["new_fail"]["log"] == log, data)
    # The parameter wins over a value already in the environment, and the variables are back for later stages.
    leftover = dict(env, ES2_NEW_FAIL="any:9", ES2_NEW_FAIL_LOG=str(fx.tmp / "left-over.ndjson"))
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "any:0", env=leftover)
    run = fx.runner_json()
    check(code == 0 and run["ES2_NEW_FAIL"] == "any:0" and run["ES2_NEW_FAIL_LOG"] == str(ev / "new-fail-{suite}.ndjson"), (code, out, run))
    after = fx.log("py.log.env")
    check("ES2_NEW_FAIL=any:9;ES2_NEW_FAIL_LOG=" + str(fx.tmp / "left-over.ndjson") in after, after)
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "46162b:1", env=dict(env, FAKE_NEW_FAIL_WRITE="s1"))
    check(code == 0 and (ev / "new-fail-s1.ndjson").exists() and re.search(r"new fail logs: new-fail-s1\.ndjson \(\d+ bytes\)", summary(ev)), (code, out, summary(ev)))
    # Without the parameter the environment passes through unchanged, and an inherited value is noted.
    code, out, ev = fx.run(*LIVE_ONLY, env=leftover)
    run = fx.runner_json()
    check(code == 0 and run["ES2_NEW_FAIL"] == "any:9" and run["ES2_NEW_FAIL_LOG"] == str(fx.tmp / "left-over.ndjson"), (code, out, run))
    text = summary(ev)
    check("ES2_NEW_FAIL=any:9 is set in the environment (not by -NewFail)" in text and "new_fail" not in (ev / "summary.json").read_text(encoding="utf-8-sig"), text)
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    run = fx.runner_json()
    check(code == 0 and run["ES2_NEW_FAIL"] is None and run["ES2_NEW_FAIL_LOG"] is None and "new fail" not in summary(ev).lower(), (code, out, run))
    # Spec forms: case-insensitive, optional 0x, n up to 1000000.
    for spec in ("ANY:0", "0x4779E5:1000000", "480fbc:12"):
        code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", spec, env=env)
        check(code == 0 and fx.runner_json()["ES2_NEW_FAIL"] == spec.lower(), (spec, code, out))


def test_new_fail_replaced_site(fx):
    """A -NewFail site inside a function whose hook is replaced in the run warns (console, live details, summary
    header, summary.json); the same site with that hook original, a site in an unhooked function and any:<n> do not; a
    site that is none of the six call sites warns that nothing is injected."""
    env = live_template(fx, "t-newfail-site", all_identical_suites())
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "0x46162B:1", env=env)
    text = summary(ev)
    data = stage_rows(ev)[1]
    warning = data["new_fail"]["warning"]
    check(code == 0 and "lies in 0x461608 (alpha), replaced in this run" in warning and "set alpha=original with -HookModes" in warning, (code, out, data))
    check("check.ps1: warning: -NewFail site 0x46162b lies in 0x461608" in out, out)
    check(("- new fail: ES2_NEW_FAIL=0x46162b:1, logs %s (-NewFail); WARNING: %s" % (ev / "new-fail-{suite}.ndjson", warning)) in text, text)
    check(("WARNING: " + warning) in data["stages"][[s["name"] for s in data["stages"]].index("live")]["details"], data)
    # beta contains 480b23; compare is not replaced.
    modes = fx.tmp / "newfail-modes.txt"
    write_lf(modes, "beta=compare\n")
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "480b23:2", "-HookModes", str(modes), env=env)
    check(code == 0 and not stage_rows(ev)[1]["new_fail"]["warning"] and "WARNING" not in summary(ev) and "warning:" not in out, (code, out))
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "480b23:2", env=env)
    check(code == 0 and "lies in 0x480af0 (beta)" in stage_rows(ev)[1]["new_fail"]["warning"], (code, out))
    write_lf(modes, "# the containing hook original\nalpha=original\n")
    for args in (("-NewFail", "46162b:1", "-HookModes", str(modes)), ("-NewFail", "4779e5:1"), ("-NewFail", "any:1"), ("-NewFail", "ANY:0")):
        code, out, ev = fx.run(*LIVE_ONLY, *args, env=env)
        check(code == 0 and stage_rows(ev)[1]["new_fail"]["warning"] == "" and "WARNING" not in summary(ev) and "warning:" not in out, (args, code, out))
        check(fx.runner_json()["ES2_NEW_FAIL"] == args[1].lower(), fx.runner_json())
    code, out, ev = fx.run(*LIVE_ONLY, "-NewFail", "123abc:1", env=env)
    warning = stage_rows(ev)[1]["new_fail"]["warning"]
    check(code == 0 and "0x123abc is not one of the call sites of 0x479cfc" in warning and "nothing is injected" in warning and "WARNING" in summary(ev), (code, out, warning))


def test_suite_selection(fx):
    """-Suite limits the runner (and the own-sim replays) to registered suites; unknown or repeated names, an
    empty list and -Suite without -Live are refused before anything is created; without -Suite every suite runs."""
    env = live_template(fx, "t-suite", [s for s in all_identical_suites() if s[0] != "c4"])
    code, out, ev = fx.run(*LIVE_ONLY, "-Suite", "s-short, s-long", env=env)
    run = fx.runner_json()
    stages, data = stage_rows(ev)
    check(code == 0 and run["Suite"] == ["s-short", "s-long"] and data["suites"] == ["s-short", "s-long"], (code, out, run))
    check("suites: s-short, s-long (-Suite; not the whole registry)" in stages["live"]["details"] and "- suites: s-short, s-long (-Suite)" in summary(ev), summary(ev))
    # The counters and the verdict follow the runner's results.
    check("suites identical 2 of 2" in stages["live"]["numbers"] and sorted(json.loads((ev / "hook-counters.json").read_text(encoding="utf-8"))["suites"]) == ["a60", "c1s"], stages["live"])
    # Default: no -Suite reaches the runner, every suite of the registry.
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 0 and fx.runner_json()["Suite"] == [] and stage_rows(ev)[1]["suites"] == "all" and "-Suite" not in summary(ev), (code, out))
    # Own-sim replays only the selected suites.
    reports = dict(x1=dict(exit=2, errors=0, end="end x1"), x3=dict(exit=0, errors=0, end="end x3"))
    chain = own_verdict(fx, "suite-sim", sim="own", shell="gui", shell_reports=reports)["FAKE_OWN_VERDICT"]
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", "-Suite", "s-mid", env=dict(env, FAKE_OWN_VERDICT_SIM=chain))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 0 and [r["suite"] for r in sim_calls(fx, "replay_suite.py")] == ["s-mid"] and own["numbers"].startswith("replay of 1 suites: IDENTICAL 1"), (code, out, own))
    for args, needle in ((("-Live", "-Suite", "s-mid,nope"), "-Suite: not in"), (("-Live", "-Suite", "s-mid,s-mid"), "names a suite twice"),
                         (("-Live", "-Suite", ","), "names no suite"), (("-Suite", "s-mid"), "-Suite is a parameter of the live stage")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists() and not (fx.logs / "runner.json").exists(), (args, code, out))
    check("nope (registered: s-long, s-mid, s-short)" in fx.run("-Live", "-Suite", "nope", env=env)[1], "the refusal lists the registry")


def test_new_fail_refusals(fx):
    """A bad spec, -NewFail without -Live and with the own-shell or own-sim chains are refused (exit 2, nothing created)."""
    env = live_template(fx, "t-newfail-refuse", all_identical_suites())
    for args, needle in ((("-NewFail", "any:1"), "-NewFail is a parameter of the live stage"),
                         (("-Live", "-NewFail", ""), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "46162b"), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "46162b:"), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "xyz:1"), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "any:-1"), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "any:1000001"), "n 0..1000000"),
                         (("-Live", "-NewFail", "123456789:1"), "-NewFail is <hex site>:<n>"),
                         (("-Live", "-NewFail", "46162b:1", "-OwnShell"), "drop -OwnShell/-OwnSim"),
                         (("-Live", "-NewFail", "46162b:1", "-OwnSim"), "drop -OwnShell/-OwnSim")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists() and not (fx.logs / "runner.json").exists(), (args, code, out))


def test_runtime_real_script(fx):
    """check.ps1 with the real configure-runtime.ps1 (in its session) on a synthetic clone: the own DLL
    is in place while the runner runs and the original is back afterwards."""
    if not REAL_RUNTIME:
        print("skip test_runtime_real_script: no configure-runtime.ps1/cw3220.dll given")
        return
    import hashlib
    data = fx.tmp / "real-data"
    clone = data / "installs" / "timing-r111"
    clone.mkdir(parents=True)
    hashes = []
    for name in ("ES.EXE", "VSHELL.EXE", "DBSIM.EXE", "CW3220.DLL", "BWCC32.DLL"):
        (clone / name).write_bytes(("stand-in for %s\n" % name).encode() * 40)
        hashes.append({"file": name, "sha256": hashlib.sha256((clone / name).read_bytes()).hexdigest().upper()})
    original = (clone / "CW3220.DLL").read_bytes()
    manifest = {"destination": os.path.abspath(str(clone)), "measurement_clone_of": os.path.abspath(str(data / "installs" / "source")),
                "hashes": hashes}
    (clone / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    env = dict(live_template(fx, "t-real-runtime", all_identical_suites()), FAKE_RUNNER_CLONE_DLL=str(clone / "CW3220.DLL"))
    args = (*LIVE_ONLY, "-InstallName", "timing-r111", "-OracleDataRoot", str(data), "-RuntimeScript", str(REAL_RUNTIME),
            "-RuntimeSource", str(OWN_DLL))
    code, out, ev = fx.run(*args, env=env)
    own = hashlib.sha256(OWN_DLL.read_bytes()).hexdigest().upper()
    check(code == 0, out)
    check(fx.runner_json()["CloneDll"] == own, fx.runner_json())
    check((clone / "CW3220.DLL").read_bytes() == original and not (clone / "CW3220-ORIGINAL.DLL").exists() and
          not (clone / "oracle-runtime-manifest.json").exists(), sorted(p.name for p in clone.iterdir()))
    text = summary(ev)
    check("- runtime: own cw3220.dll %s; deployed on 1 of 1 clone(s), restored on 1" % own in text, text)
    # A clone that is not a separate measurement clone is refused by the real preview: nothing runs.
    manifest["measurement_clone_of"] = manifest["destination"]
    (clone / "installation-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    code, out, ev = fx.run(*args, env=env)
    check(code == 2 and "refused by configure-runtime.ps1: The own runtime is deployed only into a separate measurement clone" in out
          and not ev.exists(), (code, out))
    check((clone / "CW3220.DLL").read_bytes() == original)


def own_verdict(fx, name, **changes):
    verdict = dict(schema=1, suite="c1_01-success", verdict="PASSED", dbsim="identical", launcher_sequence="expected", launch_files="pinned",
                   timed_out=False, problems=[], chain_verify=dict(bazel_exit=0, results_steps=1, preparations=2, preparations_equal=2),
                   comparison=dict(identical=True, common_ticks=6198, golden_ticks=6198, run_ticks=6198, golden_watch_changes=16305,
                                   run_watch_changes=16305, problems=[]))
    verdict.update(changes)
    path = fx.tmp / (name + "-verdict.json")
    write_lf(path, json.dumps(verdict))
    return {"FAKE_OWN_VERDICT": str(path)}


def own_json(fx, shell="text"):
    path = fx.logs / ("own-shell.json" if shell == "text" else "own-shell-%s.json" % shell)
    return json.loads(path.read_text(encoding="utf-8-sig")) if path.exists() else None


TEXT_NUM = ("DBSIM identical to the c1_01-success golden; chain verify 2 of 2 preparations equal, 1 results step(s); "
            "launcher sequence expected; launch files pinned")


def test_own_shell(fx):
    """-OwnShell runs start-own-menu.ps1 -Unattended after the suites, inside the live stage (prepared proxy still
    there, the hooks of the run, the clone still on the deployed runtime), and reports it as the stage own-shell."""
    env = dict(live_template(fx, "t-own", all_identical_suites()), **own_verdict(fx, "pass"))
    # Without -OwnShell there is no such stage and the script is not started.
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 0 and "own-shell" not in stage_rows(ev)[0] and own_json(fx) is None, (code, out))
    code, out, ev = fx.run("-Live", "-OwnShell", "-OwnShellShell", "text", "-BazelOutputBase", str(fx.tmp / "base"), env=env)
    check(code == 0, out)
    stages, data = stage_rows(ev)
    check(list(stages) == ["quick", "full", "signatures", "audit", "live", "own-shell", "evidence"], list(stages))
    own = stages["own-shell"]
    check(own["result"] == "PASSED" and own["numbers"] == ("text PASSED: DBSIM identical to the c1_01-success golden; chain verify 2 of 2 preparations "
          "equal, 1 results step(s); launcher sequence expected; launch files pinned"), own)
    check(own_json(fx, "gui") is None and own_json(fx)["Shell"] == "text", "-OwnShellShell text runs the text chain only")
    check(any("text: clone: timing-r111; suite c1_01-success; runtime: own cw3220.dll" in d for d in own["details"]) and
          any("common ticks 6198 (golden 6198, run 6198)" in d for d in own["details"]), own["details"])
    mine = own_json(fx)
    runner = fx.runner_json()
    check(mine["Unattended"] and mine["InstallName"] == "timing-r111" and mine["PreparedProxy"] == runner["PreparedProxy"] and mine["ProxyExisted"], mine)
    check(mine["Hooks"] == runner["Hooks"] and mine["SessionDirectory"] == str(ev / "own-shell") and mine["RunnerDone"] and mine["Runtime"] == "own", mine)
    check(mine["BazelOutputBase"] == str(fx.tmp / "base") and mine["Python"] == str(fx.fake / "python.cmd") and not mine["DataRoot"], mine)
    check(fx.runtime_states() == {}, "the runtime is restored after the own-shell run")
    check((ev / "own-shell.log").is_file() and "| own-shell | PASSED |" in summary(ev), summary(ev))
    # Another clone of the run, the runner's data root.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", "-OwnShellShell", "text", "-OwnShellInstall", "timing-r111-p2", "-OracleDataRoot", str(fx.tmp / "od"), env=env)
    mine = own_json(fx)
    check(code == 0 and mine["InstallName"] == "timing-r111-p2" and mine["DataRoot"] == str(fx.tmp / "od") and mine["Runtime"] == "own", (code, out, mine))
    # A failed verdict fails the stage and the run; its problems are listed.
    failed = own_verdict(fx, "fail", verdict="FAILED", dbsim="DIFFERENT", problems=["DBSIM differs from the c1_01-success golden (x)"],
                         chain_verify="skipped (-NoVerify)")
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", "-OwnShellShell", "text", env=dict(env, FAKE_OWN_EXIT="6", **failed))
    stages, data = stage_rows(ev)
    own = stages["own-shell"]
    check(code == 1 and data["result"] == "FAILED" and stages["live"]["result"] == "PASSED" and own["result"] == "FAILED", (code, stages))
    check(own["numbers"].startswith("text FAILED: DBSIM DIFFERENT to the c1_01-success golden; chain verify skipped (-NoVerify)") and
          own["numbers"].endswith("start-own-menu.ps1 exit 6") and "text: problem: DBSIM differs from the c1_01-success golden (x)" in own["details"], own)
    # No verdict at all (the script refused or failed early).
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", "-OwnShellShell", "text", env=dict(env, FAKE_OWN_EXIT="1", FAKE_OWN_VERDICT=""))
    own = stage_rows(ev)[0]["own-shell"]
    check(code == 1 and own["result"] == "FAILED" and own["numbers"] == "text FAILED: start-own-menu.ps1 exit 1, no unattended.json"
          and "text: own shell mock: done (text)" in own["details"], own)
    # A verdict that says PASSED with a nonzero exit (restore deferred) is no pass.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", env=dict(env, FAKE_OWN_EXIT="2"))
    own = stage_rows(ev)[0]["own-shell"]
    check(code == 1 and own["result"] == "FAILED" and any(d.startswith("text: restore deferred") for d in own["details"]), own)
    # The live stage never reached its runner: NOT RUN.
    code, out, ev = fx.run("-Live", "-OwnShell", env=dict(env, FAKE_AUDIT_PROBLEMS="1"))
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "NOT RUN" and stages["own-shell"]["result"] == "NOT RUN" and own_json(fx) is None, stages)
    # -Overlap: a failing quick tier discards a passing own-shell result like the live one.
    write_lf(fx.bazel_out, BAZEL_FAIL)
    code, out, ev = fx.run("-Live", "-Overlap", "-OwnShell", env=dict(env, FAKE_BAZEL_CODE="3"))
    write_lf(fx.bazel_out, BAZEL_PASS)
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "DISCARDED" and stages["own-shell"]["result"] == "DISCARDED", stages)
    check(list(stages) == ["quick", "full", "signatures", "audit", "live", "own-shell", "evidence"], list(stages))
    # Refusals: nothing runs.
    for args, needle in ((("-OwnShell",), "-OwnShell is a parameter of the live stage"),
                         (("-Live", "-OwnShellInstall", "timing-r111"), "add -OwnShell"),
                         (("-Live", "-OwnShell", "-OwnShellInstall", "timing-r111-p3"), "is not one of the clones of the live stage"),
                         (("-OwnShellShell", "gui"), "-OwnShellShell is a parameter of the live stage"),
                         (("-Live", "-OwnShellShell", "gui"), "add -OwnShell"),
                         (("-Live", "-OwnShell", "-OwnShellShell", "vshell"), "-OwnShellShell is text, gui or both")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists() and own_json(fx) is None, (args, code, out))

    # ---- -OwnShellShell gui and the default both: one stage, a sub-verdict per shell ---------------------------------
    gui_ok = own_verdict(fx, "gui-pass", shell="gui", shell_reports=dict(x1=dict(exit=2, errors=0, end="end x1"), x3=dict(exit=0, errors=0, end="end x3")))
    gui_env = dict(env, FAKE_OWN_VERDICT_GUI=gui_ok["FAKE_OWN_VERDICT"])
    gui_numbers = ("gui PASSED: " + TEXT_NUM + "; headless X1 exit 2, X3 exit 0, 0 script error(s)")
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", env=gui_env)
    own = stage_rows(ev)[0]["own-shell"]
    check(code == 0 and own["result"] == "PASSED" and own["numbers"] == "text PASSED: " + TEXT_NUM + " / " + gui_numbers, own)
    check((ev / "own-shell.log").is_file() and (ev / "own-shell-gui.log").is_file(), own)
    text_run, gui_run = own_json(fx), own_json(fx, "gui")
    check(text_run["Shell"] == "text" and gui_run["Shell"] == "gui" and gui_run["SessionDirectory"] == str(ev / "own-shell-gui")
          and text_run["SessionDirectory"] == str(ev / "own-shell") and gui_run["PreparedProxy"] == text_run["PreparedProxy"]
          and gui_run["ProxyExisted"] and gui_run["Runtime"] == "own" and gui_run["Hooks"] == text_run["Hooks"], (text_run, gui_run))
    check(any(d.startswith("gui: clone: timing-r111") for d in own["details"]) and "| own-shell | PASSED |" in summary(ev), own)
    check(fx.runtime_states() == {}, "the runtime is restored after both own-shell runs")
    # gui only.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", "-OwnShellShell", "gui", env=gui_env)
    own = stage_rows(ev)[0]["own-shell"]
    check(code == 0 and own["numbers"] == gui_numbers and own_json(fx) is None and own_json(fx, "gui")["Shell"] == "gui", own)
    # The text chain passes, the gui chain fails: the stage fails, each sub-verdict is named.
    gui_bad = own_verdict(fx, "gui-fail", verdict="FAILED", shell="gui", problems=["headless report X3: no end line (x)"],
                          shell_reports=dict(x1=dict(exit=2, errors=0, end="end x1"), x3=dict(exit=None, errors=None, end=None)))
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", env=dict(env, FAKE_OWN_VERDICT_GUI=gui_bad["FAKE_OWN_VERDICT"], FAKE_OWN_EXIT_GUI="6"))
    stages, data = stage_rows(ev)
    own = stages["own-shell"]
    check(code == 1 and data["result"] == "FAILED" and own["result"] == "FAILED" and own["numbers"].startswith("text PASSED: ")
          and " / gui FAILED: " in own["numbers"] and own["numbers"].endswith("start-own-menu.ps1 exit 6")
          and "gui: problem: headless report X3: no end line (x)" in own["details"], own)


def sim_calls(fx, script):
    text = fx.logs / "sim.jsonl"
    rows = [json.loads(l) for l in text.read_text(encoding="utf-8").splitlines() if l.strip()] if text.exists() else []
    return [r for r in rows if r["script"] == script]


SIM_CHAIN = ("chain PASSED: DBSIM (es2sim) identical to the c1_01-success golden; launcher sequence expected; launch files pinned; "
             "headless X1 exit 2, X3 exit 0")


def test_own_sim(fx):
    """-OwnSim runs, after the live stage, every registry suite through replay_suite.py --frames with the built
    es2sim, frames_suite.py --capture on each identical suite, and start-own-menu.ps1 -Unattended -Shell gui -Sim own."""
    reports = dict(x1=dict(exit=2, errors=0, end="end x1"), x3=dict(exit=0, errors=0, end="end x3"))
    chain = own_verdict(fx, "sim-pass", sim="own", shell="gui", shell_reports=reports)["FAKE_OWN_VERDICT"]
    env = dict(live_template(fx, "t-sim", all_identical_suites()), FAKE_OWN_VERDICT_SIM=chain)
    # Without -OwnSim nothing of it runs.
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 0 and "own-sim" not in stage_rows(ev)[0] and not sim_calls(fx, "replay_suite.py") and own_json(fx, "sim") is None, (code, out))
    code, out, ev = fx.run("-Live", "-OwnSim", "-BazelOutputBase", str(fx.tmp / "base"), env=env)
    check(code == 0, out)
    stages, data = stage_rows(ev)
    check(list(stages) == ["quick", "full", "signatures", "audit", "live", "own-sim", "evidence"], list(stages))
    own = stages["own-sim"]
    check(own["result"] == "PASSED" and own["numbers"] == "replay of 3 suites: IDENTICAL 3; frames: PASS 3; " + SIM_CHAIN, own)
    replays, frames = sim_calls(fx, "replay_suite.py"), sim_calls(fx, "frames_suite.py")
    check(sorted(r["suite"] for r in replays) == SIM_SUITES and sorted(r["suite"] for r in frames) == SIM_SUITES, (replays, frames))
    bin_dir = ev / "own-sim" / "bin"
    for r in replays:
        o = r["opts"]
        check(o["--sim"] == str(bin_dir / "es2sim_window.exe") and r["sim_exists"] and r["sdl_beside"] and r["flags"] == ["--frames"], r)
        check(o["--registry"] == str(fx.repo / "tools/reference-game/oracle-suites.json").replace("/", "\\")
              and o["--oracle-script"].endswith("oracle_regression.py") and o["--output-dir"] == str(ev / "own-sim" / "replay"), r)
        check(r["data_root"] == str(fx.tmp / "data") and not r["frames_reference"] and not r["oracle_dir"] and r["concurrent"] <= 5, r)
    for r in frames:
        check(r["opts"]["--capture"] == str(ev / "own-sim" / "replay" / r["suite"] / "frames.ndjson") and r["capture_exists"]
              and r["opts"]["--frames-script"].endswith("dbsim_frames.py"), r)
    # The binaries go, the hashes stay; passing suites keep result.json but not their raw logs.
    check(bin_dir.is_dir() and not list(bin_dir.iterdir()) and any(d.startswith("es2sim: es2sim_window.exe ") for d in own["details"]), own["details"])
    for name in SIM_SUITES:
        d = ev / "own-sim" / "replay" / name
        check((d / "result.json").is_file() and not (d / "tick-log.jsonl").exists() and not (d / "frames.ndjson").exists(), name)
        check((ev / "own-sim" / "replay-logs" / (name + ".log")).is_file() and (ev / "own-sim" / "frames-logs" / (name + ".log")).is_file(), name)
    check("build //sim:es2sim_window into " + str(bin_dir) in fx.log("runtime.log") and "build //sim:sdl_runtime" in fx.log("runtime.log"), fx.log("runtime.log"))
    mine = own_json(fx, "sim")
    check(mine and mine["Unattended"] and mine["Shell"] == "gui" and mine["Sim"] == "own" and mine["InstallName"] == "timing-r111"
          and mine["SessionDirectory"] == str(ev / "own-sim-chain") and not mine["PreparedProxy"] and not mine["Hooks"], mine)
    check(mine["SimExe"] == str(bin_dir / "es2sim_window.exe") and mine["SimExeExisted"] and mine["SimSdl"] == str(bin_dir / "SDL3.dll"), mine)
    check(mine["RunnerDone"] and mine["Runtime"] == "original" and mine["BazelOutputBase"] == str(fx.tmp / "base"), mine)
    check((ev / "own-sim-chain.log").is_file() and "| own-sim | PASSED |" in summary(ev) and fx.runtime_states() == {}, summary(ev))
    # With -OwnShell as well: own-shell, then own-sim.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnShell", "-OwnShellShell", "text", "-OwnSim", env=dict(env, **own_verdict(fx, "sim-text")))
    check(code == 0 and list(stage_rows(ev)[0]) == ["quick", "full", "signatures", "audit", "live", "own-shell", "own-sim", "evidence"], (code, out))
    # One suite at a time, the chain on another clone, a frame reference and the runner's data root.
    refs = fx.tmp / "frame-refs"
    refs.mkdir(exist_ok=True)
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", "-OwnSimJobs", "1", "-OwnSimInstall", "timing-r111-p4", "-OwnSimFramesReference", str(refs),
                           "-OracleDataRoot", str(fx.tmp / "od"), env=env)
    replays = sim_calls(fx, "replay_suite.py")
    check(code == 0 and all(r["concurrent"] == 1 for r in replays) and len(replays) == 3, (code, out, replays))
    check(all(r["frames_reference"] == str(refs) and r["oracle_dir"] == str(fx.tmp / "od" / "oracle") for r in sim_calls(fx, "frames_suite.py") + replays), replays)
    mine = own_json(fx, "sim")
    check(mine["InstallName"] == "timing-r111-p4" and mine["DataRoot"] == str(fx.tmp / "od"), mine)
    check(any("suites: 1 at a time" in d and str(refs) in d for d in stage_rows(ev)[0]["own-sim"]["details"]), "details name jobs and references")
    # A different suite fails the stage; its raw tick log stays, its frames are not compared.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_REPLAY=json.dumps({"s-mid": "DIFFERENT"})))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and own["numbers"].startswith("replay of 3 suites: DIFFERENT 1, IDENTICAL 2; frames: PASS 2; chain PASSED"), own)
    check("replay s-mid: replay s-mid: DIFFERENT (fake)" in own["details"] and (ev / "own-sim/replay/s-mid/tick-log.jsonl").is_file()
          and sorted(r["suite"] for r in sim_calls(fx, "frames_suite.py")) == ["s-long", "s-short"], own)
    # A frame compare that fails, and an identical suite without a capture.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_FRAMES=json.dumps({"s-long": "FAIL"}), FAKE_NO_CAPTURE="s-short"))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and "; frames: FAIL 1, no 1, PASS 1; " in own["numbers"].replace("no capture", "no"), own)
    check("frames s-long: frames s-long: FAIL" in own["details"] and (ev / "own-sim/replay/s-long/frames.ndjson").is_file(), own["details"])
    # A skipped suite (no golden) does not fail; all skipped is no pass.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_REPLAY=json.dumps({"s-short": "SKIP"})))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 0 and own["numbers"].startswith("replay of 3 suites: IDENTICAL 2, SKIP 1; frames: PASS 2; chain PASSED"), own)
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_REPLAY=json.dumps({n: "SKIP" for n in SIM_SUITES})))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and own["numbers"].startswith("replay of 3 suites: SKIP 3; frames: none compared; no suite ran; chain PASSED"), own)
    # The chain: a failed verdict, no verdict, a verdict of the DBSIM chain.
    bad = own_verdict(fx, "sim-fail", sim="own", verdict="FAILED", launcher_sequence="DIFFERENT", problems=["launcher events differ (x)"])["FAKE_OWN_VERDICT"]
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_OWN_VERDICT_SIM=bad, FAKE_OWN_EXIT_SIM="6"))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and "chain FAILED: DBSIM (es2sim) identical to the c1_01-success golden; launcher sequence DIFFERENT" in own["numbers"]
          and own["numbers"].endswith("start-own-menu.ps1 exit 6") and "chain: problem: launcher events differ (x)" in own["details"], own)
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_OWN_VERDICT_SIM="", FAKE_OWN_EXIT_SIM="1"))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["numbers"].endswith("chain FAILED: start-own-menu.ps1 exit 1, no unattended.json") and "chain: own shell mock: done (gui)" in own["details"], own)
    dbsim = own_verdict(fx, "sim-dbsim", sim="dbsim", shell="gui", shell_reports=reports)["FAKE_OWN_VERDICT"]
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_OWN_VERDICT_SIM=dbsim))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and "chain: problem: the verdict names sim 'dbsim', not own" in own["details"], own)
    # es2sim not built: the stage fails before any suite.
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=dict(env, FAKE_SIM_BUILD_FAIL="1"))
    own = stage_rows(ev)[0]["own-sim"]
    check(code == 1 and own["result"] == "FAILED" and any("es2sim was not built" in d for d in own["details"])
          and not sim_calls(fx, "replay_suite.py") and own_json(fx, "sim") is None, own)
    # The live stage did not run: NOT RUN.
    code, out, ev = fx.run("-Live", "-OwnSim", env=dict(env, FAKE_AUDIT_PROBLEMS="1"))
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "NOT RUN" and stages["own-sim"]["result"] == "NOT RUN" and not sim_calls(fx, "replay_suite.py"), stages)
    # -Overlap: a failing quick tier discards a passing own-sim result like the live one.
    write_lf(fx.bazel_out, BAZEL_FAIL)
    code, out, ev = fx.run("-Live", "-Overlap", "-OwnSim", env=dict(env, FAKE_BAZEL_CODE="3"))
    write_lf(fx.bazel_out, BAZEL_PASS)
    stages, _ = stage_rows(ev)
    check(code == 1 and stages["live"]["result"] == "DISCARDED" and stages["own-sim"]["result"] == "DISCARDED", stages)
    check(list(stages) == ["quick", "full", "signatures", "audit", "live", "own-sim", "evidence"], list(stages))
    # sim/ is a source-stamp path with -OwnSim only.
    write_lf(fx.repo / "sim/src/main.c", "changed\n")
    code, out, ev = fx.run(*LIVE_ONLY, env=env)
    check(code == 0, out)
    code, out, ev = fx.run(*LIVE_ONLY, "-OwnSim", env=env)
    check(code == 2 and "sim/src/main.c" in out and not ev.exists(), (code, out))
    git(fx.repo, "checkout", "--", "sim")
    # Refusals: nothing runs.
    for args, needle in ((("-OwnSim",), "-OwnSim is a parameter of the live stage"),
                         (("-OwnSimJobs", "2"), "-OwnSimJobs is a parameter of the live stage"),
                         (("-Live", "-OwnSimInstall", "timing-r111"), "-OwnSimInstall belongs to the own-sim stage: add -OwnSim"),
                         (("-Live", "-OwnSimFramesReference", str(refs)), "add -OwnSim"),
                         (("-Live", "-OwnSim", "-OwnSimInstall", "timing-r111-p3"), "is not one of the clones of the live stage"),
                         (("-Live", "-OwnSim", "-OwnSimJobs", "0"), "-OwnSimJobs takes 1 to 16"),
                         (("-Live", "-OwnSim", "-OwnSimFramesReference", str(fx.tmp / "no-refs")), "frame reference directory not found")):
        code, out, ev = fx.run(*args, env=env)
        check(code == 2 and needle in out and not ev.exists() and not sim_calls(fx, "replay_suite.py"), (args, code, out))


def test_counters_helper(fx):
    live = fx.tmp / "helper-run"
    make_live(live, [("good", dict(ticks=4, common=4, exit_scale=50, skipped=1)), ("nolog", dict(ticks=2, common=2, with_jsonl=False)),
                     ("short", dict(ticks=2, common=5))])
    out_path = fx.tmp / "helper-counters.json"

    def helper(*extra):
        r = subprocess.run([sys.executable, "-B", str(COUNTERS), str(live), "--output", str(out_path), *extra],
                           capture_output=True, text=True, encoding="utf-8", creationflags=NO_WINDOW, timeout=60)
        return r.returncode, r.stdout, r.stderr

    code, out, err = helper("--suites", "good")
    check(code == 0, (out, err))
    report = json.loads(out)["suites"]["good"]
    check(report["ticks"] == 4 and report["has_exit_counters"] and report["nonzero"] == {"hk_beta_skipped": 1}, report)
    code, out, err = helper()
    check(code == 1, (out, err))
    reports = json.loads(out)["suites"]
    check(sorted(reports) == ["good", "nolog", "short"], reports.keys())
    check(any("missing" in p for p in reports["nolog"]["problems"]), reports["nolog"])
    check(any("comparison says 5 common ticks" in p for p in reports["short"]["problems"]), reports["short"])
    check(reports["good"]["problems"] == [], reports["good"])
    code, out, err = helper("--suites", "absent")
    check(code == 1 and json.loads(out)["suites"]["absent"]["problems"], (code, out, err))
    r = subprocess.run([sys.executable, "-B", str(COUNTERS), str(fx.tmp / "nope"), "--output", str(out_path)], capture_output=True, text=True, creationflags=NO_WINDOW)
    check(r.returncode == 2, r)


def test_counters_rtl(fx):
    """rtl_* counters: reported when nonzero, not kept; rtl_refused and a failed own-heap walk are problems."""
    live = fx.tmp / "rtl-run"
    clean = {"rtl_calloc_calls": 9, "rtl_foreign_calls": 0, "rtl_heap_foreign_frees": 0, "rtl_heap_shadow_mismatches": 0}
    make_live(live, [
        ("clean", dict(ticks=3, common=3, exit_scale=5, rtl_last=clean, rtl_exit=dict(clean, rtl_heap_own_blocks=12))),
        ("noisy", dict(ticks=3, common=3, exit_scale=5,
                       rtl_last={"rtl_calloc_calls": 4, "rtl_heap_foreign_frees": 3, "rtl_heap_shadow_mismatches": 2},
                       rtl_exit={"rtl_memset_skipped": 1, "rtl_free_mismatches": 5, "rtl_heap_foreign_frees": 4,
                                 "rtl_first_mismatch": "memset:x", "rtl_heap_own_blocks": 7})),
        ("refused", dict(ticks=2, common=2, rtl_last={"rtl_refused": "slot_free"})),
        ("broken", dict(ticks=2, common=2, exit_scale=5, rtl_exit={"rtl_heap_own_blocks": -1})),
    ])
    out_path = fx.tmp / "rtl-counters.json"
    r = subprocess.run([sys.executable, "-B", str(COUNTERS), str(live), "--output", str(out_path)],
                       capture_output=True, text=True, encoding="utf-8", creationflags=NO_WINDOW, timeout=60)
    check(r.returncode == 1, (r.stdout, r.stderr))
    reports = json.loads(r.stdout)["suites"]
    check(reports["clean"]["nonzero"] == {} and reports["clean"]["mismatches"] == {} and reports["clean"]["problems"] == [], reports["clean"])
    noisy = reports["noisy"]
    check(noisy["nonzero"] == {"rtl_heap_foreign_frees": 4, "rtl_memset_skipped": 1}, noisy)
    check(noisy["mismatches"] == {"rtl_heap_shadow_mismatches": 2, "rtl_free_mismatches": 5} and noisy["problems"] == [], noisy)
    check(reports["refused"]["problems"] == ["rtl_refused: slot_free"], reports["refused"])
    check(reports["broken"]["problems"] == ["rtl_heap_own_blocks: the own heap's walk failed"], reports["broken"])
    kept = json.loads(out_path.read_text(encoding="utf-8"))["suites"]["noisy"]
    check(all(k.startswith("hk_") for record in kept.values() if isinstance(record, dict) for k in record), kept)


def main():
    with tempfile.TemporaryDirectory(prefix="es2-check-test-") as tmp:
        fx = Fixture(Path(tmp).resolve())
        for name, test in list(globals().items()):
            if name.startswith("test_") and callable(test):
                test(fx)
                print("ok", name)
    print("%d checks" % checks)


if __name__ == "__main__":
    main()
