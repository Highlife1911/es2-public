[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD (cd) and the data ISO (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    [ValidateSet('English','German','French')][string]$Language = 'German',
    [Parameter(Mandatory)][string]$OutputPath
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
# Read-only installation verification. The sole write is the requested new report.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $ExtractedRoot 'cd'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
foreach ($protectedRoot in @($installation, $sourceRoot)) {
    if ($OutputPath -eq $protectedRoot -or $OutputPath.StartsWith($protectedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputPath must be outside the installation and extracted source.'
    }
}
if (Test-Path -LiteralPath $OutputPath) { throw "Report already exists: $OutputPath" }
if (!(Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($OutputPath)) -PathType Container)) { throw 'The report parent directory must already exist.' }

$issues = [Collections.Generic.List[object]]::new()
$hashCache = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
$expected = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
$counts = [ordered]@{}
function Add-Issue([string]$Code, [string]$Path, [string]$Detail) {
    $issues.Add([ordered]@{code=$Code;path=$Path;detail=$Detail})
}
function Get-VerifiedHash([string]$Path) {
    if ($hashCache.ContainsKey($Path)) { return $hashCache[$Path] }
    try {
        if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'File is missing.' }
        $value = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        $hashCache[$Path] = $value
        return $value
    } catch { Add-Issue 'file_unreadable' $Path $_.Exception.Message; return $null }
}
function Read-Manifest([string]$Path) {
    try { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { Add-Issue 'manifest_unreadable' $Path $_.Exception.Message; return $null }
}
function Compare-Hash([string]$Path, [string]$Hash, [string]$Reason) {
    $actual = Get-VerifiedHash $Path
    if (!$Hash -or $Hash -notmatch '^[a-fA-F0-9]{64}$') { Add-Issue 'invalid_expected_hash' $Path $Reason }
    elseif ($actual -and $actual -ne $Hash) { Add-Issue 'hash_mismatch' $Path $Reason }
}
function Compare-Bytes([string]$Path, [byte[]]$Bytes, [string]$Reason) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($hasher.ComputeHash($Bytes)).Replace('-', '') }
    finally { $hasher.Dispose() }
    Compare-Hash $Path $hash $Reason
}

$extension = @{English='ENG';German='GER';French='FRE'}[$Language]
$languageCode = @{English='E';German='G';French='F'}[$Language]
foreach ($folder in @('DATA','SAV','TAPES','VOL',$Language.ToUpperInvariant())) {
    $counts[$folder] = 0
    $sourceFolder = Join-Path $sourceRoot $folder
    $targetFolder = Join-Path $installation $folder
    if (!(Test-Path -LiteralPath $sourceFolder -PathType Container)) { Add-Issue 'source_folder_missing' $sourceFolder 'Required original folder.'; continue }
    if (!(Test-Path -LiteralPath $targetFolder -PathType Container)) { Add-Issue 'target_folder_missing' $targetFolder 'Required copied folder.' }
    foreach ($entry in Get-ChildItem -LiteralPath $sourceFolder -Recurse -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { Add-Issue 'source_reparse_point' $entry.FullName 'Source must contain ordinary files and directories.'; continue }
        if ($entry.PSIsContainer) { continue }
        $relative = $entry.FullName.Substring($sourceRoot.Length + 1)
        $expected[$relative] = $entry.FullName
        $counts[$folder]++
    }
}
$rootFiles = @('CW3220.DLL','BWCC32.DLL','SOSLIBS3.DLL','HMIDRV.WIN','HMIMDRV.WIN','SOSLIB.INI','ESII.ICO','VERSION.TXT')
$ver95Files = @('ES.EXE','VSHELL.EXE','DBSIM.EXE','SOS9503.DLL')
foreach ($file in $rootFiles) { $expected[$file] = Join-Path $sourceRoot $file }
foreach ($file in $ver95Files) { $expected[$file] = Join-Path $sourceRoot ('VER95/' + $file) }
$expected['ERROR.STR'] = Join-Path $sourceRoot ($Language + '/ERROR.' + $extension)
$expected['DATA\MISSION.STR'] = Join-Path $sourceRoot ($Language + '/MISSION.' + $extension)
# These two files are generated, not copied verbatim from the CD.
$expected['DATA\DRIVE.CFG'] = ''
$expected['DATA\LANGUAGE.CFG'] = ''
$counts['root_originals'] = $rootFiles.Count + $ver95Files.Count + 1
# retire-originals.ps1 removed these; their sources are still checked, the installation must not hold them.
$retiredPath = Join-Path $installation 'retired-originals.json'
$retired = @()
if (Test-Path -LiteralPath $retiredPath -PathType Leaf) {
    $retiredRecord = Read-Manifest $retiredPath
    if ($retiredRecord) { $retired = @($retiredRecord.files.PSObject.Properties.Name) }
}
$compared = 0
$fileComparisons = [Collections.Generic.List[object]]::new()
foreach ($pair in ($expected.GetEnumerator() | Sort-Object Key)) {
    if (!$pair.Value) { continue }
    $sourceHash = Get-VerifiedHash $pair.Value
    $targetPath = Join-Path $installation $pair.Key
    if ($pair.Key -in $retired) {
        if (Test-Path -LiteralPath $targetPath) { Add-Issue 'retired_file_present' $targetPath 'Retired by retire-originals.ps1.' }
        $fileComparisons.Add([ordered]@{file=$pair.Key;source=$pair.Value;source_sha256=$sourceHash;installed_sha256=$null;retired=$true;matched=$false})
        $compared++
        continue
    }
    Compare-Hash $targetPath $sourceHash ('Original source: ' + $pair.Value)
    $targetHash = if ($hashCache.ContainsKey($targetPath)) { $hashCache[$targetPath] } else { $null }
    $fileComparisons.Add([ordered]@{
        file=$pair.Key;source=$pair.Value;source_sha256=$sourceHash;installed_sha256=$targetHash
        matched=([bool]$sourceHash -and [bool]$targetHash -and $sourceHash -eq $targetHash)
    })
    $compared++
}
$allowed = [Collections.Generic.HashSet[string]]::new($expected.Keys, [StringComparer]::OrdinalIgnoreCase)
foreach ($file in @('installation-manifest.json','oracle-media-manifest.json','ddraw.dll','es2-oracle.ini')) { [void]$allowed.Add($file) }
# The own launcher and shell (configure-shell.ps1); their hashes are checked against own-shell-manifest.json.
$ownShellPath = Join-Path $installation 'own-shell-manifest.json'
if (Test-Path -LiteralPath $ownShellPath -PathType Leaf) {
    $ownShell = Read-Manifest $ownShellPath
    foreach ($file in @('own-shell-manifest.json','ES2LAUNCH.EXE','ES2SHELL\es2shell.exe','ES2SHELL\SDL3.dll','ES2SHELL\menu.ini','ES2SHELL\shell.log')) { [void]$allowed.Add($file) }
    if ($ownShell) {
        Compare-Hash (Join-Path $installation 'ES2LAUNCH.EXE') $ownShell.files.launcher 'Own-shell manifest.'
        Compare-Hash (Join-Path $installation 'ES2SHELL\es2shell.exe') $ownShell.files.shell 'Own-shell manifest.'
        Compare-Hash (Join-Path $installation 'ES2SHELL\SDL3.dll') $ownShell.files.sdl 'Own-shell manifest.'
    }
}
# es2sim (configure-sim.ps1); its hashes are checked against own-sim-manifest.json.
$ownSimPath = Join-Path $installation 'own-sim-manifest.json'
if (Test-Path -LiteralPath $ownSimPath -PathType Leaf) {
    $ownSim = Read-Manifest $ownSimPath
    $simFiles = [ordered]@{sim='ES2SIM\es2sim.exe'; sdl='ES2SIM\SDL3.dll'; config='ES2SIM\es2sim.ini'; launch_ini='es2launch.ini'}
    [void]$allowed.Add('own-sim-manifest.json')
    if ($ownSim) {
        foreach ($key in @($ownSim.files.PSObject.Properties.Name)) {
            if (!$simFiles.Contains($key)) { Add-Issue 'own_sim_manifest' $ownSimPath "Unknown file key: $key"; continue }
            [void]$allowed.Add($simFiles[$key])
            Compare-Hash (Join-Path $installation $simFiles[$key]) $ownSim.files.$key 'Own-sim manifest.'
        }
    }
}
if ($retired.Count) { [void]$allowed.Add('retired-originals.json') }
$actualCount = 0
if (Test-Path -LiteralPath $installation -PathType Container) {
    foreach ($entry in Get-ChildItem -LiteralPath $installation -Recurse -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { Add-Issue 'target_reparse_point' $entry.FullName 'Installation must contain ordinary files and directories.'; continue }
        if ($entry.PSIsContainer) { continue }
        $actualCount++
        $relative = $entry.FullName.Substring($installation.Length + 1)
        if (!$allowed.Contains($relative)) { Add-Issue 'unexpected_file' $entry.FullName 'Fresh installation must not contain generated saves, results, or unknown files.' }
        if ($relative -match '^(DATA|SAV)[\\/]' -and ($entry.Attributes -band [IO.FileAttributes]::ReadOnly)) {
            Add-Issue 'mutable_file_readonly' $entry.FullName 'DATA and SAV files must permit normal game updates; the copied ReadOnly attribute must be cleared.'
        }
    }
} else { Add-Issue 'installation_missing' $installation 'Prepare a fresh installation before verification.' }

$manifestPath = Join-Path $installation 'installation-manifest.json'
$manifest = Read-Manifest $manifestPath
if ($manifest) {
    if ($manifest.destination -ne $installation -or $manifest.language -ne $Language -or !$manifest.bundled_runtime_approved -or $manifest.execution_tested -ne $false) {
        Add-Issue 'installation_manifest_metadata' $manifestPath 'Expected requested destination/language, approved local runtime, and execution_tested=false.'
    }
}
$cdRoot = $null
try {
    $image = Get-DiskImage -ImagePath (Join-Path $ExtractedRoot 'earthsiege2-data.iso')
    if (!$image.Attached) { throw 'Data CD is not mounted; verifier never mounts it.' }
    $volumes = @($image | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystemLabel -eq 'EARTHSIEGE2' })
    if ($volumes.Count -ne 1) { throw 'Expected exactly one mounted EARTHSIEGE2 data volume.' }
    $cdRoot = $volumes[0].DriveLetter + ':'
    if ($manifest -and $manifest.cd -ne $cdRoot) { Add-Issue 'cd_manifest_mismatch' $manifestPath 'Recorded CD letter differs from mounted image.' }
    Compare-Bytes (Join-Path $installation 'DATA/DRIVE.CFG') ([Text.Encoding]::ASCII.GetBytes("$cdRoot`r`n$installation`r`n")) 'Exact original-derived two-line configuration, ASCII/CRLF.'
} catch { Add-Issue 'mounted_cd_verification' $installation $_.Exception.Message }
Compare-Bytes (Join-Path $installation 'DATA/LANGUAGE.CFG') ([Text.Encoding]::ASCII.GetBytes($languageCode)) 'Exact original-derived one-character language code.'

$referencePath = Join-Path $repository 'data/reference_hashes.txt'
$reference = @{}
try {
    foreach ($line in Get-Content -LiteralPath $referencePath) {
        if ($line -match '^([a-fA-F0-9]{64})\s+(.+)$') {
            $key = $Matches[2].Replace('\','/')
            if ($reference.ContainsKey($key)) { Add-Issue 'duplicate_reference_hash' $referencePath $key }
            $reference[$key] = $Matches[1]
        }
    }
} catch { Add-Issue 'reference_hashes_unreadable' $referencePath $_.Exception.Message }
$pinned = @()
foreach ($file in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','CW3220.DLL','BWCC32.DLL','SOS9503.DLL','SOSLIBS3.DLL')) {
    $sourceFile = $(if ($file -in $ver95Files) { 'VER95/' } else { '' }) + $file
    $hash = $reference['extracted/cd/' + $sourceFile]
    Compare-Hash (Join-Path $sourceRoot $sourceFile) $hash 'Pinned repository reference SHA-256 (source).'
    if ($file -notin $retired) { Compare-Hash (Join-Path $installation $file) $hash 'Pinned repository reference SHA-256 (installed).' }
    $records = @($manifest.hashes | Where-Object { $_.file -eq $file })
    if ($records.Count -ne 1 -or $records[0].sha256 -ne $hash) { Add-Issue 'manifest_reference_hash' $manifestPath $file }
    $pinned += [ordered]@{file=$file;sha256=$hash}
}

$mediaPath = Join-Path $installation 'oracle-media-manifest.json'
$media = Read-Manifest $mediaPath
$proofPath = Join-Path $DataRoot 'logs/media-compat-validation.json'
$proof = Read-Manifest $proofPath
$proxyHash = Get-VerifiedHash (Join-Path $repository 'build/reference-game/proxy/ddraw.dll')
$guardHash = Get-VerifiedHash (Join-Path $PSScriptRoot '../proxy/mission_map_guard.h')
Compare-Hash (Join-Path $installation 'ddraw.dll') $media.proxy_sha256 'Own-file ownership manifest.'
Compare-Hash (Join-Path $installation 'ddraw.dll') $proxyHash 'Exact current locally built proxy.'
Compare-Hash (Join-Path $installation 'es2-oracle.ini') $media.config_sha256 'Own-file ownership manifest.'
Compare-Hash (Join-Path $env:WINDIR 'SysWOW64/ddraw.dll') $media.system_ddraw_sha256 'Exact system DirectDraw recorded by ownership manifest.'
$logRoot = Join-Path $DataRoot 'logs'
# The manifest's mission_map_guard says whether configure-media.ps1 ran with -VshellMeasures (the VSHELL-only keys
# are in the INI; older manifests say true) or without (the INI holds only the log path).
$vshellMeasures = $media -and $media.mission_map_guard -eq $true
$configText = if ($vshellMeasures) { "[video]`r`nsystem_indeo=1`r`nlog=$logRoot\video-compat.jsonl`r`n[compat]`r`nmission_map_lifetime=1`r`n" } else { "[video]`r`nlog=$logRoot\video-compat.jsonl`r`n" }
# configure-media.ps1 -OwnPresenter and -ReplaceHooks append [presentation] and [hooks] (every hook of
# proxy/hook-names.txt replaced); the manifest records both.
if ($media -and $media.own_presenter -eq $true) { $configText += "[presentation]`r`npresenter=gdi`r`ndirectdraw=windowed`r`n" }
if ($media -and $media.replaced_hooks -gt 0) {
    $hookNames = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot '../proxy/hook-names.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and !$_.StartsWith('#') })
    if ($hookNames.Count -ne $media.replaced_hooks) { Add-Issue 'replaced_hooks_count' $mediaPath "The manifest records $($media.replaced_hooks) replaced hooks; hook-names.txt lists $($hookNames.Count)." }
    $configText += "[hooks]`r`n" + (($hookNames | ForEach-Object { "$_=replaced`r`n" }) -join '')
}
$configBytes = [byte[]]([Text.Encoding]::Unicode.GetPreamble() + [Text.Encoding]::Unicode.GetBytes($configText))
Compare-Bytes (Join-Path $installation 'es2-oracle.ini') $configBytes $(if ($vshellMeasures) { 'Exact generated UTF-16 configuration enabling Indeo and mission-map lifetime guard.' } else { 'Exact generated UTF-16 configuration with the VSHELL-only measures off.' })
if (!$media -or $media.schema -ne 2 -or $media.kind -ne 'es2-own-code-media-proxy' -or $media.mission_map_guard -isnot [bool] -or $media.mission_map_guard_header_sha256 -ne $guardHash -or $media.external_dependency_installed -ne $false -or $media.system_codec_registry_changed -ne $false) {
    Add-Issue 'media_manifest_metadata' $mediaPath 'Expected own-code schema 2 with current guard header and no external/system installation.'
}
if (!$proof -or $proof.proxy_sha256 -ne $proxyHash -or $proof.system_ddraw_sha256 -ne $media.system_ddraw_sha256 -or !$proof.codec_registry_unchanged -or $proof.runs.Count -ne 8 -or !$proof.mission_map_guard_tests -or $proof.mission_map_guard_header_sha256 -ne $guardHash) {
    Add-Issue 'validation_proof_mismatch' $proofPath 'Expected completed media and guard validation for this exact proxy, guard header and system DirectDraw.'
}
$scriptHashes = @()
foreach ($relative in @('data/reference_hashes.txt','tools/reference-game/setup/prepare-native.ps1','tools/reference-game/setup/configure-media.ps1','tools/reference-game/setup/start-native.ps1','tools/reference-game/setup/verify-native-rebuild.ps1','tools/reference-game/setup/Es2Bazel.psm1','tools/reference-game/proxy/BUILD.bazel','tools/reference-game/proxy/generate_exports.py','tools/reference-game/proxy/media_proxy.c','tools/reference-game/proxy/mission_map_guard.h','tools/reference-game/helpers/probe_media.c','tools/reference-game/setup/validate-media.ps1','tools/reference-game/proxy/tests/mission_map_guard_fixture.c')) {
    $scriptHashes += [ordered]@{file=$relative;sha256=(Get-VerifiedHash (Join-Path $repository $relative))}
}
$report = [ordered]@{
    schema=1;utc=[DateTime]::UtcNow.ToString('o');kind='es2-native-fresh-rebuild-verification'
    installation=$installation;source=$sourceRoot;language=$Language;mounted_cd=$cdRoot
    passed=($issues.Count -eq 0);game_launched=$false;installation_modified=$false
    original_files_compared=$compared;installed_file_count=$actualCount;expected_file_count=$allowed.Count
    source_folder_file_counts=$counts;original_file_comparisons=@($fileComparisons.ToArray());pinned_originals=$pinned
    proxy_sha256=$proxyHash;guard_header_sha256=$guardHash
    validation_proof_sha256=(Get-VerifiedHash $proofPath);script_hashes=$scriptHashes
    issues=@($issues.ToArray())
}
# The last evidence hash may itself add an issue, so compute success afterwards.
$report.passed = $issues.Count -eq 0
$report.issues = @($issues.ToArray())
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 8) + "`n")
$stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
if (!$report.passed) { throw "Fresh rebuild verification failed with $($issues.Count) issue(s). Evidence: $OutputPath" }
Write-Output "PASS: $compared original files compared, $actualCount installed files, seven pinned binaries and current compatibility configuration verified. Evidence: $OutputPath"
