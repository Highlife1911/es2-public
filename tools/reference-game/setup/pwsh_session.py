"""One PowerShell process per test instead of one per step.

Starting `pwsh -File script ...` or `pwsh -Command ...` for every step lets
process start-up and module loading dominate a script test's run time.
A PwshSession starts a single PowerShell process that serves steps over
stdin/stdout. Each step still runs in its own fresh runspace, so functions,
modules and variables never leak from one step into the next, exactly as with
separate processes. The server also restores the process environment and the
current directory after every step, forces a garbage collection so that a lock
a step left open (FileStream with DeleteOnClose) is released when the step
ends, and emulates the exit code of `-File`: an uncaught error is 1, `exit N`
is N, otherwise 0.

What a session cannot stand in for stays a separate process on purpose: a
second engine (Windows PowerShell 5.1 next to PowerShell 7), a lock holder that
must live in another session, and a check of what happens when a process ends.
Use `isolated=True` for the last kind.

Only the scripts under test are run, with the arguments the test passes. The
steps use synthetic fixtures; nothing here starts or touches a game.
"""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time

NO_WINDOW = 0x08000000
MARKER = "@@ES2STEP@@"

SERVER = r'''
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$reader = New-Object System.IO.StreamReader([Console]::OpenStandardInput(), $utf8)
$writer = [Console]::OpenStandardOutput()
$marker = '@@ES2STEP@@'
function Get-EnvSnapshot {
    $h = @{}
    foreach ($e in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $h[[string]$e.Key] = [string]$e.Value }
    return $h
}
function Restore-Env($snapshot) {
    $now = Get-EnvSnapshot
    foreach ($k in @($now.Keys)) {
        if (!$snapshot.ContainsKey($k)) { [Environment]::SetEnvironmentVariable($k, [NullString]::Value) }
        elseif ($snapshot[$k] -cne $now[$k]) { [Environment]::SetEnvironmentVariable($k, $snapshot[$k]) }
    }
    foreach ($k in @($snapshot.Keys)) {
        if (!$now.ContainsKey($k)) { [Environment]::SetEnvironmentVariable($k, $snapshot[$k]) }
    }
}
function Add-StepArguments($ps, $list) {
    # Mirrors `pwsh -File script -Name value -Switch ...`: AddParameter binds
    # like a command line, so a script's own $args splatting keeps working.
    $isName = '^-[A-Za-z][A-Za-z0-9_]*$'
    $i = 0
    while ($i -lt $list.Count) {
        $t = [string]$list[$i]
        if ($t -match $isName) {
            if (($i + 1) -lt $list.Count -and ([string]$list[$i + 1]) -notmatch $isName) { [void]$ps.AddParameter($t.Substring(1), [string]$list[$i + 1]); $i += 2 }
            else { [void]$ps.AddParameter($t.Substring(1)); $i += 1 }
        } else { [void]$ps.AddArgument($t); $i += 1 }
    }
}
function Format-StepError($record) {
    $where = if ($record.InvocationInfo) { $record.InvocationInfo.PositionMessage } else { '' }
    return ($record.ToString() + "`n" + $where + "`n" + $record.ScriptStackTrace)
}
function Invoke-Step($request) {
    $snapshot = Get-EnvSnapshot
    $cwd = [Environment]::CurrentDirectory
    $rs = $null; $ps = $null
    $result = [ordered]@{rc = 1; out = ''; err = ''}
    try {
        foreach ($p in $request.env.PSObject.Properties) { [Environment]::SetEnvironmentVariable($p.Name, [string]$p.Value) }
        $rs = [runspacefactory]::CreateRunspace()
        $rs.Open()
        $ps = [powershell]::Create()
        $ps.Runspace = $rs
        $rs.SessionStateProxy.SetVariable('LASTEXITCODE', 0)
        [void]$ps.AddCommand([string]$request.file)
        Add-StepArguments $ps @($request.args)
        $failure = ''
        $code = 0
        $output = @()
        try { $output = @($ps.Invoke()) }
        catch {
            $inner = $_.Exception
            while ($inner.InnerException -and $inner -isnot [System.Management.Automation.IContainsErrorRecord]) { $inner = $inner.InnerException }
            $failure = if ($inner -is [System.Management.Automation.IContainsErrorRecord]) { Format-StepError $inner.ErrorRecord } else { $inner.ToString() }
            $code = 1
        }
        if ($code -eq 0) { $code = [int]$rs.SessionStateProxy.PSVariable.GetValue('LASTEXITCODE') }
        $messages = @($ps.Streams.Information | ForEach-Object { [string]$_.MessageData })
        $errors = @($ps.Streams.Error | ForEach-Object { Format-StepError $_ })
        $warnings = @($ps.Streams.Warning | ForEach-Object { 'WARNING: ' + $_.Message })
        $result.rc = $code
        $text = @(@($output | Out-String -Stream) + $messages) -join "`n"
        $result.out = if ($text) { $text + "`n" } else { '' }
        $result.err = ((@($errors) + @($warnings) + @($failure)) | Where-Object { $_ }) -join "`n"
    } catch {
        $result.rc = 1
        $result.err = 'session server: ' + $_.ToString()
    } finally {
        if ($ps) { $ps.Dispose() }
        if ($rs) { $rs.Dispose() }
        $ps = $null; $rs = $null
        # A lock a step left open vanishes with the step, as with its process.
        [GC]::Collect(); [GC]::WaitForPendingFinalizers(); [GC]::Collect()
        Restore-Env $snapshot
        [Environment]::CurrentDirectory = $cwd
    }
    return $result
}
function Send($object) {
    $json = ConvertTo-Json -InputObject $object -Compress
    $bytes = $utf8.GetBytes("`n" + $marker + $json + "`n")
    $writer.Write($bytes, 0, $bytes.Length)
    $writer.Flush()
}
Send @{ready = $true}
while ($true) {
    $line = $reader.ReadLine()
    if ($null -eq $line -or $line -eq 'quit') { break }
    Send (Invoke-Step ($line | ConvertFrom-Json))
}
'''


class Result:
    """Like subprocess.CompletedProcess for the fields the tests use."""

    def __init__(self, returncode, stdout, stderr):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


class PwshSession:
    def __init__(self, shell, scratch=None, base_env=None):
        self.shell = str(shell)
        self.dir = Path(tempfile.mkdtemp(prefix="pwsh-session-", dir=scratch or os.environ.get("TEST_TMPDIR")))
        self.base_env = dict(base_env if base_env is not None else os.environ)
        self.base_env.pop("ES2_CLONE_LOCK_TOKEN", None)
        self.snippets = 0
        self.process = None
        self.lines = queue.Queue()
        (self.dir / "server.ps1").write_text(SERVER, encoding="utf-8-sig", newline="\n")

    def _start(self):
        self.error_log = open(self.dir / "server.stderr", "wb")
        self.process = subprocess.Popen(
            [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(self.dir / "server.ps1")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.error_log, env=self.base_env,
            creationflags=NO_WINDOW)
        threading.Thread(target=self._pump, args=(self.process, self.lines), daemon=True).start()
        self._read_response(60)  # ready

    @staticmethod
    def _pump(process, lines):
        for raw in process.stdout:
            lines.put(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
        lines.put(None)

    def _read_response(self, timeout):
        deadline = time.time() + timeout
        noise = []
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                self.close(kill=True)
                raise subprocess.TimeoutExpired(self.shell, timeout)
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if line is None:
                raise AssertionError("PowerShell session ended: " + self._stderr())
            if line.startswith(MARKER):
                return json.loads(line[len(MARKER):]), noise
            if line:
                noise.append(line)

    def _stderr(self):
        try:
            return (self.dir / "server.stderr").read_text(errors="replace")
        except OSError:
            return ""

    def run(self, file=None, args=(), code=None, env=None, timeout=120, isolated=False):
        """Run a script file or a code snippet in a fresh runspace (or, with
        isolated=True, in a separate one-off process) and return a Result."""
        if code is not None:
            self.snippets += 1
            file = self.dir / f"step{self.snippets}.ps1"
            file.write_text(code, encoding="utf-8-sig", newline="\n")
        if isolated:
            run_env = dict(self.base_env)
            run_env.update(env or {})
            command = [self.shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(file), *map(str, args)]
            done = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=timeout,
                                  env=run_env, creationflags=NO_WINDOW)
            return Result(done.returncode, done.stdout, done.stderr)
        if self.process is None:
            self._start()
        request = dict(file=str(file), args=[str(a) for a in args], env={k: str(v) for k, v in (env or {}).items()})
        self.process.stdin.write((json.dumps(request) + "\n").encode("utf-8"))
        self.process.stdin.flush()
        response, noise = self._read_response(timeout)
        if response["err"].startswith("session server:"):
            raise AssertionError("PowerShell session failed internally: " + response["err"])
        return Result(response["rc"], "".join(l + "\n" for l in noise) + response["out"], response["err"])

    def close(self, kill=False):
        process, self.process = self.process, None
        if process is not None:
            try:
                if kill:
                    process.kill()
                else:
                    process.stdin.write(b"quit\n")
                    process.stdin.flush()
                    process.stdin.close()
                process.wait(timeout=30)
            except (OSError, subprocess.TimeoutExpired):
                process.kill()
            self.error_log.close()

    def dispose(self):
        self.close()
        shutil.rmtree(self.dir, ignore_errors=True)


# ---- Bazel test sharding ---------------------------------------------------------------------------
# A script test whose sections are independent (each owns a synthetic tree) is split over
# `shard_count` Bazel shards: section i belongs to shard i % TEST_TOTAL_SHARDS. Without Bazel's
# variables there is one shard and every section runs, so a direct run is the whole test.

def shard_position():
    """(index, total) of this process from Bazel's sharding variables; touches TEST_SHARD_STATUS_FILE
    (Bazel's sign that the test honours sharding). A malformed value is an error."""
    total, index = os.environ.get("TEST_TOTAL_SHARDS"), os.environ.get("TEST_SHARD_INDEX")
    if total is None and index is None:
        return 0, 1
    if not (total and index and total.isdigit() and index.isdigit() and 0 <= int(index) < int(total)):
        raise AssertionError(f"invalid Bazel shard variables: TEST_TOTAL_SHARDS={total!r} TEST_SHARD_INDEX={index!r}")
    status = os.environ.get("TEST_SHARD_STATUS_FILE")
    if status:
        Path(status).write_bytes(b"")
    return int(index), int(total)


def shard_items(items):
    """The sections of `items` that this shard owns (all of them without sharding), in order."""
    index, total = shard_position()
    items = list(items)
    if total > len(items):
        raise AssertionError(f"{total} Bazel shards for {len(items)} sections")
    return [item for position, item in enumerate(items) if position % total == index]
