# Bazel entry points for the reference-game PowerShell scripts. Bazel is the
# only build system: scripts never call the compiler directly.
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
    <# Runs the proxy's fixture tests. Cached results are reused when nothing
       changed. No original game is launched. #>
    Invoke-Es2Bazel test //tools/reference-game/proxy/... --test_summary=terse | Out-Host
}

Export-ModuleMember -Function Resolve-Es2Bazel, Invoke-Es2Bazel, Get-Es2BazelOutput, Copy-Es2BazelOutput, Invoke-Es2FixtureTests
