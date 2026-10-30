# Builds the web site from the extracted CD (docs/web_build.md section 17): the wasm modules and
# zstd_pack with Bazel, then tools/web/build_site.py with the page's files, the converted movies,
# the CD music and the part logs.
#
#   & tools\web\make-site.ps1                 # build\web\site, the site to deploy
#   & tools\web\make-site.ps1 -Selftest       # build\web\selftest-site with selftest.html
#
# -Selftest adds the self-test page and selftest_suites.json; that site holds game files and
# journals and is never deployed. An earlier site in the output folder is replaced.
param(
    [switch]$Selftest,
    [string]$Out = '',
    [string]$Cd = '',
    [string]$Media = '',
    [string]$CdAudio = '',
    [string]$PartLogs = '',
    [string]$Suites = '',
    [string]$Bazel = '',
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$web = Join-Path $repo 'build\web'
if (!$Out) { $Out = Join-Path $web $(if ($Selftest) { 'selftest-site' } else { 'site' }) }
if (!$Cd) { $Cd = if ($env:ES2_DATA_ROOT) { $env:ES2_DATA_ROOT } else { Join-Path $repo 'build\extracted\cd' } }
if (!$Media) { $Media = Join-Path $web 'media' }
if (!$CdAudio) { $CdAudio = Join-Path $web 'cd' }
if (!$PartLogs) { $PartLogs = Join-Path $web 'part-logs' }
if (!$Suites) { $Suites = Join-Path $web 'selftest_suites.json' }
if (!$Bazel) { $Bazel = if ($env:ES2_BAZEL) { $env:ES2_BAZEL } else { 'bazel' } }
if (!$Python) {
    Import-Module (Join-Path $repo 'tools\reference-game\setup\CloneGuard.psm1')
    $pinned = (Get-Es2Layout).Python
    $Python = if (Test-Path -LiteralPath $pinned) { $pinned } else { 'python' }
}
foreach ($dir in @($Cd, $Media, $CdAudio)) {
    if (!(Test-Path -LiteralPath $dir -PathType Container)) { throw "missing input folder: $dir" }
}
if ($Selftest -and !(Test-Path -LiteralPath $Suites -PathType Leaf)) { throw "missing $Suites (tools/web/selftest_suites.py)" }

# The build outputs under their site names (section 17).
$targets = [ordered]@{
    '//shell:es2shell_wasm' = 'es2shell'
    '//sim:es2sim_window_wasm' = 'es2sim'
    '//launcher:launch_wasm' = 'launch'
    '//web:vfs_wasm' = 'vfs'
    '//web:tools_wasm' = 'tools'
}
Push-Location $repo
try {
    & $Bazel build @($targets.Keys) '//tools/web:zstd_pack'
    if ($LASTEXITCODE) { throw "bazel build failed ($LASTEXITCODE)" }
    $execroot = (& $Bazel info execution_root).Trim()
    if ($LASTEXITCODE) { throw "bazel info failed ($LASTEXITCODE)" }
    $outputs = { param($target) & $Bazel cquery --output=files $target 2>$null | ForEach-Object { Join-Path $execroot $_ } }

    $files = @()
    foreach ($target in $targets.Keys) {
        foreach ($path in & $outputs $target) {
            $files += '--file', "$($targets[$target])$([IO.Path]::GetExtension($path))=$path"
        }
    }
    $page = @('index.html', 'app.js', 'sw.js', 'checks.mjs', 'isolation.mjs', 'texts.mjs', 'game.mjs', 'worker.js',
        'game_worker.mjs', 'launcher.mjs', 'es2shell_imports.mjs', 'es2sim_imports.mjs', 'cdaudio.mjs', 'cd_player.mjs',
        'movie_player.mjs', 'keys.mjs', 'key_layouts.mjs', 'shared.mjs', 'store.mjs', 'vfs.mjs', 'vfs_files.mjs',
        'saves.mjs', 'zip.mjs', 'unzip.mjs', 'volume.mjs')
    if ($Selftest) { $page += 'selftest.html', 'selftest.mjs', 'selftest_run.mjs', 'tick_log_hash.mjs' }
    foreach ($name in $page) { $files += '--file', (Join-Path $repo "web\$name") }
    $files += '--file', "audio.js=$(Join-Path $repo 'web\audio_worklet.mjs')"
    if ($Selftest) { $files += '--file', "selftest_suites.json=$Suites" }

    $logs = @()
    if (Test-Path -LiteralPath $PartLogs) {
        foreach ($log in Get-ChildItem -LiteralPath $PartLogs -Filter '*.json' | Sort-Object Name) { $logs += '--part-log', $log.FullName }
    }
    $zstd = @(& $outputs '//tools/web:zstd_pack')[0]
    $arguments = @('-B', (Join-Path $repo 'tools\web\build_site.py'), '--cd', $Cd, '--out', $Out, '--zstd-pack', $zstd,
        '--clean', '--media', $Media, '--cd-audio', $CdAudio, '--report', "$Out-report.json") + $files + $logs
    if ($Selftest) { $arguments += '--selftest' }
    & $Python @arguments
    if ($LASTEXITCODE) { throw "build_site.py failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
