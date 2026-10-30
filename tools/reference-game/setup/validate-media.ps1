[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD (cd; below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    # Where the smoke log and media-compat-validation.json go (default: <DataRoot>\logs).
    [string]$LogRoot = ''
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Invoke-Es2FixtureTests
# The IV32 binding library the proxy links (refusals, release, two own processes).
Invoke-Es2Bazel test //media/... --test_summary=terse | Out-Host
$guardHeaderHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../proxy/mission_map_guard.h') -Algorithm SHA256).Hash
# configure-media.ps1 and verify-native-rebuild.ps1 deploy/check this staged copy.
$proxy = Copy-Es2BazelOutput -Target '//tools/reference-game/proxy:ddraw.dll' -Destination (Join-Path $repository 'build/reference-game/proxy')
$smokeRoot = Join-Path $repository 'build/reference-game/media-smoke'
$probe = Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:probe_media' -Destination $smokeRoot
Copy-Item -LiteralPath $proxy -Destination $smokeRoot -Force
$configPath = Join-Path $smokeRoot 'es2-oracle.ini'
$logRoot = if ($LogRoot) { [IO.Path]::GetFullPath($LogRoot) } else { Join-Path $DataRoot 'logs' }
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$enabledConfig = "[video]`r`nsystem_indeo=1`r`nlog=$logRoot\media-proxy-smoke.jsonl`r`n[compat]`r`nmission_map_lifetime=1`r`n"
[IO.File]::WriteAllText($configPath, $enabledConfig, [Text.Encoding]::Unicode)
function Read-CodecMap {
    foreach ($hive in @([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryHive]::CurrentUser)) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,[Microsoft.Win32.RegistryView]::Registry32)
        try {
            $key = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $false)
            if ($key) {
                try { foreach ($name in ($key.GetValueNames() | Sort-Object)) { '{0}/{1}={2}' -f $hive,$name,$key.GetValue($name) } } finally { $key.Dispose() }
            }
        } finally { $base.Dispose() }
    }
}
$before = @(Read-CodecMap)
$priorLayer = $env:__COMPAT_LAYER
$results = @()
try {
    foreach ($layer in @('','256COLOR')) {
        $env:__COMPAT_LAYER = $layer
        foreach ($movie in @('AVG\INTR_PT1.AVI','AVG\INTR_PT2.AVI','AVI\INTR_PT1.AVI','AVI\INTR_PT2.AVI')) {
            $output = @(& $probe (Join-Path (Join-Path $ExtractedRoot 'cd') $movie) --proxy-mci)
            $exit = $LASTEXITCODE
            if ($exit -ne 0) { throw "Decode test failed: $movie, layer '$layer' ($exit): $output" }
            $events = @($output | ForEach-Object { $_ | ConvertFrom-Json })
            $display = $events | Where-Object kind -eq display
            $frames = $events | Where-Object kind -eq frames_decoded
            $module = $events | Where-Object { $_.kind -eq 'module' -and $_.name -eq 'ddraw.dll' }
            if ($layer -eq '256COLOR' -and $display.bits_pixel -ne 8) { throw 'Expected 8-bit color mode' }
            if ($frames.decoded -ne $frames.expected -or !$frames.decoded) { throw "Incomplete decode: $movie" }
            if ([IO.Path]::GetFullPath($module.path) -ne [IO.Path]::GetFullPath((Join-Path $smokeRoot 'ddraw.dll'))) { throw 'Media proxy was not loaded' }
            $results += [ordered]@{movie=$movie;layer=$layer;exit=$exit;events=$events}
            Write-Output "$movie | layer '$layer' | $($frames.decoded)/$($frames.expected) frames decoded"
        }
    }
    [IO.File]::WriteAllText($configPath, "[video]`r`nsystem_indeo=0`r`n", [Text.Encoding]::Unicode)
    $disabled = @(& $probe (Join-Path $ExtractedRoot 'cd/AVG/INTR_PT1.AVI') --proxy-mci)
    $disabledExit = $LASTEXITCODE
    if ($disabledExit -ne 1) { throw "Disabled proxy should retain baseline missing-codec behavior, got $disabledExit" }
} finally {
    $env:__COMPAT_LAYER = $priorLayer
    [IO.File]::WriteAllText($configPath, $enabledConfig, [Text.Encoding]::Unicode)
}
$after = @(Read-CodecMap)
$diff = @(Compare-Object $before $after)
if ($diff.Count) { throw ($diff | Out-String) }
$proof = [ordered]@{utc=[DateTime]::UtcNow.ToString('o');runs=$results;disabled_proxy_exit=$disabledExit;codec_registry_unchanged=$true;system_ddraw_sha256=(Get-FileHash -LiteralPath (Join-Path $env:WINDIR 'SysWOW64/ddraw.dll') -Algorithm SHA256).Hash;proxy_sha256=(Get-FileHash -LiteralPath (Join-Path $smokeRoot 'ddraw.dll') -Algorithm SHA256).Hash;mission_map_guard_tests=$true;iv32_bind_tests=$true;mission_map_guard_header_sha256=$guardHeaderHash;visual_acceptance=$false}
[IO.File]::WriteAllText((Join-Path $logRoot 'media-compat-validation.json'), ($proof | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output 'Video decode, local proxy loading, disabled fallback and unchanged codec mappings verified.'
