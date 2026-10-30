<#
DBSIM.EXE and VSHELL.EXE leave the played installations; the oracle's
measurement clones keep them (docs/reference-game/setup.md, section
"played installations without `DBSIM.EXE` and `VSHELL.EXE`").

  -Apply    the installation's DBSIM.EXE and VSHELL.EXE, each with the hash
            recorded in installation-manifest.json, are deleted and
            retired-originals.json records them. The installation must run
            the own chain: the own shell (configure-shell.ps1) and es2sim
            (configure-sim.ps1) deployed, since neither reads either
            file. start-native.ps1 then skips both files'
            checks and refuses -Shell vshell; configure-shell.ps1 -Remove and
            configure-sim.ps1 -Remove refuse until -Restore.
  -Restore  both files are copied back from the extracted CD
            (<ExtractedRoot>\cd\VER95), checked against the recorded hashes,
            and retired-originals.json is deleted.
  neither   a preview (JSON): the state found and what -Apply/-Restore would do.

Refused: measurement clones (installation-manifest.json measurement_clone_of:
the oracle runs DBSIM.EXE there), the primary installation native-r111 unless
-AllowPrimary (only on the owner's word), linked paths,
a running game process of the installation, a clone held by another session
(CloneGuard.psm1), and a state whose files do not have the recorded hashes.
#>
[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD (cd), the source of -Restore.
    [string]$ExtractedRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    [switch]$Apply,
    [switch]$Restore,
    [switch]$AllowPrimary
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
if ($Apply -and $Restore) { throw 'Use either -Apply or -Restore.' }
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
if ($InstallName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
if ($InstallName -ieq 'native-r111' -and !$AllowPrimary) { throw 'native-r111 is the primary installation; change it only on the owner''s word (-AllowPrimary).' }
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$manifestFile = Join-Path $installation 'installation-manifest.json'
if (!(Test-Path -LiteralPath $manifestFile -PathType Leaf)) { throw "Not a prepared installation (no installation-manifest.json): $installation" }
Assert-NoLinks $installation
$installationManifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
if ($installationManifest.measurement_clone_of) { throw "$InstallName is a measurement clone; the oracle keeps DBSIM.EXE and VSHELL.EXE there." }

$retired = @('DBSIM.EXE', 'VSHELL.EXE')
$recordPath = Join-Path $installation 'retired-originals.json'
function Get-Hash([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }
$want = [ordered]@{}
foreach ($file in $retired) {
    $records = @($installationManifest.hashes | Where-Object { $_.file -eq $file })
    if ($records.Count -ne 1) { throw "Missing or ambiguous original hash: $file" }
    $want[$file] = [string]$records[0].sha256
}

# The state found: present (both files with their hashes, no record), retired (record, neither file) or anything else.
$present = @($retired | Where-Object { Test-Path -LiteralPath (Join-Path $installation $_) })
$record = if (Test-Path -LiteralPath $recordPath) { Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json } else { $null }
$state = if (!$record -and $present.Count -eq $retired.Count -and
             !(@($retired | Where-Object { (Get-Hash (Join-Path $installation $_)) -ne $want[$_] }).Count)) { 'present' }
         elseif ($record -and !$present.Count) { 'retired' }
         else { 'unknown' }
if ($state -eq 'unknown') { throw "Unexpected state in $installation (present: $($present -join ', '); record $([bool]$record)); nothing changed." }

$plan = [ordered]@{installation = $installation; state = $state; files = $want}
if (!$Apply -and !$Restore) {
    $plan.preview = $true
    $plan.apply = 'delete DBSIM.EXE and VSHELL.EXE, write retired-originals.json (needs the own shell and es2sim deployed)'
    $plan.restore = 'copy both back from the extracted CD''s VER95, check the hashes, delete retired-originals.json'
    $plan | ConvertTo-Json -Depth 4
    return
}
if (($Apply -and $state -eq 'retired') -or ($Restore -and $state -eq 'present')) {
    $plan.changed = $false
    $plan | ConvertTo-Json -Depth 4
    return
}

# Nothing of the installation may run, and no other session may hold it.
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','ES2LAUNCH.EXE','es2shell.exe','es2menu.exe','es2sim.exe') -and $_.ExecutablePath -and
    [IO.Path]::GetDirectoryName($_.ExecutablePath) -in @($installation, (Join-Path $installation 'ES2SHELL'), (Join-Path $installation 'ES2SIM'))
})
if ($running.Count) { throw 'Close EarthSiege II in this installation before changing its original files.' }

if ($Apply) {
    # The own chain must stand in for both: the own shell and es2sim, each with its manifest and launch file.
    foreach ($needed in @('own-shell-manifest.json', 'own-sim-manifest.json', 'es2launch.ini', 'ES2LAUNCH.EXE', 'ES2SHELL\es2shell.exe', 'ES2SIM\es2sim.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $installation $needed) -PathType Leaf)) {
            throw "The own chain is not deployed in ${InstallName} (missing $needed): configure-shell.ps1 and configure-sim.ps1 -Apply first."
        }
    }
    # The record first, so that an interrupted run leaves a state -Restore repairs.
    $entry = [ordered]@{schema = 1; kind = 'es2-retired-originals'; utc = [DateTime]::UtcNow.ToString('o'); installation = $installation; files = $want
        restore = 'retire-originals.ps1 -Restore (from the extracted CD''s VER95)'}
    [IO.File]::WriteAllText($recordPath, ($entry | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    foreach ($file in $retired) { Remove-Item -LiteralPath (Join-Path $installation $file) -Force }
    if (@($retired | Where-Object { Test-Path -LiteralPath (Join-Path $installation $_) }).Count) { throw 'The original files could not be removed completely.' }
    $plan.state = 'retired'
    $plan.changed = $true
    $plan | ConvertTo-Json -Depth 4
    return
}

# -Restore: from the extracted CD, each copy checked against the recorded hash.
$ver95 = Join-Path ([IO.Path]::GetFullPath($ExtractedRoot)) 'cd\VER95'
foreach ($file in $retired) {
    $source = Join-Path $ver95 $file
    if ((Get-Hash $source) -ne $want[$file]) { throw "The extracted CD's $file does not have the recorded hash: $source" }
}
foreach ($file in $retired) {
    $target = Join-Path $installation $file
    Copy-Item -LiteralPath (Join-Path $ver95 $file) -Destination $target
    if ((Get-Hash $target) -ne $want[$file]) { Remove-Item -LiteralPath $target -Force; throw "The restored $file does not match its recorded hash." }
}
Remove-Item -LiteralPath $recordPath -Force
$plan.state = 'present'
$plan.changed = $true
$plan | ConvertTo-Json -Depth 4
