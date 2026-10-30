[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The data ISO's directory, as start-native.ps1 -ExtractedRoot.
    [string]$ExtractedRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    [Parameter(Mandatory)][string]$OutputPath,
    [switch]$Preview,
    [switch]$Interactive,
    # Extra DBSIM arguments, appended to the fixed ones exactly as given and in
    # this order (quote each element: '-ptapes\demo1'). Arguments that repeat or
    # contradict a fixed one are refused (Assert-GameArguments).
    [string[]]$GameArguments = @()
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
Assert-MeasurementName $InstallName
$GameArguments = @($GameArguments)
Assert-GameArguments $GameArguments
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
Assert-NoLinks $installation
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must remain inside $installRoot." }
$manifest = Get-Content -LiteralPath (Join-Path $installation 'installation-manifest.json') -Raw | ConvertFrom-Json
if (!$manifest.measurement_clone_of -or $manifest.destination -ne $installation -or $manifest.measurement_clone_of -eq $installation) {
    throw 'Direct measurement launch requires a separate measurement clone with an explicit source.'
}
if ($manifest.language -ne 'German') { throw 'This verified original launch command is for the German reference.' }
# Another session's run holds this clone (CloneGuard.psm1): refuse before anything else.
if (!$Preview) { Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Launch report already exists.' }
if (!(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($OutputPath)) -PathType Container)) { throw 'Launch report parent is missing.' }
& (Join-Path $PSScriptRoot 'start-native.ps1') -DataRoot $DataRoot -ExtractedRoot $ExtractedRoot -InstallName $InstallName -Preview | Out-Null
$missionFiles = @('DATA/script.dat','DATA/player.mec','DATA/mission.var','DATA/mission.str')
$inputs = foreach ($relative in $missionFiles) {
    $file = Get-Item -LiteralPath (Join-Path $installation $relative)
    if (!$file.Length) { throw "Missing prepared mission data: $relative" }
    [ordered]@{file=$relative;size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
}
$iso = [IO.Path]::GetFullPath((Join-Path $ExtractedRoot 'earthsiege2-data.iso'))
$arguments = @(Get-DefaultGameArguments) + $GameArguments
$record = [ordered]@{
    utc=[DateTime]::UtcNow.ToString('o');installation=$installation;source=$manifest.measurement_clone_of
    exe=(Join-Path $installation 'DBSIM.EXE');arguments=$arguments;working_directory=$installation
    compatibility_layer='256COLOR';prepared_mission_inputs=@($inputs);image=$iso
    launch_kind='original_ES_wrapper_simulator_arguments';named_save_slot_loaded=$false
    preview=[bool]$Preview;interactive=[bool]$Interactive;pid=$null;process_start_utc=$null
}
# Only a run with extra arguments carries the key: the record is otherwise unchanged.
if ($GameArguments.Count) { $record.game_arguments = $GameArguments }
# Only a clone with the own cw3220.dll (configure-runtime.ps1) carries these keys
# (start-native.ps1 checked both DLLs above).
$runtimeManifest = Join-Path $installation 'oracle-runtime-manifest.json'
if (Test-Path -LiteralPath $runtimeManifest) {
    $runtime = Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
    $record.runtime = 'own'; $record.runtime_sha256 = $runtime.own_sha256
}
if ($Preview) { $record | ConvertTo-Json -Depth 6; return }
$parallel = @()
if ($env:ES2_PARALLEL_CLONES) {
    foreach ($name in $env:ES2_PARALLEL_CLONES.Split(',')) {
        Assert-MeasurementName $name
        $clone=Join-Path $installRoot $name; Assert-NoLinks $clone
        $identity=Get-Content -LiteralPath (Join-Path $clone 'installation-manifest.json') -Raw | ConvertFrom-Json
        if (!$identity.measurement_clone_of -or $identity.destination -ne $clone -or $identity.measurement_clone_of -eq $clone) { throw 'Invalid parallel clone identity.' }
        $parallel += $clone
    }
    if ($installation -notin $parallel -or $Interactive) { throw 'Parallel launches require a listed clone and a hidden window.' }
}
# Machine-wide rule (CloneGuard.psm1): a game process (ES/VSHELL/DBSIM) running from an
# installation under the installation root (Get-CloneInstallRoot) blocks this launch (this clone always;
# another installation, native-r111 included, unless it is a DBSIM of the listed parallel clones).
# Processes outside it (the stand-in of a Bazel test) are ignored; a process whose path cannot be read
# blocks. Nothing is stopped.
Assert-CloneMachineIdle -DataRoot $DataRoot -Installation $installation -PermittedClones $parallel
$disk = Get-DiskImage -ImagePath $iso
if (!$disk.Attached) { $disk = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
$volumes = @($disk | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystemLabel -eq 'EARTHSIEGE2' })
if ($volumes.Count -ne 1) { throw 'Expected one read-only mounted EARTHSIEGE2 data volume.' }
$cdRoot = $volumes[0].DriveLetter + ':'
$config = "$cdRoot`r`n$installation`r`n"
$driveFile = Join-Path $installation 'DATA/DRIVE.CFG'
if ([IO.File]::ReadAllText($driveFile) -ne $config) { [IO.File]::WriteAllText($driveFile, $config, [Text.Encoding]::ASCII) }
# Reserve the report before starting a process; a conflicting output must not launch a game.
$stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$priorLayer = $env:__COMPAT_LAYER
try {
    $env:__COMPAT_LAYER = '256COLOR'
    $windowStyle = if ($Interactive) { 'Normal' } else { 'Hidden' }
    # window-lint: allow gui-child DBSIM.EXE (the window style is the game window: Hidden unless the owner passes -Interactive, which must stay visible)
    $process = Start-Process -FilePath $record.exe -ArgumentList $arguments -WorkingDirectory $installation -WindowStyle $windowStyle -PassThru
    $record.pid = $process.Id
    $record.process_start_utc = $process.StartTime.ToUniversalTime().ToString('o')
    # Keep the handle for the runner's exit-code record (CloneGuard.psm1).
    Register-CloneStartedProcess $process
} finally {
    $env:__COMPAT_LAYER = $priorLayer
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Depth 6) + "`n")
    try { $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
}
Write-Output "Prepared mission started in measurement copy (DBSIM PID $($record.pid)). Report: $OutputPath"
