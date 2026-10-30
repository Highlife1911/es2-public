# Delete the Bazel output bases of finished workers.
#
#   & tools\clean-worker-bazel.ps1 tools-comments,shell-comments [-DryRun]
#
# For each worker name P it removes <OutputRoot>\P (the Bazel output root of local.bazelrc)
# and the variants P-contents, P-mut, P-mut-contents, P-mutation and
# P-mutation-contents that exist. It
# refuses the shared directories (cache, install), names that are not worker
# names (the main tree's hashed base has no hyphen), and workers whose
# worktree <WorktreeParent>\es2-P still exists. A running Bazel server of the base is
# shut down first. Bazel's execroot contains junctions into the source tree,
# so every reparse point is unlinked (never followed) before anything else is
# deleted, and the deletion stops if one cannot be unlinked.
#
# -OutputRoot, -Repo and -WorktreeParent exist for the test
# (tools/test_worker.py); their defaults come from Get-Es2Layout.
param(
    [Parameter(Mandatory = $true, Position = 0)][string[]]$Package,
    [switch]$DryRun,
    [string]$OutputRoot = '',
    [string]$Repo = '',
    [string]$WorktreeParent = ''
)
$ErrorActionPreference = 'Stop'
if (!$OutputRoot -or !$Repo -or !$WorktreeParent) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$OutputRoot) { $OutputRoot = $es2.BazelOutputRoot }
    if (!$Repo) { $Repo = $es2.Repository }
    if (!$WorktreeParent) { $WorktreeParent = Split-Path -Parent $es2.Repository }
}
$root = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$repo = $Repo
$reserved = @('cache', 'install')
$suffixes = @('', '-contents', '-mut', '-mut-contents', '-mutation', '-mutation-contents')

function Assert-PackageName([string]$name) {
    if ($name -cnotmatch '^[a-z][a-z0-9]*(-[a-z0-9]+)*$') { throw "Not a package name: '$name'" }
    if ($name -notmatch '-' -and $name -cnotmatch '^wp[0-9]+$') { throw "Refused '$name': worker packages are named like code-ids, doc-web or wp4" }
    if ($name -in $reserved) { throw "Refused '$name': shared Bazel directory" }
    $worktree = Join-Path $WorktreeParent "es2-$name"
    if (Test-Path -LiteralPath $worktree) { throw "Refused '$name': worktree $worktree still exists" }
}

function Stop-BazelServer([string]$base) {
    if (!(Test-Path -LiteralPath (Join-Path $base 'server\server.pid.txt'))) { return }
    Write-Host "shutdown  $base"
    if ($DryRun) { return }
    Push-Location $repo
    try { & bazel "--output_base=$base" shutdown 2>&1 | Out-Null } finally { Pop-Location }
}

function Remove-OutputBase([string]$dir) {
    $full = [IO.Path]::GetFullPath($dir)
    if ([IO.Path]::GetDirectoryName($full) -ne $root) { throw "Refused '$full': not directly under $root" }
    if ((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refused '$full': is itself a link" }
    # Unlink every junction/symlink first (deepest first); Get-ChildItem does not follow them.
    $links = @(Get-ChildItem -LiteralPath $full -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName.Length } -Descending)
    Write-Host ("delete    {0} ({1} links)" -f $full, $links.Count)
    if ($DryRun) { return }
    foreach ($l in $links) {
        if ($l.PSIsContainer) { [IO.Directory]::Delete($l.FullName, $false) } else { [IO.File]::Delete($l.FullName) }
    }
    $left = @(Get-ChildItem -LiteralPath $full -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
    if ($left.Count) { throw "Stopped: $($left.Count) links left in $full (first: $($left[0].FullName))" }
    Remove-Item -LiteralPath $full -Recurse -Force
}

foreach ($p in $Package) {
    Assert-PackageName $p
    $dirs = @($suffixes | ForEach-Object { Join-Path $root ($p + $_) } | Where-Object { Test-Path -LiteralPath $_ })
    if (!$dirs.Count) { Write-Host "nothing   $p"; continue }
    foreach ($d in $dirs) { Stop-BazelServer $d }
    foreach ($d in $dirs) { Remove-OutputBase $d }
}
