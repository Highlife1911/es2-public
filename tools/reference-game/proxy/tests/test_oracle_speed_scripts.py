# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Exercise actual runner/module/clone/parallel scripts with synthetic state."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import importlib.util
import types

PS = str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe')
# One Windows PowerShell process serves every step, each step in a fresh runspace
# (setup/pwsh_session.py, found next to the parallel runner passed as argv[1]).
sys.path.insert(0, str(Path(sys.argv[1]).resolve().parent))
from pwsh_session import PwshSession, shard_items  # noqa: E402
SESSION = None


def run(script, *args, ok=True):
    global SESSION
    if SESSION is None:
        SESSION = PwshSession(PS)
    p = SESSION.run(file=script, args=args, timeout=120)
    if ok and p.returncode:
        raise AssertionError((script, args, p.returncode, p.stdout, p.stderr))
    if not ok and not p.returncode:
        raise AssertionError(('expected refusal', script, args, p.stdout))
    return p


def run_code(code, ok=True):
    """A code snippet in a fresh runspace of the same session (real array binding)."""
    global SESSION
    if SESSION is None:
        SESSION = PwshSession(PS)
    p = SESSION.run(code=code, timeout=120)
    if ok and p.returncode:
        raise AssertionError((code, p.returncode, p.stdout, p.stderr))
    if not ok and not p.returncode:
        raise AssertionError(('expected refusal', code, p.stdout))
    return p


def q(value):
    return "'" + str(value).replace("'", "''") + "'"


# The tick-log runner with a launcher stub that has the -GameArguments
# parameter and records what it received in its launch report, like the real one.
GAME_ARGS_START = r'''
param($DataRoot,$InstallName,$OutputPath,[switch]$Interactive,[string[]]$GameArguments=@())
$r=[ordered]@{pid=42;exe='C:\synthetic\DBSIM.EXE';process_start_utc='2026-10-07T00:00:00Z'}
if($GameArguments.Count){$r.game_arguments=$GameArguments}
$r | ConvertTo-Json | Set-Content $OutputPath
'launched'
'''
GAME_ARGS_TEST = r'''
param($Runner,$Root,$Mode)
$ErrorActionPreference='Stop'
$out=Join-Path $Root ('game-args-'+$Mode);New-Item -ItemType Directory $out | Out-Null
Import-Module (Join-Path (Split-Path $Runner) '../scripts/OracleSpeed.psm1') -Force
function Import-Module {param($Name,[switch]$Force)}
function Get-Es2Layout {[pscustomobject]@{DataRepository='X:\synthetic\es2-data'}}
function Start-Sleep {param($Milliseconds);[IO.File]::AppendAllText((Join-Path $out 'mock.jsonl'),"{`"event`":`"tick`"}`n")}
function Get-Process {param($Id,$ErrorAction);return @{Id=42}}
function Get-CimInstance {param($ClassName,$Filter);return @{ExecutablePath='C:\synthetic\DBSIM.EXE';CreationDate=[datetime]'2026-10-07T00:00:00Z'}}
function Stop-Process {param($Id)}
if($Mode -eq 'absent') {
    $result=& $Runner -OutputDirectory $out -RunName mock -Ticks 3 -SkipTests -VolumePercent 100 | Select-Object -Last 1 | ConvertFrom-Json
    $launch=Get-Content (Join-Path $out 'mock-launch.json') -Raw | ConvertFrom-Json
    if(@($result.PSObject.Properties.Name) -contains 'game_arguments' -or @($launch.PSObject.Properties.Name) -contains 'game_arguments'){throw 'key without the option'}
    'PASS no game_arguments key without the option; launcher called without the parameter'
    return
}
$extra=@('-ptapes\demo1','-rrun')
$result=& $Runner -OutputDirectory $out -RunName mock -Ticks 3 -SkipTests -VolumePercent 100 -GameArguments $extra | Select-Object -Last 1 | ConvertFrom-Json
$launch=Get-Content (Join-Path $out 'mock-launch.json') -Raw | ConvertFrom-Json
foreach($report in @($result,$launch)) {
    $got=@($report.game_arguments)
    if($got.Count -ne 2 -or $got[0] -cne $extra[0] -or $got[1] -cne $extra[1]){throw 'arguments not passed through in order'}
}
'PASS game arguments reach the launcher and both reports'
'''

# The tick-log runner's hidden-window decision (ES2_ORACLE_HIDDEN_WINDOW from
# run-oracle-regression.ps1), with a configure stub that records its arguments.
HIDDEN_CONFIGURE = r'''
Set-Content -LiteralPath $env:FAKE_CONFIGURE_LOG -Value ('args:'+($args -join ' '))
'configured'
'''
HIDDEN_TEST = r'''
param($Runner,$Root,$Mode)
$ErrorActionPreference='Stop'
$out=Join-Path $Root ('hidden-'+$Mode);New-Item -ItemType Directory $out | Out-Null
$global:volumes=@();$global:handle=if($Mode -eq 'seen'){[IntPtr]4660}else{[IntPtr]::Zero}
Import-Module (Join-Path (Split-Path $Runner) '../scripts/OracleSpeed.psm1') -Force
function Import-Module {param($Name,[switch]$Force)}
function Get-Es2Layout {[pscustomobject]@{DataRepository='X:\synthetic\es2-data'}}
function Start-Sleep {param($Milliseconds);[IO.File]::AppendAllText((Join-Path $out 'mock.jsonl'),"{`"event`":`"tick`"}`n")}
function Get-Process {param($Id,$ErrorAction);return [pscustomobject]@{Id=42;MainWindowHandle=$global:handle}}
function Get-CimInstance {param($ClassName,$Filter);return @{ExecutablePath='C:\synthetic\DBSIM.EXE';CreationDate=[datetime]'2026-10-07T00:00:00Z'}}
function Stop-Process {param($Id)}
function Copy-Es2BazelOutput {param($Target,$Destination);return 'C:\synthetic\session_volume.exe'}
function Start-Process {param($FilePath,$ArgumentList,[switch]$NoNewWindow,[switch]$PassThru,$RedirectStandardOutput);$global:volumes+=,($ArgumentList -join ' ');return @{Id=7}}
$env:FAKE_CONFIGURE_LOG=Join-Path $out 'configure.txt'
$prior=$env:ES2_ORACLE_HIDDEN_WINDOW
try {
    if($Mode -ne 'off'){$env:ES2_ORACLE_HIDDEN_WINDOW='1'}
    $common=@{OutputDirectory=$out;RunName='mock';Ticks=3;SkipTests=$true;InputMode='replay';InputJournal=(Join-Path $out 'journal.jsonl')}
    if($Mode -eq 'record'){$common.InputMode='record'}
    if($Mode -eq 'interactive'){$common.Interactive=$true}
    if($Mode -eq 'volume'){$common.VolumePercent=25}
    if($Mode -eq 'hold'){$common.HoldAtTicks=$true}
    if($Mode -eq 'switch'){$env:ES2_ORACLE_HIDDEN_WINDOW=$null;$common.HiddenWindow=$true}
    $result=& $Runner @common | Select-Object -Last 1 | ConvertFrom-Json
} finally { $env:ES2_ORACLE_HIDDEN_WINDOW=$prior;$env:FAKE_CONFIGURE_LOG=$null }
$configure=Get-Content -LiteralPath (Join-Path $out 'configure.txt') -Raw
$hidden=$Mode -in @('env','seen','volume','switch','hold')
$keys=@($result.PSObject.Properties.Name)
# -HoldAtTicks: configure gets -HoldFrame Ticks, the JSON line hold_frame; otherwise neither.
if($Mode -eq 'hold') {
    if($configure -notmatch 'HoldFrame:?\s+3\b' -or $result.hold_frame -ne 3){throw "hold: $configure"}
} elseif($configure -match 'HoldFrame' -or $keys -contains 'hold_frame'){throw "hold off: $configure"}
if($hidden) {
    if($configure -notmatch 'HiddenWindow'){throw "configure without -HiddenWindow: $configure"}
    if(!$result.hidden_window -or $result.window_visible_seen -ne ($Mode -eq 'seen')){throw 'hidden keys'}
    $percent=if($Mode -eq 'volume'){25}else{0}
    if($result.volume_percent -ne $percent -or $global:volumes.Count -ne 1 -or $global:volumes[0] -notlike "--pid 42 --percent $percent --wait-ms *"){throw "volume: $($global:volumes)"}
} else {
    if($configure -match 'HiddenWindow'){throw "configure got -HiddenWindow: $configure"}
    if($keys -contains 'hidden_window' -or $keys -contains 'window_visible_seen' -or $keys -contains 'volume_percent'){throw 'keys without a hidden run'}
    $expected=if($Mode -eq 'interactive'){0}else{1}
    if($global:volumes.Count -ne $expected -or ($expected -and $global:volumes[0] -notlike '--pid 42 --percent 10 --wait-ms *')){throw "volume: $($global:volumes)"}
}
"PASS hidden window runner $Mode"
'''

# The real configure-tick-log.ps1 -Apply on a synthetic clone (module functions
# stubbed: no lock, no build, no fixture tests): [window] hidden=1 only with -HiddenWindow.
CONFIGURE_TEST = r'''
param($Configure,$Root,$Mode)
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $Configure) '../scripts/OracleSpeed.psm1') -Force
function Import-Module {param($Name,[switch]$Force)}
function Get-Es2Layout {[pscustomobject]@{DataRepository='X:\synthetic\es2-data'}}
function Get-CloneInstallRoot {param($DataRoot);return Join-Path ([IO.Path]::GetFullPath($DataRoot)) 'installs'}
function Assert-CloneLockAvailable {param($DataRoot,$InstallName)}
function Assert-CloneIdle {param($Installation,$Purpose,$WaitSeconds)}
function Get-Es2PreparedProxy {param($Manifest);return @{proxy=(Join-Path $Root 'synthetic-ddraw.dll')}}
function Invoke-Es2FixtureTests {throw 'no fixture tests here'}
function Copy-Es2BazelOutput {throw 'no build here'}
[IO.File]::WriteAllText((Join-Path $Root 'synthetic-ddraw.dll'),'synthetic proxy')
$data=Join-Path $Root ('configure-'+$Mode);$install=Join-Path $data 'installs\timing-r111-p1'
New-Item -ItemType Directory (Join-Path $install 'DATA') | Out-Null
@{destination=$install;measurement_clone_of='synthetic-source'} | ConvertTo-Json | Set-Content (Join-Path $install 'installation-manifest.json')
'{}' | Set-Content (Join-Path $install 'oracle-media-manifest.json')
$journal=Join-Path $data 'journal.jsonl';'' | Set-Content $journal
$arguments=@{DataRoot=$data;InstallName='timing-r111-p1';TickLogPath=(Join-Path $data 'tick.jsonl');InputMode='replay';InputJournal=$journal;PreparedProxy='synthetic'}
if($Mode -in @('hidden','record')){$arguments.HiddenWindow=$true}
if($Mode -eq 'record'){$arguments.InputMode='record';Remove-Item $journal}
if($Mode -eq 'early'){$arguments.EarlyArming=$true;$arguments.AfterLoadSnapshot=$true;$arguments.AfterLoadSnapshotExtra='0x4aaa7c:0x34, 0x49aac8:0x29'}
if($Mode -eq 'late'){$arguments.EarlyArming=$false}
if($Mode -eq 'hold'){$arguments.HoldFrame=650}
if($Mode -eq 'vshell'){$arguments.VshellMeasures=$true}
if($Mode -eq 'vshell-env'){$env:ES2_VSHELL_MEASURES='1'}
# D-46: [input] message_trace=1 only with -MessageTrace (or ES2_MESSAGE_TRACE=1); an existing trace refuses.
if($Mode -in @('trace','trace-exists')){$arguments.MessageTrace=$true}
if($Mode -eq 'trace-env'){$env:ES2_MESSAGE_TRACE='1'}
$ini=Join-Path $install 'es2-oracle.ini'
$trace=(Join-Path $data 'tick.jsonl')+'.messages.ndjson'
if($Mode -eq 'trace-exists') {
    'keep' | Set-Content $trace
    $refused=$false
    try { & $Configure @arguments -Apply } catch { $refused=$_.Exception.Message -like '*Message trace already exists*' }
    if(!$refused -or (Test-Path $ini)){throw 'an existing message trace was not refused before the deployment'}
    'PASS configure refuses an existing message trace';return
}
if($Mode -eq 'record') {
    $refused=$false
    try { & $Configure @arguments -Apply } catch { $refused=$_.Exception.Message -like '*replay runs only*' }
    if(!$refused -or (Test-Path $ini)){throw 'record mode with -HiddenWindow was not refused before the deployment'}
    'PASS configure refuses -HiddenWindow outside replay';return
}
& $Configure @arguments -Apply | Out-Null
$text=[IO.File]::ReadAllText($ini,[Text.Encoding]::Unicode)
$manifest=Get-Content (Join-Path $install 'oracle-media-manifest.json') -Raw | ConvertFrom-Json
if(!$text.Contains("[input]`r`nmode=replay`r`n")){throw "input section: $text"}
if($Mode -in @('trace','trace-env')) {
    if(!$text.Contains("[input]`r`nmode=replay`r`njournal=$journal`r`nmessage_trace=1`r`n") -or $manifest.message_trace -ne $trace){throw "trace: $text"}
} elseif($text.Contains('message_trace') -or @($manifest.PSObject.Properties.Name) -contains 'message_trace'){throw "trace off: $text"}
# The VSHELL-only keys are written only with -VshellMeasures (or ES2_VSHELL_MEASURES=1); the manifest says which.
if($Mode -in @('vshell','vshell-env')) {
    if(!$text.StartsWith("[video]`r`nsystem_indeo=1`r`nlog=") -or !$text.Contains("[compat]`r`nmission_map_lifetime=1`r`n[trace]`r`n") -or
       $manifest.vshell_measures -ne $true -or $manifest.mission_map_guard -ne $true){throw "vshell measures: $text"}
} elseif($text.Contains('system_indeo') -or $text.Contains('mission_map_lifetime') -or $text.Contains('[compat]') -or
         !$text.StartsWith("[video]`r`nlog=") -or !$text.Contains("`r`n[trace]`r`n") -or
         $manifest.vshell_measures -ne $false -or $manifest.mission_map_guard -ne $false){throw "measures off: $text"}
if($Mode -eq 'hidden') {
    if(!$text.EndsWith("[window]`r`nhidden=1`r`n") -or !$manifest.hidden_window -or !$manifest.window_shim_header_sha256){throw "hidden: $text"}
} else {
    if($text.Contains('[window]') -or @($manifest.PSObject.Properties.Name) -contains 'hidden_window'){throw "default: $text"}
}
# L0: [arming] early=1 by default (late: -EarlyArming:$false omits it), [snapshot] only with -AfterLoadSnapshot.
$snap=Join-Path $data 'tick-after-load.snap'
if($Mode -eq 'early') {
    if(!$text.EndsWith("[arming]`r`nearly=1`r`n[snapshot]`r`nafter_load=1`r`npath=$snap`r`nextra=0x4aaa7c:0x34,0x49aac8:0x29`r`n") -or !$manifest.early_arming -or
       $manifest.after_load_snapshot -ne $snap -or !$manifest.early_arming_header_sha256){throw "early: $text"}
} elseif($Mode -eq 'late') {
    if($text.Contains('[arming]') -or $text.Contains('[snapshot]') -or @($manifest.PSObject.Properties.Name) -contains 'early_arming'){throw "late: $text"}
} elseif(!$text.Contains("[arming]`r`nearly=1`r`n") -or $text.Contains('[snapshot]') -or !$manifest.early_arming){throw "default early: $text"}
# [trace] hold_frame only with -HoldFrame (proxy/tick_log.h); the manifest records it.
if($Mode -eq 'hold') {
    if(!$text.Contains("[trace]`r`nhold_frame=650`r`ntick_log=") -or $manifest.hold_frame -ne 650){throw "hold: $text"}
} elseif($text.Contains('hold_frame') -or @($manifest.PSObject.Properties.Name) -contains 'hold_frame'){throw "hold off: $text"}
"PASS configure $Mode"
'''


# Sections of the test. Each one is independent of the others (own synthetic files below the shared
# temporary tree), so Bazel shards (shard_count in BUILD.bazel) can split them; without sharding all run.
# The order is the shard assignment for shard_count = 4 (section i belongs to shard i % 4): about 27, 22, 15 and 18 s
# plus 'environment' (shard 0, a few seconds of mock runs).
SECTIONS = ('suite_arguments', 'coverage', 'game_arguments', 'parallel', 'cli', 'clone', 'compact', 'runner', 'environment')


def section_compact(setup):
    compact_path = setup.parent/'scripts/expand_compact_ticks.py'
    spec = importlib.util.spec_from_file_location('compact', compact_path)
    compact = importlib.util.module_from_spec(spec); spec.loader.exec_module(compact)
    header = dict(event='start', schema=3)
    first = dict(event='tick_compact', sequence=1, rng_a_hex='00'*114, rng_b_hex='a5'*114)
    second = dict(event='tick_compact', sequence=2, rng_a_same=True, rng_b_same=True)
    third = dict(event='tick_compact', sequence=3, rng_a_hex='01'*114, rng_b_same=True)
    expanded = list(compact.expand([header, first, second, third]))
    assert expanded[0]['schema'] == 2 and expanded[1]['event'] == 'tick'
    assert expanded[2]['rng_a_hex'] == first['rng_a_hex'] and expanded[3]['rng_a_hex'] == '01'*114
    assert expanded[3]['rng_b_hex'] == first['rng_b_hex']
    for invalid in ([first], [header], [header, second], [header, first, first],
                    [header, dict(first, rng_a_hex='a')], [header, first, dict(second, rng_a_same=False)],
                    [header, first, dict(second, rng_a_hex='00'*114)], [header, first, dict(second, event='tick')]):
        try:
            list(compact.expand(invalid))
        except ValueError:
            pass
        else:
            raise AssertionError(('accepted malformed compact stream', invalid))


def section_cli(setup, root):
    # CLI equivalence on success AND divergent ticks/watch/output/frame
    # windows; the comparator itself is deliberately unchanged.
    oracle = setup.parent/'scripts/oracle_regression.py'
    synthetic_log = root/'oracle.jsonl'
    rows = [dict(event='start', schema=2, watches=1),
            dict(event='tick', frame_counter_before_increment=0, completed_clock_ms=47),
            dict(event='watch', frame=0, name='plain', hex='ab', length=1),
            dict(event='tick', frame_counter_before_increment=1, completed_clock_ms=94)]
    synthetic_log.write_text(''.join(json.dumps(row)+'\n' for row in rows))
    ordinary = root/'ordinary.json'; combined = root/'combined.json'; golden = root/'golden.json'
    old_report = root/'old-report.json'; new_report = root/'new-report.json'
    def cli(*args):
        return subprocess.run([sys.executable, '-B', str(oracle), *map(str, args)], capture_output=True, text=True)
    normalized = cli('normalize', synthetic_log, ordinary)
    assert normalized.returncode == 0, normalized.stderr
    original = json.loads(ordinary.read_text())
    for case in ('equal', 'tick', 'watch', 'output', 'window', 'frames'):
        changed = json.loads(ordinary.read_text())
        if case == 'tick': changed['ticks'][0][1] += 1
        if case == 'watch': changed['watches'][0][2] = 'ac'
        if case == 'output': changed['outputs']['synthetic'] = 'abc'
        if case == 'window': changed['frame_limit'] = 9
        if case == 'frames': changed['ticks'].pop()
        golden.write_text(json.dumps(changed))
        separate = cli('compare', golden, ordinary, '--report', old_report)
        joined = cli('normalize-compare', synthetic_log, combined, '--golden', golden, '--report', new_report)
        assert joined.returncode == separate.returncode == (case != 'equal'), (case, joined.stderr)
        assert joined.stdout == normalized.stdout + separate.stdout
        assert combined.read_bytes() == ordinary.read_bytes()
        assert new_report.read_bytes() == old_report.read_bytes()


def synthetic_tree(setup, root):
    """The copied scripts, synthetic suite inventory and inputs every PowerShell section works on."""
    repo = root/'repo'
    local = repo/'tools/reference-game/setup'
    local.mkdir(parents=True)
    scripts = local.parent/'scripts'
    scripts.mkdir()
    for name in ('run-tick-log-mission.ps1', 'configure-tick-log.ps1', 'Es2Bazel.psm1',
                 'new-measurement-clone.ps1', 'run-oracle-parallel.ps1', 'start-timing-mission.ps1',
                 'CloneGuard.psm1'):
        shutil.copyfile(setup/name, local/name)
    shutil.copyfile(setup.parent/'scripts/OracleSpeed.psm1', scripts/'OracleSpeed.psm1')
    # Synthetic suite inventory. The real parallel orchestration is unchanged.
    suites = dict(suites=[dict(name=n, frame_limit=3) for n in ('one', 'two', 'three')])
    (local.parent/'oracle-suites.json').write_text(json.dumps(suites))
    for name in ('MODULE.bazel', 'MODULE.bazel.lock', '.bazelrc'):
        (repo/name).write_text('synthetic input')
    proxy = local.parent/'proxy'; proxy.mkdir()
    (proxy/'test.h').write_text('synthetic source')
    parallel = local/'run-oracle-parallel.ps1'
    return types.SimpleNamespace(root=root, setup=setup, repo=repo, local=local, scripts=scripts, suites=suites,
                                 parallel=parallel)


def section_runner(t):
    root, local, scripts = t.root, t.local, t.scripts
    harness = root/'module-test.ps1'
    harness.write_text(r'''
param($Module,$Bazel,$Root)
$ErrorActionPreference='Stop'
Import-Module $Module -Force
$p=Join-Path $Root 'stream.jsonl';$c=New-TickCursor $p
if ((Read-TickCursor $c) -ne 0) {throw 'missing file'}
[IO.File]::WriteAllText($p,'{"event":"ti')
if ((Read-TickCursor $c) -ne 0) {throw 'partial record counted'}
[IO.File]::AppendAllText($p,"ck`"}`n{`"event`":`"watch`"}`n{`"event`":`"tick`"}`n")
if ((Read-TickCursor $c) -ne 2) {throw 'split marker lost'}
$size=(Get-Item $p).Length
1..4 | ForEach-Object { if ((Read-TickCursor $c) -ne 2) {throw 'double counted'} }
if ($c.BytesRead -ne $size) {throw 'rescanned old bytes'}
[IO.File]::WriteAllText($p,'')
$refused=$false;try {Read-TickCursor $c} catch {$refused=$true}
if (!$refused) {throw 'truncation accepted'}
$compact=New-TickCursor $p -CompactRng
[IO.File]::WriteAllText($p,"{`"event`":`"tick_compact`"}`n{`"event`":`"tick`"}`n")
if ((Read-TickCursor $compact) -ne 1) {throw 'compact cursor mixed schemas'}
$unbounded=New-TickCursor $p
[IO.File]::WriteAllText($p,('x'*131073))
$refused=$false;try {Read-TickCursor $unbounded} catch {$refused=$true}
if (!$refused) {throw 'unbounded partial line accepted'}
$now=[datetime]::UtcNow;$s=@{Previous=-1;Progress=$now;Stalled=$false}
Update-TickProgress $s 0 $now 5;Update-TickProgress $s 0 $now.AddSeconds(20) 5
if ($s.Stalled) {throw 'zero ticks must use deadline'}
Update-TickProgress $s 1 $now.AddSeconds(21) 5;Update-TickProgress $s 1 $now.AddSeconds(25) 5
if ($s.Stalled) {throw 'early stall'}
Update-TickProgress $s 1 $now.AddSeconds(26) 5
if (!$s.Stalled) {throw 'missing stall'}
foreach($n in @('native-r111','../native-r111','x/y','')) {
    $refused=$false;try {Assert-MeasurementName $n} catch {$refused=$true}
    if (!$refused) {throw "unsafe name: $n"}
}
Import-Module $Bazel -Force
$m=Get-Module Es2Bazel
& $m {
    $script:tests=0;$script:builds=0
    function script:Invoke-Es2FixtureTests {$script:tests++}
    function script:Copy-Es2BazelOutput($Target,$Destination) {
        $script:builds++;New-Item -ItemType Directory -Force $Destination | Out-Null
        $name=if ($Target -like '*ddraw.dll') {'ddraw.dll'} elseif ($Target -like '*session_volume') {'session_volume.exe'} else {'hang_snapshot.exe'}
        $p=Join-Path $Destination $name;[IO.File]::WriteAllText($p,$Target);return $p
    }
}
$manifest=New-Es2PreparedProxy (Join-Path $Root 'prepared')
1..3 | ForEach-Object {Get-Es2PreparedProxy $manifest | Out-Null}
if ((& $m {$script:tests}) -ne 1 -or (& $m {$script:builds}) -ne 3) {throw 'build/test repeated'}
if ((Split-Path -Leaf (Get-Es2PreparedProxy $manifest).volume) -ne 'session_volume.exe') {throw 'volume helper missing'}
$artifact=(Get-Es2PreparedProxy $manifest).proxy
[IO.File]::AppendAllText($artifact,'changed')
$refused=$false;try {Get-Es2PreparedProxy $manifest} catch {$refused=$true}
if (!$refused) {throw 'changed artifact accepted'}
'PASS incremental poll, partial lines, truncation, stall and prepared build reuse'
''', encoding='utf-8')
    run(harness, scripts/'OracleSpeed.psm1', local/'Es2Bazel.psm1', root)
    # Actual bounded runner with stub configuration/launch and no process APIs.
    original_configure = (local/'configure-tick-log.ps1').read_bytes()
    original_start = (local/'start-timing-mission.ps1').read_bytes()
    (local/'configure-tick-log.ps1').write_text("'configured'\n")
    (local/'start-timing-mission.ps1').write_text(r'''
param($DataRoot,$InstallName,$OutputPath,[switch]$Interactive)
@{pid=42;exe='C:\synthetic\DBSIM.EXE';process_start_utc='2026-10-07T00:00:00Z'} | ConvertTo-Json | Set-Content $OutputPath
'launched'
''')
    runner_test = root/'runner-test.ps1'
    runner_test.write_text(r'''
param($Runner,$Root,$Mode='normal')
$ErrorActionPreference='Stop';$global:polls=0;$global:stops=0
$global:mode=$Mode;$global:flushes=0;$global:volumes=@()
$out=Join-Path $Root ('bounded-'+$Mode);New-Item -ItemType Directory $out | Out-Null
Import-Module (Join-Path (Split-Path $Runner) '../scripts/OracleSpeed.psm1') -Force
function Import-Module {param($Name,[switch]$Force)}
function Get-Es2Layout {[pscustomobject]@{DataRepository='X:\synthetic\es2-data'}}
function Request-TickFlush {param($ProcessId);if($ProcessId -ne 42){throw 'foreign flush'};$global:flushes++}
function Start-Sleep {param($Milliseconds);$global:polls++;[IO.File]::AppendAllText((Join-Path $out 'mock.jsonl'),"{`"event`":`"tick`"}`n")}
function Get-Process {param($Id,$ErrorAction);return @{Id=42}}
function Get-CimInstance {param($ClassName,$Filter);return @{ExecutablePath='C:\synthetic\DBSIM.EXE';CreationDate=$(if($global:mode -eq 'foreign'){[datetime]'2026-10-06T00:00:00Z'}else{[datetime]'2026-10-07T00:00:00Z'})}}
function Stop-Process {param($Id);if($Id -ne 42 -or ($global:mode -eq 'buffered' -and !$global:flushes)){throw 'unsafe stop'};$global:stops++}
function Copy-Es2BazelOutput {param($Target,$Destination);if($Target -ne '//tools/reference-game/helpers:session_volume'){throw 'unexpected build'};return 'C:\synthetic\session_volume.exe'}
function Start-Process {param($FilePath,$ArgumentList,[switch]$NoNewWindow,[switch]$PassThru,$RedirectStandardOutput);$global:volumes+=,@($FilePath,($ArgumentList -join ' '),$RedirectStandardOutput);return @{Id=7}}
$result=& $Runner -OutputDirectory $out -RunName mock -Ticks 3 -SkipTests -BufferedTickLog:($Mode -eq 'buffered') | Select-Object -Last 1 | ConvertFrom-Json
$expectedStops=if($Mode -eq 'foreign'){0}else{1}
if($result.ticks_seen -ne 3 -or $global:polls -ne 3 -or $global:stops -ne $expectedStops -or $result.stopped_after_identity_check -ne [bool]$expectedStops){throw 'runner contract'}
$timing=Get-Content (Join-Path $out 'mock-runner-timing.json') -Raw | ConvertFrom-Json
if($timing.poll_bytes -ne (Get-Item (Join-Path $out 'mock.jsonl')).Length){throw 'runner rescans'}
if($global:volumes.Count -ne 1 -or $global:volumes[0][0] -ne 'C:\synthetic\session_volume.exe' -or $global:volumes[0][1] -notlike '--pid 42 --percent 10 --wait-ms *' -or $global:volumes[0][2] -ne (Join-Path $out 'mock-volume.json')){throw 'volume helper contract'}
'PASS actual runner (mock launch/poll/identity/stop)'
''')
    run(runner_test, local/'run-tick-log-mission.ps1', root)
    run(runner_test, local/'run-tick-log-mission.ps1', root, 'buffered')
    run(runner_test, local/'run-tick-log-mission.ps1', root, 'foreign')
    # DBSIM argument pass-through. The runs above use a launcher stub
    # without the -GameArguments parameter: the runner calls it unchanged
    # when the option is absent (no key in its JSON line either).
    game_args_test = root/'game-args-test.ps1'
    game_args_test.write_text(GAME_ARGS_TEST)
    run(game_args_test, local/'run-tick-log-mission.ps1', root, 'absent')
    (local/'start-timing-mission.ps1').write_text(GAME_ARGS_START)
    run(game_args_test, local/'run-tick-log-mission.ps1', root, 'normal')
    (local/'configure-tick-log.ps1').write_text(HIDDEN_CONFIGURE)
    hidden_test = root/'hidden-test.ps1'
    hidden_test.write_text(HIDDEN_TEST)
    for mode in ('off', 'env', 'switch', 'seen', 'volume', 'record', 'interactive', 'hold'):
        run(hidden_test, local/'run-tick-log-mission.ps1', root, mode)
    # Collisions are refused before anything is configured or launched.
    (local/'configure-tick-log.ps1').write_text("throw 'configure must not run'\n")
    first = q('-ptapes\\demo1')
    for number, bad in enumerate(('-X5', '-R9', '-G', '-eggplant', '-p x')):
        refused = run_code(f"& {q(local/'run-tick-log-mission.ps1')} -OutputDirectory {q(root/('collide%d' % number))} -RunName mock "
                           f"-Ticks 3 -SkipTests -GameArguments {first},{q(bad)}", ok=False)
        assert 'GameArguments[1]' in refused.stderr and 'configure must not run' not in refused.stderr, refused.stderr
        assert not (root/('collide%d' % number)).exists()
    (local/'configure-tick-log.ps1').write_bytes(original_configure)
    (local/'start-timing-mission.ps1').write_bytes(original_start)
    # The headers whose hashes configure-tick-log.ps1 records (synthetic stand-ins).
    for name in ('mission_map_guard.h', 'tick_log.h', 'mem_watch.h', 'virtual_time.h', 'input_journal.h', 'window_shim.h', 'early_arming.h'):
        (local.parent/'proxy'/name).write_text('synthetic ' + name)
    configure_test = root/'configure-test.ps1'
    configure_test.write_text(CONFIGURE_TEST)
    for mode in ('default', 'hidden', 'record', 'early', 'late', 'vshell', 'vshell-env', 'trace', 'trace-env', 'trace-exists', 'hold'):
        run(configure_test, local/'configure-tick-log.ps1', root, mode)


def section_clone(t):
    root, local = t.root, t.local
    start_timing_preview(root, local)
    # Clone dry-run, all-file hashes, metadata, name/existing-output refusals.
    data = root/'data'; source = data/'installs/timing-r111'; source.mkdir(parents=True)
    (source/'installation-manifest.json').write_text(json.dumps(dict(destination=str(source), measurement_clone_of='synthetic-parent', language='German')))
    (source/'DATA').mkdir(); (source/'DATA/synthetic.txt').write_bytes(bytes(range(256)))
    clone = local/'new-measurement-clone.ps1'
    clone_mock = root/'clone-test.ps1'
    clone_mock.write_text(r'''
param($Script,$DataRoot,$Number)
$ErrorActionPreference='Stop'
function Get-CimInstance {param($ClassName);return @()}
& $Script -DataRoot $DataRoot -Number $Number -Apply
''')
    run(clone, '-DataRoot', data, '-Number', 1)
    assert not (source.parent/'timing-r111-p1').exists()
    run(clone_mock, clone, data, 1)
    target = source.parent/'timing-r111-p1'
    assert (target/'DATA/synthetic.txt').read_bytes() == bytes(range(256))
    record = json.loads((target/'measurement-clone-copy.json').read_text(encoding='utf-8-sig'))
    assert record['verified'] and len(record['copied_hashes']) == 2
    run(clone, '-DataRoot', data, '-Number', 1, '-Apply', ok=False)
    run(clone, '-DataRoot', data, '-SourceName', 'native-r111', '-Number', 2, ok=False)
    # Numbers 1..5 only (timing-r111 plus five clones are the six the scripts accept); the source is
    # timing-r111 alone (not a clone, not native-r111); an existing target is never touched.
    for number in (0, 6, 99):
        run(clone, '-DataRoot', data, '-Number', number, ok=False)
    run(clone, '-DataRoot', data, '-SourceName', 'timing-r111-p1', '-Number', 2, ok=False)
    assert not (source.parent/'timing-r111-p2').exists() and not (source.parent/'timing-r111-p6').exists()
    marker = target/'DATA/synthetic.txt'
    before = marker.read_bytes()
    run(clone, '-DataRoot', data, '-Number', 1, '-Apply', ok=False)
    assert marker.read_bytes() == before and len(list(target.rglob('*'))) == len(list(source.rglob('*'))) + 1
    run(clone_mock, clone, data, 5)
    last = source.parent/'timing-r111-p5'
    assert (last/'DATA/synthetic.txt').read_bytes() == bytes(range(256))
    assert json.loads((last/'installation-manifest.json').read_text(encoding='utf-8-sig'))['destination'] == str(last)


def write_mock_child(root):
    """The mock run-oracle-regression.ps1 of the parallel sections (in root/mock); returns that directory."""
    # Parallel child jobs use the same argument contract and summary schema (called by more than one section).
    mock = root/'mock';mock.mkdir(exist_ok=True)
    child = mock/'run-oracle-regression.ps1'
    child.write_text(r'''
param($Mode,$Suite,$InstallName,$OutputDirectory,$DataRoot,$GoldenRoot,$RepositoryRoot,$Python,$Hooks='',$GameArgumentsJson='',$SuitesFile='')
$ErrorActionPreference='Stop'
# An exclusive per-clone file catches simultaneous use of the same clone.
$lock=[IO.File]::Open((Join-Path $DataRoot ($InstallName+'.lock')),'OpenOrCreate','ReadWrite','None')
try {
    Start-Sleep -Milliseconds 200
    # Like the real runner: the output directory must be fresh and is created here.
    if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    $result=if(Test-Path (Join-Path $DataRoot ($Suite+'.fail'))){'DIFFERENT'}else{'identical'}
    $item=@{suite=$Suite;result=$result}
    if($SuitesFile) {
        # The registry file handed on by the parallel runner; like the real runner, a suite with
        # registry arguments names the list it ran with (registry first, then the given ones) in its result.
        Set-Content (Join-Path $DataRoot ('suites-'+$Suite+'.txt')) $SuitesFile
        $own=@((Get-Content -LiteralPath $SuitesFile -Raw | ConvertFrom-Json).suites | Where-Object { $_.name -eq $Suite })[0].game_arguments
        if($own -and !(Test-Path (Join-Path $DataRoot ($Suite+'.dropown')))) {
            $list=@();foreach($x in $own){$list+=$x}
            if($GameArgumentsJson) { foreach($x in (ConvertFrom-Json $GameArgumentsJson)){$list+=$x} }
            $item.game_arguments=$list
        }
    }
    $summary=@{mode=$Mode;output=$OutputDirectory;commit='synthetic';dirty_tree=$false;hooks=$Hooks;results=@($item)}
    # The variable the parallel runner hands down; the real runner reports the key only when hidden.
    Set-Content (Join-Path $DataRoot ('hidden-'+$Suite+'.txt')) ('value='+[string]$env:ES2_ORACLE_HIDDEN_WINDOW)
    if($env:ES2_ORACLE_HIDDEN_WINDOW -eq '1') { $summary.hidden_window=$true }
    if($GameArgumentsJson) {
        # Keep what arrived, and report it like the real runner (unless told to drop it).
        Set-Content (Join-Path $DataRoot ('args-'+$Suite+'.json')) $GameArgumentsJson
        if(!(Test-Path (Join-Path $DataRoot ($Suite+'.dropargs')))) { $list=@();foreach($x in (ConvertFrom-Json $GameArgumentsJson)){$list+=$x};$summary.game_arguments=$list }
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'summary.json')
    if($env:ES2_ORACLE_SPEED_CONFIG) {
        $ini=Get-Content $env:ES2_ORACLE_SPEED_CONFIG -Raw
        $every=[int][regex]::Match($ini,'every_n=(\d+)').Groups[1].Value
        $defer=[int][regex]::Match($ini,'defer_flip=(\d+)').Groups[1].Value
        $phases=@{};foreach($n in @('tick_format','tick_write','watch_reads','input_journal','hook_dispatch','directdraw','frame_other')){$phases[$n]=@{qpc=10;count=2}}
        @{schema=1;module_sha256='8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad';pid=42;thread=43;qpc_frequency=1000;error=0
            complete=(!(Test-Path (Join-Path $DataRoot ($Suite+'.badperf'))));reason='frame_limit';frame_limit=3;last_frame=2;phases=$phases
            presentation=@{every_n=$every;defer_flip=[bool]$defer}} | ConvertTo-Json -Depth 6 |
            Set-Content ([regex]::Match($ini,'report=(.+)').Groups[1].Value.Trim())
        @{pid=42} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory ($Suite+'-launch.json'))
        @{last_frame=2} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory ($Suite+'-normalized.json'))
    }
} finally {$lock.Dispose()}
if(Test-Path (Join-Path $DataRoot ($Suite+'.exitfail'))){exit 2}
if($result -ne 'identical'){exit 1}
''')
    return mock


def section_parallel(t):
    root, local, suites = t.root, t.local, t.suites
    mock = write_mock_child(root)
    parallel = local/'run-oracle-parallel.ps1'
    out = root/'parallel-pass'
    run(parallel, '-MockDirectory', mock, '-DataRoot', root, '-OutputDirectory', out)
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert set(summary) == {'mode','output','commit','dirty_tree','hooks','results'}
    assert summary['results'] == [dict(suite=s['name'], result='identical') for s in suites['suites']]
    assert not list(root.glob('args-*.json')), 'the default runs passed no arguments'
    parallel_hidden_window(root, mock, parallel, suites, summary)
    parallel_dispatch(root, mock, parallel)
    run(parallel, '-MockDirectory', mock, '-DataRoot', root, '-OutputDirectory', root/'perf-pass', '-Perf', '-PresentationEvery', 4)
    (root/'one.badperf').touch()
    run(parallel, '-MockDirectory', mock, '-DataRoot', root, '-OutputDirectory', root/'perf-fail', '-Perf', ok=False)
    (root/'three.exitfail').touch()
    run(parallel, '-MockDirectory', mock, '-DataRoot', root, '-OutputDirectory', root/'exit-fail', ok=False)
    (root/'two.fail').touch()
    out = root/'parallel-fail'
    run(parallel, '-MockDirectory', mock, '-DataRoot', root, '-OutputDirectory', out, ok=False)
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert summary['results'][1]['result'] == 'DIFFERENT'
    run(parallel, '-MockDirectory', mock, '-InstallName', 'native-r111', '-OutputDirectory', root/'protected', ok=False)
    run(parallel, '-MockDirectory', mock, '-Suite', 'missing', '-OutputDirectory', root/'unknown', ok=False)
    assert not (root/'protected').exists() and not (root/'unknown').exists()
    # Without -LiveCoverage nothing coverage-related is written or configured.
    assert not any(p.name.startswith('coverage') for p in (root/'parallel-pass').rglob('*'))


# The process environment around runner invocations. Every ES2_* variable a runner sets for its
# children is handed back as the caller had it (absent: removed; present: its old value), so a second
# invocation in the same PowerShell process starts its children with the environment of the first.
ENV_SNAPSHOT = r'''
function Get-Es2Snapshot {
    $h = [ordered]@{}
    foreach ($e in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $h[[string]$e.Key] = [string]$e.Value }
    return $h
}
function Assert-Es2Same($before, $after, [string]$what) {
    $diff = @()
    foreach ($k in (@($before.Keys) + @($after.Keys) | Sort-Object -Unique)) {
        if (!$before.Contains($k)) { $diff += "$k added=$($after[$k])" }
        elseif (!$after.Contains($k)) { $diff += "$k removed" }
        elseif ($before[$k] -cne $after[$k]) { $diff += "$k changed to $($after[$k])" }
    }
    if ($diff) { throw "environment leak after ${what}: $($diff -join '; ')" }
}
'''
# The variables run-oracle-parallel.ps1 sets for its children (including the caller-controlled window variable).
PARALLEL_ENV = ('ES2_CLONE_LOCK_TOKEN', 'ES2_LIVE_COVERAGE_CONFIG', 'ES2_ORACLE_SPEED_CONFIG', 'ES2_PARALLEL_CLONES',
                'ES2_PREPARED_PROXY', 'ES2_TICK_BUFFERED', 'ES2_ORACLE_HIDDEN_WINDOW')


def environment_step(parallel, mock, data, out, preset, invocations):
    """One PowerShell step: prepare the caller's environment (every name in PARALLEL_ENV absent, or preset
    to a marker), run each invocation of the parallel runner in this very process and assert that the
    complete environment is the one before after each of them."""
    prepare = ''.join((f"[Environment]::SetEnvironmentVariable('{n}', 'caller-{n}')\n" if preset
                       else f"[Environment]::SetEnvironmentVariable('{n}', [NullString]::Value)\n") for n in PARALLEL_ENV)
    body = ''
    for i, (extra, fails) in enumerate(invocations):
        args = f"-MockDirectory {q(mock)} -DataRoot {q(data)} -OutputDirectory {q(out/f'run{i}')} {extra}"
        body += (f"$before = Get-Es2Snapshot\n& {q(parallel)} {args} | Out-Null\n"
                 f"if ({'$true' if fails else '$false'} -ne ($LASTEXITCODE -eq 1)) {{ throw 'unexpected exit code {i}: ' + $LASTEXITCODE }}\n"
                 f"$global:LASTEXITCODE = 0\n"
                 f"Assert-Es2Same $before (Get-Es2Snapshot) 'invocation {i} ({extra})'\n")
    return run_code(ENV_SNAPSHOT + prepare + body)


def section_environment(t):
    root, parallel = t.root, t.parallel
    mock = write_mock_child(root)
    data = root/'envdata'; data.mkdir()
    # Absent before and preset by the caller, over the option combinations that change which variables are set.
    runs = [('', False), ('-HiddenWindow', False), ('-HiddenWindow:$false', False), ('-Perf -PresentationEvery 4', False),
            ('-BufferedTickLog -PreparedProxy ' + q(root/'proxy.dll'), False),
            ('-LiveCoverage run -Python ' + q(sys.executable), False)]
    for name, preset in (('absent', False), ('preset', True)):
        environment_step(parallel, mock, data, root/f'env-{name}', preset, runs)
    # A failing suite (exit 1) leaves nothing behind either.
    (data/'two.fail').touch()
    environment_step(parallel, mock, data, root/'env-fail-absent', False, [('', True)])
    environment_step(parallel, mock, data, root/'env-fail-preset', True, [('', True)])
    (data/'two.fail').unlink()
    # The same process twice in a row: the second run's children see what the first run's did.
    environment_step(parallel, mock, data, root/'env-twice', False, [('', False), ('', False)])
    # A caller's choice made on purpose stays: the children see it and it is still there afterwards.
    keep = (ENV_SNAPSHOT + "[Environment]::SetEnvironmentVariable('ES2_ORACLE_HIDDEN_WINDOW', '1')\n"
            f"$before = Get-Es2Snapshot\n& {q(parallel)} -MockDirectory {q(mock)} -DataRoot {q(data)} -OutputDirectory {q(root/'env-kept')} | Out-Null\n"
            "Assert-Es2Same $before (Get-Es2Snapshot) 'caller-set window variable'\n")
    run_code(keep)
    assert (data/'hidden-one.txt').read_text(encoding='utf-8-sig').strip() == 'value=1'


def section_game_arguments(t):
    root, local, suites = t.root, t.local, t.suites
    mock = write_mock_child(root)
    # The key set of the default run's summary (pinned by section_parallel).
    parallel_game_arguments(root, mock, t.parallel, suites, dict.fromkeys(('mode', 'output', 'commit', 'dirty_tree', 'hooks', 'results')))


def section_suite_arguments(t):
    mock = write_mock_child(t.root)
    suite_game_arguments(t.root, mock, t.parallel, [s['name'] for s in t.suites['suites']])


def section_coverage(t):
    live_coverage_runs(t.root, t.setup, t.local, t.parallel)


def main():
    setup = Path(sys.argv[1]).resolve().parent
    owned = shard_items(SECTIONS)
    if 'compact' in owned:
        section_compact(setup)
    with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as tmp:
        root = Path(tmp)
        if 'cli' in owned:
            section_cli(setup, root)
        t = synthetic_tree(setup, root)
        # Within one process the sections that edit the shared tree run in a fixed order (coverage rewrites the suite inventory last).
        for name in ('runner', 'clone', 'parallel', 'environment', 'game_arguments', 'suite_arguments', 'coverage'):
            if name in owned:
                globals()['section_' + name](t)
        print('PASS: ' + ', '.join(owned) + ' (sections of: ' + ', '.join(SECTIONS) + ')')


def start_timing_preview(root, local):
    """The real start-timing-mission.ps1 in -Preview mode on a synthetic clone, with a
    stand-in start-native.ps1. -Preview returns before any process or mount; the record shows
    the exact DBSIM argument list that Start-Process would receive."""
    (local/'start-native.ps1').write_text("# stand-in: the real one verifies the original executables")
    data = root/'preview-data'
    install = data/'installs/timing-r111-p1'
    (install/'DATA').mkdir(parents=True)
    for name in ('script.dat', 'player.mec', 'mission.var', 'mission.str'):
        (install/'DATA'/name).write_bytes(b'synthetic ' + name.encode())
    (install/'installation-manifest.json').write_text(json.dumps(dict(destination=str(install), measurement_clone_of='synthetic-source', language='German')))
    script = local/'start-timing-mission.ps1'
    base = f"-DataRoot {q(data)} -InstallName timing-r111-p1 -OutputPath {q(root/'preview-launch.json')} -Preview"
    fixed = ['-eggplant', '-X2', '-R0', '-G']
    default = json.loads(run(script, '-DataRoot', data, '-InstallName', 'timing-r111-p1', '-OutputPath', root/'preview-launch.json', '-Preview').stdout)
    assert default['arguments'] == fixed and 'game_arguments' not in default and default['preview'] is True, default
    extra = ['-ptapes' + chr(92) + 'demo1', '-rrun', '-dsave']
    given = json.loads(run_code(f"& {q(script)} {base} -GameArguments {','.join(q(a) for a in extra)}").stdout)
    assert given['arguments'] == fixed + extra and given['game_arguments'] == extra, given
    assert {k: v for k, v in given.items() if k not in ('arguments', 'game_arguments', 'utc')} == {k: v for k, v in default.items() if k not in ('arguments', 'utc')}
    one = json.loads(run_code(f"& {q(script)} {base} -GameArguments {q(extra[0])}").stdout)
    assert one['arguments'] == fixed + extra[:1] and one['game_arguments'] == extra[:1], one
    for bad in ('-X5', '-R1', '-G', '-E', '-EGGPLANT', '/eggplant', '-p x', ''):
        refused = run_code(f"& {q(script)} {base} -GameArguments {q(extra[0])},{q(bad)}", ok=False)
        assert 'GameArguments[1]' in refused.stderr, (bad, refused.stderr)
    assert not (root/'preview-launch.json').exists()


def parallel_hidden_window(root, mock, parallel, suites, default_summary):
    """run-oracle-parallel.ps1 -HiddenWindow hands ES2_ORACLE_HIDDEN_WINDOW to every child and
    restores it; the merged summary carries hidden_window as the children report it."""
    names = [s['name'] for s in suites['suites']]

    def seen():
        return {n: (root/f'hidden-{n}.txt').read_text(encoding='utf-8-sig').strip() for n in names}
    assert seen() == {n: 'value=' for n in names}, seen()   # the default run above: nothing set
    common = f"-MockDirectory {q(mock)} -DataRoot {q(root)}"
    leak = "; if ($env:ES2_ORACLE_HIDDEN_WINDOW) { throw 'variable leaked' }"
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-hidden')} -DryRun -HiddenWindow").stdout)
    assert plan['hidden_window'] is True and not (root/'dry-hidden').exists(), plan
    out = root/'parallel-hidden'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -HiddenWindow{leak}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert summary['hidden_window'] is True and set(summary) == set(default_summary) | {'hidden_window'}, summary
    assert summary['results'] == [dict(suite=n, result='identical') for n in names] and seen() == {n: 'value=1' for n in names}
    out = root/'parallel-visible'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -HiddenWindow:$false{leak}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert 'hidden_window' not in summary and seen() == {n: 'value=0' for n in names}, (summary, seen())


def parallel_game_arguments(root, mock, parallel, suites, default_summary):
    """run-oracle-parallel.ps1 -GameArguments with the mock child (it keeps what
    arrives on its command line and reports it back in its summary.json)."""
    names = [s['name'] for s in suites['suites']]
    assert not list(root.glob('args-*.json')), 'the default runs passed no arguments'
    plan = json.loads(run_code(f"& {q(parallel)} -MockDirectory {q(mock)} -DataRoot {q(root)} -OutputDirectory {q(root/'dry-default')} -DryRun").stdout)
    assert 'game_arguments' not in plan and 'suite_game_arguments' not in plan, plan
    extra = ['-ptapes\\demo1', '-rrun', '-dsave']
    arrays = ','.join(q(a) for a in extra)
    common = f"-MockDirectory {q(mock)} -DataRoot {q(root)}"
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-args')} -DryRun -GameArguments {arrays}").stdout)
    assert plan['game_arguments'] == extra and not (root/'dry-args').exists(), plan
    out = root/'parallel-args'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -GameArguments {arrays}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert summary['game_arguments'] == extra and summary['hooks'] == '', summary
    assert set(summary) == set(default_summary) | {'game_arguments'}
    assert summary['results'] == [dict(suite=n, result='identical') for n in names]
    for name in names:
        received = json.loads((root/f'args-{name}.json').read_text())
        assert received == extra, (name, received)
        child = json.loads((out/name/'run/summary.json').read_text(encoding='utf-8-sig'))
        assert child['game_arguments'] == extra, child
    # A child that does not report the list it was given is not a valid run.
    (root/'three.dropargs').touch()
    out = root/'parallel-dropped'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -GameArguments {arrays}", ok=False)
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert [r['result'] for r in summary['results']] == ['identical', 'identical', 'DIFFERENT'], summary
    (root/'three.dropargs').unlink()
    # Collisions are refused before an output directory, a clone lock or a child exists.
    before = sorted(p.name for p in root.iterdir())
    for number, bad in enumerate(('-X3', '-R0', '-G', '-FOO', '-p x')):
        refused = run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/('refused%d' % number))} -GameArguments {q(extra[0])},{q(bad)}", ok=False)
        assert 'GameArguments[1]' in refused.stderr, refused.stderr
    assert sorted(p.name for p in root.iterdir()) == before


def suite_game_arguments(root, mock, parallel, names):
    """Registry game_arguments in run-oracle-parallel.ps1 (check mode only): plan, children,
    reported lists, golden manifest guard and collisions, all before a child exists."""
    one, two, three = names
    registry = root/'suite-args-registry.json'
    entries = [dict(name=one, frame_limit=3), dict(name=two, frame_limit=3, game_arguments=['-SPRUNKNOWN']), dict(name=three, frame_limit=3)]
    registry.write_text(json.dumps(dict(format=3, suites=entries)))
    golden = root/'suite-args-golden'
    for name in names:
        (golden/name).mkdir(parents=True)
        manifest = dict(commit='synthetic')
        if name == two:
            manifest['game_arguments'] = ['-SPRUNKNOWN']
        (golden/name/'manifest.json').write_text(json.dumps(manifest))
    common = f"-MockDirectory {q(mock)} -DataRoot {q(root)} -GoldenRoot {q(golden)} -SuitesFile {q(registry)}"
    extra = ['-ptapes\\demo1', '-rrun']
    arrays = ','.join(q(a) for a in extra)
    for stale in root.glob('args-*.json'):
        stale.unlink()
    # Dry plan: only the suite with registry arguments is listed, with its effective list.
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-suite-args')} -DryRun").stdout)
    assert plan['suite_game_arguments'] == {two: ['-SPRUNKNOWN']} and 'game_arguments' not in plan, plan
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-suite-args')} -DryRun -GameArguments {arrays}").stdout)
    assert plan['suite_game_arguments'] == {two: ['-SPRUNKNOWN'] + extra} and plan['game_arguments'] == extra, plan
    assert not (root/'dry-suite-args').exists()
    # Run: children get the registry file; the result of the suite with arguments names its list.
    out = root/'parallel-suite-args'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert 'game_arguments' not in summary, summary
    assert summary['results'] == [dict(suite=one, result='identical'), dict(suite=two, result='identical', game_arguments=['-SPRUNKNOWN']),
                                  dict(suite=three, result='identical')], summary
    for name in names:
        assert (root/f'suites-{name}.txt').read_text().strip() == str(registry), name
    out = root/'parallel-suite-args-given'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -GameArguments {arrays}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert summary['game_arguments'] == extra and summary['results'][1]['game_arguments'] == ['-SPRUNKNOWN'] + extra, summary
    assert 'game_arguments' not in summary['results'][0] and 'game_arguments' not in summary['results'][2], summary
    for name in names:
        assert json.loads((root/f'args-{name}.json').read_text()) == extra, name
    # A child that does not report the registry list it ran with is not a valid run.
    (root/f'{two}.dropown').touch()
    out = root/'parallel-suite-args-dropped'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)}", ok=False)
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert [r['result'] for r in summary['results']] == ['identical', 'DIFFERENT', 'identical'], summary
    (root/f'{two}.dropown').unlink()
    # Refused before an output directory, clone lock or child exists.
    for number, (bad, word) in enumerate((('-SPRUNKNOWN', 'repeats'), ('-Sother', 'option letter'), ('-X3', 'exit-code'))):
        refused = run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/('suite-refused%d' % number))} -GameArguments {q(bad)}", ok=False)
        assert 'GameArguments[0]' in refused.stderr and word in refused.stderr, refused.stderr
    (golden/two/'manifest.json').write_text(json.dumps(dict(commit='synthetic')))
    refused = run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'suite-refused-golden')}", ok=False)
    assert 'golden manifest' in refused.stderr and two in refused.stderr, refused.stderr
    (golden/two/'manifest.json').write_text(json.dumps(dict(commit='synthetic', game_arguments=['-SPRUNKNOWN'])))
    entries[0]['game_arguments'] = ['-SPRUNKNOWN']
    registry.write_text(json.dumps(dict(format=3, suites=entries)))
    refused = run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'suite-refused-golden2')}", ok=False)
    assert 'golden manifest' in refused.stderr and one in refused.stderr, refused.stderr
    # The same mismatch on a suite that is not selected does not matter.
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'suite-unselected')} -Suite {two}")
    assert not list(root.glob('suite-refused*')) and (root/'suite-unselected').exists()
    for stale in root.glob('args-*.json'):
        stale.unlink()


def parallel_dispatch(root, mock, parallel):
    """Suites start longest first (golden tick count), one to six clones; results keep the
    registry order. The golden tick count is read from manifest.json (launch.ticks_seen, capped by the
    registry frame_limit), else the head of normalized.json, else the registry frame_limit, else 0."""
    names = ['d1', 'd2', 'd3', 'd4', 'd5', 'd6']
    entries = [dict(name='d1'), dict(name='d2'), dict(name='d3', frame_limit=400), dict(name='d4', frame_limit=450),
               dict(name='d5'), dict(name='d6')]
    registry = root/'dispatch-registry.json'
    registry.write_text(json.dumps(dict(format=3, suites=entries)))
    golden = root/'dispatch-golden'
    for name in names:
        (golden/name).mkdir(parents=True)
    def manifest(name, ticks):
        (golden/name/'manifest.json').write_text(json.dumps(dict(commit='synthetic', launch=dict(ticks_seen=ticks))))
    manifest('d1', 100)                      # manifest
    (golden/'d2'/'normalized.json').write_text('{"format":1,"frame_limit":null,"tick_count":500,"ticks":[[0,1]]}')   # normalized head
    manifest('d3', 900)                      # capped by frame_limit 400
    # d4: no golden at all, only frame_limit 450
    (golden/'d5'/'manifest.json').write_text('{ not json')        # unreadable: unknown, 0
    manifest('d6', 400)                      # ties d3: registry order
    expected = ['d2', 'd4', 'd3', 'd6', 'd1', 'd5']
    common = f"-MockDirectory {q(mock)} -DataRoot {q(root)} -GoldenRoot {q(golden)} -SuitesFile {q(registry)}"
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-dispatch')} -DryRun").stdout)
    assert plan['suites'] == names and plan['dispatch'] == expected and plan['dispatch_order'] == 'longest', plan
    assert plan['dispatch_basis'] == {'d1': dict(ticks=100, source='manifest'), 'd2': dict(ticks=500, source='normalized'),
                                      'd3': dict(ticks=400, source='manifest'), 'd4': dict(ticks=450, source='frame_limit'),
                                      'd5': dict(ticks=0, source='unknown'), 'd6': dict(ticks=400, source='manifest')}, plan
    plan = json.loads(run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/'dry-dispatch')} -DryRun -DispatchOrder registry").stdout)
    assert plan['dispatch'] == names and plan['dispatch_order'] == 'registry' and not (root/'dry-dispatch').exists(), plan
    six = ['timing-r111'] + [f'timing-r111-p{n}' for n in range(1, 6)]
    # Six clones: the first six suites start in one sweep, longest first, one per clone, in clone order.
    out = root/'parallel-dispatch'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -InstallName {','.join(q(c) for c in six)}")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert [r['suite'] for r in summary['results']] == names and all(r['result'] == 'identical' for r in summary['results']), summary
    record = json.loads((out/'dispatch.json').read_text(encoding='utf-8-sig'))
    assert record['planned'] == expected and record['started'] == [f'{s}@{c}' for s, c in zip(expected, six)], record
    # Two clones: the start order is the dispatch order (which clone takes which follows the timing).
    out = root/'parallel-dispatch-two'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -InstallName timing-r111-p1,timing-r111-p2")
    summary = json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))
    assert [r['suite'] for r in summary['results']] == names, summary
    record = json.loads((out/'dispatch.json').read_text(encoding='utf-8-sig'))
    assert [x.split('@')[0] for x in record['started']] == expected and record['started'][:2] == [
        'd2@timing-r111-p1', 'd4@timing-r111-p2'], record
    # -DispatchOrder registry starts in registry order; the summary is the same.
    out = root/'parallel-dispatch-registry'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -InstallName timing-r111-p1 -DispatchOrder registry")
    record = json.loads((out/'dispatch.json').read_text(encoding='utf-8-sig'))
    assert [x.split('@')[0] for x in record['started']] == names and record['dispatch_order'] == 'registry', record
    assert json.loads((out/'summary.json').read_text(encoding='utf-8-sig'))['results'] == summary['results']
    # A selection keeps the dispatch rule on the selected suites only.
    out = root/'parallel-dispatch-selected'
    run_code(f"& {q(parallel)} {common} -OutputDirectory {q(out)} -InstallName timing-r111-p1 -Suite d1,d6,d3")
    assert json.loads((out/'dispatch.json').read_text(encoding='utf-8-sig'))['planned'] == ['d3', 'd6', 'd1']
    # More than six clones, repeated names and native-r111 are refused before anything is created.
    for number, (clones, word) in enumerate(((six + ['timing-r111-p6'], 'six clones'), (['timing-r111-p1', 'timing-r111-p1'], 'unique'), (['native-r111'], 'protected'))):
        refused = run_code(f"& {q(parallel)} {common} -OutputDirectory {q(root/('dispatch-refused%d' % number))} "
                           f"-InstallName {','.join(q(c) for c in clones)}", ok=False)
        assert word in refused.stderr and not (root/('dispatch-refused%d' % number)).exists(), refused.stderr


# Synthetic inventory for the -LiveCoverage runs: three hooks, gamma branchless.
COVERAGE_SITES = [('alpha', '0x00401000', 'je'), ('alpha', '0x00401010', 'jne'), ('beta', '0x00401020', 'jl')]
# (fall, taken) per site and suite. 'original': every hook original; 'run':
# alpha replaced, so only its bypassed call (suite one, 0x401010) reaches original code.
COVERAGE_COUNTS = {
    'original': {'one': [(2, 0), (0, 0), (0, 1)], 'two': [(0, 1), (0, 0), (0, 0)], 'three': [(0, 0), (0, 0), (0, 0)]},
    'run': {'one': [(0, 0), (1, 0), (1, 1)], 'two': [(0, 0), (0, 0), (0, 0)], 'three': [(0, 0), (0, 0), (0, 0)]},
}
COVERAGE_CHILD = r'''
param($Mode,$Suite,$InstallName,$OutputDirectory,$DataRoot,$GoldenRoot,$RepositoryRoot,$Python,$Hooks='',$SuiteTimeoutSeconds=300,$SuitesFile='')
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$config=$env:ES2_LIVE_COVERAGE_CONFIG
@{coverage_config=$config;speed_config=$env:ES2_ORACLE_SPEED_CONFIG;hooks=$Hooks;timeout=[int]$SuiteTimeoutSeconds} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'mock-env.json')
$normalized=Get-Content -LiteralPath (Join-Path $GoldenRoot "$Suite/normalized.json") -Raw
$result='identical'
if($Hooks -and (Test-Path (Join-Path $DataRoot ($Suite+'.fail')))) { $result='DIFFERENT' }
# A simulation perturbed while coverage traps: the comparison differs.
if($config -and (Test-Path (Join-Path $DataRoot ($Suite+'.perturb')))) { $normalized=$normalized.Replace('141','142');$result='DIFFERENT' }
Set-Content -LiteralPath (Join-Path $OutputDirectory "$Suite-normalized.json") -Value $normalized
# A held suite (registry hold) reports the tick record it parked after, as run-tick-log-mission.ps1 does.
$launch=@{pid=4242}
$holdMarker=Join-Path $DataRoot ($Suite+'.hold')
if(Test-Path -LiteralPath $holdMarker) { $launch.hold_frame=[int](Get-Content -LiteralPath $holdMarker) }
@{suite=$Suite;mode=$Mode;hooks=$Hooks;result=$result;proxy_sha256='synthetic-proxy';launch=$launch} | ConvertTo-Json -Depth 4 |
    Set-Content (Join-Path $OutputDirectory "$Suite-entry.json")
if($config) {
    # Stand-in for the proxy: publish the prepared report, in the configured mode, at the configured path.
    $text=Get-Content -LiteralPath $config -Raw
    $report=[regex]::Match($text,'report=(.+)').Groups[1].Value.Trim()
    $source=Join-Path $PSScriptRoot ($Suite+'-'+$(if($Hooks){'run'}else{'original'})+'.json')
    $coverageMode=[regex]::Match($text,'\nmode=(\w+)').Groups[1].Value
    $json=(Get-Content -LiteralPath $source -Raw).Replace('"mode": "saturate"','"mode": "'+$coverageMode+'"')
    if($coverageMode -eq 'edge') {
        # Edge mode: echo the known= file the way live_coverage.h reports it.
        $known=@{};$file=[regex]::Match($text,'known=(.+)').Groups[1].Value.Trim()
        if($file) { foreach($line in Get-Content -LiteralPath $file) { $f=$line -split ' ';$known[$f[0]]=@([int]$f[1],[int]$f[2]) } }
        $r=$json | ConvertFrom-Json;$n=0
        foreach($s in $r.sites) { $k=$known[$s.va];$kf=[bool]($k -and $k[0]);$kt=[bool]($k -and $k[1]);$n+=[int]$kf+[int]$kt
            $s | Add-Member known_fall $kf;$s | Add-Member known_taken $kt }
        $r | Add-Member known_outcomes $n;$r | Add-Member edge_roles @{jcc=0;redirect=0;fall=0;taken=0}
        $json=$r | ConvertTo-Json -Depth 6
    }
    [IO.File]::WriteAllText($report,$json)
}
@{mode=$Mode;output=$OutputDirectory;commit='synthetic';dirty_tree=$false;hooks=$Hooks;results=@(@{suite=$Suite;result=$result})} |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'summary.json')
if($result -ne 'identical'){exit 1}
'''


def live_coverage_runs(root, setup, local, parallel):
    """run-oracle-parallel.ps1 -LiveCoverage run|original with mock children that
    publish synthetic proxy reports; the real live_coverage_union.py validates."""
    tools = local.parent
    for name in ('live_coverage_union.py', 'live_coverage_report.py', 'live_coverage_column.py', 'oracle_regression.py',
                 'mission_watch_report.py'):
        shutil.copyfile(setup.parent/'scripts'/name, tools/'scripts'/name)
    (tools/'proxy/hook-names.txt').write_text('alpha\nbeta\ngamma\n')
    (tools/'invitro').mkdir()
    (tools/'invitro/branch_sites.h').write_text(''.join(f'BRANCH({h},{va},2,{c},0x401100)\n' for h, va, c in COVERAGE_SITES))
    # Suite two runs to its natural exit (no frame window).
    (tools/'oracle-suites.json').write_text(json.dumps(dict(suites=[dict(name='one', frame_limit=3), dict(name='two', outcome=True),
                                                                    dict(name='three', frame_limit=3)])))
    data = root/'cdata'; data.mkdir()
    golden = root/'cgolden'
    cmock = root/'cmock'; cmock.mkdir()
    (cmock/'run-oracle-regression.ps1').write_text(COVERAGE_CHILD)
    limits = dict(one=3, two=None, three=3)
    for suite, limit in limits.items():
        (golden/suite).mkdir(parents=True)
        (golden/suite/'normalized.json').write_text(json.dumps(dict(format=1, frame_limit=limit, tick_fields=['frame', 'clock'],
            ticks=[[0, 47], [1, 94], [2, 141]], watches=[], outputs={}, last_frame=2)))
        for kind, counts in COVERAGE_COUNTS.items():
            modes = dict(alpha=1 if kind == 'run' else 0, beta=0, gamma=0)
            sites = [dict(hook=h, va=va, condition=c, fall=f, taken=t, restored=True) for (h, va, c), (f, t) in zip(COVERAGE_SITES, counts[suite])]
            (cmock/f'{suite}-{kind}.json').write_text(json.dumps(dict(
                schema=1, module_sha256='8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad', mode='saturate', pid=4242,
                install_thread=9, first_thread=9, other_thread=0, tick_thread=9, error=0, complete=True,
                reason='frame_limit' if limit else 'exit', has_tick=True, last_frame=2, frame_limit=limit or 0, first_hit_ticks=False,
                hooks=[dict(name=n, mode=m) for n, m in modes.items()], sites=sites)))

    def coverage(out, *extra, ok=True):
        return run(parallel, '-MockDirectory', cmock, '-DataRoot', data, '-GoldenRoot', golden, '-Python', sys.executable,
                   '-OutputDirectory', out, *extra, ok=ok)

    def read(path):
        return json.loads(path.read_text(encoding='utf-8-sig'))

    def ini(path):
        return path.read_text(encoding='utf-16')

    # In-run union under the run's own hook modes.
    out = root/'cov-run'
    coverage(out, '-LiveCoverage', 'run', '-Hooks', 'alpha=replaced')
    summary = read(out/'summary.json')
    assert [r['result'] for r in summary['results']] == ['identical'] * 3 and summary['hooks'] == 'alpha=replaced'
    assert summary['live_coverage']['valid'] is True and summary['live_coverage']['mode'] == 'run', summary
    union = read(out/'coverage-union.json')
    assert union['valid'] and union['kind'] == 'run' and union['residual_hooks'] == ['alpha'], union
    assert union['totals']['both_ways'] == 1 and union['totals']['never'] == 1 and union['totals']['fall_only'] == 1
    for suite, limit in limits.items():
        text = ini(out/suite/'coverage.ini')
        assert 'enabled=1' in text and 'hooks=all' in text and 'mode=saturate' in text and f'frame_limit={limit or 0}' in text, text
        assert f"report={out/suite/'coverage.json'}" in text and 'report_every_ticks=0' in text, text
        assert read(out/suite/'run/mock-env.json')['coverage_config'] == str(out/suite/'coverage.ini')
        assert read(out/suite/'run/mock-env.json')['timeout'] == 3600
        assert read(out/suite/'coverage-summary.json')['valid'] and (out/suite/'coverage-summary.md').exists()
    assert '| all 3 hooks (3 sites) | 1 / 1 / 1 |' in (out/'coverage-column.md').read_text()
    # Original-mode union: the verdict pass is uninstrumented, then a second
    # pass with every hook original carries [coverage].
    out = root/'cov-original'
    coverage(out, '-LiveCoverage', 'original', '-Hooks', 'alpha=replaced', '-CoverageMode', 'count', '-CoverageReportEveryTicks', 64,
             '-CoverageTimeoutSeconds', 900)
    summary = read(out/'summary.json')
    assert [r['result'] for r in summary['results']] == ['identical'] * 3 and summary['hooks'] == 'alpha=replaced'
    assert summary['live_coverage']['valid'] is True and summary['live_coverage']['suites'] == str(out/'coverage-original')
    for suite in limits:
        env = read(out/suite/'run/mock-env.json')
        assert env['coverage_config'] is None and env['hooks'] == 'alpha=replaced' and env['timeout'] == 300, env
        assert not (out/suite/'coverage.ini').exists()
        env = read(out/'coverage-original'/suite/'run/mock-env.json')
        assert env['coverage_config'] == str(out/'coverage-original'/suite/'coverage.ini') and env['hooks'] == '', env
        assert env['timeout'] == 900, env
        text = ini(out/'coverage-original'/suite/'coverage.ini')
        assert 'mode=count' in text and 'report_every_ticks=64' in text, text
    assert read(out/'coverage-original/summary.json')['hooks'] == ''
    union = read(out/'coverage-union.json')
    assert union['valid'] and union['kind'] == 'original' and union['all_hooks_original'], union
    assert '| `alpha` | 1 / 0 / 1 |' in (out/'coverage-column.md').read_text()
    # Edge mode against that union as baseline: the runner writes the known= file
    # once, every proxy INI names it, and the union validates the echoed bits.
    baseline = out/'coverage-union.json'
    out = root/'cov-edge'
    coverage(out, '-LiveCoverage', 'run', '-CoverageMode', 'edge', '-CoverageBaseline', baseline)
    assert (out/'coverage-known.txt').read_text() == '0x00401000 1 1\n0x00401020 0 1\n'
    for suite in limits:
        text = ini(out/suite/'coverage.ini')
        assert 'mode=edge' in text and f"known={out/'coverage-known.txt'}" in text, text
    union = read(out/'coverage-union.json')
    assert union['valid'] and union['mode'] == 'edge' and union['baseline_outcomes'] == 3 and union['baseline'] == str(baseline), union
    assert read(out/'summary.json')['live_coverage']['valid'] is True
    # Without a baseline an edge pass has no known= line.
    out = root/'cov-edge-plain'
    coverage(out, '-LiveCoverage', 'run', '-CoverageMode', 'edge')
    assert 'known=' not in ini(out/'one'/'coverage.ini') and read(out/'coverage-union.json')['baseline_outcomes'] == 0
    # A baseline needs edge mode and an existing union; refused before any output.
    for extra in (('-CoverageMode', 'saturate'), ('-CoverageMode', 'edge', '-CoverageBaseline', root/'missing.json')):
        args = ('-LiveCoverage', 'run') + extra + (() if '-CoverageBaseline' in extra else ('-CoverageBaseline', baseline))
        coverage(root/'cov-edge-refused', *args, ok=False)
        assert not (root/'cov-edge-refused').exists()
    plan = run(parallel, '-MockDirectory', cmock, '-DataRoot', data, '-OutputDirectory', root/'cov-edge-dry', '-LiveCoverage', 'run',
               '-CoverageMode', 'edge', '-CoverageBaseline', baseline, '-DryRun')
    plan = json.loads(plan.stdout[plan.stdout.index('{'):])
    assert plan['coverage_mode'] == 'edge' and plan['coverage_baseline'] == str(baseline), plan
    # A failed regression skips the coverage pass; the exit code stays the verdict's.
    (data/'two.fail').touch()
    out = root/'cov-skipped'
    coverage(out, '-LiveCoverage', 'original', '-Hooks', 'alpha=replaced', ok=False)
    assert not (out/'coverage-original').exists() and not read(out/'coverage-union.json')['valid']
    assert read(out/'summary.json')['live_coverage']['error'].startswith('Skipped')
    (data/'two.fail').unlink()
    # Coverage that perturbs the simulation: in the original pass it invalidates
    # only the union; in run mode it is a DIFFERENT suite of the run itself.
    (data/'one.perturb').touch()
    out = root/'cov-perturbed'
    coverage(out, '-LiveCoverage', 'original', '-Hooks', 'alpha=replaced')
    summary = read(out/'summary.json')
    assert [r['result'] for r in summary['results']] == ['identical'] * 3 and summary['live_coverage']['valid'] is False
    assert read(out/'coverage-original/summary.json')['results'][0]['result'] == 'DIFFERENT'
    assert read(out/'coverage-original/two/coverage-summary.json')['valid']
    assert not read(out/'coverage-original/one/coverage-summary.json')['valid']
    out = root/'cov-run-perturbed'
    coverage(out, '-LiveCoverage', 'run', ok=False)
    assert read(out/'summary.json')['results'][0]['result'] == 'DIFFERENT' and not read(out/'coverage-union.json')['valid']
    (data/'one.perturb').unlink()
    # A suite with hold ends parked after its tick count, with no exit and no registry frame_limit: the
    # proxy window is frozen at that tick (frame_limit = ticks, last frame ticks - 1) and the union checks it
    # against the launch's hold_frame.
    held = root/'hold-suites.json'
    held.write_text(json.dumps(dict(suites=[dict(name='four', ticks=3, outcome=False, hold=True)])))
    (golden/'four').mkdir()
    (golden/'four'/'normalized.json').write_text(json.dumps(dict(format=1, frame_limit=None, tick_fields=['frame', 'clock'],
        ticks=[[0, 47], [1, 94], [2, 141]], watches=[], outputs={}, last_frame=2)))
    (data/'four.hold').write_text('3')
    sites = [dict(hook=h, va=va, condition=c, fall=f, taken=t, restored=True) for (h, va, c), (f, t) in zip(COVERAGE_SITES, COVERAGE_COUNTS['original']['one'])]
    def held_report(**changes):
        report = dict(schema=1, module_sha256='8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad', mode='saturate', pid=4242,
                      install_thread=9, first_thread=9, other_thread=0, tick_thread=9, error=0, complete=True, reason='frame_limit',
                      has_tick=True, last_frame=2, frame_limit=3, first_hit_ticks=False,
                      hooks=[dict(name=n, mode=0) for n in ('alpha', 'beta', 'gamma')], sites=sites)
        (cmock/'four-original.json').write_text(json.dumps(dict(report, **changes)))
    held_report()
    out = root/'cov-hold'
    coverage(out, '-SuitesFile', held, '-LiveCoverage', 'run')
    assert 'frame_limit=3' in ini(out/'four'/'coverage.ini')
    assert read(out/'four/run/four-entry.json')['launch']['hold_frame'] == 3
    assert read(out/'summary.json')['live_coverage']['valid'] is True and read(out/'coverage-union.json')['valid']
    # An exit-time report, a window of another length, a hold that does not end the golden and a launch without
    # the hold do not validate.
    bad = (dict(reason='exit', frame_limit=0), '3'), (dict(frame_limit=2, last_frame=1), '3'), (dict(), '2'), (dict(), None)
    for number, (changes, marker) in enumerate(bad):
        held_report(**changes)
        if marker is None: (data/'four.hold').unlink()
        else: (data/'four.hold').write_text(marker)
        out = root/f'cov-hold-bad{number}'
        coverage(out, '-SuitesFile', held, '-LiveCoverage', 'run')
        assert read(out/'summary.json')['live_coverage']['valid'] is False and not read(out/'coverage-union.json')['valid'], number
    (cmock/'four-original.json').unlink()
    # Refusals before any output: profiling with traps, an unknown value, a path the proxy would refuse.
    for extra in (('-LiveCoverage', 'run', '-Perf'), ('-LiveCoverage', 'always')):
        coverage(root/'cov-refused', *extra, ok=False)
    coverage(root/('cov-'+'x'*200), '-LiveCoverage', 'run', ok=False)
    assert not (root/'cov-refused').exists() and not (root/('cov-'+'x'*200)).exists()
    plan = run(parallel, '-MockDirectory', cmock, '-DataRoot', data, '-OutputDirectory', root/'cov-dry', '-LiveCoverage', 'original', '-DryRun')
    plan = json.loads(plan.stdout[plan.stdout.index('{'):])
    assert plan['live_coverage'] == 'original' and plan['coverage_root'] == str(root/'cov-dry'/'coverage-original')


if __name__ == '__main__':
    try:
        main()
    finally:
        if SESSION is not None:
            SESSION.dispose()
