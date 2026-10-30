[CmdletBinding()]
param(
    [string]$DataRoot='', [string]$InstallName='timing-r111',
    [Parameter(Mandatory)][string]$OutputDirectory, [Parameter(Mandatory)][string]$RunName,
    [string]$Launch='', [Parameter(Mandatory)][string]$InputJournal,
    [Parameter(Mandatory)][string]$WatchFile, [int]$Ticks=8000,
    [int]$TimeoutSeconds=300, [int]$StallSeconds=30,
    [ValidateSet('record','replay')][string]$InputMode='replay',
    [ValidateSet(0,1)][int]$VirtualTimePace=0, [switch]$Interactive,
    [switch]$SnapshotOnStall, [switch]$KeepReturns, [string]$Hooks='', [string]$RepositoryRoot='',
    [switch]$DryRun, [string]$MockDirectory='',
    # Extra DBSIM arguments, passed on to run-tick-log-mission.ps1 as given.
    [string[]]$GameArguments=@(),
    # Tape suites: derive the tape plan from GameArguments (Get-SuiteTapePlan)
    # and let the transaction restore the tape files; -DemoTape (the one name of
    # the synthetic TAPES\DEMOLIST.STR for -D) implies it. Without either switch a
    # tape option is passed on unchanged (the caller's responsibility).
    [switch]$TapeTransaction, [string]$DemoTape='',
    # Passed on to run-tick-log-mission.ps1: the game thread parks after Ticks records.
    [switch]$HoldAtTicks
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'SuiteLaunch.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
$GameArguments=@($GameArguments)
Assert-GameArguments $GameArguments
if($MockDirectory) { Assert-SuiteMockRoot $DataRoot }
Assert-SuiteName $RunName
$install=Get-SuiteInstallation $DataRoot $InstallName -DryRun:$DryRun
if(!$RepositoryRoot) { $RepositoryRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')) }
$arguments=@{DataRoot=$DataRoot;InstallName=$InstallName;OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);RunName=$RunName;
    Ticks=$Ticks;TimeoutSeconds=$TimeoutSeconds;StallSeconds=$StallSeconds;VirtualTimeStepMs=47;VirtualTimePace=$VirtualTimePace;
    InputMode=$InputMode;InputJournal=[IO.Path]::GetFullPath($InputJournal);Watch=@(Get-Content -LiteralPath $WatchFile);Hooks=$Hooks}
if($Interactive) { $arguments.Interactive=$true }
if($GameArguments.Count) { $arguments.GameArguments=$GameArguments }
if($SnapshotOnStall) { $arguments.SnapshotOnStall=$true }
if($HoldAtTicks) { $arguments.HoldAtTicks=$true }
$launchInfo=if($Launch) { Read-SuiteLaunch $DataRoot $Launch } else { $null }
$tapePlan=if($TapeTransaction -or $DemoTape) { Get-SuiteTapePlan $GameArguments $DemoTape } else { $null }
if($DryRun) {
    $dry=@{dry_run=$true;runner='run-tick-log-mission.ps1';arguments=$arguments;launch_set=$launchInfo;restore_files=@('script.dat','player.mec','mission.var','MISSION.STR','results.dat','EXIT.CFG')}
    if($tapePlan) { $dry.tape_plan=$tapePlan; $dry.tape_restore_files=@(Get-SuiteTapeRestoreFiles $tapePlan) }
    $dry | ConvertTo-Json -Depth 12 -Compress; return
}
if($Ticks -lt 1 -or $TimeoutSeconds -lt 1 -or $StallSeconds -lt 1) { throw 'Positive tick/time limits required.' }
if(Test-Path -LiteralPath (Join-Path $OutputDirectory "$RunName.jsonl")) { throw 'Tick log already exists.' }
if($InputMode -eq 'record' -and (Test-Path -LiteralPath $InputJournal)) { throw 'Recording journal already exists.' }
if($InputMode -eq 'replay' -and !(Test-Path -LiteralPath $InputJournal -PathType Leaf)) { throw 'Replay journal missing.' }
$transaction=Invoke-SuiteTransaction -DataRoot $DataRoot -InstallName $InstallName -Launch $Launch -OutputDirectory $OutputDirectory -RunName $RunName -KeepReturns:$KeepReturns -Mock:([bool]$MockDirectory) -TapePlan $tapePlan -Action {
    if($MockDirectory) {
        # Only synthetic fixtures: no configure/build/start/process queries.
        $source=Join-Path $MockDirectory $RunName
        Copy-Item -LiteralPath (Join-Path $source 'tick.jsonl') -Destination (Join-Path $OutputDirectory "$RunName.jsonl")
        # Stands in for the hook mismatch log the proxy opens in compare mode.
        $hookLog = Join-Path $source 'tick.jsonl.hooks.jsonl'
        if(Test-Path -LiteralPath $hookLog) { Copy-Item -LiteralPath $hookLog -Destination (Join-Path $OutputDirectory "$RunName.jsonl.hooks.jsonl") }
        if($InputMode -eq 'record') { Copy-Item -LiteralPath (Join-Path $source 'input.jsonl') -Destination $InputJournal }
        $after=Join-Path $source 'data-after'
        if(Test-Path -LiteralPath $after) {
            foreach($f in Get-ChildItem -LiteralPath $after -File) {
                if($f.Name -notin @('script.dat','player.mec','mission.var','MISSION.STR','results.dat','EXIT.CFG')) { throw 'Unexpected mock mutation.' }
                Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $install "DATA/$($f.Name)") -Force
            }
        }
        return Get-Content -LiteralPath (Join-Path $source 'report.json') -Raw
    }
    & (Join-Path $RepositoryRoot 'tools/reference-game/setup/run-tick-log-mission.ps1') @arguments | Select-Object -Last 1
}
$result=$transaction.result | ConvertFrom-Json
$media=Get-Content -LiteralPath (Join-Path $install 'oracle-media-manifest.json') -Raw | ConvertFrom-Json
$result | Add-Member -NotePropertyName proxy_sha256 -NotePropertyValue $media.proxy_sha256 -Force
$result | Add-Member -NotePropertyName changed_and_restored -NotePropertyValue $transaction.changed_and_restored -Force
if($GameArguments.Count) { $result | Add-Member -NotePropertyName game_arguments -NotePropertyValue $GameArguments -Force }
if($transaction.launch_set) { $result | Add-Member -NotePropertyName launch_set -NotePropertyValue $transaction.launch_set -Force }
# Only a tape suite carries the keys; a recorded tape is reported by size and hash.
if($tapePlan) {
    $result | Add-Member -NotePropertyName tape_plan -NotePropertyValue $tapePlan -Force
    $result | Add-Member -NotePropertyName tape -NotePropertyValue $transaction.tape -Force
}
$result | ConvertTo-Json -Depth 12 -Compress
