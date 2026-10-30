<#
.SYNOPSIS
  Regenerates everything that is derived from the hook registry, the DBSIM map and the
  verdict file: docs/functions/, sim_gap.md, hook_frontier.md and its bridged-call
  tables.

.DESCRIPTION
  One command for the generators, in the order that matters:

    1. hook_candidates.py --check-verdicts   the Verdicts table of function_verdicts.md must hold
                                           every non-hook function that a subsystem
                                           section classifies (stops here when not)
    2. areas.py --check-document           the module map matches dbsim_gap.md section 1
    3. sim_gap.py                          docs/reference-game/sim_gap.md, the own process's
                                           gap list; also writes the JSON that gives
                                           the function index its Sim column
    4. function_index.py                   docs/functions/ (with the area column, the Sim
                                           column and the area report of the index page)
    5. hook_candidates.py --frontier         docs/reference-game/hook_frontier.md (the hand-written
                                           tail from "## Bridged-call inventory" is kept)
    6. hook_candidates.py --bridge-table     the bridged-call tables between the markers of
                                           hook_frontier.md

  Run it from the repository root (or the worktree) after a hook or a verdict row
  was merged, or when a new map or regression is available. The disassembly listing that
  function_index.py reads is written to an ignored scratch directory and deleted again;
  nothing derived from the CD is left behind. Only pinned tools are
  used. The generated files hold addresses, counts, names and our own descriptions only.

.PARAMETER MapJson
  map.json of a DBSIM map run (default: the newest dbsim-map-*\map.json under LogRoot).
.PARAMETER Counters
  hook-counters.json of a regression (default: the newest check-*\hook-counters.json under
  LogRoot); used by the function index and the bridged-call tables.
.PARAMETER DataRoot
  The extracted CD (default: ES2_DATA_ROOT, else <main checkout>\build\extracted\cd).
.PARAMETER LogRoot
  Where the map and regression directories are (default <es2-data>\logs).
.PARAMETER Scratch
  Ignored scratch directory for the listing (default <repo>\build\function-docs).
.PARAMETER CheckOnly
  Only run the checks (steps 1 and 2, and sim_gap.py --check: the committed sim_gap.md is
  current for the rows, hooks and sim/src sources of this tree; it needs DBSIM.EXE and the
  map, about 20 s); write nothing. Run it after each merge.
#>
[CmdletBinding()]
param(
    [string]$MapJson = '',
    [string]$Counters = '',
    [string]$DataRoot = '',
    [string]$LogRoot = '',
    [string]$Scratch = '',
    [string]$Python = '',
    [string]$Objdump = '',
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Set-Location $repo
Import-Module (Join-Path $PSScriptRoot '..\setup\CloneGuard.psm1')
$es2 = Get-Es2Layout
if (-not $DataRoot) {
    $DataRoot = if ($env:ES2_DATA_ROOT) { $env:ES2_DATA_ROOT } else { Join-Path $es2.Extracted 'cd' }
}
if (-not $LogRoot) { $LogRoot = Join-Path $es2.DataRepository 'logs' }
if (-not $Python) { $Python = $es2.Python }
if (-not $Objdump) { $Objdump = $es2.Objdump }
if (-not $Scratch) { $Scratch = Join-Path $repo 'build\function-docs' }
$exe = Join-Path $DataRoot 'VER95\DBSIM.EXE'
$scripts = 'tools\reference-game\scripts'
$registry = 'tools/reference-game/proxy/hook_registry.h'
$classified = 'docs/reference-game/function_verdicts.md'

function Invoke-Step([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    & $body
    if ($LASTEXITCODE -ne 0) { throw "$name failed (exit code $LASTEXITCODE)" }
}

Invoke-Step 'verdict table (hook_candidates.py --check-verdicts)' {
    & $Python -B "$scripts\hook_candidates.py" --check-verdicts --registry $registry --classified $classified
}
Invoke-Step 'module map (areas.py --check-document)' {
    & $Python -B "$scripts\areas.py" --check-document docs/reference-game/dbsim_gap.md
}

if (-not $MapJson) {
    $dir = Get-ChildItem -LiteralPath $LogRoot -Directory -Filter 'dbsim-map-*' |
        Where-Object { Test-Path (Join-Path $_.FullName 'map.json') } |
        Sort-Object Name | Select-Object -Last 1
    if (-not $dir) { throw "no dbsim-map-*\map.json under $LogRoot (pass -MapJson)" }
    $MapJson = Join-Path $dir.FullName 'map.json'
}
$simGap = 'docs/reference-game/sim_gap.md'
$simArgs = @('--executable', $exe.Replace('\', '/'), '--objdump', $Objdump.Replace('\', '/'),
            '--map', $MapJson.Replace('\', '/'), '--output', $simGap)
if ($CheckOnly) {
    foreach ($p in $exe, $MapJson, $Python, $Objdump) {
        if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" }
    }
    Invoke-Step 'own process gap list (sim_gap.py --check)' {
        & $Python -B "$scripts\sim_gap.py" --check @simArgs
    }
    return
}

if (-not $Counters) {
    $file = Get-ChildItem -Path (Join-Path $LogRoot 'check-*\hook-counters.json') -File |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $file) { throw "no check-*\hook-counters.json under $LogRoot (pass -Counters)" }
    $Counters = $file.FullName
}
foreach ($p in $exe, $MapJson, $Counters, $Python, $Objdump) {
    if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" }
}
Write-Host "map:      $MapJson"
Write-Host "counters: $Counters"
# The generated headers quote these paths; forward slashes keep them as in the committed files.
$mapArg = $MapJson.Replace('\', '/')
$countersArg = $Counters.Replace('\', '/')
$exeArg = $exe.Replace('\', '/')

New-Item -ItemType Directory -Force $Scratch | Out-Null
$listing = Join-Path $Scratch 'dbsim.lst'
$simJson = Join-Path $Scratch 'sim_gap.json'
$simJsonArg = $simJson.Replace('\', '/')
try {
    # Out-File would add a BOM and CRLF; the listing parser reads UTF-8 lines either way.
    & $Objdump -d -M intel $exe | Out-File -Encoding utf8 -LiteralPath $listing
    if ($LASTEXITCODE -ne 0) { throw "llvm-objdump failed (exit code $LASTEXITCODE)" }

    Invoke-Step 'own process gap list (sim_gap.py)' {
        & $Python -B "$scripts\sim_gap.py" @simArgs --json $simJsonArg
    }
    Invoke-Step 'function index (function_index.py)' {
        & $Python -B "$scripts\function_index.py" --listing $listing --image $exeArg `
            --registry $registry --hooks-doc docs/reference-game/hooks.md `
            --classified $classified --docs docs/reverse-engineering --core core `
            --map $mapArg --counters $countersArg --sim $simJsonArg `
            --output docs/functions
    }
}
finally {
    Remove-Item -LiteralPath $listing, $simJson -Force -ErrorAction SilentlyContinue
}

$common = @('--map', $mapArg, '--registry', $registry, '--classified', $classified)
Invoke-Step 'frontier (hook_candidates.py --frontier)' {
    & $Python -B "$scripts\hook_candidates.py" @common --frontier --image $exeArg `
        --output docs/reference-game/hook_frontier.md
}
Invoke-Step 'bridged-call tables (hook_candidates.py --bridge-table)' {
    & $Python -B "$scripts\hook_candidates.py" @common --bridge-table $countersArg `
        --bridge-calls tools/reference-game/proxy --image $exeArg `
        --bridge-notes docs/reference-game/hook_frontier.md --output docs/reference-game/hook_frontier.md
}
Write-Host 'done: review the diff of docs/functions and hook_frontier.md'
