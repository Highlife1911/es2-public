[CmdletBinding()]
param(
    [string]$DataRoot='', [string]$InstallName='timing-r111',
    [Parameter(Mandatory)][string]$OutputDirectory, [Parameter(Mandatory)][string]$RunName,
    [Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$WatchFile,
    [int]$Ticks=8000, [int]$TimeoutSeconds=300, [int]$StallSeconds=30,
    [string]$Hooks='', [string]$Launch='', [switch]$DryRun, [string]$MockDirectory='',
    # Extra DBSIM arguments, passed on to run-prepared-suite.ps1 as given.
    [string[]]$GameArguments=@(),
    # Tape suites: passed on to run-prepared-suite.ps1.
    [switch]$TapeTransaction, [string]$DemoTape='',
    # Passed on to run-prepared-suite.ps1.
    [switch]$HoldAtTicks
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
# The replay parameters of run-prepared-suite.ps1; its shared transaction also restores missing,
# created or deleted files and attempts every restoration after a failure.
$ErrorActionPreference='Stop'
$gameSplat=@{}
if(@($GameArguments).Count) { $gameSplat.GameArguments=@($GameArguments) }
if($TapeTransaction) { $gameSplat.TapeTransaction=$true }
if($DemoTape) { $gameSplat.DemoTape=$DemoTape }
if($HoldAtTicks) { $gameSplat.HoldAtTicks=$true }
$report=& (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') -DataRoot $DataRoot -InstallName $InstallName `
    -OutputDirectory $OutputDirectory -RunName $RunName -Launch $Launch -InputJournal $Script -WatchFile $WatchFile `
    -Ticks $Ticks -TimeoutSeconds $TimeoutSeconds -StallSeconds $StallSeconds -SnapshotOnStall -Hooks $Hooks `
    -DryRun:$DryRun -MockDirectory $MockDirectory @gameSplat | Select-Object -Last 1
if(!$DryRun) { Copy-Item -LiteralPath $Script -Destination (Join-Path $OutputDirectory "$RunName-input.jsonl") }
$report
