[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD and the data ISO (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'native-r111',
    [switch]$UseNativeColors,
    [switch]$CaptureCrash,
    [switch]$Preview,
    # own = ES2LAUNCH.EXE with the own graphical shell (ES2SHELL\es2shell.exe) deployed by configure-shell.ps1,
    # vshell = ES.EXE with the original VSHELL.EXE. Default: own when the installation has the own shell.
    [ValidateSet('', 'own', 'vshell')][string]$Shell = ''
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
$expectedFiles = @('ES.EXE','VSHELL.EXE','DBSIM.EXE','CW3220.DLL','BWCC32.DLL','SOS9503.DLL','SOSLIBS3.DLL')
# A clone may run the own cw3220.dll (configure-runtime.ps1): CW3220.DLL is
# then the recorded own DLL and the original is checked as CW3220-ORIGINAL.DLL.
$runtimeManifest = Join-Path $installation 'oracle-runtime-manifest.json'
$runtime = if (Test-Path -LiteralPath $runtimeManifest) { Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json } else { $null }
# retire-originals.ps1 removed DBSIM.EXE and VSHELL.EXE; only the own chain can run.
$retiredRecord = Join-Path $installation 'retired-originals.json'
$retired = if (Test-Path -LiteralPath $retiredRecord) { @((Get-Content -LiteralPath $retiredRecord -Raw | ConvertFrom-Json).files.PSObject.Properties.Name) } else { @() }
foreach ($file in $retired) {
    if (Test-Path -LiteralPath (Join-Path $installation $file)) { throw "Retired original file is back without retire-originals.ps1 -Restore: $file" }
}
foreach ($file in @($expectedFiles | Where-Object { $_ -notin $retired })) {
    $record = @($manifest.hashes | Where-Object { $_.file -eq $file })
    if ($record.Count -ne 1) { throw "Missing or ambiguous original hash: $file" }
    $path = Join-Path $installation $file
    if ($runtime -and $file -eq 'CW3220.DLL') {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $runtime.own_sha256) { throw 'The own runtime changed: CW3220.DLL' }
        $path = Join-Path $installation 'CW3220-ORIGINAL.DLL'
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
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
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
# The own shell deployed by configure-shell.ps1 (ES2LAUNCH.EXE, ES2SHELL\es2shell.exe, SDL3.dll, menu.ini).
$shellManifestPath = Join-Path $installation 'own-shell-manifest.json'
$ownShell = if (Test-Path -LiteralPath $shellManifestPath) { Get-Content -LiteralPath $shellManifestPath -Raw | ConvertFrom-Json } else { $null }
if (!$Shell) { $Shell = if ($ownShell) { 'own' } else { 'vshell' } }
if ($retired.Count -and $Shell -ne 'own') { throw "VSHELL.EXE is retired in $InstallName; only the own shell starts (retire-originals.ps1 -Restore for the original)." }
$shellDirectory = Join-Path $installation 'ES2SHELL'
$ownShellFiles = [ordered]@{launcher=(Join-Path $installation 'ES2LAUNCH.EXE'); shell=(Join-Path $shellDirectory 'es2shell.exe'); sdl=(Join-Path $shellDirectory 'SDL3.dll')}
if ($Shell -eq 'own') {
    if (!$ownShell) { throw "The own shell is not deployed in $InstallName (configure-shell.ps1 -InstallName $InstallName -Apply)." }
    foreach ($key in $ownShellFiles.Keys) {
        $path = $ownShellFiles[$key]
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $ownShell.files.$key) { throw "The own shell changed: $path" }
    }
}
$program = if ($Shell -eq 'own') { $ownShellFiles.launcher } else { Join-Path $installation 'ES.EXE' }
$crashCollector = $null
if ($CaptureCrash) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $crashCollector = Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:capture_crash' -Destination (Join-Path $repository 'build/reference-game')
}
if ($Preview) {
    $previewRecord = [ordered]@{preview=$true;installation=$installation;image=$iso;compatibility_layer=$layer;shell=$Shell;program=$program;original_hashes_verified=$true;installs_dependencies=$false;capture_crash=[bool]$CaptureCrash}
    if ($runtime) { $previewRecord.runtime = 'own'; $previewRecord.runtime_sha256 = $runtime.own_sha256 }
    if ($retired.Count) { $previewRecord.retired_originals = $retired }
    if ($Shell -eq 'own') { $previewRecord.own_shell = $ownShell.files; $previewRecord.launch_shell = $ownShell.launch_shell }
    $previewRecord | ConvertTo-Json
    return
}
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','ES2LAUNCH.EXE','es2shell.exe','es2menu.exe') -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -in @($installation, $shellDirectory)
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
$record = [ordered]@{utc=[DateTime]::UtcNow.ToString('o');installation=$installation;image=$iso;cd=$cdRoot;compatibility_layer=$layer;shell=$Shell;program=$program;launcher_pid=$null;original_hashes_verified=$true;gameplay_acceptance=$false;capture_crash=[bool]$CaptureCrash}
if ($runtime) { $record.runtime = 'own'; $record.runtime_sha256 = $runtime.own_sha256 }
$launcherLog = $null
if ($Shell -eq 'own') {
    # menu.ini follows the CD letter like DRIVE.CFG (the shell reads VER95\VSHELL.EXE's tables and the VOLs there).
    $iniPath = Join-Path $shellDirectory 'menu.ini'
    $iniLines = @([IO.File]::ReadAllText($iniPath) -split "`r?`n" | Where-Object { $_ -ne '' })
    $newIni = (@($iniLines | ForEach-Object { if ($_ -match '^\s*cd_root\s*=') { "cd_root = $cdRoot" } else { $_ } }) -join "`r`n") + "`r`n"
    if ([IO.File]::ReadAllText($iniPath) -ne $newIni) { [IO.File]::WriteAllText($iniPath, $newIni, [Text.Encoding]::ASCII) }
    $launcherLog = Join-Path $logRoot "own-launcher-$stamp.log"
    $record.launch_shell = $ownShell.launch_shell
    $record.launcher_log = $launcherLog
}
$saved = @{}
foreach ($name in @('__COMPAT_LAYER', 'ES2_LAUNCH_SHELL', 'ES2_LAUNCH_SHELL_CONSOLE', 'ES2_LAUNCH_LOG', 'ES2_LAUNCH_NO_UI', 'ES2_LAUNCH_SCRIPT')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
}
try {
    # This environment applies only to the launcher and its children. No persistent compatibility registry entry.
    # VSHELL and DBSIM get the 256COLOR layer; es2launch leaves it out of the own shell's environment (launcher.md).
    $env:__COMPAT_LAYER = $layer
    foreach ($name in @('ES2_LAUNCH_SHELL', 'ES2_LAUNCH_SHELL_CONSOLE', 'ES2_LAUNCH_LOG', 'ES2_LAUNCH_NO_UI', 'ES2_LAUNCH_SCRIPT')) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
    if ($Shell -eq 'own') {
        $env:ES2_LAUNCH_SHELL = [string]$ownShell.launch_shell
        $env:ES2_LAUNCH_LOG = $launcherLog
    }
    if ($CaptureCrash) {
        $record.diagnostic_output = Join-Path $logRoot ('crash-capture-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N'))
        [IO.File]::WriteAllText($logPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        Write-Output "Diagnose aktiv. Dieses Fenster bis zum Spielende offen lassen. Berichte: $($record.diagnostic_output)"
        & $crashCollector --exe $program --working-directory $installation --output $record.diagnostic_output
        $record.diagnostic_exit_code = $LASTEXITCODE
        [IO.File]::WriteAllText($logPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        if ($record.diagnostic_exit_code -ne 0) {
            throw "Diagnose beendet (Code $($record.diagnostic_exit_code)). Berichte: $($record.diagnostic_output)"
        }
    } else {
        $process = Start-Process -FilePath $program -WorkingDirectory $installation -WindowStyle Hidden -PassThru
        $record.launcher_pid = $process.Id
        [IO.File]::WriteAllText($logPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        Write-Output "EarthSiege II started ($Shell shell, launcher PID $($process.Id)). Log: $logPath"
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
