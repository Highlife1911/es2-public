# Bazel entry points for the reference-game PowerShell scripts. Bazel is the
# only build system: scripts never call the compiler directly (the in-vitro
# mutation check runs Bazel in a scratch copy).
$ErrorActionPreference = 'Stop'

$script:Repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
# Bazel is the system installation on PATH; its
# version must equal the repository's .bazelversion. ES2_BAZEL overrides the path.
function Resolve-Es2Bazel {
    $path = $env:ES2_BAZEL
    if (!$path) {
        $command = Get-Command bazel -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (!$command) { throw 'Bazel not found on PATH (see docs/dependencies.md); set ES2_BAZEL to override.' }
        $path = $command.Source
    }
    $expected = (Get-Content -LiteralPath (Join-Path $script:Repository '.bazelversion') -TotalCount 1).Trim()
    $actual = (& $path --version 2>$null | Select-Object -First 1)
    if ($actual -ne "bazel $expected") { throw "Bazel at $path reports '$actual'; .bazelversion requires $expected." }
    return $path
}
$script:Bazel = $null   # resolved on first use: test sandboxes import this module without Bazel on PATH

function Invoke-Es2Bazel {
    <# Runs one Bazel command in the repository with the lock file enforced. #>
    param(
        [Parameter(Mandatory)][ValidateSet('build', 'test', 'cquery')][string]$Command,
        [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Arguments
    )
    if (!$script:Bazel) { $script:Bazel = Resolve-Es2Bazel }
    Push-Location $script:Repository
    try {
        $startup = @(); $cache = @()
        if ($env:ES2_BAZEL_OUTPUT_BASE) { $startup += "--output_base=$env:ES2_BAZEL_OUTPUT_BASE" }
        if ($env:ES2_BAZEL_CONTENTS_CACHE) { $cache += "--repo_contents_cache=$env:ES2_BAZEL_CONTENTS_CACHE" }
        $output = & $script:Bazel @startup $Command --lockfile_mode=error @cache @Arguments
        if ($LASTEXITCODE -ne 0) { throw "bazel $Command $($Arguments -join ' ') failed ($LASTEXITCODE)." }
        return $output
    } finally { Pop-Location }
}

function Get-Es2BazelOutput {
    <# Builds one single-output target and returns the absolute path of that output. #>
    param([Parameter(Mandatory)][string]$Target)
    Invoke-Es2Bazel build $Target | Out-Null
    $files = @(Invoke-Es2Bazel cquery $Target '--output=files' 2>$null)
    if ($files.Count -ne 1) { throw "Expected exactly one output for ${Target}: $($files -join ', ')" }
    Push-Location $script:Repository
    try {
        $startup = @(); $cache = @()
        if ($env:ES2_BAZEL_OUTPUT_BASE) { $startup += "--output_base=$env:ES2_BAZEL_OUTPUT_BASE" }
        if ($env:ES2_BAZEL_CONTENTS_CACHE) { $cache += "--repo_contents_cache=$env:ES2_BAZEL_CONTENTS_CACHE" }
        $root = (& $script:Bazel @startup info execution_root --lockfile_mode=error @cache 2>$null)
    } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0 -or !$root) { throw 'bazel info execution_root failed.' }
    $path = [IO.Path]::GetFullPath((Join-Path $root $files[0]))
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Bazel output missing: $path" }
    return $path
}

function Copy-Es2BazelOutput {
    <# Builds a target and copies its output into Destination (created if needed).
       Deployment scripts copy from there, so later Bazel builds cannot change
       a file between validation and deployment. Returns the copied path. #>
    param([Parameter(Mandatory)][string]$Target, [Parameter(Mandatory)][string]$Destination)
    $source = Get-Es2BazelOutput -Target $Target
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $copy = Join-Path $Destination ([IO.Path]::GetFileName($source))
    Copy-Item -LiteralPath $source -Destination $copy -Force
    return $copy
}

function Invoke-Es2FixtureTests {
    <# Runs the own-code fixture tests for hook, core and the proxy. Cached
       results are reused when nothing changed. No original game is launched. #>
    Invoke-Es2Bazel test //hook/... //core/... //tools/reference-game/proxy/... --test_summary=terse | Out-Host
}

function Get-Es2SourceStamp {
    $lines = foreach ($dir in @('tools/reference-game/proxy','tools/reference-game/setup','tools/reference-game/scripts','hook','core','media','bazel/defs','bazel/toolchains')) {
        if (Test-Path (Join-Path $script:Repository $dir)) {
            Get-ChildItem -LiteralPath (Join-Path $script:Repository $dir) -Recurse -File |
                Where-Object { $_.Extension -notin @('.pyc') } | Sort-Object FullName | ForEach-Object {
                    $_.FullName.Substring($script:Repository.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash
                }
        }
    }
    foreach ($name in @('MODULE.bazel','MODULE.bazel.lock','.bazelrc','tools/reference-game/invitro/branch_sites.h')) {
        if (!(Test-Path -LiteralPath (Join-Path $script:Repository $name))) { continue }
        $lines += $name + ':' + (Get-FileHash -LiteralPath (Join-Path $script:Repository $name)).Hash
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))))).Replace('-','') }
    finally { $sha.Dispose() }
}
function New-Es2PreparedProxy {
    param([Parameter(Mandatory)][string]$Directory)
    if (Test-Path -LiteralPath $Directory) { throw 'Prepared build directory must be fresh.' }
    $stamp=Get-Es2SourceStamp
    Invoke-Es2FixtureTests
    $dll = Copy-Es2BazelOutput '//tools/reference-game/proxy:ddraw.dll' $Directory
    $snapshot = Copy-Es2BazelOutput '//tools/reference-game/helpers:hang_snapshot' $Directory
    $volume = Copy-Es2BazelOutput '//tools/reference-game/helpers:session_volume' $Directory
    if ($stamp -ne (Get-Es2SourceStamp)) { throw 'Sources changed during the prepared build.' }
    $manifest = Join-Path $Directory 'prepared-proxy.json'
    [ordered]@{schema=1;repository=$script:Repository;source_stamp=$stamp;tests_passed=$true
        proxy=$dll;proxy_sha256=(Get-FileHash -LiteralPath $dll).Hash
        snapshot=$snapshot;snapshot_sha256=(Get-FileHash -LiteralPath $snapshot).Hash
        volume=$volume;volume_sha256=(Get-FileHash -LiteralPath $volume).Hash
        system_ddraw_sha256=(Get-FileHash -LiteralPath (Join-Path ([Environment]::GetFolderPath('Windows')) 'SysWOW64/ddraw.dll')).Hash} |
        ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
    return [IO.Path]::GetFullPath($manifest)
}
function Get-Es2PreparedProxy {
    param([Parameter(Mandatory)][string]$Manifest)
    $record = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    if ($record.schema -ne 1 -or !$record.tests_passed -or $record.repository -ne $script:Repository -or
        $record.source_stamp -ne (Get-Es2SourceStamp)) { throw 'Prepared proxy is stale or belongs to another source tree.' }
    if ($record.system_ddraw_sha256 -ne (Get-FileHash -LiteralPath (Join-Path ([Environment]::GetFolderPath('Windows')) 'SysWOW64/ddraw.dll')).Hash) { throw 'System DirectDraw changed after the prepared build.' }
    $artifacts = @(@($record.proxy,$record.proxy_sha256),@($record.snapshot,$record.snapshot_sha256))
    if ($record.volume) { $artifacts += ,@($record.volume,$record.volume_sha256) }
    foreach ($item in $artifacts) {
        if ((Get-FileHash -LiteralPath $item[0]).Hash -ne $item[1]) { throw 'Prepared artifact hash mismatch.' }
    }
    return $record
}
Export-ModuleMember -Function Resolve-Es2Bazel, Invoke-Es2Bazel, Get-Es2BazelOutput, Copy-Es2BazelOutput, Invoke-Es2FixtureTests, New-Es2PreparedProxy, Get-Es2PreparedProxy
