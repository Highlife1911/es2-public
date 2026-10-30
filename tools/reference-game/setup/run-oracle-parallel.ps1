[CmdletBinding()]
param(
    [string]$DataRoot='',
    [string[]]$InstallName=@('timing-r111-p1','timing-r111-p2'),
    [string[]]$Suite=@(),
    [string]$OutputDirectory='',
    [string]$GoldenRoot='',
    [string]$Hooks='',
    [string]$PreparedProxy='',
    [string]$Python='',
    [switch]$BufferedTickLog,
    [switch]$Perf,
    [switch]$DeferFlip,
    [ValidateRange(0,1000)][int]$PresentationEvery=0,
    [switch]$DryRun,
    [string]$MockDirectory='',
    # Live branch-site union (docs/reference-game/live_coverage.md).
    # off: the unchanged regression. run: [coverage] in this run, under its own
    # hook modes. original: after an identical regression, a second pass on the
    # same clones with every hook original and [coverage]; the verdict pass
    # stays uninstrumented. Neither changes summary results or the exit code.
    [ValidateSet('off','run','original')][string]$LiveCoverage='off',
    # edge: each outcome is observed once (one-shot INT3 on exclusive edge
    # destinations, taken redirects), live_coverage_engine.h; much faster.
    [ValidateSet('saturate','count','edge')][string]$CoverageMode='saturate',
    # Edge mode only: a valid coverage-union.json whose outcomes this pass does
    # not watch (they count as seen); the union then shows what the pass adds.
    [string]$CoverageBaseline='',
    [ValidateRange(0,1000000)][int]$CoverageReportEveryTicks=0,
    # Per-suite limit of a pass with [coverage] traps (the verdict pass keeps
    # the runner's 300 s): the first original-mode union ran 10-20x slower.
    [ValidateRange(60,86400)][int]$CoverageTimeoutSeconds=3600,
    # Extra DBSIM arguments for every suite run, appended to the fixed ones
    # exactly as given (quote each element: '-ptapes\demo1'). They reach the
    # children as -GameArgumentsJson and are recorded in each run's launch and
    # entry JSON and in every summary.json. The goldens belong to the recorded
    # command line (the fixed one plus the suite's registry "game_arguments"),
    # so such a run normally ends DIFFERENT (exit 1).
    [string[]]$GameArguments=@(),
    # Alternative suite registry (a draft); handed on to the children only when
    # given. The default is oracle-suites.json next to this script.
    [string]$SuitesFile='',
    # Headless replay window for every child (ES2_ORACLE_HIDDEN_WINDOW '1',
    # -HiddenWindow:$false '0'); not given, run-oracle-regression.ps1's default holds.
    [switch]$HiddenWindow,
    # Start order of the suites. longest (default): by golden tick count, longest first,
    # ties in registry order; registry: the order of the registry file. Only the start order
    # differs: results, summaries and counters keep the registry order.
    [ValidateSet('longest','registry')][string]$DispatchOrder='longest'
)
if (!$DataRoot -or !$GoldenRoot -or !$Python) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$GoldenRoot) { $GoldenRoot = (Join-Path $es2.DataRepository 'oracle\golden') }
    if (!$Python) { $Python = $es2.Python }
}
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$givenSuitesFile=[bool]$SuitesFile
if ($givenSuitesFile) { $SuitesFile=[IO.Path]::GetFullPath($SuitesFile) } else { $SuitesFile=Join-Path $PSScriptRoot '../oracle-suites.json' }
$all=@((Get-Content -LiteralPath $SuitesFile -Raw | ConvertFrom-Json).suites)
if ($Suite.Count -and @($Suite | Where-Object { $_ -notin $all.name }).Count) { throw 'Unknown suite.' }
$selected=@($all | Where-Object { !$Suite.Count -or $_.name -in $Suite })
if (!$InstallName.Count -or @($InstallName | Select-Object -Unique).Count -ne $InstallName.Count) { throw 'Clone names must be unique.' }
foreach ($name in $InstallName) { Assert-MeasurementName $name }
# timing-r111 plus -p1..-p5 (new-measurement-clone.ps1 -Number 1..5).
if ($InstallName.Count -gt 6) { throw 'At most six clones can run in parallel.' }
$GameArguments=@($GameArguments)
Assert-GameArguments $GameArguments
# Per suite: its registry "game_arguments" (format 3) and the list it runs with
# (those followed by -GameArguments). Refused here, before any directory, lock or
# child exists, when a suite's argument is invalid, a command-line argument
# collides with it or its golden manifest records other arguments.
$suiteArguments=@{}
foreach ($s in $selected) {
    $own=Get-SuiteGameArguments $s
    $suiteArguments[$s.name]=[pscustomobject]@{Own=$own;Effective=(Merge-GameArguments $s.name $own $GameArguments)}
    Assert-GoldenGameArguments $GoldenRoot $s.name $own
}
# Tick count of a suite from its golden: launch.ticks_seen of manifest.json (capped by the
# registry frame_limit, the window the golden was normalized with), else the tick_count at the
# start of normalized.json, else the registry frame_limit, else 0 (unknown: after all known ones).
function Get-SuiteTicks($s) {
    $limit=[int]$s.frame_limit
    $dir=Join-Path $GoldenRoot $s.name
    try {
        $seen=[int64](Get-Content -LiteralPath (Join-Path $dir 'manifest.json') -Raw | ConvertFrom-Json).launch.ticks_seen
        if ($seen -gt 0) { return @{Ticks=$(if ($limit -gt 0 -and $limit -lt $seen) {$limit} else {$seen});Source='manifest'} }
    } catch {}
    try {
        $stream=[IO.File]::Open((Join-Path $dir 'normalized.json'),'Open','Read','ReadWrite')
        try { $buffer=New-Object byte[] 8192;$count=$stream.Read($buffer,0,$buffer.Length) } finally { $stream.Dispose() }
        $found=[regex]::Match([Text.Encoding]::ASCII.GetString($buffer,0,$count),'"tick_count"\s*:\s*(\d+)')
        if ($found.Success -and [int64]$found.Groups[1].Value -gt 0) { return @{Ticks=[int64]$found.Groups[1].Value;Source='normalized'} }
    } catch {}
    if ($limit -gt 0) { return @{Ticks=[int64]$limit;Source='frame_limit'} }
    return @{Ticks=[int64]0;Source='unknown'}
}
$ticksBasis=[ordered]@{};$index=0;$ranked=@()
foreach ($s in $selected) {
    $t=Get-SuiteTicks $s
    $ticksBasis[$s.name]=[ordered]@{ticks=$t.Ticks;source=$t.Source}
    $ranked+=[pscustomobject]@{Suite=$s;Ticks=$t.Ticks;Index=$index++}
}
# Deterministic: the index breaks every tie, so equal inputs give one order.
$dispatch=@(if ($DispatchOrder -eq 'longest') { $ranked | Sort-Object @{Expression='Ticks';Descending=$true},@{Expression='Index';Descending=$false} | ForEach-Object { $_.Suite } } else { $selected })
if (!$OutputDirectory) { $OutputDirectory=Join-Path $DataRoot ('logs/oracle-parallel-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')) }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
$LiveCoverage=$LiveCoverage.ToLowerInvariant()
# Pass directory whose <suite>\coverage.json reports form the union.
$coverageRoot=if ($LiveCoverage -eq 'original') { Join-Path $OutputDirectory 'coverage-original' } else { $OutputDirectory }
if ($LiveCoverage -ne 'off') {
    if ($LiveCoverage -eq 'run' -and $Perf) { throw 'LiveCoverage run cannot be combined with -Perf: the profile would include the coverage traps.' }
    # The proxy refuses report paths longer than MAX_PATH-32 characters (live_coverage.h).
    foreach ($s in $selected) { if ((Join-Path (Join-Path $coverageRoot $s.name) 'coverage.json').Length -gt 228) { throw 'Coverage report path too long for the proxy; choose a shorter OutputDirectory.' } }
}
$knownFile=''
if ($CoverageBaseline) {
    if ($LiveCoverage -eq 'off' -or $CoverageMode -ne 'edge') { throw 'CoverageBaseline needs -LiveCoverage run|original with -CoverageMode edge.' }
    $CoverageBaseline=[IO.Path]::GetFullPath($CoverageBaseline)
    if (!(Test-Path -LiteralPath $CoverageBaseline -PathType Leaf)) { throw "Coverage baseline not found: $CoverageBaseline" }
    $knownFile=Join-Path $OutputDirectory 'coverage-known.txt'
}
if ($DryRun) {
    $plan=@{dry_run=$true;clones=$InstallName;suites=@($selected.name);output=$OutputDirectory;dispatch_order=$DispatchOrder;dispatch=@($dispatch.name);dispatch_basis=$ticksBasis}
    if ($LiveCoverage -ne 'off') { $plan.live_coverage=$LiveCoverage;$plan.coverage_mode=$CoverageMode;$plan.coverage_root=$coverageRoot }
    if ($CoverageBaseline) { $plan.coverage_baseline=$CoverageBaseline;$plan.coverage_known=$knownFile }
    if ($GameArguments.Count) { $plan.game_arguments=$GameArguments }
    if ($PSBoundParameters.ContainsKey('HiddenWindow')) { $plan.hidden_window=[bool]$HiddenWindow }
    # Only the suites with registry arguments are listed (their effective lists).
    $own=@($selected | Where-Object { $suiteArguments[$_.name].Own.Count })
    if ($own.Count) { $plan.suite_game_arguments=[ordered]@{}; foreach ($s in $own) { $plan.suite_game_arguments[$s.name]=$suiteArguments[$s.name].Effective } }
    $plan | ConvertTo-Json -Depth 5;return
}
if (!$MockDirectory) {
    foreach ($name in $InstallName) {
        $install=Join-Path (Get-CloneInstallRoot $DataRoot) $name;Assert-NoLinks $install
        $m=Get-Content -LiteralPath (Join-Path $install 'installation-manifest.json') -Raw | ConvertFrom-Json
        if (!$m.measurement_clone_of -or $m.destination -ne $install -or $m.measurement_clone_of -eq $install) { throw "Invalid measurement clone: $name" }
    }
}
# The caller's complete environment as it is now (variables the caller set on purpose are part of it).
# The workers below set ES2_PREPARED_PROXY, ES2_PARALLEL_CLONES, ES2_TICK_BUFFERED, ES2_ORACLE_SPEED_CONFIG,
# ES2_CLONE_LOCK_TOKEN, ES2_LIVE_COVERAGE_CONFIG and (with -HiddenWindow) ES2_ORACLE_HIDDEN_WINDOW for their
# children; the finally gives the process back exactly as it was, whatever else a step may have set.
$callerEnvironment=Get-RunnerEnvironment
# Windows command-line quoting, including trailing backslashes.
function Join-NativeArguments([string[]]$Values) {
    ($Values | ForEach-Object { '"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"' }) -join ' '
}
# One session per clone (CloneGuard.psm1). Every child runner inherits the
# lock of its clone through ES2_CLONE_LOCK_TOKEN; the locks are released after
# the last child has exited (or vanish with this process).
$locks=@{}
function Start-OracleWorker($s,[string]$clone,[string]$root,[string]$passHooks,[bool]$coverage,[bool]$speed) {
    $dir=Join-Path $root $s.name
    New-Item -ItemType Directory -Path $dir | Out-Null
    # run-oracle-regression.ps1 requires a fresh output directory of its own.
    $runDir=Join-Path $dir 'run'
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=(Get-Process -Id $PID).Path
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    if ($speed -and ($Perf -or $DeferFlip -or $PresentationEvery -gt 1)) {
        $ini=Join-Path $dir 'speed.ini'
        "[perf]`nenabled=1`nreport=$(Join-Path $dir 'perf.json')`nframe_limit=$([int]$s.frame_limit)`n[presentation]`ndefer_flip=$([int][bool]$DeferFlip)`nevery_n=$PresentationEvery`n" | Set-Content -LiteralPath $ini -Encoding Unicode
        $env:ES2_ORACLE_SPEED_CONFIG=$ini
    } else { $env:ES2_ORACLE_SPEED_CONFIG=$null }
    if ($coverage) {
        # The proxy reads this separate INI (live_coverage.h); DBSIM inherits the
        # variable through the child runner. Report beside the suite's run.
        $coverageIni=Join-Path $dir 'coverage.ini'
        $known=if ($knownFile) { "known=$knownFile`n" } else { '' }
        # A suite with hold ends parked after its tick count (no exit): the window is frozen at that tick.
        $window=if ($s.frame_limit) { [int]$s.frame_limit } elseif ($s.PSObject.Properties['hold'] -and $s.hold -eq $true) { [int]$s.ticks } else { 0 }
        "[coverage]`nenabled=1`nmode=$CoverageMode`nhooks=all`nreport=$(Join-Path $dir 'coverage.json')`nreport_every_ticks=$CoverageReportEveryTicks`nframe_limit=$window`nfirst_hit_ticks=0`n$known" | Set-Content -LiteralPath $coverageIni -Encoding Unicode
        $env:ES2_LIVE_COVERAGE_CONFIG=$coverageIni
    } elseif ($LiveCoverage -ne 'off') { $env:ES2_LIVE_COVERAGE_CONFIG=$null }
    $env:ES2_PREPARED_PROXY=$PreparedProxy;$env:ES2_PARALLEL_CLONES=($InstallName -join ',');$env:ES2_TICK_BUFFERED=if ($BufferedTickLog) {'1'} else {$null}
    $env:ES2_CLONE_LOCK_TOKEN=$locks[$clone].Token
    if ($MockDirectory) {
        # The mock is a child PowerShell script with the exact production
        # argument contract. Tests supply it alongside synthetic summaries.
        $runner=Join-Path $MockDirectory 'run-oracle-regression.ps1'
    } else { $runner=Join-Path $PSScriptRoot 'run-oracle-regression.ps1' }
    $args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$runner,'-Mode','check','-Suite',$s.name,
        '-InstallName',$clone,'-OutputDirectory',$runDir,'-DataRoot',$DataRoot,'-GoldenRoot',$GoldenRoot,'-RepositoryRoot',$repo,'-Python',$Python)
    if ($passHooks) { $args+=@('-Hooks',$passHooks) }
    if ($coverage) { $args+=@('-SuiteTimeoutSeconds',"$CoverageTimeoutSeconds") }
    if ($givenSuitesFile) { $args+=@('-SuitesFile',$SuitesFile) }
    # One JSON string: "-File" cannot bind an array whose elements start with a dash.
    if ($GameArguments.Count) { $args+=@('-GameArgumentsJson',(ConvertTo-Json -InputObject $GameArguments -Compress)) }
    $psi.Arguments=Join-NativeArguments $args
    $p=[Diagnostics.Process]::Start($psi)
    return @{Process=$p;Out=$p.StandardOutput.ReadToEndAsync();Err=$p.StandardError.ReadToEndAsync();Suite=$s.name;Clone=$clone;Directory=$dir;RunDirectory=$runDir;FrameLimit=[int]$s.frame_limit}
}
# One check-mode pass of every selected suite over the clones; returns the
# per-suite results. Running children live in $script:active for the finally.
$active=@()
function Invoke-OraclePass([string]$root,[string]$passHooks,[bool]$coverage,[bool]$speed) {
    $finished=@{};$queue=[Collections.Queue]::new();foreach ($s in $dispatch) {$queue.Enqueue($s)}
    $started=[Collections.Generic.List[string]]::new()
    while ($queue.Count -or $script:active.Count) {
        foreach ($clone in $InstallName) {
            if ($queue.Count -and $clone -notin @($script:active | ForEach-Object {$_.Clone})) {
                $next=$queue.Dequeue()
                $script:active+=Start-OracleWorker $next $clone $root $passHooks $coverage $speed
                # What this pass started, in which order and on which clone (evidence only).
                $started.Add("$($next.name)@$clone")
                [ordered]@{schema=1;dispatch_order=$DispatchOrder;planned=@($dispatch.name);started=@($started);basis=$ticksBasis} |
                    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'dispatch.json') -Encoding UTF8
            }
        }
        Start-Sleep -Milliseconds 100
        foreach ($job in @($script:active)) {
            if (!$job.Process.HasExited) { continue }
            $code=$job.Process.ExitCode
            [IO.File]::WriteAllText((Join-Path $job.Directory 'runner.log'),$job.Out.Result+$job.Err.Result)
            $summary=$null
            try { $summary=Get-Content -LiteralPath (Join-Path $job.RunDirectory 'summary.json') -Raw | ConvertFrom-Json } catch {}
            $valid=$code -eq 0 -and $summary -and $summary.mode -eq 'check' -and @($summary.results).Count -eq 1 -and
                $summary.results[0].suite -eq $job.Suite -and $summary.results[0].result -eq 'identical' -and $summary.hooks -eq $passHooks
            # The child must report the very list it was given.
            if ($valid -and $GameArguments.Count) { $valid=@($summary.game_arguments).Count -eq $GameArguments.Count -and !@(0..($GameArguments.Count-1) | Where-Object { $summary.game_arguments[$_] -cne $GameArguments[$_] }).Count }
            # A suite with registry arguments: its result names the list it ran with
            # (registry arguments first, then the given ones).
            $ownArguments=$suiteArguments[$job.Suite]
            if ($valid -and $ownArguments.Own.Count) {
                $reported=@($summary.results[0].game_arguments);$wanted=@($ownArguments.Effective)
                $valid=$reported.Count -eq $wanted.Count -and !@(0..($wanted.Count-1) | Where-Object { $reported[$_] -cne $wanted[$_] }).Count
            }
            if ($speed -and ($Perf -or $DeferFlip -or $PresentationEvery -gt 1)) {
                $perfValid=$false;$errorText=''
                try {
                    $profile=Get-Content -LiteralPath (Join-Path $job.Directory 'perf.json') -Raw | ConvertFrom-Json
                    $launch=Get-Content -LiteralPath (Join-Path $job.RunDirectory ($job.Suite+'-launch.json')) -Raw | ConvertFrom-Json
                    $normalized=Get-Content -LiteralPath (Join-Path $job.RunDirectory ($job.Suite+'-normalized.json')) -Raw | ConvertFrom-Json
                    $reason=if($job.FrameLimit){'frame_limit'}else{'exit'}
                    $last=if($job.FrameLimit){$job.FrameLimit-1}else{$normalized.last_frame}
                    $perfValid=$profile.schema -eq 1 -and $profile.module_sha256 -eq '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad' -and
                        $profile.pid -eq $launch.pid -and $profile.thread -gt 0 -and $profile.qpc_frequency -gt 0 -and $profile.error -eq 0 -and
                        $profile.complete -and $profile.reason -eq $reason -and $profile.frame_limit -eq $job.FrameLimit -and $profile.last_frame -eq $last -and
                        $profile.presentation.every_n -eq $PresentationEvery -and $profile.presentation.defer_flip -eq [bool]$DeferFlip
                    foreach($phase in @('tick_format','tick_write','watch_reads','input_journal','hook_dispatch','directdraw','frame_other')) {
                        if (!$profile.phases.$phase -or $profile.phases.$phase.qpc -lt 0 -or $profile.phases.$phase.count -lt 0) {$perfValid=$false}
                    }
                    if (!$perfValid) {$errorText='Profile identity, completion, window, options or counters are invalid.'}
                } catch {$errorText=$_.Exception.Message}
                @{valid=[bool]$perfValid;golden_identical=[bool]$valid;error=$errorText} | ConvertTo-Json |
                    Set-Content -LiteralPath (Join-Path $job.Directory 'speed-validation.json') -Encoding UTF8
                $valid=$valid -and $perfValid
            }
            $finished[$job.Suite]=@{suite=$job.Suite;result=$(if ($valid) {'identical'} else {'DIFFERENT'});summary=$summary}
            $job.Process.Dispose();$script:active=@($script:active | Where-Object {$_ -ne $job})
        }
    }
    return $finished
}
function Get-PassSummary($finished,[string]$root,[string]$passHooks) {
    $first=$finished[$selected[0].name].summary
    $results=@(foreach ($s in $selected) {
        $entry=$finished[$s.name]
        if (!$entry.summary -or !$first -or $entry.summary.commit -ne $first.commit -or $entry.summary.dirty_tree -ne $first.dirty_tree) { $entry.result='DIFFERENT' }
        $item=[ordered]@{suite=$entry.suite;result=$entry.result}
        if ($suiteArguments[$s.name].Own.Count) { $item.game_arguments=$suiteArguments[$s.name].Effective }
        # The child's exit-code record of the own DBSIM (run-oracle-regression.ps1), when it has one.
        $childItem = if ($entry.summary -and @($entry.summary.results).Count -eq 1) { $entry.summary.results[0] } else { $null }
        if ($childItem -and $childItem.PSObject.Properties['exit_code_state']) {
            $item.exit_code=$childItem.exit_code; $item.exit_code_state=$childItem.exit_code_state
            if ($childItem.PSObject.Properties['exit_code_check']) { $item.exit_code_check=$childItem.exit_code_check }
        }
        $item
    })
    $merged=[ordered]@{mode='check';output=$root;commit=$(if ($first) {$first.commit} else {''});dirty_tree=$(if ($first) {$first.dirty_tree} else {$true});hooks=$passHooks;results=$results}
    if ($GameArguments.Count) { $merged.game_arguments=$GameArguments }
    # As the children report it (run-oracle-regression.ps1 writes the key only when hidden).
    if ($first -and $first.hidden_window) { $merged.hidden_window=$true }
    return $merged
}
# live_coverage_union.py with this pass's inventory (and baseline, if any).
function Invoke-UnionScript([string]$passHooks,[string[]]$extra) {
    $tools=Join-Path $repo 'tools/reference-game'
    $arguments=@('-B',(Join-Path $tools 'scripts/live_coverage_union.py'),$coverageRoot,'--output',$OutputDirectory,'--kind',$LiveCoverage,
        '--coverage-mode',$CoverageMode,'--suites',(@($selected.name) -join ','),'--golden-root',$GoldenRoot,"--hooks=$passHooks",
        '--names',(Join-Path $tools 'proxy/hook-names.txt'),'--sites',(Join-Path $tools 'invitro/branch_sites.h'))
    if ($CoverageBaseline) { $arguments+=@('--baseline',$CoverageBaseline) }
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Python;$psi.Arguments=Join-NativeArguments ($arguments+$extra)
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $p=[Diagnostics.Process]::Start($psi)
    $out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$p.WaitForExit()
    $result=@{Code=$p.ExitCode;Output=$out.Result+$err.Result;Error=$err.Result};$p.Dispose()
    return $result
}
# Validates the coverage reports with live_coverage_union.py. Coverage never
# changes a suite result or the exit code; an invalid union is reported here.
function Invoke-CoverageUnion([bool]$passRan,[string]$passHooks) {
    $union=Join-Path $OutputDirectory 'coverage-union.json'
    $status=[ordered]@{mode=$LiveCoverage;coverage_mode=$CoverageMode;valid=$false;union=$union;suites=$coverageRoot;error=''}
    try {
        if (!$passRan) { throw 'Skipped: the regression was not identical, so no original-mode coverage pass ran.' }
        $run=Invoke-UnionScript $passHooks @()
        $code=$run.Code
        [IO.File]::WriteAllText((Join-Path $OutputDirectory 'coverage-union.log'),$run.Output)
        if (!(Test-Path -LiteralPath $union)) { throw "live_coverage_union.py exited with $code and wrote no union: $($run.Error.Trim())" }
        $report=Get-Content -LiteralPath $union -Raw | ConvertFrom-Json
        $status.valid=$code -eq 0 -and $report.valid -eq $true
        if (!$status.valid) { $status.error=if ($report.error) {[string]$report.error} else {"live_coverage_union.py exited with $code."} }
    } catch {
        $status.error=$_.Exception.Message
        if (!(Test-Path -LiteralPath $union)) { [ordered]@{schema=1;valid=$false;kind=$LiveCoverage;error=$status.error} | ConvertTo-Json | Set-Content -LiteralPath $union -Encoding UTF8 }
    }
    if (!$status.valid) { Write-Warning "Live coverage union invalid (suite results unaffected): $($status.error)" }
    return $status
}
$finished=$null;$coverageFinished=$null
try {
    if ($PSBoundParameters.ContainsKey('HiddenWindow')) { $env:ES2_ORACLE_HIDDEN_WINDOW=if ($HiddenWindow) {'1'} else {'0'} }
    foreach ($name in $InstallName) { $locks[$name]=Enter-CloneLock -DataRoot $DataRoot -InstallName $name -Purpose 'run-oracle-parallel' }
    if (!$MockDirectory) {
        foreach ($name in $InstallName) { Assert-CloneIdle (Join-Path (Get-CloneInstallRoot $DataRoot) $name) 'a parallel oracle run' }
        # Mount once before workers: no simultaneous mount/drive-letter race.
        # The image of start-timing-mission.ps1's default -ExtractedRoot (the es2 checkout's build).
        $iso=Join-Path (Get-Es2Layout).Extracted 'earthsiege2-data.iso'
        $disk=Get-DiskImage -ImagePath $iso
        if (!$disk.Attached) { $disk=Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
    }
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    if ($knownFile) {
        # The baseline's outcomes for the current inventory, read by every proxy of the pass.
        $run=Invoke-UnionScript $(if ($LiveCoverage -eq 'original') {''} else {$Hooks}) @('--write-known',$knownFile)
        if ($run.Code -ne 0 -or !(Test-Path -LiteralPath $knownFile)) { throw "Coverage baseline refused: $($run.Output.Trim())" }
    }
    if (!$MockDirectory -and !$PreparedProxy) {
        Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
        $PreparedProxy=New-Es2PreparedProxy (Join-Path $OutputDirectory 'prepared')
    }
    $finished=Invoke-OraclePass $OutputDirectory $Hooks ($LiveCoverage -eq 'run') $true
    if ($LiveCoverage -eq 'original' -and !@($selected | Where-Object { $finished[$_.name].result -ne 'identical' }).Count) {
        # Same suites, clones and prepared proxy; no -Hooks (every hook original).
        New-Item -ItemType Directory -Path $coverageRoot | Out-Null
        $coverageFinished=Invoke-OraclePass $coverageRoot '' $true $false
    }
} finally {
    Restore-RunnerEnvironment $callerEnvironment
    # Do not kill games or runners on an orchestration failure. Their own
    # bounded runner owns process identity and DATA restoration; the clone
    # locks are kept until those children have finished.
    $running=@($active | Where-Object { !$_.Process.HasExited })
    if ($running.Count) {
        Write-Warning "Waiting for $($running.Count) child runner(s) to finish before releasing the clone locks."
        foreach ($job in $running) { [void]$job.Process.WaitForExit(900000) }
    }
    foreach ($lock in $locks.Values) { Exit-CloneLock $lock }
}
$merged=Get-PassSummary $finished $OutputDirectory $Hooks
if ($LiveCoverage -ne 'off') {
    $coverageHooks=$Hooks
    if ($LiveCoverage -eq 'original') {
        $coverageHooks=''
        if ($coverageFinished) {
            Get-PassSummary $coverageFinished $coverageRoot '' | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $coverageRoot 'summary.json') -Encoding UTF8
        }
    }
    $merged['live_coverage']=Invoke-CoverageUnion ($LiveCoverage -eq 'run' -or [bool]$coverageFinished) $coverageHooks
}
$merged | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding UTF8
$merged | ConvertTo-Json -Depth 4 -Compress
if (@($merged.results | Where-Object {$_.result -ne 'identical'}).Count) { exit 1 }
