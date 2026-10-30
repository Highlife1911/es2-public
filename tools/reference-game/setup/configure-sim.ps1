<#
Deploys the own DBSIM process (es2sim, //sim:es2sim_window) into an
installation so that our launcher starts it instead of DBSIM.EXE, or removes
it again (docs/reference-game/setup.md, section "the own DBSIM process in an installation").

  -Apply   the directory ES2SIM\ with es2sim.exe (//sim:es2sim_window),
           SDL3.dll (//sim:sdl_runtime) and, with -Config, es2sim.ini (a copy
           of that es2sim configuration); es2launch.ini beside the original
           files with [sim] command (the es2sim command line es2launch puts
           in front of ` --arguments "<DBSIM argv>"`) and, with -Output,
           [sim] output; own-sim-manifest.json records the hashes. Every
           ES2LAUNCH.EXE started in the installation (start-own-launcher.ps1,
           start-native.ps1 with the own shell) then runs es2sim, which
           reads no DBSIM.EXE (the guest image is built in; the
           installation's DBSIM.EXE stays in place, unread, until
           retire-originals.ps1 removes it). A deployment of
           another build or another command is replaced.
  -Remove  ES2SIM\ (with anything in it), es2launch.ini and the manifest are
           deleted: the installation is as it was before -Apply.
  neither  a preview (JSON): the state found and what -Apply/-Remove would do.

The command line: "<ES2SIM>\es2sim.exe" [--headless] [--config "<ES2SIM>\es2sim.ini"]
[--tick-log "<TickLog>"] (es2sim reads no DBSIM.EXE).
GetPrivateProfileString drops a quote
pair around a whole value, and the command starts with a quote and may end
with one, so es2launch.ini holds it inside one more pair of quotes:
command = "<command line>".

No original file is touched. Refused: the primary installation native-r111
unless -AllowPrimary (only on the owner's word), linked
paths, a running game process of the installation (es2sim included), a clone
held by another session (CloneGuard.psm1), sources that are not 32-bit PE
images or are original files, paths that are not plain ASCII, and a state
whose files do not have the recorded hashes.
#>
[CmdletBinding()]
param(
    [string]$DataRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    # The files to deploy; default: built by Bazel and copied to build/reference-game first.
    [string]$Sim,
    [string]$Sdl,
    # An es2sim configuration (sim/src/main.c: [trace], [time], [input], [run], [game], [snapshot]) deployed as ES2SIM\es2sim.ini.
    [string]$Config = '',
    [switch]$Headless,
    # es2sim's tick log (--tick-log; es2sim creates it and refuses an existing one).
    [string]$TickLog = '',
    # es2launch.ini [sim] output: the file es2launch appends es2sim's stdout and stderr to.
    [string]$Output = '',
    [switch]$Apply,
    [switch]$Remove,
    [switch]$AllowPrimary
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
$ErrorActionPreference = 'Stop'
if ($Apply -and $Remove) { throw 'Use either -Apply or -Remove.' }
if (!$Apply -and ($Sim -or $Sdl -or $Config -or $Headless -or $TickLog -or $Output)) { throw 'The deployment options belong to -Apply.' }
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
if ($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
if ($InstallName -ieq 'native-r111' -and !$AllowPrimary) { throw 'native-r111 is the primary installation; deploy there only on the owner''s word (-AllowPrimary).' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$manifestFile = Join-Path $installation 'installation-manifest.json'
if (!(Test-Path -LiteralPath $manifestFile -PathType Leaf)) { throw "Not a prepared installation (no installation-manifest.json): $installation" }
Assert-NoLinks $installation
$installationManifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
$originalHashes = @($installationManifest.hashes | ForEach-Object { [string]$_.sha256 })
$originalNames = @($installationManifest.hashes | ForEach-Object { [string]$_.file })

$simDirectoryName = 'ES2SIM'
$simDirectory = Join-Path $installation $simDirectoryName
$launchIni = Join-Path $installation 'es2launch.ini'
$manifestPath = Join-Path $installation 'own-sim-manifest.json'
# Relative paths of the deployment (keys of the manifest's files).
$files = [ordered]@{sim = "$simDirectoryName\es2sim.exe"; sdl = "$simDirectoryName\SDL3.dll"; config = "$simDirectoryName\es2sim.ini"; launch_ini = 'es2launch.ini'}
foreach ($relative in $files.Values) {
    if ($originalNames | Where-Object { $_ -ieq $relative -or $_ -ieq (Split-Path -Leaf $relative) }) { throw "The deployment would use an original file name: $relative" }
}
function Get-Hash([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }
function Test-Pe32([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x100 -or $b[0] -ne 0x4d -or $b[1] -ne 0x5a) { return $false }
    $pe = [BitConverter]::ToInt32($b, 0x3c)
    return $pe -ge 0x40 -and $pe + 24 -le $b.Length -and [BitConverter]::ToUInt32($b, $pe) -eq 0x4550 -and [BitConverter]::ToUInt16($b, $pe + 4) -eq 0x14c
}
function Assert-Ascii([string]$Text, [string]$What) {
    if ($Text -match '[^\x20-\x7e]' -or $Text.Contains('"')) { throw "$What must be plain ASCII without quotes (es2launch reads es2launch.ini with GetPrivateProfileStringA): $Text" }
}

# The state found: none, own (every recorded file with its hash) or anything else.
$manifest = if (Test-Path -LiteralPath $manifestPath) { Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json } else { $null }
$present = @($files.Keys | Where-Object { Test-Path -LiteralPath (Join-Path $installation $files[$_]) -PathType Leaf })
$recorded = if ($manifest) { @($manifest.files.PSObject.Properties.Name) } else { @() }
$state = if (!$manifest -and !(Test-Path -LiteralPath $simDirectory) -and !$present.Count) { 'none' }
         elseif ($manifest -and $recorded.Count -and !(Compare-Object @($recorded | Sort-Object) @($present | Sort-Object)) -and
                 !(@($recorded | Where-Object { (Get-Hash (Join-Path $installation $files[$_])) -ne $manifest.files.$_ }).Count)) { 'own' }
         else { 'unknown' }
if ($state -eq 'unknown' -and !$Remove) {
    throw "Unexpected own-sim files in $installation (present: $($present -join ', '); manifest $([bool]$manifest)); nothing changed. -Remove clears them."
}

$plan = [ordered]@{installation = $installation; state = $state}
if ($state -eq 'own') { $plan.files = $manifest.files; $plan.command = $manifest.command }
if (!$Apply -and !$Remove) {
    $plan.preview = $true
    $plan.apply = "copy $simDirectoryName\{es2sim.exe, SDL3.dll[, es2sim.ini]}, write es2launch.ini ([sim] command) and own-sim-manifest.json"
    $plan.remove = "delete $simDirectoryName\, es2launch.ini and own-sim-manifest.json"
    $plan | ConvertTo-Json -Depth 4
    return
}

# Nothing of the installation may run, and no other session may hold it.
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','ES2LAUNCH.EXE','es2sim.exe') -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -in @($installation, $simDirectory)
})
if ($running.Count) { throw 'Close EarthSiege II in this installation before changing its simulator.' }

if ($Remove) {
    if ($state -eq 'none') { $plan.removed = $false; $plan | ConvertTo-Json -Depth 4; return }
    # Without DBSIM.EXE the installation needs es2sim (retire-originals.ps1 -Restore first).
    if (Test-Path -LiteralPath (Join-Path $installation 'retired-originals.json')) { throw 'The installation''s DBSIM.EXE is retired; run retire-originals.ps1 -Restore before removing the own simulator.' }
    if (Test-Path -LiteralPath $simDirectory) { Remove-Item -LiteralPath $simDirectory -Recurse -Force }
    foreach ($path in @($launchIni, $manifestPath)) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
    if ((Test-Path -LiteralPath $simDirectory) -or (Test-Path -LiteralPath $launchIni) -or (Test-Path -LiteralPath $manifestPath)) { throw 'The own simulator could not be removed completely.' }
    $plan.state = 'none'
    $plan.removed = $true
    $plan | ConvertTo-Json -Depth 4
    return
}

# -Apply (es2sim reads no DBSIM.EXE, so its hash is not checked here).
$sources = [ordered]@{sim = $Sim; sdl = $Sdl}
if (!$Sim -or !$Sdl) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $destination = Join-Path $repository 'build/reference-game/sim'
    if (!$Sim) { $sources.sim = Copy-Es2BazelOutput -Target '//sim:es2sim_window' -Destination $destination }
    if (!$Sdl) { $sources.sdl = Copy-Es2BazelOutput -Target '//sim:sdl_runtime' -Destination $destination }
}
if ($Config) {
    $sources.config = [IO.Path]::GetFullPath($Config)
    if (!(Test-Path -LiteralPath $sources.config -PathType Leaf)) { throw "No es2sim configuration: $($sources.config)" }
}
$hashes = [ordered]@{}
foreach ($key in @($sources.Keys)) {
    $sources[$key] = [IO.Path]::GetFullPath($sources[$key])
    if ($key -ne 'config' -and !(Test-Pe32 $sources[$key])) { throw "Not a 32-bit PE image: $($sources[$key])" }
    $hashes[$key] = Get-Hash $sources[$key]
    if ($hashes[$key] -in $originalHashes) { throw "The source is an original file of the installation: $($sources[$key])" }
}
# es2launch.ini: the command (es2launch appends the DBSIM argv as --arguments).
$command = "`"$(Join-Path $simDirectory 'es2sim.exe')`""
if ($Headless) { $command += ' --headless' }
if ($Config) { $command += " --config `"$(Join-Path $simDirectory 'es2sim.ini')`"" }
if ($TickLog) { $TickLog = [IO.Path]::GetFullPath($TickLog); Assert-Ascii $TickLog 'The tick log path'; $command += " --tick-log `"$TickLog`"" }
Assert-Ascii $installation 'The installation path'
# One more quote pair: GetPrivateProfileString strips it, the command's own quotes stay.
$iniLines = @('; written by configure-sim.ps1; configure-sim.ps1 -Remove deletes it.', '[sim]', "command = `"$command`"")
if ($Output) { $Output = [IO.Path]::GetFullPath($Output); Assert-Ascii $Output 'The output path'; $iniLines += "output = $Output" }
$iniText = ($iniLines -join "`r`n") + "`r`n"
$iniHash = ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::ASCII.GetBytes($iniText)))).Replace('-', '')
$want = [ordered]@{}
foreach ($key in $hashes.Keys) { $want[$key] = $hashes[$key] }
$want.launch_ini = $iniHash
if ($state -eq 'own' -and !(Compare-Object @($recorded | Sort-Object) @($want.Keys | Sort-Object)) -and
    !(@($want.Keys | Where-Object { $manifest.files.$_ -ne $want[$_] }).Count)) {
    $plan.applied = $false
    $plan | ConvertTo-Json -Depth 4
    return
}

# Staged beside its place, checked, then swapped in.
$staged = Join-Path $installation "$simDirectoryName.staged"
$stagedIni = "$launchIni.staged"
foreach ($path in @($staged, $stagedIni)) { if (Test-Path -LiteralPath $path) { throw "A staged simulator from an interrupted run is in the way: $path" } }
New-Item -ItemType Directory -Path $staged | Out-Null
$moved = $false
try {
    foreach ($key in $sources.Keys) {
        $target = Join-Path $staged (Split-Path -Leaf $files[$key])
        Copy-Item -LiteralPath $sources[$key] -Destination $target
        [IO.File]::SetAttributes($target, [IO.FileAttributes]::Normal)   # Bazel outputs are read-only
        if ((Get-Hash $target) -ne $hashes[$key]) { throw "The staged $($files[$key]) does not match its source." }
    }
    [IO.File]::WriteAllText($stagedIni, $iniText, [Text.Encoding]::ASCII)
    if ((Get-Hash $stagedIni) -ne $iniHash) { throw 'The staged es2launch.ini is not the text written.' }
    if (Test-Path -LiteralPath $simDirectory) { Remove-Item -LiteralPath $simDirectory -Recurse -Force }
    if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath -Force }
    $moved = $true
    Move-Item -LiteralPath $staged -Destination $simDirectory
    Move-Item -LiteralPath $stagedIni -Destination $launchIni -Force
    foreach ($key in $want.Keys) {
        if ((Get-Hash (Join-Path $installation $files[$key])) -ne $want[$key]) { throw "The deployed $($files[$key]) does not match its source." }
    }
    $record = [ordered]@{
        schema = 1; kind = 'es2-own-sim'; utc = [DateTime]::UtcNow.ToString('o'); installation = $installation
        files = $want; sources = $sources; sim_directory = $simDirectoryName; command = $command
        output = $(if ($Output) { $Output } else { $null })
        dbsim = 'DBSIM.EXE stays in place; es2sim does not read it'
    }
    [IO.File]::WriteAllText($manifestPath, ($record | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
} catch {
    # Nothing half-deployed stays: the staged files go, and once the old deployment was taken away, the new one too.
    foreach ($path in @($staged, $stagedIni)) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force } }
    if ($moved) {
        if (Test-Path -LiteralPath $simDirectory) { Remove-Item -LiteralPath $simDirectory -Recurse -Force }
        foreach ($path in @($launchIni, $manifestPath)) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
    }
    throw
}
$plan.state = 'own'
$plan.files = $want
$plan.command = $command
$plan.applied = $true
$plan | ConvertTo-Json -Depth 4
