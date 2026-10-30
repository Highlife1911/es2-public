[CmdletBinding()]
param(
    [ValidateSet('record', 'check')][string]$Mode = 'check',
    [string]$DataRoot = '',
    [string]$InstallName = 'timing-r111',
    # Repository whose measurement scripts and proxy sources are used. Golden
    # references are recorded from a clean worktree of a reviewed commit.
    [string]$RepositoryRoot = '',
    [string]$GoldenRoot = '',
    [string]$OutputDirectory = '',
    [string[]]$Suite = @(),
    # Optional hook modes "name=mode,..." for builds whose run scripts support -Hooks.
    [string]$Hooks = '',
    [string]$Python = '',
    [switch]$Force,
    [string]$SuitesFile = '',
    [switch]$DryRun,
    [string]$MockDirectory = '',
    # After each suite: how long the own DBSIM may take to exit before this
    # runner stops it (identity checked). CloneGuard.psm1
    [ValidateRange(0,3600)][int]$ExitGraceSeconds = 30,
    # Per-suite wall-clock limit of the mission runners. 300 s for verdict
    # runs; run-oracle-parallel.ps1 raises it only for a pass that carries
    # [coverage] traps (-CoverageTimeoutSeconds, live_coverage.md).
    [ValidateRange(1,86400)][int]$SuiteTimeoutSeconds = 300,
    # Extra DBSIM arguments, appended to the fixed ones and to the suite's own
    # registry "game_arguments" exactly as given (quote each element:
    # '-ptapes\demo1'; see Assert-GameArguments and Merge-GameArguments for what
    # is refused). They make a run differ from the goldens: check mode only,
    # never record. -GameArgumentsJson is the same list as a JSON array of
    # strings, the form run-oracle-parallel.ps1 uses for its child "powershell -File".
    [string[]]$GameArguments = @(),
    [string]$GameArgumentsJson = '',
    # Headless replay window ([window] hidden=1, proxy/window_shim.h; volume
    # 0 unless ES2_ORACLE_VOLUME_PERCENT says otherwise). Given: this value. Not
    # given: ES2_ORACLE_HIDDEN_WINDOW ('1'/'0', set by run-oracle-parallel.ps1,
    # check.ps1 when they were given -HiddenWindow), else the
    # default below. The decision reaches the mission runners through the same
    # variable; record-mode input and -Interactive runs never hide the window.
    [switch]$HiddenWindow,
    # main_loop.md "the notices against the paced original": 1 holds each virtual step for at least its 47 ms of wall time
    # ([time] virtual_time_pace=1, proxy/virtual_time.h), as a windowed es2sim paces its frames; the
    # tick log is the same, only wall-clock work (the sound device, the SOS timer) sees the
    # difference. Check mode only: the goldens are recorded unpaced.
    [ValidateSet(0,1)][int]$VirtualTimePace = 0
)
if (!$DataRoot -or !$GoldenRoot -or !$Python) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$GoldenRoot) { $GoldenRoot = (Join-Path $es2.DataRepository 'oracle\golden') }
    if (!$Python) { $Python = $es2.Python }
}
# Plays the oracle regression suites (tools/reference-game/oracle-suites.json)
# in the measurement clone with virtual time and scripted input. record: stores
# normalized golden references; check: compares each run with its golden
# reference and exits 1 on any difference. docs/reference-game/oracle_regression.md
$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'SuiteLaunch.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
Import-Module (Join-Path $toolRoot 'scripts/OracleSpeed.psm1') -Force
if ($GameArgumentsJson) {
    if (@($GameArguments).Count) { throw 'Give -GameArguments or -GameArgumentsJson, not both.' }
    $GameArguments = ConvertFrom-GameArgumentsJson $GameArgumentsJson
}
$GameArguments = @($GameArguments)
Assert-GameArguments $GameArguments
if ($VirtualTimePace -and $Mode -eq 'record') { throw 'Golden references are recorded unpaced; -VirtualTimePace 1 is for check runs.' }
if ($GameArguments.Count -and $Mode -eq 'record') { throw 'Golden references are recorded with the fixed DBSIM arguments and the suite''s registry game_arguments only; the command-line -GameArguments is for check runs.' }
# The one place to flip after the live check over every suite: $true hides
# the window by default in check mode (also for run-oracle-parallel.ps1 and check.ps1,
# which delegate here); record mode keeps the visible window.
$hiddenWindowDefault = $true
$windowHidden = $hiddenWindowDefault -and $Mode -eq 'check'
if ($env:ES2_ORACLE_HIDDEN_WINDOW -eq '1' -or $env:ES2_ORACLE_HIDDEN_WINDOW -eq '0') { $windowHidden = $env:ES2_ORACLE_HIDDEN_WINDOW -eq '1' }
if ($PSBoundParameters.ContainsKey('HiddenWindow')) { $windowHidden = [bool]$HiddenWindow }
if ($MockDirectory) { Assert-SuiteMockRoot $DataRoot }
$installation = Get-SuiteInstallation $DataRoot $InstallName -DryRun:$DryRun
if (!$RepositoryRoot) { $RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $toolRoot '../..')) }
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$runTools = Join-Path $RepositoryRoot 'tools\reference-game'
if (!$SuitesFile) { $SuitesFile = Join-Path $toolRoot 'oracle-suites.json' }
$suites = @((Get-Content -LiteralPath $SuitesFile -Raw | ConvertFrom-Json).suites)
if (@($suites.name | Select-Object -Unique).Count -ne $suites.Count) { throw 'Duplicate suite names.' }
if (@($Suite | Where-Object { $_ -notin $suites.name }).Count) { throw 'Unknown suite selected.' }
if ($Suite.Count) { $suites = @($suites | Where-Object { $_.name -in $Suite }) }
if (!$suites.Count) { throw 'No suite selected.' }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
if (!$OutputDirectory) { $OutputDirectory = Join-Path $DataRoot "logs\oracle-$Mode-$stamp" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
# The arguments each suite runs with: its registry "game_arguments" (format 3,
# optional) followed by the command-line ones. Splatted into a runner only when
# not empty, so a suite without any calls every runner without -GameArguments.
$suiteArguments = @{}
foreach ($s in $suites) {
    Assert-SuiteName $s.name
    $own = Get-SuiteGameArguments $s
    $effective = Merge-GameArguments $s.name $own $GameArguments
    # Tape suites: a registry tape option (-r/-p/-D/-d) or the field
    # demo_tape runs the suite inside the file transaction with the tape plan
    # (SuiteLaunch.psm1 Get-SuiteTapePlan). Tape options given only on the
    # command line are the caller's responsibility.
    $demoProperty = $s.PSObject.Properties['demo_tape']
    $demo = ''
    if ($demoProperty -and $null -ne $demoProperty.Value) {
        if ($demoProperty.Value -isnot [string]) { throw "Suite '$($s.name)': demo_tape must be a string." }
        $demo = $demoProperty.Value
    }
    $tapePlan = $null
    if ($demo -or @($own | Where-Object { $_ -cmatch '^[-/][rpDd]' }).Count) {
        try { $tapePlan = Get-SuiteTapePlan $effective $demo } catch { throw "Suite '$($s.name)': $($_.Exception.Message)" }
    }
    # hold: the game thread parks after `ticks` records (proxy [trace] hold_frame),
    # for runs without the limiter (tape playback) that a poll cannot stop in time.
    $holdProperty = $s.PSObject.Properties['hold']
    $hold = $false
    if ($holdProperty -and $null -ne $holdProperty.Value) {
        if ($holdProperty.Value -isnot [bool]) { throw "Suite '$($s.name)': hold must be true or false." }
        $hold = $holdProperty.Value
        if ($hold -and $s.outcome) { throw "Suite '$($s.name)': hold stops the run, an outcome suite must end by itself." }
    }
    $suiteArguments[$s.name] = [pscustomobject]@{ Own = $own; Effective = $effective; Tape = $tapePlan; DemoTape = $demo; Hold = $hold }
    if ($s.launch) { $null = Read-SuiteLaunch $DataRoot $s.launch }
    if ($s.ticks -lt 1 -or ($s.frame_limit -and ($s.frame_limit -lt 1 -or $s.frame_limit -gt $s.ticks))) { throw 'Invalid suite tick window.' }
}
if ($DryRun) {
    $plans = foreach ($s in $suites) {
        $args = [ordered]@{DataRoot=$DataRoot;InstallName=$InstallName;OutputDirectory=$OutputDirectory;RunName=$s.name;Ticks=$s.ticks}
        if ($s.outcome) {
            $runner = 'run-mission-outcome.ps1'
            $args.Script = Resolve-SuiteInputPath $toolRoot $s.script; $args.WatchFile = Resolve-SuiteInputPath $toolRoot $s.watches
        } else {
            $runner = 'run-tick-log-mission.ps1'
            $args.TimeoutSeconds=$SuiteTimeoutSeconds; $args.VirtualTimeStepMs=47; $args.VirtualTimePace=$VirtualTimePace; $args.InputMode='replay'
            $args.InputJournal=Resolve-SuiteInputPath $toolRoot $s.script; $args.Watch=@(Get-Content -LiteralPath (Resolve-SuiteInputPath $toolRoot $s.watches))
        }
        if ($Hooks) { $args.Hooks=$Hooks }
        if ($suiteArguments[$s.name].Effective.Count) { $args.GameArguments=$suiteArguments[$s.name].Effective }
        if ($suiteArguments[$s.name].Hold) { $args.HoldAtTicks=$true }
        $plan = @{suite=$s.name;runner=$runner;arguments=$args;launch_set=$(if($s.launch){Read-SuiteLaunch $DataRoot $s.launch}else{$null});frame_limit=$s.frame_limit}
        # Only a tape suite carries the key (it runs inside the transaction of run-prepared-suite.ps1).
        if ($suiteArguments[$s.name].Tape) { $plan.tape_plan = $suiteArguments[$s.name].Tape }
        $plan
    }
    $dry=@{dry_run=$true;mode=$Mode;plans=@($plans)}
    if ($windowHidden) { $dry.hidden_window=$true }
    $dry | ConvertTo-Json -Depth 12 -Compress
    return
}
if (!$MockDirectory) { $OutputDirectory = Assert-SuiteChild $OutputDirectory (Join-Path $DataRoot 'logs') }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
# A check belongs to the command line its golden was recorded with: refuse
# before a clone lock, a directory or a process exists.
if ($Mode -eq 'check') { foreach ($s in $suites) { Assert-GoldenGameArguments $GoldenRoot $s.name $suiteArguments[$s.name].Own } }
# One session per clone (CloneGuard.psm1): refuse before anything is created
# when another session holds the clone or a game runs in it. Child scripts of
# this run see the lock token and pass; the lock vanishes when this process exits.
# The caller's environment as it is now; the finally gives the process back as it was (the variables
# set below, and whatever the runners called in this process set), so a second run in the same PowerShell
# process starts its game with the same environment strings.
$callerEnvironment = Get-RunnerEnvironment
$cloneLock = Enter-CloneLock -DataRoot $DataRoot -InstallName $InstallName -Purpose "run-oracle-regression -Mode $Mode"
$env:ES2_CLONE_LOCK_TOKEN = $cloneLock.Token
$env:ES2_ORACLE_HIDDEN_WINDOW = if ($windowHidden) { '1' } else { '0' }
try {
    if (!$MockDirectory) { Assert-CloneIdle $installation 'starting an oracle run' }
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    if ($MockDirectory) { $commit='synthetic-mock'; $dirty=$false } else {
        $git=(Get-Command git -ErrorAction Stop).Source
        $identity=Invoke-SuiteProcess $git @('-C',$RepositoryRoot,'rev-parse','HEAD')
        $status=Invoke-SuiteProcess $git @('-C',$RepositoryRoot,'status','--porcelain','--untracked-files=no')
        if ($identity.status -ne 0 -or $status.status -ne 0) { throw 'Cannot establish repository identity.' }
        $commit=$identity.stdout.Trim(); $dirty=[bool]$status.stdout.Trim()
    }
    if ($Mode -eq 'record' -and $dirty) { throw "Golden references must come from a clean tree: $RepositoryRoot" }

    $results = @()
    foreach ($s in $suites) {
        if (!$MockDirectory) { Assert-CloneIdle $installation "suite $($s.name)" }
        $script = Resolve-SuiteInputPath $toolRoot $s.script
        $watchFile = Resolve-SuiteInputPath $toolRoot $s.watches
        $run = "$($s.name)"
        $effective = $suiteArguments[$run].Effective
        # Splatted only when given, so a run without extra arguments calls every runner without -GameArguments.
        $gameSplat = @{}
        if ($effective.Count) { $gameSplat.GameArguments = $effective }
        $hookArguments = @{}
        if ($Hooks) { $hookArguments['Hooks'] = $Hooks }
        if ($effective.Count) { $hookArguments['GameArguments'] = $effective }
        $tapePlan = $suiteArguments[$run].Tape
        $tapeSplat = @{}
        if ($tapePlan) { $tapeSplat.TapeTransaction = $true; if ($suiteArguments[$run].DemoTape) { $tapeSplat.DemoTape = $suiteArguments[$run].DemoTape } }
        $holdSplat = @{}
        if ($suiteArguments[$run].Hold) { $holdSplat.HoldAtTicks = $true }
        $suiteStarted = [DateTime]::UtcNow; $launchError = $null
        $configSaved = Save-SuiteConfigFiles $installation
        try {
            if ($s.launch -or $MockDirectory -or $tapePlan) {
                $launch = & (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') -DataRoot $DataRoot -InstallName $InstallName `
                    -OutputDirectory $OutputDirectory -RunName $run -Launch $s.launch -InputJournal $script -WatchFile $watchFile `
                    -Ticks $s.ticks -TimeoutSeconds $SuiteTimeoutSeconds -VirtualTimePace $VirtualTimePace -SnapshotOnStall:([bool]$s.outcome) -KeepReturns:([bool]($s.launch -and $s.outcome)) -Hooks $Hooks `
                    -RepositoryRoot $RepositoryRoot -MockDirectory $MockDirectory @gameSplat @tapeSplat @holdSplat | Select-Object -Last 1
            } elseif ($s.outcome) {
                $launch = & (Join-Path $runTools 'setup\run-mission-outcome.ps1') -DataRoot $DataRoot -InstallName $InstallName `
                    -OutputDirectory $OutputDirectory -RunName $run -Script $script -WatchFile $watchFile -Ticks $s.ticks -TimeoutSeconds $SuiteTimeoutSeconds @hookArguments | Select-Object -Last 1
            } else {
                $launch = & (Join-Path $runTools 'setup\run-tick-log-mission.ps1') -DataRoot $DataRoot -InstallName $InstallName `
                    -OutputDirectory $OutputDirectory -RunName $run -Ticks $s.ticks -TimeoutSeconds $SuiteTimeoutSeconds -VirtualTimeStepMs 47 `
                    -VirtualTimePace $VirtualTimePace -InputMode replay -InputJournal $script -Watch (Get-Content -LiteralPath $watchFile) @hookArguments @holdSplat | Select-Object -Last 1
            }
        } catch { $launchError = $_ }
        # The clone is released only after this suite's own DBSIM has exited:
        # wait for it, stop it after the grace period (PID, path and start time
        # from the fresh launch report must match), never touch anything else.
        $ownExit = $null
        if (!$MockDirectory) {
            try {
                $ownExit = Wait-CloneOwnedProcessExit -Installation $installation -LaunchReport (Join-Path $OutputDirectory "$run-launch.json") `
                    -NotBefore $suiteStarted -GraceSeconds $ExitGraceSeconds
            } catch { if ($launchError) { throw "$launchError; afterwards: $_" }; throw }
        } else {
            # A mock suite has no process; its fixture may describe one (Wait-CloneOwnedProcessExit's result) to test the exit-code record.
            $mockExit = Join-Path (Join-Path $MockDirectory $run) 'own-exit.json'
            if (Test-Path -LiteralPath $mockExit -PathType Leaf) { $ownExit = Get-Content -LiteralPath $mockExit -Raw | ConvertFrom-Json }
        }
        # A compare-mode hook leaves a zero-byte mismatch log when nothing mismatched; the game has exited.
        $emptyHookLog = Remove-SuiteEmptyHookLog $OutputDirectory $run
        # DATA\PREFS.CFG as before the suite, whatever runner it took (SuiteLaunch.psm1).
        $configRestored = @()
        try { $configRestored = Restore-SuiteConfigFiles $configSaved } catch { if ($launchError) { throw "$launchError; afterwards: $_" }; throw }
        if ($launchError) { throw $launchError }
        # The exit code of the own DBSIM; 'none' when the runner stopped it (SuiteLaunch.psm1 Get-SuiteOwnExitRecord).
        if ($ownExit) { $ownExit = Get-SuiteOwnExitRecord $ownExit ($launch | ConvertFrom-Json) }
        if ($s.launch -or $tapePlan) {
            $runReport=$launch | ConvertFrom-Json
            if ($runReport.stalled -or ($s.outcome -and ($runReport.process_alive_at_end -or $runReport.stopped_after_identity_check))) { throw "Launch-aware suite did not finish its intended window: $run" }
            if ($s.launch -and $s.outcome -and !(Test-Path -LiteralPath (Join-Path $OutputDirectory "$run-output/results.dat"))) { throw "No new outcome result: $run" }
        }
        $normalized = Join-Path $OutputDirectory "$run-normalized.json"
        $outputs = Join-Path $OutputDirectory "$run-output"
        $limit = @()
        if ($s.frame_limit) { $limit = @('--frame-limit', "$($s.frame_limit)") }
        $normalization=Invoke-SuiteProcess $Python (@('-B',(Join-Path $toolRoot 'scripts/oracle_regression.py'),'normalize',(Join-Path $OutputDirectory "$run.jsonl"),$normalized,'--outputs',$outputs)+$limit)
        if ($normalization.status -ne 0 -or !(Test-Path -LiteralPath $normalized)) { throw "Normalization failed: $run $($normalization.stderr)" }
        $media = Get-Content -LiteralPath (Join-Path $installation 'oracle-media-manifest.json') -Raw | ConvertFrom-Json
        $entry = [ordered]@{
            suite = $run; mode = $Mode; utc = [DateTime]::UtcNow.ToString('o'); repository = $RepositoryRoot; commit = $commit
            dirty_tree = $dirty; proxy_sha256 = $media.proxy_sha256; hooks = $Hooks
            script_sha256 = (Get-FileHash -LiteralPath $script -Algorithm SHA256).Hash
            watches_sha256 = (Get-FileHash -LiteralPath $watchFile -Algorithm SHA256).Hash
            launch = ($launch | ConvertFrom-Json); own_process_exit = $ownExit
        }
        if ($s.launch) { $entry.launch_set = ($launch | ConvertFrom-Json).launch_set }
        if ($configRestored.Count) { $entry.config_restored = $configRestored }
        if ($effective.Count) { $entry.game_arguments = $effective }
        if ($suiteArguments[$run].Own.Count) { $entry.suite_game_arguments = $suiteArguments[$run].Own }
        if ($tapePlan) { $entry.tape_plan = $tapePlan }
        if ($windowHidden) { $entry.hidden_window = $true }
        if ($emptyHookLog) { $entry.empty_hook_log_removed = $true }
        $golden = Join-Path $GoldenRoot $run
        if ($Mode -eq 'record') {
            if ((Test-Path -LiteralPath $golden) -and !$Force) { throw "Golden reference exists: $golden (use -Force to replace)" }
            New-Item -ItemType Directory -Force -Path $golden | Out-Null
            Copy-Item -LiteralPath $normalized -Destination (Join-Path $golden 'normalized.json') -Force
            Copy-Item -LiteralPath (Join-Path $OutputDirectory "$run.jsonl") -Destination (Join-Path $golden 'tick-log.jsonl') -Force
            if (Test-Path -LiteralPath $outputs) { Copy-Item -LiteralPath $outputs -Destination (Join-Path $golden 'output') -Recurse -Force }
            [IO.File]::WriteAllText((Join-Path $golden 'manifest.json'), ($entry | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
            $entry.result = 'recorded'
        } else {
            $report = Join-Path $OutputDirectory "$run-comparison.json"
            $comparison=Invoke-SuiteProcess $Python @('-B',(Join-Path $toolRoot 'scripts/oracle_regression.py'),'compare',(Join-Path $golden 'normalized.json'),$normalized,'--report',$report)
            $comparison.stdout | Out-Host
            $entry.result = if ($comparison.status -eq 0 -and (Test-Path -LiteralPath $report)) { 'identical' } else { 'DIFFERENT' }
            $goldenManifest = Get-Content -LiteralPath (Join-Path $golden 'manifest.json') -Raw | ConvertFrom-Json
            $entry.golden_commit = $goldenManifest.commit
            # The exit code against the golden's. A golden without the field only reports 'no golden exit code'.
            if ($ownExit) {
                $exitCompare = Compare-SuiteOwnExit $goldenManifest $ownExit
                $entry.exit_code_check = $exitCompare.check
                if ($exitCompare.golden_state) { $entry.golden_exit_code_state = $exitCompare.golden_state; $entry.golden_exit_code = $exitCompare.golden_exit_code }
                if ($exitCompare.check -eq 'different') {
                    $entry.result = 'DIFFERENT'
                    Write-Host "Exit code of the own DBSIM differs: golden $($exitCompare.golden_state) $($exitCompare.golden_exit_code), run $($ownExit.exit_code_state) $($ownExit.exit_code)"
                }
            }
        }
        [IO.File]::WriteAllText((Join-Path $OutputDirectory "$run-entry.json"), ($entry | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
        $resultItem = [ordered]@{ suite = $run; result = $entry.result }
        # Only a suite with a process (or a mock fixture describing one) carries the keys.
        if ($ownExit) {
            $resultItem.exit_code = $ownExit.exit_code; $resultItem.exit_code_state = $ownExit.exit_code_state
            if ($entry.Contains('exit_code_check')) { $resultItem.exit_code_check = $entry.exit_code_check }
        }
        # Only a suite with registry arguments names its (effective) list per result.
        if ($suiteArguments[$run].Own.Count) { $resultItem.game_arguments = $effective }
        $results += $resultItem
    }
} finally {
    Restore-RunnerEnvironment $callerEnvironment
    Exit-CloneLock $cloneLock
}
$summary = [ordered]@{ mode = $Mode; output = $OutputDirectory; commit = $commit; dirty_tree = $dirty; hooks = $Hooks; results = $results }
if ($GameArguments.Count) { $summary.game_arguments = $GameArguments }
if ($windowHidden) { $summary.hidden_window = $true }
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'summary.json'), ($summary | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
$summary | ConvertTo-Json -Depth 4 -Compress
if ($Mode -eq 'check' -and ($results | Where-Object { $_.result -ne 'identical' })) { exit 1 }
