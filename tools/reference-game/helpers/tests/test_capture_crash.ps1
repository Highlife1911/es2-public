[CmdletBinding()]
param([string]$OutputDirectory)
# Process-level test of the crash collector against its own fixture. Runs
# outside `bazel test` because it debugs child processes and waits on them.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../../setup/Es2Bazel.psm1') -Force
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$build = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repository 'build/reference-game' }
$helper = Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:capture_crash' -Destination $build
$fixture = Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:capture_crash_fixture' -Destination $build
$run = Join-Path $build ('capture-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$priorMode = $env:ES2_CAPTURE_FIXTURE_MODE
try {
    foreach ($mode in @('handled', 'tree-crash')) {
        $env:ES2_CAPTURE_FIXTURE_MODE = $mode
        $output = Join-Path $run $mode
        & $helper --exe $fixture --working-directory $build --output $output
        $expectedExit = if ($mode -eq 'tree-crash') { 1 } else { 0 }
        if ($LASTEXITCODE -ne $expectedExit) { throw "Fixture $mode exited $LASTEXITCODE; expected $expectedExit." }
        $events = @(Get-Content -LiteralPath (Join-Path $output 'events.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        if (@($events | Where-Object kind -EQ 'session_end').Count -ne 1) { throw "Incomplete $mode event log." }
        if (@($events | Where-Object kind -EQ 'helper_error').Count) { throw "Helper errors in $mode." }
        if ($mode -eq 'handled') {
            if (@($events | Where-Object kind -EQ 'crash_capture').Count) { throw 'Handled exception was incorrectly captured.' }
            if (@($events | Where-Object { $_.kind -eq 'exception' -and $_.code -eq '0xe0421001' -and $_.first_chance }).Count -ne 1) { throw 'Handled first-chance exception was not observed.' }
        } else {
            if (@($events | Where-Object kind -EQ 'process_start').Count -ne 2) { throw 'Child process tree was not captured.' }
            if (@($events | Where-Object kind -EQ 'initial_breakpoint').Count -ne 2) { throw 'Loader breakpoints were not handled.' }
            $capture = @($events | Where-Object kind -EQ 'crash_capture')
            if ($capture.Count -ne 2 -or !$capture[0].first_chance -or $capture[1].first_chance) { throw 'Expected first-chance and unhandled capture.' }
            $summary = Get-Content -LiteralPath $capture[1].summary_path -Raw | ConvertFrom-Json
            if (!$summary.context_ok -or !$summary.full_memory_dump_ok -or $summary.exception_code -ne '0xc0000005' -or $summary.stack.read -le 0) { throw 'Incomplete crash capture.' }
            $stream = [IO.File]::OpenRead($summary.dump_path)
            try {
                $header = [byte[]]::new(32)
                if ($stream.Read($header, 0, 32) -ne 32 -or [BitConverter]::ToUInt32($header, 0) -ne 0x504d444d -or !([BitConverter]::ToUInt64($header, 24) -band 2)) { throw 'Dump lacks valid full-memory minidump header.' }
            } finally { $stream.Dispose() }
        }
    }
    $env:ES2_CAPTURE_FIXTURE_MODE = 'storm'
    $storm = Join-Path $run 'storm'
    New-Item -ItemType Directory -Path $storm | Out-Null
    & $helper --exe $fixture --working-directory $storm --output $storm
    if ($LASTEXITCODE -ne 1) { throw "Storm helper exited $LASTEXITCODE; expected 1." }
    $stormEvents = @(Get-Content -LiteralPath (Join-Path $storm 'events.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $stormCaptures = @($stormEvents | Where-Object kind -EQ 'crash_capture')
    if ($stormCaptures.Count -ne 2 -or !$stormCaptures[0].first_chance -or !$stormCaptures[1].first_chance) { throw 'Expected earlier handled AV and later storm context.' }
    $stormSummary = Get-Content -LiteralPath $stormCaptures[1].summary_path -Raw | ConvertFrom-Json
    if (!$stormSummary.first_chance -or $stormSummary.kind -ne 'first_chance_exception' -or !$stormSummary.full_memory_dump_ok) { throw 'Storm metadata or dump incorrect.' }
    $limit = @($stormEvents | Where-Object kind -EQ 'diagnostic_limit')
    if ($limit.Count -ne 1 -or $limit[0].reason -ne 'repeated_access_violation' -or $limit[0].rapid_repeat_count -ne 32) { throw 'Storm limit missing.' }
    if (@($stormEvents | Where-Object kind -EQ 'exception').Count -gt 8) { throw 'Repeated exceptions were not sampled.' }
    if (@($stormEvents | Where-Object { $_.kind -eq 'detach' -and $_.ok }).Count -ne 1) { throw 'Storm target was not detached.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline -and !(Test-Path -LiteralPath (Join-Path $storm 'fixture-storm-detached.txt'))) { Start-Sleep -Milliseconds 50 }
    if (!(Test-Path -LiteralPath (Join-Path $storm 'fixture-storm-detached.txt'))) { throw 'Storm fixture did not survive and observe detach.' }
    $env:ES2_CAPTURE_FIXTURE_MODE = 'detach'
    $detach = Join-Path $run 'detach'
    New-Item -ItemType Directory -Path $detach | Out-Null
    $arguments = @('--exe', ('"' + $fixture + '"'), '--working-directory', ('"' + $detach + '"'), '--output', ('"' + $detach + '"'))
    $watcher = Start-Process -FilePath $helper -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $detach 'stdout.txt') -RedirectStandardError (Join-Path $detach 'stderr.txt')
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $started = $false
    while ([DateTime]::UtcNow -lt $deadline -and !$watcher.HasExited) {
        $log = Join-Path $detach 'events.jsonl'
        if ((Test-Path -LiteralPath $log) -and ((Get-Content -LiteralPath $log -Raw) -match 'initial_breakpoint')) { $started = $true; break }
        Start-Sleep -Milliseconds 50
    }
    if (!$started) { throw 'Detach fixture did not reach running state.' }
    # Only our own helper is stopped; the fixture must survive debugger exit.
    Stop-Process -Id $watcher.Id
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline -and !(Test-Path -LiteralPath (Join-Path $detach 'fixture-survived.txt'))) { Start-Sleep -Milliseconds 50 }
    if (!(Test-Path -LiteralPath (Join-Path $detach 'fixture-survived.txt'))) { throw 'Fixture did not survive debugger exit.' }
    Write-Output "PASS: handled exception, first/second-chance full dumps, bounded first-chance AV storm with safe detach, target survives debugger exit. Evidence: $run"
} finally {
    $env:ES2_CAPTURE_FIXTURE_MODE = $priorMode
}
