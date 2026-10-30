<#
Deploys the own cw3220.dll (//rtl:cw3220.dll) into a measurement clone, or
removes it again (docs/reference-game/rtl_imports.md, section "the own cw3220.dll").

  -Apply   CW3220.DLL (the original, checked against installation-manifest.json)
           is renamed to CW3220-ORIGINAL.DLL, the own DLL copied as CW3220.DLL,
           oracle-runtime-manifest.json records both hashes. DBSIM.EXE then runs
           the own runtime; ES.EXE and VSHELL.EXE run the original through it
           (delegate mode). A deployment of another build is replaced.
  -Remove  CW3220.DLL (the own DLL, checked against the runtime manifest) is
           deleted, CW3220-ORIGINAL.DLL renamed back and checked, the manifest
           removed.
  neither  a preview (JSON): the state found and what -Apply/-Remove would do.

The original file is never modified, copied over or deleted: it only changes
its name and its hash is checked before and after each step. Refused: the
primary installation native-r111 unless -AllowPrimary
(only on the owner's word), anything else that is not a separate
measurement clone, linked paths, a running game process of the clone, a clone held by
another session (CloneGuard.psm1), a source that is not the own DLL, and any
state whose files do not have the expected hashes.
#>
[CmdletBinding()]
param(
    [string]$DataRoot = '',
    [Parameter(Mandatory)][string]$InstallName,
    # The own DLL to deploy; default: //rtl:cw3220.dll built by Bazel and copied
    # to build/reference-game/rtl first.
    [string]$Source,
    [switch]$Apply,
    [switch]$Remove,
    # The primary installation native-r111, only on the owner's word.
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
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
$primary = $InstallName -ieq 'native-r111'
if ($primary -and !$AllowPrimary) { throw 'native-r111 is the primary installation; deploy there only on the owner''s word (-AllowPrimary).' }
if (!$primary) { Assert-MeasurementName $InstallName }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$installRoot = (Get-CloneInstallRoot $DataRoot) + [IO.Path]::DirectorySeparatorChar
$installation = [IO.Path]::GetFullPath((Join-Path $installRoot $InstallName))
if (!$installation.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $installRoot." }
Assert-NoLinks $installation
$manifest = Get-Content -LiteralPath (Join-Path $installation 'installation-manifest.json') -Raw | ConvertFrom-Json
if ($primary) {
    if ($manifest.destination -ne $installation -or $manifest.measurement_clone_of) { throw "Unexpected installation manifest of the primary installation: $installation" }
} elseif (!$manifest.measurement_clone_of -or $manifest.destination -ne $installation -or $manifest.measurement_clone_of -eq $installation) {
    throw 'The own runtime is deployed only into a separate measurement clone with an explicit source.'
}
$records = @($manifest.hashes | Where-Object { $_.file -eq 'CW3220.DLL' })
if ($records.Count -ne 1) { throw 'Missing or ambiguous original hash: CW3220.DLL' }
$originalHash = $records[0].sha256

$dll = Join-Path $installation 'CW3220.DLL'
$kept = Join-Path $installation 'CW3220-ORIGINAL.DLL'
$runtimePath = Join-Path $installation 'oracle-runtime-manifest.json'
function Get-Hash([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }

# The own DLL: a 32-bit DLL named cw3220.dll in its export directory that
# exports es2_own_runtime (the descriptor), parsed from the file's bytes.
function Test-OwnRuntimeDll([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x200 -or $b[0] -ne 0x4d -or $b[1] -ne 0x5a) { return $false }
    $pe = [BitConverter]::ToInt32($b, 0x3c)
    if ($pe -lt 0x40 -or $pe + 0xf8 -gt $b.Length -or [BitConverter]::ToUInt32($b, $pe) -ne 0x4550) { return $false }
    if ([BitConverter]::ToUInt16($b, $pe + 4) -ne 0x14c -or !([BitConverter]::ToUInt16($b, $pe + 22) -band 0x2000)) { return $false }
    $opt = $pe + 24
    if ([BitConverter]::ToUInt16($b, $opt) -ne 0x10b) { return $false }
    $sections = $opt + [BitConverter]::ToUInt16($b, $pe + 20)
    $count = [BitConverter]::ToUInt16($b, $pe + 6)
    $offset = {
        param([uint32]$rva)
        for ($i = 0; $i -lt $count; ++$i) {
            $s = $sections + 40 * $i
            $va = [BitConverter]::ToUInt32($b, $s + 12); $size = [Math]::Max([BitConverter]::ToUInt32($b, $s + 8), [BitConverter]::ToUInt32($b, $s + 16))
            if ($rva -ge $va -and $rva -lt $va + $size) { return [int64]$rva - $va + [BitConverter]::ToUInt32($b, $s + 20) }
        }
        return -1
    }
    $text = {
        param([int64]$at)
        if ($at -lt 0 -or $at -ge $b.Length) { return '' }
        $end = [Array]::IndexOf($b, [byte]0, [int]$at)
        if ($end -lt 0) { return '' }
        return [Text.Encoding]::ASCII.GetString($b, [int]$at, $end - [int]$at)
    }
    $exports = & $offset ([BitConverter]::ToUInt32($b, $opt + 96))
    if ($exports -lt 0 -or $exports + 40 -gt $b.Length) { return $false }
    if ((& $text (& $offset ([BitConverter]::ToUInt32($b, [int]$exports + 12)))) -ne 'cw3220.dll') { return $false }
    $names = [BitConverter]::ToUInt32($b, [int]$exports + 24)
    $table = & $offset ([BitConverter]::ToUInt32($b, [int]$exports + 32))
    if ($table -lt 0 -or $names -gt 4096) { return $false }
    for ($i = 0; $i -lt $names; ++$i) {
        if ((& $text (& $offset ([BitConverter]::ToUInt32($b, [int]$table + 4 * $i)))) -eq 'es2_own_runtime') { return $true }
    }
    return $false
}

# The state found: original, own (deployed) or anything else (refused).
$runtime = if (Test-Path -LiteralPath $runtimePath) { Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json } else { $null }
$dllHash = Get-Hash $dll
$keptHash = Get-Hash $kept
$state = if (!$runtime -and $dllHash -eq $originalHash -and !$keptHash) { 'original' }
         elseif ($runtime -and $runtime.original_sha256 -eq $originalHash -and $keptHash -eq $originalHash -and $dllHash -and $dllHash -eq $runtime.own_sha256) { 'own' }
         else { 'unknown' }
if ($state -eq 'unknown') {
    throw "Unexpected runtime files in $installation (CW3220.DLL $dllHash, CW3220-ORIGINAL.DLL $keptHash, runtime manifest $([bool]$runtime)); nothing changed."
}

$plan = [ordered]@{installation=$installation;state=$state;original_sha256=$originalHash}
if ($state -eq 'own') { $plan.own_sha256 = $runtime.own_sha256 }
if (!$Apply -and !$Remove) {
    $plan.preview = $true
    $plan.apply = 'rename CW3220.DLL to CW3220-ORIGINAL.DLL, copy the own cw3220.dll as CW3220.DLL, write oracle-runtime-manifest.json'
    $plan.remove = 'delete the own CW3220.DLL, rename CW3220-ORIGINAL.DLL back, remove oracle-runtime-manifest.json'
    $plan | ConvertTo-Json -Depth 4
    return
}

# Nothing of the clone may run, and no other session may hold it.
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
$running = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE') -and $_.ExecutablePath -and [IO.Path]::GetDirectoryName($_.ExecutablePath) -eq $installation
})
if ($running.Count) { throw 'Close EarthSiege II in this clone before changing its runtime.' }

function Write-RuntimeManifest([string]$OwnHash, [string]$SourcePath) {
    $record = [ordered]@{
        schema=1; kind='es2-own-runtime'; utc=[DateTime]::UtcNow.ToString('o'); installation=$installation
        own_sha256=$OwnHash; original_sha256=$originalHash; original_file='CW3220-ORIGINAL.DLL'; source=$SourcePath
        own_mode='DBSIM.EXE and every other process'; delegate_mode=@('ES.EXE','VSHELL.EXE')
    }
    [IO.File]::WriteAllText($runtimePath, ($record | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
}

if ($Remove) {
    if ($state -eq 'original') { $plan.removed = $false; $plan | ConvertTo-Json -Depth 4; return }
    Remove-Item -LiteralPath $dll
    Move-Item -LiteralPath $kept -Destination $dll
    if ((Get-Hash $dll) -ne $originalHash -or (Test-Path -LiteralPath $kept)) { throw 'The original CW3220.DLL did not come back unchanged.' }
    Remove-Item -LiteralPath $runtimePath
    $plan.state = 'original'
    $plan.removed = $true
    $plan | ConvertTo-Json -Depth 4
    return
}

# -Apply
if (!$Source) {
    Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
    $Source = Copy-Es2BazelOutput -Target '//rtl:cw3220.dll' -Destination (Join-Path $repository 'build/reference-game/rtl')
}
$Source = [IO.Path]::GetFullPath($Source)
if (!(Test-OwnRuntimeDll $Source)) { throw "Not the own cw3220.dll (no export es2_own_runtime): $Source" }
$sourceHash = Get-Hash $Source
if ($sourceHash -eq $originalHash) { throw 'The source is the original CW3220.DLL.' }
if ($state -eq 'own' -and $runtime.own_sha256 -eq $sourceHash) { $plan.applied = $false; $plan | ConvertTo-Json -Depth 4; return }
# The new DLL is staged next to its place and checked before anything is renamed.
$staged = Join-Path $installation 'CW3220.DLL.staged'
if (Test-Path -LiteralPath $staged) { throw "A staged runtime from an interrupted run is in the way: $staged" }
Copy-Item -LiteralPath $Source -Destination $staged
# Bazel's outputs are read-only; the deployed DLL is an ordinary file of the clone.
[IO.File]::SetAttributes($staged, [IO.FileAttributes]::Normal)
if ((Get-Hash $staged) -ne $sourceHash) { Remove-Item -LiteralPath $staged; throw 'The staged CW3220.DLL does not match the source.' }
if ($state -eq 'original') {
    Move-Item -LiteralPath $dll -Destination $kept
    if ((Get-Hash $kept) -ne $originalHash) { throw 'CW3220-ORIGINAL.DLL does not hold the original after the rename.' }
}
try {
    Move-Item -LiteralPath $staged -Destination $dll -Force
    if ((Get-Hash $dll) -ne $sourceHash) { throw 'The deployed CW3220.DLL does not match the source.' }
    Write-RuntimeManifest $sourceHash $Source
} catch {
    # Back to the original state when this run renamed it.
    if ($state -eq 'original') {
        if (Test-Path -LiteralPath $dll) { Remove-Item -LiteralPath $dll }
        if (Test-Path -LiteralPath $runtimePath) { Remove-Item -LiteralPath $runtimePath }
        Move-Item -LiteralPath $kept -Destination $dll
    }
    if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged }
    throw
}
$plan.state = 'own'
$plan.own_sha256 = $sourceHash
$plan.applied = $true
$plan | ConvertTo-Json -Depth 4
