# Worktree helper for the parallel-worker protocol (docs/reference-game/setup.md, "Worker helper").
#
#   & tools\worker.ps1 tools-comments           # create worktree + branch, print the Bazel commands
#   & tools\worker.ps1 tools-comments -Finish   # shut the worker's Bazel servers down, list the next steps
#   & tools\worker.ps1 tools-comments -Finish -Archive
#                                                            # ... and copy build\claude\tools-comments to es2-data\logs\tools-comments-<date>
#
# Create: `git worktree add <WorktreeParent>\es2-<pkg> -b claude/<pkg> main` (default: beside the main
# checkout) and a copy of the main checkout's local.bazelrc. Refuses a
# name that is not a worker name (same rule as clean-worker-bazel.ps1), an
# existing worktree directory, an existing branch claude/<pkg>, a missing base.
# Creates the ignored scratch directory build\claude\<pkg> and prints the exact
# Bazel commands (own output base and repo cache, cpu=4) and the shutdown
# command. It starts no Bazel and builds nothing.
#
# -Finish: shuts down the Bazel servers of the output bases <pkg>, <pkg>-mut
# and <pkg>-mutation (only those with a server pid file) and LISTS what the
# coordinator does next; it removes nothing. With -Archive it also performs the
# evidence copy of the worktree's build\claude\<pkg> to
# <EvidenceRoot>\<pkg>-<yyyyMMdd>, leaving out mutation-workspace-* directories,
# *.exe, *.dll, *.lst, *.asm, *.elf and *.pkl (copies of or derived from the
# original's bytes (a worker's DBSIM.EXE copy, harness builds), or
# regenerable binaries), and any reparse point (never followed). It refuses if the
# target (or its .partial staging directory) exists, copies into the staging
# directory first and renames it, and writes nothing else into the evidence
# root.
#
# The path parameters exist for the test (tools/test_worker.py); their defaults
# come from Get-Es2Layout (tools/reference-game/setup/CloneGuard.psm1).
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Package,
    [switch]$Finish,
    [switch]$Archive,
    [string]$Repo = '',
    [string]$WorktreeParent = '',
    [string]$OutputRoot = '',
    [string]$EvidenceRoot = '',
    [string]$Base = 'main',
    [string]$Date = (Get-Date -Format 'yyyyMMdd')
)
$ErrorActionPreference = 'Stop'
if (!$Repo -or !$WorktreeParent -or !$OutputRoot -or !$EvidenceRoot) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$Repo) { $Repo = $es2.Repository }
    if (!$WorktreeParent) { $WorktreeParent = Split-Path -Parent $es2.Repository }
    if (!$OutputRoot) { $OutputRoot = $es2.BazelOutputRoot }
    if (!$EvidenceRoot) { $EvidenceRoot = Join-Path $es2.DataRepository 'logs' }
}
$reserved = @('cache', 'install')
$serverSuffixes = @('', '-mut', '-mutation')
$excludedExtensions = @('.exe', '.dll', '.lst', '.asm', '.elf', '.pkl')

if ($Package -cnotmatch '^[a-z][a-z0-9]*(-[a-z0-9]+)*$') { throw "Not a package name: '$Package'" }
if ($Package -notmatch '-' -and $Package -cnotmatch '^wp[0-9]+$') { throw "Refused '$Package': worker packages are named like h4-79, t-05 or wp4" }
if ($Package -in $reserved) { throw "Refused '$Package': shared Bazel directory" }
if ($Archive -and !$Finish) { throw '-Archive is a part of -Finish: use -Finish -Archive' }
if ($Date -notmatch '^[0-9]{8}$') { throw "Not a date (yyyyMMdd): '$Date'" }

$worktree = Join-Path $WorktreeParent "es2-$Package"
$branch = "claude/$Package"
$outputRootFull = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$baseDir = Join-Path $outputRootFull $Package
$scratch = Join-Path $worktree "build\claude\$Package"

function Invoke-Git([string[]]$GitArgs) {
    # git reports progress on stderr; with 'Stop' Windows PowerShell 5.1 would treat it as an error.
    $ErrorActionPreference = 'Continue'
    $out = & git -C $Repo @GitArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw ("git {0} failed ({1}): {2}" -f ($GitArgs -join ' '), $LASTEXITCODE, ($out -join ' ')) }
    return $out
}

function Test-GitRef([string]$ref) {
    $ErrorActionPreference = 'Continue'
    & git -C $Repo show-ref --verify --quiet $ref 2>&1 | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function New-Worker {
    if (Test-Path -LiteralPath $worktree) { throw "Refused: $worktree already exists" }
    if (Test-GitRef "refs/heads/$branch") { throw "Refused: branch $branch already exists" }
    if (!(Test-GitRef "refs/heads/$Base")) { throw "Refused: base branch '$Base' does not exist in $Repo" }
    Invoke-Git @('worktree', 'add', $worktree, '-b', $branch, $Base) | Out-Null
    $localRc = Join-Path $Repo 'local.bazelrc'
    if (Test-Path -LiteralPath $localRc) { Copy-Item -LiteralPath $localRc -Destination (Join-Path $worktree 'local.bazelrc') }
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    Write-Host "created   $worktree on branch $branch (from $Base)"
    Write-Host "scratch   $scratch (ignored by git; evidence and report.md go here)"
    Write-Host ''
    Write-Host 'Bazel (run from PowerShell, not Git Bash; one worktree, own output base and repo cache):'
    Write-Host "  Set-Location $worktree"
    Write-Host "  bazel --output_base=$baseDir test --repo_contents_cache=$baseDir-contents --local_resources=cpu=4 <targets>"
    Write-Host 'At the very end, always:'
    Write-Host "  bazel --output_base=$baseDir shutdown"
    Write-Host "(or: $Repo\tools\worker.ps1 $Package -Finish)"
}

function Stop-PackageServers {
    foreach ($s in $serverSuffixes) {
        $b = Join-Path $outputRootFull ($Package + $s)
        if (!(Test-Path -LiteralPath (Join-Path $b 'server\server.pid.txt'))) { Write-Host "no server $b"; continue }
        Write-Host "shutdown  $b"
        $dir = if (Test-Path -LiteralPath $worktree) { $worktree } else { $Repo }
        Push-Location $dir
        try { & bazel "--output_base=$b" shutdown 2>&1 | Out-Null } finally { Pop-Location }
    }
}

function Copy-Evidence([string]$src, [string]$dst, [hashtable]$stats) {
    New-Item -ItemType Directory -Path $dst | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $src -Force)) {
        $target = Join-Path $dst $item.Name
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            $stats.links++; Write-Host "skip link $($item.FullName)"; continue
        }
        if ($item.PSIsContainer) {
            if ($item.Name -like 'mutation-workspace-*') { $stats.excluded++; continue }
            Copy-Evidence $item.FullName $target $stats
        } else {
            if ($item.Extension.ToLowerInvariant() -in $excludedExtensions) { $stats.excluded++; continue }
            Copy-Item -LiteralPath $item.FullName -Destination $target
            $stats.files++; $stats.bytes += $item.Length
        }
    }
}

function Copy-EvidenceToArchive {
    if (!(Test-Path -LiteralPath $EvidenceRoot -PathType Container)) { throw "Refused: evidence root $EvidenceRoot does not exist (not created)" }
    if (!(Test-Path -LiteralPath $scratch -PathType Container)) { throw "Refused: nothing to archive, $scratch does not exist" }
    $evidenceFull = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
    $target = [IO.Path]::GetFullPath((Join-Path $evidenceFull "$Package-$Date"))
    if ([IO.Path]::GetDirectoryName($target) -ne $evidenceFull) { throw "Refused '$target': not directly under $evidenceFull" }
    $staging = "$target.partial"
    if (Test-Path -LiteralPath $target) { throw "Refused: $target already exists" }
    if (Test-Path -LiteralPath $staging) { throw "Refused: staging directory $staging exists (an earlier copy was interrupted; the owner removes it)" }
    $stats = @{ files = 0; bytes = 0L; excluded = 0; links = 0 }
    Copy-Evidence $scratch $staging $stats
    Move-Item -LiteralPath $staging -Destination $target
    Write-Host ("archived  {0} -> {1} ({2} files, {3} bytes; {4} excluded, {5} links skipped)" -f $scratch, $target, $stats.files, $stats.bytes, $stats.excluded, $stats.links)
}

function Show-NextSteps([bool]$archived) {
    $dest = Join-Path $EvidenceRoot "$Package-$Date"
    Write-Host ''
    Write-Host 'Next, done by the coordinator (this script does none of it):'
    if ($archived) {
        Write-Host "  1. (done) evidence copied to $dest"
    } else {
        Write-Host "  1. copy $scratch to $dest"
        Write-Host '     leaving out mutation-workspace-*, *.exe, *.dll, *.lst, *.asm, *.elf, *.pkl (or rerun with -Finish -Archive)'
    }
    Write-Host "  2. git -C $Repo worktree remove $worktree   (deletes the ignored files in it)"
    Write-Host "  3. git -C $Repo branch -d $branch           (after the merge)"
    Write-Host "  4. $Repo\tools\clean-worker-bazel.ps1 $Package   (also removes $Package-mut and $Package-mutation variants; -DryRun lists)"
}

if (!$Finish) {
    New-Worker
} else {
    Stop-PackageServers
    $archived = $false
    if ($Archive) { Copy-EvidenceToArchive; $archived = $true }
    Show-NextSteps $archived
}
