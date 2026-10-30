[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The data ISO's directory, as start-native.ps1 -ExtractedRoot.
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'native-r111',
    [switch]$Apply,
    # The VSHELL-only measures ([video] system_indeo=1 and [compat] mission_map_lifetime=1). OFF by default (the
    # own shell is the default); pass it for an installation whose original VSHELL.EXE is still played
    # (native-r111) so that its intro movies and the mission-map lifetime guard keep working.
    [switch]$VshellMeasures,
    # [presentation] presenter=gdi and directdraw=windowed, the own presenter with DBSIM's DirectDraw in a window
    # (it survives the own shell's full screen; the setting of rebuild-r111).
    [switch]$OwnPresenter,
    # [hooks] with every hook of proxy/hook-names.txt replaced, so DBSIM runs the own code of each function.
    [switch]$ReplaceHooks
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$source = Join-Path $repository 'build/reference-game/proxy/ddraw.dll'
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$proof = Get-Content -LiteralPath (Join-Path $DataRoot 'logs/media-compat-validation.json') -Raw | ConvertFrom-Json
if ($proof.proxy_sha256 -ne $sourceHash -or !$proof.codec_registry_unchanged -or $proof.runs.Count -ne 8) { throw 'Run validate-media.ps1 successfully for this exact build first.' }
$guardHeaderHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../proxy/mission_map_guard.h') -Algorithm SHA256).Hash
if (!$proof.mission_map_guard_tests -or $proof.mission_map_guard_header_sha256 -ne $guardHeaderHash) { throw 'Mission map lifetime guard validation is missing or outdated; run validate-media.ps1.' }
$hookNames = @()
if ($ReplaceHooks) {
    $hookNames = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot '../proxy/hook-names.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and !$_.StartsWith('#') })
    if (!$hookNames.Count) { throw 'proxy/hook-names.txt lists no hook.' }
}
$systemHash = (Get-FileHash -LiteralPath (Join-Path $env:WINDIR 'SysWOW64/ddraw.dll') -Algorithm SHA256).Hash
if ($systemHash -ne $proof.system_ddraw_sha256) { throw 'Windows DirectDraw changed; rebuild and revalidate the proxy.' }
& (Join-Path $PSScriptRoot 'start-native.ps1') -DataRoot $DataRoot -ExtractedRoot $ExtractedRoot -InstallName $InstallName -Preview | Out-Null
$target = Join-Path $installation 'ddraw.dll'
$config = Join-Path $installation 'es2-oracle.ini'
$manifestPath = Join-Path $installation 'oracle-media-manifest.json'
$old = if (Test-Path -LiteralPath $manifestPath) { Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json } else { $null }
foreach ($item in @(@{path=$target;hash=$old.proxy_sha256}, @{path=$config;hash=$old.config_sha256})) {
    if ((Test-Path -LiteralPath $item.path) -and (!$item.hash -or (Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash -ne $item.hash)) {
        throw "Refusing to overwrite an unknown or edited file: $($item.path)"
    }
}
if (!$Apply) {
    $measures = if ($VshellMeasures) { 'enable process-local Indeo plus the mission map lifetime guard (-VshellMeasures)' } else { 'leave the VSHELL-only measures (process-local Indeo, mission map lifetime guard) off (pass -VshellMeasures for an installation that plays the original VSHELL.EXE)' }
    $play = @()
    if ($OwnPresenter) { $play += 'the own presenter (presenter=gdi, directdraw=windowed)' }
    if ($ReplaceHooks) { $play += "every one of the $($hookNames.Count) hooks replaced" }
    if ($play.Count) { $measures += '; ' + ($play -join ', ') }
    Write-Output "PREVIEW: copy the validated, own-code DirectDraw proxy to $target and $measures. No external dependencies, codec registration or system DLL replacement."
    return
}
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE') -and $_.ExecutablePath -and [IO.Path]::GetDirectoryName($_.ExecutablePath) -eq $installation
})
if ($running.Count) { throw 'Close EarthSiege II before configuring its local media proxy.' }
$logRoot = Join-Path $DataRoot 'logs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $target -Force
$configText = if ($VshellMeasures) { "[video]`r`nsystem_indeo=1`r`nlog=$logRoot\video-compat.jsonl`r`n[compat]`r`nmission_map_lifetime=1`r`n" } else { "[video]`r`nlog=$logRoot\video-compat.jsonl`r`n" }
if ($OwnPresenter) { $configText += "[presentation]`r`npresenter=gdi`r`ndirectdraw=windowed`r`n" }
if ($ReplaceHooks) { $configText += "[hooks]`r`n" + (($hookNames | ForEach-Object { "$_=replaced`r`n" }) -join '') }
[IO.File]::WriteAllText($config, $configText, [Text.Encoding]::Unicode)
$record = [ordered]@{schema=2;kind='es2-own-code-media-proxy';utc=[DateTime]::UtcNow.ToString('o');proxy_sha256=$sourceHash;config_sha256=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash;system_ddraw_sha256=$systemHash;mission_map_guard=[bool]$VshellMeasures;vshell_measures=[bool]$VshellMeasures;mission_map_guard_header_sha256=$guardHeaderHash;own_presenter=[bool]$OwnPresenter;replaced_hooks=$hookNames.Count;external_dependency_installed=$false;system_codec_registry_changed=$false}
[IO.File]::WriteAllText($manifestPath, ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Output $(if ($VshellMeasures) { "Local video compatibility and mission map lifetime guard enabled: $installation" } else { "Local media proxy deployed, VSHELL-only measures off: $installation" })
