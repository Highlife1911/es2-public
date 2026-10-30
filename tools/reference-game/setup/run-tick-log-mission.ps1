[CmdletBinding()]
param(
    [string]$DataRoot = '',
    [string]$InstallName = 'timing-r111',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$RunName,
    [int]$Ticks = 1000,
    [int]$TimeoutSeconds = 120,
    [int]$VirtualTimeStepMs = 0,
    [int]$VirtualTimePace = 1,
    [ValidateSet(0,1)][int]$VirtualTimeIntraFrame = 1,
    [ValidateSet('off', 'record', 'replay')][string]$InputMode = 'off',
    [string]$InputJournal = '',
    [int]$StallSeconds = 5,
    [string[]]$Watch = @(),
    [switch]$SnapshotOnStall,
    [switch]$ActorRaw,
    [switch]$SkipTests,
    [switch]$Interactive,
    [string]$Hooks = '',
    [string]$HookMismatchLog = '',
    [string]$PreparedProxy = $env:ES2_PREPARED_PROXY,
    [switch]$BufferedTickLog = ($env:ES2_TICK_BUFFERED -eq '1'),
    [ValidateRange(1,1000)][int]$FlushIntervalMs = 100,
    [switch]$CompactRng,
    # [trace] hold_frame=Ticks: the game thread parks after the last requested tick
    # record, so an unpaced run (tape playback) ends at exactly Ticks records.
    [switch]$HoldAtTicks,
    # Audio session volume of the game in percent for unattended runs;
    # 100 leaves it untouched, -Interactive never changes it.
    [ValidateRange(0,100)][int]$VolumePercent = $(if ($env:ES2_ORACLE_VOLUME_PERCENT) { [int]$env:ES2_ORACLE_VOLUME_PERCENT } else { 10 }),
    # Headless replay window ([window] hidden=1, proxy/window_shim.h). Applies
    # to replay runs without -Interactive only; there the volume default becomes 0.
    # run-oracle-regression.ps1 hands its decision to this runner through
    # ES2_ORACLE_HIDDEN_WINDOW ('1' hidden, '0' visible).
    [switch]$HiddenWindow = ($env:ES2_ORACLE_HIDDEN_WINDOW -eq '1'),
    # Extra DBSIM arguments for start-timing-mission.ps1 (appended as given;
    # quote each element, for example '-ptapes\demo1'). Checked before anything
    # is configured; recorded in the launch report and in this script's JSON line.
    [string[]]$GameArguments = @()
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
# One bounded measurement run in a measurement clone: deploy the tick-log proxy
# (optionally with virtual time), launch the prepared mission with the original
# DBSIM arguments, wait for the requested number of logged ticks, then stop
# only that DBSIM process after checking executable path and start time.
# -Hooks replaces the clone's hook selection; omission clears old selections.
# Compare mode defaults its mismatch output to <tick-log-path>.hooks.jsonl.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
$runTimer = [Diagnostics.Stopwatch]::StartNew()
function Assert-HookRunOutputPaths {
    param([string]$MismatchLog, [string[]]$ReservedPaths)
    if (!$MismatchLog) { return }
    $normalizedLog = [IO.Path]::GetFullPath($MismatchLog)
    foreach ($reserved in $ReservedPaths) {
        if ([string]::Equals($normalizedLog, [IO.Path]::GetFullPath($reserved), [StringComparison]::OrdinalIgnoreCase)) {
            throw "HookMismatchLog conflicts with a reserved run output: $reserved"
        }
    }
}
$GameArguments = @($GameArguments)
Assert-GameArguments $GameArguments
# Record mode and interactive play keep the visible window and the focus pause.
$hidden = [bool]$HiddenWindow -and !$Interactive -and $InputMode -eq 'replay'
if ($hidden -and !$PSBoundParameters.ContainsKey('VolumePercent') -and !$env:ES2_ORACLE_VOLUME_PERCENT) { $VolumePercent = 0 }
$windowSplat = @{}
if ($hidden) { $windowSplat.HiddenWindow = $true }
# Splatted only when given, so a run without extra arguments calls the launcher without -GameArguments.
$gameSplat = @{}
if ($GameArguments.Count) { $gameSplat.GameArguments = $GameArguments }
$holdSplat = @{}
if ($HoldAtTicks) { $holdSplat.HoldFrame = $Ticks }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$log = Join-Path $OutputDirectory "$RunName.jsonl"
$report = Join-Path $OutputDirectory "$RunName-launch.json"
# The launcher reserves its report before starting DBSIM, and a stall snapshot
# needs a directory at its own path. Neither can double as a mismatch log.
Assert-HookRunOutputPaths -MismatchLog $HookMismatchLog -ReservedPaths @($report, (Join-Path $OutputDirectory "$RunName-snapshot"))
if ($BufferedTickLog -and $FlushIntervalMs -ge $StallSeconds*1000) { throw 'FlushIntervalMs must be shorter than the stall interval.' }
# Build the snapshot helper before the launch, not while the game is stalled.
$snapshotTool = $null
if ($SnapshotOnStall) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $snapshotTool = if ($PreparedProxy) { (Get-Es2PreparedProxy $PreparedProxy).snapshot } else {
        Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:hang_snapshot' `
            -Destination ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../build/reference-game')))
    }
}
# The volume helper sets the game's audio session from outside the process;
# it never touches the game's memory, so compared state cannot change.
$volumeTool = $null
if (!$Interactive -and $VolumePercent -lt 100) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $volumeTool = if ($PreparedProxy) { (Get-Es2PreparedProxy $PreparedProxy).volume } else {
        Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:session_volume' `
            -Destination ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../build/reference-game')))
    }
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& (Join-Path $PSScriptRoot 'configure-tick-log.ps1') -DataRoot $DataRoot -InstallName $InstallName -TickLogPath $log `
    -VirtualTimeStepMs $VirtualTimeStepMs -VirtualTimePace $VirtualTimePace -VirtualTimeIntraFrame $VirtualTimeIntraFrame -InputMode $InputMode -InputJournal $InputJournal -Watch $Watch -Hooks $Hooks -HookMismatchLog $HookMismatchLog -ActorRaw:$ActorRaw -SkipTests:$SkipTests `
    -PreparedProxy $PreparedProxy -BufferedTickLog:$BufferedTickLog -TickTarget $Ticks -FlushIntervalMs $FlushIntervalMs -CompactRng:$CompactRng @windowSplat @holdSplat -Apply | Select-Object -Last 1
$configuredMs = $runTimer.Elapsed.TotalMilliseconds
& (Join-Path $PSScriptRoot 'start-timing-mission.ps1') -DataRoot $DataRoot -InstallName $InstallName -OutputPath $report -Interactive:$Interactive @gameSplat | Select-Object -Last 1
$launch = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
$launchedMs = $runTimer.Elapsed.TotalMilliseconds
# Polls until DBSIM opens its audio device and exits with the game (or after
# the run's timeout); its one-line JSON report lands next to the tick log.
if ($volumeTool -and $launch.pid) {
    $null = Start-Process -FilePath $volumeTool -NoNewWindow -PassThru `
        -ArgumentList @('--pid', [string]$launch.pid, '--percent', [string]$VolumePercent, '--wait-ms', [string](($TimeoutSeconds + 30) * 1000)) `
        -RedirectStandardOutput (Join-Path $OutputDirectory "$RunName-volume.json")
}
$pollMs = 0.0
$cursor = New-TickCursor $log -CompactRng:$CompactRng
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
# A loop that stops logging while the process lives (for example a modal in-game
# dialog waiting for input that the replay does not provide) is reported as a stall.
$progressState = @{Previous=-1;Progress=(Get-Date);Stalled=$false}; $stalled=$false
# Hidden runs: Process.MainWindowHandle is nonzero only for a visible top-level window.
$windowVisibleSeen = $false
do {
    Start-Sleep -Milliseconds 500
    $pollTimer = [Diagnostics.Stopwatch]::StartNew()
    $logged = Read-TickCursor $cursor
    $pollMs += $pollTimer.Elapsed.TotalMilliseconds
    $alive = Get-Process -Id $launch.pid -ErrorAction SilentlyContinue
    if ($hidden -and $alive -and $alive.MainWindowHandle -and $alive.MainWindowHandle -ne [IntPtr]::Zero) { $windowVisibleSeen = $true }
    Update-TickProgress $progressState $logged (Get-Date) $StallSeconds
    $stalled = $progressState.Stalled
} while ($logged -lt $Ticks -and $alive -and !$stalled -and (Get-Date) -lt $deadline)
$process = Get-CimInstance Win32_Process -Filter "ProcessId=$($launch.pid)"
$stopped = $false; $snapshot = $null
$identified = $process -and $process.ExecutablePath -eq $launch.exe -and
    ([DateTime]$process.CreationDate).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss') -eq ([DateTime]$launch.process_start_utc).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss')
# A stalled loop is usually a modal game screen; a read-only snapshot (windows,
# thread contexts, full-memory minidump) preserves its state before the stop.
if ($identified -and $BufferedTickLog) {
    Request-TickFlush -ProcessId $launch.pid
    $logged = Read-TickCursor $cursor
    # Refresh identity after waiting for the acknowledgement, before any stop.
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$($launch.pid)"
    $identified = $process -and $process.ExecutablePath -eq $launch.exe -and
        ([DateTime]$process.CreationDate).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss') -eq ([DateTime]$launch.process_start_utc).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss')
}
if ($identified -and $stalled -and $SnapshotOnStall) {
    $snapshot = Join-Path $OutputDirectory "$RunName-snapshot"
    & $snapshotTool --pid $launch.pid --exe $launch.exe --output $snapshot | Out-Null
    if ($LASTEXITCODE -ne 0) { $snapshot = "failed ($LASTEXITCODE)" }
}
if ($identified) {
    # A snapshot can take time: recheck identity immediately before the stop.
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$($launch.pid)"
    if ($process -and $process.ExecutablePath -eq $launch.exe -and
        ([DateTime]$process.CreationDate).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss') -eq ([DateTime]$launch.process_start_utc).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss')) {
        # The game can end on its own between the identity check and the stop (an
        # outcome suite under compare mode, check-v09-compare-dts c3_09): a process
        # that is gone by then was not stopped; any other failure still throws.
        try {
            Stop-Process -Id $launch.pid -ErrorAction Stop
            $stopped = $true
        } catch {
            if (Get-Process -Id $launch.pid -ErrorAction SilentlyContinue) { throw }
        }
    }
}
[ordered]@{schema=1;configure_ms=$configuredMs;launch_ms=($launchedMs-$configuredMs);poll_read_ms=$pollMs
    poll_bytes=$cursor.BytesRead;total_ms=$runTimer.Elapsed.TotalMilliseconds} | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $OutputDirectory "$RunName-runner-timing.json") -Encoding UTF8
$final = [ordered]@{run=$RunName; ticks_seen=$logged; stalled=$stalled; process_alive_at_end=[bool]$alive; stopped_after_identity_check=$stopped
    virtual_time_step_ms=$VirtualTimeStepMs; virtual_time_pace=$VirtualTimePace; virtual_time_intra_frame=$VirtualTimeIntraFrame; input_mode=$InputMode; input_journal=$InputJournal; watches=$Watch.Count; snapshot=$snapshot; pid=$launch.pid}
if ($GameArguments.Count) { $final.game_arguments = $GameArguments }
if ($HoldAtTicks) { $final.hold_frame = $Ticks }
# Only a hidden run carries the keys: the line is otherwise unchanged.
if ($hidden) { $final.hidden_window = $true; $final.window_visible_seen = $windowVisibleSeen; $final.volume_percent = $VolumePercent }
$final | ConvertTo-Json -Compress
