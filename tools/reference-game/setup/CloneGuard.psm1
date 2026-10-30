# Clone guard for the oracle runners.
#
# Two rules keep sessions from stepping on each other's measurement clone:
#  1. One run per clone: a lock file under .clone-locks\ of the installation
#     root (Get-CloneInstallRoot) that the holder keeps open (share: read only, deleted on close). While
#     the holder lives, nobody can delete it; when the holder exits, crashes
#     or is killed, Windows closes the handle and the lock disappears. A lock
#     file that can be deleted is therefore stale and is reclaimed.
#     Child runners of the holder inherit the lock through ES2_CLONE_LOCK_TOKEN.
#  2. Only own processes: a game process is waited for or stopped only when a
#     fresh launch report of this run names it and its PID, executable path and
#     start time all still match. Anything else is reported and left alone.
# This module starts no process. docs/reference-game/oracle_regression.md
$ErrorActionPreference = 'Stop'
$script:GameNames = @('ES.EXE','VSHELL.EXE','DBSIM.EXE')
$script:SessionHint = ''
function Get-Es2Layout {
    # The machine layout: the main checkout (also when called from a worktree, through git's common
    # directory), its build\ directory, and es2-data and es2-dependencies beside it. ES2_DATA_REPO and
    # ES2_DEPENDENCIES override the two siblings.
    $repository = $null
    try {
        $common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir 2>$null
        if ($LASTEXITCODE -eq 0 -and $common) { $repository = Split-Path -Parent ([IO.Path]::GetFullPath(([string]$common).Trim())) }
    } catch { $repository = $null }
    if (!$repository) { $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')) }
    $repository = $repository.TrimEnd([char]'\', [char]'/')
    $parent = Split-Path -Parent $repository
    $data = if ($env:ES2_DATA_REPO) { [IO.Path]::GetFullPath($env:ES2_DATA_REPO).TrimEnd([char]'\', [char]'/') } else { Join-Path $parent 'es2-data' }
    $dependencies = if ($env:ES2_DEPENDENCIES) { [IO.Path]::GetFullPath($env:ES2_DEPENDENCIES).TrimEnd([char]'\', [char]'/') } else { Join-Path $parent 'es2-dependencies' }
    $build = Join-Path $repository 'build'
    [pscustomobject]@{
        Repository = $repository; Build = $build; Extracted = (Join-Path $build 'extracted'); Installs = (Join-Path $build 'installs')
        BazelOutputRoot = (Join-Path $build 'bazel'); DataRepository = $data; Dependencies = $dependencies
        Python = (Join-Path $dependencies 'python\python.exe'); Objdump = (Join-Path $dependencies 'llvm-mingw\bin\llvm-objdump.exe')
    }
}
# The installations of the data repository lie in the es2 checkout's
# build\installs; any other DataRoot (a test's synthetic root) keeps
# them in <DataRoot>\installs.
$script:Layout = Get-Es2Layout
$script:DataRepository = $script:Layout.DataRepository
$script:BuildInstallRoot = $script:Layout.Installs

function ConvertTo-CloneUtc($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    return [DateTime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
function Get-CloneDirectory([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd([char]'\', [char]'/')
}
function Get-CloneInstallRoot([string]$DataRoot) {
    # The directory of the installations (native-r111 and the clones) that
    # belong to DataRoot, as a full path without a trailing separator.
    $root = Get-CloneDirectory $DataRoot
    if ($root -ieq $script:DataRepository) { return $script:BuildInstallRoot }
    return Join-Path $root 'installs'
}
function Get-CloneGameProcess {
    # Game processes (ES/VSHELL/DBSIM) with PID, path and start time; with
    # -Installation only those whose executable lies directly in that clone.
    param([string]$Installation = '')
    $directory = if ($Installation) { Get-CloneDirectory $Installation } else { '' }
    $found = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in $script:GameNames })
    foreach ($p in $found) {
        # Right after its start a process often has no ExecutablePath in the CIM snapshot yet; the
        # kernel32 read (Get-CloneProcessImagePath) has it at once, so a game that has just started in the
        # clone is not overlooked.
        $path = [string]$p.ExecutablePath
        if (!$path) { $path = Get-CloneProcessImagePath ([int]$p.ProcessId) }
        if ($directory -and (!$path -or
            ![string]::Equals([IO.Path]::GetDirectoryName($path), $directory, [StringComparison]::OrdinalIgnoreCase))) { continue }
        $start = $null
        try {
            $live = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
            if ($live) { $start = $live.StartTime.ToUniversalTime() }
        } catch { $start = $null }
        if (!$start -and $p.CreationDate) { $start = ConvertTo-CloneUtc $p.CreationDate }
        [pscustomobject]@{ProcessId=[int]$p.ProcessId; Name=[string]$p.Name; ExecutablePath=$path; StartTimeUtc=$start}
    }
}
function Format-CloneGameProcess($Process) {
    $start = if ($Process.StartTimeUtc) { (ConvertTo-CloneUtc $Process.StartTimeUtc).ToString('o') } else { 'unknown' }
    $path = if ($Process.ExecutablePath) { $Process.ExecutablePath } else { 'path not readable' }
    return "$($Process.Name) PID $($Process.ProcessId) ($path, started $start)"
}
function Assert-CloneIdle {
    # Refuses while any game process runs in the clone. -WaitSeconds only
    # waits for such processes to exit by themselves; it never stops one.
    param([Parameter(Mandatory)][string]$Installation, [string]$Action = 'using it', [ValidateRange(0,3600)][int]$WaitSeconds = 0)
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while ($true) {
        $busy = @(Get-CloneGameProcess -Installation $Installation)
        if (!$busy.Count) { return }
        if ([DateTime]::UtcNow -ge $deadline) { break }
        Start-Sleep -Milliseconds 250
    }
    $list = ($busy | ForEach-Object { Format-CloneGameProcess $_ }) -join '; '
    $waited = if ($WaitSeconds) { " (still running after waiting $WaitSeconds s)" } else { '' }
    throw ("Close the game in this clone before $Action. Running in $(Get-CloneDirectory $Installation)$waited : $list. " +
        'This run did not start it and leaves it alone; if another session owns it, wait for that run to finish.')
}

function Expand-CloneShortPath([string]$Path) {
    # 8.3 names to long names for an existing path (kernel32 GetLongPathNameW);
    # '' when the path does not exist (any more) or the call is not available.
    try {
        if (!('Es2.CloneGuardNative' -as [type])) {
            Add-Type -Namespace 'Es2' -Name 'CloneGuardNative' -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern uint GetLongPathNameW(string shortPath, System.Text.StringBuilder longPath, uint length);
'@
        }
        $buffer = [Text.StringBuilder]::new(32768)
        $length = [Es2.CloneGuardNative]::GetLongPathNameW($Path, $buffer, [uint32]$buffer.Capacity)
        if ($length -gt 0 -and $length -lt $buffer.Capacity) { return $buffer.ToString() }
    } catch { }
    return ''
}
function Resolve-ClonePath {
    # Normal form for path comparisons: no \\?\ (\??\, \\.\) prefix, full path
    # (. and .. resolved), long names instead of 8.3 names when the path
    # exists, no trailing separator. '' for an empty or unusable path.
    # Case is not changed here; comparisons are case-insensitive.
    param([string]$Path)
    if (!$Path -or !$Path.Trim()) { return '' }
    $p = $Path.Trim()
    if ($p.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) { $p = '\\' + $p.Substring(8) }
    elseif ($p.StartsWith('\\?\', [StringComparison]::Ordinal) -or $p.StartsWith('\??\', [StringComparison]::Ordinal) -or
        $p.StartsWith('\\.\', [StringComparison]::Ordinal)) { $p = $p.Substring(4) }
    try { $p = [IO.Path]::GetFullPath($p) } catch { return '' }
    if ($p.Contains('~')) {
        $long = Expand-CloneShortPath $p
        if ($long) { $p = $long }
    }
    return $p.TrimEnd([char]'\', [char]'/')
}
function Test-ClonePathInside {
    # True when Path lies strictly below Root (whole directory names only:
    # 'installs-old' is not inside 'installs'), after Resolve-ClonePath.
    param([string]$Path, [string]$Root)
    $p = Resolve-ClonePath $Path
    $r = Resolve-ClonePath $Root
    if (!$p -or !$r) { return $false }
    return $p.StartsWith($r + '\', [StringComparison]::OrdinalIgnoreCase)
}
$script:ParallelGameName = 'DBSIM.EXE'
function Get-CloneBlockingGame {
    # The machine-wide rule of start-timing-mission.ps1. Only a game
    # process that runs from an installation under Get-CloneInstallRoot (the
    # primary installation native-r111 and every clone alike) can block a
    # run; a process anywhere else (the stand-in dbsim.EXE of a Bazel test in
    # its temporary directory) is none of the guard's business and is ignored.
    # A DBSIM in one of the PermittedClones (the clones of a parallel run) is
    # the run's own neighbour and passes, except in the target Installation.
    # A process whose path cannot be read cannot be shown to lie outside the
    # installations. Right after its start a process often has no readable
    # path yet (a neighbour clone's DBSIM started in the same second refused
    # a suite of a five-clone check), so the path is read
    # again (PathReader, every 250 ms, PathRetries times, about 3 s); a path
    # that appears is judged like any other, a process that exits meanwhile
    # is ignored, and one whose path stays unreadable blocks.
    # Returns objects {Process; Reason} with Reason this-clone,
    # other-installation or path-unreadable; nothing is stopped or touched.
    param([Parameter(Mandatory)][string]$DataRoot, [string]$Installation = '', [string[]]$PermittedClones = @(), $Processes = $null,
        [scriptblock]$PathReader = { param($ProcessId) (Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue).ExecutablePath },
        [int]$PathRetries = 12)
    $installs = Resolve-ClonePath (Get-CloneInstallRoot $DataRoot)
    $target = Resolve-ClonePath $Installation
    $permitted = @($PermittedClones | Where-Object { $_ } | ForEach-Object { Resolve-ClonePath $_ })
    if ($null -eq $Processes) { $Processes = @(Get-CloneGameProcess) }
    foreach ($p in @($Processes)) {
        $path = Resolve-ClonePath $p.ExecutablePath
        $reason = ''
        $alive = $true
        for ($try = 0; !$path -and $try -lt $PathRetries; $try++) {
            $alive = [bool](Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue)
            if (!$alive) { break }
            Start-Sleep -Milliseconds 250
            $path = Resolve-ClonePath ([string](& $PathReader $p.ProcessId))
        }
        if (!$path) {
            if ($alive -and (Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue)) { $reason = 'path-unreadable' }
        } elseif ($target -and (Test-ClonePathInside $path $target)) {
            $reason = 'this-clone'
        } elseif (Test-ClonePathInside $path $installs) {
            $neighbour = $p.Name -ieq $script:ParallelGameName -and
                @($permitted | Where-Object { Test-ClonePathInside $path $_ }).Count -gt 0
            if (!$neighbour) { $reason = 'other-installation' }
        }
        if ($reason) { [pscustomobject]@{Process=$p; Reason=$reason} }
    }
}
function Assert-CloneMachineIdle {
    # Refuses (throws) when Get-CloneBlockingGame finds a blocker; names the
    # process and leaves it alone. The messages carry the phrases that
    # tools/check.ps1 recognises as a refusal to rerun ($RefusedPatterns).
    param([Parameter(Mandatory)][string]$DataRoot, [Parameter(Mandatory)][string]$Installation, [string[]]$PermittedClones = @(), $Processes = $null,
        [scriptblock]$PathReader = $null, [int]$PathRetries = 12)
    $reader = @{}
    if ($PathReader) { $reader.PathReader = $PathReader }
    $blocking = @(Get-CloneBlockingGame -DataRoot $DataRoot -Installation $Installation -PermittedClones $PermittedClones -Processes $Processes -PathRetries $PathRetries @reader)
    if (!$blocking.Count) { return }
    $own = @($blocking | Where-Object { $_.Reason -eq 'this-clone' })
    if ($own.Count) {
        throw "Close the game in this clone before starting another: $(Format-CloneGameProcess $own[0].Process). It was not started by this launch and is left alone."
    }
    $first = $blocking[0]
    if ($first.Reason -eq 'path-unreadable') {
        throw ("A game process whose path cannot be read is running and may belong to an installation: $(Format-CloneGameProcess $first.Process). " +
            'It is left alone; start the run again when it has exited.')
    }
    throw ("An existing game of another installation is running, outside the permitted, separate parallel clones: $(Format-CloneGameProcess $first.Process). " +
        'It is left alone.')
}

function Get-CloneLockPath {
    param([Parameter(Mandatory)][string]$DataRoot, [Parameter(Mandatory)][string]$InstallName)
    if ($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw "Invalid clone name: $InstallName" }
    return Join-Path (Get-CloneInstallRoot $DataRoot) ".clone-locks\$InstallName.json"
}
function Read-CloneLock([string]$Path) {
    # Shared read beside the holder's open handle; null when absent or unreadable.
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try { $text = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8).ReadToEnd() } finally { $stream.Dispose() }
        if (!$text.Trim()) { return $null }
        return $text | ConvertFrom-Json
    } catch { return $null }
}
function Test-CloneLockHeld([string]$Path) {
    # The holder's handle denies exclusive opens; a stale file allows one.
    try { $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None) }
    catch [IO.FileNotFoundException] { return $false }
    catch [IO.DirectoryNotFoundException] { return $false }
    catch { return $true }
    $stream.Dispose()
    return $false
}
function Format-CloneLockHolder($Holder) {
    $start = if ($Holder.holder_start_utc) { (ConvertTo-CloneUtc $Holder.holder_start_utc).ToString('o') } else { 'unknown' }
    $since = if ($Holder.acquired_utc) { (ConvertTo-CloneUtc $Holder.acquired_utc).ToString('o') } else { 'unknown' }
    return "holder PID $($Holder.holder_pid) ($($Holder.holder_path), started $start), session '$($Holder.session)', purpose '$($Holder.purpose)', locked since $since"
}
function Get-CloneSessionHint {
    # ES2_SESSION_HINT names the session explicitly; otherwise user, runner
    # parent and the repository the runner comes from identify it.
    if ($env:ES2_SESSION_HINT) { return $env:ES2_SESSION_HINT }
    if ($script:SessionHint) { return $script:SessionHint }
    # PowerShell 7 knows the parent process without a (slow) CIM query; 5.1
    # leaves it out.
    $parent = ''
    try {
        $up = (Get-Process -Id $PID).Parent
        if ($up) { $parent = ", parent $($up.ProcessName) PID $($up.Id)" }
    } catch { $parent = '' }
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
    $script:SessionHint = "$env:USERNAME@$env:COMPUTERNAME$parent, repository $repository"
    return $script:SessionHint
}
function Enter-CloneLock {
    # Returns {Path, Token, Inherited, Stream}. Keep the object referenced for
    # the whole run and pass it to Exit-CloneLock; set ES2_CLONE_LOCK_TOKEN to
    # its Token for child runners (and for the low-level scripts it calls).
    param([Parameter(Mandatory)][string]$DataRoot, [Parameter(Mandatory)][string]$InstallName, [string]$Purpose = 'oracle run')
    $path = Get-CloneLockPath $DataRoot $InstallName
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    $self = Get-Process -Id $PID
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        if (Test-Path -LiteralPath $path) {
            $held = $false
            try { [IO.File]::Delete($path) }
            catch [IO.IOException] { $held = $true }
            catch [UnauthorizedAccessException] { Start-Sleep -Milliseconds 100; continue }  # delete pending
            if ($held) {
                $holder = Read-CloneLock $path
                if (!$holder) { Start-Sleep -Milliseconds 100; continue }  # still being written
                if ($env:ES2_CLONE_LOCK_TOKEN -and $holder.token -ceq $env:ES2_CLONE_LOCK_TOKEN -and $holder.clone -ceq $InstallName) {
                    return [pscustomobject]@{Path=$path; Token=[string]$holder.token; Inherited=$true; Stream=$null; Clone=$InstallName}
                }
                throw ("Clone $InstallName is in use by another session: $(Format-CloneLockHolder $holder). Lock file: $path. " +
                    'Nothing was deployed or started; wait for that run to finish (the lock disappears when its holder exits).')
            }
        }
        $token = [Guid]::NewGuid().ToString('N')
        try {
            $stream = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read, 4096, [IO.FileOptions]::DeleteOnClose)
        }
        catch [IO.IOException] { Start-Sleep -Milliseconds 50; continue }  # another session created it first
        catch [UnauthorizedAccessException] { Start-Sleep -Milliseconds 100; continue }
        try {
            $record = [ordered]@{
                schema=1; clone=$InstallName; token=$token; holder_pid=$PID; holder_path=$self.Path
                holder_start_utc=$self.StartTime.ToUniversalTime().ToString('o'); session=(Get-CloneSessionHint)
                purpose=$Purpose; acquired_utc=[DateTime]::UtcNow.ToString('o')
            }
            $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json) + "`n")
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } catch { $stream.Dispose(); throw }
        return [pscustomobject]@{Path=$path; Token=$token; Inherited=$false; Stream=$stream; Clone=$InstallName}
    }
    throw "Could not acquire the clone lock $path (it kept changing); nothing was deployed or started."
}
function Exit-CloneLock($Lock) {
    if ($Lock -and !$Lock.Inherited -and $Lock.Stream) { $Lock.Stream.Dispose() }
}
function Assert-CloneLockAvailable {
    # For the low-level deployment/launch scripts: refuse when another
    # session's live lock covers the clone. No lock or an own (inherited)
    # lock passes; nothing is created here.
    param([Parameter(Mandatory)][string]$DataRoot, [Parameter(Mandatory)][string]$InstallName)
    $path = Get-CloneLockPath $DataRoot $InstallName
    if (!(Test-Path -LiteralPath $path)) { return }
    $holder = Read-CloneLock $path
    if ($holder -and $env:ES2_CLONE_LOCK_TOKEN -and $holder.token -ceq $env:ES2_CLONE_LOCK_TOKEN) { return }
    if (!(Test-CloneLockHeld $path)) { return }
    $who = if ($holder) { Format-CloneLockHolder $holder } else { 'holder unreadable' }
    throw "Clone $InstallName is in use by another session: $who. Lock file: $path. Nothing was deployed or started."
}

function Get-CloneProcessImagePath {
    # The executable path of a running process from kernel32
    # QueryFullProcessImageNameW; '' when it cannot be read (no such process,
    # access denied, API not available). System.Diagnostics.Process.Path
    # (MainModule) is not used for the identity check: while a process is
    # still starting up, and the busier the machine the longer, the loader's
    # module list is incomplete and Path then answers the first module that
    # exists (ntdll.dll) or nothing. A process that is the run's own was then
    # classified 'foreign' (status pid-reused) (measured under CPU load:
    # about 5 % of the first 15 reads of a fresh process were wrong; the
    # kernel32 call was right in 900 of 900).
    param([Parameter(Mandatory)][int]$Id)
    try {
        if (!('Es2.CloneGuardProcess' -as [type])) {
            Add-Type -Namespace 'Es2' -Name 'CloneGuardProcess' -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
private static extern System.IntPtr OpenProcess(uint access, bool inherit, uint pid);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
private static extern bool QueryFullProcessImageNameW(System.IntPtr process, uint flags, System.Text.StringBuilder path, ref uint size);
[System.Runtime.InteropServices.DllImport("kernel32.dll")]
private static extern bool CloseHandle(System.IntPtr handle);
public static string ImagePath(uint pid)
{
    System.IntPtr handle = OpenProcess(0x1000, false, pid);   // PROCESS_QUERY_LIMITED_INFORMATION
    if (handle == System.IntPtr.Zero) return "";
    try
    {
        System.Text.StringBuilder path = new System.Text.StringBuilder(32768);
        uint size = (uint)path.Capacity;
        return QueryFullProcessImageNameW(handle, 0, path, ref size) ? path.ToString() : "";
    }
    finally { CloseHandle(handle); }
}
'@
        }
        return [string][Es2.CloneGuardProcess]::ImagePath([uint32]$Id)
    } catch { return '' }
}
function Get-CloneOwnedState([int]$Id, [string]$Exe, [DateTime]$StartUtc) {
    # owned: PID, executable path and start time all match; gone: no such process (any more);
    # foreign: the PID names a process with another start time or another path.
    # A start time that differs is final (it is read from the process times and is never transient);
    # a path that kernel32 reads and that differs is final too. A process whose path or start time cannot be
    # read right now (just started, or in the middle of exiting, on a busy machine) is looked at again for
    # about 2 s: it then either matches, is gone, or is judged foreign.
    for ($try = 0; ; $try++) {
        $live = Get-Process -Id $Id -ErrorAction SilentlyContinue
        if (!$live) { return @{State='gone'} }
        $start = $null; $path = ''
        try {
            if ($live.HasExited) { return @{State='gone'} }
            $start = $live.StartTime.ToUniversalTime()
            $path = Get-CloneProcessImagePath $Id
            if ($start -ne $StartUtc) { return @{State='foreign'} }
            if ($path) {
                if ([string]::Equals($path, $Exe, [StringComparison]::OrdinalIgnoreCase)) { return @{State='owned'; Process=$live} }
                return @{State='foreign'}
            }
            # kernel32 could not read it: the older read (System.Diagnostics.Process.Path), which can be wrong
            # during start-up, so a match counts but a mismatch is looked at again.
            $path = $live.Path
            if ($path -and [string]::Equals($path, $Exe, [StringComparison]::OrdinalIgnoreCase)) { return @{State='owned'; Process=$live} }
        } catch { }
        if ($try -ge 20) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!(Get-Process -Id $Id -ErrorAction SilentlyContinue)) { return @{State='gone'} }
    return @{State='foreign'}
}
function Register-CloneStartedProcess {
    # The launcher (start-timing-mission.ps1) hands over the Process
    # object that Start-Process -PassThru returned. Its handle stays open for
    # this PowerShell process, so Wait-CloneOwnedProcessExit can read the real
    # exit code even when the game ended before the wait began (a process
    # nobody holds a handle to leaves no exit code behind). Global on purpose:
    # every script of a run imports this module with its own scope.
    param([Parameter(Mandatory)]$Process)
    if (!$global:Es2StartedProcesses) { $global:Es2StartedProcesses = @{} }
    $global:Es2StartedProcesses[[int]$Process.Id] = [pscustomobject]@{Process=$Process; StartUtc=$Process.StartTime.ToUniversalTime()}
}
function Get-CloneExitCode($Process) {
    # The exit code of a process object that has exited; $null when it cannot be read.
    try {
        if (!$Process.WaitForExit(2000)) { return $null }
        return [int]$Process.ExitCode
    } catch { return $null }
}
function Wait-CloneOwnedProcessExit {
    # Waits up to GraceSeconds for the process named by this run's launch
    # report to exit, then stops it and waits StopTimeoutSeconds. Identity is
    # PID + executable path + start time; the report must lie in this clone and
    # be no older than NotBefore. A reused PID is never touched.
    # Result: exit_code is the process's own exit code when it ended by
    # itself, $null when this function stopped it (stopped_by_runner), when the
    # PID was reused or no process was started, or when no handle gave the code
    # (exit_code_source 'none' with status 'exited'). exit_code_source:
    # 'launcher-handle' (Register-CloneStartedProcess), 'opened-handle' (opened
    # here while the process still ran) or 'none'.
    param([Parameter(Mandatory)][string]$Installation, [Parameter(Mandatory)][string]$LaunchReport,
        [Parameter(Mandatory)][DateTime]$NotBefore, [ValidateRange(0,3600)][int]$GraceSeconds = 30,
        [ValidateRange(1,600)][int]$StopTimeoutSeconds = 15)
    $result = [ordered]@{status='no-launch-report'; pid=$null; waited_ms=0; exit_code=$null; exit_code_source='none'; stopped_by_runner=$false}
    if (!(Test-Path -LiteralPath $LaunchReport -PathType Leaf)) { return [pscustomobject]$result }
    $report = Get-Content -LiteralPath $LaunchReport -Raw | ConvertFrom-Json
    if (!$report.pid) { $result.status = 'not-started'; return [pscustomobject]$result }
    $result.pid = [int]$report.pid
    $exe = [IO.Path]::GetFullPath([string]$report.exe)
    $start = ConvertTo-CloneUtc $report.process_start_utc
    if (![string]::Equals([IO.Path]::GetDirectoryName($exe), (Get-CloneDirectory $Installation), [StringComparison]::OrdinalIgnoreCase) -or
        $start -lt $NotBefore.ToUniversalTime().AddSeconds(-1)) {
        throw ("Launch report $LaunchReport does not name a process this run started in $(Get-CloneDirectory $Installation) " +
            "(PID $($report.pid), $exe, started $($start.ToString('o'))); nothing was waited for or stopped.")
    }
    # A handle that outlives the process: the launcher's, else one opened here
    # at the first look (only possible while the process still runs).
    $held = $null; $source = 'none'
    $registered = $global:Es2StartedProcesses
    if ($registered -and $registered.ContainsKey($result.pid) -and $registered[$result.pid].StartUtc -eq $start) {
        $held = $registered[$result.pid].Process; $source = 'launcher-handle'
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $state = Get-CloneOwnedState $result.pid $exe $start
    if (!$held -and $state.State -eq 'owned') {
        try { $null = $state.Process.Handle; $held = $state.Process; $source = 'opened-handle' } catch { $held = $null }
    }
    while ($state.State -eq 'owned' -and $timer.Elapsed.TotalSeconds -lt $GraceSeconds) {
        Start-Sleep -Milliseconds 200
        $state = Get-CloneOwnedState $result.pid $exe $start
    }
    $result.waited_ms = [int]$timer.Elapsed.TotalMilliseconds
    if ($state.State -eq 'foreign') { $result.status = 'pid-reused'; return [pscustomobject]$result }
    if ($state.State -eq 'gone') {
        $result.status = 'exited'
        if ($held) {
            $code = Get-CloneExitCode $held
            if ($null -ne $code) { $result.exit_code = $code; $result.exit_code_source = $source }
        }
        return [pscustomobject]$result
    }
    # Still the own process after the grace period: recheck, stop, wait.
    $state = Get-CloneOwnedState $result.pid $exe $start
    if ($state.State -eq 'owned') {
        Stop-Process -InputObject $state.Process -Force
        [void]$state.Process.WaitForExit($StopTimeoutSeconds * 1000)
        $after = Get-CloneOwnedState $result.pid $exe $start
        if ($after.State -eq 'owned') {
            throw "Own process PID $($result.pid) ($exe, started $($start.ToString('o'))) did not exit within $StopTimeoutSeconds s after the stop; clone $(Get-CloneDirectory $Installation) stays busy."
        }
        $result.status = 'stopped'; $result.stopped_by_runner = $true
    } elseif ($state.State -eq 'gone') {
        # Ended between the last look and the recheck.
        $result.status = 'exited'
        if ($held) {
            $code = Get-CloneExitCode $held
            if ($null -ne $code) { $result.exit_code = $code; $result.exit_code_source = $source }
        }
    } else { $result.status = 'pid-reused' }
    $result.waited_ms = [int]$timer.Elapsed.TotalMilliseconds
    return [pscustomobject]$result
}
function Get-RunnerEnvironment {
    # The complete process environment as a name -> value table (names compare case-insensitively
    # like Windows does). A runner takes it before it sets any variable for its children and hands it to
    # Restore-RunnerEnvironment in a finally, so the calling PowerShell process ends as it began and a
    # second invocation starts its children (DBSIM included) with the same environment strings.
    $snapshot = @{}
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $snapshot[[string]$entry.Key] = [string]$entry.Value }
    return $snapshot
}
function Restore-RunnerEnvironment($Snapshot) {
    # A variable that did not exist is removed ([NullString]::Value: in PowerShell 7 a plain $null reaches
    # .NET as an empty string and leaves an empty variable behind, which DBSIM would still see as a string),
    # one that existed gets its old value back, one the
    # snapshot has and the process lost is set again. Everything else is left alone. Variables the
    # caller set on purpose before the call are in the snapshot and therefore stay.
    $now = Get-RunnerEnvironment
    foreach ($name in @($now.Keys)) {
        if (!$Snapshot.ContainsKey($name)) { [Environment]::SetEnvironmentVariable($name, [NullString]::Value) }
        elseif ($Snapshot[$name] -cne $now[$name]) { [Environment]::SetEnvironmentVariable($name, $Snapshot[$name]) }
    }
    foreach ($name in @($Snapshot.Keys)) {
        if (!$now.ContainsKey($name)) { [Environment]::SetEnvironmentVariable($name, $Snapshot[$name]) }
    }
}
Export-ModuleMember -Function Get-CloneInstallRoot, Get-CloneGameProcess, Get-CloneProcessImagePath, Format-CloneGameProcess, Assert-CloneIdle, Resolve-ClonePath, Test-ClonePathInside,
    Get-CloneBlockingGame, Assert-CloneMachineIdle, Get-CloneLockPath, Read-CloneLock,
    Enter-CloneLock, Exit-CloneLock, Assert-CloneLockAvailable, Wait-CloneOwnedProcessExit, Register-CloneStartedProcess,
    Get-RunnerEnvironment, Restore-RunnerEnvironment, Get-Es2Layout
