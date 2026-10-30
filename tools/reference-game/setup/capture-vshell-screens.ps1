[CmdletBinding()]
param(
    [string]$DataRoot = '',
    # The data ISO's directory (below the es2 checkout's build, not in DataRoot).
    [string]$ExtractedRoot = '',
    [string]$InstallName = 'calibration-r111',
    # The tour: runs with start state, VSHELL arguments and journal (input-scripts/vshell/tour.json).
    [string]$Tour = '',
    # Only these runs of the tour (names); default: every enabled run.
    [string[]]$Run = @(),
    # Prints the plan (resolved journals included) and touches nothing.
    [switch]$DryRun,
    # A session directory (logs\vshell-screens-<stamp>) of an interrupted session: restores its backup and nothing else.
    [string]$RestoreOnly = '',
    # A session directory: rebuilds frames.json from its runs\*\capture.jsonl and run.json and nothing else.
    [string]$SummarizeOnly = '',
    # Per run: how long VSHELL may run before this script stops its own process (PID, path and start time checked).
    [ValidateRange(10, 3600)][int]$RunTimeoutSeconds = 600,
    # Raw frames and colour tables (by hash) into the session's ignored private\dumps directory.
    [switch]$Dump,
    # A record for every present, not only for changed frames.
    [switch]$Every,
    # Capture record schema 2: the QueryPerformanceCounter value of every record (start record: "schema":2 and
    # "qpc_frequency"). frames.json keeps each frame's "qpc" (omitted for schema-1 logs). vshell_ui.md section 42.5.
    [switch]$Qpc,
    # Without the 256COLOR compatibility layer (start-native.ps1 -UseNativeColors).
    [switch]$NativeColors,
    # Answers VSHELL's colour-depth notice (a MessageBoxA with OK, shown in entry modes 0 and 1 when the
    # desktop is not 8 bits deep, vshell_process.md section 4 step 11) by pressing its button; the box's modal loop
    # does not run through the pump, so the journal cannot answer it. Waits at most this many seconds per run for
    # the box (0 = do not look for it).
    [ValidateRange(0, 600)][int]$ConfirmNoticeSeconds = 0,
    # Fixture override: a directory used as the CD root (no image is mounted).
    [string]$CdRoot = '',
    [string]$Layout = ''
)
if (!$DataRoot -or !$ExtractedRoot) {
    Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')
    $es2 = Get-Es2Layout
    if (!$DataRoot) { $DataRoot = $es2.DataRepository }
    if (!$ExtractedRoot) { $ExtractedRoot = $es2.Extracted }
}
# The VSHELL capture tour. Backs up the clone's DATA and SAV, then per run puts them back to the backup,
# applies the run's start state, starts VSHELL.EXE with the proxy's capture and journal on
# (ES2_VSHELL_CAPTURE_CONFIG, proxy/vshell_capture.h), waits for it to end (stops only its own process after
# -RunTimeoutSeconds), and finally restores DATA and SAV (always, in a finally). Result:
# <DataRoot>\logs\vshell-screens-<stamp>\frames.json (hashes, screens, states, journal steps; never pixels).
# Contract: docs/reverse-engineering/vshell_presentation.md, section "capture tour".
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1') -Force
$utf8 = New-Object System.Text.UTF8Encoding($false)
$folders = @('DATA', 'SAV')
$referenceSha256 = '1534AA8F732BDECD826484D893AD58B8A5E6EBD4D25AB3158A2469B8F69BA16E'
if (!$Tour) { $Tour = Join-Path $PSScriptRoot '..\input-scripts\vshell\tour.json' }
if (!$Layout) { $Layout = Join-Path $repository 'shell\data\vshell_layout.json' }

function Say([string]$Text) { Write-Host "[vshell-screens] $Text" }
function Write-TextFile([string]$Path, [string]$Text) { [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), $Text, $utf8) }
function Write-JsonFile([string]$Path, $Object) { Write-TextFile $Path (($Object | ConvertTo-Json -Depth 10) + "`n") }
function New-IgnoredDirectory([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    Write-TextFile (Join-Path $Path '.gitignore') "*`n"
}
function Confirm-NoticeBox([Diagnostics.Process]$Process, [int]$Seconds) {
    # Looks for a dialog-class (#32770) top-level window of this run's own process (the PID of the process this
    # script started, still running) and presses its button once: WM_COMMAND with ID 2, the ID an OK-only message
    # box gives its single button (a posted ID 1 leaves the box open; ID 2 closes it and MessageBoxA returns IDOK,
    # checked with the mock game's box). Result: what was found; never pressed twice.
    if (!('Es2.VshellNotice' -as [type])) {
        Add-Type -Namespace 'Es2' -Name 'VshellNotice' -MemberDefinition @'
private delegate bool EnumProc(System.IntPtr window, System.IntPtr data);
[System.Runtime.InteropServices.DllImport("user32.dll")]
private static extern bool EnumWindows(EnumProc callback, System.IntPtr data);
[System.Runtime.InteropServices.DllImport("user32.dll")]
private static extern uint GetWindowThreadProcessId(System.IntPtr window, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll")]
private static extern bool IsWindowVisible(System.IntPtr window);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
private static extern int GetClassNameW(System.IntPtr window, System.Text.StringBuilder name, int size);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
private static extern int GetWindowTextW(System.IntPtr window, System.Text.StringBuilder text, int size);
[System.Runtime.InteropServices.DllImport("user32.dll")]
private static extern System.IntPtr GetDlgItem(System.IntPtr dialog, int id);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PostMessageW(System.IntPtr window, uint message, System.IntPtr wParam, System.IntPtr lParam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool IsWindow(System.IntPtr window);
public static string Text(System.IntPtr window)
{
    System.Text.StringBuilder b = new System.Text.StringBuilder(1024);
    GetWindowTextW(window, b, b.Capacity);
    return b.ToString();
}
public static string ItemText(System.IntPtr dialog, int id)
{
    System.IntPtr item = GetDlgItem(dialog, id);
    return item == System.IntPtr.Zero ? "" : Text(item);
}
public static System.IntPtr FindDialog(uint pid)
{
    System.IntPtr found = System.IntPtr.Zero;
    EnumWindows(delegate (System.IntPtr w, System.IntPtr d) {
        uint owner;
        GetWindowThreadProcessId(w, out owner);
        if (owner != pid || !IsWindowVisible(w)) return true;
        System.Text.StringBuilder c = new System.Text.StringBuilder(64);
        GetClassNameW(w, c, c.Capacity);
        if (c.ToString() != "#32770") return true;
        found = w;
        return false;
    }, System.IntPtr.Zero);
    return found;
}
'@
    }
    $result = [ordered]@{looked_seconds = $Seconds; found = $false; title = $null; text = $null; waited_ms = 0; posted = $false; closed = $false}
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds -and !$Process.HasExited) {
        $box = [Es2.VshellNotice]::FindDialog([uint32]$Process.Id)
        if ($box -ne [IntPtr]::Zero) {
            $result.found = $true; $result.waited_ms = [int]$timer.ElapsedMilliseconds
            $result.title = [Es2.VshellNotice]::Text($box)
            $result.text = [Es2.VshellNotice]::ItemText($box, 0xFFFF)   # the message box's static text
            $result.posted = [Es2.VshellNotice]::PostMessageW($box, 0x0111, [IntPtr]2, [IntPtr]::Zero)   # WM_COMMAND, ID 2
            for ($i = 0; $i -lt 50 -and [Es2.VshellNotice]::IsWindow($box); $i++) { Start-Sleep -Milliseconds 100 }
            $result.closed = ![Es2.VshellNotice]::IsWindow($box)
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if (!$result.found) { $result.waited_ms = [int]$timer.ElapsedMilliseconds }
    return [pscustomobject]$result
}
function Get-Sha([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }

# ---- trees -------------------------------------------------------------------------------------------
function Get-TreeManifest([string]$Root) {
    $files = @{}; $dirs = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $Root -PathType Container) {
        $prefix = $Root.TrimEnd('\') + '\'
        foreach ($item in (Get-ChildItem -LiteralPath $Root -Recurse -Force)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $($item.FullName)" }
            $relative = $item.FullName.Substring($prefix.Length)
            if ($item.PSIsContainer) { $dirs.Add($relative) } else { $files[$relative] = Get-Sha $item.FullName }
        }
    }
    return [pscustomobject]@{Files = $files; Dirs = @($dirs)}
}
function Get-ManifestDigest($Manifest) {
    $lines = foreach ($key in ($Manifest.Files.Keys | Sort-Object { $_.ToLowerInvariant() })) { $key.ToLowerInvariant() + '|' + $Manifest.Files[$key] }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes((@($lines) -join "`n"))))).Replace('-', '') } finally { $sha.Dispose() }
}
function Sync-Tree([string]$BackupRoot, [string]$TargetRoot) {
    # Makes TargetRoot equal to BackupRoot (files and directories). Returns the number of actions.
    $want = Get-TreeManifest $BackupRoot; $have = Get-TreeManifest $TargetRoot; $actions = 0
    foreach ($relative in @($have.Files.Keys)) {
        if (!$want.Files.ContainsKey($relative)) { Remove-Item -LiteralPath (Join-Path $TargetRoot $relative) -Force; $actions++ }
    }
    foreach ($relative in @($want.Files.Keys)) {
        if ($have.Files.ContainsKey($relative) -and $have.Files[$relative] -eq $want.Files[$relative]) { continue }
        $destination = Join-Path $TargetRoot $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $BackupRoot $relative) -Destination $destination -Force; $actions++
    }
    $wanted = @{}; foreach ($d in $want.Dirs) { $wanted[$d] = $true }
    foreach ($d in ($have.Dirs | Sort-Object Length -Descending)) {
        if (!$wanted.ContainsKey($d) -and (Test-Path -LiteralPath (Join-Path $TargetRoot $d))) { Remove-Item -LiteralPath (Join-Path $TargetRoot $d) -Recurse -Force; $actions++ }
    }
    foreach ($d in $want.Dirs) { if (!(Test-Path -LiteralPath (Join-Path $TargetRoot $d))) { New-Item -ItemType Directory -Force -Path (Join-Path $TargetRoot $d) | Out-Null; $actions++ } }
    return $actions
}
function Restore-Session([string]$Session, [string]$Installation) {
    # DATA and SAV back to the verified backup; returns report lines and whether both trees equal it.
    $record = Get-Content -LiteralPath (Join-Path $Session 'restore.json') -Raw | ConvertFrom-Json
    $lines = New-Object System.Collections.Generic.List[string]; $ok = $true
    foreach ($folder in $folders) {
        $backup = Join-Path $Session "backup\$folder"
        if ((Get-ManifestDigest (Get-TreeManifest $backup)) -ne $record.digests.$folder) { throw "The backup of $folder in $Session does not match restore.json; nothing was restored." }
    }
    foreach ($folder in $folders) {
        $actions = Sync-Tree (Join-Path $Session "backup\$folder") (Join-Path $Installation $folder)
        $digest = Get-ManifestDigest (Get-TreeManifest (Join-Path $Installation $folder))
        $same = $digest -eq $record.digests.$folder
        if (!$same) { $ok = $false }
        $lines.Add(("{0}: {1} action(s), tree {2} {3} before-session tree {4}" -f $folder, $actions, $digest, $(if ($same) { '==' } else { '!=' }), $record.digests.$folder))
    }
    return [pscustomobject]@{Ok = $ok; Lines = @($lines)}
}

# ---- installation and tour ---------------------------------------------------------------------------
function Resolve-Installation([string]$Name) {
    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') { throw 'InstallName must be a plain installation name.' }
    if ($Name -ieq 'native-r111') { throw 'native-r111 is the primary installation; the capture tour runs on a clone such as calibration-r111. Nothing was touched.' }
    $path = [IO.Path]::GetFullPath((Join-Path (Get-CloneInstallRoot $DataRoot) $Name))
    foreach ($item in @('DATA', 'SAV', 'VSHELL.EXE', 'ddraw.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $path $item))) { throw "$path has no $item; not a prepared game installation (the proxy ddraw.dll is deployed by check.ps1 / configure-media.ps1)." }
    }
    return $path
}
function Get-ControlPoints {
    # control id -> centre of its screen_rect (vshell_widgets.md section 4.5).
    $table = Get-Content -LiteralPath $Layout -Raw | ConvertFrom-Json
    $points = @{}
    foreach ($screen in $table.screens) {
        foreach ($control in $screen.controls) {
            $r = $control.screen_rect
            if ($r -and @($r).Count -eq 4) { $points[[string]$control.id] = @([int][Math]::Floor(([int]$r[0] + [int]$r[2]) / 2), [int][Math]::Floor(([int]$r[1] + [int]$r[3]) / 2)) }
        }
    }
    return $points
}
function Resolve-Journal([string]$Path, $Points) {
    # Returns the journal lines with every "control" resolved into x/y; refuses unknown controls and bad lines.
    $out = New-Object System.Collections.Generic.List[string]; $number = 0; $ops = @{}
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        $number++
        if (!$line.Trim()) { continue }
        try { $entry = $line | ConvertFrom-Json } catch { throw "$Path line ${number}: not JSON." }
        if ($entry.op -notin @('mark', 'wait', 'snap', 'click', 'key', 'close')) { throw "$Path line ${number}: unknown op '$($entry.op)'." }
        if ($entry.PSObject.Properties['control']) {
            $id = [string]$entry.control
            if (!$Points.ContainsKey($id)) { throw "$Path line ${number}: control '$id' has no screen_rect in $Layout." }
            $entry | Add-Member -NotePropertyName x -NotePropertyValue $Points[$id][0] -Force
            $entry | Add-Member -NotePropertyName y -NotePropertyValue $Points[$id][1] -Force
        }
        $ops[[string]$entry.op] = 1 + [int]$ops[[string]$entry.op]
        $out.Add(($entry | ConvertTo-Json -Compress -Depth 4))
    }
    return [pscustomobject]@{Lines = @($out); Ops = $ops}
}
function Get-TourRuns {
    $tourData = Get-Content -LiteralPath $Tour -Raw | ConvertFrom-Json
    if ($tourData.schema -ne 1) { throw "Unknown tour schema in ${Tour}: $($tourData.schema)" }
    $selected = @($tourData.runs | Where-Object { if ($Run.Count) { $_.name -in $Run } else { $_.enabled -ne $false } })
    foreach ($name in $Run) { if (!($tourData.runs | Where-Object { $_.name -eq $name })) { throw "The tour has no run '$name'." } }
    if (!$selected.Count) { throw 'No run selected.' }
    $points = Get-ControlPoints
    foreach ($r in $selected) {
        if ([string]$r.name -notmatch '^[A-Za-z0-9_-]+$') { throw "Run name '$($r.name)' must be a plain name." }
        foreach ($c in @($r.copy)) {
            if (!$c) { continue }
            foreach ($p in @([string]$c.from, [string]$c.to)) { if ($p -notmatch '^(DATA|SAV)[\\/][A-Za-z0-9_.\\/-]+$' -or $p -match '\.\.') { throw "Run $($r.name): copy path '$p' must lie in DATA or SAV." } }
        }
        $journalPath = Join-Path (Split-Path -Parent $Tour) ([string]$r.journal)
        $resolved = Resolve-Journal $journalPath $points
        [pscustomobject]@{Name = [string]$r.name; StartState = [string]$r.start_state; Copy = @($r.copy | Where-Object { $_ }); Arguments = @($r.arguments | ForEach-Object { [string]$_ })
            Journal = $journalPath; Lines = $resolved.Lines; Ops = $resolved.Ops}
    }
}

# ---- frames.json -------------------------------------------------------------------------------------
function Read-CaptureLog([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    # A line the proxy did not finish (a stopped process) is skipped.
    return @(foreach ($line in (Get-Content -LiteralPath $Path)) { if ($line.Trim()) { try { $line | ConvertFrom-Json } catch { } } })
}
function Write-Frames([string]$Session) {
    $sessionRecord = Get-Content -LiteralPath (Join-Path $Session 'session.json') -Raw | ConvertFrom-Json
    $runs = New-Object System.Collections.Generic.List[object]; $screens = New-Object System.Collections.Generic.List[object]
    foreach ($runRecord in @($sessionRecord.runs)) {
        $dir = Join-Path $Session "runs\$($runRecord.name)"
        $info = Get-Content -LiteralPath (Join-Path $dir 'run.json') -Raw | ConvertFrom-Json
        $records = Read-CaptureLog (Join-Path $dir 'capture.jsonl')
        $counts = [ordered]@{}
        foreach ($k in @('present', 'clear', 'palette', 'snap', 'step', 'focus_injected')) { $counts[$k] = @($records | Where-Object { $_.kind -eq $k }).Count }
        $frames = @($records | Where-Object { $_.kind -in @('present', 'clear', 'snap') } | ForEach-Object {
            $frame = [ordered]@{seq = $_.seq; kind = $_.kind; pass = $_.pass; step = $_.step; screen = $_.screen; state = $_.state; site = $_.site; dd = $_.dd
                index_sha256 = $_.index_sha256; palette_sha256 = $_.palette_sha256}
            # Schema 2 records carry "qpc"; a schema-1 log has none and its frames stay as they were.
            if ($null -ne $_.PSObject.Properties['qpc']) { $frame['qpc'] = [uint64]$_.qpc }
            $frame })
        $snaps = @($records | Where-Object { $_.kind -eq 'snap' } | ForEach-Object {
            [ordered]@{run = $info.name; screen = $_.screen; state = $_.state; step = $_.step; index_sha256 = $_.index_sha256; palette_sha256 = $_.palette_sha256} })
        foreach ($s in $snaps) { $screens.Add($s) }
        $start = $records | Where-Object { $_.kind -in @('start', 'refused') } | Select-Object -First 1
        $end = $records | Where-Object { $_.kind -eq 'end' } | Select-Object -First 1
        $status = if (!$records.Count) { 'no-capture-log' } elseif ($start.kind -eq 'refused') { 'refused' } elseif (!$end) { 'no-end-record' } elseif (!$end.journal_done) { 'journal-incomplete' } else { 'complete' }
        $runs.Add([ordered]@{name = $info.name; start_state = $info.start_state; copy = $info.copy; arguments = $info.arguments; journal = $info.journal
            journal_sha256 = $info.journal_sha256; sav_at_launch = $info.sav_at_launch; process = $info.process; status = $status
            capture_start = $start; capture_end = $end; counts = $counts; screens = $snaps; frames = $frames})
    }
    $result = [ordered]@{schema = 1; kind = 'es2-vshell-screens'; created_utc = [DateTime]::UtcNow.ToString('o'); session = $Session
        installation = $sessionRecord.installation; vshell_sha256 = $sessionRecord.vshell_sha256; proxy_sha256 = $sessionRecord.proxy_sha256
        compatibility_layer = $sessionRecord.compatibility_layer; tour = $sessionRecord.tour; runs = $runs.ToArray(); screens = $screens.ToArray()}
    Write-JsonFile (Join-Path $Session 'frames.json') $result
    return $result
}

# ---- modes without a session -------------------------------------------------------------------------
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
if ($SummarizeOnly) {
    $frames = Write-Frames ([IO.Path]::GetFullPath($SummarizeOnly).TrimEnd('\'))
    foreach ($r in $frames.runs) { Say ("{0}: {1}, {2} snap(s), {3} frame record(s)" -f $r.name, $r.status, $r.counts.snap, @($r.frames).Count) }
    exit 0
}
if ($RestoreOnly) {
    $session = [IO.Path]::GetFullPath($RestoreOnly).TrimEnd('\')
    $record = Get-Content -LiteralPath (Join-Path $session 'restore.json') -Raw | ConvertFrom-Json
    $installation = Resolve-Installation (Split-Path -Leaf ([string]$record.installation))
    $lock = Enter-CloneLock -DataRoot $DataRoot -InstallName (Split-Path -Leaf $installation) -Purpose 'vshell screens restore'
    try {
        Assert-CloneIdle -Installation $installation -Action 'restoring'
        $report = Restore-Session $session $installation
        $report.Lines | ForEach-Object { Say $_ }
        if ($report.Ok) { Say 'RESTORED.'; exit 0 }
        exit 3
    } finally { Exit-CloneLock $lock }
}

# ---- plan --------------------------------------------------------------------------------------------
$installation = Resolve-Installation $InstallName
$runs = @(Get-TourRuns)
$vshell = Join-Path $installation 'VSHELL.EXE'
$plan = [ordered]@{installation = $installation; tour = [IO.Path]::GetFullPath($Tour); layout = [IO.Path]::GetFullPath($Layout)
    compatibility_layer = $(if ($NativeColors) { '' } else { '256COLOR' }); run_timeout_seconds = $RunTimeoutSeconds; confirm_notice_seconds = $ConfirmNoticeSeconds; dump = [bool]$Dump; every = [bool]$Every; qpc = [bool]$Qpc
    runs = @($runs | ForEach-Object { [ordered]@{name = $_.Name; start_state = $_.StartState; copy = $_.Copy; arguments = $_.Arguments; journal = $_.Journal; ops = $_.Ops; lines = $_.Lines} })}
if ($DryRun) {
    Say "DRY RUN for $installation (nothing is created, started or changed)."
    $plan | ConvertTo-Json -Depth 8
    exit 0
}

# ---- session -----------------------------------------------------------------------------------------
$manifestPath = Join-Path $installation 'installation-manifest.json'
$manifest = if (Test-Path -LiteralPath $manifestPath) { Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json } else { $null }
$expected = @($manifest.hashes | Where-Object { $_.file -eq 'VSHELL.EXE' })
$vshellSha = Get-Sha $vshell
if ($expected.Count -ne 1 -or $expected[0].sha256 -ne $vshellSha) { throw "VSHELL.EXE of $installation does not match its installation-manifest.json; nothing was started." }
if ($vshellSha -ne $referenceSha256) { Say "Note: VSHELL.EXE is not the reference $referenceSha256; the proxy will refuse the capture." }
Assert-CloneIdle -Installation $installation -Action 'starting the capture tour'
Assert-CloneLockAvailable -DataRoot $DataRoot -InstallName $InstallName
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$session = Join-Path $DataRoot "logs\vshell-screens-$stamp"
$lock = Enter-CloneLock -DataRoot $DataRoot -InstallName $InstallName -Purpose 'vshell capture tour'
$environment = Get-RunnerEnvironment
$failure = $null; $restored = $false; $backedUp = $false
$sessionRecord = [ordered]@{schema = 1; stamp = $stamp; installation = $installation; vshell_sha256 = $vshellSha; proxy_sha256 = (Get-Sha (Join-Path $installation 'ddraw.dll'))
    compatibility_layer = $plan.compatibility_layer; confirm_notice_seconds = $ConfirmNoticeSeconds; tour = $plan.tour; runs = @()}
try {
    New-Item -ItemType Directory -Force -Path $session | Out-Null
    New-IgnoredDirectory (Join-Path $session 'backup')
    New-IgnoredDirectory (Join-Path $session 'private')
    $digests = [ordered]@{}
    foreach ($folder in $folders) {
        Copy-Item -LiteralPath (Join-Path $installation $folder) -Destination (Join-Path $session "backup\$folder") -Recurse -Force
        $digests[$folder] = Get-ManifestDigest (Get-TreeManifest (Join-Path $installation $folder))
        if ((Get-ManifestDigest (Get-TreeManifest (Join-Path $session "backup\$folder"))) -ne $digests[$folder]) { throw "The backup copy of $folder differs from the source; nothing was started." }
    }
    Write-JsonFile (Join-Path $session 'restore.json') ([ordered]@{schema = 1; installation = $installation; started_utc = [DateTime]::UtcNow.ToString('o'); digests = $digests})
    $backedUp = $true
    Say "Backup verified: $session\backup (restore with -RestoreOnly '$session')."

    # The CD: the original installer's two-line DRIVE.CFG (in DATA, restored afterwards).
    $cd = $CdRoot
    if (!$cd) {
        $iso = Join-Path $ExtractedRoot 'earthsiege2-data.iso'
        $image = Get-DiskImage -ImagePath $iso
        if (!$image.Attached) { $image = Mount-DiskImage -ImagePath $iso -Access ReadOnly -PassThru }
        $volumes = @($image | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystemLabel -eq 'EARTHSIEGE2' })
        if ($volumes.Count -ne 1) { throw 'Expected exactly one mounted EARTHSIEGE2 data volume.' }
        $cd = $volumes[0].DriveLetter + ':'
    }
    $runRecords = New-Object System.Collections.Generic.List[object]
    foreach ($r in $runs) {
        $runDirectory = Join-Path $session "runs\$($r.Name)"
        New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
        foreach ($folder in $folders) { [void](Sync-Tree (Join-Path $session "backup\$folder") (Join-Path $installation $folder)) }
        foreach ($c in $r.Copy) { Copy-Item -LiteralPath (Join-Path $session ('backup\' + [string]$c.from)) -Destination (Join-Path $installation ([string]$c.to)) -Force }
        [IO.File]::WriteAllText((Join-Path $installation 'DATA\DRIVE.CFG'), "$cd`r`n$installation`r`n", [Text.Encoding]::ASCII)
        $savAtLaunch = [ordered]@{}
        foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $installation 'SAV') -File | Sort-Object Name)) { $savAtLaunch[$f.Name] = Get-Sha $f.FullName }
        $journal = Join-Path $runDirectory 'journal.jsonl'
        Write-TextFile $journal (($r.Lines -join "`n") + "`n")
        $dumpLine = ''
        if ($Dump) { $dumpDirectory = Join-Path $session "private\dumps\$($r.Name)"; New-Item -ItemType Directory -Force -Path $dumpDirectory | Out-Null; $dumpLine = "dump_dir=$dumpDirectory`r`n" }
        if ($Qpc) { $dumpLine += "qpc=1`r`n" }
        $ini = Join-Path $runDirectory 'capture.ini'
        [IO.File]::WriteAllText($ini, "[vshell_capture]`r`nlog=$(Join-Path $runDirectory 'capture.jsonl')`r`ncapture=1`r`nevery=$([int][bool]$Every)`r`njournal=$journal`r`nhold_focus=1`r`nblock_real_input=1`r`n$dumpLine", [Text.Encoding]::Unicode)
        $info = [ordered]@{name = $r.Name; start_state = $r.StartState; copy = $r.Copy; arguments = $r.Arguments; journal = $r.Journal
            journal_sha256 = (Get-Sha $journal); sav_at_launch = $savAtLaunch; notice = $null; process = $null}
        Write-JsonFile (Join-Path $runDirectory 'run.json') $info
        $runRecords.Add([ordered]@{name = $r.Name})
        $sessionRecord.runs = $runRecords.ToArray()
        Write-JsonFile (Join-Path $session 'session.json') $sessionRecord
        Say "Run $($r.Name): VSHELL.EXE $($r.Arguments -join ' ') (journal $(Split-Path -Leaf $r.Journal), timeout $RunTimeoutSeconds s) ..."
        $env:ES2_VSHELL_CAPTURE_CONFIG = $ini
        $env:__COMPAT_LAYER = $plan.compatibility_layer
        $notBefore = [DateTime]::UtcNow
        try {
            # window-lint: allow gui-child VSHELL.EXE (visible on purpose: the shell must hold the focus for its main loop, vshell_process.md section 8)
            $process = Start-Process -FilePath $vshell -ArgumentList $r.Arguments -WorkingDirectory $installation -PassThru
        } finally { Restore-RunnerEnvironment $environment }
        Register-CloneStartedProcess -Process $process
        $launch = Join-Path $runDirectory 'launch.json'
        Write-JsonFile $launch ([ordered]@{pid = $process.Id; exe = $vshell; process_start_utc = $process.StartTime.ToUniversalTime().ToString('o')})
        if ($ConfirmNoticeSeconds -gt 0) {
            $info.notice = Confirm-NoticeBox $process $ConfirmNoticeSeconds
            Say "Run $($r.Name): notice box found $($info.notice.found) after $($info.notice.waited_ms) ms, OK pressed $($info.notice.posted), closed $($info.notice.closed)."
        }
        $wait = Wait-CloneOwnedProcessExit -Installation $installation -LaunchReport $launch -NotBefore $notBefore -GraceSeconds $RunTimeoutSeconds
        $info.process = $wait
        Write-JsonFile (Join-Path $runDirectory 'run.json') $info
        Say "Run $($r.Name): $($wait.status), exit code $($wait.exit_code), stopped by this script: $($wait.stopped_by_runner)."
        Assert-CloneIdle -Installation $installation -Action 'the next run' -WaitSeconds 30
    }
} catch {
    $failure = $_
    Say "FAILED: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"
} finally {
    Restore-RunnerEnvironment $environment
    if ($backedUp) {
        try {
            Assert-CloneIdle -Installation $installation -Action 'restoring' -WaitSeconds 30
            $report = Restore-Session $session $installation
            $report.Lines | ForEach-Object { Say $_ }
            Write-TextFile (Join-Path $session 'restore-report.txt') (($report.Lines -join "`n") + "`n")
            $restored = $report.Ok
            Say $(if ($restored) { 'RESTORED: DATA and SAV equal the state before the session.' } else { "RESTORE INCOMPLETE: run -RestoreOnly '$session'." })
        } catch { Say "NOT RESTORED: $($_.Exception.Message) - run -RestoreOnly '$session' once no game process of the clone runs." }
    }
    Exit-CloneLock $lock
}
if ($failure) { throw $failure }
$frames = Write-Frames $session
foreach ($r in $frames.runs) { Say ("{0}: {1}, {2} snap(s), {3} frame record(s)" -f $r.name, $r.status, $r.counts.snap, @($r.frames).Count) }
Say "frames.json: $session\frames.json"
exit $(if ($restored) { 0 } else { 3 })
