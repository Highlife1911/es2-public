[CmdletBinding(DefaultParameterSetName='Record')]
param(
    [Parameter(ParameterSetName='Record',Mandatory)][string]$Name,
    [Parameter(ParameterSetName='Record',Mandatory)][string]$Launch,
    [Parameter(ParameterSetName='Record',Mandatory)][string]$WatchFile,
    [Parameter(ParameterSetName='Record')][ValidateRange(1,900000)][int]$Ticks=40000,
    [Parameter(ParameterSetName='Record')][int]$FrameLimit=0,
    [Parameter(ParameterSetName='Record')][switch]$Outcome,
    [Parameter(ParameterSetName='Verify',Mandatory)][string]$RecordingDirectory,
    [Parameter(ParameterSetName='Verify',Mandatory)][switch]$VerifyRecording,
    [Parameter(ParameterSetName='Verify')][ValidateRange(1,255)][int[]]$ModalOpenerKeys=@(),
    [string]$DataRoot='', [string]$InstallName='timing-r111',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [int]$TimeoutSeconds=2400, [int]$StallSeconds=120,
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
$OutputDirectory=Assert-SuitePlainPath $OutputDirectory
if(!$MockDirectory -and !$DryRun) { $OutputDirectory=Assert-SuiteChild $OutputDirectory (Join-Path $DataRoot 'logs') }
if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
if($VerifyRecording) {
    $RecordingDirectory=Assert-SuitePlainPath $RecordingDirectory
    $session=Get-Content -LiteralPath (Join-Path $RecordingDirectory 'session.json') -Raw | ConvertFrom-Json
    if($session.schema -ne 1 -or $session.record.input_mode -ne 'record') { throw 'Invalid recording session.' }
    $Name=$session.name; $Launch=$session.launch; $Ticks=$session.ticks; $FrameLimit=$session.frame_limit; $Outcome=[bool]$session.outcome
    $WatchFile=Join-Path $RecordingDirectory 'watches.txt'; $journal=Join-Path $RecordingDirectory 'input.jsonl'
    foreach($pair in @(@($WatchFile,$session.watches_sha256),@($journal,$session.journal_sha256),@((Join-Path $RecordingDirectory 'record.jsonl'),$session.tick_sha256))) {
        if((Get-SuiteFileState $pair[0]).sha256 -ne $pair[1]) { throw "Recording input changed: $($pair[0])" }
    }
} else { $journal=Join-Path $OutputDirectory 'input.jsonl' }
Assert-SuiteName $Name
if(!$Outcome -and ($FrameLimit -lt 1 -or $FrameLimit -gt $Ticks)) { throw 'Bounded recordings require 1 <= FrameLimit <= Ticks.' }
if($Outcome -and $FrameLimit) { throw 'Outcome recordings compare the complete run; FrameLimit must be zero.' }
$launchInfo=Read-SuiteLaunch $DataRoot $Launch
if($VerifyRecording -and $session.launch_manifest_sha256 -ne $launchInfo.manifest_sha256) { throw 'Launch manifest changed since recording.' }
$shared=@{DataRoot=$DataRoot;InstallName=$InstallName;OutputDirectory=$OutputDirectory;Launch=$Launch;InputJournal=$journal;
    WatchFile=$WatchFile;Ticks=$Ticks;TimeoutSeconds=$TimeoutSeconds;StallSeconds=$StallSeconds;KeepReturns=[bool]$Outcome;DryRun=[bool]$DryRun;MockDirectory=$MockDirectory}
if($DryRun) {
    if($VerifyRecording) {
        foreach($run in @('replay1','replay2')) { & (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') @shared -RunName $run -InputMode replay -VirtualTimePace 0 }
    } else { & (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') @shared -RunName record -InputMode record -VirtualTimePace 1 -Interactive }
    return
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
function Assert-RecordingRun($Report, [string]$Run, [string]$Mode) {
    foreach($key in @('input_mode','virtual_time_step_ms','virtual_time_pace','stalled','process_alive_at_end','stopped_after_identity_check','ticks_seen','proxy_sha256')) {
        if($key -notin $Report.PSObject.Properties.Name) { throw "Missing $Run report field: $key" }
    }
    if($Report.input_mode -ne $Mode -or $Report.virtual_time_step_ms -ne 47 -or $Report.stalled) { throw "Invalid $Run timing/input/stall report." }
    $pace=if($Mode -eq 'record'){1}else{0}
    if($Report.virtual_time_pace -ne $pace) { throw "Invalid $Run pace." }
    if($Outcome) {
        if($Report.process_alive_at_end -or $Report.stopped_after_identity_check) { throw "$Run did not exit naturally; no outcome acceptance." }
        foreach($file in @('results.dat','mission.var')) {
            if(!(Test-Path -LiteralPath (Join-Path $OutputDirectory "$Run-output/$file"))) { throw "$Run did not produce $file" }
        }
    } elseif($Report.ticks_seen -lt $FrameLimit) { throw "$Run ended before the comparison window." }
}
if(!$VerifyRecording) {
    Copy-Item -LiteralPath $WatchFile -Destination (Join-Path $OutputDirectory 'watches.txt')
    $shared.WatchFile=Join-Path $OutputDirectory 'watches.txt'
    $report=& (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') @shared -RunName record -InputMode record -VirtualTimePace 1 -Interactive | Select-Object -Last 1 | ConvertFrom-Json
    Assert-RecordingRun $report 'record' 'record'
    $session=@{schema=1;name=$Name;launch=$Launch;launch_manifest_sha256=$launchInfo.manifest_sha256;launch_set=$launchInfo;
        ticks=$Ticks;frame_limit=$FrameLimit;outcome=[bool]$Outcome;record=$report;
        journal_sha256=(Get-SuiteFileState $journal).sha256;watches_sha256=(Get-SuiteFileState $shared.WatchFile).sha256;
        tick_sha256=(Get-SuiteFileState (Join-Path $OutputDirectory 'record.jsonl')).sha256}
    Write-SuiteJson (Join-Path $OutputDirectory 'session.json') $session
    $session | ConvertTo-Json -Depth 12 -Compress
    return
}
# Both replays use a derived journal whose modal-screen messages wait for the
# screen's message loop (modal_pump_keys.py); the recording journal is unchanged.
# -ModalOpenerKeys names the keys that opened a screen during the recording.
$replayJournal=Join-Path $OutputDirectory 'input-replay.jsonl'
$deriveArguments=@('-B',(Join-Path $PSScriptRoot '../scripts/modal_pump_keys.py'),(Join-Path $RecordingDirectory 'record.jsonl'),
    $journal,'--output',$replayJournal,'--report',(Join-Path $OutputDirectory 'modal-frames.json'))
foreach($vk in $ModalOpenerKeys) { $deriveArguments+=@('--opener-vk',"$vk") }
$derive=Invoke-SuiteProcess $Python $deriveArguments
if($derive.status -ne 0) { throw "Modal pump-key derivation failed. $($derive.stderr)" }
$derive.stdout | Out-Host
$shared.InputJournal=$replayJournal
foreach($run in @('replay1','replay2')) {
    $report=& (Join-Path $PSScriptRoot 'run-prepared-suite.ps1') @shared -RunName $run -InputMode replay -VirtualTimePace 0 | Select-Object -Last 1 | ConvertFrom-Json
    Assert-RecordingRun $report $run 'replay'
    if($report.proxy_sha256 -ne $session.record.proxy_sha256) { throw 'Proxy changed since recording.' }
    Write-SuiteJson (Join-Path $OutputDirectory "$run-report.json") $report
}
$compare=@('-B',(Join-Path $PSScriptRoot '../scripts/recording_compare.py'),(Join-Path $RecordingDirectory 'record.jsonl'),
    (Join-Path $OutputDirectory 'replay1.jsonl'),(Join-Path $OutputDirectory 'replay2.jsonl'),
    '--record-outputs',(Join-Path $RecordingDirectory 'record-output'),'--replay1-outputs',(Join-Path $OutputDirectory 'replay1-output'),
    '--replay2-outputs',(Join-Path $OutputDirectory 'replay2-output'),'--journal',$journal,'--report',(Join-Path $OutputDirectory 'verification.json'))
if($FrameLimit) { $compare+=@('--frame-limit',"$FrameLimit") }
$comparison=Invoke-SuiteProcess $Python $compare
$comparison.stdout | Out-Host
if($comparison.status -ne 0) { throw "Recording or replays differ; no suite draft produced. $($comparison.stderr)" }
if(!(Test-Path -LiteralPath (Join-Path $OutputDirectory 'verification.json'))) { throw 'Comparator did not produce its report.' }
$draft=[ordered]@{name=$Name;launch=$Launch;script=$replayJournal;recording=$RecordingDirectory;watches=$WatchFile;ticks=$Ticks;outcome=[bool]$Outcome}
if($FrameLimit) { $draft.frame_limit=$FrameLimit }
Write-SuiteJson (Join-Path $OutputDirectory 'suite-entry.json') $draft
Write-SuiteJson (Join-Path $OutputDirectory 'draft-suites.json') @{format=2;suites=@($draft)}
$draft | ConvertTo-Json -Depth 6
