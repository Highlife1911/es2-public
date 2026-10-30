<#
Deploys the own launcher and the own graphical shell into a portable
installation, or removes them again (docs/reference-game/setup.md, section
"Own shell as the default").

  -Apply   ES2LAUNCH.EXE (//launcher:es2launch) beside the original files and
           the directory ES2SHELL\ with es2shell.exe (//shell:es2shell), SDL3.dll
           (//shell:gui_sdl_runtime) and menu.ini (game_dir, cd_root from
           DATA\DRIVE.CFG); own-shell-manifest.json records the hashes.
           start-native.ps1 then starts ES2LAUNCH.EXE with ES2_LAUNCH_SHELL set
           to es2shell.exe, so VSHELL.EXE no longer runs (it stays untouched;
           retire-originals.ps1 removes it).
           A deployment of another build is replaced.
  -Remove  the three files, menu.ini, ES2SHELL\ and the manifest are deleted
           (shell.log and anything else in ES2SHELL\ as well).
  neither  a preview (JSON): the state found and what -Apply/-Remove would do.

No original file is touched. Refused: the primary installation native-r111
unless -AllowPrimary (only on the owner's word), linked
paths, a running game process of the installation, sources that are not
32-bit PE images, and a state whose files do not have the recorded hashes.
#>
[CmdletBinding()]
param(
    [string]$DataRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    # The files to deploy; default: built by Bazel and copied to build/reference-game first.
    [string]$Launcher,
    [string]$Shell,
    [string]$Sdl,
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
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
if ($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
if ($InstallName -ieq 'native-r111' -and !$AllowPrimary) { throw 'native-r111 is the primary installation; deploy there only on the owner''s word (-AllowPrimary, O-26).' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
if (!(Test-Path -LiteralPath (Join-Path $installation 'installation-manifest.json') -PathType Leaf)) { throw "Not a prepared installation (no installation-manifest.json): $installation" }
Assert-NoLinks $installation

$shellDirectoryName = 'ES2SHELL'
$files = [ordered]@{
    launcher = 'ES2LAUNCH.EXE'
    shell = "$shellDirectoryName\es2shell.exe"
    sdl = "$shellDirectoryName\SDL3.dll"
}
$shellDirectory = Join-Path $installation $shellDirectoryName
$manifestPath = Join-Path $installation 'own-shell-manifest.json'
$iniPath = Join-Path $shellDirectory 'menu.ini'
function Get-Hash([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }
function Test-Pe32([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x100 -or $b[0] -ne 0x4d -or $b[1] -ne 0x5a) { return $false }
    $pe = [BitConverter]::ToInt32($b, 0x3c)
    return $pe -ge 0x40 -and $pe + 24 -le $b.Length -and [BitConverter]::ToUInt32($b, $pe) -eq 0x4550 -and [BitConverter]::ToUInt16($b, $pe + 4) -eq 0x14c
}
# menu.ini of the installation: the shell's game directory and the CD root of
# DATA\DRIVE.CFG (the first line; start-native.ps1 rewrites both when the CD
# letter changes). No evidence log, no fixed seed: VSHELL's behaviour.
function Get-MenuIni {
    $drive = Join-Path $installation 'DATA\DRIVE.CFG'
    $cdRoot = if (Test-Path -LiteralPath $drive -PathType Leaf) { ([IO.File]::ReadAllText($drive) -split "`r?`n")[0].Trim() } else { '' }
    $lines = @('[Menu]', "cd_root = $cdRoot", "game_dir = $installation", 'hangar = lowest-free', 'seed = ', 'chain_dir = ')
    # The shell itself refuses native-r111 unless menu.ini allows it; only reached with -AllowPrimary.
    if ($InstallName -ieq 'native-r111') { $lines += 'primary = allowed' }
    return ($lines -join "`r`n") + "`r`n"
}

$manifest = if (Test-Path -LiteralPath $manifestPath) { Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json } else { $null }
$present = @($files.Keys | Where-Object { Test-Path -LiteralPath (Join-Path $installation $files[$_]) -PathType Leaf })
# An ES2LAUNCH.EXE alone (start-own-launcher.ps1 copies it on every start) is no deployment: replaced by -Apply.
$state = if (!$manifest -and !(Test-Path -LiteralPath $shellDirectory) -and !($present | Where-Object { $_ -ne 'launcher' })) { 'none' }
         elseif ($manifest -and $present.Count -eq $files.Count -and (@($files.Keys | Where-Object { (Get-Hash (Join-Path $installation $files[$_])) -ne $manifest.files.$_ }).Count -eq 0)) { 'own' }
         else { 'unknown' }
if ($state -eq 'unknown' -and !$Remove) {
    throw "Unexpected own-shell files in $installation (present: $($present -join ', '); manifest $([bool]$manifest)); nothing changed. -Remove clears them."
}

$plan = [ordered]@{installation=$installation; state=$state}
if ($state -eq 'own') { $plan.files = $manifest.files }
if (!$Apply -and !$Remove) {
    $plan.preview = $true
    $plan.apply = "copy ES2LAUNCH.EXE and $shellDirectoryName\{es2shell.exe, SDL3.dll, menu.ini}, write own-shell-manifest.json"
    $plan.remove = "delete ES2LAUNCH.EXE, $shellDirectoryName\ and own-shell-manifest.json"
    $plan | ConvertTo-Json -Depth 4
    return
}

$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','ES2LAUNCH.EXE','es2shell.exe','es2menu.exe') -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -in @($installation, $shellDirectory)
})
if ($running.Count) { throw 'Close EarthSiege II in this installation before changing its shell.' }

if ($Remove) {
    if ($state -eq 'none') { $plan.removed = $false; $plan | ConvertTo-Json -Depth 4; return }
    # Without VSHELL.EXE the installation needs the own shell (retire-originals.ps1 -Restore first).
    if (Test-Path -LiteralPath (Join-Path $installation 'retired-originals.json')) { throw 'The installation''s VSHELL.EXE is retired; run retire-originals.ps1 -Restore before removing the own shell.' }
    foreach ($path in @((Join-Path $installation $files.launcher), $manifestPath)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    if (Test-Path -LiteralPath $shellDirectory) { Remove-Item -LiteralPath $shellDirectory -Recurse -Force }
    $plan.state = 'none'
    $plan.removed = $true
    $plan | ConvertTo-Json -Depth 4
    return
}

# -Apply
$sources = [ordered]@{launcher=$Launcher; shell=$Shell; sdl=$Sdl}
if (!$Launcher -or !$Shell -or !$Sdl) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $destination = Join-Path $repository 'build/reference-game'
    if (!$Launcher) { $sources.launcher = Copy-Es2BazelOutput -Target '//launcher:es2launch' -Destination $destination }
    if (!$Shell) { $sources.shell = Copy-Es2BazelOutput -Target '//shell:es2shell' -Destination $destination }
    if (!$Sdl) { $sources.sdl = Copy-Es2BazelOutput -Target '//shell:gui_sdl_runtime' -Destination $destination }
}
$hashes = [ordered]@{}
foreach ($key in $files.Keys) {
    $sources[$key] = [IO.Path]::GetFullPath($sources[$key])
    if (!(Test-Pe32 $sources[$key])) { throw "Not a 32-bit PE image: $($sources[$key])" }
    $hashes[$key] = Get-Hash $sources[$key]
}
if ($state -eq 'own' -and (@($files.Keys | Where-Object { $manifest.files.$_ -ne $hashes[$_] }).Count -eq 0)) {
    # Same build: only menu.ini is refreshed (the CD letter may have changed).
    [IO.File]::WriteAllText($iniPath, (Get-MenuIni), [Text.Encoding]::ASCII)
    $plan.applied = $false
    $plan | ConvertTo-Json -Depth 4
    return
}
# Staged beside its place, checked, then swapped in.
$staged = Join-Path $installation "$shellDirectoryName.staged"
if (Test-Path -LiteralPath $staged) { throw "A staged shell from an interrupted run is in the way: $staged" }
New-Item -ItemType Directory -Path $staged | Out-Null
try {
    $stagedFiles = @{launcher=(Join-Path $staged 'ES2LAUNCH.EXE'); shell=(Join-Path $staged 'es2shell.exe'); sdl=(Join-Path $staged 'SDL3.dll')}
    foreach ($key in $files.Keys) {
        Copy-Item -LiteralPath $sources[$key] -Destination $stagedFiles[$key]
        [IO.File]::SetAttributes($stagedFiles[$key], [IO.FileAttributes]::Normal)   # Bazel outputs are read-only
        if ((Get-Hash $stagedFiles[$key]) -ne $hashes[$key]) { throw "The staged $($files[$key]) does not match its source." }
    }
    [IO.File]::WriteAllText((Join-Path $staged 'menu.ini'), (Get-MenuIni), [Text.Encoding]::ASCII)
    if (Test-Path -LiteralPath $shellDirectory) { Remove-Item -LiteralPath $shellDirectory -Recurse -Force }
    Move-Item -LiteralPath $stagedFiles.launcher -Destination (Join-Path $installation $files.launcher) -Force
    Move-Item -LiteralPath $staged -Destination $shellDirectory
    foreach ($key in $files.Keys) {
        if ((Get-Hash (Join-Path $installation $files[$key])) -ne $hashes[$key]) { throw "The deployed $($files[$key]) does not match its source." }
    }
    $record = [ordered]@{
        schema=1; kind='es2-own-shell'; utc=[DateTime]::UtcNow.ToString('o'); installation=$installation
        files=$hashes; sources=$sources; shell_directory=$shellDirectoryName
        launch_shell="`"$(Join-Path $shellDirectory 'es2shell.exe')`" --menu `"$shellDirectory`""
        vshell='VSHELL.EXE stays in place, unused while the own shell is the default'
    }
    [IO.File]::WriteAllText($manifestPath, ($record | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
} catch {
    if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force }
    throw
}
$plan.state = 'own'
$plan.files = $hashes
$plan.applied = $true
$plan | ConvertTo-Json -Depth 4
