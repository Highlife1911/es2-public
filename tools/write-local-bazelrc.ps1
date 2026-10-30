# Writes local.bazelrc, the machine's paths that .bazelrc imports (git-ignored):
#
#   & tools\write-local-bazelrc.ps1                # this checkout, the layout of Get-Es2Layout
#   & tools\write-local-bazelrc.ps1 -NoData        # a machine without the extracted CD and es2-data
#
# Written: Bazel's output root, repository cache and disk cache (default the main checkout's build\bazel,
# shared by every worktree), the dependency directory as ES2_DEPENDENCIES, PowerShell 7 as ES2_PWSH (Bazel's
# repository rules do not find the Store's pwsh.exe alias on PATH), the compiler PATH of the pinned
# LLVM-MinGW, and ES2_DATA_ROOT / ES2_DATA_REPO for the real-data tests (unless -NoData). An existing file is
# replaced. tools\worker.ps1 copies the main checkout's file into a new worktree.
[CmdletBinding()]
param(
    # The checkout that receives local.bazelrc (default: the one this script is in).
    [string]$Repository = '',
    [string]$Dependencies = '',
    [string]$OutputRoot = '',
    # Default: ES2_PWSH, else pwsh.exe on PATH.
    [string]$Pwsh = '',
    # The extracted CD (default <main checkout>\build\extracted\cd) and the es2-data checkout.
    [string]$DataRoot = '',
    [string]$DataRepository = '',
    [switch]$NoData
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
$layout = Get-Es2Layout
if (!$Repository) { $Repository = Split-Path -Parent $PSScriptRoot }
if (!$Dependencies) { $Dependencies = $layout.Dependencies }
if (!$OutputRoot) { $OutputRoot = $layout.BazelOutputRoot }
if (!$DataRoot) { $DataRoot = Join-Path $layout.Extracted 'cd' }
if (!$DataRepository) { $DataRepository = $layout.DataRepository }
if (!$Pwsh) { $Pwsh = if ($env:ES2_PWSH) { $env:ES2_PWSH } else { Get-Command pwsh.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1 -ExpandProperty Source } }
function ConvertTo-BazelPath([string]$Path) { return ([IO.Path]::GetFullPath($Path).TrimEnd([char]'\', [char]'/') -replace '\\', '/') }
$deps = ConvertTo-BazelPath $Dependencies
$out = ConvertTo-BazelPath $OutputRoot
$lines = @(
    '# Written by tools/write-local-bazelrc.ps1: the paths of this machine.',
    "startup `"--output_user_root=$out`"",
    "common `"--repository_cache=$out/cache/repos`"",
    "common `"--disk_cache=$out/cache/disk`"",
    "common `"--repo_env=ES2_DEPENDENCIES=$deps`"",
    "common `"--repo_env=ES2_PWSH=$(ConvertTo-BazelPath $Pwsh)`"",
    "build `"--action_env=PATH=$deps/llvm-mingw/bin;C:/Windows/System32`""
)
if (!$NoData) {
    $cd = ConvertTo-BazelPath $DataRoot
    $repo = ConvertTo-BazelPath $DataRepository
    $lines += @(
        "test `"--test_env=ES2_DATA_ROOT=$cd`"",
        "common `"--repo_env=ES2_DATA_ROOT=$cd`"",
        "test `"--test_env=ES2_DATA_REPO=$repo`"",
        "common `"--repo_env=ES2_DATA_REPO=$repo`""
    )
}
$target = Join-Path $Repository 'local.bazelrc'
[IO.File]::WriteAllText($target, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
Write-Host "Wrote $target"
