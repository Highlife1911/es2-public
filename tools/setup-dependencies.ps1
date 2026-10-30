[CmdletBinding()]
param(
    [string]$Destination = '',
    [switch]$Platform,
    # Delegate to setup-web-dependencies.ps1 (emsdk and FFmpeg); -VerifyOnly passes through.
    [switch]$Web,
    [switch]$Python,
    [string]$PythonSource,
    # Check every installed dependency against the pins and report what is missing; never downloads, copies or writes.
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
if (!$Destination) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $Destination = (Get-Es2Layout).Dependencies
}
if ($Platform) {
    & (Join-Path $PSScriptRoot 'setup-platform-dependencies.ps1') -Destination $Destination -VerifyOnly:$VerifyOnly
    return
}
if ($Web) {
    & (Join-Path $PSScriptRoot 'setup-web-dependencies.ps1') -Destination $Destination -VerifyOnly:$VerifyOnly
    exit $LASTEXITCODE
}
$Destination = [IO.Path]::GetFullPath($Destination)
# Pinned Python runtime (-Python and -VerifyOnly; docs/dependencies.md).
$pin = @{ version='3.12.14'; exe='10d845f50a2af64e3500bb2fcb348b5bc98a75d8ddada63e45ba1da6a1fc79d1';
    dll='c19758a4dfccc35b5f4da513945dfab85e70bed8cdbf4bc25587662fa7f457d2';
    tree='63c034f113f0013c8f86d4aa326d17442e3a45a29ed9724f4486c8853d23cd28'; files=1029 }
if ($Python) {
    # Python 3.12.14, standard library only: copied once from a local runtime's
    # python folder (no download), without
    # Scripts, tcl, site-packages contents and __pycache__. Copies only when
    # absent; always verifies the pinned hashes and the tree digest.
    $target = Join-Path $Destination 'python'
    if (!(Test-Path -LiteralPath $target)) {
        if (!$PythonSource) { throw "Missing $target; pass -PythonSource <python folder> (docs/dependencies.md)." }
        $source = [IO.Path]::GetFullPath($PythonSource).TrimEnd('\')
        foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File -Force) {
            $relative = $file.FullName.Substring($source.Length + 1)
            if ($relative -match '^(Scripts|tcl|Lib\\site-packages)\\' -or $relative -match '(^|\\)__pycache__\\') { continue }
            $copy = Join-Path $target $relative
            New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($copy)) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $copy
        }
        New-Item -ItemType Directory -Force -Path (Join-Path $target 'Lib/site-packages') | Out-Null
    }
    $exe = Join-Path $target 'python.exe'
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $pin.exe -or
        (Get-FileHash -LiteralPath (Join-Path $target 'python312.dll') -Algorithm SHA256).Hash -ne $pin.dll) { throw "Python binary hash mismatch: $target" }
    $tree = & $exe -B -I (Join-Path $PSScriptRoot 'python_tree_digest.py') $target
    if ($LASTEXITCODE -ne 0 -or $tree -ne "$($pin.tree) $($pin.files)") { throw "Python tree digest mismatch: $tree" }
    Write-Output "Python $($pin.version) verified: $exe ($($pin.files) files)"
    return
}
$packages = @(
    @{ directory='llvm-mingw'; archive='llvm-mingw-20260922-ucrt-x86_64.zip'; url='https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip'; sha256='e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666' }
)
if ($VerifyOnly) {
    # Reads and hashes only: no download, copy, extraction or write (not even downloads/ or manifest.json).
    # One line per item, "[OK|MISSING|MISMATCH|WARN] item - detail"; exit status 1 if an item the build needs is
    # MISSING or MISMATCH. WARN lines are advisory (installers).
    $problems = 0
    function Write-Verdict([string]$Status, [string]$Item, [string]$Detail) {
        Write-Output "[$Status] $Item - $Detail"
        if ($Status -in 'MISSING', 'MISMATCH') { $script:problems++ }
    }
    $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    # 7-Zip: only the installer extracts archives; builds and tests never call it.
    if (Test-Path -LiteralPath 'C:\Program Files\7-Zip\7z.exe') { Write-Verdict OK '7-Zip' 'C:\Program Files\7-Zip\7z.exe' }
    else { Write-Verdict WARN '7-Zip' 'C:\Program Files\7-Zip\7z.exe absent: only needed to install LLVM-MinGW or SDL3 from the archives' }
    # System Bazel against .bazelversion.
    $expected = (Get-Content -LiteralPath (Join-Path $repoRoot '.bazelversion') -TotalCount 1).Trim()
    $bazelCommand = Get-Command bazel -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$bazelCommand) { Write-Verdict MISSING 'Bazel' "no bazel on PATH; .bazelversion requires $expected" }
    else {
        $actual = (& $bazelCommand.Source --version 2>$null | Select-Object -First 1)
        if ($actual -eq "bazel $expected") { Write-Verdict OK 'Bazel' "$expected at $($bazelCommand.Source)" }
        else { Write-Verdict MISMATCH 'Bazel' "$($bazelCommand.Source) reports '$actual'; .bazelversion requires $expected" }
    }
    # PowerShell 7 of Bazel's actions (bazel/platforms/host_repository.bzl): ES2_PWSH, else pwsh.exe on PATH.
    $pwshPath = if ($env:ES2_PWSH) { $env:ES2_PWSH } else { Get-Command pwsh.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source }
    if (!$pwshPath) { Write-Verdict MISSING 'PowerShell 7' 'no pwsh.exe on PATH and ES2_PWSH unset' }
    elseif (Test-Path -LiteralPath $pwshPath) { Write-Verdict OK 'PowerShell 7' $pwshPath }
    else { Write-Verdict MISSING 'PowerShell 7' "ES2_PWSH $pwshPath does not exist" }
    # Pinned Python: hashes and tree digest as -Python does, but never copies.
    $pythonDirectory = Join-Path $Destination 'python'
    $pythonExe = Join-Path $pythonDirectory 'python.exe'
    $pythonDll = Join-Path $pythonDirectory 'python312.dll'
    if (!(Test-Path -LiteralPath $pythonExe)) { Write-Verdict MISSING "Python $($pin.version)" "$pythonDirectory absent (setup-dependencies.ps1 -Python -PythonSource <python folder> recreates it)" }
    elseif (!(Test-Path -LiteralPath $pythonDll) -or
        (Get-FileHash -LiteralPath $pythonExe -Algorithm SHA256).Hash -ne $pin.exe -or
        (Get-FileHash -LiteralPath $pythonDll -Algorithm SHA256).Hash -ne $pin.dll) {
        Write-Verdict MISMATCH "Python $($pin.version)" "python.exe or python312.dll in $pythonDirectory is missing or differs from the pinned hash"
    } else {
        $tree = & $pythonExe -B -I (Join-Path $PSScriptRoot 'python_tree_digest.py') $pythonDirectory
        if ($LASTEXITCODE -eq 0 -and $tree -eq "$($pin.tree) $($pin.files)") { Write-Verdict OK "Python $($pin.version)" "$pythonExe, tree digest matches ($($pin.files) files)" }
        else { Write-Verdict MISMATCH "Python $($pin.version)" "tree digest differs: $tree" }
    }
    # LLVM-MinGW: installed tree (the zip is normally not kept, so its hash cannot be rechecked afterwards).
    foreach ($package in $packages) {
        $target = Join-Path $Destination $package.directory
        $archive = Join-Path $Destination ('downloads/' + $package.archive)
        $clang = Join-Path $target 'bin/clang.exe'
        if (!(Test-Path -LiteralPath $clang)) { Write-Verdict MISSING $package.directory "$clang absent (setup-dependencies.ps1 downloads $($package.url))" }
        elseif (Test-Path -LiteralPath $archive) {
            if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -eq $package.sha256) { Write-Verdict OK $package.directory 'installed; kept archive matches the pinned SHA-256' }
            else { Write-Verdict MISMATCH $package.directory "kept archive $($package.archive) does not match the pinned SHA-256" }
        } else {
            $version = (& $clang --version 2>$null | Select-Object -First 1)
            if ($version -match 'clang version 23\.1\.2 ') { Write-Verdict OK $package.directory "installed ($version); archive not kept, so its hash $($package.sha256) cannot be rechecked" }
            else { Write-Verdict MISMATCH $package.directory "clang reports '$version'; docs/dependencies.md pins Clang 23.1.2" }
        }
    }
    # System DirectDraw library read by the proxy build (proxy_host_inputs).
    $module = Get-Content -LiteralPath (Join-Path $repoRoot 'MODULE.bazel') -Raw
    if ($module -match '(?m)^\s*ddraw\s*=\s*"([^"]+)"') {
        if (Test-Path -LiteralPath $Matches[1]) { Write-Verdict OK 'ddraw.dll' $Matches[1] } else { Write-Verdict MISSING 'ddraw.dll' "$($Matches[1]) (MODULE.bazel proxy_host)" }
    }
    # SDL3 tree (separate pin in setup-platform-dependencies.ps1).
    $platformOutput = @(& (Join-Path $PSScriptRoot 'setup-platform-dependencies.ps1') -Destination $Destination -VerifyOnly)
    $platformOutput | ForEach-Object { Write-Output $_ }
    $problems += @($platformOutput | Where-Object { $_ -match '^\[(MISSING|MISMATCH)\]' }).Count
    if ($problems) { Write-Output "$problems item(s) missing or mismatched."; exit 1 }
    Write-Output 'All required dependencies verified.'
    exit 0
}
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
if (!(Test-Path -LiteralPath $sevenZip)) { throw 'System 7-Zip is required.' }
New-Item -ItemType Directory -Path (Join-Path $Destination 'downloads') -Force | Out-Null
foreach ($package in $packages) {
    $target = Join-Path $Destination $package.directory
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite existing tool: $target" }
    $archive = Join-Path $Destination ('downloads/' + $package.archive)
    if (!(Test-Path -LiteralPath $archive)) {
        Write-Output "Downloading $($package.archive)"
        Invoke-WebRequest -Uri $package.url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.sha256) { throw "Checksum mismatch: $archive" }
    $staging = Join-Path $Destination ('extract-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging | Out-Null
    & $sevenZip x $archive "-o$staging" -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $archive" }
    $children = @(Get-ChildItem -LiteralPath $staging -Force)
    if ($children.Count -ne 1 -or !$children[0].PSIsContainer) { throw "Unexpected archive layout: $archive" }
    # Both paths are checked within the explicitly requested dependency directory.
    $source = [IO.Path]::GetFullPath($children[0].FullName)
    $prefix = $Destination.TrimEnd('\') + '\'
    if (!$source.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or !$target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path outside dependency directory.' }
    Move-Item -LiteralPath $source -Destination $target
    Remove-Item -LiteralPath $staging
    Write-Output "Installed $($package.directory); SHA-256 verified."
}
# Bazel is the system installation on PATH (not downloaded here); only its
# version is checked against the repository's .bazelversion.
$bazelCommand = Get-Command bazel -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$bazelExpected = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../.bazelversion') -TotalCount 1).Trim()
if (!$bazelCommand) { Write-Warning "System Bazel $bazelExpected not found on PATH (see docs/dependencies.md)." }
else {
    $bazelActual = (& $bazelCommand.Source --version 2>$null | Select-Object -First 1)
    if ($bazelActual -ne "bazel $bazelExpected") { Write-Warning "System Bazel at $($bazelCommand.Source) reports '$bazelActual'; .bazelversion requires $bazelExpected." }
    else { Write-Output "System Bazel $bazelExpected at $($bazelCommand.Source)." }
}
$packages | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding utf8
