[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$VerifiedDirectory,
    [Parameter(Mandatory)][string]$BaselineSummary,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$DataRoot='', [string]$InstallName='timing-r111',
    [string]$Python='',
    [switch]$DryRun, [string]$MockDirectory=''
)
if (!$DataRoot -or !$Python) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$Python) { $Python = $es2.Python }
}
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'SuiteLaunch.psm1') -Force
if($MockDirectory) { Assert-SuiteMockRoot $DataRoot }
$null=Get-SuiteInstallation $DataRoot $InstallName -DryRun:$DryRun
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$tools=Join-Path $repo 'tools/reference-game'
$VerifiedDirectory=Assert-SuitePlainPath $VerifiedDirectory
$registry=Join-Path $VerifiedDirectory 'draft-suites.json'
$verification=Get-Content -LiteralPath (Join-Path $VerifiedDirectory 'verification.json') -Raw | ConvertFrom-Json
if(!$verification.identical) { throw 'A passing record/two-replay verification is required.' }
$drafts=@((Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json).suites)
if($drafts.Count -ne 1) { throw 'Expected one verified draft.' }
$s=$drafts[0]; Assert-SuiteName $s.name
$launch=Read-SuiteLaunch $DataRoot $s.launch
# Drafts name their recording; the script is the derived replay journal
# (modal pump keys), checked against the verification's derivation report.
$recording=if($s.recording) { $s.recording } else { Split-Path -Parent $s.script }
$recordedJournal=Join-Path $recording 'input.jsonl'
$session=Get-Content -LiteralPath (Join-Path $recording 'session.json') -Raw | ConvertFrom-Json
if($s.recording) {
    $modal=Get-Content -LiteralPath (Join-Path $VerifiedDirectory 'modal-frames.json') -Raw | ConvertFrom-Json
    if((Get-SuiteFileState $s.script).sha256 -ne $modal.replay_journal_sha256 -or $modal.journal_sha256 -ne $session.journal_sha256) { throw 'Draft replay journal does not match its derivation.' }
} else { $recordedJournal=$s.script }
if($session.name -ne $s.name -or $session.launch_manifest_sha256 -ne $launch.manifest_sha256 -or
   $session.launch -ne $s.launch -or $session.ticks -ne $s.ticks -or
   [bool]$session.outcome -ne [bool]$s.outcome -or [int]$session.frame_limit -ne [int]$s.frame_limit -or
   (Get-SuiteFileState (Join-Path $recording 'record.jsonl')).sha256 -ne $session.tick_sha256 -or
   (Get-SuiteFileState $recordedJournal).sha256 -ne $session.journal_sha256 -or
   (Get-SuiteFileState $s.watches).sha256 -ne $session.watches_sha256) { throw 'Draft no longer matches the verified recording.' }
$prior=Get-Content -LiteralPath $BaselineSummary -Raw | ConvertFrom-Json
if(!$prior.valid) { throw 'Baseline coverage summary is invalid.' }
$OutputDirectory=Assert-SuitePlainPath $OutputDirectory
if(!$MockDirectory -and !$DryRun) { $OutputDirectory=Assert-SuiteChild $OutputDirectory (Join-Path $DataRoot 'logs') }
if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
if($DryRun) {
    @{dry_run=$true;suite=$s;launch_set=$launch;delegate='run-live-coverage.ps1';mode='saturate';hooks='all';
      baseline=$BaselineSummary;output=$OutputDirectory;registry_edit=$false;canonical_golden_write=$false} | ConvertTo-Json -Depth 12 -Compress
    return
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$recheck=@('-B',(Join-Path $tools 'scripts/recording_compare.py'),(Join-Path $recording 'record.jsonl'),
    (Join-Path $VerifiedDirectory 'replay1.jsonl'),(Join-Path $VerifiedDirectory 'replay2.jsonl'),
    '--record-outputs',(Join-Path $recording 'record-output'),'--replay1-outputs',(Join-Path $VerifiedDirectory 'replay1-output'),
    '--replay2-outputs',(Join-Path $VerifiedDirectory 'replay2-output'),'--journal',$recordedJournal,
    '--report',(Join-Path $OutputDirectory 'verification-recheck.json'))
if($s.frame_limit) { $recheck+=@('--frame-limit',"$($s.frame_limit)") }
$checked=Invoke-SuiteProcess $Python $recheck
if($checked.status -ne 0) { throw "Verified logs changed: $($checked.stderr)" }
# The public coverage wrapper has no registry parameter. An isolated own-code
# driver lets it read a draft without changing either that script or the registry.
$driver=Join-Path $OutputDirectory 'driver/tools/reference-game'
foreach($part in @('setup','scripts','proxy','invitro')) { New-Item -ItemType Directory -Force -Path (Join-Path $driver $part) | Out-Null }
foreach($relative in @('setup/run-live-coverage.ps1','setup/CloneGuard.psm1','scripts/live_coverage_report.py','scripts/oracle_regression.py',
    'scripts/mission_watch_report.py','proxy/hook_registry.h','invitro/branch_sites.h')) {
    Copy-Item -LiteralPath (Join-Path $tools $relative) -Destination (Join-Path $driver $relative)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'run-draft-oracle-bridge.ps1') -Destination (Join-Path $driver 'setup/run-oracle-regression.ps1')
Copy-Item -LiteralPath $registry -Destination (Join-Path $driver 'oracle-suites.json')
Write-SuiteJson (Join-Path $driver 'setup/bridge.json') @{repository=$repo;runner=(Join-Path $PSScriptRoot 'run-oracle-regression.ps1');suites_file=$registry}
$provisional=Join-Path $OutputDirectory 'provisional'
$golden=Join-Path $provisional $s.name
New-Item -ItemType Directory -Path $golden -Force | Out-Null
$normal=@('-B',(Join-Path $tools 'scripts/oracle_regression.py'),'normalize',(Join-Path $VerifiedDirectory 'replay1.jsonl'),
    (Join-Path $golden 'normalized.json'),'--outputs',(Join-Path $VerifiedDirectory 'replay1-output'))
if($s.frame_limit) { $normal+=@('--frame-limit',"$($s.frame_limit)") }
$normalized=Invoke-SuiteProcess $Python $normal
if($normalized.status -ne 0) { throw "Provisional normalization failed: $($normalized.stderr)" }
Write-SuiteJson (Join-Path $golden 'manifest.json') @{commit='provisional-verified-recording';launch_set=$launch;
    verification_sha256=(Get-SuiteFileState (Join-Path $VerifiedDirectory 'verification.json')).sha256}
$measurement=Join-Path $OutputDirectory 'measurement'
$coverageArgs=@('-NoProfile','-File',(Join-Path $driver 'setup/run-live-coverage.ps1'),'-DataRoot',$DataRoot,
    '-InstallName',$InstallName,'-GoldenRoot',$provisional,'-OutputDirectory',$measurement,'-Suite',$s.name,
    '-CoverageMode','saturate','-CoverageHooks','all','-FirstHitTicks','-Python',$Python)
if($MockDirectory) { $coverageArgs+=@('-MockDirectory',$MockDirectory) }
$run=Invoke-SuiteProcess (Get-Process -Id $PID).Path $coverageArgs
$run.stdout | Out-Host
if($run.status -ne 0) { throw "Draft coverage rejected: $($run.stderr)" }
$delta=Invoke-SuiteProcess $Python @('-B',(Join-Path $tools 'scripts/suite_coverage_delta.py'),$BaselineSummary,
    (Join-Path $measurement 'coverage-summary.json'),'--output',(Join-Path $OutputDirectory 'coverage-gaps.json'))
$delta.stdout | Out-Host
if($delta.status -ne 0) { throw "Coverage delta failed: $($delta.stderr)" }
