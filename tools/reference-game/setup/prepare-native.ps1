[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD (cd) and the data ISO (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'native-r111',
    [ValidateSet('English','German','French')][string]$Language = 'German',
    [switch]$InstallBundledRuntime,
    # Deploy the own launcher and shell as the installation's default afterwards (configure-shell.ps1 -Apply;
    # builds them with Bazel). VSHELL.EXE is copied in any case and stays untouched.
    [switch]$OwnShell,
    # Deploy es2sim as what the own launcher starts (configure-sim.ps1 -Apply; builds it with Bazel).
    [switch]$OwnSim,
    # A played installation without DBSIM.EXE and VSHELL.EXE (retire-originals.ps1 -Apply); needs -OwnShell
    # and -OwnSim. The files are copied and checked first, so the manifest records their hashes for -Restore.
    [switch]$RetireOriginals
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
$ErrorActionPreference = 'Stop'
if ($RetireOriginals -and !($OwnShell -and $OwnSim)) { throw '-RetireOriginals needs -OwnShell and -OwnSim (only the own chain runs without the originals).' }
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
$cd = Join-Path $ExtractedRoot 'cd'
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$destination = Join-Path $installRoot $InstallName
$destination = [IO.Path]::GetFullPath($destination)
if (!$destination.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
$bundled = @('CW3220.DLL','BWCC32.DLL','SOSLIBS3.DLL','VER95/SOS9503.DLL')
if (!$InstallBundledRuntime) {
    Write-Output "PREVIEW ONLY. Destination: $destination"
    Write-Output 'This setup copies the original game and the following bundled runtime libraries locally:'
    $bundled | ForEach-Object { Write-Output ('  ' + $_) }
    Write-Output 'No downloads, codec/driver installations, system DLL replacement or registry writes.'
    Write-Output 'Obtain explicit user approval before running again with -InstallBundledRuntime.'
    return
}
if (Test-Path -LiteralPath $destination) { throw "Installation already exists; choose a fresh InstallName: $destination" }
$iso = Join-Path $ExtractedRoot 'earthsiege2-data.iso'
$image = Get-DiskImage -ImagePath $iso
if (!$image.Attached) { $image = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
$volume = $image | Get-Volume
if (!$volume.DriveLetter -or $volume.FileSystemLabel -ne 'EARTHSIEGE2') { throw 'Expected mounted EARTHSIEGE2 data CD.' }
$cdRoot = $volume.DriveLetter + ':'
$languageCode = @{English='E';German='G';French='F'}[$Language]
$languageExtension = @{English='ENG';German='GER';French='FRE'}[$Language]
New-Item -ItemType Directory -Path $destination | Out-Null
foreach($folder in @('DATA','SAV','TAPES','VOL',$Language.ToUpperInvariant())) {
    Copy-Item -LiteralPath (Join-Path $cd $folder) -Destination $destination -Recurse
}
foreach($file in @('CW3220.DLL','BWCC32.DLL','SOSLIBS3.DLL','HMIDRV.WIN','HMIMDRV.WIN','SOSLIB.INI','ESII.ICO','VERSION.TXT')) {
    Copy-Item -LiteralPath (Join-Path $cd $file) -Destination $destination
}
foreach($file in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','SOS9503.DLL')) {
    Copy-Item -LiteralPath (Join-Path $cd ('VER95/' + $file)) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $cd ($Language + '/ERROR.' + $languageExtension)) -Destination (Join-Path $destination 'ERROR.STR')
Copy-Item -LiteralPath (Join-Path $cd ($Language + '/MISSION.' + $languageExtension)) -Destination (Join-Path $destination 'DATA/MISSION.STR')
# Derived from original BATCH.EXE: source root then install root, CRLF; language is one character.
[IO.File]::WriteAllText((Join-Path $destination 'DATA/DRIVE.CFG'), "$cdRoot`r`n$destination`r`n", [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $destination 'DATA/LANGUAGE.CFG'), $languageCode, [Text.Encoding]::ASCII)
$hashes = foreach($file in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','CW3220.DLL','BWCC32.DLL','SOS9503.DLL','SOSLIBS3.DLL')) {
    $installed = Join-Path $destination $file
    $source = if ($file -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE','SOS9503.DLL')) { Join-Path $cd ('VER95/' + $file) } else { Join-Path $cd $file }
    $actual = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash
    if ($actual -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw "Original file hash changed: $file" }
    [ordered]@{file=$file;sha256=$actual}
}
[ordered]@{destination=$destination;language=$Language;cd=$cdRoot;bundled_runtime_approved=$true;hashes=$hashes;execution_tested=$false} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'installation-manifest.json') -Encoding utf8
Write-Output "Portable files prepared: $destination. No game was launched; runtime acceptance is still required."
if ($OwnShell) {
    $shellArgs = @{DataRoot=$DataRoot; InstallName=$InstallName; Apply=$true}
    if ($InstallName -ieq 'native-r111') { $shellArgs.AllowPrimary = $true }
    & (Join-Path $PSScriptRoot 'configure-shell.ps1') @shellArgs
    Write-Output "Own shell deployed: START_ES2.cmd starts ES2LAUNCH.EXE with ES2SHELL\es2shell.exe (-Shell vshell for the original)."
}
$changeArgs = @{DataRoot=$DataRoot; InstallName=$InstallName; Apply=$true}
if ($InstallName -ieq 'native-r111') { $changeArgs.AllowPrimary = $true }
if ($OwnSim) {
    & (Join-Path $PSScriptRoot 'configure-sim.ps1') @changeArgs
    Write-Output 'Own simulator deployed: ES2LAUNCH.EXE starts ES2SIM\es2sim.exe instead of DBSIM.EXE.'
}
if ($RetireOriginals) {
    & (Join-Path $PSScriptRoot 'retire-originals.ps1') @changeArgs -ExtractedRoot $ExtractedRoot
    Write-Output 'DBSIM.EXE and VSHELL.EXE retired: only the own chain starts; retire-originals.ps1 -Restore brings them back.'
}