# Fresh-clone check: can a new clone of the repository be verified and tested on a
# machine that has the pinned dependencies but not the original game data?
#
#   & tools\check-fresh-clone.ps1
#   & tools\check-fresh-clone.ps1 -Source ..\es2-worker -Ref claude/worker
#
# Steps:
#   1. git clone --no-hardlinks <Source> <WorkRoot>\es2. Source must be a local repository path, never a URL;
#      -Ref checks out a branch, tag or commit of it afterwards. The clone gets a local.bazelrc with the
#      dependency directory and the shared repository cache and disk cache below <OutputRoot>, without data.
#   2. <clone>\tools\setup-dependencies.ps1 -VerifyOnly -Destination <Dependencies>: hashes and versions of the
#      pinned tools; never downloads (a missing item is reported, not installed).
#   3. bazel test <Targets> --test_env=ES2_DATA_ROOT= --repo_env=ES2_DATA_ROOT= --keep_going in the clone (the
#      second flag empties @es2_data), with an output base and a repository contents cache of its own
#      (<OutputBase> and <OutputBase>-contents). Skipped when step 2 found a missing or mismatched item
#      unless -RunTestsAnyway is given, and with -NoTests.
#   4. report.md and summary.json in -ReportDirectory (default %TEMP%\es2-fresh-clone-report-<utc>): what
#      a fresh machine needs, the absolute paths the build files assume, the tests that skipped for lack of
#      data, and every failing test with the first error lines of its log.
#   5. Always, also after errors: bazel shutdown of <OutputBase>, then deletion of the clone and of
#      <WorkRoot> (unless -KeepClone). The output base itself is left for
#      tools\clean-worker-bazel.ps1 (name it like a package, e.g. fresh-clone).
#
# Only the files committed in <Source> are cloned (uncommitted changes are not part of the check). The script
# reads the main repository and the dependency directory and writes only below <WorkRoot>, <OutputBase>,
# <OutputBase>-contents and <ReportDirectory>. It never starts the game or touches es2-data.
#
# Exit status: 0 dependencies verified and all tests passed; 1 a dependency is missing or a test failed
# (the report says which); 2 refused (bad parameters, existing directories) before anything was created.
[CmdletBinding()]
param(
    [string]$Source = '',
    [string]$Ref = '',
    [string]$WorkRoot = '',
    [string]$ReportDirectory = '',
    [string]$Dependencies = '',
    # The Bazel output root of local.bazelrc; the output base sits directly below it (default <OutputRoot>\t-03-fresh).
    [string]$OutputRoot = '',
    [string]$OutputBase = '',
    [string[]]$Targets = @('//...'),
    [int]$Cpu = 4,
    [string[]]$BazelArgs = @(),
    [string]$Git = 'git',
    [string]$Bazel = 'bazel',
    [switch]$KeepClone,
    [switch]$NoTests,
    [switch]$RunTestsAnyway
)
$ErrorActionPreference = 'Stop'
if (!$Source -or !$Dependencies -or !$OutputRoot) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$Source) { $Source = $es2.Repository }
    if (!$Dependencies) { $Dependencies = $es2.Dependencies }
    if (!$OutputRoot) { $OutputRoot = $es2.BazelOutputRoot }
}
$Started = [DateTime]::UtcNow
$stamp = $Started.ToString('yyyyMMddTHHmmssZ')
if (!$WorkRoot) { $WorkRoot = Join-Path ([IO.Path]::GetTempPath()) "es2-fresh-clone-$stamp" }
if (!$ReportDirectory) { $ReportDirectory = Join-Path ([IO.Path]::GetTempPath()) "es2-fresh-clone-report-$stamp" }
if (!$OutputBase) { $OutputBase = Join-Path $OutputRoot 't-03-fresh' }

function Stop-Refused([string]$Message) {
    [Console]::Error.WriteLine("check-fresh-clone.ps1: refused: $Message")
    exit 2
}

# ---- Refusals before anything is created --------------------------------------------------------------
if ($Source -match '^[A-Za-z][A-Za-z0-9+.-]+://' -or $Source -match '^[^\\/:]+@[^\\/:]+:') { Stop-Refused "Source must be a local repository path, not a URL: $Source" }
if (!(Test-Path -LiteralPath $Source -PathType Container)) { Stop-Refused "Source is not a directory: $Source" }
$Source = [IO.Path]::GetFullPath($Source).TrimEnd('\')
& $Git -C $Source rev-parse --git-dir 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Stop-Refused "Source is not a git repository: $Source" }
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot).TrimEnd('\')
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
$Dependencies = [IO.Path]::GetFullPath($Dependencies).TrimEnd('\')
$OutputBase = [IO.Path]::GetFullPath($OutputBase).TrimEnd('\')
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
function Test-Inside([string]$Path, [string]$Parent) { return $Path.StartsWith($Parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) }
foreach ($pair in @(@('WorkRoot', $WorkRoot), @('ReportDirectory', $ReportDirectory))) {
    if (Test-Path -LiteralPath $pair[1]) { Stop-Refused "$($pair[0]) exists: $($pair[1])" }
    # The report may sit inside the source tree (an ignored scratch directory); the clone may not.
    if ($pair[1] -ieq $Source -or (Test-Inside $Source $pair[1]) -or ($pair[0] -eq 'WorkRoot' -and (Test-Inside $pair[1] $Source))) { Stop-Refused "$($pair[0]) overlaps Source: $($pair[1])" }
    if ($pair[1] -ieq $Dependencies -or (Test-Inside $pair[1] $Dependencies) -or (Test-Inside $Dependencies $pair[1])) { Stop-Refused "$($pair[0]) overlaps the dependency directory: $($pair[1])" }
}
if ($WorkRoot -ieq $ReportDirectory -or (Test-Inside $ReportDirectory $WorkRoot) -or (Test-Inside $WorkRoot $ReportDirectory)) { Stop-Refused 'WorkRoot and ReportDirectory overlap (the clone is deleted at the end)' }
# The output base must look like a worker package base (tools\clean-worker-bazel.ps1 deletes those) and sit
# directly below <OutputRoot>, never on the shared directories or the main tree's hashed base.
$leaf = Split-Path -Leaf $OutputBase
if ($leaf -cnotmatch '^[a-z][a-z0-9]*(-[a-z0-9]+)+$') { Stop-Refused "OutputBase name '$leaf' must look like a package name such as fresh-clone" }
if ((Split-Path -Parent $OutputBase) -ine $OutputRoot) { Stop-Refused "OutputBase must be directly below ${OutputRoot}: $OutputBase" }
if ($Cpu -lt 1) { Stop-Refused '-Cpu must be at least 1' }
$ContentsCache = "$OutputBase-contents"

# ---- State shared by the steps and the report ---------------------------------------------------------
$Clone = Join-Path $WorkRoot 'es2'
$Problems = New-Object System.Collections.Generic.List[string]
$Notes = New-Object System.Collections.Generic.List[string]
$Verify = @()
$VerifyExit = $null
$BazelExit = $null
$Tests = @()
$BuildErrors = @()
$Assumed = @()
$SourceCommit = ''
$CloneRemoved = $false
$ServerShutdown = ''
$Stages = [ordered]@{}

function Invoke-Logged([string]$Log, [scriptblock]$Command) {
    # Runs a native command, mirrors stdout+stderr to the console and to $Log; returns its exit code.
    & $Command 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log -Append | Out-Host
    return $LASTEXITCODE
}

function Remove-Clone([string]$Path) {
    # Unlink every junction/symlink first (never followed), then delete; only below the WorkRoot this run created.
    if (!(Test-Path -LiteralPath $Path)) { return $true }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($full -ine $WorkRoot -and !(Test-Inside $full $WorkRoot)) { throw "Refused to delete outside the work root: $Path" }
    $links = @(Get-ChildItem -LiteralPath $Path -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($l in $links) {
        if ($l.PSIsContainer) { [IO.Directory]::Delete($l.FullName, $false) } else { [IO.File]::Delete($l.FullName) }
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    return !(Test-Path -LiteralPath $Path)
}

New-Item -ItemType Directory -Path $ReportDirectory | Out-Null
New-Item -ItemType Directory -Path $WorkRoot | Out-Null
Write-Output "Fresh-clone check ${stamp}: source $Source -> $Clone"
Write-Output "Report: $ReportDirectory"
try {
    # ---- 1. Clone ---------------------------------------------------------------------------------------
    $t0 = [DateTime]::UtcNow
    $cloneLog = Join-Path $ReportDirectory 'clone.log'
    $code = Invoke-Logged $cloneLog { & $Git clone --no-hardlinks $Source $Clone }
    if ($code -ne 0) { throw "git clone failed (exit $code), see $cloneLog" }
    if ($Ref) {
        $code = Invoke-Logged $cloneLog { & $Git -C $Clone checkout --quiet $Ref }
        if ($code -ne 0) { throw "git checkout $Ref failed (exit $code), see $cloneLog" }
    }
    $bazelDeps = $Dependencies -replace '\\', '/'
    $bazelOut = $OutputRoot -replace '\\', '/'
    $bazelPwsh = $(if ($env:ES2_PWSH) { $env:ES2_PWSH } else { (Get-Process -Id $PID).Path }) -replace '\\', '/'
    $localRc = @("common `"--repository_cache=$bazelOut/cache/repos`"", "common `"--disk_cache=$bazelOut/cache/disk`"",
        "common `"--repo_env=ES2_DEPENDENCIES=$bazelDeps`"", "common `"--repo_env=ES2_PWSH=$bazelPwsh`"", "build `"--action_env=PATH=$bazelDeps/llvm-mingw/bin;C:/Windows/System32`"")
    [IO.File]::WriteAllText((Join-Path $Clone 'local.bazelrc'), (($localRc -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
    $SourceCommit = (& $Git -C $Clone rev-parse HEAD).Trim()
    $branchName = (& $Git -C $Clone rev-parse --abbrev-ref HEAD).Trim()
    $Notes.Add("Cloned $Source at $SourceCommit ($branchName) with --no-hardlinks; uncommitted changes of the source are not part of the check.")
    $Stages['clone'] = [Math]::Round(([DateTime]::UtcNow - $t0).TotalSeconds, 1)

    # ---- 2. Dependencies --------------------------------------------------------------------------------
    $t0 = [DateTime]::UtcNow
    $verifyLog = Join-Path $ReportDirectory 'verify-dependencies.log'
    $setup = Join-Path $Clone 'tools\setup-dependencies.ps1'
    if (!(Test-Path -LiteralPath $setup)) { throw "The clone has no tools\setup-dependencies.ps1" }
    $pwshExe = (Get-Process -Id $PID).Path
    $VerifyExit = Invoke-Logged $verifyLog { & $pwshExe -NoProfile -NonInteractive -File $setup -VerifyOnly -Destination $Dependencies }
    $Verify = @(Get-Content -LiteralPath $verifyLog | Where-Object { $_ -match '^\[(OK|MISSING|MISMATCH|WARN)\] ' } | ForEach-Object {
        $m = [regex]::Match($_, '^\[(\w+)\] (.*?) - (.*)$')
        [pscustomobject]@{ Status = $m.Groups[1].Value; Item = $m.Groups[2].Value; Detail = $m.Groups[3].Value }
    })
    if ($VerifyExit -ne 0) { $Problems.Add("Dependency check failed (setup-dependencies.ps1 -VerifyOnly exit $VerifyExit): " + ((@($Verify | Where-Object { $_.Status -in 'MISSING', 'MISMATCH' } | ForEach-Object { $_.Item })) -join ', ')) }
    $bazelVersion = (& $Bazel --version 2>&1 | Select-Object -First 1)
    $expectedVersion = (Get-Content -LiteralPath (Join-Path $Clone '.bazelversion') -TotalCount 1).Trim()
    if ("$bazelVersion" -ne "bazel $expectedVersion") { $Problems.Add("Bazel '$Bazel' reports '$bazelVersion'; .bazelversion requires $expectedVersion") }
    $Stages['verify'] = [Math]::Round(([DateTime]::UtcNow - $t0).TotalSeconds, 1)

    # Absolute paths the build files assume (documented in the report).
    $files = @(Get-ChildItem -LiteralPath $Clone -Recurse -File -Force -Include '*.bzl', 'BUILD.bazel', 'MODULE.bazel', '.bazelrc' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\\.git\\' })
    $hits = @(foreach ($f in $files) {
        foreach ($line in (Select-String -LiteralPath $f.FullName -Pattern '(?<![A-Za-z0-9_])[A-Za-z]:[/\\][A-Za-z0-9_.+/\\ -]*[A-Za-z0-9_./]' -AllMatches)) {
            if ($line.Line.TrimStart().StartsWith('#')) { continue }
            foreach ($m in $line.Matches) {
                [pscustomobject]@{ Path = ($m.Value.TrimEnd(' ', '/', '\') -replace '\\', '/'); File = $f.FullName.Substring($Clone.Length + 1).Replace('\', '/') }
            }
        }
    })
    $Assumed = @($hits | Group-Object Path | Sort-Object Name | ForEach-Object {
        [pscustomobject]@{ Path = $_.Name; Files = (($_.Group | Select-Object -ExpandProperty File -Unique) -join ', ') }
    })

    # ---- 3. Tests ---------------------------------------------------------------------------------------
    $bep = Join-Path $ReportDirectory 'bep.json'
    $testLog = Join-Path $ReportDirectory 'bazel-test.log'
    if ($NoTests) { $Notes.Add('Bazel was not run (-NoTests).') }
    elseif ($Problems.Count -and !$RunTestsAnyway) { $Notes.Add('Bazel was not run because a dependency check failed (use -RunTestsAnyway to try the build regardless).') }
    else {
        $t0 = [DateTime]::UtcNow
        Push-Location $Clone
        try {
            $bazelCommand = @("--output_base=$OutputBase", 'test', "--repo_contents_cache=$ContentsCache", "--local_resources=cpu=$Cpu") +
                @($Targets) + @('--test_env=ES2_DATA_ROOT=', '--repo_env=ES2_DATA_ROOT=', '--keep_going', "--build_event_json_file=$bep") + @($BazelArgs)
            $BazelExit = Invoke-Logged $testLog { & $Bazel @bazelCommand }
        } finally { Pop-Location }
        $Stages['bazel_test'] = [Math]::Round(([DateTime]::UtcNow - $t0).TotalSeconds, 1)
        $Notes.Add('bazel ' + ($bazelCommand -join ' '))
        $BuildErrors = @(Get-Content -LiteralPath $testLog | Where-Object { $_ -match '^(ERROR|FATAL):' } | Select-Object -First 12)

        # Per-target results from the build event stream.
        if (Test-Path -LiteralPath $bep) {
            $rows = foreach ($raw in [IO.File]::ReadLines($bep)) {
                if ($raw -notmatch '^\s*\{\s*"id"\s*:\s*\{\s*"testSummary"') { continue }
                $e = $raw | ConvertFrom-Json
                if (!$e.testSummary) { continue }
                $logs = @($e.testSummary.passed) + @($e.testSummary.failed) | Where-Object { $_ -and $_.uri -and $_.uri -match '/test\.log$' }
                $logPath = ''
                if ($logs) { try { $logPath = ([Uri]$logs[0].uri).LocalPath } catch { $logPath = '' } }
                $skip = ''; $tail = @()
                if ($logPath -and (Test-Path -LiteralPath $logPath)) {
                    $lines = @(Get-Content -LiteralPath $logPath -ErrorAction SilentlyContinue)
                    $skipLine = $lines | Where-Object { $_ -match '^\s*SKIP\b' -or $_ -match 'ES2_DATA_ROOT[^\r\n]*(not set|unset|empty)' -or $_ -match '\(skipped=\d+\)' -or $_ -match 'real error\.str files skipped' } | Select-Object -First 1
                    if ($skipLine) { $skip = "$skipLine".Trim() }
                    $tail = @($lines | Where-Object { $_.Trim() } | Select-Object -Last 8)
                }
                [pscustomobject]@{ Label = $e.id.testSummary.label; Status = "$($e.testSummary.overallStatus)"; Log = $logPath; Skip = $skip; Tail = $tail }
            }
            $Tests = @($rows | Sort-Object Label -Unique)
        } else { $Notes.Add('No build event file was written; per-target results are unavailable.') }
        if ($BazelExit -ne 0) { $Problems.Add("bazel test exited with $BazelExit") }
    }
} catch {
    $Problems.Add("Check aborted: $($_.Exception.Message)")
} finally {
    # ---- 5. Cleanup (always) ----------------------------------------------------------------------------
    if (Test-Path -LiteralPath $Clone) {
        try {
            Push-Location $WorkRoot
            & $Bazel "--output_base=$OutputBase" shutdown 2>&1 | Out-Null
            $ServerShutdown = "bazel --output_base=$OutputBase shutdown (exit $LASTEXITCODE)"
        } catch { $ServerShutdown = "bazel shutdown failed: $($_.Exception.Message)" } finally { Pop-Location }
    } else { $ServerShutdown = 'no bazel server started (no clone)' }
    if ($KeepClone) { $Notes.Add("Clone kept: $Clone") }
    else {
        try { $CloneRemoved = Remove-Clone $WorkRoot } catch { $Problems.Add("Cleanup failed: $($_.Exception.Message)") }
        if (!$CloneRemoved) { $Problems.Add("The temporary clone could not be removed completely: $WorkRoot") }
    }
}

# ---- 4. Report ----------------------------------------------------------------------------------------
$byStatus = @($Tests | Group-Object Status | Sort-Object Name | ForEach-Object { "$($_.Count) $($_.Name)" }) -join ', '
$failed = @($Tests | Where-Object { $_.Status -notin 'PASSED', 'FLAKY' })
$skipped = @($Tests | Where-Object { $_.Skip })
$passed = $VerifyExit -eq 0 -and $Problems.Count -eq 0
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Fresh-clone check $stamp")
$md.Add('')
$md.Add("- Source: ``$Source`` at ``$SourceCommit``$(if ($Ref) { " (ref $Ref)" })")
$md.Add("- Result: **$(if ($passed) { 'PASS' } else { 'FAIL' })**; dependencies: $(if ($null -eq $VerifyExit) { 'not checked' } elseif ($VerifyExit -eq 0) { 'verified' } else { 'missing or mismatched' }); ``bazel test`` exit: $(if ($null -eq $BazelExit) { 'not run' } else { $BazelExit })")
$md.Add("- Targets: ``$($Targets -join ' ')`` with ``--test_env=ES2_DATA_ROOT= --repo_env=ES2_DATA_ROOT=`` (no game data); tests: $(if ($byStatus) { $byStatus } else { 'none recorded' })")
$md.Add("- Seconds: " + (($Stages.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', '))
$md.Add("- Bazel server: $ServerShutdown; output base ``$OutputBase`` (and ``-contents``) left for ``tools\clean-worker-bazel.ps1 $leaf``")
$md.Add("- Temporary clone: $(if ($KeepClone) { 'kept' } elseif ($CloneRemoved) { 'removed' } else { 'NOT removed' })")
foreach ($n in $Notes) { $md.Add("- $n") }
$md.Add('')
$md.Add('## Problems')
$md.Add('')
if ($Problems.Count) { foreach ($p in $Problems) { $md.Add("- $p") } } else { $md.Add('None.') }
$md.Add('')
$md.Add('## Dependency check (setup-dependencies.ps1 -VerifyOnly)')
$md.Add('')
$md.Add('| Status | Item | Detail |')
$md.Add('| --- | --- | --- |')
foreach ($v in $Verify) { $md.Add("| $($v.Status) | $($v.Item) | $($v.Detail -replace '\|', '/') |") }
$md.Add('')
$md.Add('## What a fresh machine needs')
$md.Add('')
$md.Add('- Windows x64 with git, PowerShell 7 (`pwsh.exe` on PATH, or `ES2_PWSH`), the system Bazel named by `.bazelversion` on PATH and `C:\Windows\SysWOW64\ddraw.dll`; 7-Zip only for the installers.')
$md.Add('- The dependency directory beside the checkout (or `ES2_DEPENDENCIES`) with `python` (pinned copy), `llvm-mingw`, `sdl3-3.4.18`, and the `local.bazelrc` of `tools\write-local-bazelrc.ps1` (Bazel `--output_user_root`, repository cache and disk cache in the main checkout''s ignored `build\bazel`).')
$md.Add('- Network access on the first build: Bazel fetches the pinned BCR modules and its own JDK runtime unless the repository cache already holds them.')
$md.Add('- Original game data is not needed for the build and the quick tests; real-data tests skip when `ES2_DATA_ROOT` is empty.')
$md.Add('')
$md.Add('Absolute paths in `.bazelrc`, `MODULE.bazel` and `*.bzl` / `BUILD.bazel` (comments excluded):')
$md.Add('')
$md.Add('| Path | Files |')
$md.Add('| --- | --- |')
foreach ($a in $Assumed) { $md.Add("| ``$($a.Path)`` | $($a.Files) |") }
$md.Add('')
$md.Add("## Tests that skipped without data ($($skipped.Count))")
$md.Add('')
if ($skipped.Count) { foreach ($s in $skipped) { $md.Add("- ``$($s.Label)`` ($($s.Status)): $($s.Skip)") } } else { $md.Add('None recorded.') }
$md.Add('')
$md.Add("## Failing tests ($($failed.Count))")
$md.Add('')
if ($failed.Count) {
    foreach ($f in $failed) {
        $md.Add("- ``$($f.Label)``: $($f.Status)")
        foreach ($t in $f.Tail) { $md.Add("    $t") }
    }
} else { $md.Add('None.') }
if ($BuildErrors.Count) {
    $md.Add('')
    $md.Add('## Bazel errors (first lines of bazel-test.log)')
    $md.Add('')
    foreach ($e in $BuildErrors) { $md.Add("    $e") }
}
[IO.File]::WriteAllText((Join-Path $ReportDirectory 'report.md'), (($md -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
$summary = [ordered]@{
    stamp = $stamp; source = $Source; commit = $SourceCommit; pass = $passed; dependencies_exit = $VerifyExit; bazel_exit = $BazelExit
    tests = @($Tests | ForEach-Object { [ordered]@{ label = $_.Label; status = $_.Status; skip = $_.Skip } })
    problems = @($Problems); clone_removed = $CloneRemoved; output_base = $OutputBase; seconds = $Stages
}
[IO.File]::WriteAllText((Join-Path $ReportDirectory 'summary.json'), (($summary | ConvertTo-Json -Depth 5) + "`n"), (New-Object Text.UTF8Encoding $false))
Write-Output "Result: $(if ($passed) { 'PASS' } else { 'FAIL' }); report $(Join-Path $ReportDirectory 'report.md')"
if ($passed) { exit 0 } else { exit 1 }
