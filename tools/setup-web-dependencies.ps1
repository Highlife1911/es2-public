[CmdletBinding()]
param(
    [string]$Destination = '',
    # Check the pins, the kept archives, the installed trees and the frozen cache; never downloads, extracts or writes.
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
if (!$Destination) {
    Import-Module (Join-Path $PSScriptRoot 'reference-game\setup\CloneGuard.psm1')
    $Destination = (Get-Es2Layout).Dependencies
}
$Destination = [IO.Path]::GetFullPath($Destination)
$downloads = Join-Path $Destination 'downloads'
$emsdkDir = Join-Path $Destination 'emsdk'
$ffmpegDir = Join-Path $Destination 'ffmpeg'

# Approved in docs/dependencies.md. Every archive is downloaded by this script and verified before it is
# used; emsdk is only handed the verified files (downloads\ of the emsdk directory, EMSDK_KEEP_DOWNLOADS) and
# is never allowed to fetch anything itself.
$emsdkTag = '6.0.12'
$emsdkCommit = '35ff8a6d150541276abbc6bae512ca90bcfbe220'
$releasesHash = '6e9f6885cb17a3d6c8fd488a5cff730f50aba61d'   # emscripten-releases-tags.json, "6.0.12"
$releases = 'https://storage.googleapis.com/webassembly/emscripten-releases-builds'
# role 'emsdk': extracted by this script; role 'sdk': placed under 'sdkName' in emsdk\downloads for emsdk to unpack.
$archives = @(
    @{ role='emsdk'; name="emsdk-$emsdkTag.zip"; url="https://github.com/emscripten-core/emsdk/archive/refs/tags/$emsdkTag.zip"
       sha256='199dc3ad6a662e832c97005a8c7de8697f5d3496ffe7029b6620fee302eacfcc' }
    @{ role='sdk'; name="$releasesHash-wasm-binaries.zip"; url="$releases/win/$releasesHash/wasm-binaries.zip"
       sha256='51ac2e51a2c6eeba09fa04418496750662b5c1f022665814b0ee8f557c8328c9' }
    @{ role='sdk'; name='node-v24.19.0-win-x64.zip'; url="$releases/deps/node-v24.19.0-win-x64.zip"
       sha256='57f71ab3652e797d84acddc79c81cc9ff1c6ddb2a1974cdb83f00fee9bff4c73' }   # also in nodejs.org SHASUMS256.txt
    @{ role='sdk'; name='python-3.13.3-0-win-amd64.zip'; url="$releases/deps/python-3.13.3-0-win-amd64.zip"
       sha256='6fe7a540c6b8b185780467cf7495e884ee62316ec4abb19e1c735b8a77c62465' }
)
$ffmpegArchive = @{ role='ffmpeg'; name='ffmpeg-9.0.2-full_build.zip'
    url='https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-full_build.zip'
    sha256='759d0a9831c436a0eb331ad36f236c06cb04aaa0005da46571f0e9d3d9206f6b' }   # also the GitHub release asset digest
$ffmpegVersionPattern = '^ffmpeg version 9\.0\.2-full_build-www\.gyan\.dev '
$emccVersionPattern = '\) 6\.0\.12 \('
# System libraries the web build links (docs/web_build.md section 14: -sFILESYSTEM=0, no pthreads, no ASYNCIFY).
# The SDK ships them prebuilt in emsdk\upstream\emscripten\cache; embuilder checks (and would build) each one, and
# EM_FROZEN_CACHE=1 makes every later build use only those, with no cache write and no network.
$frozenLibs = @('crt1', 'libc', 'libc-debug', 'libc++', 'libc++-debug', 'libc++-noexcept', 'libc++abi', 'libc++abi-debug', 'libc++abi-noexcept',
    'libunwind', 'libclang_rt.builtins', 'libdlmalloc', 'libdlmalloc-debug', 'libGL-getprocaddr', 'libal', 'libhtml5', 'libsockets',
    'libstubs', 'libstubs-debug', 'libnoexit')
# The link options of web_build.md section 14 that emcc 6.0.12 accepts: -sSTACK_FIRST is an internal setting there
# (the SDK links with --no-stack-first already) and -sGLOBAL_BASE takes a decimal number.
$smokeLinkFlags = @('-sMODULARIZE', '-sEXPORT_ES6', '-sENVIRONMENT=worker,node', '-sINITIAL_MEMORY=64MB', '-sALLOW_MEMORY_GROWTH',
    '-sMAXIMUM_MEMORY=2GB', '-sSTACK_SIZE=16MB', '-sGLOBAL_BASE=8388608')
# source, compiler, extra flags; the cases reach the release, debug (-O0), exit-less and Node-filesystem variants of the libraries.
$smokeCases = @(
    @('smoke.c', 'emcc', '-O2', '-sFILESYSTEM=0', '-sEXIT_RUNTIME'),
    @('smoke.c', 'emcc', '-O0', '-sFILESYSTEM=0', '-sEXIT_RUNTIME'),
    @('smoke.c', 'emcc', '-O3', '-g', '-sFILESYSTEM=0'),
    @('smoke.c', 'emcc', '-O2', '-sNODERAWFS', '-sEXIT_RUNTIME'),
    @('smoke.cpp', 'em++', '-O2', '-sFILESYSTEM=0', '-sEXIT_RUNTIME'),
    @('smoke.cpp', 'em++', '-O0', '-fno-exceptions', '-sFILESYSTEM=0')
)

$problems = 0
function Write-Verdict([string]$Status, [string]$Item, [string]$Detail) {
    Write-Output "[$Status] $Item - $Detail"
    if ($Status -in 'MISSING', 'MISMATCH') { $script:problems++ }
}
function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-Inside([string]$Path) {
    $prefix = $Destination.TrimEnd('\') + '\'
    if (![IO.Path]::GetFullPath($Path).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Path outside dependency directory: $Path" }
}

# Environment of an emsdk-installed Emscripten, as emsdk_env would set it; restored after the call.
function Invoke-WithEmscriptenEnvironment([hashtable]$Extra, [scriptblock]$Body) {
    $set = @{
        EM_CONFIG = Join-Path $emsdkDir '.emscripten'
        EMSDK_PYTHON = Join-Path $emsdkDir 'python\3.13.3_64bit\python.exe'
        EMSDK_NODE = Join-Path $emsdkDir 'node\24.19.0_64bit\node.exe'
        EM_FROZEN_CACHE = '1'
        EMSDK = $emsdkDir
        PATH = (Join-Path $emsdkDir 'upstream\emscripten') + ';' + (Join-Path $emsdkDir 'upstream\bin') + ';' + $env:PATH
    }
    foreach ($key in $Extra.Keys) { $set[$key] = $Extra[$key] }
    $saved = @{}
    foreach ($key in $set.Keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key); [Environment]::SetEnvironmentVariable($key, $set[$key]) }
    try { & $Body } finally { foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key]) } }
}

# Ensures the archive is in downloads\ with the pinned hash (downloads it unless -VerifyOnly or $NoDownload); returns its
# path, 'MISMATCH' (-VerifyOnly) or $null when it is absent.
function Confirm-Archive([hashtable]$Archive, [bool]$NoDownload) {
    $path = Join-Path $downloads $Archive.name
    if (!(Test-Path -LiteralPath $path)) {
        if ($VerifyOnly -or $NoDownload) { return $null }
        New-Item -ItemType Directory -Path $downloads -Force | Out-Null
        Write-Host "Downloading $($Archive.name)"
        $part = "$path.part"
        $previous = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try { Invoke-WebRequest -Uri $Archive.url -OutFile $part } finally { $ProgressPreference = $previous }
        if ((Get-Sha256 $part) -ne $Archive.sha256) { Remove-Item -LiteralPath $part; throw "Checksum mismatch after download: $($Archive.name)" }
        Move-Item -LiteralPath $part -Destination $path
    }
    if ((Get-Sha256 $path) -ne $Archive.sha256) {
        if ($VerifyOnly) { return 'MISMATCH' }
        throw "Checksum mismatch: $path"
    }
    return $path
}

function Read-Stamp([string]$Directory) {
    $stamp = Join-Path $Directory 'es2-install.json'
    if (Test-Path -LiteralPath $stamp) { return Get-Content -LiteralPath $stamp -Raw | ConvertFrom-Json }
    return $null
}
function Get-ArchiveRecord([hashtable[]]$List) {
    $record = [ordered]@{}
    foreach ($archive in $List) { $record[$archive.name] = $archive.sha256 }
    return $record
}
function Test-StampMatches($Stamp, [hashtable[]]$List) {
    if (!$Stamp -or !$Stamp.archives) { return $false }
    foreach ($archive in $List) { if ($Stamp.archives.($archive.name) -ne $archive.sha256) { return $false } }
    return @($Stamp.archives.PSObject.Properties).Count -eq $List.Count
}

# Builds, in a scratch directory outside the dependency tree, a C and a C++ program with the web link options
# against the frozen cache and runs the first one under the emsdk Node. Returns '' or the failure.
function Test-FrozenCache {
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ('es2-emcc-smoke-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $scratch | Out-Null
    try {
        [IO.File]::WriteAllText((Join-Path $scratch 'smoke.c'), "#include <stdio.h>`n#include <stdlib.h>`n#include <string.h>`n#include <math.h>`nint main(int c, char** v) { char* p = malloc(64); strcpy(p, `"ok`"); printf(`"%s %f\n`", p, sqrt((double)c)); free(p); return 0; }`n")
        [IO.File]::WriteAllText((Join-Path $scratch 'smoke.cpp'), "#include <string>`n#include <vector>`nint main(int c, char**) { std::vector<std::string> v(c, `"ok`"); return (int)v.size() - c; }`n")
        return Invoke-WithEmscriptenEnvironment @{} {
            $index = 0
            foreach ($case in $smokeCases) {
                $index++
                $tool = Join-Path $emsdkDir "upstream\emscripten\$($case[1]).exe"
                $log = & $tool (Join-Path $scratch $case[0]) -o (Join-Path $scratch "smoke$index.mjs") @($case[2..($case.Count - 1)]) @smokeLinkFlags 2>&1
                if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $scratch "smoke$index.wasm"))) { return "$($case -join ' ') failed: $($log -join ' ')" }
            }
            $node = Join-Path $emsdkDir 'node\24.19.0_64bit\node.exe'
            $module = [Uri]::new((Join-Path $scratch 'smoke1.mjs')).AbsoluteUri
            $out = & $node --input-type=module -e "import m from '$module'; await m();" 2>&1
            if ($LASTEXITCODE -ne 0 -or ($out -join ' ') -notmatch '^ok ') { return "smoke program failed under Node: $($out -join ' ')" }
            return ''
        }
    } finally { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
}

function Install-Emsdk {
    $stamp = Read-Stamp $emsdkDir
    $installed = Test-Path -LiteralPath $emsdkDir
    $paths = @{}
    foreach ($archive in $archives) {
        $paths[$archive.name] = Confirm-Archive $archive $installed
        if ($paths[$archive.name] -eq 'MISMATCH') { Write-Verdict MISMATCH $archive.name 'kept archive does not match the pinned SHA-256' }
        elseif (!$paths[$archive.name] -and $installed) { Write-Verdict WARN $archive.name 'archive not kept, so its hash cannot be rechecked' }
        elseif (!$paths[$archive.name]) { Write-Verdict MISSING $archive.name "not in $downloads (setup-web-dependencies.ps1 downloads $($archive.url))" }
        else { Write-Verdict OK $archive.name "SHA-256 $($archive.sha256)" }
    }
    if ($VerifyOnly) {
        if (!$installed) { Write-Verdict MISSING 'emsdk' "$emsdkDir absent (setup-web-dependencies.ps1 installs it)"; return }
    } elseif ($installed -and !(Test-StampMatches $stamp $archives)) {
        throw "Refusing to overwrite an existing dependency that is not this script's emsdk installation: $emsdkDir"
    } elseif (!$installed) {
        $pythonExe = Join-Path $Destination 'python\python.exe'
        if (!(Test-Path -LiteralPath $pythonExe)) { throw "Pinned Python missing: $pythonExe (setup-dependencies.ps1 -Python)" }
        Assert-Inside $emsdkDir
        $staging = Join-Path $Destination ('extract-web-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        [IO.Compression.ZipFile]::ExtractToDirectory($paths["emsdk-$emsdkTag.zip"], $staging)
        $children = @(Get-ChildItem -LiteralPath $staging -Force)
        if ($children.Count -ne 1 -or !$children[0].PSIsContainer) { throw "Unexpected archive layout: emsdk-$emsdkTag.zip" }
        Move-Item -LiteralPath $children[0].FullName -Destination $emsdkDir
        Remove-Item -LiteralPath $staging
        $keep = Join-Path $emsdkDir 'downloads'
        New-Item -ItemType Directory -Path $keep | Out-Null
        foreach ($archive in $archives) { if ($archive.role -eq 'sdk') { Copy-Item -LiteralPath $paths[$archive.name] -Destination (Join-Path $keep $archive.name) } }
        $env:EMSDK_KEEP_DOWNLOADS = '1'
        try {
            foreach ($command in @(@('install', $emsdkTag), @('activate', $emsdkTag))) {
                & $pythonExe -B -I (Join-Path $emsdkDir 'emsdk.py') @command
                if ($LASTEXITCODE -ne 0) { throw "emsdk $($command -join ' ') failed" }
            }
        } finally { Remove-Item Env:\EMSDK_KEEP_DOWNLOADS -ErrorAction SilentlyContinue }
        # Whatever emsdk holds in downloads\ must be one of the pinned, verified files.
        foreach ($file in Get-ChildItem -LiteralPath $keep -File) {
            $pin = $archives | Where-Object { $_.name -eq $file.Name }
            if (!$pin -or (Get-Sha256 $file.FullName) -ne $pin.sha256) { throw "emsdk fetched or changed an unpinned file: $($file.FullName)" }
        }
        Remove-Item -LiteralPath $keep -Recurse -Force
        $record = [ordered]@{ tag=$emsdkTag; commit=$emsdkCommit; archives=(Get-ArchiveRecord $archives); frozenLibs=@() }
        $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $emsdkDir 'es2-install.json') -Encoding utf8
        Write-Output "Installed emsdk $emsdkTag; SHA-256 verified."
        $stamp = Read-Stamp $emsdkDir
    }
    if (!(Test-StampMatches $stamp $archives)) { Write-Verdict MISMATCH 'emsdk' "$emsdkDir has no es2-install.json matching the pinned archives"; return }
    $version = Invoke-WithEmscriptenEnvironment @{} { (& (Join-Path $emsdkDir 'upstream\emscripten\emcc.exe') --version 2>&1 | Select-Object -First 1) }
    if ($version -match $emccVersionPattern) { Write-Verdict OK 'emsdk' "emcc: $version" }
    else { Write-Verdict MISMATCH 'emsdk' "emcc reports '$version'; docs/dependencies.md pins Emscripten $emsdkTag" }

    # Frozen system libraries.
    $libsBuilt = @($stamp.frozenLibs)
    if (!$VerifyOnly -and $frozenLibs.Count -and ($libsBuilt -join ',') -ne ($frozenLibs -join ',')) {
        Invoke-WithEmscriptenEnvironment @{ EM_FROZEN_CACHE = '0' } {
            & (Join-Path $emsdkDir 'upstream\emscripten\embuilder.exe') build @frozenLibs
            if ($LASTEXITCODE -ne 0) { throw 'embuilder failed' }
        }
        $stamp.frozenLibs = @($frozenLibs)
        $stamp | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $emsdkDir 'es2-install.json') -Encoding utf8
        Write-Output "Built the frozen system libraries: $($frozenLibs -join ' ')"
        $libsBuilt = $frozenLibs
    }
    if (($libsBuilt -join ',') -ne ($frozenLibs -join ',')) { Write-Verdict MISSING 'emsdk cache' "frozen libraries '$($libsBuilt -join ' ')' differ from the pinned list '$($frozenLibs -join ' ')'"; return }
    $failure = Test-FrozenCache
    if ($failure) { Write-Verdict MISMATCH 'emsdk cache' $failure }
    else { Write-Verdict OK 'emsdk cache' "frozen ($($frozenLibs -join ' ')); C and C++ link with the web options offline" }
}

function Install-Ffmpeg {
    $installed = Test-Path -LiteralPath $ffmpegDir
    $path = Confirm-Archive $ffmpegArchive $installed
    if ($path -eq 'MISMATCH') { Write-Verdict MISMATCH $ffmpegArchive.name 'kept archive does not match the pinned SHA-256' }
    elseif (!$path -and $installed) { Write-Verdict WARN $ffmpegArchive.name 'archive not kept, so its hash cannot be rechecked' }
    elseif (!$path) { Write-Verdict MISSING $ffmpegArchive.name "not in $downloads (setup-web-dependencies.ps1 downloads $($ffmpegArchive.url))" }
    else { Write-Verdict OK $ffmpegArchive.name "SHA-256 $($ffmpegArchive.sha256)" }
    $stamp = Read-Stamp $ffmpegDir
    if ($VerifyOnly) {
        if (!$installed) { Write-Verdict MISSING 'ffmpeg' "$ffmpegDir absent (setup-web-dependencies.ps1 installs it)"; return }
    } elseif ($installed -and !(Test-StampMatches $stamp @($ffmpegArchive))) {
        throw "Refusing to overwrite an existing dependency that is not this script's FFmpeg installation: $ffmpegDir"
    } elseif (!$installed) {
        Assert-Inside $ffmpegDir
        $staging = Join-Path $Destination ('extract-web-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        [IO.Compression.ZipFile]::ExtractToDirectory($path, $staging)
        $children = @(Get-ChildItem -LiteralPath $staging -Force)
        if ($children.Count -ne 1 -or !$children[0].PSIsContainer) { throw "Unexpected archive layout: $($ffmpegArchive.name)" }
        Move-Item -LiteralPath $children[0].FullName -Destination $ffmpegDir
        Remove-Item -LiteralPath $staging
        [ordered]@{ archives=(Get-ArchiveRecord @($ffmpegArchive)) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $ffmpegDir 'es2-install.json') -Encoding utf8
        Write-Output 'Installed ffmpeg; SHA-256 verified.'
        $stamp = Read-Stamp $ffmpegDir
    }
    if (!(Test-StampMatches $stamp @($ffmpegArchive))) { Write-Verdict MISMATCH 'ffmpeg' "$ffmpegDir has no es2-install.json matching the pinned archive"; return }
    $exe = Join-Path $ffmpegDir 'bin\ffmpeg.exe'
    $version = (& $exe -hide_banner -version 2>&1 | Select-Object -First 1)
    if ($version -notmatch $ffmpegVersionPattern) { Write-Verdict MISMATCH 'ffmpeg' "ffmpeg reports '$version'; docs/dependencies.md pins 9.0.2"; return }
    $decoders = (& $exe -hide_banner -decoders 2>&1) -join "`n"
    $encoders = (& $exe -hide_banner -encoders 2>&1) -join "`n"
    $missing = @()
    if ($decoders -notmatch '\blibdav1d\b') { $missing += 'libdav1d (decoder)' }
    if ($encoders -notmatch '\blibsvtav1\b' -and $encoders -notmatch '\blibaom-av1\b') { $missing += 'libsvtav1 or libaom-av1 (encoder)' }
    if ($encoders -notmatch '\blibopus\b') { $missing += 'libopus (encoder)' }
    if ($missing) { Write-Verdict MISMATCH 'ffmpeg' "build lacks $($missing -join ', ')" }
    else { Write-Verdict OK 'ffmpeg' "$version libdav1d, libsvtav1/libaom-av1 and libopus present" }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
Install-Emsdk
Install-Ffmpeg
$manifestPath = Join-Path $Destination 'web-manifest.json'
$manifest = [ordered]@{
    emsdk = [ordered]@{ directory='emsdk'; tag=$emsdkTag; commit=$emsdkCommit; frozenLibs=$frozenLibs; archives=@($archives | ForEach-Object { [ordered]@{ archive=$_.name; url=$_.url; sha256=$_.sha256 } }) }
    ffmpeg = [ordered]@{ directory='ffmpeg'; archive=$ffmpegArchive.name; url=$ffmpegArchive.url; sha256=$ffmpegArchive.sha256 }
} | ConvertTo-Json -Depth 5
if (!$VerifyOnly -and !$problems) {
    if (!(Test-Path -LiteralPath $manifestPath) -or ((Get-Content -LiteralPath $manifestPath -Raw).Trim() -ne $manifest.Trim())) {
        $manifest | Set-Content -LiteralPath $manifestPath -Encoding utf8
        Write-Output "Wrote $manifestPath"
    }
}
if ($problems) { Write-Output "$problems item(s) missing or mismatched."; exit 1 }
Write-Output 'All web dependencies verified.'
