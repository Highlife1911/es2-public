[CmdletBinding()]
param(
    [string]$DataRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    [Parameter(Mandatory)][string]$TickLogPath,
    [int]$MaxRecords = 0,
    [switch]$ActorRaw,
    [int]$VirtualTimeStepMs = 0,
    [int]$VirtualTimeStartMs = 16777216,
    [int]$VirtualTimePace = 1,
    # 1: the virtual clock also moves inside a frame (cockpit instruments need it).
    [ValidateSet(0,1)][int]$VirtualTimeIntraFrame = 1,
    [ValidateSet('off', 'record', 'replay')][string]$InputMode = 'off',
    [string]$InputJournal = '',
    [string[]]$Watch = @(),
    [switch]$SkipTests,
    [switch]$Apply,
    [string]$Hooks = '',
    [string]$HookMismatchLog = '',
    [string]$PreparedProxy = $env:ES2_PREPARED_PROXY,
    [switch]$BufferedTickLog = ($env:ES2_TICK_BUFFERED -eq '1'),
    [ValidateRange(0,1000000)][int]$TickTarget = 0,
    [ValidateRange(1,1000)][int]$FlushIntervalMs = 100,
    [switch]$CompactRng,
    # [trace] hold_frame=N (proxy/tick_log.h): the game thread parks after the N-th
    # tick record, before the next frame starts. 0 writes no key.
    [ValidateRange(0,1000000)][int]$HoldFrame = 0,
    # [window] hidden=1, the headless replay window of proxy/window_shim.h
    # (no WS_VISIBLE, no window/cursor moves). Replay only; written only when set.
    [switch]$HiddenWindow,
    # proxy/early_arming.h, mission_load.md section 13: [arming] early=1, the
    # proxy arms at the call of the mission load 0x45f42b instead of at the first
    # MCI call inside it. On by default (33 live suites identical with every hook
    # original and with every hook replaced); ES2_EARLY_ARMING
    # = '0' or -EarlyArming:$false keeps the first MCI call (the INI then has no [arming]).
    [switch]$EarlyArming = ($env:ES2_EARLY_ARMING -ne '0'),
    # [snapshot] after_load=1, the after-load snapshot at the first logic frame,
    # written to <tick log path without .jsonl>-after-load.snap. Default from
    # ES2_AFTER_LOAD_SNAPSHOT ('1'); written only when set.
    [switch]$AfterLoadSnapshot = ($env:ES2_AFTER_LOAD_SNAPSHOT -eq '1'),
    # With -AfterLoadSnapshot: extra fixed regions `va:size[,va:size...]` for [snapshot] extra (the proxy's
    # ea_parse_extra checks them again; a refused list writes no snapshot). Default from ES2_AFTER_LOAD_SNAPSHOT_EXTRA.
    [string]$AfterLoadSnapshotExtra = $env:ES2_AFTER_LOAD_SNAPSHOT_EXTRA,
    # The VSHELL-only measures of the proxy ([video] system_indeo=1, the process-local IV32 binding for VSHELL's
    # intro movies; [compat] mission_map_lifetime=1, the mission-map lifetime guard in VSHELL.EXE). OFF by default: this
    # script configures DBSIM runs (the oracle suites, the own shell's chain), which use neither. Set it (or
    # ES2_VSHELL_MEASURES=1) for a clone on which the original VSHELL.EXE is started (start-own-menu.ps1 -Shell vshell).
    [switch]$VshellMeasures = ($env:ES2_VSHELL_MEASURES -eq '1'),
    # proxy/input_journal.h, main_loop.md "the message trace of the replay pump": [input] message_trace=1, the trace of every message
    # DBSIM's PeekMessageA calls remove, written to <tick log>.messages.ndjson. Off by default; default from
    # ES2_MESSAGE_TRACE ('1'), so the runners that call this script (run-oracle-parallel.ps1, capture-dbsim-frames.ps1)
    # pass it through their environment. Written only when set: without it the INI keeps its previous bytes.
    [switch]$MessageTrace = ($env:ES2_MESSAGE_TRACE -eq '1'),
    # With -Apply: wait up to this long for game processes in the clone to exit
    # by themselves (a just-stopped DBSIM still holds ddraw.dll); never stops one.
    [ValidateRange(0,600)][int]$BusyWaitSeconds = 15
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
# Deploys the own-code proxy with the in-process DBSIM tick log to a separate
# MEASUREMENT CLONE only. The primary installation is refused. Runs the own
# fixture tests and builds the proxy first; the log path must not exist yet.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
Assert-MeasurementName $InstallName

# Pure configuration construction: reads output-path metadata but does not
# deploy, create a log, run a fixture, or access an installation.
function Get-TickLogHookConfiguration {
    param([string]$Specification, [string]$MismatchLog, [string]$TickLog, [string]$Journal = '', [string[]]$ReservedOutputs = @())
    $modes = [ordered]@{}
    if (!$Specification) {
        if ($MismatchLog) { throw 'HookMismatchLog requires a nonempty Hooks selection.' }
        return [pscustomobject]@{ Text=''; Modes=$modes; MismatchLog=''; MismatchLimit=32 }
    }
    $hookNames = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../proxy/hook-names.txt')
    $namePattern = ($hookNames | ForEach-Object { [regex]::Escape($_) }) -join '|'
    foreach ($entry in $Specification.Split(',')) {
        $match = [regex]::Match($entry, ('^\s*(' + $namePattern + ')\s*=\s*(original|replaced|compare)\s*$'), [Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if (!$match.Success) { throw "Malformed hook selection: $entry. Use a hook name documented in hooks.md with original, replaced, or compare." }
        $name = $match.Groups[1].Value.ToLowerInvariant()
        if ($modes.Contains($name)) { throw "Duplicate hook selection: $name" }
        $modes[$name] = $match.Groups[2].Value.ToLowerInvariant()
    }
    if (!$MismatchLog -and $modes.Values -contains 'compare') { $MismatchLog = $TickLog + '.hooks.jsonl' }
    if ($MismatchLog) {
        if ($MismatchLog -match '[\x00-\x1f"<>|?*]' -or
            $MismatchLog -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+[\\/])' -or
            $MismatchLog.StartsWith('\\?\') -or $MismatchLog.StartsWith('\\.\')) {
            throw 'HookMismatchLog must be an absolute normal Windows file path without control characters or invalid filename characters.'
        }
        # Check the supplied spelling before GetFullPath normalizes Windows'
        # trailing-dot/space aliases into a different filename.
        $pathForValidation = $MismatchLog.Replace('/', '\')
        $relativePart = $pathForValidation.Substring([IO.Path]::GetPathRoot($pathForValidation).Length)
        foreach ($component in $relativePart.Split([char]'\')) {
            if (!$component -or $component.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
                $component.EndsWith('.') -or $component.EndsWith(' ') -or
                $component -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
                throw 'HookMismatchLog contains an invalid or reserved Windows path component.'
            }
        }
        $MismatchLog = [IO.Path]::GetFullPath($MismatchLog)
        if ($MismatchLog.Length -ge 260) { throw 'HookMismatchLog must fit the proxy MAX_PATH buffer (fewer than 260 characters).' }
        if ([string]::Equals($MismatchLog, [IO.Path]::GetFullPath($TickLog), [StringComparison]::OrdinalIgnoreCase) -or
            ($Journal -and [string]::Equals($MismatchLog, [IO.Path]::GetFullPath($Journal), [StringComparison]::OrdinalIgnoreCase))) {
            throw 'HookMismatchLog must be distinct from the tick log and input journal.'
        }
        foreach ($reserved in $ReservedOutputs) {
            if ([string]::Equals($MismatchLog, [IO.Path]::GetFullPath($reserved), [StringComparison]::OrdinalIgnoreCase)) {
                throw "HookMismatchLog conflicts with another configured output: $reserved"
            }
        }
        if (Test-Path -LiteralPath $MismatchLog) { throw "Hook mismatch log already exists: $MismatchLog" }
        if (!(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($MismatchLog)) -PathType Container)) {
            throw 'Hook mismatch log parent directory is missing.'
        }
    }
    $text = "[hooks]`r`n"
    foreach ($name in $modes.Keys) { $text += "$name=$($modes[$name])`r`n" }
    if ($MismatchLog) { $text += "mismatch_log=$MismatchLog`r`n" }
    $text += "mismatch_limit=32`r`n"
    return [pscustomobject]@{ Text=$text; Modes=$modes; MismatchLog=$MismatchLog; MismatchLimit=32 }
}

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
Assert-NoLinks $installation
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$manifest = Get-Content -LiteralPath (Join-Path $installation 'installation-manifest.json') -Raw | ConvertFrom-Json
if (!$manifest.measurement_clone_of -or $manifest.measurement_clone_of -eq $installation) {
    throw 'The tick log is only deployed to a separate measurement clone.'
}
if ($Apply) {
    # Before any build or file change (CloneGuard.psm1): another session's run
    # holds the clone, or a game still runs in it after the wait.
    Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
    Assert-CloneIdle $installation 'configuring it' -WaitSeconds $BusyWaitSeconds
}
if ($HiddenWindow -and $InputMode -ne 'replay') { throw 'HiddenWindow applies to replay runs only; record and interactive runs keep the visible window.' }
$TickLogPath = [IO.Path]::GetFullPath($TickLogPath)
if (Test-Path -LiteralPath $TickLogPath) { throw "Tick log already exists: $TickLogPath" }
if (!(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($TickLogPath)) -PathType Container)) { throw 'Tick log parent directory is missing.' }
$snapshotPath = if ($AfterLoadSnapshot) { ($TickLogPath -replace '\.jsonl$', '') + '-after-load.snap' } else { $null }
if ($snapshotPath -and (Test-Path -LiteralPath $snapshotPath)) { throw "After-load snapshot already exists: $snapshotPath" }
$messageTracePath = if ($MessageTrace) { $TickLogPath + '.messages.ndjson' } else { $null }
if ($messageTracePath -and $messageTracePath.Length -ge 260) { throw 'The message trace path must fit the proxy MAX_PATH buffer (fewer than 260 characters).' }
if ($messageTracePath -and (Test-Path -LiteralPath $messageTracePath)) { throw "Message trace already exists: $messageTracePath" }
$region = '0x[0-9a-fA-F]+:(0x)?[0-9a-fA-F]+'
if ($AfterLoadSnapshotExtra -and ($AfterLoadSnapshotExtra -replace '\s', '') -notmatch "^$region(,$region)*$") {
    throw "-AfterLoadSnapshotExtra is a list of va:size, not '$AfterLoadSnapshotExtra'"
}
$snapshotExtra = if ($snapshotPath -and $AfterLoadSnapshotExtra) { "extra=$($AfterLoadSnapshotExtra -replace '\s', '')`r`n" } else { '' }
$hookConfiguration = Get-TickLogHookConfiguration -Specification $Hooks -MismatchLog $HookMismatchLog -TickLog $TickLogPath -Journal $InputJournal `
    -ReservedOutputs @((Join-Path $DataRoot 'logs/video-compat.jsonl'))

# -SkipTests is for repeated runs of one session after a run with the full
# fixture suite; the proxy is always rebuilt.
$prepared = if ($PreparedProxy) { Get-Es2PreparedProxy $PreparedProxy } else { $null }
if (!$prepared -and !$SkipTests) { Invoke-Es2FixtureTests }
if ($InputMode -ne 'off') {
    if (!$InputJournal) { throw 'InputJournal is required for record or replay.' }
    $InputJournal = [IO.Path]::GetFullPath($InputJournal)
    if ($InputMode -eq 'record' -and (Test-Path -LiteralPath $InputJournal)) { throw "Input journal already exists: $InputJournal" }
    if ($InputMode -eq 'replay' -and !(Test-Path -LiteralPath $InputJournal)) { throw "Input journal missing: $InputJournal" }
}
$source = if ($prepared) { $prepared.proxy } else {
    Copy-Es2BazelOutput -Target '//tools/reference-game/proxy:ddraw.dll' -Destination (Join-Path $repository 'build/reference-game/proxy-ticklog')
}
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$systemHash = (Get-FileHash -LiteralPath (Join-Path ([Environment]::GetFolderPath('Windows')) 'SysWOW64/ddraw.dll') -Algorithm SHA256).Hash

$target = Join-Path $installation 'ddraw.dll'
$config = Join-Path $installation 'es2-oracle.ini'
$mediaManifestPath = Join-Path $installation 'oracle-media-manifest.json'
$old = Get-Content -LiteralPath $mediaManifestPath -Raw | ConvertFrom-Json
foreach ($item in @(@{path=$target;hash=$old.proxy_sha256}, @{path=$config;hash=$old.config_sha256})) {
    if ((Test-Path -LiteralPath $item.path) -and (Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash -ne $item.hash) {
        throw "Refusing to overwrite an unknown or edited file: $($item.path)"
    }
}
$logRoot = Join-Path $DataRoot 'logs'
# Memory watches (proxy/mem_watch.h): "NAME ADDRESS LENGTH" each; checked by the proxy.
if ($Watch.Count -gt 48) { throw 'At most 48 watches.' }
$watchText = "watch_count=$($Watch.Count)`r`n"
for ($i = 0; $i -lt $Watch.Count; ++$i) {
    if ($Watch[$i] -notmatch '^[A-Za-z0-9_]+ 0x[0-9a-fA-F]+(@0x[0-9a-fA-F]+|@[0-9]+)* (w0x[0-9a-fA-F]+\*)?[0-9]+$') { throw "Malformed watch: $($Watch[$i])" }
    $watchText += "watch$($i + 1)=$($Watch[$i])`r`n"
}
$text = "[video]`r`nsystem_indeo=1`r`nlog=$logRoot\video-compat.jsonl`r`n[compat]`r`nmission_map_lifetime=1`r`n[trace]`r`ntick_log=$TickLogPath`r`ntick_log_max=$MaxRecords`r`ntick_log_actor_raw=$([int][bool]$ActorRaw)`r`n$watchText[time]`r`nvirtual_time_step_ms=$VirtualTimeStepMs`r`nvirtual_time_start_ms=$VirtualTimeStartMs`r`nvirtual_time_pace=$VirtualTimePace`r`nvirtual_time_intra_frame=$VirtualTimeIntraFrame`r`n[input]`r`nmode=$InputMode`r`njournal=$InputJournal`r`n"
# Without -VshellMeasures the two VSHELL-only keys are not written (the proxy's defaults are off).
if (!$VshellMeasures) {
    $text = $text.Replace("system_indeo=1`r`n", '').Replace("[compat]`r`nmission_map_lifetime=1`r`n", '')
}
# The message trace only with -MessageTrace (or ES2_MESSAGE_TRACE=1), as the last key of [input].
if ($messageTracePath) { $text = $text.Replace("journal=$InputJournal`r`n", "journal=$InputJournal`r`nmessage_trace=1`r`n") }
# This script replaces the whole INI. Omitted -Hooks deliberately removes any
# previous [hooks] section, restoring default original-only behavior.
$text += $hookConfiguration.Text
# [arming] by default (-EarlyArming:$false or ES2_EARLY_ARMING=0 omit it), [snapshot] only with -AfterLoadSnapshot.
if ($EarlyArming) { $text += "[arming]`r`nearly=1`r`n" }
if ($snapshotPath) { $text += "[snapshot]`r`nafter_load=1`r`npath=$snapshotPath`r`n$snapshotExtra" }
# Only with -HiddenWindow: without it the INI keeps its previous bytes.
if ($HiddenWindow) { $text += "[window]`r`nhidden=1`r`n" }
if ($HoldFrame) { $text = $text.Replace("[trace]`r`n", "[trace]`r`nhold_frame=$HoldFrame`r`n") }
if ($CompactRng) { $text = $text.Replace("[trace]`r`n", "[trace]`r`ntick_log_compact_rng=1`r`n") }
if ($BufferedTickLog) {
    $text = $text.Replace("[trace]`r`n", "[trace]`r`ntick_log_buffered=1`r`ntick_log_target=$TickTarget`r`ntick_log_flush_ms=$FlushIntervalMs`r`n")
}
if (!$Apply) {
    Write-Output "PREVIEW: deploy proxy $sourceHash to $installation with tick log $TickLogPath (max $MaxRecords, actor raw $([bool]$ActorRaw), virtual time step $VirtualTimeStepMs ms, pace $VirtualTimePace, intra-frame $VirtualTimeIntraFrame)."
    Write-Output "PREVIEW: VSHELL-only measures ([video] system_indeo, [compat] mission_map_lifetime) $(if ($VshellMeasures) { 'ON (-VshellMeasures)' } else { 'off (default)' })."
    if ($HiddenWindow) { Write-Output 'PREVIEW: hidden replay window ([window] hidden=1).' }
    if ($HoldFrame) { Write-Output "PREVIEW: game thread parks after tick record $HoldFrame ([trace] hold_frame)." }
    if ($EarlyArming) { Write-Output 'PREVIEW: arming at the mission load ([arming] early=1).' }
    if ($snapshotPath) { Write-Output "PREVIEW: after-load snapshot $snapshotPath ([snapshot] after_load=1)." }
    if ($messageTracePath) { Write-Output "PREVIEW: message trace $messageTracePath ([input] message_trace=1)." }
    if ($hookConfiguration.Modes.Count) {
        Write-Output "PREVIEW: hooks $Hooks; mismatch log '$($hookConfiguration.MismatchLog)'; mismatch limit $($hookConfiguration.MismatchLimit)."
    }
    return
}
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
Assert-CloneIdle $installation 'configuring it'
Copy-Item -LiteralPath $source -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Deployed proxy hash mismatch.' }
[IO.File]::WriteAllText($config, $text, [Text.Encoding]::Unicode)
$record = [ordered]@{
    schema=3; kind='es2-own-code-media-proxy'; utc=[DateTime]::UtcNow.ToString('o')
    proxy_sha256=$sourceHash; config_sha256=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash
    system_ddraw_sha256=$systemHash; mission_map_guard=[bool]$VshellMeasures; vshell_measures=[bool]$VshellMeasures
    mission_map_guard_header_sha256=(Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/mission_map_guard.h') -Algorithm SHA256).Hash
    tick_log=$true; tick_log_header_sha256=(Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/tick_log.h') -Algorithm SHA256).Hash
    tick_log_path=$TickLogPath; watches=@($Watch)
    mem_watch_header_sha256=(Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/mem_watch.h') -Algorithm SHA256).Hash
    previous_proxy_sha256=$old.proxy_sha256
    virtual_time_step_ms=$VirtualTimeStepMs; virtual_time_start_ms=$VirtualTimeStartMs; virtual_time_pace=$VirtualTimePace; virtual_time_intra_frame=$VirtualTimeIntraFrame
    virtual_time_header_sha256=(Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/virtual_time.h') -Algorithm SHA256).Hash
    fixture_tests_run=(!$SkipTests -or [bool]$prepared); input_mode=$InputMode; input_journal=$InputJournal
    prepared_proxy=$PreparedProxy; tick_log_buffered=[bool]$BufferedTickLog; tick_log_target=$TickTarget; tick_log_flush_ms=$FlushIntervalMs; tick_log_compact_rng=[bool]$CompactRng
    input_journal_header_sha256=(Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/input_journal.h') -Algorithm SHA256).Hash
    media_decode_validation='not repeated for this build; video path unchanged, DBSIM launched directly'
    external_dependency_installed=$false; system_codec_registry_changed=$false
}
if ($HoldFrame) { $record.hold_frame = $HoldFrame }
if ($HiddenWindow) {
    $record.hidden_window = $true
    $record.window_shim_header_sha256 = (Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/window_shim.h') -Algorithm SHA256).Hash
}
if ($EarlyArming -or $snapshotPath) {
    $record.early_arming = [bool]$EarlyArming
    if ($snapshotPath) { $record.after_load_snapshot = $snapshotPath }
    $record.early_arming_header_sha256 = (Get-FileHash -LiteralPath (Join-Path $repository 'tools/reference-game/proxy/early_arming.h') -Algorithm SHA256).Hash
}
if ($messageTracePath) { $record.message_trace = $messageTracePath }
if ($hookConfiguration.Modes.Count) {
    $record.hooks = $hookConfiguration.Modes
    $record.hook_mismatch_log = $hookConfiguration.MismatchLog
    $record.hook_mismatch_limit = $hookConfiguration.MismatchLimit
}
[IO.File]::WriteAllText($mediaManifestPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Output "Tick-log proxy deployed to measurement clone: $installation"
