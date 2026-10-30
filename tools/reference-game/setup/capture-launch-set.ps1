[CmdletBinding(DefaultParameterSetName='Capture')]
param(
    [string]$DataRoot='', [string]$InstallName='timing-r111',
    [Parameter(Mandatory)][string]$Name,
    [Parameter(ParameterSetName='Capture')][ValidateRange(1,3600)][int]$TimeoutSeconds=300,
    [Parameter(ParameterSetName='Derive',Mandatory)][string]$FromLaunch,
    [Parameter(ParameterSetName='Derive',Mandatory)][string]$PlayerMec,
    [switch]$DryRun, [string]$MockDirectory=''
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
# Observe a NEW DBSIM in the named clone. Never start or stop any process.
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'SuiteLaunch.psm1') -Force
if($MockDirectory) { Assert-SuiteMockRoot $DataRoot }
Assert-SuiteName $Name
$install=Get-SuiteInstallation $DataRoot $InstallName -DryRun:$DryRun
$destination=Assert-SuiteChild (Join-Path $DataRoot "oracle/launch/$Name") (Join-Path $DataRoot 'oracle/launch')
if(Test-Path -LiteralPath $destination) { throw 'Launch sets are immutable; choose a new name.' }
$files=@('script.dat','player.mec','mission.var','MISSION.STR')
$source=Join-Path $install 'DATA'; $provenance=$null
if($FromLaunch) {
    $parent=Read-SuiteLaunch $DataRoot $FromLaunch; $source=$parent.directory
    $null=Get-SuiteFileState $PlayerMec
    if(!(Test-Path -LiteralPath $PlayerMec -PathType Leaf)) { throw 'Missing generated MEC.' }
    $provenance=@{kind='generated-loadout';parent=$parent;player_mec_sha256=(Get-SuiteFileState $PlayerMec).sha256}
}
if($DryRun) { @{dry_run=$true;destination=$destination;source=$source;files=$files;wait_for_new_exe=$(if(!$FromLaunch){Join-Path $install 'DBSIM.EXE'}else{$null});timeout_seconds=$TimeoutSeconds;provenance=$provenance} | ConvertTo-Json -Depth 12 -Compress; return }
$expected=Join-Path $install 'DBSIM.EXE'
$samples=$null; $sampleIndex=0
if($MockDirectory) { $samples=Get-Content -LiteralPath (Join-Path $MockDirectory 'process-samples.json') -Raw | ConvertFrom-Json }
function Find-CaptureProcess {
    if($samples) {
        $index=[Math]::Min($script:sampleIndex,@($samples.polls).Count-1)
        $script:sampleIndex++
        return @($samples.polls[$index] | Where-Object { $_.exe -ieq $expected })
    }
    return @(Get-CimInstance Win32_Process -Filter "Name='DBSIM.EXE'" | Where-Object { $_.ExecutablePath -ieq $expected } | ForEach-Object {
        @{pid=$_.ProcessId;exe=$_.ExecutablePath;process_start_utc=([DateTime]$_.CreationDate).ToUniversalTime().ToString('o')}
    })
}
if(!$FromLaunch) {
    $process=$null
    if(@(Find-CaptureProcess).Count) { throw 'Arm capture before DBSIM starts; an existing process is not a new launch.' }
    $armed=if($samples){[DateTime]$samples.armed_utc}else{[DateTime]::UtcNow}
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $found=@(Find-CaptureProcess)
        if($found.Count -gt 1) { throw 'Ambiguous DBSIM process identity.' }
        if($found.Count -eq 1) {
            $process=$found[0]
            if(([DateTime]$process.process_start_utc).ToUniversalTime() -lt $armed.ToUniversalTime()) { throw 'Observed historical process start time.' }
            break
        }
        if(!$samples) { Start-Sleep -Milliseconds 50 }
        elseif($script:sampleIndex -ge @($samples.polls).Count) { throw 'Mock capture timeout.' }
    } while([DateTime]::UtcNow -lt $deadline)
    if(!$process) { throw 'Timed out waiting for new DBSIM; no launch set captured.' }
    $provenance=@{kind='vshell-handoff';installation=$install;armed_utc=$armed.ToUniversalTime().ToString('o');process=$process}
}
$parentDirectory=Split-Path -Parent $destination
New-Item -ItemType Directory -Force -Path $parentDirectory | Out-Null
$staging=Join-Path $parentDirectory ('.capture-'+$Name+'-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    $entries=@()
    foreach($file in $files) {
        $path=if($FromLaunch -and $file -eq 'player.mec'){$PlayerMec}else{Join-Path $source $file}
        $before=Get-SuiteFileState $path
        if(!$before -or !$before.size) { throw "Missing/empty launch input: $file" }
        Copy-Item -LiteralPath $path -Destination (Join-Path $staging $file)
        $copy=Get-SuiteFileState (Join-Path $staging $file)
        if($copy.sha256 -ne $before.sha256) { throw "Input changed while copying: $file" }
        $entries+=@{file=$file;size=$copy.size;sha256=$copy.sha256}
    }
    foreach($entry in $entries) {
        $path=if($FromLaunch -and $entry.file -eq 'player.mec'){$PlayerMec}else{Join-Path $source $entry.file}
        if((Get-SuiteFileState $path).sha256 -ne $entry.sha256) { throw "Input changed during capture: $($entry.file)" }
    }
    if(!$FromLaunch) {
        $after=@(Find-CaptureProcess)
        if($after.Count -ne 1 -or $after[0].pid -ne $process.pid -or $after[0].process_start_utc -ne $process.process_start_utc) { throw 'DBSIM identity changed during capture.' }
    }
    Write-SuiteJson (Join-Path $staging 'manifest.json') @{schema=1;name=$Name;utc=[DateTime]::UtcNow.ToString('o');files=$entries;provenance=$provenance}
    $null=Assert-SuiteChild $staging $parentDirectory
    $null=Assert-SuiteChild $destination $parentDirectory
    Move-Item -LiteralPath $staging -Destination $destination
} finally {
    if(Test-Path -LiteralPath $staging) { $null=Assert-SuiteChild $staging $parentDirectory; Remove-Item -LiteralPath $staging -Recurse -Force }
}
Read-SuiteLaunch $DataRoot $Name | ConvertTo-Json -Depth 12 -Compress
