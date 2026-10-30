[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # Measurement clones (new-measurement-clone.ps1), one suite at a time on each; native-r111 is refused.
    [string[]]$InstallName = @('timing-r111-p1', 'timing-r111-p2'),
    # Suites of the registry; default: every registered suite.
    [string[]]$Suite = @(),
    # Where dbsim-frames-<date>[-<label>] is created; default <DataRoot>\logs.
    [string]$EvidenceRoot = '',
    # yyyyMMdd of the evidence directory; default today (UTC).
    [string]$Date = '',
    # Names a run other than the baseline; required with -Hooks (the baseline has every hook original).
    [string]$Label = '',
    # Hook modes as run-oracle-parallel.ps1 -Hooks. Default: none, every hook original.
    [string]$Hooks = '',
    # Dumps of the 8-bit frames (ES2F files with the colour table) into <ScratchRoot>\dumps, never into the
    # evidence: decoded game frames. Needs a selection:
    #   -DumpFrames 100,278,279    these frames (at most 64)
    #   -DumpRange 100-400/4       from-to[/step]: at most 4096 frames, 8192 dumps (proxy/presenter.h)
    [switch]$Dump,
    [string[]]$DumpFrames = @(),
    [string]$DumpRange = '',
    # The region hashes per record (proxy/frame_regions.h; [presentation] capture_regions=1).
    [switch]$Regions,
    # A QueryPerformanceCounter stamp per record ([presentation] capture_qpc=1; capture schema 2, the start record
    # gets qpc_frequency). Needs a -Label (the baseline directory holds the run without it). The raw capture is copied to
    # <evidence>\<suite>\capture.ndjson, because the work directory is deleted: read it with
    # scripts/dbsim_present_times.py (own_process.md, "a time per DBSIM capture record"). Changes no table, no hash, no tick field.
    [switch]$Qpc,
    # DBSIM's DirectDraw branch in a window: [presentation] directdraw=windowed in the capture INI (proxy/ddraw_windowed.h) and
    # the game argument -Z1 (sets [0x4d255e], 0x45e706), so DBSIM takes the branch from its start; the capture then holds the
    # DirectDraw presents (source "surface"). Needs a -Label (the baseline directory holds the GDI-branch run). With -Qpc the
    # stamp is read on entry to ddw_present. The run is compared with the goldens of the GDI branch as usual: a different
    # result is recorded, not hidden. Nothing on the clone is changed (INI and argument are per run).
    [switch]$WindowedDirectDraw,
    # The call probe (proxy/call_probe.h; [call_probe] in the capture INI; own_process.md, "the free camera's shape
    # edge", its live request and the tool paragraph). -CallProbeVa switches it on and needs -Label, -CallProbeFrames, -CallProbePointer
    # and -CallProbeLength; the proxy checks the syntax and refuses a malformed value with a "refused" record. Its
    # report <tick log>.call-probe.ndjson is copied to <evidence>\<suite>\call-probe.ndjson.
    [string]$CallProbeVa = '',
    [string]$CallProbeFrames = '',
    [string]$CallProbeWhen = '',
    [string]$CallProbePointer = '',
    [string]$CallProbeOffset = '0',
    [string]$CallProbeLength = '',
    [string]$CallProbeMaxDumps = '',
    # Ignored scratch for the dumps; default <repository>\build\dbsim-frames-<date>[-<label>].
    [string]$ScratchRoot = '',
    [string]$GoldenRoot = '',
    [string]$PreparedProxy = '',
    [string]$Python = '',
    # Keep the runner's output (raw tick logs, captures) in <DataRoot>\logs\dbsim-frames-work-<stamp> after
    # a run in which every suite is ok; a run with a suite that is not ok always keeps it.
    [switch]$KeepWork,
    [switch]$HiddenWindow,
    # Alternative suite registry (a draft) and the mock child of run-oracle-parallel.ps1 (tests; synthetic DataRoot only).
    [string]$SuitesFile = '',
    [string]$MockDirectory = '',
    # Prints the plan (suites, clones, directories, the capture INI) and touches nothing.
    [switch]$DryRun
)
if (!$DataRoot -or !$GoldenRoot -or !$Python) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$GoldenRoot) { $GoldenRoot = (Join-Path $es2.DataRepository 'oracle\golden') }
    if (!$Python) { $Python = $es2.Python }
}
# The DBSIM frame capture per suite. Runs the suites through run-oracle-parallel.ps1 (check mode, every hook
# original unless -Hooks) with the proxy's presentation capture on (ES2_PRESENTATION_CONFIG, proxy/presenter.h:
# capture=1, capture_report=beside_tick_log), then turns each suite's capture report into
#   <EvidenceRoot>\dbsim-frames-<date>[-<label>]\<suite>\manifest.json   identity, counters, frame summary
#   <EvidenceRoot>\dbsim-frames-<date>[-<label>]\<suite>\frames.tsv.gz    one line per present: frame, site, ordinal,
#                                                                         page origin, SHA-256 of frame and colour table
# (scripts/dbsim_frames.py). The run must end identical to the goldens: the capture changes no tick field.
# Contract and use: docs/reverse-engineering/presentation.md, section "frame capture per suite".
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SuiteLaunch.psm1') -Force
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$utf8 = New-Object System.Text.UTF8Encoding($false)
$parallel = Join-Path $PSScriptRoot 'run-oracle-parallel.ps1'
$builder = Join-Path $PSScriptRoot '../scripts/dbsim_frames.py'

function Say([string]$Text) { Write-Host "[dbsim-frames] $Text" }
function Write-JsonFile([string]$Path, $Object) { [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), (($Object | ConvertTo-Json -Depth 8) + "`n"), $utf8) }
function Read-JsonFile([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json } catch { return $null }
}

# ---- arguments ---------------------------------------------------------------------------------------
# `pwsh -File script -InstallName a,b` hands the list over as the one string 'a,b' (oracle_regression.md, "More clones and longest-first dispatch").
function Split-List([string[]]$Values) { @($Values | ForEach-Object { $_ -split '[,\s]+' } | Where-Object { $_ }) }
$InstallName = @(Split-List $InstallName)
$Suite = @(Split-List $Suite)
$DumpFrames = @(Split-List $DumpFrames)
if (@($DumpFrames | Where-Object { $_ -notmatch '^\d{1,9}$' }).Count) { throw "-DumpFrames must be frame numbers: $($DumpFrames -join ' ')" }
$frameNumbers = @($DumpFrames | ForEach-Object { [int]$_ })   # $DumpFrames is a string[]: a cast back into it would stay text
if (!$Date) { $Date = [DateTime]::UtcNow.ToString('yyyyMMdd') }
if ($Date -notmatch '^\d{8}$') { throw "-Date must be yyyyMMdd: $Date" }
if ($Label -and $Label -notmatch '^[A-Za-z0-9][A-Za-z0-9_]*$') { throw "-Label must be a plain name (letters, digits, underscore): $Label" }
if ($Qpc -and !$Label) { throw 'A run with -Qpc needs a -Label: the baseline directory holds the run without time stamps.' }
if ($WindowedDirectDraw -and !$Label) { throw 'A run with -WindowedDirectDraw needs a -Label: the baseline directory holds the run of the GDI branch.' }
if ($Hooks -and !$Label) { throw 'A run with -Hooks needs a -Label: the baseline directory holds the run with every hook original.' }
$selection = $frameNumbers.Count -gt 0 -or $DumpRange
if ($Dump -and !$selection) { throw '-Dump needs a selection: -DumpFrames and/or -DumpRange (a dump of every frame is not offered).' }
if (!$Dump -and $selection) { throw '-DumpFrames/-DumpRange need -Dump.' }
if ($frameNumbers.Count -gt 64) { throw '-DumpFrames names at most 64 frames (proxy/presenter.h); use -DumpRange for a series.' }
if ($DumpRange) {
    if ($DumpRange -notmatch '^(\d+)-(\d+)(?:/(\d+))?$') { throw "-DumpRange must be from-to or from-to/step: $DumpRange" }
    $from = [int64]$Matches[1]; $to = [int64]$Matches[2]; $step = if ($Matches[3]) { [int64]$Matches[3] } else { 1 }
    if ($from -gt $to -or $step -lt 1 -or $to -ge [uint32]::MaxValue -or [Math]::Floor(($to - $from) / $step) -ge 4096) {
        throw "-DumpRange is empty, or names more than 4096 frames: $DumpRange"
    }
}
$callProbe = $null
if ($CallProbeVa) {
    if (!$Label) { throw 'A run with -CallProbeVa needs a -Label: the baseline directory holds the run without probes.' }
    if ($CallProbeVa -notmatch '^0x[0-9A-Fa-f]{1,8}$') { throw "-CallProbeVa must be one 0x hex address: $CallProbeVa" }
    if (!$CallProbeFrames -or !$CallProbePointer -or !$CallProbeLength) { throw '-CallProbeVa needs -CallProbeFrames, -CallProbePointer and -CallProbeLength.' }
    $callProbe = [ordered]@{va = $CallProbeVa; frames = $CallProbeFrames; when = $CallProbeWhen; pointer = $CallProbePointer
        offset = $CallProbeOffset; length = $CallProbeLength; max_dumps = $CallProbeMaxDumps}
    foreach ($key in @($callProbe.Keys)) {
        if ([string]$callProbe[$key] -match '[\r\n]') { throw "-CallProbe value '$key' must be one line." }
    }
} elseif ($CallProbeFrames -or $CallProbeWhen -or $CallProbePointer -or $CallProbeLength -or $CallProbeMaxDumps -or $CallProbeOffset -ne '0') {
    throw '-CallProbeFrames/-CallProbeWhen/-CallProbePointer/-CallProbeOffset/-CallProbeLength/-CallProbeMaxDumps need -CallProbeVa.'
}
if (!(Test-Path -LiteralPath $Python -PathType Leaf)) { throw "Python not found: $Python" }
foreach ($name in $InstallName) { Assert-SuiteName $name }
if (@($InstallName | Where-Object { $_ -ieq 'native-r111' }).Count) { throw 'native-r111 is the primary installation; use measurement clones.' }
if ($MockDirectory) { Assert-SuiteMockRoot $DataRoot }
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
$givenSuitesFile = [bool]$SuitesFile
$registryPath = if ($givenSuitesFile) { [IO.Path]::GetFullPath($SuitesFile) } else { Join-Path $PSScriptRoot '../oracle-suites.json' }
$registry = @((Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json).suites)
if (@($Suite | Where-Object { $_ -notin $registry.name }).Count) { throw 'Unknown suite.' }
$selected = @($registry | Where-Object { !$Suite.Count -or $_.name -in $Suite })
if (!$selected.Count) { throw 'No suite selected.' }
foreach ($s in $selected) { Assert-SuiteName $s.name }

$folder = "dbsim-frames-$Date" + $(if ($Label) { "-$Label" } else { '' })
if (!$EvidenceRoot) { $EvidenceRoot = Join-Path $DataRoot 'logs' }
$root = Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) $folder
if (!$ScratchRoot) { $ScratchRoot = Join-Path $repository "build\$folder" }
$ScratchRoot = [IO.Path]::GetFullPath($ScratchRoot)
$dumpDirectory = Join-Path $ScratchRoot 'dumps'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$work = Join-Path $DataRoot "logs\dbsim-frames-work-$stamp"
$runOutput = Join-Path $work 'run'
$iniPath = Join-Path $work 'presentation.ini'

$dumpLines = ''
if ($Dump) {
    if ($frameNumbers.Count) { $dumpLines += "capture_dump_frames=$($frameNumbers -join ' ')`r`n" }
    if ($DumpRange) { $dumpLines += "capture_dump_range=$DumpRange`r`n" }
    $dumpLines += "capture_dump_dir=$dumpDirectory`r`n"
}
if ($Regions) { $dumpLines += "capture_regions=1`r`n" }
if ($Qpc) { $dumpLines += "capture_qpc=1`r`n" }
if ($WindowedDirectDraw) { $dumpLines += "directdraw=windowed`r`n" }
$gameArguments = if ($WindowedDirectDraw) { @('-Z1') } else { @() }
$iniText = "[presentation]`r`ncapture=1`r`ncapture_report=beside_tick_log`r`n$dumpLines"
if ($callProbe) {
    $iniText += "[call_probe]`r`nenabled=1`r`nva=$($callProbe.va)`r`nframes=$($callProbe.frames)`r`nwhen=$($callProbe.when)`r`n" +
        "pointer=$($callProbe.pointer)`r`noffset=$($callProbe.offset)`r`nlength=$($callProbe.length)`r`n" +
        $(if ($callProbe.max_dumps) { "max_dumps=$($callProbe.max_dumps)`r`n" } else { '' }) + "report=beside_tick_log`r`n"
}
$dumpPlan = if ($Dump) { [ordered]@{frames = $frameNumbers; range = $DumpRange; directory = $dumpDirectory} } else { $null }

# A suite already in the evidence directory is never replaced: delete it first if that is meant.
$existing = @($selected | Where-Object { Test-Path -LiteralPath (Join-Path $root $_.name) })
if ($existing.Count) { throw "Already in ${root}: $(@($existing.name) -join ', '). Nothing was started." }

$plan = [ordered]@{evidence = $root; work = $work; scratch = $(if ($Dump) { $ScratchRoot } else { $null }); clones = @($InstallName)
    suites = @($selected.name); hooks = $Hooks; label = $Label; date = $Date; dump = $dumpPlan; call_probe = $callProbe; qpc = [bool]$Qpc
    windowed_directdraw = [bool]$WindowedDirectDraw; game_arguments = @($gameArguments); presentation_ini = $iniText}
if ($DryRun) {
    Say 'DRY RUN (nothing is created, started or changed).'
    $plan | ConvertTo-Json -Depth 6
    return
}

# ---- the run -----------------------------------------------------------------------------------------
$environment = Get-RunnerEnvironment
New-Item -ItemType Directory -Force -Path $root | Out-Null
New-Item -ItemType Directory -Force -Path $work | Out-Null
if ($Dump) {
    New-Item -ItemType Directory -Force -Path $dumpDirectory | Out-Null
    [IO.File]::WriteAllText((Join-Path $ScratchRoot '.gitignore'), "*`n", $utf8)
}
[IO.File]::WriteAllText($iniPath, $iniText, [Text.Encoding]::Unicode)
$parameters = @{DataRoot = $DataRoot; InstallName = $InstallName; Suite = @($selected.name); OutputDirectory = $runOutput
    GoldenRoot = $GoldenRoot; Python = $Python}
if ($Hooks) { $parameters.Hooks = $Hooks }
if ($gameArguments.Count) { $parameters.GameArguments = $gameArguments }
if ($PreparedProxy) { $parameters.PreparedProxy = $PreparedProxy }
if ($givenSuitesFile) { $parameters.SuitesFile = $registryPath }
if ($MockDirectory) { $parameters.MockDirectory = $MockDirectory }
if ($PSBoundParameters.ContainsKey('HiddenWindow')) { $parameters.HiddenWindow = [bool]$HiddenWindow }
$startedUtc = [DateTime]::UtcNow
$runnerFailure = $null
try {
    $env:ES2_PRESENTATION_CONFIG = $iniPath
    Say "Running $($selected.Count) suite(s) on $($InstallName -join ', ') (capture on, hooks '$Hooks') ..."
    & $parallel @parameters | Out-Null
    $global:LASTEXITCODE = 0
} catch {
    $runnerFailure = $_.Exception.Message
    Say "RUNNER FAILED: $runnerFailure"
} finally {
    Restore-RunnerEnvironment $environment
}
$finishedUtc = [DateTime]::UtcNow

# ---- the manifests -----------------------------------------------------------------------------------
$merged = Read-JsonFile (Join-Path $runOutput 'summary.json')
$dispatch = Read-JsonFile (Join-Path $runOutput 'dispatch.json')
$cloneOf = @{}
if ($dispatch) { foreach ($entry in @($dispatch.started)) { $name, $clone = ([string]$entry) -split '@', 2; $cloneOf[$name] = $clone } }
$results = [ordered]@{}
foreach ($s in $selected) {
    $suiteRun = Join-Path (Join-Path $runOutput $s.name) 'run'
    $captureFiles = @()
    if (Test-Path -LiteralPath $suiteRun) { $captureFiles = @(Get-ChildItem -LiteralPath $suiteRun -Recurse -File -Filter '*.capture.ndjson' | Sort-Object FullName) }
    $capture = if ($captureFiles.Count) { $captureFiles[0].FullName } else { Join-Path $suiteRun "$($s.name).jsonl.capture.ndjson" }
    $childSummary = Read-JsonFile (Join-Path $suiteRun 'summary.json')
    $entry = Read-JsonFile (Join-Path $suiteRun "$($s.name)-entry.json")
    $mergedResult = if ($merged) { @($merged.results | Where-Object { $_.suite -eq $s.name }) | Select-Object -First 1 } else { $null }
    $info = [ordered]@{
        result = $(if ($mergedResult) { [string]$mergedResult.result } else { 'no-result' })
        hooks = $Hooks; label = $Label; clone = $(if ($cloneOf.ContainsKey($s.name)) { $cloneOf[$s.name] } else { $null })
        commit = $(if ($childSummary) { $childSummary.commit } else { $null }); dirty_tree = $(if ($childSummary) { $childSummary.dirty_tree } else { $null })
        proxy_sha256 = $(if ($entry) { $entry.proxy_sha256 } else { $null })
        ticks = [int]$s.ticks; frame_limit = $(if ($s.frame_limit) { [int]$s.frame_limit } else { $null })
        golden_root = $GoldenRoot; started_utc = $startedUtc.ToString('o'); finished_utc = $finishedUtc.ToString('o')
    }
    if ($entry -and $entry.PSObject.Properties['hidden_window']) { $info.hidden_window = $entry.hidden_window }
    if ($entry -and $entry.PSObject.Properties['game_arguments']) { $info.game_arguments = $entry.game_arguments }
    if ($mergedResult -and $mergedResult.PSObject.Properties['exit_code']) { $info.exit_code = $mergedResult.exit_code; $info.exit_code_state = $mergedResult.exit_code_state }
    if ($captureFiles.Count -gt 1) { $info.capture_candidates = @($captureFiles.Name) }
    if ($runnerFailure) { $info.runner_error = $runnerFailure }
    if ($Dump) {
        $frames = Join-Path $dumpDirectory "$([IO.Path]::GetFileName($capture)).frames"
        $files = if (Test-Path -LiteralPath $frames) { @(Get-ChildItem -LiteralPath $frames -File -Filter '*.es2f') } else { @() }
        $info.dumps = [ordered]@{requested = $dumpPlan; directory = $frames; files = $files.Count
            bytes = [int64]($files | Measure-Object Length -Sum).Sum; committed = $false}
    }
    $probeFile = $null
    if ($Qpc) { $info.qpc = [ordered]@{capture_qpc = $true; capture = $(if (Test-Path -LiteralPath $capture -PathType Leaf) { 'capture.ndjson' } else { $null }); tool = 'scripts/dbsim_present_times.py'} }
    if ($WindowedDirectDraw) { $info.directdraw = 'windowed' }
    if ($callProbe) {
        $probeFiles = if (Test-Path -LiteralPath $suiteRun) { @(Get-ChildItem -LiteralPath $suiteRun -Recurse -File -Filter '*.call-probe.ndjson' | Sort-Object FullName) } else { @() }
        $probeFile = if ($probeFiles.Count) { $probeFiles[0].FullName } else { $null }
        $probeLines = if ($probeFile) { @(Get-Content -LiteralPath $probeFile | Where-Object { $_ }) } else { @() }
        $info.call_probe = [ordered]@{requested = $callProbe; report = $(if ($probeFile) { 'call-probe.ndjson' } else { $null })
            dumps = @($probeLines | Where-Object { $_ -match '"event":"dump"' }).Count
            refused = @($probeLines | Where-Object { $_ -match '"event":"refused"' }).Count
            ended = @($probeLines | Where-Object { $_ -match '"event":"end"' }).Count}
        if ($probeFiles.Count -gt 1) { $info.call_probe.candidates = @($probeFiles.Name) }
    }
    $infoPath = Join-Path $work "$($s.name)-run-info.json"
    Write-JsonFile $infoPath $info
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Python
    $psi.Arguments = '-B "' + $builder + '" build "' + $capture + '" "' + (Join-Path $root $s.name) + '" --suite ' + $s.name + ' --run-info "' + $infoPath + '" --date ' + $Date
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($psi)
    $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $code = $process.ExitCode; $process.Dispose()
    $manifest = Read-JsonFile (Join-Path (Join-Path $root $s.name) 'manifest.json')
    if (!$manifest) { throw "dbsim_frames.py wrote no manifest for $($s.name) (exit $code): $($err.Result)$($out.Result)" }
    if ($Qpc -and (Test-Path -LiteralPath $capture -PathType Leaf)) { Copy-Item -LiteralPath $capture -Destination (Join-Path (Join-Path $root $s.name) 'capture.ndjson') }
    if ($probeFile) { Copy-Item -LiteralPath $probeFile -Destination (Join-Path (Join-Path $root $s.name) 'call-probe.ndjson') }
    $records = if ($manifest.PSObject.Properties['frames']) { $manifest.frames.records } else { 0 }
    $last = if ($manifest.PSObject.Properties['frames']) { $manifest.frames.last_frame } else { $null }
    $results[$s.name] = [ordered]@{status = [string]$manifest.status; result = $info.result; records = $records; last_frame = $last; clone = $info.clone}
    if ($callProbe) { $results[$s.name].call_probe_dumps = $info.call_probe.dumps; $results[$s.name].call_probe_report = [bool]$probeFile }
    Say ("{0}: {1} (result {2}, {3} record(s), last frame {4})" -f $s.name, $manifest.status, $info.result, $records, $last)
}
$failed = @($results.Keys | Where-Object { $results[$_].status -ne 'ok' })
$session = [ordered]@{schema = 1; kind = 'es2-dbsim-frames-session'; stamp = $stamp; evidence = $root; work = $work; clones = @($InstallName)
    hooks = $Hooks; label = $Label; dump = $dumpPlan; call_probe = $callProbe; qpc = [bool]$Qpc; windowed_directdraw = [bool]$WindowedDirectDraw; runner_error = $runnerFailure; suites = $results}
Write-JsonFile (Join-Path $root "session-$stamp.json") $session
if ($failed.Count -or $KeepWork) {
    Say "Runner output kept in $work$(if ($failed.Count) { " (not ok: $($failed -join ', '))" } else { '' })."
} else {
    # The raw tick logs and captures are in this run's work directory only (a fresh directory of this script).
    Remove-Item -LiteralPath $work -Recurse -Force
}
Say "Manifests: $root"
if ($failed.Count -or $runnerFailure) { exit 1 }
exit 0
