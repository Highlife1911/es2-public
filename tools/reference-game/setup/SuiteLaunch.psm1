# Prepared-file transactions. No process is started by this module.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'CloneGuard.psm1')   # Get-CloneProcessImagePath
$script:LaunchFiles = @('script.dat','player.mec','mission.var','MISSION.STR')
$script:RestoreFiles = $script:LaunchFiles + @('results.dat','EXIT.CFG')
function Assert-SuiteName([string]$Name) {
    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$' -or $Name -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
        throw "Invalid plain suite/clone/launch name: $Name"
    }
}
function Assert-SuiteMockRoot([string]$DataRoot) {
    $marker=Join-Path $DataRoot '.es2-synthetic-fixture'
    if (!(Test-Path -LiteralPath $marker -PathType Leaf) -or [IO.File]::ReadAllText($marker).Trim() -cne 'synthetic suite fixtures only') {
        throw 'Mock mode requires a marked synthetic DataRoot; never use a real installation.'
    }
}
function Assert-SuitePlainPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $part = $full
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Links/junctions are refused: $part" }
        }
        $parent = [IO.Path]::GetDirectoryName($part)
        if ($parent -eq $part) { break }
        $part = $parent
    }
    return $full
}
function Assert-SuiteChild([string]$Path, [string]$Root) {
    $full = Assert-SuitePlainPath $Path
    $base = (Assert-SuitePlainPath $Root).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (!$full.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) { throw "Path must be inside $Root : $full" }
    return $full
}
function Get-SuiteInstallation([string]$DataRoot, [string]$InstallName, [switch]$DryRun) {
    Assert-SuiteName $InstallName
    if ($InstallName -ieq 'native-r111') { throw 'native-r111 is protected; use a measurement clone.' }
    $root = Get-CloneInstallRoot $DataRoot
    $install = Assert-SuiteChild (Join-Path $root $InstallName) $root
    if (!$DryRun) {
        $m = Get-Content -LiteralPath (Join-Path $install 'installation-manifest.json') -Raw | ConvertFrom-Json
        if (!$m.measurement_clone_of -or $m.destination -ne $install -or $m.measurement_clone_of -eq $install) { throw 'Expected an explicit separate measurement clone.' }
        $null = Assert-SuitePlainPath (Join-Path $install 'DATA')
        foreach ($name in $script:RestoreFiles) { $null = Assert-SuitePlainPath (Join-Path $install "DATA/$name") }
    }
    return $install
}
function Write-SuiteJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 16) + "`n", [Text.UTF8Encoding]::new($false))
}
function Resolve-SuiteInputPath([string]$Root, [string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Root $Path))
}
function Invoke-SuiteProcess([string]$Executable, [string[]]$Arguments) {
    # Explicit pipes and wait also work in Bazel's non-console test process.
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = ($Arguments | ForEach-Object {
        '"' + [regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1') + '"'
    }) -join ' '
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $result=@{status=$process.ExitCode;stdout=$stdout.Result;stderr=$stderr.Result}
    $process.Dispose()
    return $result
}
function Get-SuiteFileState([string]$Path) {
    $null = Assert-SuitePlainPath $Path
    if (!(Test-Path -LiteralPath $Path)) { return $null }
    $f = Get-Item -LiteralPath $Path
    if ($f.PSIsContainer) { throw "Expected a file: $Path" }
    return [ordered]@{size=$f.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Read-SuiteLaunch([string]$DataRoot, [string]$Name) {
    Assert-SuiteName $Name
    $dir = Assert-SuiteChild (Join-Path $DataRoot "oracle/launch/$Name") (Join-Path $DataRoot 'oracle/launch')
    $manifestPath = Assert-SuitePlainPath (Join-Path $dir 'manifest.json')
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.name -cne $Name -or @($manifest.files).Count -ne 4) { throw 'Invalid launch manifest schema, name or file count.' }
    $found = @{}
    foreach ($entry in $manifest.files) {
        if ($entry.file -cnotin $script:LaunchFiles -or $found.ContainsKey($entry.file)) { throw "Unexpected/duplicate launch member: $($entry.file)" }
        $state = Get-SuiteFileState (Join-Path $dir $entry.file)
        if (!$state -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or $state.sha256 -ine $entry.sha256 -or $state.size -ne $entry.size -or !$state.size) { throw "Launch hash/size mismatch: $($entry.file)" }
        $found[$entry.file] = $state
    }
    return [pscustomobject]@{name=$Name;directory=$dir;manifest_sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant();files=$manifest.files}
}
function Get-SuiteProcesses([string]$Install) {
    return @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('ES.EXE','VSHELL.EXE','DBSIM.EXE') -and $_.ExecutablePath -and
        [IO.Path]::GetDirectoryName($_.ExecutablePath) -ieq $Install
    })
}
function Stop-SuiteOwnedProcess([string]$Install, [string]$ReportPath, [DateTime]$NotBefore, [int]$WaitSeconds = 60) {
    # Reports must be fresh, and identify both path and start time. Never use a
    # historical PID or stop a shell process observed by the capture utility.
    $running = @(Get-SuiteProcesses $Install)
    if (!$running.Count) { return }
    if (!(Test-Path -LiteralPath $ReportPath)) { throw 'Game still running without an owned launch report; backup retained, no restore attempted.' }
    $r = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    $start = ([DateTime]$r.process_start_utc).ToUniversalTime()
    if ($r.exe -ine (Join-Path $Install 'DBSIM.EXE') -or $start -lt $NotBefore.ToUniversalTime().AddSeconds(-1)) { throw 'Unowned launch report; no process stopped.' }
    foreach ($p in $running) {
        if ($p.ProcessId -ne $r.pid -or $p.ExecutablePath -ine $r.exe) { throw 'Process identity changed; no process stopped.' }
        # The launcher records System.Diagnostics.Process.StartTime. Compare
        # through that API too, rather than CIM's differently serialized time.
        # The path is read with kernel32 (Get-CloneProcessImagePath), not from
        # Process.Path, which is incomplete while a process is still loading.
        $live=Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
        if (!$live) { continue }
        if ((Get-CloneProcessImagePath $p.ProcessId) -ine $r.exe -or $live.StartTime.ToUniversalTime() -ne $start) { throw 'Process identity changed; no process stopped.' }
    }
    foreach ($p in $running) {
        $live=Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
        if (!$live) { continue }
        if ((Get-CloneProcessImagePath $p.ProcessId) -ine $r.exe -or $live.StartTime.ToUniversalTime() -ne $start) { throw 'Process identity changed before stop; backup retained.' }
        Stop-Process -InputObject $live -ErrorAction Stop
        Wait-Process -Id $p.ProcessId -Timeout 15 -ErrorAction SilentlyContinue
    }
    # Under load a stopped DBSIM can stay listed longer than Wait-Process's 15 s
    # (check-r6-set3: timing-r111-p5 kept its launch set because the caller
    # skips the restore while the clone is busy). Poll until the clone is
    # empty; refuse only when it is still busy after $WaitSeconds.
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while (@(Get-SuiteProcesses $Install).Count) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Clone still busy $WaitSeconds s after the stop; backup retained." }
        Start-Sleep -Milliseconds 250
    }
}
# Tape suites (docs/reference-game/oracle_regression.md "Tape suites").
# DBSIM's option parser 0x45e758 knows four tape options (file_io.md, DBSIM
# recording and playback):
# -r<name> records <name>.tap in the working directory (the clone root),
# -p<name> plays <name>.tap back, -D plays the tape named by TAPES\DEMOLIST.STR
# and -d<name> opens <name>.dmp. All of them keep only the first name given, a
# missing playback tape is read through a null FILE*, and playback overwrites
# DATA\script.dat, player.mec, mission.var, restore.dat, object.str and writes
# TAPES\prefs.cfg and TAPES\keyjoy.cfg (0x45cd5c). The plan names what a suite
# transaction deploys, restores and must not capture; $null = no tape option.
$script:TapeRestoreFiles = @('DATA/restore.dat','DATA/object.str','TAPES/prefs.cfg','TAPES/keyjoy.cfg')
# DBSIM rewrites DATA\PREFS.CFG when a mission ends and, when a joystick answers, fills its
# mapping bytes 12..36: without a restore one joystick run changed every later suite
# on that clone (check-seq-8b, c4_08-slope-dive). Restored after every run like the tape
# files, never captured as an output.
$script:ConfigRestoreFiles = @('DATA/PREFS.CFG')
# run-oracle-regression.ps1 saves and restores them around every suite, whatever runner the
# suite takes: the transaction above covers only launch-set and tape suites, and the joystick
# suites (no launch set) run through run-tick-log-mission.ps1, so check-seq-8c left timing-r111
# with a joystick mapping although every suite was identical.
function Save-SuiteConfigFiles([string]$Installation) {
    $saved = @()
    foreach ($relative in $script:ConfigRestoreFiles) {
        $path = Assert-SuiteChild (Join-Path $Installation $relative) $Installation
        $state = Get-SuiteFileState $path
        $item = [pscustomobject]@{relative=$relative; path=$path; state=$state; bytes=$null; time=$null; attributes=$null}
        if ($state) {
            $item.bytes = [IO.File]::ReadAllBytes($path)
            $file = Get-Item -LiteralPath $path
            $item.time = $file.LastWriteTimeUtc; $item.attributes = $file.Attributes
        }
        $saved += $item
    }
    return ,$saved
}
# Puts every saved file back (bytes, write time, attributes; a file that was absent is removed)
# when it changed. Returns the relative names restored; throws when a restore cannot be verified.
function Restore-SuiteConfigFiles($Saved) {
    $restored = @()
    foreach ($item in @($Saved)) {
        if (((Get-SuiteFileState $item.path) | ConvertTo-Json -Compress) -eq ($item.state | ConvertTo-Json -Compress)) { continue }
        $restored += $item.relative
        if ($item.state) {
            if (Test-Path -LiteralPath $item.path) { Set-ItemProperty -LiteralPath $item.path -Name IsReadOnly -Value $false }
            [IO.File]::WriteAllBytes($item.path, $item.bytes)
            $file = Get-Item -LiteralPath $item.path
            $file.LastWriteTimeUtc = $item.time; $file.Attributes = $item.attributes
        } elseif (Test-Path -LiteralPath $item.path) { Remove-Item -LiteralPath $item.path -Force }
        if (((Get-SuiteFileState $item.path) | ConvertTo-Json -Compress) -ne ($item.state | ConvertTo-Json -Compress)) { throw "Restore of $($item.relative) could not be verified." }
    }
    return ,$restored
}
function New-SuiteDemoList([string]$Name) {
    # STR file (docs/formats/str.md) with one group of one string: u32 body size,
    # i16 count 1, i16 length incl. NUL, the text, the byte E = 0. With one entry
    # 0x45ceb8 takes it without the wall-clock pick time() % count (0x45cf87).
    $text = [Text.Encoding]::ASCII.GetBytes($Name)
    $bytes = [Collections.Generic.List[byte]]::new()
    $bytes.AddRange([BitConverter]::GetBytes([uint32]($text.Length + 6)))
    $bytes.AddRange([BitConverter]::GetBytes([int16]1))
    $bytes.AddRange([BitConverter]::GetBytes([int16]($text.Length + 1)))
    $bytes.AddRange($text); $bytes.Add(0); $bytes.Add(0)
    return ,$bytes.ToArray()
}
function Get-SuiteTapePlan {
    param([AllowEmptyCollection()][AllowNull()][string[]]$GameArguments, [AllowNull()][string]$DemoTape)
    $options = @()
    foreach ($argument in @($GameArguments | Where-Object { $null -ne $_ })) {
        if ($argument -cmatch '^[-/]([rpDd])(.*)$') { $options += [pscustomobject]@{letter=$Matches[1];name=$Matches[2];argument=$argument} }
    }
    if (!$options.Count -and !$DemoTape) { return $null }
    if ($options.Count -gt 1) { throw "Tape suite: more than one tape option ($(($options.argument) -join ' ')); DBSIM keeps only the first name for all of them (0x4d24da)." }
    if (!$options.Count) { throw "Tape suite: demo_tape '$DemoTape' needs the DBSIM argument -D." }
    $option = $options[0]
    $plain = '^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$'
    if ($option.letter -cne 'D' -and $DemoTape) { throw "Tape suite: demo_tape '$DemoTape' belongs to -D, not to '$($option.argument)'." }
    switch -CaseSensitive ($option.letter) {
        'd' { throw "Tape suite: '$($option.argument)' is refused; the dump file shares the tape name and no suite needs it." }
        'r' {
            if ($option.name -cnotmatch $plain) { throw "Tape suite: '$($option.argument)' needs a plain tape name ($plain); the tape is written to the clone root." }
            return [pscustomobject]@{mode='record';argument=$option.argument;tape="$($option.name).tap";demo_tape=$null}
        }
        'p' {
            if ($option.name -notmatch '^(?:tapes\\)?[A-Za-z0-9][A-Za-z0-9_-]{0,31}$') { throw "Tape suite: '$($option.argument)' needs a plain tape name or tapes\<name> (a tape of the installation)." }
            return [pscustomobject]@{mode='play';argument=$option.argument;tape=(($option.name -replace '\\','/') + '.tap');demo_tape=$null}
        }
        'D' {
            if ($option.name) { throw "Tape suite: '$($option.argument)' is refused; -D takes no name." }
            if (!$DemoTape) { throw 'Tape suite: -D needs demo_tape (one tape name): with the shipped three-entry DEMOLIST.STR DBSIM picks the tape by time() % 3, which no golden can follow.' }
            if ($DemoTape -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,7}$') { throw "Tape suite: demo_tape '$DemoTape' must be a plain name of up to eight characters." }
            return [pscustomobject]@{mode='demo';argument=$option.argument;tape="TAPES/$DemoTape.tap";demo_tape=$DemoTape}
        }
    }
}
function Get-SuiteTapeRestoreFiles($TapePlan) {
    if (!$TapePlan) { return @() }
    $files = @($script:TapeRestoreFiles)
    if ($TapePlan.mode -eq 'demo') { $files += 'TAPES/DEMOLIST.STR' }
    if ($TapePlan.mode -eq 'record') { $files += $TapePlan.tape }
    return $files
}
function Invoke-SuiteTransaction {
    param([string]$DataRoot, [string]$InstallName, [string]$Launch='',
          [string]$OutputDirectory, [string]$RunName, [scriptblock]$Action, [switch]$Mock, [switch]$KeepReturns,
          $TapePlan=$null)
    Assert-SuiteName $RunName
    if ($Mock) { Assert-SuiteMockRoot $DataRoot }
    $install = Get-SuiteInstallation $DataRoot $InstallName
    $launchInfo = if ($Launch) { Read-SuiteLaunch $DataRoot $Launch } else { $null }
    # Tape files: restored like the launch files but never captured as outputs (a
    # tape and the files playback writes carry copies of installation files).
    $tapeFiles = @($script:ConfigRestoreFiles) + @(Get-SuiteTapeRestoreFiles $TapePlan)
    foreach ($relative in $tapeFiles) { $null = Assert-SuiteChild (Join-Path $install $relative) $install }
    if ($TapePlan -and $TapePlan.mode -ne 'record') {
        $tapePath = Assert-SuiteChild (Join-Path $install $TapePlan.tape) $install
        if (!(Test-Path -LiteralPath $tapePath -PathType Leaf)) { throw "Tape suite: the tape $($TapePlan.tape) is missing in the clone; DBSIM would read it through a null FILE*." }
    }
    $playback = $TapePlan -and $TapePlan.mode -ne 'record'
    $output = Assert-SuitePlainPath $OutputDirectory
    if (!$Mock) { $output = Assert-SuiteChild $output (Join-Path $DataRoot 'logs') }
    if (!$Mock -and @(Get-SuiteProcesses $install).Count) { throw 'Close the game in this clone before deployment.' }
    $reportPath = Join-Path $output "$RunName-launch.json"
    $keep = Join-Path $output "$RunName-output"
    if ((Test-Path -LiteralPath $reportPath) -or (Test-Path -LiteralPath $keep)) { throw 'Run outputs must be fresh.' }
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $backup = Join-Path $output ('.backup-' + $RunName + '-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    $data = Join-Path $install 'DATA'; $before = @{}; $baseline = @{}; $errors = @(); $changed = @()
    $tapeBefore = @{}; $tapeState = $null
    $backedUp = $false; $deploymentStarted = $false; $started = [DateTime]::UtcNow
    try {
        foreach ($name in $script:RestoreFiles) {
            $source = Join-Path $data $name
            $before[$name] = Get-SuiteFileState $source
            if ($before[$name]) {
                Copy-Item -LiteralPath $source -Destination (Join-Path $backup $name)
                if ((Get-SuiteFileState (Join-Path $backup $name)).sha256 -ne $before[$name].sha256) { throw "Backup verification failed: $name" }
            }
        }
        foreach ($relative in $tapeFiles) {
            $tapeBefore[$relative] = Get-SuiteFileState (Join-Path $install $relative)
            if ($tapeBefore[$relative]) {
                $copy = Join-Path $backup ('tape-' + ($relative -replace '[\\/]','-'))
                Copy-Item -LiteralPath (Join-Path $install $relative) -Destination $copy
                if ((Get-SuiteFileState $copy).sha256 -ne $tapeBefore[$relative].sha256) { throw "Backup verification failed: $relative" }
            }
        }
        $restore = @{installation=$install;before=$before}
        $restore.tape_before = $tapeBefore
        Write-SuiteJson (Join-Path $backup 'restore.json') $restore
        $backedUp = $true
        $deploymentStarted = $true
        if ($TapePlan -and $TapePlan.mode -eq 'demo') {
            $list = Join-Path $install 'TAPES/DEMOLIST.STR'
            $bytes = New-SuiteDemoList $TapePlan.demo_tape
            # A file copied from the CD may be read-only; the restore copies the backup (with its attributes) back.
            if ((Test-Path -LiteralPath $list) -and ((Get-Item -LiteralPath $list).Attributes -band [IO.FileAttributes]::ReadOnly)) {
                Set-ItemProperty -LiteralPath $list -Name IsReadOnly -Value $false
            }
            [IO.File]::WriteAllBytes($list, $bytes)
            $expected = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes)).Replace('-','').ToLowerInvariant()
            if ((Get-SuiteFileState $list).sha256 -ne $expected) { throw 'Deployed hash mismatch: TAPES/DEMOLIST.STR' }
        }
        if ($launchInfo) {
            # Reverify immediately before copying; post-copy hashes check every member too.
            $launchInfo = Read-SuiteLaunch $DataRoot $Launch
            foreach ($entry in $launchInfo.files) {
                Copy-Item -LiteralPath (Join-Path $launchInfo.directory $entry.file) -Destination (Join-Path $data $entry.file) -Force
                if ((Get-SuiteFileState (Join-Path $data $entry.file)).sha256 -ine $entry.sha256) { throw "Deployed hash mismatch: $($entry.file)" }
            }
        }
        if ($KeepReturns) {
            # Absence after a run proves no result was produced; an old result
            # must not masquerade as a successful new outcome.
            $oldResult = Join-Path $data 'results.dat'
            if (Test-Path -LiteralPath $oldResult) { Remove-Item -LiteralPath $oldResult }
        }
        foreach ($name in $script:RestoreFiles) { $baseline[$name] = Get-SuiteFileState (Join-Path $data $name) }
        $result = & $Action
    } finally {
        if ($backedUp -and $deploymentStarted) {
            if (!$Mock) { Stop-SuiteOwnedProcess $install $reportPath $started }
            # Playback writes the tape's copies of script.dat, player.mec and
            # mission.var: those are no outputs. mission.var is one only when the
            # mission ended (results.dat written by 0x42412c, which rewrites it).
            $missionEnded = $false
            if ($playback) {
                try { $missionEnded = $baseline.ContainsKey('results.dat') -and (((Get-SuiteFileState (Join-Path $data 'results.dat')) | ConvertTo-Json -Compress) -ne ($baseline['results.dat'] | ConvertTo-Json -Compress)) }
                catch { $errors += "Capture results.dat : $_" }
            }
            foreach ($name in $script:RestoreFiles) {
                $path = Join-Path $data $name
                # Evidence failure must not prevent restoration of this or later files.
                try {
                    $state = Get-SuiteFileState $path
                    if ($baseline.ContainsKey($name) -and (($state | ConvertTo-Json -Compress) -ne ($baseline[$name] | ConvertTo-Json -Compress) -or ($KeepReturns -and $name -in @('results.dat','mission.var')))) {
                        $changed += $name
                        $tapeCopy = $playback -and ($name -in @('script.dat','player.mec','MISSION.STR') -or ($name -eq 'mission.var' -and !$missionEnded))
                        if ($state -and !$tapeCopy) {
                            New-Item -ItemType Directory -Force -Path $keep | Out-Null
                            Copy-Item -LiteralPath $path -Destination (Join-Path $keep $name)
                        }
                    }
                } catch { $errors += "Capture $name : $_" }
                try {
                    if ($before[$name]) { Copy-Item -LiteralPath (Join-Path $backup $name) -Destination $path -Force }
                    elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
                    if (((Get-SuiteFileState $path) | ConvertTo-Json -Compress) -ne ($before[$name] | ConvertTo-Json -Compress)) { throw 'Restored hash/absence differs.' }
                } catch { $errors += "Restore $name : $_" }
            }
            foreach ($relative in $tapeFiles) {
                $path = Join-Path $install $relative
                try {
                    $state = Get-SuiteFileState $path
                    # The recorded tape is evidence by size and hash only (it stays out of <run>-output).
                    if ($TapePlan.mode -eq 'record' -and $relative -eq $TapePlan.tape) { $tapeState = $state }
                    if (($state | ConvertTo-Json -Compress) -ne ($tapeBefore[$relative] | ConvertTo-Json -Compress)) { $changed += $relative }
                } catch { $errors += "Capture $relative : $_" }
                try {
                    $copy = Join-Path $backup ('tape-' + ($relative -replace '[\\/]','-'))
                    if ($tapeBefore[$relative]) { Copy-Item -LiteralPath $copy -Destination $path -Force }
                    elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
                    if (((Get-SuiteFileState $path) | ConvertTo-Json -Compress) -ne ($tapeBefore[$relative] | ConvertTo-Json -Compress)) { throw 'Restored hash/absence differs.' }
                } catch { $errors += "Restore $relative : $_" }
            }
        }
        if ($errors.Count) { throw "Transaction errors; backup retained at $backup : $($errors -join '; ')" }
        # A generated directory, verified under the explicit output root.
        $null = Assert-SuiteChild $backup $output
        Remove-Item -LiteralPath $backup -Recurse -Force
    }
    return [pscustomobject]@{result=$result;launch_set=$launchInfo;changed_and_restored=$changed;tape_plan=$TapePlan;tape=$tapeState}
}
# The exit code of a suite's own DBSIM (docs/reference-game/oracle_regression.md,
# "Exit code of the own DBSIM"). The record is Wait-CloneOwnedProcessExit's result
# plus, from the launch report of the mission runner, whether the runner ended the
# game itself (run-tick-log-mission.ps1 stops it once the ticks are logged).
# exit_code_state: 'exited' = exit_code is the game's own code; 'none' = the runner
# stopped the process (or none started): there is no code of the game, exit_code is
# $null; 'unknown' = the process ended by itself but no handle gave the code (or
# the PID was reused).
function Get-SuiteOwnExitRecord($Wait, $Launch) {
    $record = [ordered]@{}
    foreach ($property in $Wait.PSObject.Properties) { $record[$property.Name] = $property.Value }
    $byLauncher = [bool]($Launch -and $Launch.PSObject.Properties['stopped_after_identity_check'] -and $Launch.stopped_after_identity_check)
    $stopped = $byLauncher -or [bool]$record['stopped_by_runner']
    $code = $record['exit_code']
    $record['stopped_by_runner'] = $stopped
    if ($stopped) { $record['exit_code'] = $null; $record['exit_code_state'] = 'none' }
    elseif ($record['status'] -eq 'exited') {
        $record['exit_code_state'] = if ($null -ne $code) { 'exited' } else { 'unknown' }
    }
    elseif ($record['status'] -eq 'pid-reused') { $record['exit_code'] = $null; $record['exit_code_state'] = 'unknown' }
    else { $record['exit_code'] = $null; $record['exit_code_state'] = 'none' }
    return [pscustomobject]$record
}
# The run's exit code against the golden's (manifest.json own_process_exit).
# check: 'same' | 'different' | 'no golden exit code' (the golden predates the
# field: reported, never a difference). A state or code that differs is
# 'different'; so is an unknown on either side (nothing to confirm).
function Compare-SuiteOwnExit($GoldenManifest, $Run) {
    $golden = $null
    if ($GoldenManifest -and $GoldenManifest.PSObject.Properties['own_process_exit']) { $golden = $GoldenManifest.own_process_exit }
    if (!$golden -or !$golden.PSObject.Properties['exit_code_state']) {
        return [pscustomobject]@{check='no golden exit code'; golden_state=$null; golden_exit_code=$null}
    }
    $goldenCode = if ($golden.PSObject.Properties['exit_code']) { $golden.exit_code } else { $null }
    $same = $golden.exit_code_state -ceq $Run.exit_code_state -and $golden.exit_code_state -cne 'unknown' -and
        ($golden.exit_code_state -cne 'exited' -or ($null -ne $goldenCode -and $null -ne $Run.exit_code -and [int64]$goldenCode -eq [int64]$Run.exit_code))
    return [pscustomobject]@{check=$(if ($same) { 'same' } else { 'different' }); golden_state=[string]$golden.exit_code_state; golden_exit_code=$goldenCode}
}
# The hook mismatch log (<run>.jsonl.hooks.jsonl) is opened by the proxy at start
# whenever one hook runs in compare mode, so a run without mismatches leaves a
# zero-byte file. Called after the game has exited; removes only zero-byte files.
function Remove-SuiteEmptyHookLog([string]$Directory, [string]$RunName) {
    $path = Join-Path $Directory "$RunName.jsonl.hooks.jsonl"
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    if ((Get-Item -LiteralPath $path).Length -ne 0) { return $false }
    try { Remove-Item -LiteralPath $path -Force -ErrorAction Stop; return $true } catch { return $false }
}
Export-ModuleMember -Function *-Suite*
