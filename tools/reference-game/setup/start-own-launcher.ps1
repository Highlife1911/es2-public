[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The data ISO's directory, as start-native.ps1 -ExtractedRoot.
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'calibration-r111',
    [switch]$UseNativeColors,
    [switch]$AllowOracleConfig,
    [string]$LauncherArguments = '',
    [switch]$DryRun
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
# Starts the game through our own launcher (launcher/) instead of
# ES.EXE. Same checks and environment as start-native.ps1, which stays the
# owner's normal start. Contract: docs/reverse-engineering/launcher.md.
$ErrorActionPreference = 'Stop'
if ($InstallName -ieq 'native-r111') { throw 'native-r111 is the primary installation; use a measurement clone.' }
if ($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$launcherName = 'ES2LAUNCH.EXE'
$originals = @('ES.EXE','VSHELL.EXE','DBSIM.EXE','CW3220.DLL','BWCC32.DLL','SOS9503.DLL','SOSLIBS3.DLL')
if ($launcherName -in $originals) { throw 'The launcher must not use an original file name.' }

$nativeArgs = @{DataRoot=$DataRoot; ExtractedRoot=$ExtractedRoot; InstallName=$InstallName; Preview=$true}
if ($UseNativeColors) { $nativeArgs.UseNativeColors = $true }
# Hash checks of the originals and of the proxy; mounts nothing, starts nothing.
$preview = (& (Join-Path $PSScriptRoot 'start-native.ps1') @nativeArgs) | Out-String | ConvertFrom-Json
$installation = $preview.installation
# Oracle runs leave input replay, virtual time, tick logs, hooks or probes in
# es2-oracle.ini; a person cannot play with those (input is scripted, time
# runs unpaced). Refuse such an installation unless explicitly allowed.
$ini = Join-Path $installation 'es2-oracle.ini'
$oracleSections = @()
if (Test-Path -LiteralPath $ini) {
    $section = ''
    foreach ($line in (Get-Content -LiteralPath $ini)) {
        $text = $line.Trim()
        if ($text -match '^\[(.+)\]$') { $section = $Matches[1].ToLowerInvariant(); continue }
        if ($section -in @('input','time','trace','hooks','coverage','probes','perf') -and $text -and $section -notin $oracleSections) { $oracleSections += $section }
    }
}
if ($oracleSections.Count -and !$AllowOracleConfig) {
    throw "$InstallName carries oracle settings ($($oracleSections -join ', ')) in es2-oracle.ini; use an interactive clone such as calibration-r111, or -AllowOracleConfig."
}
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$logPath = Join-Path $DataRoot "logs/own-launcher-$stamp.log"
$plan = [ordered]@{
    installation=$installation; launcher=(Join-Path $installation $launcherName); image=$preview.image
    compatibility_layer=$preview.compatibility_layer; launch_log=$logPath; dry_run=[bool]$DryRun
    oracle_sections=@($oracleSections); launcher_arguments=$LauncherArguments
    expected_first_child='vshell.EXE dummy -eggplant -X1 -<language byte unless E>'
}
if ($DryRun) { $plan | ConvertTo-Json; return }

$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE',$launcherName) -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -eq $installation
})
if ($running.Count) { throw 'EarthSiege II is already running from this installation. Close it before starting again.' }

Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
$built = Copy-Es2BazelOutput -Target '//launcher:es2launch' -Destination (Join-Path $repository 'build/reference-game')
Copy-Item -LiteralPath $built -Destination $plan.launcher -Force

$image = Get-DiskImage -ImagePath $preview.image
if (!$image.Attached) { $image = Mount-DiskImage -ImagePath $preview.image -Access ReadOnly -PassThru }
$volumes = @($image | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystemLabel -eq 'EARTHSIEGE2' })
if ($volumes.Count -ne 1) { throw 'Expected exactly one mounted EARTHSIEGE2 data volume.' }
$cdRoot = $volumes[0].DriveLetter + ':'
$configPath = Join-Path $installation 'DATA/DRIVE.CFG'
$config = "$cdRoot`r`n$installation`r`n"
if ([IO.File]::ReadAllText($configPath) -ne $config) { [IO.File]::WriteAllText($configPath, $config, [Text.Encoding]::ASCII) }

$priorLayer = $env:__COMPAT_LAYER
$priorLog = $env:ES2_LAUNCH_LOG
try {
    # Applies only to the launcher and its children, like start-native.ps1.
    $env:__COMPAT_LAYER = $preview.compatibility_layer
    $env:ES2_LAUNCH_LOG = $logPath
    $start = @{FilePath=$plan.launcher; WorkingDirectory=$installation; WindowStyle='Hidden'; PassThru=$true}
    # Passed unchanged, like the command line of ES.EXE (e.g. '-SPRUNKNOWN').
    if ($LauncherArguments) { $start.ArgumentList = $LauncherArguments }
    $process = Start-Process @start
    $plan.launcher_pid = $process.Id
    $plan.launcher_sha256 = (Get-FileHash -LiteralPath $plan.launcher -Algorithm SHA256).Hash
    $plan | ConvertTo-Json
    Write-Output "EarthSiege II started through our launcher (PID $($process.Id)). Log: $logPath"
} finally {
    $env:__COMPAT_LAYER = $priorLayer
    $env:ES2_LAUNCH_LOG = $priorLog
}
