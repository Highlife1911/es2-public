[CmdletBinding()]
param(
    [string]$DataRoot='',
    [string]$SourceName='timing-r111',
    [Parameter(Mandatory)][ValidateRange(1,5)][int]$Number,
    [switch]$Apply
)
if (!$DataRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
}
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/OracleSpeed.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneInstallRoot
Assert-MeasurementName $SourceName
if ($SourceName -ne 'timing-r111') { throw 'The source must be timing-r111.' }
$root=Get-CloneInstallRoot $DataRoot
$source=Join-Path $root $SourceName
# timing-r111 plus p1..p5 are the six clones the scripts accept (run-oracle-parallel.ps1).
$destination=Join-Path $root "timing-r111-p$Number"
Assert-MeasurementName ([IO.Path]::GetFileName($destination))
Assert-NoLinks $source
if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination" }
$manifest=Get-Content -LiteralPath (Join-Path $source 'installation-manifest.json') -Raw | ConvertFrom-Json
if (!$manifest.measurement_clone_of -or $manifest.destination -ne $source -or $manifest.measurement_clone_of -eq $source) {
    throw 'Source is not an identified measurement clone.'
}
$entries=@();$pending=[Collections.Queue]::new();$pending.Enqueue($source)
while ($pending.Count) {
    foreach ($entry in Get-ChildItem -LiteralPath ($pending.Dequeue()) -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source contains a link/junction.' }
        $entries+=$entry
        if ($entry.PSIsContainer) {$pending.Enqueue($entry.FullName)}
    }
}
$files=@($entries | Where-Object { !$_.PSIsContainer })
if (!$Apply) {
    [ordered]@{dry_run=$true;source=$source;destination=$destination;files=$files.Count;verify='SHA256 of every copied file before updating clone metadata'} | ConvertTo-Json
    return
}
$running=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE') -and $_.ExecutablePath -and [IO.Path]::GetDirectoryName($_.ExecutablePath) -eq $source })
if ($running.Count) { throw 'Source clone is running; close it before taking a consistent copy.' }
# No overwrite or recursive cleanup. A failed copy remains visibly incomplete.
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($directory in @($entries | Where-Object {$_.PSIsContainer})) {
    New-Item -ItemType Directory -Path (Join-Path $destination $directory.FullName.Substring($source.Length+1)) -Force | Out-Null
}
$hashes=@()
foreach ($file in $files) {
    $relative=$file.FullName.Substring($source.Length+1)
    $copy=Join-Path $destination $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($copy)) -Force | Out-Null
    $before=(Get-FileHash -LiteralPath $file.FullName).Hash
    Copy-Item -LiteralPath $file.FullName -Destination $copy
    if ((Get-FileHash -LiteralPath $copy).Hash -ne $before -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $before) {
        throw "Copy hash mismatch or source changed: $relative"
    }
    $hashes+=@{file=$relative;sha256=$before}
}
# The whole destination tree is checked once more before it is recorded as a clone: exactly
# the copied files (none missing, none extra) and every hash read again.
$expected=@{};foreach ($h in $hashes) { $expected[$h.file]=$h.sha256 }
$found=@(Get-ChildItem -LiteralPath $destination -Recurse -Force -File)
if ($found.Count -ne $expected.Count) { throw "Copy verification failed: $($found.Count) files in the destination, $($expected.Count) copied." }
foreach ($f in $found) {
    $relative=$f.FullName.Substring($destination.Length+1)
    if (!$expected.ContainsKey($relative) -or (Get-FileHash -LiteralPath $f.FullName).Hash -ne $expected[$relative]) { throw "Copy verification failed: $relative" }
}
$manifest.destination=$destination
$manifest.measurement_clone_of=$source
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $destination 'installation-manifest.json') -Encoding UTF8
[ordered]@{schema=1;source=$source;destination=$destination;utc=[DateTime]::UtcNow.ToString('o');copied_hashes=$hashes
    changed_after_copy=@('installation-manifest.json');verified=$true} | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $destination 'measurement-clone-copy.json') -Encoding UTF8
Write-Output "Verified measurement clone: $destination"
