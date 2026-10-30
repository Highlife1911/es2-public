[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD and the data ISO (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'native-r111',
    [switch]$UseNativeColors,
    [switch]$Preview
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "InstallName must stay within $installRoot."
}
$manifest = Get-Content -LiteralPath (Join-Path $installation 'installation-manifest.json') -Raw | ConvertFrom-Json
foreach ($file in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','CW3220.DLL','BWCC32.DLL','SOS9503.DLL','SOSLIBS3.DLL')) {
    $record = @($manifest.hashes | Where-Object { $_.file -eq $file })
    if ($record.Count -ne 1) { throw "Missing or ambiguous original hash: $file" }
    $actual = (Get-FileHash -LiteralPath (Join-Path $installation $file) -Algorithm SHA256).Hash
    if ($actual -ne $record[0].sha256) { throw "Original file changed: $file" }
}
$mediaManifest = Join-Path $installation 'oracle-media-manifest.json'
if (Test-Path -LiteralPath $mediaManifest) {
    $media = Get-Content -LiteralPath $mediaManifest -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath (Join-Path $installation 'ddraw.dll') -Algorithm SHA256).Hash -ne $media.proxy_sha256) {
        throw 'The local media proxy changed; rebuild and validate it before launch.'
    }
    if ((Get-FileHash -LiteralPath (Join-Path $env:WINDIR 'SysWOW64/ddraw.dll') -Algorithm SHA256).Hash -ne $media.system_ddraw_sha256) {
        throw 'Windows DirectDraw changed; rebuild and validate the local media proxy before launch.'
    }
}
$iso = [IO.Path]::GetFullPath((Join-Path $ExtractedRoot 'earthsiege2-data.iso'))
if (!(Test-Path -LiteralPath $iso -PathType Leaf)) { throw "Missing data CD: $iso" }
$layer = if ($UseNativeColors) { '' } else { '256COLOR' }
$program = Join-Path $installation 'ES.EXE'
if ($Preview) {
    [ordered]@{preview=$true;installation=$installation;image=$iso;compatibility_layer=$layer;program=$program;original_hashes_verified=$true;installs_dependencies=$false} | ConvertTo-Json
    return
}
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE') -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -eq $installation
})
if ($running.Count) { throw 'EarthSiege II is already running from this installation. Close it before starting again.' }
$image = Get-DiskImage -ImagePath $iso
if (!$image.Attached) { $image = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
$volumes = @($image | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystemLabel -eq 'EARTHSIEGE2' })
if ($volumes.Count -ne 1) { throw 'Expected exactly one mounted EARTHSIEGE2 data volume.' }
$cdRoot = $volumes[0].DriveLetter + ':'
# Recreate the original installer's two-line configuration if Windows assigned a different CD letter.
$configPath = Join-Path $installation 'DATA/DRIVE.CFG'
$config = "$cdRoot`r`n$installation`r`n"
if ([IO.File]::ReadAllText($configPath) -ne $config) { [IO.File]::WriteAllText($configPath, $config, [Text.Encoding]::ASCII) }
$logRoot = Join-Path $DataRoot 'logs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$logPath = Join-Path $logRoot ('launch-' + $stamp + '-' + [Guid]::NewGuid().ToString('N') + '.json')
$record = [ordered]@{utc=[DateTime]::UtcNow.ToString('o');installation=$installation;image=$iso;cd=$cdRoot;compatibility_layer=$layer;program=$program;launcher_pid=$null;original_hashes_verified=$true;gameplay_acceptance=$false}
$savedLayer = [Environment]::GetEnvironmentVariable('__COMPAT_LAYER')
try {
    # This environment applies only to ES.EXE and its children. No persistent compatibility registry entry.
    $env:__COMPAT_LAYER = $layer
    $process = Start-Process -FilePath $program -WorkingDirectory $installation -WindowStyle Hidden -PassThru
    $record.launcher_pid = $process.Id
    [IO.File]::WriteAllText($logPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    Write-Output "EarthSiege II started (launcher PID $($process.Id)). Log: $logPath"
} finally {
    [Environment]::SetEnvironmentVariable('__COMPAT_LAYER', $savedLayer)
}
