# Where the game installations of a data repository live.
$ErrorActionPreference = 'Stop'
function Get-Es2Layout {
    # The machine layout: the main checkout (also when called from a worktree, through git's common
    # directory), its build\ directory, and es2-data and es2-dependencies beside it. ES2_DATA_REPO and
    # ES2_DEPENDENCIES override the two siblings.
    $repository = $null
    try {
        $common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir 2>$null
        if ($LASTEXITCODE -eq 0 -and $common) { $repository = Split-Path -Parent ([IO.Path]::GetFullPath(([string]$common).Trim())) }
    } catch { $repository = $null }
    if (!$repository) { $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')) }
    $repository = $repository.TrimEnd([char]'\', [char]'/')
    $parent = Split-Path -Parent $repository
    $data = if ($env:ES2_DATA_REPO) { [IO.Path]::GetFullPath($env:ES2_DATA_REPO).TrimEnd([char]'\', [char]'/') } else { Join-Path $parent 'es2-data' }
    $dependencies = if ($env:ES2_DEPENDENCIES) { [IO.Path]::GetFullPath($env:ES2_DEPENDENCIES).TrimEnd([char]'\', [char]'/') } else { Join-Path $parent 'es2-dependencies' }
    $build = Join-Path $repository 'build'
    [pscustomobject]@{
        Repository = $repository; Build = $build; Extracted = (Join-Path $build 'extracted'); Installs = (Join-Path $build 'installs')
        BazelOutputRoot = (Join-Path $build 'bazel'); DataRepository = $data; Dependencies = $dependencies
        Python = (Join-Path $dependencies 'python\python.exe')
    }
}
# The installations of the data repository lie in the es2 checkout's
# build\installs; any other DataRoot (a test's synthetic root) keeps
# them in <DataRoot>\installs.
$script:Layout = Get-Es2Layout
$script:DataRepository = $script:Layout.DataRepository
$script:BuildInstallRoot = $script:Layout.Installs

function Get-CloneDirectory([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd([char]'\', [char]'/')
}
function Get-CloneInstallRoot([string]$DataRoot) {
    # The directory of the installations that belong to DataRoot, as a full
    # path without a trailing separator.
    $root = Get-CloneDirectory $DataRoot
    if ($root -ieq $script:DataRepository) { return $script:BuildInstallRoot }
    return Join-Path $root 'installs'
}
Export-ModuleMember -Function Get-CloneInstallRoot, Get-Es2Layout
