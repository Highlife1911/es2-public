[CmdletBinding()]
param(
    [string]$Destination = '',
    # Check the installed SDL3 trees and kept archives against the pins below; never downloads, extracts or writes.
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
if (!$Destination) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $Destination = (Get-Es2Layout).Dependencies
}
$Destination = [IO.Path]::GetFullPath($Destination)

# Approved in docs/dependencies.md. Hashes are from the official release asset metadata:
# https://github.com/libsdl-org/SDL/releases/expanded_assets/release-3.4.18
$packages = @(
    @{
        directory='sdl3-3.4.18'; archive='SDL3-devel-3.4.18-mingw.zip'
        url='https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-mingw.zip'
        sha256='cd98158c8d025a816f430a600b4e9526524e6219ee884ec72bd498b80fd3bf58'
    }
)
if ($VerifyOnly) {
    # One line per item, "[OK|MISSING|MISMATCH|WARN] item - detail" (same format as setup-dependencies.ps1 -VerifyOnly).
    $problems = 0
    foreach ($package in $packages) {
        $target = Join-Path $Destination $package.directory
        $archive = Join-Path $Destination ('downloads/' + $package.archive)
        if (!(Test-Path -LiteralPath $target -PathType Container)) {
            Write-Output "[MISSING] $($package.directory) - not installed under $Destination (setup-dependencies.ps1 -Platform installs it from $($package.url))"
            $problems++
        } elseif (!(Test-Path -LiteralPath $archive)) {
            Write-Output "[OK] $($package.directory) - directory present; archive $($package.archive) not kept, so its hash $($package.sha256) cannot be rechecked"
        } elseif ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
            Write-Output "[MISMATCH] $($package.directory) - kept archive $($package.archive) does not match the pinned SHA-256"
            $problems++
        } else {
            Write-Output "[OK] $($package.directory) - directory present, kept archive matches the pinned SHA-256"
        }
    }
    if ($problems) { exit 1 }
    return
}
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
if (!(Test-Path -LiteralPath $sevenZip)) { throw 'Existing system 7-Zip is required.' }
$downloads = Join-Path $Destination 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
foreach ($package in $packages) {
    $target = [IO.Path]::GetFullPath((Join-Path $Destination $package.directory))
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite an existing dependency: $target" }
    $archive = Join-Path $downloads $package.archive
    if (!(Test-Path -LiteralPath $archive)) {
        Write-Output "Downloading $($package.archive)"
        Invoke-WebRequest -Uri $package.url -OutFile $archive
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $package.sha256) { throw "Checksum mismatch: $archive" }
    $staging = Join-Path $Destination ('extract-platform-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging | Out-Null
    & $sevenZip x $archive "-o$staging" -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $archive" }
    $children = @(Get-ChildItem -LiteralPath $staging -Force)
    if ($children.Count -ne 1 -or !$children[0].PSIsContainer) { throw "Unexpected archive layout: $archive" }
    $source = [IO.Path]::GetFullPath($children[0].FullName)
    $prefix = $Destination.TrimEnd('\') + '\'
    if (!$source.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        !$target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to move a dependency path outside the requested directory.'
    }
    Move-Item -LiteralPath $source -Destination $target
    Remove-Item -LiteralPath $staging
    Write-Output "Installed $($package.directory); SHA-256 verified."
}
$packages | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'platform-manifest.json') -Encoding utf8
