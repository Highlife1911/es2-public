[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The extracted CD's parent (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'calibration-r111',
    # The primary installation is refused unless the owner asks for it.
    [switch]$AllowPrimary,
    # Lists every step (and what a restore would change) without touching anything.
    [switch]$DryRun,
    # A session directory (logs\own-menu-<stamp>) of an interrupted session: restores its backup and nothing else.
    [string]$RestoreOnly = '',
    [ValidateRange(0, 127)][int]$Seed = 71,
    # The shell the launcher starts: text (es2menu.exe in a console; the default),
    # gui (es2shell.exe, the graphical shell), vshell (the original VSHELL.EXE, for the comparison).
    [ValidateSet('text', 'gui', 'vshell')][string]$Shell = 'text',
    # Number of launcher runs in this session (2: the saved-games route with a restart in between).
    [ValidateRange(1, 5)][int]$Runs = 1,
    # Skip the ES2_CHAIN_VERIFY test after the session.
    [switch]$NoVerify,
    # After the launcher has ended: how long to wait for game processes of the installation to exit before the restore is deferred.
    [ValidateRange(0, 3600)][int]$SettleSeconds = 30,
    [string]$Python = '',
    [string]$BazelOutputBase = '',
    # Fixture overrides (the script test): other launcher script, backend, prebuilt menu and core executables.
    [string]$LauncherScript = '',
    [string]$BackendScript = '',
    [string]$MenuExe = '',
    [string]$CoreExe = '',
    # ---- The unattended own-shell chain regression (launcher.md "Unattended own-shell chain") ----
    # No owner: the menu reads -MenuInput (es2menu --input), DBSIM runs with the oracle settings of -Suite (scripted
    # input, virtual time, hidden window, tick log), and the run is judged against the suite's golden. Measurement
    # clones only (default timing-r111).
    [switch]$Unattended,
    [ValidateSet('c1_01-success')][string]$Suite = 'c1_01-success',
    # The menu script (one answer per line, shared by every menu start of the run). Default: CONTINUE, MISSION
    # (Rock & Roll), after the results 1 HAUPTMENUE, 5 QUIT.
    [string]$MenuInput = '',
    # Passed to configure-tick-log.ps1: the hook selection (name=mode,...; empty: the original DBSIM) and a prepared proxy.
    [string]$Hooks = '',
    [string]$PreparedProxy = '',
    [string]$GoldenRoot = '',
    # The session directory (default DataRoot\logs\own-menu-<stamp>); it must not exist yet (check.ps1 -OwnShell names it).
    [string]$SessionDirectory = '',
    # Unattended: after this long the run's own ES2LAUNCH.EXE and DBSIM are stopped (identity checked) and the run fails.
    [ValidateRange(5, 7200)][int]$TimeoutSeconds = 900,
    # Unattended: DBSIM's audio session volume (session_volume helper, as the oracle runner); 100 leaves it untouched.
    [ValidateRange(0, 100)][int]$VolumePercent = 0,
    # Fixture overrides (the script test): configure-tick-log.ps1 and the volume helper.
    [string]$ConfigureScript = '',
    [string]$VolumeTool = '',
    # ---- DBSIM's full screen in interactive sessions (launcher.md "DBSIM's full screen") ----
    # Writes [presentation] directdraw=windowed and presenter=gdi into the clone's es2-oracle.ini for the session
    # and puts the file back afterwards (with DATA and SAV; -RestoreOnly too). Only when the clone's proxy has these options.
    [switch]$WindowedDirectDraw,
    # ---- The own DBSIM process in the unattended chain (setup.md "the own DBSIM process in an installation") ----
    # own: instead of configure-tick-log.ps1, configure-sim.ps1 deploys es2sim (//sim:es2sim_window, --headless) with
    # the suite's oracle settings as its es2sim.ini and the tick log; ES2LAUNCH.EXE then starts es2sim in place of
    # DBSIM.EXE (es2launch.ini [sim] command). The restore removes it again (configure-sim.ps1 -Remove).
    [ValidateSet('dbsim', 'own')][string]$Sim = 'dbsim',
    # Fixture overrides (the script test): configure-sim.ps1 and prebuilt es2sim.exe / SDL3.dll (default: built by it).
    [string]$ConfigureSimScript = '',
    [string]$SimExe = '',
    [string]$SimSdl = '',
    # The CD root the own executables read (menu.ini cd_root), e.g. a copy without VER95\DBSIM.EXE and
    # VER95\VSHELL.EXE; default ExtractedRoot\cd. The verification against the original keeps ES2_DATA_ROOT.
    [string]$CdRoot = ''
)
if (!$DataRoot -or !$ExtractedRoot -or !$Python -or !$BazelOutputBase) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
    if (!$Python) { $Python = $es2.Python }
    if (!$BazelOutputBase) { $BazelOutputBase = (Join-Path $es2.BazelOutputRoot 'es2-shell') }
}
# One-command owner acceptance of the own text menu. Backs up the clone's DATA and SAV, builds and
# configures the menu, starts it through our launcher (start-own-launcher.ps1; the menu console and the game
# windows are visible by design), waits for the session to end, collects the evidence, puts DATA and SAV back
# (always, in a finally), verifies the own-menu chain with the original VSHELL harness and prints the compare
# checklist. Contract: docs/reverse-engineering/launcher.md, "Owner acceptance: one command".
# Never starts or touches anything when -DryRun is given. Never stops a game process, except with -Unattended after
# -TimeoutSeconds: then only this run's own ES2LAUNCH.EXE and game processes (path in the clone, started after the run
# began, identity rechecked right before the stop). -Unattended: see the parameter block and launcher.md.
# -Shell gui starts the graphical shell es2shell.exe (no console; its shell.log in the menu directory) with
# the same menu directory; -Shell vshell the original VSHELL.EXE through the same launcher, backup and restore
# (the owner's comparison). Contract: launcher.md, "Graphical shell".
$ErrorActionPreference = 'Stop'
if ($Unattended -and !$PSBoundParameters.ContainsKey('InstallName')) { $InstallName = 'timing-r111' }
if ($Unattended -and $Runs -ne 1) { throw '-Unattended runs one launcher run (-Runs 1).' }
if ($Unattended -and $Shell -eq 'vshell') { throw '-Unattended drives the own shell (-Shell text or gui), not the original VSHELL.' }
if ($Unattended -and $Shell -eq 'gui' -and $PSBoundParameters.ContainsKey('MenuInput')) { throw '-MenuInput is the text menu''s script; -Shell gui runs the fixed headless scripts of es2shell.' }
if ($Unattended -and $WindowedDirectDraw) { throw '-WindowedDirectDraw belongs to interactive sessions; -Unattended uses the oracle settings of its suite.' }
if (!$Unattended) {
    foreach ($name in @('MenuInput', 'Hooks', 'PreparedProxy', 'GoldenRoot', 'TimeoutSeconds', 'VolumePercent', 'ConfigureScript', 'VolumeTool', 'Suite',
                        'Sim', 'ConfigureSimScript', 'SimExe', 'SimSdl')) {
        if ($PSBoundParameters.ContainsKey($name)) { throw "-$name belongs to the unattended run: add -Unattended." }
    }
}
if ($Sim -eq 'own' -and ($Hooks -or $PreparedProxy)) { throw '-Hooks and -PreparedProxy configure the proxy of DBSIM.EXE; -Sim own runs es2sim instead.' }
if ($Sim -ne 'own') {
    foreach ($name in @('ConfigureSimScript', 'SimExe', 'SimSdl')) {
        if ($PSBoundParameters.ContainsKey($name)) { throw "-$name belongs to the own DBSIM process: add -Sim own." }
    }
}
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
$utf8 = New-Object System.Text.UTF8Encoding($false)
$folders = @('DATA', 'SAV')
$primaryName = 'native-r111'

function Get-FullDirectory([string]$Path) { return [IO.Path]::GetFullPath($Path).TrimEnd([char]92, [char]47) }
function Format-Failure($Record) { return "$($Record.Exception.Message) (line $($Record.InvocationInfo.ScriptLineNumber))" }
function Say([string]$Text) { Write-Host "[own-menu] $Text" }
function Write-TextFile([string]$Path, [string]$Text) { [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), $Text, $utf8) }
function Write-JsonFile([string]$Path, $Object) { Write-TextFile $Path (($Object | ConvertTo-Json -Depth 8) + "`n") }
function Invoke-Native([string]$Program, [string[]]$Arguments) {
    # Runs a program and returns {Code, Text} (stdout and stderr together); stderr text never raises here.
    $previous = $ErrorActionPreference
    $pathExt = $env:PATHEXT
    $ErrorActionPreference = 'Continue'
    # Without .EXE in PATHEXT (a Bazel test environment) PowerShell takes a .exe for a document and refuses it in a pipeline.
    if (!$pathExt -or $pathExt -notmatch '(?i)\.EXE') { $env:PATHEXT = '.COM;.EXE;.BAT;.CMD' }
    try {
        $text = (& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
        return [pscustomobject]@{Code = $LASTEXITCODE; Text = $text}
    } finally { $ErrorActionPreference = $previous; $env:PATHEXT = $pathExt }
}
function New-IgnoredDirectory([string]$Path) {
    # A directory that holds original game state: a .gitignore of its own keeps it out of any repository above.
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    Write-TextFile (Join-Path $Path '.gitignore') "*`n"
}

# ---- trees: manifests, digests, comparison, synchronisation ----------------------------------------
function Get-TreeManifest([string]$Root) {
    # Files (relative path -> sha256, size) and directories below Root; linked paths are refused.
    $files = @{}; $dirs = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $Root -PathType Container) {
        $prefix = $Root.TrimEnd('\') + '\'
        foreach ($item in (Get-ChildItem -LiteralPath $Root -Recurse -Force)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $($item.FullName)" }
            $relative = $item.FullName.Substring($prefix.Length)
            if ($item.PSIsContainer) { $dirs.Add($relative) }
            else { $files[$relative] = [pscustomobject]@{Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash; Size = [int64]$item.Length} }
        }
    }
    return [pscustomobject]@{Files = $files; Dirs = @($dirs)}
}
function Get-ManifestDigest($Manifest) {
    $lines = foreach ($key in ($Manifest.Files.Keys | Sort-Object { $_.ToLowerInvariant() })) { $key.ToLowerInvariant() + '|' + $Manifest.Files[$key].Sha256 }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes((@($lines) -join "`n"))))).Replace('-', '') }
    finally { $sha.Dispose() }
}
function ConvertTo-ManifestRecord($Manifest) {
    return [ordered]@{
        digest = (Get-ManifestDigest $Manifest)
        files = @($Manifest.Files.Keys | Sort-Object { $_.ToLowerInvariant() } | ForEach-Object { [ordered]@{path = $_; sha256 = $Manifest.Files[$_].Sha256; size = $Manifest.Files[$_].Size} })
        dirs = @($Manifest.Dirs | Sort-Object { $_.ToLowerInvariant() })
    }
}
function ConvertFrom-ManifestRecord($Record) {
    $files = @{}
    foreach ($f in @($Record.files)) { $files[[string]$f.path] = [pscustomobject]@{Sha256 = [string]$f.sha256; Size = [int64]$f.size} }
    return [pscustomobject]@{Files = $files; Dirs = @($Record.dirs | Where-Object { $_ })}
}
function Compare-Manifests($Before, $After) {
    # Names only: what a session added, removed or changed.
    $added = @($After.Files.Keys | Where-Object { !$Before.Files.ContainsKey($_) } | Sort-Object)
    $removed = @($Before.Files.Keys | Where-Object { !$After.Files.ContainsKey($_) } | Sort-Object)
    $changed = @($Before.Files.Keys | Where-Object { $After.Files.ContainsKey($_) -and $After.Files[$_].Sha256 -ne $Before.Files[$_].Sha256 } | Sort-Object)
    return [pscustomobject]@{Added = $added; Removed = $removed; Changed = $changed}
}
function Format-ManifestLines([string]$Folder, $Manifest) {
    foreach ($key in ($Manifest.Files.Keys | Sort-Object { $_.ToLowerInvariant() })) {
        '{0}  {1,9}  {2}\{3}' -f $Manifest.Files[$key].Sha256, $Manifest.Files[$key].Size, $Folder, $key
    }
}
function Get-InstallationManifests([string]$Installation) {
    $result = [ordered]@{}
    foreach ($folder in $folders) { $result[$folder] = Get-TreeManifest (Join-Path $Installation $folder) }
    return $result
}
function Sync-Tree([string]$BackupRoot, [string]$TargetRoot, $Want, [bool]$Apply) {
    # Makes TargetRoot equal to the manifest Want, taking the bytes from BackupRoot. Returns the actions.
    $actions = New-Object System.Collections.Generic.List[string]
    $have = Get-TreeManifest $TargetRoot
    foreach ($relative in ($have.Files.Keys | Sort-Object)) {
        if (!$Want.Files.ContainsKey($relative)) {
            $actions.Add("remove file $relative")
            if ($Apply) { Remove-Item -LiteralPath (Join-Path $TargetRoot $relative) -Force }
        }
    }
    foreach ($relative in ($Want.Files.Keys | Sort-Object)) {
        $present = $have.Files.ContainsKey($relative)
        if ($present -and $have.Files[$relative].Sha256 -eq $Want.Files[$relative].Sha256) { continue }
        $actions.Add($(if ($present) { "overwrite $relative" } else { "recreate $relative" }))
        if ($Apply) {
            $destination = Join-Path $TargetRoot $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath (Join-Path $BackupRoot $relative) -Destination $destination -Force
        }
    }
    $wanted = @{}; foreach ($d in $Want.Dirs) { $wanted[$d] = $true }
    foreach ($d in ($have.Dirs | Sort-Object Length -Descending)) {
        if (!$wanted.ContainsKey($d)) {
            $actions.Add("remove directory $d")
            if ($Apply -and (Test-Path -LiteralPath (Join-Path $TargetRoot $d))) { Remove-Item -LiteralPath (Join-Path $TargetRoot $d) -Recurse -Force }
        }
    }
    foreach ($d in $Want.Dirs) {
        if (!(Test-Path -LiteralPath (Join-Path $TargetRoot $d) -PathType Container)) {
            $actions.Add("recreate directory $d")
            if ($Apply) { New-Item -ItemType Directory -Force -Path (Join-Path $TargetRoot $d) | Out-Null }
        }
    }
    return @($actions)
}

# ---- processes ---------------------------------------------------------------------------------------
function Get-InstallationProcesses([string]$Installation) {
    # ES.EXE, VSHELL.EXE, DBSIM.EXE (CloneGuard) and our ES2LAUNCH.EXE that run from the installation directory, and
    # es2sim.exe from its ES2SIM directory.
    $list = @(Get-CloneGameProcess -Installation $Installation)
    $directory = Get-FullDirectory $Installation
    $own = @{'ES2LAUNCH.EXE' = $directory; 'ES2SIM.EXE' = (Join-Path $directory 'ES2SIM')}
    foreach ($p in @(Get-CimInstance Win32_Process -Filter "Name='ES2LAUNCH.EXE' OR Name='es2sim.exe'")) {
        $where = $own[([string]$p.Name).ToUpperInvariant()]
        if (!$p.ExecutablePath -or !$where -or ![string]::Equals([IO.Path]::GetDirectoryName($p.ExecutablePath), $where, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $start = $null
        try { $live = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue; if ($live) { $start = $live.StartTime.ToUniversalTime() } } catch { $start = $null }
        $list += [pscustomobject]@{ProcessId = [int]$p.ProcessId; Name = [string]$p.Name; ExecutablePath = [string]$p.ExecutablePath; StartTimeUtc = $start}
    }
    return @($list)
}
function Assert-InstallationIdle([string]$Installation, [string]$Action) {
    $busy = @(Get-InstallationProcesses $Installation)
    if ($busy.Count) {
        throw ("Close the game in this clone before ${Action}: " + (($busy | ForEach-Object { Format-CloneGameProcess $_ }) -join '; ') +
            '. This script did not start it and leaves it alone; nothing was backed up, started or restored.')
    }
}

# ---- the installation ---------------------------------------------------------------------------------
function Resolve-Installation([string]$Name) {
    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
    $root = Get-CloneInstallRoot $DataRoot
    $path = [IO.Path]::GetFullPath((Join-Path $root $Name))
    if (!$path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "InstallName must stay within $root." }
    return $path
}
function Assert-PrimaryRule([string]$Name) {
    if ($Name -ieq $primaryName) {
        if (!$AllowPrimary) {
            throw ("$primaryName is the primary installation; this script works on a clone such as calibration-r111. " +
                'It starts on the primary installation only with an explicit -AllowPrimary, which only the owner may give. Nothing was touched.')
        }
        Say "-AllowPrimary was given: working on the primary installation $primaryName because the owner asked for it."
        Say "Note: the backend (es2chain.checked_game_dir), start-own-launcher.ps1 and es2menu.exe refuse $primaryName themselves; this switch lifts only this script's own refusal."
    }
}
function Assert-GameInstallation([string]$Installation) {
    if (!(Test-Path -LiteralPath $Installation -PathType Container)) { throw "No such installation: $Installation" }
    foreach ($folder in $folders) {
        if (!(Test-Path -LiteralPath (Join-Path $Installation $folder) -PathType Container)) { throw "$Installation has no $folder directory; not a game installation." }
    }
    foreach ($path in @($Installation) + @($folders | ForEach-Object { Join-Path $Installation $_ })) {
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $path" }
    }
}

# ---- restore --------------------------------------------------------------------------------------------
function Read-RestoreRecord([string]$SessionDirectory) {
    $path = Join-Path $SessionDirectory 'restore.json'
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "No restore.json in $SessionDirectory; give the session directory logs\own-menu-<stamp>." }
    $record = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($record.schema -ne 1) { throw "Unknown restore.json schema: $($record.schema)" }
    return $record
}
function Remove-OwnSim($Record, [bool]$Apply, $Lines, [string]$Token) {
    # configure-sim.ps1 -Remove (ES2SIM\, es2launch.ini, own-sim-manifest.json) under this script's clone lock;
    # the dry run shows its preview. Returns whether the installation has no own simulator any more (or would have).
    $script = [string]$Record.own_sim.configure_script
    $name = Split-Path -Leaf ([string]$Record.installation)
    $splat = @{DataRoot = $DataRoot; InstallName = $name}
    if ($Record.primary) { $splat.AllowPrimary = $true }
    if ($Apply) { $splat.Remove = $true }
    $saved = $env:ES2_CLONE_LOCK_TOKEN
    try {
        if ($Token) { $env:ES2_CLONE_LOCK_TOKEN = $Token }
        $text = (& $script @splat *>&1 | ForEach-Object { [string]$_ }) -join "`n"
        $result = $text.Substring($text.IndexOf('{')) | ConvertFrom-Json
        if ($Apply) {
            $Lines.Add("own simulator: configure-sim.ps1 -Remove: removed $($result.removed), state $($result.state)")
            return $result.state -eq 'none'
        }
        $Lines.Add("own simulator: state $($result.state) (would run configure-sim.ps1 -Remove)")
        return $true
    } catch {
        $Lines.Add("own simulator: configure-sim.ps1 failed: $(Format-Failure $_)")
        return $false
    } finally { $env:ES2_CLONE_LOCK_TOKEN = $saved }
}
function Invoke-Restore([string]$SessionDirectory, $Record, [bool]$Apply, [string]$Token = '') {
    # Verifies the backup against its recorded manifest, then makes DATA and SAV equal to it. Returns {ok, lines}.
    # With own_sim in the record, the own simulator is removed too (Token: the clone lock this script holds).
    $installation = [string]$Record.installation
    $lines = New-Object System.Collections.Generic.List[string]
    $ok = $true
    foreach ($folder in $folders) {
        $want = ConvertFrom-ManifestRecord $Record.manifest.$folder
        $backup = Get-TreeManifest (Join-Path $SessionDirectory "backup\$folder")
        if ((Get-ManifestDigest $backup) -ne $Record.manifest.$folder.digest -or $backup.Files.Count -ne $want.Files.Count) {
            throw "The backup of $folder in $SessionDirectory does not match restore.json; nothing was restored."
        }
    }
    foreach ($folder in $folders) {
        $want = ConvertFrom-ManifestRecord $Record.manifest.$folder
        $actions = Sync-Tree (Join-Path $SessionDirectory "backup\$folder") (Join-Path $installation $folder) $want $Apply
        $verb = if ($Apply) { 'restore' } else { 'would restore' }
        $lines.Add("${folder}: $($actions.Count) action(s) ($verb)")
        foreach ($a in $actions) { $lines.Add("  $a") }
        if ($Apply) {
            $now = Get-TreeManifest (Join-Path $installation $folder)
            $digest = Get-ManifestDigest $now
            $same = $digest -eq $Record.manifest.$folder.digest -and $now.Files.Count -eq $want.Files.Count
            if (!$same) { $ok = $false }
            $lines.Add(("{0}: {1} files, tree {2} {3} before-session tree {4}" -f $folder, $now.Files.Count, $digest, $(if ($same) { '==' } else { '!=' }), $Record.manifest.$folder.digest))
        }
    }
    if ($Record.oracle_ini) { if (!(Restore-OracleIni $SessionDirectory $Record $Apply $lines)) { $ok = $false } }
    if ($Record.PSObject.Properties['own_sim'] -and $Record.own_sim) { if (!(Remove-OwnSim $Record $Apply $lines $Token)) { $ok = $false } }
    return [pscustomobject]@{Ok = $ok; Lines = @($lines)}
}

function Invoke-RestoreOnly {
    $session = [IO.Path]::GetFullPath($RestoreOnly).TrimEnd('\')
    if ((Split-Path -Leaf $session) -ieq 'backup') { $session = Split-Path -Parent $session }
    $record = Read-RestoreRecord $session
    $installation = Resolve-Installation (Split-Path -Leaf ([string]$record.installation))
    if (![string]::Equals((Get-FullDirectory $installation), (Get-FullDirectory ([string]$record.installation)), [StringComparison]::OrdinalIgnoreCase)) {
        throw "restore.json names $($record.installation), which is not below $(Get-CloneInstallRoot $DataRoot); nothing was restored."
    }
    Assert-PrimaryRule (Split-Path -Leaf $installation)
    Assert-GameInstallation $installation
    Say "Restore only: $($record.installation) from $session (session of $($record.started_utc))"
    if ($DryRun) {
        Say 'DRY RUN: nothing is touched.'
        Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName (Split-Path -Leaf $installation)
        $busy = @(Get-InstallationProcesses $installation)
        if ($busy.Count) { Say ('A restore would be refused: ' + (($busy | ForEach-Object { Format-CloneGameProcess $_ }) -join '; ')) }
        $report = Invoke-Restore $session $record $false
        $report.Lines | ForEach-Object { Say $_ }
        return 0
    }
    $lock = Enter-CloneLock -DataRoot $DataRoot -InstallName (Split-Path -Leaf $installation) -Purpose 'own menu restore'
    try {
        Assert-InstallationIdle $installation 'restoring'
        $report = Invoke-Restore $session $record $true $lock.Token
        $report.Lines | ForEach-Object { Say $_ }
        Write-TextFile (Join-Path $session ("evidence\restore-only-" + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.txt')) (($report.Lines -join "`n") + "`n")
        if ($report.Ok) { Say 'RESTORED: DATA and SAV equal the state before the session.'; return 0 }
        Say 'RESTORE INCOMPLETE: the trees differ from the backup (see above).'
        return 3
    } finally { Exit-CloneLock $lock }
}

# ---- text: play guide and checklist ----------------------------------------------------------------------
function Get-PlayGuide {
    if ($Shell -eq 'gui' -or $Shell -eq 'vshell') {
        $which = if ($Shell -eq 'gui') { 'the own graphical shell es2shell.exe' } else { 'the ORIGINAL VSHELL.EXE (the comparison run)' }
@"
What to do ($which; German labels; the game windows are the real mission). Run the same routes with
-Shell gui and -Shell vshell on the same clone (this script restores DATA and SAV after each session) and compare:
    pwsh -File tools\reference-game\setup\start-own-menu.ps1 -InstallName rebuild-r111 -Shell gui
    pwsh -File tools\reference-game\setup\start-own-menu.ps1 -InstallName rebuild-r111 -Shell vshell
  (rebuild-r111: a proxy with presenter=gdi and directdraw=windowed, so DBSIM survives the shell's Full Screen.)

  Route A (one run)
    SPIEL FORTSETZEN                    -> the service bay with the campaign's C1_01
    WAFFEN: Befestigungen < > selects a slot, a row of the Waffenarsenal list mounts it (e.g. Laser 300 GW into slot 1)
    MISSION                             -> first visit: page 0 and its movies (c1, the theatre); MISSION again: page 1
    ROCK & ROLL                         -> DBSIM starts C1_01: check the Herc's weapons in the cockpit, play it to its end
    mission report (page 4), then       -> HAUPTMENUE, then AUFHOEREN
  Route B (start the script with -Runs 2)
    run 1: NEUES SPIEL (name, SKILL LEVEL, ACCEPT); in the bay SPEICHERN, row 3, SPEICHERN, type TEST 1, ACCEPT,
           AUSGANG; HAUPTMENUE; AUFHOEREN.
    run 2: SPEICHERN/LADEN, row 3, LADEN; in the bay MISSION (twice), ROCK & ROLL, play it to its end; HAUPTMENUE; AUFHOEREN.
  Screen checklist (each against the original): main menu (the background animation, the ten buttons, CREDITS,
  ONLINE HANDBUCH), Registration, Saved games (rows, statistics panel, naming), Preferences (radios, check boxes,
  Window/Full Screen with the Alert box, ACCEPT/CANCEL), Practice (eight rows, five parameters, BEGIN MISSION),
  the service bay bar and the squad panel, WAFFEN, REPARATUR, BAUEN, ARSENAL, BESATZUNG, the mission screen pages
  0, 1 (with the map's fly-over and its six arrows) and 4, the boxes (Quit box with Alt+F4, the launch warning with an unarmed Herc, End of game, Replay), the
  movies (intro, group, theatre, debrief) and the sounds (button clicks, bay bar, music ramps in Preferences).
  Known gaps of the own shell (launcher.md, "Graphical shell"): the squad panel's Herc diagram and hot spots,
  the Herc condition word in the squad panel (100), the DEBUG dialog of -@.
  Say what looks or behaves differently. To stop early: end ES2LAUNCH.EXE first, then DBSIM.
"@
        return
    }
@"
What to do in the menu (console window, German labels; the game windows are the real mission):

  Route A (default, one run; main menu and service bay)
    2 SPIEL FORTSETZEN                  -> the service bay (WARTUNGSHANGAR) with the campaign's C1_01
    3 WAFFEN                            -> 2 weapon, slot 1, row 13 (Laser 300 GW); 2 weapon, slot 3, row 16 (Raketenwerfer 6X); 4 back
    4 MISSION (Rock & Roll)             -> DBSIM starts C1_01: check the Herc's weapons in the cockpit, play it to its end
    mission report, then the bay again  -> 1 HAUPTMENUE, then 5 to quit
  Route B (saved games and new career; start the script with -Runs 2)
    run 1: 1 NEUES SPIEL (name, skill 1..4, 1 ACCEPT); in the bay 2 SPEICHERN, 1 SPEICHERN, slot 3, name TEST 1, 1 ACCEPT,
           3 AUSGANG; 1 HAUPTMENUE; 5 quit.   (the slot list shows ' 3. LEER' before and ' 3. TEST 1' after)
    run 2: 6 SPEICHERN/LADEN, 2 LADEN, slot 3; in the bay 4 MISSION (Rock & Roll), play it to its end; 1 HAUPTMENUE; 5.
  Say what was odd: wrong text, a screen that did not fit, anything of the original VSHELL appearing, the mission playing differently.
  To stop early: end ES2LAUNCH.EXE first, then DBSIM (this script never stops a game process).
"@
}
function Get-Checklist([string]$Evidence, [string]$Session) {
    if ($Shell -ne 'text') {
@"
COMPARE CHECKLIST, -Shell $Shell (docs/reverse-engineering/launcher.md, "Graphical shell")
Session directory: $Session
Evidence directory: $Evidence

1. Launcher log (evidence\launcher-run*.log, summary.txt): after a mission 'exit shell-replacement 2' (gui) or the
   VSHELL lines (vshell), 'start dbsim.EXE dummy -eggplant -X2 -R0 -G', the -X3 start of the shell, 'end 0'.
2. gui: chain.json / status.json as for the text menu (the WEAPONS and editor screens change the state directly:
   the campaign log has no 'bay' steps; the launch files show the changes); private\shell.log: the flow events,
   the movies played and the slot saves.
3. Every screen of the play guide's checklist against the original (-Shell vshell on the same clone): layout,
   texts, colours, the buttons' behaviour, the movies and the sounds. Note what differs.
4. restore-report.txt and the printed tree hashes: DATA and SAV equal the state before the session.
5. Your verdict: the graphical shell can replace VSHELL and become the default, or what must change.
"@
        return
    }
@"
COMPARE CHECKLIST (docs/reverse-engineering/launcher.md, "Owner acceptance: one command")
Session directory: $Session
Evidence directory: $Evidence

1. Launcher log (evidence\launcher-run*.log, summarised in summary.txt): after a mission the lines are
   'start shell-replacement ... -X1 -G', 'exit shell-replacement 2', 'start dbsim.EXE dummy -eggplant -X2 -R0 -G',
   'exit dbsim.EXE 3' (1 after a scripted replay), 'start shell-replacement ... -X3 -G', 'exit shell-replacement 0',
   'end 0'. A run without a mission: '-X1', 'exit shell-replacement 0', 'end 0'.
2. chain.json / status.json (evidence): missions[0] is C1_01. With CONTINUE on the clone's GAME_R.SAV and the seed
   in session.json (71), its files are the pinned hashes 61efab9b..., 4c359b61..., 20c8eb56..., f669d125...;
   advance and missions[1] as the played outcome gives (after a success like the golden: advance 2, C1_02).
   When the clone's slot 10 holds its copies (a played installation), missions[0] is a from_slot record instead:
   not prepared (no input_rng/output_rng), its launch_files carry the pinned hashes, results_rng is set.
   Service bay: the 'bay' steps are each 'applied' 1; launch_files.player.mec differs from files.player.mec
   (mec-hashes.txt) when weapons were changed, the other three equal; results_save = launch_save.
   Route B: one campaign (campaign-000, no campaign-001: RESTORE of the own slot reuses it); mission 0 C1_01 launched
   and advanced, mission 1 prepared; 'save' ends in '-prepared.sav'.
3. chain-verify.log: the 'verify' line has results_steps 1, preparations_equal 2 and, with the bay, a 'bay' entry
   with steps and original_runs (a from_slot first mission: preparations 1 and a slot_launch entry), no FAIL (the results step, both preparations and the bay equal the original VSHELL).
4. private\menu.log (game text: it stays out of git): the German labels and the debrief text; Route B: ' 3. LEER'
   before and ' 3. TEST 1' after the save, '[saved slot 10: SAV\GAME_R.SAV]' after LADEN. sav-compare.txt: the
   clone's GAME_R.SAV after the session against the campaign's last SAV (equal when the new image is not shorter
   than the old file, as after C1_01).
5. restore-report.txt and the printed tree hashes: DATA and SAV equal the state before the session.
6. Your verdict: the menu was usable, nothing of VSHELL appeared, the mission played as usual (the cockpit showed
   the changed weapons).
Optional (not done here): the original VSHELL on the clone with the same choices (launcher.md, "Optional, not done by the script").
"@
}
function Get-LauncherEvents([string]$File) {
    # The start/exit/end lines of a launcher log; a shell start is reduced to its -X state.
    $events = New-Object System.Collections.Generic.List[string]
    foreach ($line in (Get-Content -LiteralPath $File)) {
        if ($line -match '^\S+\s+(start|exit|end)\b\s*(.*)$') {
            $kind = $Matches[1]; $rest = $Matches[2]
            # The state is the argument after the VSHELL argv's "dummy -eggplant" (a path in the prefix may hold "-x1").
            if ($kind -eq 'start' -and $rest -cmatch '^shell-replacement\b.*\sdummy -eggplant (-X\d)\b') { $events.Add("start shell-replacement $($Matches[1])") }
            # An es2sim start is reduced to the DBSIM argv es2launch appended (the prefix holds paths).
            elseif ($kind -eq 'start' -and $rest -cmatch '^sim-replacement\b.*\s(--arguments "[^"]*")$') { $events.Add("start sim-replacement $($Matches[1])") }
            elseif ($kind -eq 'start') { $events.Add("start $rest") }
            else { $events.Add("$kind $rest".Trim()) }
        }
    }
    return @($events)
}
function Get-LauncherSummary([string[]]$LogFiles) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($file in $LogFiles) {
        $out.Add("launcher log $(Split-Path -Leaf $file):")
        $events = @(Get-LauncherEvents $file)
        foreach ($e in $events) { $out.Add("  $e") }
        $dbsim = @($events | Where-Object { $_ -like 'start dbsim.EXE*' }).Count
        $own = @($events | Where-Object { $_ -like 'start sim-replacement*' }).Count
        $ends = $events.Count -gt 0 -and $events[$events.Count - 1] -eq 'end 0'
        $out.Add("  -> $dbsim DBSIM start(s)" + $(if ($own) { ", $own es2sim start(s)" } else { '' }) + '; ' + $(if ($ends) { 'ends with "end 0" as expected' } else { 'does NOT end with "end 0": look at the log' }))
    }
    return @($out)
}

# ---- the unattended run ------------------------------------------------------------------------------------
# The launcher events of one mission through the own shell (measured live; DBSIM's 3 = the regular mission end).
$unattendedEvents = @('start shell-replacement -X1', 'exit shell-replacement 2', 'start dbsim.EXE dummy -eggplant -X2 -R0 -G',
    'exit dbsim.EXE 3', 'start shell-replacement -X3', 'exit shell-replacement 0', 'end 0')
# -Sim own: es2launch starts es2sim with DBSIM's argv after --arguments (without "dummy"), and es2sim's exit
# code is WinMain's (3 = the regular mission end) as DBSIM's.
$unattendedEventsOwnSim = @('start shell-replacement -X1', 'exit shell-replacement 2', 'start sim-replacement --arguments "-eggplant -X2 -R0 -G"',
    'exit sim-replacement 3', 'start shell-replacement -X3', 'exit shell-replacement 0', 'end 0')
function Get-OwnSimConfig($SuiteInfo) {
    # es2sim's configuration of the suite, as sim/tests/replay_suite.py writes it for the replay of a suite
    # (UTF-16 LE with a byte order mark): the proxy's [trace] watches, [time], [input], [arming] of
    # configure-tick-log.ps1 and the host's [run] (a plain mission suite: no [game], no frame_limit, outcome 1).
    $watches = @(Get-Content -LiteralPath $SuiteInfo.Watches | ForEach-Object { $_.Trim() } | Where-Object { $_ -and !$_.StartsWith('#') })
    $lines = @('[trace]', 'tick_log_max=0', 'tick_log_actor_raw=0', "watch_count=$($watches.Count)")
    for ($i = 0; $i -lt $watches.Count; $i++) { $lines += "watch$($i + 1)=$($watches[$i])" }
    $lines += @('[time]', 'virtual_time_step_ms=47', 'virtual_time_start_ms=16777216', 'virtual_time_pace=0', 'virtual_time_intra_frame=1')
    $lines += @('[input]', 'mode=replay', "journal=$($SuiteInfo.Script)", '[arming]', 'early=1', '[run]', "ticks=$($SuiteInfo.Ticks)", 'outcome=1')
    return ($lines -join "`r`n") + "`r`n"
}
# The files the golden's DBSIM ran (reference_pins.py: the clone's C1_01, prepared from GAME_R.SAV with 71 draws).
$unattendedPins = @{
    'c1_01-success' = [ordered]@{
        'script.dat' = '61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb'
        'MISSION.STR' = '4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9'
        'mission.var' = '20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc'
        'player.mec' = 'f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984'
    }
}
$unattendedSeed = @{'c1_01-success' = 71}
# es2shell's headless scripts of -Unattended -Shell gui (launcher.md "Graphical shell"; the controls of
# gui_route_real_test): -X1 CONTINUE GAME, MISSION twice (page 1), Rock & Roll; -X3 (the bay with the mission screen's
# page 4) MAIN MENU, QUIT. The graphical equivalent of the text menu's 2, 4 | 1, 5.
$guiScriptX1 = "step 200`nexpect screen=0`nclick main_menu.09`nstep 5`nexpect screen=5 game=1`nclick service_bay.19`nstep 40`nclick service_bay.19`nstep 40`nexpect page=1`nclick mission.27`nstep 5`n"
$guiScriptX3 = "step 20`nexpect screen=5 page=4`nclick service_bay.05`nstep 5`nexpect screen=0`nclick main_menu.13`nstep 5`n"
$guiReportExpect = [ordered]@{x1 = 2; x3 = 0}
function Get-UnattendedSuite([string]$Name) {
    # The registry entry of a plain mission suite (no launch set, no extra arguments, no tape, no frame window).
    $toolRoot = Join-Path $repository 'tools\reference-game'
    $entry = @((Get-Content -LiteralPath (Join-Path $toolRoot 'oracle-suites.json') -Raw | ConvertFrom-Json).suites | Where-Object { $_.name -ceq $Name })
    if ($entry.Count -ne 1) { throw "Suite $Name is not in oracle-suites.json once." }
    $entry = $entry[0]
    foreach ($field in @('launch', 'game_arguments', 'demo_tape', 'frame_limit')) {
        if ($entry.PSObject.Properties[$field] -and $null -ne $entry.$field) { throw "Suite $Name has '$field'; the unattended chain runs plain mission suites only." }
    }
    if (!$entry.outcome) { throw "Suite $Name does not end by itself (outcome false); the chain needs DBSIM's own end." }
    $script = Join-Path $toolRoot ([string]$entry.script)
    $watches = Join-Path $toolRoot ([string]$entry.watches)
    foreach ($p in @($script, $watches)) { if (!(Test-Path -LiteralPath $p -PathType Leaf)) { throw "Suite file missing: $p" } }
    return [pscustomobject]@{Name = $Name; Script = $script; Watches = $watches; Ticks = [int]$entry.ticks}
}
function Assert-MeasurementClone([string]$Installation) {
    # configure-tick-log.ps1 deploys only to a separate measurement clone; refuse before anything is created.
    $path = Join-Path $Installation 'installation-manifest.json'
    $manifest = $null
    if (Test-Path -LiteralPath $path -PathType Leaf) { try { $manifest = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { $manifest = $null } }
    if (!$manifest -or !$manifest.PSObject.Properties['measurement_clone_of'] -or !$manifest.measurement_clone_of -or
        [string]::Equals((Get-FullDirectory ([string]$manifest.measurement_clone_of)), (Get-FullDirectory $Installation), [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Installation is not a measurement clone (installation-manifest.json measurement_clone_of); -Unattended runs on a timing clone such as timing-r111."
    }
}
function Stop-RunProcesses([string]$Installation, [DateTime]$NotBefore) {
    # After the timeout: ES2LAUNCH.EXE first, then the game processes, each only if its executable lies directly in
    # the installation and it started after this run began; PID, path and start time are read again right before
    # the stop. Anything else is left alone and named. Returns the lines for the evidence.
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($launcherRound in @($true, $false)) {
        foreach ($p in @(Get-InstallationProcesses $Installation | Where-Object { ($_.Name -ieq 'ES2LAUNCH.EXE') -eq $launcherRound })) {
            if (!$p.StartTimeUtc -or (ConvertTo-UtcTime $p.StartTimeUtc) -lt $NotBefore.AddSeconds(-1)) { $lines.Add("left alone (not started by this run): $(Format-CloneGameProcess $p)"); continue }
            $live = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
            if (!$live) { continue }
            $path = $null; $start = $null
            try { $path = $live.Path; $start = $live.StartTime.ToUniversalTime() } catch { $path = $null }
            if (!$path -or ![string]::Equals($path, $p.ExecutablePath, [StringComparison]::OrdinalIgnoreCase) -or $start -ne (ConvertTo-UtcTime $p.StartTimeUtc)) {
                $lines.Add("left alone (identity changed): $(Format-CloneGameProcess $p)"); continue
            }
            Stop-Process -InputObject $live -Force
            [void]$live.WaitForExit(15000)
            $lines.Add("stopped after the timeout: $(Format-CloneGameProcess $p)")
        }
    }
    return @($lines)
}
function ConvertTo-UtcTime($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    return [DateTime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
function Start-VolumeHelper([string]$Tool, [int]$ProcessId, [string]$Output) {
    # As run-tick-log-mission.ps1: the helper sets DBSIM's audio session from outside and exits with the game.
    # A console helper started without a window of its own (-NoNewWindow; the script itself runs hidden or in a console).
    $null = Start-Process -FilePath $Tool -NoNewWindow -PassThru -RedirectStandardOutput $Output `
        -ArgumentList @('--pid', [string]$ProcessId, '--percent', [string]$VolumePercent, '--wait-ms', [string](($TimeoutSeconds + 30) * 1000))
}

# ---- DBSIM's full screen (prefs byte 6) and the clone's proxy ----------------------------------------------
# With prefs byte 6 = 1 (the shell's Full Screen, or Alt+Enter) DBSIM takes its DirectDraw branch; on this machine the
# original branch leaves the primary surface [0x4d3714] NULL and DBSIM faults at 0x4654a8 (owner session
# own-menu-20261014T052324Z). A proxy with the own presenter replaces the branch with [presentation] directdraw=windowed
# (presentation.md); an older proxy build carries none of the strings "directdraw" and "presenter".
$oracleIniName = 'es2-oracle.ini'
function Test-BytesContain([byte[]]$Bytes, [string]$Text) {
    $needle = [Text.Encoding]::ASCII.GetBytes($Text)
    $first = $needle[0]
    $at = [Array]::IndexOf($Bytes, $first)
    while ($at -ge 0 -and $at -le $Bytes.Length - $needle.Length) {
        $match = $true
        for ($i = 1; $i -lt $needle.Length; $i++) { if ($Bytes[$at + $i] -ne $needle[$i]) { $match = $false; break } }
        if ($match) { return $true }
        $at = [Array]::IndexOf($Bytes, $first, $at + 1)
    }
    return $false
}
function Get-IniEncoding([byte[]]$Bytes) {
    if ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) { return [Text.Encoding]::Unicode }
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) { return (New-Object System.Text.UTF8Encoding($true)) }
    return $utf8
}
function Get-PresentationState([string]$Installation) {
    # {Proxy (a ddraw.dll exists), Supports (it has the [presentation] options), DirectDraw, Presenter (from es2-oracle.ini), Ok}
    $proxy = Join-Path $Installation 'ddraw.dll'
    $supports = $false
    $hasProxy = Test-Path -LiteralPath $proxy -PathType Leaf
    if ($hasProxy) {
        $bytes = [IO.File]::ReadAllBytes($proxy)
        $supports = (Test-BytesContain $bytes 'directdraw') -and (Test-BytesContain $bytes 'presenter')
    }
    $values = @{}
    $ini = Join-Path $Installation $oracleIniName
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        $bytes = [IO.File]::ReadAllBytes($ini)
        $section = ''
        foreach ($line in ((Get-IniEncoding $bytes).GetString($bytes).TrimStart([char]0xFEFF) -split "`r?`n")) {
            $t = $line.Trim()
            if ($t -match '^\[(.+)\]$') { $section = $Matches[1].Trim().ToLowerInvariant(); continue }
            if ($section -eq 'presentation' -and $t -match '^([^=;]+)=(.*)$') { $values[$Matches[1].Trim().ToLowerInvariant()] = $Matches[2].Trim() }
        }
    }
    $dd = [string]$values['directdraw']; $pr = [string]$values['presenter']
    return [pscustomobject]@{Proxy = $hasProxy; Supports = $supports; DirectDraw = $dd; Presenter = $pr
        Ok = ($supports -and $dd -ieq 'windowed')}
}
function Get-FullScreenWarning($State, [bool]$WillSet) {
    if ($State.Ok -or ($WillSet -and $State.Supports)) { return $null }
    $why = if (!$State.Proxy) { "$InstallName has no proxy ddraw.dll" }
        elseif (!$State.Supports) { "the proxy of $InstallName has no [presentation] presenter/directdraw options" }
        else { "es2-oracle.ini of $InstallName has [presentation] directdraw=$(if ($State.DirectDraw) { $State.DirectDraw } else { '<unset: original>' }), not windowed" }
    $fix = if ($State.Supports) { ' or add -WindowedDirectDraw' } else { '' }
    return ("WARNING: $why. DBSIM's full-screen setting (prefs byte 6 = 1, set by the shell's Full Screen or Alt+Enter) " +
        "crashes DBSIM on this machine (0xc0000005 at 0x4654a8, primary surface [0x4d3714] NULL; owner session " +
        "own-menu-20261014T052324Z). Keep the shell in Window mode, or play on rebuild-r111 (a proxy with " +
        "presenter=gdi, directdraw=windowed): -InstallName rebuild-r111$fix.")
}
function Get-VshellMeasureNotice([string]$Installation) {
    # The VSHELL-only measures of the proxy ([video] system_indeo=1: IV32 binding for the intro movies; [compat]
    # mission_map_lifetime=1: mission-map lifetime guard) are off by default. Only -Shell vshell needs them. Returns the
    # note when the clone has a proxy but its es2-oracle.ini lacks one of them, else $null.
    if (!(Test-Path -LiteralPath (Join-Path $Installation 'ddraw.dll') -PathType Leaf)) { return $null }
    $on = @{}
    $ini = Join-Path $Installation $oracleIniName
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        $bytes = [IO.File]::ReadAllBytes($ini)
        $section = ''
        foreach ($line in ((Get-IniEncoding $bytes).GetString($bytes).TrimStart([char]0xFEFF) -split "`r?`n")) {
            $t = $line.Trim()
            if ($t -match '^\[(.+)\]$') { $section = $Matches[1].Trim().ToLowerInvariant(); continue }
            if ($t -match '^([^=;]+)=\s*1\s*$') { $on["$section/$($Matches[1].Trim().ToLowerInvariant())"] = $true }
        }
    }
    $missing = @()
    if (!$on['video/system_indeo']) { $missing += '[video] system_indeo=1 (the intro movies stay black)' }
    if (!$on['compat/mission_map_lifetime']) { $missing += '[compat] mission_map_lifetime=1 (the mission-map lifetime guard; VSHELL can exit in mission selection after a mission)' }
    if (!$missing.Count) { return $null }
    return ("NOTE: -Shell vshell on $InstallName, whose es2-oracle.ini lacks the VSHELL-only proxy measures (off by default): " +
        ($missing -join '; ') + ". Switch them on with configure-media.ps1 -VshellMeasures or configure-tick-log.ps1 -VshellMeasures (docs/reference-game/media_compatibility.md).")
}
function Set-WindowedDirectDraw([string]$Path) {
    # Sets [presentation] directdraw=windowed and presenter=gdi; keeps every other line and the file's encoding.
    $exists = Test-Path -LiteralPath $Path -PathType Leaf
    $bytes = if ($exists) { [IO.File]::ReadAllBytes($Path) } else { [byte[]]@() }
    $encoding = if ($exists) { Get-IniEncoding $bytes } else { [Text.Encoding]::Unicode }
    $lines = New-Object System.Collections.Generic.List[string]
    if ($exists) { foreach ($l in ($encoding.GetString($bytes).TrimStart([char]0xFEFF) -split "`r?`n")) { $lines.Add($l) } }
    while ($lines.Count -and $lines[$lines.Count - 1] -eq '') { $lines.RemoveAt($lines.Count - 1) }
    $want = [ordered]@{directdraw = 'windowed'; presenter = 'gdi'}
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -match '^\[\s*presentation\s*\]$') { $start = $i; break } }
    if ($start -lt 0) { $lines.Add('[presentation]'); $start = $lines.Count - 1 }
    $end = $lines.Count
    for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -match '^\[.+\]$') { $end = $i; break } }
    $done = @{}
    for ($i = $start + 1; $i -lt $end; $i++) {
        if ($lines[$i].Trim() -match '^([^=;]+)=') {
            $key = $Matches[1].Trim().ToLowerInvariant()
            if ($want.Contains($key)) { $lines[$i] = "$key=$($want[$key])"; $done[$key] = $true }
        }
    }
    $insert = $end
    foreach ($key in $want.Keys) { if (!$done[$key]) { $lines.Insert($insert, "$key=$($want[$key])"); $insert++ } }
    $text = ($lines -join "`r`n") + "`r`n"
    [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($Path), [byte[]]($encoding.GetPreamble() + $encoding.GetBytes($text)))
}
function Restore-OracleIni([string]$SessionDirectory, $Record, [bool]$Apply, $Lines) {
    # Puts es2-oracle.ini back as it was before -WindowedDirectDraw (deleted when there was none). Returns ok.
    $info = $Record.oracle_ini
    $target = Join-Path ([string]$Record.installation) $oracleIniName
    $backup = Join-Path $SessionDirectory "backup\$oracleIniName"
    $current = if (Test-Path -LiteralPath $target -PathType Leaf) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { $null }
    $want = if ($info.existed) { [string]$info.sha256 } else { $null }
    if ($current -eq $want) { $Lines.Add("${oracleIniName}: unchanged"); return $true }
    if ($info.existed -and (!(Test-Path -LiteralPath $backup -PathType Leaf) -or (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $want)) {
        throw "The backup of $oracleIniName in $SessionDirectory does not match restore.json; it was not restored."
    }
    if (!$Apply) { $Lines.Add("${oracleIniName}: would $(if ($info.existed) { 'restore' } else { 'remove' }) (-WindowedDirectDraw)"); return $true }
    if ($info.existed) { Copy-Item -LiteralPath $backup -Destination $target -Force } else { Remove-Item -LiteralPath $target -Force }
    $now = if (Test-Path -LiteralPath $target -PathType Leaf) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { $null }
    $Lines.Add("${oracleIniName}: $(if ($info.existed) { 'restored' } else { 'removed' }) $(if ($now -eq $want) { '== before-session' } else { '!= before-session' })")
    return ($now -eq $want)
}

# ---- restore-only mode ---------------------------------------------------------------------------------
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
if ($RestoreOnly) { exit (Invoke-RestoreOnly) }

# ---- refusals and the plan --------------------------------------------------------------------------------
$installation = Resolve-Installation $InstallName
Assert-PrimaryRule $InstallName
Assert-GameInstallation $installation
Assert-InstallationIdle $installation 'starting the own menu'
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$session = if ($SessionDirectory) { [IO.Path]::GetFullPath($SessionDirectory).TrimEnd('\') } else { Join-Path $DataRoot "logs\own-menu-$stamp" }
if ($SessionDirectory -and (Test-Path -LiteralPath $session)) { throw "-SessionDirectory exists already: $session" }
$suiteInfo = $null
if ($Unattended) {
    Assert-MeasurementClone $installation
    $suiteInfo = Get-UnattendedSuite $Suite
    if ($PSBoundParameters.ContainsKey('Seed') -and $Seed -ne $unattendedSeed[$Suite]) { throw "-Unattended $Suite runs with the seed of its golden ($($unattendedSeed[$Suite]))." }
    $Seed = $unattendedSeed[$Suite]
    if (!$GoldenRoot) { $GoldenRoot = Join-Path $DataRoot 'oracle\golden' }
    $golden = Join-Path $GoldenRoot "$Suite\normalized.json"
    if (!$DryRun -and !(Test-Path -LiteralPath $golden -PathType Leaf)) { throw "No golden for ${Suite}: $golden" }
    if ($MenuInput -and !(Test-Path -LiteralPath $MenuInput -PathType Leaf)) { throw "No menu input file: $MenuInput" }
    if (!$ConfigureScript) { $ConfigureScript = Join-Path $PSScriptRoot 'configure-tick-log.ps1' }
    if (!$ConfigureSimScript) { $ConfigureSimScript = Join-Path $PSScriptRoot 'configure-sim.ps1' }
    $regressionScript = Join-Path $repository 'tools\reference-game\scripts\oracle_regression.py'
    $runDirectory = Join-Path $session 'run'
    $tickLog = Join-Path $runDirectory "$Suite.jsonl"
    $simConfig = Join-Path $runDirectory 'es2sim.ini'
    $simOutput = Join-Path $runDirectory 'es2sim-output.txt'
}
$presentation = $null
$fullScreenWarning = $null
if (!$Unattended) {
    $presentation = Get-PresentationState $installation
    if ($WindowedDirectDraw -and !$presentation.Supports) {
        throw "-WindowedDirectDraw needs a proxy with the [presentation] options; $InstallName's $(if ($presentation.Proxy) { 'proxy has no [presentation] options' } else { 'has no proxy ddraw.dll' }). Play on rebuild-r111."
    }
    $fullScreenWarning = Get-FullScreenWarning $presentation ([bool]$WindowedDirectDraw)
}
$vshellMeasureNotice = if ($Shell -eq 'vshell') { Get-VshellMeasureNotice $installation } else { $null }
$evidence = Join-Path $session 'evidence'
$private = Join-Path $session 'private'
$menuDirectory = Join-Path $DataRoot "oracle\launch\own-menu-$stamp"
$cdRoot = if ($CdRoot) { [IO.Path]::GetFullPath($CdRoot).TrimEnd('\') } else { Join-Path ([IO.Path]::GetFullPath($ExtractedRoot).TrimEnd('\')) 'cd' }
if (!$BackendScript) { $BackendScript = Join-Path $repository 'shell\tools\es2menu_backend.py' }
if (!$LauncherScript) { $LauncherScript = Join-Path $PSScriptRoot 'start-own-launcher.ps1' }
$chainScript = Join-Path (Split-Path -Parent $BackendScript) 'es2chain.py'
$buildDirectory = Join-Path $repository 'build\reference-game'
$shellTarget = if ($Shell -eq 'gui') { '//shell:es2shell (with //shell:gui_sdl_runtime, SDL3.dll)' } else { '//shell:es2menu' }
$shellName = if ($Shell -eq 'gui') { 'es2shell.exe' } else { 'es2menu.exe' }
if ($Shell -eq 'vshell') { $NoVerify = [switch]$true }
$bazelContents = "$BazelOutputBase-contents"

$steps = @(
    "check: $InstallName is not $primaryName, no ES.EXE/VSHELL.EXE/DBSIM.EXE/ES2LAUNCH.EXE of $installation runs, no other session holds its clone lock"
    "lock the clone ($(Get-CloneLockPath $DataRoot $InstallName)) until the end of this script"
    "BACKUP first: copy DATA and SAV of $installation to $session\backup (git-ignored), print their hashes, write restore.json"
    $(if ($WindowedDirectDraw) { "back up $oracleIniName of $installation, then set [presentation] directdraw=windowed and presenter=gdi in it for the session (put back with DATA and SAV)" })
    $(if ($Shell -eq 'vshell') { "no build: the launcher starts the original VSHELL.EXE (-Shell vshell, no ES2_LAUNCH_SHELL)" } elseif ($MenuExe -and $CoreExe) { "use the given $shellName and es2prep_core.exe (no build)" } else { "build $shellTarget and //shell:es2prep_core with Bazel (output base $BazelOutputBase) into $buildDirectory" })
    $(if ($Shell -eq 'vshell') { "no menu directory (-Shell vshell)" } else { "set up the menu directory $menuDirectory (es2menu_backend.py setup, seed $Seed, game dir $installation)" })
    $(if ($Unattended -and $Sim -eq 'own') { "deploy the own DBSIM process: write $simConfig (es2sim's configuration of ${Suite}: the watches of <$($suiteInfo.Watches)>, virtual time 47 ms, replay input $($suiteInfo.Script), early arming, ticks $($suiteInfo.Ticks), outcome 1), then $ConfigureSimScript -Apply -Headless -Config $simConfig -TickLog $tickLog -Output $simOutput" + $(if ($SimExe) { " -Sim $SimExe" } else { '' }) + $(if ($SimSdl) { " -Sdl $SimSdl" } else { '' }) + ' (with this script''s clone lock token): ES2LAUNCH.EXE starts es2sim instead of DBSIM.EXE; the restore runs configure-sim.ps1 -Remove' } elseif ($Unattended) { "configure DBSIM with the oracle settings of ${Suite}: $ConfigureScript -TickLogPath $tickLog -VirtualTimeStepMs 47 -VirtualTimePace 0 -VirtualTimeIntraFrame 1 -InputMode replay -InputJournal $($suiteInfo.Script) -Watch <$($suiteInfo.Watches)> -TickTarget $($suiteInfo.Ticks) -HiddenWindow -Hooks '$Hooks'" + $(if ($PreparedProxy) { " -PreparedProxy $PreparedProxy" } else { '' }) + ' -Apply (with this script''s clone lock token)' })
    $(if ($Unattended -and $Shell -eq 'gui') { "start the launcher once with ES2_LAUNCH_SHELL = es2shell.exe --menu <dir> --headless --script <evidence\shell-script-x{X}.txt> --report <evidence\shell-report-x{X}.txt> (X1: CONTINUE, MISSION, MISSION, Rock & Roll; X3: MAIN MENU, QUIT), no console, ES2_LAUNCH_NO_UI=1: $LauncherScript -InstallName $InstallName -AllowOracleConfig" } elseif ($Unattended) { "start the launcher once with ES2_LAUNCH_SHELL = <menu> --input <$(if ($MenuInput) { $MenuInput } else { 'CONTINUE, MISSION, HAUPTMENUE, QUIT' })>, no menu console, ES2_LAUNCH_NO_UI=1: $LauncherScript -InstallName $InstallName -AllowOracleConfig" } elseif ($Shell -eq 'text') { "start the launcher $Runs time(s) with ES2_LAUNCH_SHELL and ES2_LAUNCH_SHELL_CONSOLE=1 (visible menu console): $LauncherScript -InstallName $InstallName" } elseif ($Shell -eq 'gui') { "start the launcher $Runs time(s) with ES2_LAUNCH_SHELL = es2shell.exe --menu (the graphical shell, no console): $LauncherScript -InstallName $InstallName" } else { "start the launcher $Runs time(s) without ES2_LAUNCH_SHELL (the original VSHELL.EXE): $LauncherScript -InstallName $InstallName" })
    $(if ($Unattended) { "wait for ES2LAUNCH.EXE (at most $TimeoutSeconds s; then only this run's ES2LAUNCH.EXE and game processes are stopped, identity checked) and every game process of the installation to end" + $(if ($VolumePercent -lt 100 -and $Sim -ne 'own') { "; DBSIM's audio session at $VolumePercent %" } else { '' }) } else { 'wait for ES2LAUNCH.EXE and every game process of the installation to end (nothing is ever stopped)' })
    "collect evidence into $evidence (launcher logs, chain.json/chain.log, status, hashes of DATA/SAV after the session; the menu log into $private)"
    "RESTORE in a finally: DATA and SAV of $installation equal the backup again (tree hashes printed)$(if ($Sim -eq 'own') { ', the own simulator removed (configure-sim.ps1 -Remove)' }); -RestoreOnly '$session' does it after an interrupted session"
    $(if ($NoVerify) { 'skip the ES2_CHAIN_VERIFY test (-NoVerify)' } else { 'run bazel test //shell:chain_real_test with ES2_CHAIN_VERIFY=<campaign> (the own-menu campaign against the original VSHELL harness)' })
    "print the compare checklist and write $evidence\checklist.txt"
)
if ($Unattended) {
    $steps += @(
        "judge (evidence\unattended.json): the launcher events are the one-mission sequence; missions[0] of the campaign has the files the golden's DBSIM ran (pins); the tick log with DBSIM's results.dat/mission.var, normalized by oracle_regression.py, equals $golden; the chain verify has every preparation equal" + $(if ($Sim -eq 'own') { '; the launcher events start es2sim (sim-replacement) where DBSIM.EXE was started' } else { '' }) + $(if ($Shell -eq 'gui') { "; es2shell's headless reports: X1 ends with exit 2, X3 with exit 0, no script error" } else { '' })
    )
}

if ($DryRun) {
    Say "DRY RUN for $installation (nothing is created, started or changed). Steps:"
    $number = 0
    foreach ($s in $steps) { $number++; Say ("  {0}. {1}" -f $number, $s) }
    if (!(Test-Path -LiteralPath $cdRoot -PathType Container)) { Say "WOULD REFUSE: no $cdRoot (extracted CD)." }
    if (!(Test-Path -LiteralPath $Python -PathType Leaf)) { Say "WOULD REFUSE: no Python at $Python." }
    foreach ($folder in $folders) {
        $m = Get-TreeManifest (Join-Path $installation $folder)
        Say ("  {0}: {1} files, tree {2}" -f $folder, $m.Files.Count, (Get-ManifestDigest $m))
    }
    $failed = $false
    if ($Unattended -and !(Test-Path -LiteralPath $golden -PathType Leaf)) { $failed = $true; Say "WOULD REFUSE: no golden for ${Suite}: $golden" }
    try {
        $launcherSplat = @{DataRoot = $DataRoot; InstallName = $InstallName; DryRun = $true}
        if ($Unattended) { $launcherSplat.AllowOracleConfig = $true }
        $plan = & $LauncherScript @launcherSplat | Out-String
        Say "launcher plan (start-own-launcher.ps1 -DryRun):"
        $plan.TrimEnd().Split("`n") | ForEach-Object { Say ("    " + $_.TrimEnd()) }
    } catch {
        $failed = $true
        Say "WOULD REFUSE at the launcher: $(Format-Failure $_)"
    }
    if ($Unattended -and $Sim -eq 'own') {
        try {
            $preview = (& $ConfigureSimScript -DataRoot $DataRoot -InstallName $InstallName *>&1 | ForEach-Object { [string]$_ }) -join "`n"
            Say "own simulator (configure-sim.ps1 preview): state $(($preview.Substring($preview.IndexOf('{')) | ConvertFrom-Json).state)"
        } catch {
            $failed = $true
            Say "WOULD REFUSE at configure-sim.ps1: $(Format-Failure $_)"
        }
    }
    if ($fullScreenWarning) { Say $fullScreenWarning }
    if ($vshellMeasureNotice) { Say $vshellMeasureNotice }
    if (!$Unattended) { Say (Get-PlayGuide) }
    if ($failed) { exit 1 }
    exit 0
}

if (!(Test-Path -LiteralPath $cdRoot -PathType Container)) { throw "No extracted CD at $cdRoot." }
if (!(Test-Path -LiteralPath $Python -PathType Leaf)) { throw "No Python at $Python." }
if (!(Test-Path -LiteralPath $BackendScript -PathType Leaf)) { throw "No backend at $BackendScript." }
if (!(Test-Path -LiteralPath $LauncherScript -PathType Leaf)) { throw "No launcher script at $LauncherScript." }

# ---- the session --------------------------------------------------------------------------------------------
$lock = Enter-CloneLock -DataRoot $DataRoot -InstallName $InstallName -Purpose 'own menu session'
$exitCode = 0
$failure = $null
$record = $null
$restored = $false
$deferred = $false
$campaign = ''
$setup = $null
$runLogs = New-Object System.Collections.Generic.List[string]
$runInfo = New-Object System.Collections.Generic.List[object]
$savedEnvironment = @{}
foreach ($name in @('ES2_LAUNCH_SHELL', 'ES2_LAUNCH_SHELL_CONSOLE', 'ES2_BAZEL_OUTPUT_BASE', 'ES2_BAZEL_CONTENTS_CACHE', 'ES2_LAUNCH_NO_UI', 'ES2_CLONE_LOCK_TOKEN',
                       'ES2_LAUNCH_SIM', 'ES2_LAUNCH_SIM_OUTPUT')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}
$timedOut = $false
$stopLines = @()
try {
    # configure-tick-log.ps1 (a child of this run) passes the clone lock this script holds.
    if ($Unattended) { $env:ES2_CLONE_LOCK_TOKEN = $lock.Token }
    Say "Session $stamp on $installation (clone lock held)."
    # 1. Backup first.
    $before = Get-InstallationManifests $installation
    $total = ($before.Values | ForEach-Object { $_.Files.Values } | ForEach-Object { $_.Size } | Measure-Object -Sum).Sum
    if ($total -gt 512MB) { throw "DATA and SAV hold $([int]($total / 1MB)) MB; refusing a backup that large." }
    New-Item -ItemType Directory -Force -Path $session, $evidence | Out-Null
    New-IgnoredDirectory (Join-Path $session 'backup')
    New-IgnoredDirectory $private
    foreach ($folder in $folders) {
        Copy-Item -LiteralPath (Join-Path $installation $folder) -Destination (Join-Path $session "backup\$folder") -Recurse -Force
        $copy = Get-TreeManifest (Join-Path $session "backup\$folder")
        if ((Get-ManifestDigest $copy) -ne (Get-ManifestDigest $before[$folder]) -or $copy.Files.Count -ne $before[$folder].Files.Count) {
            throw "The backup copy of $folder differs from the source; nothing was started."
        }
    }
    $es2launch = Join-Path $installation 'ES2LAUNCH.EXE'
    $record = [ordered]@{
        schema = 1; started_utc = [DateTime]::UtcNow.ToString('o'); installation = $installation; install_name = $InstallName; shell = $Shell
        primary = ($InstallName -ieq $primaryName); data_root = $DataRoot; session = $session; seed = $Seed; runs = $Runs
        manifest = [ordered]@{DATA = (ConvertTo-ManifestRecord $before.DATA); SAV = (ConvertTo-ManifestRecord $before.SAV)}
        es2launch_sha256_before = $(if (Test-Path -LiteralPath $es2launch) { (Get-FileHash -LiteralPath $es2launch -Algorithm SHA256).Hash } else { $null })
    }
    # Recorded before the deployment, so the restore (and -RestoreOnly) removes the own simulator.
    if ($Unattended -and $Sim -eq 'own') { $record.own_sim = [ordered]@{configure_script = [IO.Path]::GetFullPath($ConfigureSimScript)} }
    if ($WindowedDirectDraw) {
        # The ini is backed up and recorded before it is changed, so -RestoreOnly can put it back.
        $iniPath = Join-Path $installation $oracleIniName
        $iniExisted = Test-Path -LiteralPath $iniPath -PathType Leaf
        if ($iniExisted) { Copy-Item -LiteralPath $iniPath -Destination (Join-Path $session "backup\$oracleIniName") -Force }
        $record.oracle_ini = [ordered]@{existed = $iniExisted; sha256 = $(if ($iniExisted) { (Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash } else { $null })}
    }
    Write-JsonFile (Join-Path $session 'restore.json') $record
    if ($WindowedDirectDraw) {
        Set-WindowedDirectDraw $iniPath
        Say "$oracleIniName of ${InstallName}: [presentation] directdraw=windowed, presenter=gdi for this session (backup: $session\backup\$oracleIniName)"
    }
    Write-TextFile (Join-Path $evidence 'before-session-hashes.txt') ((@($folders | ForEach-Object { Format-ManifestLines $_ $before[$_] }) -join "`n") + "`n")
    foreach ($folder in $folders) {
        Say ("backup {0}: {1} files, tree {2} (copy verified)" -f $folder, $before[$folder].Files.Count, (Get-ManifestDigest $before[$folder]))
    }
    Say "Backup: $session\backup (restore record: $session\restore.json)"

    # 2. Menu and core executables (none for the original VSHELL).
    if ($Shell -eq 'vshell') { $menu = $null; $core = $null }
    elseif ($MenuExe -and $CoreExe) { $menu = $MenuExe; $core = $CoreExe }
    else {
        $env:ES2_BAZEL_OUTPUT_BASE = $BazelOutputBase
        $env:ES2_BAZEL_CONTENTS_CACHE = $bazelContents
        Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
        Say "Building $shellTarget and //shell:es2prep_core (Bazel; the first build takes a while) ..."
        if ($Shell -eq 'gui') {
            $menu = Copy-Es2BazelOutput -Target '//shell:es2shell' -Destination $buildDirectory
            $null = Copy-Es2BazelOutput -Target '//shell:gui_sdl_runtime' -Destination $buildDirectory   # SDL3.dll beside es2shell.exe
        } else {
            $menu = Copy-Es2BazelOutput -Target '//shell:es2menu' -Destination $buildDirectory
        }
        $core = Copy-Es2BazelOutput -Target '//shell:es2prep_core' -Destination $buildDirectory
    }

    # 3. The menu directory (es2menu and es2shell read the same menu.ini; none for the original VSHELL).
    $launchShell = $null
    if ($Shell -ne 'vshell') {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $menuDirectory) | Out-Null
        $result = Invoke-Native $Python @('-B', $BackendScript, 'setup', '--menu', $menuDirectory, '--menu-exe', $menu, '--core', $core,
            '--cd-root', $cdRoot, '--game-dir', $installation, '--seed', [string]$Seed)
        if ($result.Code -ne 0) { throw "Backend setup failed ($($result.Code)): $($result.Text)" }
        $setup = $result.Text | ConvertFrom-Json
        Say "Menu directory: $($setup.menu)"
        Write-TextFile (Join-Path $evidence 'menu-launch-shell.txt') ([string]$setup.launch_shell + "`n")
        $launchShell = [string]$setup.launch_shell
    }

    if ($Unattended) {
        # 3a. The menu script and DBSIM's oracle settings of the suite (as run-tick-log-mission.ps1 applies them).
        if ($Shell -eq 'gui') {
            # es2shell headless, one script and report per start; es2shell puts the -X entry mode for {X}
            # (main.c), so the -X1 start reads shell-script-x1.txt and the -X3 results start shell-script-x3.txt.
            Write-TextFile (Join-Path $evidence 'shell-script-x1.txt') $guiScriptX1
            Write-TextFile (Join-Path $evidence 'shell-script-x3.txt') $guiScriptX3
            $inputFile = Join-Path $evidence 'shell-script-x1.txt'
            $launchShell = "$launchShell --headless --script `"$(Join-Path $evidence 'shell-script-x{X}.txt')`" --report `"$(Join-Path $evidence 'shell-report-x{X}.txt')`""
        } else {
            $inputFile = Join-Path $evidence 'menu-input.txt'
            if ($MenuInput) { Copy-Item -LiteralPath $MenuInput -Destination $inputFile }
            else { Write-TextFile $inputFile "2`n4`n1`n5`n" }   # CONTINUE; MISSION (Rock & Roll); after the results: HAUPTMENUE; QUIT
            $launchShell = "$launchShell --input `"$inputFile`""
        }
        New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
        if (Test-Path -LiteralPath $tickLog) { throw "The tick log exists already: $tickLog" }
        if ($Sim -eq 'own') {
            # es2sim with the suite's settings in its own configuration; es2launch starts it in place of DBSIM.EXE.
            [IO.File]::WriteAllText($simConfig, (Get-OwnSimConfig $suiteInfo), [Text.Encoding]::Unicode)
            $deploy = @{DataRoot = $DataRoot; InstallName = $InstallName; Apply = $true; Headless = $true; Config = $simConfig; TickLog = $tickLog; Output = $simOutput}
            if ($SimExe) { $deploy.Sim = $SimExe }
            if ($SimSdl) { $deploy.Sdl = $SimSdl }
            if ($InstallName -ieq $primaryName) { $deploy.AllowPrimary = $true }
            if (!$SimExe -or !$SimSdl) { $env:ES2_BAZEL_OUTPUT_BASE = $BazelOutputBase; $env:ES2_BAZEL_CONTENTS_CACHE = $bazelContents }
            Say "Deploying the own DBSIM process es2sim on $InstallName with the oracle settings of $Suite (headless, virtual time, scripted input) ..."
            $configureText = (& $ConfigureSimScript @deploy *>&1 | ForEach-Object { [string]$_ }) -join "`n"
            Write-TextFile (Join-Path $evidence 'configure.log') ($configureText + "`n")
        } else {
            $configure = @{DataRoot = $DataRoot; InstallName = $InstallName; TickLogPath = $tickLog; VirtualTimeStepMs = 47; VirtualTimePace = 0
                VirtualTimeIntraFrame = 1; InputMode = 'replay'; InputJournal = $suiteInfo.Script; Watch = @(Get-Content -LiteralPath $suiteInfo.Watches)
                TickTarget = $suiteInfo.Ticks; Hooks = $Hooks; HiddenWindow = $true; Apply = $true}
            if ($PreparedProxy) { $configure.PreparedProxy = $PreparedProxy }
            Say "Configuring DBSIM on $InstallName with the oracle settings of $Suite (hidden window, virtual time, scripted input) ..."
            $configureText = (& $ConfigureScript @configure *>&1 | ForEach-Object { [string]$_ }) -join "`n"
            Write-TextFile (Join-Path $evidence 'configure.log') ($configureText + "`n")
        }
        # es2sim --headless has the silent audio device: no volume helper.
        if ($VolumePercent -lt 100 -and !$VolumeTool -and $Sim -ne 'own') {
            Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
            $VolumeTool = if ($PreparedProxy) { (Get-Es2PreparedProxy $PreparedProxy).volume } else {
                $env:ES2_BAZEL_OUTPUT_BASE = $BazelOutputBase; $env:ES2_BAZEL_CONTENTS_CACHE = $bazelContents
                Copy-Es2BazelOutput -Target '//tools/reference-game/helpers:session_volume' -Destination $buildDirectory
            }
        }
    }

    # 4. Launch (visible by design: the menu console and the game windows are the owner's session; -Unattended: hidden).
    if ($Unattended) { Say "$(if ($Shell -eq 'gui') { 'Shell script (-X1)' } else { 'Menu input' }): $((Get-Content -LiteralPath $inputFile) -join ' | ')" } else { Say (Get-PlayGuide) }
    if ($fullScreenWarning) { Say $fullScreenWarning; Write-TextFile (Join-Path $evidence 'full-screen-warning.txt') ($fullScreenWarning + "`n") }
    if ($vshellMeasureNotice) { Say $vshellMeasureNotice }
    for ($run = 1; $run -le $Runs; $run++) {
        if ($launchShell) { $env:ES2_LAUNCH_SHELL = $launchShell } else { Remove-Item Env:ES2_LAUNCH_SHELL -ErrorAction SilentlyContinue }
        $launcherSplat = @{DataRoot = $DataRoot; InstallName = $InstallName}
        if ($Unattended) {
            # No console for the menu (CREATE_NO_WINDOW), launcher messages into its log instead of a box, the clone's
            # oracle settings accepted.
            Remove-Item Env:ES2_LAUNCH_SHELL_CONSOLE -ErrorAction SilentlyContinue
            # The simulator is the installation's es2launch.ini (none: DBSIM.EXE), never one inherited from here.
            Remove-Item Env:ES2_LAUNCH_SIM, Env:ES2_LAUNCH_SIM_OUTPUT -ErrorAction SilentlyContinue
            $env:ES2_LAUNCH_NO_UI = '1'
            $launcherSplat.AllowOracleConfig = $true
        } elseif ($Shell -eq 'text') { $env:ES2_LAUNCH_SHELL_CONSOLE = '1' }
        else { Remove-Item Env:ES2_LAUNCH_SHELL_CONSOLE -ErrorAction SilentlyContinue }   # es2shell or VSHELL, no console
        $startedAt = [DateTime]::UtcNow
        Say ("Run $run of ${Runs}: starting the launcher " + $(if ($Unattended) { '(unattended: no window, scripted menu and DBSIM input) ...' } else { '(' + $(switch ($Shell) { 'text' { 'a console with the menu opens' } 'gui' { 'the graphical shell opens' } default { 'the original VSHELL opens' } }) + ') ...' }))
        # Visible on purpose: the owner's interactive session. The launcher script starts ES2LAUNCH.EXE (a GUI
        # program) and the menu console opens because ES2_LAUNCH_SHELL_CONSOLE is set.
        $launched = & $LauncherScript @launcherSplat | Out-String
        $first = $launched.IndexOf('{'); $last = $launched.LastIndexOf('}')
        if ($first -lt 0 -or $last -lt $first) { throw "Unexpected output of the launcher script: $launched" }
        $plan = $launched.Substring($first, $last - $first + 1) | ConvertFrom-Json
        $info = [ordered]@{run = $run; launcher_pid = $plan.launcher_pid; launch_log = $plan.launch_log; started_utc = $startedAt.ToString('o'); exit_code = $null}
        if ($plan.launcher_pid) {
            $process = Get-Process -Id ([int]$plan.launcher_pid) -ErrorAction SilentlyContinue
            if ($process) {
                $started = $process.StartTime.ToUniversalTime()
                if ($started -lt $startedAt.AddSeconds(-5)) { throw "PID $($plan.launcher_pid) started at $($started.ToString('o')), before this run; not waiting for it." }
                if ($Unattended) {
                    Say "Waiting for ES2LAUNCH.EXE (PID $($plan.launcher_pid)) to end (unattended, at most $TimeoutSeconds s) ..."
                    $deadline = $startedAt.AddSeconds($TimeoutSeconds)
                    $volumeStarted = !($VolumeTool -and $VolumePercent -lt 100) -or $Sim -eq 'own'
                    while (!$process.WaitForExit(1000)) {
                        if (!$volumeStarted) {
                            $dbsim = @(Get-InstallationProcesses $installation | Where-Object { $_.Name -ieq 'DBSIM.EXE' -and $_.StartTimeUtc -and (ConvertTo-UtcTime $_.StartTimeUtc) -ge $startedAt.AddSeconds(-1) })
                            if ($dbsim.Count) {
                                Start-VolumeHelper $VolumeTool $dbsim[0].ProcessId (Join-Path $runDirectory "$Suite-volume.json")
                                $volumeStarted = $true
                            }
                        }
                        if ([DateTime]::UtcNow -gt $deadline) {
                            $timedOut = $true
                            Say "TIMEOUT after $TimeoutSeconds s: stopping this run's own processes (identity checked) ..."
                            $stopLines = @(Stop-RunProcesses $installation $startedAt)
                            $stopLines | ForEach-Object { Say "  $_" }
                            [void]$process.WaitForExit(15000)
                            break
                        }
                    }
                    $info.timed_out = $timedOut
                    if ($process.HasExited) { $info.exit_code = [int]$process.ExitCode }
                } else {
                    Say "Waiting for ES2LAUNCH.EXE (PID $($plan.launcher_pid)) to end - play, then quit in the menu (5) ..."
                    while (!$process.WaitForExit(1000)) { }
                    $info.exit_code = [int]$process.ExitCode
                }
            }
        }
        $info.ended_utc = [DateTime]::UtcNow.ToString('o')
        $runInfo.Add($info)
        if ($plan.launch_log) { $runLogs.Add([string]$plan.launch_log) }
        Say "Run $run ended (launcher exit code $($info.exit_code))."
        Remove-Item Env:ES2_LAUNCH_SHELL, Env:ES2_LAUNCH_SHELL_CONSOLE -ErrorAction SilentlyContinue
        # Game processes of the installation (a DBSIM that outlived the launcher) must be gone before the next run and the restore.
        $deadline = [DateTime]::UtcNow.AddSeconds($SettleSeconds)
        while (@(Get-InstallationProcesses $installation).Count -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 500 }
        if ($timedOut -and @(Get-InstallationProcesses $installation).Count) {
            # A game process this run started after the stop above (the launcher was between two children): once more.
            $more = @(Stop-RunProcesses $installation $startedAt)
            $stopLines += $more
            $more | ForEach-Object { Say "  $_" }
        }
        if ($timedOut) { Write-TextFile (Join-Path $evidence 'timeout-stops.txt') ((@($stopLines) -join "`n") + "`n") }
        if (@(Get-InstallationProcesses $installation).Count) { break }
    }
} catch {
    $failure = $_
    Say "FAILED: $(Format-Failure $_)"
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name]) }
    # 5. Evidence, then the restore (always, once no game process of the installation runs).
    if ($record) {
        try {
            $busy = @(Get-InstallationProcesses $installation)
            $after = Get-InstallationManifests $installation
            foreach ($folder in $folders) {
                $dest = Join-Path $session "after-session\$folder"
                if (!(Test-Path -LiteralPath (Join-Path $session 'after-session'))) { New-IgnoredDirectory (Join-Path $session 'after-session') }
                if (Test-Path -LiteralPath (Join-Path $installation $folder)) {
                    Copy-Item -LiteralPath (Join-Path $installation $folder) -Destination $dest -Recurse -Force
                }
            }
            Write-TextFile (Join-Path $evidence 'after-session-hashes.txt') ((@($folders | ForEach-Object { Format-ManifestLines $_ $after[$_] }) -join "`n") + "`n")
            $changes = New-Object System.Collections.Generic.List[string]
            foreach ($folder in $folders) {
                $diff = Compare-Manifests $before[$folder] $after[$folder]
                $changes.Add(("{0} after the session: {1} added, {2} removed, {3} changed; tree {4}" -f $folder, $diff.Added.Count, $diff.Removed.Count, $diff.Changed.Count, (Get-ManifestDigest $after[$folder])))
                foreach ($n in $diff.Added) { $changes.Add("  added   $folder\$n") }
                foreach ($n in $diff.Removed) { $changes.Add("  removed $folder\$n") }
                foreach ($n in $diff.Changed) { $changes.Add("  changed $folder\$n") }
            }
            $changes | ForEach-Object { Say $_ }
            Write-TextFile (Join-Path $evidence 'changes.txt') (($changes -join "`n") + "`n")
            $index = 0
            foreach ($log in $runLogs) {
                $index++
                if (Test-Path -LiteralPath $log -PathType Leaf) { Copy-Item -LiteralPath $log -Destination (Join-Path $evidence "launcher-run$index.log") -Force }
            }
            if ($setup) {
                $menuLog = Join-Path $setup.menu 'menu.log'
                if (Test-Path -LiteralPath $menuLog -PathType Leaf) { Copy-Item -LiteralPath $menuLog -Destination (Join-Path $private 'menu.log') -Force }
                $shellLog = Join-Path $setup.menu 'shell.log'   # es2shell's flow events, movies and saves
                if (Test-Path -LiteralPath $shellLog -PathType Leaf) { Copy-Item -LiteralPath $shellLog -Destination (Join-Path $private 'shell.log') -Force }
                $currentFile = Join-Path $setup.menu 'current'
                if (Test-Path -LiteralPath $currentFile -PathType Leaf) {
                    $campaign = Join-Path $setup.menu ((Get-Content -LiteralPath $currentFile -Raw).Trim())
                }
                if ($campaign -and (Test-Path -LiteralPath $campaign -PathType Container)) {
                    foreach ($n in @('chain.json', 'chain.log')) {
                        if (Test-Path -LiteralPath (Join-Path $campaign $n)) { Copy-Item -LiteralPath (Join-Path $campaign $n) -Destination $evidence -Force }
                    }
                    if (Test-Path -LiteralPath $chainScript) {
                        $status = Invoke-Native $Python @('-B', $chainScript, 'status', '--state', $campaign)
                        if ($status.Code -eq 0) { Write-TextFile (Join-Path $evidence 'status.json') $status.Text }
                    }
                    $chain = Get-Content -LiteralPath (Join-Path $campaign 'chain.json') -Raw | ConvertFrom-Json
                    $compare = New-Object System.Collections.Generic.List[string]
                    $gameSav = Join-Path $installation 'SAV\GAME_R.SAV'
                    $last = Join-Path $campaign ([string]$chain.save)
                    $compare.Add("clone SAV\GAME_R.SAV after the session: " + $(if (Test-Path -LiteralPath $gameSav) { (Get-FileHash -LiteralPath $gameSav -Algorithm SHA256).Hash } else { '(absent)' }))
                    $compare.Add("campaign last SAV $($chain.save): " + $(if (Test-Path -LiteralPath $last) { (Get-FileHash -LiteralPath $last -Algorithm SHA256).Hash } else { '(absent)' }))
                    Write-TextFile (Join-Path $evidence 'sav-compare.txt') (($compare -join "`n") + "`n")
                    $mec = New-Object System.Collections.Generic.List[string]
                    foreach ($p in @((Join-Path $campaign 'mission-000\launch\player.mec'), (Join-Path $campaign 'mission-000\player.mec'))) {
                        if (Test-Path -LiteralPath $p) { $mec.Add("$((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash)  $p") }
                    }
                    if ($mec.Count) { Write-TextFile (Join-Path $evidence 'mec-hashes.txt') (($mec -join "`n") + "`n") }
                    if ($Unattended) {
                        # What DBSIM returned (the chain's copy) is the run's output, as <suite>-output of the oracle runner.
                        $outputs = Join-Path $runDirectory "$Suite-output"
                        foreach ($n in @('results.dat', 'mission.var')) {
                            $returned = Join-Path $campaign "mission-000\dbsim\$n"
                            if (Test-Path -LiteralPath $returned -PathType Leaf) {
                                New-Item -ItemType Directory -Force -Path $outputs | Out-Null
                                Copy-Item -LiteralPath $returned -Destination (Join-Path $outputs $n) -Force
                            }
                        }
                    }
                }
            }
            $summary = @(Get-LauncherSummary @(Get-ChildItem -LiteralPath $evidence -Filter 'launcher-run*.log' -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { $_.FullName }))
            Write-TextFile (Join-Path $evidence 'summary.txt') (($summary -join "`n") + "`n")
            Write-JsonFile (Join-Path $evidence 'session.json') ([ordered]@{stamp = $stamp; installation = $installation; seed = $Seed; shell = $Shell; runs = $runInfo.ToArray(); menu = $(if ($setup) { $setup.menu } else { $null }); campaign = $campaign; failure = $(if ($failure) { $failure.Exception.Message } else { $null }); game_processes_left = @($busy | ForEach-Object { Format-CloneGameProcess $_ })})
        } catch { Say "Evidence collection failed (the restore continues): $(Format-Failure $_)" }
        try {
            $busy = @(Get-InstallationProcesses $installation)
            if ($busy.Count) {
                $deferred = $true
                $exitCode = 2
                Say ('NOT RESTORED: a game process of the installation still runs: ' + (($busy | ForEach-Object { Format-CloneGameProcess $_ }) -join '; ') + '. It is left alone.')
                Say ("Close it (end ES2LAUNCH.EXE first, then DBSIM), then run: pwsh -File `"$PSCommandPath`" -RestoreOnly `"$session`" -DataRoot `"$DataRoot`"" + $(if ($record.primary) { ' -AllowPrimary' } else { '' }))
            } else {
                $report = Invoke-Restore $session ([pscustomobject](Get-Content -LiteralPath (Join-Path $session 'restore.json') -Raw | ConvertFrom-Json)) $true $lock.Token
                $report.Lines | ForEach-Object { Say $_ }
                Write-TextFile (Join-Path $evidence 'restore-report.txt') (($report.Lines -join "`n") + "`n")
                $restored = $report.Ok
                if ($restored) { Say 'RESTORED: DATA and SAV equal the state before the session.' }
                else { $exitCode = 3; Say "RESTORE INCOMPLETE: run -RestoreOnly `"$session`" after closing every game window." }
            }
        } catch {
            $exitCode = 3
            Say "RESTORE FAILED: $(Format-Failure $_) - run -RestoreOnly `"$session`"."
        }
    }
    Exit-CloneLock $lock
}

# ---- after the session -------------------------------------------------------------------------------------
if ($failure) { throw $failure }
if ($deferred -or !$restored) { exit $(if ($exitCode) { $exitCode } else { 3 }) }
try {
    $verifyNote = 'skipped (-NoVerify)'
    if (!$NoVerify -and $campaign -and (Test-Path -LiteralPath $campaign -PathType Container)) {
        Import-Module (Join-Path $PSScriptRoot 'Es2Bazel.psm1') -Force
        $bazel = Resolve-Es2Bazel
        Say 'Verifying the own-menu campaign against the original VSHELL harness (bazel test //shell:chain_real_test) ...'
        Push-Location $repository
        try {
            $verifyLog = Join-Path $evidence 'chain-verify.log'
            $verify = Invoke-Native $bazel @("--output_base=$BazelOutputBase", 'test', '//shell:chain_real_test', "--repo_contents_cache=$bazelContents",
                '--lockfile_mode=error', "--test_env=ES2_CHAIN_VERIFY=$campaign", '--test_output=all')
            $verifyOutput = $verify.Text
            $verifyCode = $verify.Code
        } finally { Pop-Location }
        Write-TextFile $verifyLog $verifyOutput
        $verifyNote = "bazel exit $verifyCode (" + $(if ($verifyCode -eq 0) { 'PASSED' } else { 'FAILED: read chain-verify.log' }) + ')'
        ($verifyOutput -split "`r?`n") | Where-Object { $_ -match '"verify"|^verify|PASSED|FAILED|results_steps|preparations_equal' } | Select-Object -Last 6 | ForEach-Object { Say "  $_" }
        if ($verifyCode -ne 0) { $exitCode = 4 }
    }
    Say "Verification: $verifyNote"
    $checklist = Get-Checklist $evidence $session
    Write-TextFile (Join-Path $evidence 'checklist.txt') ($checklist + "`n")
    Get-Content -LiteralPath (Join-Path $evidence 'summary.txt') | ForEach-Object { Say $_ }
    if (!$Unattended) { Say $checklist }
} catch {
    Say "Post-session step failed: $(Format-Failure $_)"
    if (!$exitCode) { $exitCode = 4 }
}

# ---- the verdict of the unattended run -----------------------------------------------------------------------
if ($Unattended) {
    $verdict = [ordered]@{schema = 1; suite = $Suite; installation = $installation; session = $session; campaign = $campaign; hooks = $Hooks
        prepared_proxy = $PreparedProxy; sim = $Sim; timed_out = $timedOut; timeout_stops = @($stopLines)}
    $problems = New-Object System.Collections.Generic.List[string]
    try {
        if ($timedOut) { $problems.Add("the run did not end within $TimeoutSeconds s (processes stopped: timeout-stops.txt)") }
        # 1. The launcher events of one mission.
        $logFile = Join-Path $evidence 'launcher-run1.log'
        $events = if (Test-Path -LiteralPath $logFile -PathType Leaf) { @(Get-LauncherEvents $logFile) } else { @() }
        $verdict.launcher_events = $events
        $expectedEvents = if ($Sim -eq 'own') { $unattendedEventsOwnSim } else { $unattendedEvents }
        $verdict.launcher_sequence = if (($events -join "`n") -ceq ($expectedEvents -join "`n")) { 'expected' } else { 'DIFFERENT' }
        if ($verdict.launcher_sequence -ne 'expected') { $problems.Add("launcher events differ from the one-mission sequence: $($events -join ' | ')") }
        # 2. The files DBSIM ran are those of the golden's run.
        $pins = $unattendedPins[$Suite]
        $pinProblems = New-Object System.Collections.Generic.List[string]
        $chainFile = Join-Path $evidence 'chain.json'
        $verdict.launch_kind = $null
        if (Test-Path -LiteralPath $chainFile -PathType Leaf) {
            $first = @((Get-Content -LiteralPath $chainFile -Raw | ConvertFrom-Json).missions)[0]
            if (!$first) { $pinProblems.Add('no mission in chain.json') }
            else {
                # When the clone's slot 10 holds its copies, CONTINUE launches them as VSHELL does (no
                # preparation): missions[0] is a from_slot record whose 'files' hold the slot's copy of player.mec, and
                # the files DBSIM was handed (the copies plus 0x40f68c's mission.var and player.mec) are 'launch_files'.
                $fromSlot = [bool]($first.PSObject.Properties['from_slot'] -and $first.from_slot)
                $verdict.launch_kind = if ($fromSlot) { 'from_slot' } else { 'prepared' }
                $groups = if ($fromSlot) { @('launch_files') } else { @('files', 'launch_files') }
                if ($fromSlot -and (!$first.PSObject.Properties['launch_files'] -or !$first.launch_files)) {
                    $pinProblems.Add('missions[0] is a from_slot record without launch_files (the files DBSIM was handed are not recorded)')
                }
                foreach ($group in $groups) {
                    if (!$first.PSObject.Properties[$group] -or !$first.$group) { continue }
                    foreach ($n in $pins.Keys) {
                        $have = if ($first.$group.PSObject.Properties[$n]) { [string]$first.$group.$n.sha256 } else { '' }
                        if ($have -ine $pins[$n]) { $pinProblems.Add("missions[0].$group.$n = $(if ($have) { $have } else { '(absent)' }), not $($pins[$n])") }
                    }
                }
            }
        } else { $pinProblems.Add('no chain.json (no campaign)') }
        $verdict.launch_files = if ($pinProblems.Count) { 'DIFFERENT' } else { 'pinned' }
        $verdict.launch_file_problems = @($pinProblems)
        foreach ($p in $pinProblems) { $problems.Add("launch files: $p") }
        # 3. DBSIM's tick log and outputs against the golden (oracle_regression.py, as run-oracle-regression.ps1 -Mode check).
        $normalized = Join-Path $runDirectory "$Suite-normalized.json"
        $comparisonReport = Join-Path $runDirectory "$Suite-comparison.json"
        $verdict.dbsim = 'DIFFERENT'
        if (Test-Path -LiteralPath $tickLog -PathType Leaf) {
            $normalize = Invoke-Native $Python @('-B', $regressionScript, 'normalize', $tickLog, $normalized, '--outputs', (Join-Path $runDirectory "$Suite-output"))
            if ($normalize.Code -ne 0) { $problems.Add("normalization failed ($($normalize.Code)): $($normalize.Text)") }
            else {
                $compareRun = Invoke-Native $Python @('-B', $regressionScript, 'compare', $golden, $normalized, '--report', $comparisonReport)
                Write-TextFile (Join-Path $runDirectory "$Suite-compare.txt") ($compareRun.Text + "`n")
                if (Test-Path -LiteralPath $comparisonReport -PathType Leaf) {
                    $comparison = Get-Content -LiteralPath $comparisonReport -Raw | ConvertFrom-Json
                    $verdict.comparison = [ordered]@{identical = $comparison.identical; common_ticks = $comparison.common_ticks; golden_ticks = $comparison.golden_ticks
                        run_ticks = $comparison.run_ticks; golden_watch_changes = $comparison.golden_watch_changes; run_watch_changes = $comparison.run_watch_changes
                        problems = @($comparison.problems | ForEach-Object { [string]$_.kind })}
                }
                if ($compareRun.Code -eq 0 -and $verdict.comparison -and $verdict.comparison.identical) { $verdict.dbsim = 'identical' }
                else { $problems.Add("DBSIM differs from the $Suite golden ($comparisonReport)") }
            }
        } else { $problems.Add("no tick log: $tickLog") }
        if ($verdict.dbsim -eq 'identical') {
            # Evidence policy (check.ps1): the raw tick log of an identical run goes, the normalized form and the comparison stay.
            $verdict.raw_tick_log_bytes_removed = (Get-Item -LiteralPath $tickLog).Length
            Remove-Item -LiteralPath $tickLog -Force
        }
        # 4. The chain verify (chain_real_test with ES2_CHAIN_VERIFY): every results step and preparation equal the original.
        if ($NoVerify) { $verdict.chain_verify = 'skipped (-NoVerify)' }
        else {
            $line = $null
            if ($verifyOutput) { $line = @(($verifyOutput -split "`r?`n") | Where-Object { $_.TrimStart().StartsWith('{') -and $_ -match '"results_steps"' -and $_ -match '"directory"' }) | Select-Object -Last 1 }
            $parsed = $null
            if ($line) { try { $parsed = $line.Trim() | ConvertFrom-Json } catch { $parsed = $null } }
            if ($parsed) {
                $verdict.chain_verify = [ordered]@{bazel_exit = $verifyCode; results_steps = $parsed.results_steps; preparations = $parsed.preparations; preparations_equal = $parsed.preparations_equal}
                if ($verifyCode -ne 0 -or [int]$parsed.preparations_equal -ne [int]$parsed.preparations -or [int]$parsed.preparations -lt 1 -or [int]$parsed.results_steps -lt 1) {
                    $problems.Add("chain verify: bazel exit $verifyCode, results steps $($parsed.results_steps), preparations $($parsed.preparations_equal) of $($parsed.preparations) equal")
                }
            } else {
                $verdict.chain_verify = [ordered]@{bazel_exit = $verifyCode; results_steps = $null}
                $problems.Add("chain verify: no verify line (bazel exit $verifyCode; chain-verify.log)")
            }
        }
        # 5. es2shell's headless reports (-Shell gui): each start ran its script without error and answered as VSHELL.
        if ($Shell -eq 'gui') {
            $verdict.shell = 'gui'
            $reports = [ordered]@{}
            foreach ($key in $guiReportExpect.Keys) {
                $path = Join-Path $evidence "shell-report-$key.txt"
                $lines = if (Test-Path -LiteralPath $path -PathType Leaf) { @(Get-Content -LiteralPath $path) } else { @() }
                $end = @($lines | Where-Object { $_ -match '^end ' }) | Select-Object -Last 1
                $entry = [ordered]@{end = $end; exit = $null; errors = $null; error_lines = @($lines | Where-Object { $_ -match '^error ' } | Select-Object -First 5)}
                if ($end -and $end -match '\berrors=(\d+) exit=(-?\d+)') { $entry.errors = [int]$Matches[1]; $entry.exit = [int]$Matches[2] }
                $reports[$key] = $entry
                if (!$end) { $problems.Add("headless report $($key.ToUpperInvariant()): no end line ($path)") }
                elseif ($entry.errors -ne 0 -or $entry.exit -ne $guiReportExpect[$key]) {
                    $problems.Add("headless report $($key.ToUpperInvariant()): exit $($entry.exit) (expected $($guiReportExpect[$key])), $($entry.errors) script error(s): $end")
                }
            }
            $verdict.shell_reports = $reports
            $verdict.shell_final = $reports['x3'].end
        } else { $verdict.shell = 'text' }
    } catch { $problems.Add("judgement failed: $(Format-Failure $_)") }
    $verdict.problems = @($problems)
    $verdict.verdict = if ($problems.Count) { 'FAILED' } else { 'PASSED' }
    Write-JsonFile (Join-Path $evidence 'unattended.json') $verdict
    $verifyText = if ($verdict.chain_verify -is [string]) { $verdict.chain_verify } elseif ($null -ne $verdict.chain_verify.results_steps) {
        "chain verify $($verdict.chain_verify.preparations_equal) of $($verdict.chain_verify.preparations) preparations equal, $($verdict.chain_verify.results_steps) results step(s)" } else { 'chain verify: no result' }
    $kindText = if ($verdict.launch_kind -eq 'from_slot') { ' (slot 10''s copies)' } else { '' }
    $shellText = ''
    if ($Shell -eq 'gui' -and $verdict.shell_reports) {
        $r1 = $verdict.shell_reports['x1']; $r3 = $verdict.shell_reports['x3']
        $shellText = "; es2shell headless X1 exit $($r1.exit), X3 exit $($r3.exit), $([int]$r1.errors + [int]$r3.errors) script error(s)"
    }
    $simText = if ($Sim -eq 'own') { ' (es2sim)' } else { '' }
    Say ("UNATTENDED {0}: launcher sequence {1}; launch files {2}{6}; DBSIM{8} {3} to the {4} golden; {5}{7}" -f $verdict.verdict, $verdict.launcher_sequence, $verdict.launch_files, $verdict.dbsim, $Suite, $verifyText, $kindText, $shellText, $simText)
    foreach ($p in $problems) { Say "  problem: $p" }
    Say "Evidence: $evidence\unattended.json"
    if ($timedOut) { $exitCode = 5 }
    elseif ($problems.Count -and !$exitCode) { $exitCode = 6 }
}
exit $exitCode
