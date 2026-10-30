# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Actual PowerShell dry-run/mock integration plus strict comparator negatives."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
import types

SETUP = Path(sys.argv[1]).resolve().parent
REPO = SETUP.parents[2]
SCRIPTS = SETUP.parent / "scripts"
# PowerShell 7 from the test environment (Bazel sets ES2_PWSH), else from PATH,
# else Windows PowerShell.
PWSH = Path(os.environ.get("ES2_PWSH") or shutil.which("pwsh") or "")
if not PWSH.name or not PWSH.exists():
    PWSH = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SETUP))
from pwsh_session import PwshSession, shard_items
import oracle_regression as oracle
import recording_compare as compare
import propose_watch_set as watch
import live_coverage_report as coverage
import suite_coverage_delta as coverage_delta

checks = 0
steps = 0
count_lock = threading.Lock()


def check(value, detail=""):
    global checks
    with count_lock:
        checks += 1
    if not value:
        raise AssertionError(detail)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def lines(path, values):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(v)+"\n" for v in values), encoding="utf-8")


# Each lane (a section of this test with its own synthetic DataRoot) runs in a
# thread with one PowerShell process of its own; every step runs in a fresh
# runspace of that process (pwsh_session.py) instead of a process per step.
LOCAL = threading.local()


def ps(script, *args, ok=True, command=False):
    cmd = ["-Command", script] if command else ["-File", str(SETUP/script), *map(str,args)]
    global steps
    with count_lock:
        steps += 1
    session = LOCAL.session
    # Per step; 45 s was too short while parallel worker builds load the machine
    # (the lanes took over 45 s three times in one run and passed alone in 22 s).
    result = session.run(code=script, timeout=120) if command else session.run(file=SETUP/script, args=args, timeout=120)
    if (result.returncode == 0) != ok:
        print(json.dumps(dict(command=cmd,code=result.returncode,out=result.stdout,err=result.stderr)),flush=True)
    check((result.returncode == 0) == ok, (cmd, result.stdout, result.stderr))
    return result


def quote(path):
    return "'" + str(path).replace("'", "''") + "'"


def synthetic_mec():
    record = struct.pack("<4h", 123, 45, 0, 3) + struct.pack("<6h", 0, 1, 2, 5, 5, 5) + bytes(68)
    return struct.pack("<hh", 0, 2) + record*2 + struct.pack("<h",33) + bytes(33)


def synthetic_script():
    out = bytearray(20)
    for i, stride in enumerate((12,2,0,6,74,24,134,92,52,14,156,54,2)):
        n = {2:1,4:1,6:4,11:1}.get(i,0)
        out += struct.pack("<h",n)
        out += struct.pack("<3h",2,0,1) if i==2 else bytes(n*stride)
    return bytes(out)


def make_launch(root, name="launch-b"):
    directory = root/"oracle/launch"/name
    directory.mkdir(parents=True)
    data = {"script.dat":synthetic_script(), "player.mec":synthetic_mec(), "mission.var":bytes(2000), "MISSION.STR":b"synthetic-strings"}
    entries = []
    for file, payload in data.items():
        (directory/file).write_bytes(payload)
        entries.append(dict(file=file,size=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
    write(directory/"manifest.json",dict(schema=1,name=name,files=entries))
    return directory


def events(n=3, recorded=False):
    result = [dict(event="start",schema=2,record_limit=1000000,actor_raw=0,watches=0)]
    for i in range(n):
        tick = dict(event="tick",frame_counter_before_increment=i,**{f:0 for f in oracle.TICK_FIELDS})
        tick.update(completed_clock_ms=i*47,clock_raw_hex="00"*24,rng_a_hex="00"*114,rng_b_hex="00"*114,
                    position_raw_i32=[i,2,3],angles_raw_u16=[0,0,1],in_injected=0 if recorded else i,
                    in_fallbacks=0 if recorded else 4)
        result.append(tick)
    return result



def fixture(root):
    """Synthetic DataRoot with one clone, the launch set launch-b and a marker."""
    data = root/"data"; launch = make_launch(data)
    (data/'.es2-synthetic-fixture').write_text('synthetic suite fixtures only',encoding='ascii')
    install = data/"installs/test-clone"; clone = install/"DATA"; clone.mkdir(parents=True)
    write(install/"installation-manifest.json", dict(destination=str(install),measurement_clone_of="synthetic-source",language="German"))
    write(install/"oracle-media-manifest.json",dict(proxy_sha256="synthetic-proxy"))
    before = {name:("old-"+name).encode() for name in watch.FILES}
    for name, payload in before.items(): (clone/name).write_bytes(payload)
    # Synthetic stand-ins for the installation's TAPES directory (tape suites).
    tapes = install/"TAPES"; tapes.mkdir()
    for name in ("DEMO1.TAP", "DEMO2.TAP", "DEMO3.TAP"): (tapes/name).write_bytes(b"synthetic-tape-"+name.encode())
    (tapes/"DEMOLIST.STR").write_bytes(b"synthetic-demolist")
    watches = root/"watches.txt"; watches.write_text("",encoding="ascii")
    journal = root/"input.jsonl"; lines(journal,[dict(f=0,api="keydown",vk=32)])
    common = ["-DataRoot",data,"-InstallName","test-clone"]
    return types.SimpleNamespace(data=data, launch=launch, install=install, clone=clone, before=before,
                                 watches=watches, journal=journal, common=common)



def lane_dry(root):
    """Dry paths, protected clone names and the DBSIM argument pass-through of the dry plans."""
    fx = fixture(root)
    data, launch, install, clone, before, watches, journal, common = (fx.data, fx.launch, fx.install, fx.clone, fx.before, fx.watches, fx.journal, fx.common)
    # Dry paths never create output, inspect processes, configure or launch.
    # Registered suites with a launch set get a synthetic one here.
    suites = {s["name"]:s for s in json.loads((SETUP.parent/"oracle-suites.json").read_text())["suites"]}
    # Several suites may share one launch set (c3_10-loadout-hercs).
    for name in sorted({s["launch"] for s in suites.values() if s.get("launch")}): make_launch(data, name)
    out = root/"never-written"
    r = ps("run-oracle-regression.ps1",*common,"-DryRun","-OutputDirectory",out)
    plan = json.loads(r.stdout)
    check(not out.exists() and len(plan["plans"])==len(suites))
    for p in plan["plans"]:
        suite = suites[p["suite"]]
        if suite.get("launch"):
            check(p["launch_set"]["name"]==suite["launch"])
        else:
            check(p["launch_set"] is None)
        # The runner follows the outcome flag, with or without a launch set
        # (c3_10-own-building-fire is a launch-aware frame-limited suite).
        if suite.get("outcome"):
            check(p["runner"]=="run-mission-outcome.ps1")
            if p["suite"]=="c1_01-success": check(p["arguments"]["Ticks"]==8000)
        else:
            check(p["runner"]=="run-tick-log-mission.ps1" and p["arguments"]["VirtualTimeStepMs"]==47 and p["arguments"]["VirtualTimePace"]==0)
    ps("run-mission-outcome.ps1",*common,"-DryRun","-OutputDirectory",out,"-RunName","dry","-Script",journal,"-WatchFile",watches)
    ps("capture-launch-set.ps1",*common,"-Name","capture-dry","-DryRun")
    ps("record-suite.ps1",*common,"-Name","b","-Launch","launch-b","-WatchFile",watches,"-Ticks",3,"-FrameLimit",3,"-OutputDirectory",out,"-DryRun")
    check(not out.exists())
    # DBSIM argument pass-through, dry paths. Absent option: only a
    # suite with registry game_arguments (c1_01-free-camera) carries the
    # key, with its own list. Given: every plan carries its own list followed
    # by the given one in order, whether it arrives as a PowerShell array or
    # as the JSON form of the parallel runner.
    own = {n:s.get("game_arguments", []) for n, s in suites.items()}
    check(all(p["arguments"].get("GameArguments", [])==own[p["suite"]] for p in plan["plans"]) and "game_arguments" not in plan)
    check(all(own[p["suite"]] or "GameArguments" not in p["arguments"] for p in plan["plans"]))
    extra = ["-ptapes\\demo1", "-rrun", "-dsave"]
    arrays = ",".join(quote(a) for a in extra)
    regression, outcome = quote(SETUP/"run-oracle-regression.ps1"), quote(SETUP/"run-mission-outcome.ps1")
    base = f"-DataRoot {quote(data)} -InstallName test-clone -DryRun -OutputDirectory {quote(out)}"
    # Tape suites (a registry -r/-p/-D or demo_tape) take no further tape option.
    joinable = [n for n, s in suites.items() if not s.get("demo_tape") and not any(a[:2] in ("-r","-p","-D","-d") for a in own[n])]
    selection = "-Suite " + ",".join(quote(n) for n in joinable)
    given = json.loads(ps(f"& {regression} {base} {selection} -GameArguments {arrays}", command=True).stdout)
    check(len(given["plans"])==len(joinable) and all(p["arguments"]["GameArguments"]==own[p["suite"]]+extra for p in given["plans"]))
    check(all("tape_plan" not in p for p in given["plans"]))
    as_json = json.loads(ps(f"& {regression} {base} {selection} -GameArgumentsJson {quote(json.dumps(extra))}", command=True).stdout)
    check(as_json["plans"]==given["plans"])
    for p in given["plans"]:
        default = next(q for q in plan["plans"] if q["suite"]==p["suite"])["arguments"]
        rest = lambda a: {k:v for k,v in a.items() if k!="GameArguments"}
        check(rest(p["arguments"])==rest(default), p["suite"])
    outcome_args = f"-DataRoot {quote(data)} -InstallName test-clone -DryRun -OutputDirectory {quote(out)} -RunName dry -Script {quote(journal)} -WatchFile {quote(watches)}"
    check("GameArguments" not in json.loads(ps(f"& {outcome} {outcome_args}", command=True).stdout)["arguments"])
    check(json.loads(ps(f"& {outcome} {outcome_args} -GameArguments {arrays}", command=True).stdout)["arguments"]["GameArguments"]==extra)
    check(not out.exists())
    # Collisions and malformed lists are refused with the argument named, before any plan.
    for bad, word in (("-X3","exit-code option -X"),("-R7","counter option -R"),("-G","already passed"),("-Fx","language option"),
                      ("-EGGPLANT","already passed"),("/eggplant","-eggplant"),("-p x","whitespace"),("","is empty")):
        r = ps(f"& {regression} {base} -GameArguments {quote(bad)}", command=True, ok=False)
        check("GameArguments[0]" in r.stderr and word in r.stderr, (bad, r.stderr))
    second = quote(extra[0]) + "," + quote("-X9")
    r = ps(f"& {regression} {base} -GameArguments {second}", command=True, ok=False)
    check("GameArguments[1]" in r.stderr and "-X9" in r.stderr, r.stderr)
    r = ps("run-oracle-regression.ps1",*common,"-Mode","record","-DryRun","-OutputDirectory",out,"-GameArgumentsJson",json.dumps(extra),ok=False)
    check("recorded with the fixed DBSIM arguments" in r.stderr, r.stderr)
    r = ps(f"& {regression} {base} -GameArguments {arrays} -GameArgumentsJson {quote(json.dumps(extra))}", command=True, ok=False)
    check("not both" in r.stderr, r.stderr)
    r = ps("run-oracle-regression.ps1",*common,"-DryRun","-OutputDirectory",out,"-GameArgumentsJson",'{"a":1}',ok=False)
    check("JSON array of strings" in r.stderr, r.stderr)
    # -VirtualTimePace 1 reaches every tick-log plan; record mode refuses it.
    paced = json.loads(ps("run-oracle-regression.ps1",*common,"-DryRun","-OutputDirectory",out,"-VirtualTimePace","1").stdout)
    check(all(p["arguments"]["VirtualTimePace"]==1 for p in paced["plans"] if p["runner"]=="run-tick-log-mission.ps1"))
    r = ps("run-oracle-regression.ps1",*common,"-Mode","record","-DryRun","-OutputDirectory",out,"-VirtualTimePace","1",ok=False)
    check("recorded unpaced" in r.stderr, r.stderr)
    check(not out.exists())
    dry_suite_game_arguments(root, common, suites)
    for name in ("native-r111","../native-r111","test-clone/../native-r111"):
        ps("run-oracle-regression.ps1","-InstallName",name,"-DryRun",ok=False)


def lane_transactions(root):
    """Process ownership, transactions, launch manifests, capture."""
    fx = fixture(root)
    data, launch, install, clone, before, watches, journal, common = (fx.data, fx.launch, fx.install, fx.clone, fx.before, fx.watches, fx.journal, fx.common)
    # Transaction: modified, deleted, newly created and absent files, plus
    # exception midway through action. All six states must be restored.
    module = SETUP/"SuiteLaunch.psm1"
    unmarked=root/'unmarked';unmarked.mkdir()
    ps(f"Import-Module {quote(module)}; Assert-SuiteMockRoot {quote(unmarked)}",command=True,ok=False)
    # 'slow': the stopped process stays listed for five more polls (under load,
    # check-r6-set3); 'stuck': it never leaves, so the stop refuses after the wait.
    for case in ('owned','pid','path','start','historical','slow','stuck'):
        stop_marker=root/(case+'-stopped')
        owned_report=root/(case+'-launch.json')
        start='2026-10-07T12:00:01Z'
        write(owned_report,dict(pid=123,exe=str(install/'DBSIM.EXE'),process_start_utc=start))
        process_id=124 if case=='pid' else 123
        process_path=str(install/('OTHER.EXE' if case=='path' else 'DBSIM.EXE'))
        process_start='2026-10-07T12:00:02Z' if case=='start' else start
        not_before='2026-10-08T00:00:00Z' if case=='historical' else '2026-10-07T12:00:00Z'
        linger={'slow':5,'stuck':-1}.get(case,0)
        wait=' 0' if case=='stuck' else ''
        code=f"""$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force
& (Get-Module SuiteLaunch) {{
 $script:Stopped=$false; $script:Polls=0
 function script:Get-SuiteProcesses {{
  $p=@([pscustomobject]@{{ProcessId={process_id};ExecutablePath={quote(process_path)};CreationDate=[DateTime]'{process_start}'}})
  if(!$script:Stopped){{return $p}}
  $script:Polls++
  if({linger} -lt 0 -or $script:Polls -le {linger}){{return $p}}
  return @()
 }}
 function script:Get-Process {{param($Id,$ErrorAction);return [pscustomobject]@{{Id=$Id;Path={quote(process_path)};StartTime=[DateTime]'{process_start}'}}}}
 function script:Get-CloneProcessImagePath {{param($Id);return {quote(process_path)}}}
 function script:Stop-Process {{param($InputObject,$ErrorAction);$script:Stopped=$true;[IO.File]::WriteAllText({quote(stop_marker)},[string]$InputObject.Id)}}
 function script:Wait-Process {{param($Id,$Timeout,$ErrorAction)}}
 function script:Start-Sleep {{param($Milliseconds)}}
}}
Stop-SuiteOwnedProcess {quote(install)} {quote(owned_report)} ([DateTime]'{not_before}'){wait}
"""
        r=ps(code,command=True,ok=case in ('owned','slow'))
        check(stop_marker.exists()==(case in ('owned','slow','stuck')),case)
        if case=='stuck':check('still busy 0 s after the stop' in r.stderr,r.stderr)
    for throws in (False,True):
        transaction = root/("transaction-fail" if throws else "transaction-ok")
        code = f"""$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force
$action={{
 if([IO.File]::ReadAllBytes({quote(clone/'player.mec')}).Length -ne 215){{throw 'launch not deployed'}}
 [IO.File]::WriteAllText({quote(clone/'results.dat')},'new-result')
 Remove-Item -LiteralPath {quote(clone/'mission.var')}
 [IO.File]::WriteAllText({quote(clone/'EXIT.CFG')},'new-exit')
 [IO.File]::WriteAllText({quote(clone/'PREFS.CFG')},'joystick-mapping')
 {'throw "synthetic run failure"' if throws else "'{}'"}
}}
Invoke-SuiteTransaction -DataRoot {quote(data)} -InstallName test-clone -Launch launch-b -OutputDirectory {quote(transaction)} -RunName test -Mock -Action $action | Out-Null
"""
        ps(code,command=True,ok=not throws)
        check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
        check((transaction/"test-output/results.dat").read_text()=="new-result")
        # DATA/PREFS.CFG is restored (here: removed again) and never captured.
        check(not (transaction/"test-output/PREFS.CFG").exists())
        check(not list(transaction.glob(".backup-*")))
    # Save-/Restore-SuiteConfigFiles: run-oracle-regression.ps1 wraps every suite in them, whatever
    # runner it takes (the joystick suites have no launch set, so the transaction never saw their
    # PREFS.CFG; check-seq-8c). A changed file gets its bytes, write time and attributes back, an
    # absent one is removed again, an unchanged one is left alone.
    prefs = clone/'PREFS.CFG'
    prefs.write_bytes(b'keyboard-mapping')
    os.utime(prefs, (1600000000, 1600000000))
    code = f"""$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force
$saved = Save-SuiteConfigFiles {quote(install)}
[IO.File]::WriteAllText({quote(prefs)},'joystick-mapping')
Set-ItemProperty -LiteralPath {quote(prefs)} -Name IsReadOnly -Value $true
$first = Restore-SuiteConfigFiles $saved
$second = Restore-SuiteConfigFiles $saved
"$($first -join ',')|$(@($second).Count)"
"""
    r = ps(code, command=True)
    check(r.stdout.strip().splitlines()[-1] == 'DATA/PREFS.CFG|0', r.stdout)
    check(prefs.read_bytes() == b'keyboard-mapping' and int(prefs.stat().st_mtime) == 1600000000 and os.access(prefs, os.W_OK))
    prefs.unlink()
    code = f"""$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force
$saved = Save-SuiteConfigFiles {quote(install)}
[IO.File]::WriteAllText({quote(prefs)},'written by the game')
"$((Restore-SuiteConfigFiles $saved) -join ',')"
"""
    r = ps(code, command=True)
    check(r.stdout.strip().splitlines()[-1] == 'DATA/PREFS.CFG' and not prefs.exists(), r.stdout)
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    # Inject failures in the module's Copy-Item command, never in the OS or
    # another script. Partial deployment must roll back; a restore failure
    # must retain its recoverable backup and still restore later members.
    for phase in ('deploy','restore','capture'):
        transaction=root/('fault-'+phase)
        condition={
            'deploy':"$LiteralPath -match '[\\\\/]oracle[\\\\/]launch[\\\\/]' -and $Destination -like '*mission.var'",
            'restore':"$LiteralPath -match '[\\\\/]\\.backup-' -and $Destination -like '*script.dat'",
            'capture':"$Destination -match 'test-output'",
        }[phase]
        code=f"""$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force
& (Get-Module SuiteLaunch) {{
 function script:Copy-Item {{
  param([string]$LiteralPath,[string]$Destination,[switch]$Force)
  if({condition}){{throw 'synthetic {phase} failure'}}
  Microsoft.PowerShell.Management\\Copy-Item @PSBoundParameters
 }}
}}
Invoke-SuiteTransaction -DataRoot {quote(data)} -InstallName test-clone -Launch launch-b -OutputDirectory {quote(transaction)} -RunName test -Mock -Action {{ [IO.File]::WriteAllText({quote(clone/'results.dat')},'new-result'); '{{}}' }} | Out-Null
"""
        ps(code,command=True,ok=False)
        for name,payload in before.items():
            if phase!='restore' or name!='script.dat':check((clone/name).read_bytes()==payload,(phase,name))
        backups=list(transaction.glob('.backup-*'))
        check(bool(backups)==(phase!='deploy'))
        if backups:check(json.loads((backups[0]/'restore.json').read_text())['before']['script.dat']['sha256']==hashlib.sha256(before['script.dat']).hexdigest())
        for name,payload in before.items():(clone/name).write_bytes(payload)
        check(not (clone/'results.dat').exists() and not (clone/'EXIT.CFG').exists())
    # Bad hash, duplicate/path traversal members, wrong size/name, and
    # missing files are refused before deployment.
    manifest_path=launch/"manifest.json"; original=json.loads(manifest_path.read_text())
    mutations=[]
    for kind in ("hash","size","duplicate","path","name","schema"):
        m=copy.deepcopy(original)
        if kind=="hash":m["files"][0]["sha256"]="0"*64
        if kind=="size":m["files"][0]["size"]+=1
        if kind=="duplicate":m["files"][1]=m["files"][0]
        if kind=="path":m["files"][0]["file"]="../script.dat"
        if kind=="name":m["name"]="elsewhere"
        if kind=="schema":m["schema"]=2
        mutations.append(m)
    for m in mutations:
        write(manifest_path,m)
        ps(f"Import-Module {quote(module)}; Read-SuiteLaunch {quote(data)} launch-b",command=True,ok=False)
        check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    write(manifest_path,original)
    # New-process capture, stale identity, exit during copy and timeout.
    capture_mock=root/"capture-mock"; process=dict(pid=123,exe=str(install/"DBSIM.EXE"),process_start_utc="2026-10-07T12:00:01Z")
    for name,polls,ok in (("captured",[[],[process],[process]],True),("existing",[[process]],False),
                          ("gone",[[],[process],[]],False),("timeout",[[],[]],False),
                          ("historical",[[],[dict(process,process_start_utc="2026-10-06T00:00:00Z")]],False)):
        write(capture_mock/"process-samples.json",dict(armed_utc="2026-10-07T12:00:00Z",polls=polls))
        ps("capture-launch-set.ps1",*common,"-Name",name,"-MockDirectory",capture_mock,ok=ok)
        check((data/"oracle/launch"/name/"manifest.json").exists()==ok)


def draft_registry(path, entries, version=3):
    """Writes a draft registry (format `version`) and returns its path."""
    write(path, dict(format=version, suites=entries))
    return path


def dry_suite_game_arguments(root, common, suites):
    """The registry field game_arguments in dry plans, with the command-line option, and refused values."""
    plain = suites["acceptance-60s"]
    cheat = dict(plain, name="cheat-a", game_arguments=["-SPRUNKNOWN"])
    registry = draft_registry(root/"cheat-dry.json", [cheat, plain])
    out = root/"never-written-cheat"
    dry = ["-DryRun", "-OutputDirectory", out, "-SuitesFile", registry]
    plans = {p["suite"]: p for p in json.loads(ps("run-oracle-regression.ps1", *common, *dry).stdout)["plans"]}
    check(plans["cheat-a"]["arguments"]["GameArguments"] == ["-SPRUNKNOWN"] and "GameArguments" not in plans["acceptance-60s"]["arguments"], plans)
    # Apart from the name and the argument the plan is the plain suite's.
    drop = lambda p: {k: v for k, v in p["arguments"].items() if k not in ("GameArguments", "RunName")}
    check(drop(plans["cheat-a"]) == drop(plans["acceptance-60s"]))
    # Recording takes the registry arguments; the command line is refused there and is appended in check mode.
    record = json.loads(ps("run-oracle-regression.ps1", *common, "-Mode", "record", *dry).stdout)
    check({p["suite"]: p["arguments"].get("GameArguments") for p in record["plans"]} == {"cheat-a": ["-SPRUNKNOWN"], "acceptance-60s": None})
    extra = ["-ptapes\\demo1", "-rrun"]
    given = {p["suite"]: p for p in json.loads(ps("run-oracle-regression.ps1", *common, *dry, "-GameArgumentsJson", json.dumps(extra)).stdout)["plans"]}
    check(given["cheat-a"]["arguments"]["GameArguments"] == ["-SPRUNKNOWN"] + extra and given["acceptance-60s"]["arguments"]["GameArguments"] == extra)
    ps("run-oracle-regression.ps1", *common, "-Mode", "record", *dry, "-GameArgumentsJson", json.dumps(extra), ok=False)
    # A command-line argument that repeats or shares the option letter of a registry argument is refused, naming the suite.
    for bad, word in (("-SPRUNKNOWN", "repeats"), ("-sprunknown", "repeats"), ("-Sother", "option letter"), ("-X3", "exit-code option -X")):
        r = ps("run-oracle-regression.ps1", *common, *dry, "-GameArgumentsJson", json.dumps([bad]), ok=False)
        check("GameArguments[0]" in r.stderr and word in r.stderr and (bad == "-X3" or "cheat-a" in r.stderr), (bad, r.stderr))
    # Bad registry fields are refused with the suite named, before any plan.
    for field, word in (("-SPRUNKNOWN", "array of strings"), ([1], "array of strings"), ({"a": 1}, "array of strings"), (["-X3"], "exit-code option -X"),
                        (["-eggplant"], "already passed"), (["-p x"], "whitespace"), ([""], "is empty")):
        bad_registry = draft_registry(root/"cheat-bad.json", [dict(cheat, game_arguments=field)])
        r = ps("run-oracle-regression.ps1", *common, "-DryRun", "-OutputDirectory", out, "-SuitesFile", bad_registry, ok=False)
        check("cheat-a" in r.stderr and word in r.stderr, (field, r.stderr))
    # Absent, null and empty are the same as no field (old registries stay valid, format 2 and 3).
    # The reference is the registered suites without their own arguments.
    bare = [{k: v for k, v in s.items() if k not in ("game_arguments", "demo_tape")} for s in suites.values()]
    default = json.loads(ps("run-oracle-regression.ps1", *common, "-DryRun", "-OutputDirectory", out,
                            "-SuitesFile", draft_registry(root/"cheat-bare.json", bare)).stdout)
    for field in ({}, {"game_arguments": None}, {"game_arguments": []}):
        for version in (2, 3):
            same = draft_registry(root/"cheat-empty.json", [dict(s, **field) for s in bare], version)
            got = json.loads(ps("run-oracle-regression.ps1", *common, "-DryRun", "-OutputDirectory", out, "-SuitesFile", same).stdout)
            check(got == default, (field, version))
    check(not out.exists())


def lane_record_verify(root):
    """Recording, verification, comparator negatives, outcome runs, draft run, draft coverage."""
    fx = fixture(root)
    data, launch, install, clone, before, watches, journal, common = (fx.data, fx.launch, fx.install, fx.clone, fx.before, fx.watches, fx.journal, fx.common)
    out = root/"never-written"
    # Record and verify the actual scripts with differing input counters.
    mock=root/"mock"
    for run,mode in (("record","record"),("replay1","replay"),("replay2","replay")):
        lines(mock/run/"tick.jsonl",events(recorded=mode=="record"))
        lines(mock/run/"input.jsonl",[dict(f=0,api="keydown",vk=32)])
        write(mock/run/"report.json",dict(ticks_seen=3,stalled=False,process_alive_at_end=True,stopped_after_identity_check=True,
            virtual_time_step_ms=47,virtual_time_pace=1 if mode=="record" else 0,input_mode=mode,pid=123))
    recorded=root/"recording"
    ps("record-suite.ps1",*common,"-Name","suite-b","-Launch","launch-b","-WatchFile",watches,"-Ticks",3,"-FrameLimit",3,
       "-OutputDirectory",recorded,"-MockDirectory",mock)
    verified=root/"verified"
    ps("record-suite.ps1",*common,"-VerifyRecording","-RecordingDirectory",recorded,"-OutputDirectory",verified,"-MockDirectory",mock)
    check(json.loads((verified/"verification.json").read_text())["identical"])
    check(json.loads((verified/"suite-entry.json").read_text())["launch"]=="launch-b")
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    ps("record-suite.ps1",*common,"-VerifyRecording","-RecordingDirectory",recorded,"-OutputDirectory",out,"-DryRun")
    # Negative controls: RNG, pause, input replay counter, missing/duplicate
    # frame, empty log, missing fields and an out-of-range watch delta.
    good=compare.read_run(recorded/"record.jsonl",frame_limit=3)
    replay=compare.read_run(verified/"replay1.jsonl",frame_limit=3)
    check(compare.compare_three(good,replay,replay)["identical"])
    for field in ("rng_a_hex","loop_pause_flag","in_injected","vt_stall_advances"):
        bad=copy.deepcopy(replay); i=bad["tick_fields"].index(field); bad["ticks"][1][i]="changed"
        check(not compare.compare_three(good,replay,bad)["identical"],field)
    for mutation in ("empty","duplicate","gap","missing","diff"):
        es=events()
        if mutation=="empty":es=es[:1]
        if mutation=="duplicate":es.append(es[-1])
        if mutation=="gap":es.pop(2)
        if mutation=="missing":del es[1]["rng_b_hex"]
        if mutation=="diff":
            es[0]["watches"]=1
            es.insert(2,dict(event="watch",frame=0,name="x",length=1,hex="00"))
            es.append(dict(event="watch",frame=2,name="x",length=1,diff=[[1,"00"]]))
        badlog=root/"bad.jsonl"; lines(badlog,es)
        try:compare.read_run(badlog,frame_limit=3)
        except (ValueError,KeyError):check(True)
        else:check(False,mutation)
    for entry in (dict(f=0,api="msg",a=0x113),dict(f=0,api="msg",a=0x20f),dict(f=0,api="joypos",b=0)):
        lines(root/"bad-input.jsonl",[entry])
        try:compare.validate_journal(root/"bad-input.jsonl")
        except ValueError:check(True)
        else:check(False,"non-keyboard, non-mouse input accepted")
    for message in (0x200,0x201,0x202,0x20e):
        lines(root/"mouse-input.jsonl",[dict(f=0,api="msg",a=message)])
        compare.validate_journal(root/"mouse-input.jsonl")
    # Modal pump keys: only messages of slow frames and of the frame after
    # the last tick get increasing p values; the source journal stays.
    import modal_pump_keys as modal
    slow=[dict(event="start",qpc_frequency=1000)]+[dict(event="tick",frame_counter_before_increment=i,qpc=q) for i,q in enumerate((0,47,94,900,947))]
    lines(root/"slow.jsonl",slow)
    check(modal.modal_frames(root/"slow.jsonl",500)=={3,5})
    check(modal.modal_frames(recorded/"record.jsonl",500)=={3})  # no qpc: only the frame after the last tick
    modal_input=[dict(f=1,api="msg",a=0x100),dict(f=3,api="msg",a=0x201),dict(f=3,api="key",a=13,b=0),dict(f=3,api="msg",a=0x100,p=7),
                 dict(f=5,api="msg",a=0x100),dict(f=5,api="keydown",vk=13)]
    lines(root/"modal-input.jsonl",modal_input)
    derived,keyed=modal.derive(root/"modal-input.jsonl",{3,5},300,10)
    check([e.get("p") for e in derived]==[None,300,None,7,300,310] and keyed=={3:1,5:2})
    # Opener keys (Q 0x51): everything up to the first opener key-down stays
    # unkeyed, and a frame with an opener is modal even if it was fast (frame 8).
    opener_input=[dict(f=3,api="msg",a=0x200),dict(f=3,api="msg",a=0x100,b=0x51),dict(f=3,api="msg",a=0x101,b=0x51),
                  dict(f=3,api="msg",a=0x100,b=13),dict(f=8,api="keydown",vk=0x51),dict(f=8,api="keydown",vk=13),
                  dict(f=9,api="msg",a=0x100,b=13)]
    lines(root/"opener-input.jsonl",opener_input)
    check(modal.opener_frames(opener_input,{0x51})=={3,8})
    derived,keyed=modal.derive(root/"opener-input.jsonl",{3},300,10,frozenset({0x51}))
    check([e.get("p") for e in derived]==[None,None,300,310,None,300,None] and keyed=={3:2,8:1})
    derived,keyed=modal.derive(root/"opener-input.jsonl",{3},300,10)
    check([e.get("p") for e in derived]==[300,310,320,330,None,None,None] and keyed=={3:4})
    # Invalid replay fails the full verifier and never emits a draft.
    bad_events=events();bad_events[2]["rng_a_hex"]="ff"*114
    lines(mock/"replay2/tick.jsonl",bad_events)
    failed=root/"failed-verification"
    ps("record-suite.ps1",*common,"-VerifyRecording","-RecordingDirectory",recorded,"-OutputDirectory",failed,"-MockDirectory",mock,ok=False)
    check(not (failed/"suite-entry.json").exists())
    lines(mock/"replay2/tick.jsonl",events())
    golden = root/"goldens"
    # Launch-aware draft, through the same runner, with launch hashes in entry.
    draft=dict(name="new-suite",launch="launch-b",script=str(journal),watches=str(watches),ticks=3,frame_limit=3,outcome=False)
    write(root/"draft.json",dict(format=2,suites=[draft]))
    shutil.copytree(mock/"replay1",mock/"new-suite")
    write(golden/"new-suite/normalized.json",oracle.normalize(mock/"new-suite/tick.jsonl",frame_limit=3))
    write(golden/"new-suite/manifest.json",dict(commit="synthetic-baseline"))
    ps("run-oracle-regression.ps1",*common,"-SuitesFile",root/"draft.json","-MockDirectory",mock,"-GoldenRoot",golden,"-OutputDirectory",root/"draft-run","-Python",sys.executable)
    check(json.loads((root/"draft-run/new-suite-entry.json").read_text())["launch_set"]["name"]=="launch-b")
    # Draft coverage delegates to the unchanged coverage wrapper, validates
    # original branch evidence, and leaves canonical registry/goldens alone.
    site=dict(hook='rng_next',va='0x00401000',condition='je')
    baseline=dict(valid=True,module_sha256=coverage.MODULE_HASH,sites=[dict(site,fall=1,taken=0)])
    current=dict(baseline,sites=[dict(site,fall=1,taken=1)])
    change=coverage_delta.delta(baseline,current)
    check(change['closed_site_gaps']==[dict(hook='rng_next',va='0x00401000')])
    check(change['newly_observed_outcomes'][0]['outcomes']==['taken'])
    check(not coverage_delta.delta(current,current)['closed_site_gaps'])
    extra=dict(current,sites=current['sites']+[dict(site,va='0x00401010',fall=1,taken=0)])
    check(len(coverage_delta.delta(baseline,extra)['newly_instrumented_sites'])==1)
    for bad in (dict(current,valid=False),dict(current,module_sha256='other'),
                dict(current,sites=[dict(site,condition='jne',fall=1,taken=1)])):
        try:coverage_delta.delta(baseline,bad)
        except ValueError:check(True)
        else:check(False,'invalid coverage accepted')
    covmock=root/'coverage-mock'
    write(covmock/'baseline.json',baseline)
    norm=oracle.normalize(verified/'replay1.jsonl',frame_limit=3)
    write(covmock/'normalized.json',norm)
    write(covmock/'coverage.json',dict(schema=1,module_sha256=coverage.MODULE_HASH,mode='saturate',pid=123,
          install_thread=9,first_thread=9,other_thread=0,tick_thread=9,error=0,complete=True,
          reason='frame_limit',last_frame=2,frame_limit=3,has_tick=True,
          hooks=[dict(name='rng_next',mode=0)],sites=[dict(site,fall=1,taken=1,restored=True)]))
    write(covmock/'coverage-manifest.json',dict(suites=[dict(suite='suite-b',golden=str(covmock/'normalized.json'),
          normalized=str(covmock/'normalized.json'),coverage=str(covmock/'coverage.json'),pid=123,proxy_sha256='synthetic-proxy')],
          expected_hooks=dict(rng_next=0),expected_sites=[site]))
    coverage_args=(*common,'-VerifiedDirectory',verified,'-BaselineSummary',covmock/'baseline.json','-Python',sys.executable)
    ps('run-draft-coverage.ps1',*coverage_args,'-OutputDirectory',root/'coverage-dry','-DryRun')
    check(not (root/'coverage-dry').exists())
    registry_bytes=(SETUP.parent/'oracle-suites.json').read_bytes()
    covout=root/'draft-coverage'
    ps('run-draft-coverage.ps1',*coverage_args,'-OutputDirectory',covout,'-MockDirectory',covmock)
    check(len(json.loads((covout/'coverage-gaps.json').read_text())['closed_site_gaps'])==1)
    check((SETUP.parent/'oracle-suites.json').read_bytes()==registry_bytes)
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    bridge=covout/'driver/tools/reference-game/setup/run-oracle-regression.ps1'
    ps(bridge,*common,'-Mode','check','-Suite','suite-b','-OutputDirectory',root/'bridge-dry','-DryRun')
    ps(bridge,*common,'-Mode','record','-Suite','suite-b','-OutputDirectory',root/'bridge-record','-DryRun',ok=False)


def lane_outcome(root):
    """Outcome recordings: stale results cleared, natural exit mandatory for all three runs."""
    fx = fixture(root)
    data, launch, install, clone, before, watches, journal, common = (fx.data, fx.launch, fx.install, fx.clone, fx.before, fx.watches, fx.journal, fx.common)
    mock=root/"mock"
    for run,mode in (("record","record"),("replay1","replay"),("replay2","replay")):
        lines(mock/run/"tick.jsonl",events(recorded=mode=="record"))
        lines(mock/run/"input.jsonl",[dict(f=0,api="keydown",vk=32)])
    # Outcome: stale results are cleared, unchanged mission.var is still
    # retained, and a natural exit is mandatory for all three runs.
    for run,mode in (("record","record"),("replay1","replay"),("replay2","replay")):
        (mock/run/'data-after').mkdir()
        (mock/run/'data-after/results.dat').write_bytes(b'synthetic-return')
        write(mock/run/'report.json',dict(ticks_seen=3,stalled=False,process_alive_at_end=False,stopped_after_identity_check=False,
            virtual_time_step_ms=47,virtual_time_pace=1 if mode=='record' else 0,input_mode=mode,pid=123))
    outcome=root/'outcome'
    ps('record-suite.ps1',*common,'-Name','outcome-b','-Launch','launch-b','-WatchFile',watches,'-Ticks',3,'-Outcome',
       '-OutputDirectory',outcome,'-MockDirectory',mock)
    ps('record-suite.ps1',*common,'-VerifyRecording','-RecordingDirectory',outcome,'-OutputDirectory',root/'outcome-verified','-MockDirectory',mock)
    check((outcome/'record-output/mission.var').read_bytes()==bytes(2000))
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    report_path=mock/'record/report.json'; ended=json.loads(report_path.read_text())
    write(report_path,dict(ended,process_alive_at_end=True,stopped_after_identity_check=True))
    ps('record-suite.ps1',*common,'-Name','timed-out','-Launch','launch-b','-WatchFile',watches,'-Ticks',3,'-Outcome',
       '-OutputDirectory',root/'timed-out','-MockDirectory',mock,ok=False)
    check(not (root/'timed-out/session.json').exists())
    for run in ('record','replay1','replay2'):shutil.rmtree(mock/run/'data-after')


def legacy_inputs(root, fx, only=None):
    """Mock tick logs and normalized goldens of the registered suites (all of them, or just `only`: a
    suite name or a collection of names): returns (mock directory, golden root, registry)."""
    if isinstance(only, str): only = {only}
    data = fx.data
    mock = root/"mock"
    registered=json.loads((SETUP.parent/"oracle-suites.json").read_text())["suites"]
    for name in sorted({s["launch"] for s in registered if s.get("launch") and (not only or s["name"] in only)}): make_launch(data, name)
    # Legacy mock runner: exact existing normalized golden expectations,
    # unchanged suite entries, all three golden comparisons are identical.
    registry=json.loads((SETUP.parent/"oracle-suites.json").read_text())
    golden=root/"goldens"
    for suite in registry["suites"]:
        if only and suite["name"] not in only: continue
        run=suite["name"]; source=mock/run
        lines(source/"tick.jsonl",events(n=suite.get("frame_limit",3)))
        write(source/"report.json",dict(pid=123))
        outputs=None
        if suite.get("launch") and suite.get("outcome"):
            # Launch-aware outcome runs keep the returned files; the
            # golden expects the synthetic launch's mission.var as well.
            (source/'data-after').mkdir(exist_ok=True)
            (source/'data-after/results.dat').write_bytes(b'synthetic-return')
            outputs=root/"golden-outputs"/run;outputs.mkdir(parents=True)
            (outputs/'results.dat').write_bytes(b'synthetic-return');(outputs/'mission.var').write_bytes(bytes(2000))
        normalized=oracle.normalize(source/"tick.jsonl",outputs,frame_limit=suite.get("frame_limit"))
        manifest=dict(commit="synthetic-baseline")
        # A suite with registry DBSIM arguments needs them in its golden manifest.
        if suite.get("game_arguments"): manifest["game_arguments"]=suite["game_arguments"]
        write(golden/run/"normalized.json",normalized);write(golden/run/"manifest.json",manifest)
    return mock, golden, registry


# The all-suite mock run took about 1.7 s per registered suite in one PowerShell step (70 s for 36 suites
# alone, over the 120 s step limit under load, and growing with every registration). The registered suites are
# played in groups of at most SUITES_PER_GROUP, each group in a lane of its own (so the groups run side by side
# in their Bazel shard and every step is bounded by the group size, not by the registry size); the argument and
# hidden-window checks that need a baseline run of two suites follow in lane_legacy_runner.
SUITES_PER_GROUP = 10
LEGACY_BASELINE = ("acceptance-60s", "c3_09-loadout-b")


def registered_suite_names():
    return [s["name"] for s in json.loads((SETUP.parent/"oracle-suites.json").read_text())["suites"]]


def lane_legacy_group(root, names):
    """Legacy mock runner over the registered suites `names` against normalized goldens: one run, all identical."""
    fx = fixture(root)
    mock, golden, registry = legacy_inputs(root, fx, only=set(names))
    out = root/"legacy-group"
    ps(f"& {quote(SETUP/'run-oracle-regression.ps1')} -DataRoot {quote(fx.data)} -InstallName test-clone -MockDirectory {quote(mock)} "
       f"-GoldenRoot {quote(golden)} -OutputDirectory {quote(out)} -Python {quote(sys.executable)} -Suite {','.join(names)}", command=True)
    results = json.loads((out/"summary.json").read_text())["results"]
    check([r["suite"] for r in results]==list(names) and all(r["result"]=="identical" for r in results), results)
    check({p.name:p.read_bytes() for p in fx.clone.iterdir()}==fx.before)


def legacy_group_lanes():
    """One lane function per group of registered suites (named lane_legacy_group_<i>)."""
    names = registered_suite_names()
    lanes = []
    for i in range(0, len(names), SUITES_PER_GROUP):
        group = tuple(names[i:i+SUITES_PER_GROUP])
        def lane(root, group=group): lane_legacy_group(root, group)
        lane.__name__ = f"lane_legacy_group_{i//SUITES_PER_GROUP}"
        lanes.append(lane)
    return lanes


def lane_legacy_runner(root):
    """Legacy mock runner over the two baseline suites, then the argument pass-through and hidden-window checks against that baseline."""
    fx = fixture(root)
    data, launch, install, clone, before, watches, journal, common = (fx.data, fx.launch, fx.install, fx.clone, fx.before, fx.watches, fx.journal, fx.common)
    mock, golden, registry = legacy_inputs(root, fx, only=set(LEGACY_BASELINE))
    legacy=root/"legacy"
    ps(f"& {quote(SETUP/'run-oracle-regression.ps1')} -DataRoot {quote(data)} -InstallName test-clone -MockDirectory {quote(mock)} "
       f"-GoldenRoot {quote(golden)} -OutputDirectory {quote(legacy)} -Python {quote(sys.executable)} -Suite {','.join(LEGACY_BASELINE)}", command=True)
    check([r["suite"] for r in json.loads((legacy/"summary.json").read_text())["results"]]==list(LEGACY_BASELINE))
    check(all(r["result"]=="identical" for r in json.loads((legacy/"summary.json").read_text())["results"]))
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    # DBSIM argument pass-through. Without the option no entry, launch
    # report or summary carries the key; with it the list lands in all three and
    # nothing else changes (same tick log, same normalized form, same verdict).
    def load(path): return json.loads(path.read_text(encoding="utf-8-sig"))
    check("game_arguments" not in load(legacy/"summary.json"))
    extra = ["-ptapes\\demo1", "-rrun", "-dsave"]
    arrays = ",".join(quote(a) for a in extra)
    runner = quote(SETUP/"run-oracle-regression.ps1")
    for name, form in (("acceptance-60s","array"),("c3_09-loadout-b","json")):
        default = load(legacy/f"{name}-entry.json")
        check("game_arguments" not in default and "game_arguments" not in default["launch"])
        out = root/f"given-{name}"
        if form == "array":
            ps(f"& {runner} -DataRoot {quote(data)} -InstallName test-clone -MockDirectory {quote(mock)} -GoldenRoot {quote(golden)} "
               f"-OutputDirectory {quote(out)} -Python {quote(sys.executable)} -Suite {name} -GameArguments {arrays}", command=True)
        else:
            ps("run-oracle-regression.ps1",*common,"-MockDirectory",mock,"-GoldenRoot",golden,"-OutputDirectory",out,
               "-Python",sys.executable,"-Suite",name,"-GameArgumentsJson",json.dumps(extra))
        entry = load(out/f"{name}-entry.json")
        check(entry["game_arguments"]==extra and entry["launch"]["game_arguments"]==extra and entry["result"]=="identical", entry)
        summary = load(out/"summary.json")
        check(summary["game_arguments"]==extra and summary["results"]==[dict(suite=name,result="identical")] and summary["mode"]=="check")
        # Apart from the clock and the added keys the evidence is the default run's.
        for e in (default, entry): e.pop("utc")
        entry.pop("game_arguments"); entry["launch"].pop("game_arguments")
        check(entry==default, name)
        for suffix in ("-normalized.json", ".jsonl"):
            check((out/f"{name}{suffix}").read_bytes()==(legacy/f"{name}{suffix}").read_bytes(), (name, suffix))
    check({p.name:p.read_bytes() for p in clone.iterdir()}==before)
    hidden_window_runs(root, fx, mock, golden, legacy)


def lane_suite_arguments(root):
    """Registry game_arguments through record and check with the mock runner (acceptance-60s as the plain suite)."""
    fx = fixture(root)
    mock, golden, registry = legacy_inputs(root, fx, only="acceptance-60s")
    mock_suite_game_arguments(root, fx, mock, golden, {s["name"]: s for s in registry["suites"]})


# A runner hands the process environment back as it found it. Everything runs in ONE PowerShell step
# (the session server restores the environment between steps, which would hide a leak).
ENV_FUNCTIONS = r'''
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
# Every variable a runner of this family sets for its children or reads as a switch (the caller's choices).
RUNNER_ENV = ('ES2_CLONE_LOCK_TOKEN', 'ES2_LIVE_COVERAGE_CONFIG', 'ES2_ORACLE_SPEED_CONFIG', 'ES2_PARALLEL_CLONES',
              'ES2_PREPARED_PROXY', 'ES2_TICK_BUFFERED', 'ES2_ORACLE_HIDDEN_WINDOW', 'ES2_AFTER_LOAD_SNAPSHOT',
              'ES2_RTL_CONFIG', 'ES2_EARLY_ARMING')


def lane_environment(root):
    """run-oracle-regression.ps1 (with run-prepared-suite.ps1 behind it) in mock mode: the complete process
    environment after each run equals the one before, whether the variables were absent or set by the caller, after
    a success, a golden mismatch (exit 1) and a failure inside the run."""
    fx = fixture(root)
    mock, golden, registry = legacy_inputs(root, fx, only="acceptance-60s")
    bad = root/"golden-bad"
    shutil.copytree(golden, bad)
    normalized = json.loads((bad/"acceptance-60s/normalized.json").read_text())
    normalized["ticks"][0][1] += 1
    (bad/"acceptance-60s/normalized.json").write_text(json.dumps(normalized))
    empty = root/"mock-empty"; empty.mkdir()
    runner = quote(SETUP/"run-oracle-regression.ps1")
    base = (f"-DataRoot {quote(fx.data)} -InstallName test-clone -Python {quote(sys.executable)} -Suite acceptance-60s")
    runs = [  # (name, mock, golden, extra arguments, summary result or None when the run fails before a summary)
        ("plain", mock, golden, "", "identical"), ("hidden", mock, golden, "-HiddenWindow", "identical"),
        ("visible", mock, golden, "-HiddenWindow:$false", "identical"), ("hooks", mock, golden, "-Hooks alpha=replaced", "identical"),
        ("again", mock, golden, "", "identical"),   # a second run in the same process
        ("mismatch", mock, bad, "", "DIFFERENT"), ("thrown", empty, golden, "", None)]
    for case, preset in (("absent", False), ("preset", True)):
        code = ENV_FUNCTIONS
        for name in RUNNER_ENV:
            code += (f"[Environment]::SetEnvironmentVariable('{name}', 'caller-{name}')\n" if preset
                     else f"[Environment]::SetEnvironmentVariable('{name}', [NullString]::Value)\n")
        for name, run_mock, run_golden, extra, _ in runs:
            out = root/f"env-{case}-{name}"
            code += (f"$before = Get-Es2Snapshot\n"
                     f"try {{ & {runner} {base} -MockDirectory {quote(run_mock)} -GoldenRoot {quote(run_golden)} -OutputDirectory {quote(out)} {extra} | Out-Null }} catch {{ }}\n"
                     f"$global:LASTEXITCODE = 0\n"
                     f"Assert-Es2Same $before (Get-Es2Snapshot) '{case} / {name}'\n")
        ps(code, command=True)
        for name, _, _, _, result in runs:
            summary = root/f"env-{case}-{name}/summary.json"
            if result is None:
                check(not summary.exists(), (case, name))
            else:
                check(json.loads(summary.read_text(encoding="utf-8-sig"))["results"][0]["result"] == result, (case, name))


def hidden_window_runs(root, fx, mock, golden, legacy):
    """-HiddenWindow / ES2_ORACLE_HIDDEN_WINDOW in run-oracle-regression.ps1 (mock runner).
    Default in check mode: hidden; record mode
    stays visible. The decision is handed to the mission runners through the variable and restored
    afterwards."""
    data, common = fx.data, fx.common
    def load(path): return json.loads(path.read_text(encoding="utf-8-sig"))
    runner = quote(SETUP/"run-oracle-regression.ps1")
    check(load(legacy/"summary.json").get("hidden_window") is True and load(legacy/"acceptance-60s-entry.json").get("hidden_window") is True)
    base = (f"-DataRoot {quote(data)} -InstallName test-clone -MockDirectory {quote(mock)} -GoldenRoot {quote(golden)} "
            f"-Python {quote(sys.executable)} -Suite acceptance-60s")
    # The variable as the runner leaves it for its children, read by a stand-in mission runner step.
    probe = root/"hidden-probe.txt"
    def run(out, prefix="", suffix="", ok=True):
        code = (f"$prior=$env:ES2_ORACLE_HIDDEN_WINDOW; try {{ {prefix} & {runner} {base} -OutputDirectory {quote(out)} {suffix}; "
                f"Set-Content -LiteralPath {quote(probe)} -Value ('after='+[string]$env:ES2_ORACLE_HIDDEN_WINDOW) }} "
                f"finally {{ $env:ES2_ORACLE_HIDDEN_WINDOW=$prior }}")
        return ps(code, command=True, ok=ok)
    cases = (("switch", "", "-HiddenWindow", True), ("env", "$env:ES2_ORACLE_HIDDEN_WINDOW='1';", "", True),
             ("off", "$env:ES2_ORACLE_HIDDEN_WINDOW='1';", "-HiddenWindow:$false", False),
             ("env-off", "$env:ES2_ORACLE_HIDDEN_WINDOW='0';", "", False), ("default", "", "", True))
    for name, prefix, suffix, hidden in cases:
        out = root/f"hidden-{name}"
        run(out, prefix, suffix)
        summary, entry = load(out/"summary.json"), load(out/"acceptance-60s-entry.json")
        check(summary["results"]==[dict(suite="acceptance-60s",result="identical")], (name, summary))
        check(summary.get("hidden_window", False)==hidden and entry.get("hidden_window", False)==hidden, (name, summary, entry))
        # The runner restores the variable it set for its children.
        expected = {"env": "1", "off": "1", "env-off": "0"}.get(name, "")
        check(probe.read_text(encoding="utf-8-sig").strip()=="after="+expected, (name, probe.read_text(encoding="utf-8-sig")))
        check((out/"acceptance-60s-normalized.json").read_bytes()==(legacy/"acceptance-60s-normalized.json").read_bytes(), name)
    # Dry plans: the key only when hidden; check mode is hidden by default, record mode stays visible.
    dry = root/"hidden-dry"
    plan = json.loads(ps(f"& {runner} {base} -OutputDirectory {quote(dry)} -DryRun -HiddenWindow", command=True).stdout)
    check(plan["hidden_window"] is True and not dry.exists(), plan)
    for mode, hidden in (("check", True), ("record", False)):
        plan = json.loads(ps(f"& {runner} {base} -OutputDirectory {quote(dry)} -DryRun -Mode {mode}", command=True).stdout)
        check(plan.get("hidden_window", False) is hidden, (mode, plan))
    plan = json.loads(ps(f"& {runner} {base} -OutputDirectory {quote(dry)} -DryRun -HiddenWindow:$false", command=True).stdout)
    check("hidden_window" not in plan, plan)


def mock_suite_game_arguments(root, fx, mock, legacy_golden, suites_by_name):
    """Registry game_arguments through record and check with the mock runner."""
    data, clone, before, common = fx.data, fx.clone, fx.before, fx.common
    def load(path): return json.loads(path.read_text(encoding="utf-8-sig"))
    plain = suites_by_name["acceptance-60s"]
    cheat = dict(plain, name="cheat-a", game_arguments=["-SPRUNKNOWN"])
    shutil.copytree(mock/"acceptance-60s", mock/"cheat-a")
    registry = draft_registry(root/"cheat-run.json", [cheat, plain])
    golden = root/"cheat-golden"
    common_run = [*common, "-MockDirectory", mock, "-GoldenRoot", golden, "-Python", sys.executable]
    # Record: the golden manifest names the arguments (and only for the suite that has them).
    rec = root/"cheat-record"
    ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", registry, "-Mode", "record", "-OutputDirectory", rec)
    manifest = load(golden/"cheat-a/manifest.json")
    check(manifest["game_arguments"] == ["-SPRUNKNOWN"] and manifest["suite_game_arguments"] == ["-SPRUNKNOWN"] and manifest["launch"]["game_arguments"] == ["-SPRUNKNOWN"], manifest)
    plain_manifest = load(golden/"acceptance-60s/manifest.json")
    check("game_arguments" not in plain_manifest and "suite_game_arguments" not in plain_manifest)
    summary = load(rec/"summary.json")
    check([(r["suite"], r["result"]) for r in summary["results"]] == [("cheat-a", "recorded"), ("acceptance-60s", "recorded")], summary)
    check("game_arguments" not in summary and summary["results"][0]["game_arguments"] == ["-SPRUNKNOWN"] and "game_arguments" not in summary["results"][1], summary)
    # The command line stays refused in record mode.
    extra = ["-ptapes\\demo1", "-rrun"]
    r = ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", registry, "-Mode", "record", "-Force", "-OutputDirectory", root/"cheat-record2",
           "-GameArgumentsJson", json.dumps(extra), ok=False)
    check("recorded with the fixed DBSIM arguments" in r.stderr and not (root/"cheat-record2").exists(), r.stderr)
    # Check with the same registry: identical; entry and summary carry the list.
    out = root/"cheat-check"
    ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", registry, "-OutputDirectory", out)
    summary = load(out/"summary.json")
    check([(r["suite"], r["result"]) for r in summary["results"]] == [("cheat-a", "identical"), ("acceptance-60s", "identical")] and "game_arguments" not in summary, summary)
    check(summary["results"][0]["game_arguments"] == ["-SPRUNKNOWN"] and "game_arguments" not in summary["results"][1])
    entry = load(out/"cheat-a-entry.json")
    check(entry["game_arguments"] == ["-SPRUNKNOWN"] and entry["suite_game_arguments"] == ["-SPRUNKNOWN"] and entry["launch"]["game_arguments"] == ["-SPRUNKNOWN"], entry)
    plain_entry = load(out/"acceptance-60s-entry.json")
    check("game_arguments" not in plain_entry and "suite_game_arguments" not in plain_entry and "game_arguments" not in plain_entry["launch"])
    # Check with the command line appended after the registry arguments.
    out = root/"cheat-check-extra"
    ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", registry, "-OutputDirectory", out, "-GameArgumentsJson", json.dumps(extra))
    summary = load(out/"summary.json")
    check(summary["game_arguments"] == extra and summary["results"][0]["game_arguments"] == ["-SPRUNKNOWN"] + extra and "game_arguments" not in summary["results"][1], summary)
    entry = load(out/"cheat-a-entry.json")
    check(entry["game_arguments"] == ["-SPRUNKNOWN"] + extra and entry["suite_game_arguments"] == ["-SPRUNKNOWN"] and entry["launch"]["game_arguments"] == ["-SPRUNKNOWN"] + extra, entry)
    check(load(out/"acceptance-60s-entry.json")["game_arguments"] == extra)
    # A colliding command-line argument is refused before anything is written.
    out = root/"cheat-check-collide"
    r = ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", registry, "-OutputDirectory", out, "-GameArgumentsJson", '["-SPRUNKNOWN"]', ok=False)
    check("repeats" in r.stderr and "cheat-a" in r.stderr and not out.exists(), r.stderr)
    # Mismatch with the golden manifest: refused, nothing written, nothing launched.
    wrong = {"changed": [dict(cheat, game_arguments=["-SPRUNKNOWN", "-Zfoo"]), plain],
             "removed": [dict(cheat, game_arguments=[]), plain],
             "other": [dict(cheat, game_arguments=["-SPRUNKNEW"]), plain],
             "added": [cheat, dict(plain, game_arguments=["-SPRUNKNOWN"])]}
    for label, entries in wrong.items():
        out = root/f"cheat-mismatch-{label}"
        r = ps("run-oracle-regression.ps1", *common_run, "-SuitesFile", draft_registry(root/f"cheat-{label}.json", entries), "-OutputDirectory", out, ok=False)
        check("golden manifest" in r.stderr and "game_arguments" in r.stderr and not out.exists(), (label, r.stderr))
    check("acceptance-60s" in r.stderr and "[none]" in r.stderr and "[-SPRUNKNOWN]" in r.stderr, r.stderr)
    # The legacy goldens (manifest without the key) still match a registry without arguments.
    out = root/"cheat-legacy"
    ps("run-oracle-regression.ps1", *common, "-MockDirectory", mock, "-GoldenRoot", legacy_golden, "-Python", sys.executable, "-Suite", "acceptance-60s", "-OutputDirectory", out)
    check(load(out/"summary.json")["results"] == [dict(suite="acceptance-60s", result="identical")])
    check({p.name: p.read_bytes() for p in clone.iterdir()} == before)


def lane_tapes(root):
    """Tape plans, the tape transaction (play, demo, record) and the routing of tape suites."""
    fx = fixture(root)
    data, install, clone, watches, journal, common = (fx.data, fx.install, fx.clone, fx.watches, fx.journal, fx.common)
    module = SETUP/"SuiteLaunch.psm1"
    def listed(arguments): return "@(" + ",".join(quote(a) for a in arguments) + ")"
    def plan(arguments, demo=""):
        r = ps(f"Import-Module {quote(module)}; Get-SuiteTapePlan {listed(arguments)} {quote(demo)} | ConvertTo-Json -Compress", command=True)
        return json.loads(r.stdout) if r.stdout.strip() else None
    check(plan([]) is None and plan(["-SPRUNKNOWN"]) is None)
    check(plan(["-rc1tape"]) == dict(mode="record", argument="-rc1tape", tape="c1tape.tap", demo_tape=None))
    check(plan(["-SPRUNKNOWN", "-ptapes\\demo2"]) == dict(mode="play", argument="-ptapes\\demo2", tape="tapes/demo2.tap", demo_tape=None))
    check(plan(["-pc1tape"])["tape"] == "c1tape.tap")
    check(plan(["-D"], "DEMO1") == dict(mode="demo", argument="-D", tape="TAPES/DEMO1.tap", demo_tape="DEMO1"))
    for arguments, demo, word in ((["-rtapes\\x"], "", "plain tape name"), (["-p..\\x"], "", "tapes\\<name>"), (["-pC:\\x"], "", "tapes\\<name>"),
                                  (["-r"], "", "plain tape name"), (["-D"], "", "time() % 3"), ([], "DEMO1", "needs the DBSIM argument -D"),
                                  (["-Dx"], "DEMO1", "takes no name"), (["-dsave"], "", "dump file"), (["-rA", "-pB"], "", "more than one"),
                                  (["-rA"], "DEMO1", "belongs to -D"), (["-D"], "DEMO_TOO_LONG", "eight characters"), (["-D"], "DE MO", "eight characters")):
        r = ps(f"Import-Module {quote(module)}; Get-SuiteTapePlan {listed(arguments)} {quote(demo)}", command=True, ok=False)
        check(word in r.stderr, (arguments, demo, r.stderr))
    # The synthetic one-entry DEMOLIST.STR (docs/formats/str.md layout).
    r = ps(f"Import-Module {quote(module)}; [BitConverter]::ToString((New-SuiteDemoList DEMO1))", command=True)
    check(r.stdout.strip() == "0B-00-00-00-01-00-06-00-44-45-4D-4F-31-00-00", r.stdout)
    tapes = install/"TAPES"
    tapes_before = {p.name: p.read_bytes() for p in tapes.iterdir()}
    (clone/"restore.dat").write_bytes(b"old-restore")
    data_before = {p.name: p.read_bytes() for p in clone.iterdir()}
    def unchanged(label):
        check({p.name: p.read_bytes() for p in clone.iterdir()} == data_before, label)
        check({p.name: p.read_bytes() for p in tapes.iterdir()} == tapes_before, label)
    def transaction(name, arguments, demo, action, ok=True):
        out = root/name
        code = (f"$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force\n"
                f"$plan=Get-SuiteTapePlan {listed(arguments)} {quote(demo)}\n"
                f"$t=Invoke-SuiteTransaction -DataRoot {quote(data)} -InstallName test-clone -OutputDirectory {quote(out)} -RunName test -Mock -TapePlan $plan -Action {{\n"
                f"{action}\n'{{}}' }}\n"
                "@{changed=@($t.changed_and_restored);tape=$t.tape;plan=$t.tape_plan} | ConvertTo-Json -Compress -Depth 4\n")
        r = ps(code, command=True, ok=ok)
        unchanged(name)
        if ok: check(not list(install.glob("*.tap")) and not list(out.glob(".backup-*")), name)
        return (json.loads(r.stdout) if ok else r), out
    # Playback overwrites the launch files with the tape's copies and writes the
    # tape files: everything is restored, nothing of it becomes an output.
    play = (f"foreach($n in 'script.dat','player.mec','mission.var','restore.dat'){{[IO.File]::WriteAllText((Join-Path {quote(clone)} $n),'tape-copy')}}\n"
            f"[IO.File]::WriteAllText((Join-Path {quote(clone)} 'object.str'),'')\n"
            f"foreach($n in 'prefs.cfg','keyjoy.cfg'){{[IO.File]::WriteAllText((Join-Path {quote(tapes)} $n),'tape-copy')}}")
    result, out = transaction("tape-play", ["-ptapes\\demo2"], "", play)
    check(not (out/"test-output").exists(), result)
    check(set(result["changed"]) == {"script.dat", "player.mec", "mission.var", "DATA/restore.dat", "DATA/object.str", "TAPES/prefs.cfg", "TAPES/keyjoy.cfg"}, result)
    # A playback whose mission ended: results.dat and the rewritten mission.var are outputs.
    ended = play + f"\n[IO.File]::WriteAllText((Join-Path {quote(clone)} 'results.dat'),'new-result')"
    result, out = transaction("tape-ended", ["-ptapes\\demo2"], "", ended)
    check(sorted(p.name for p in (out/"test-output").iterdir()) == ["mission.var", "results.dat"], result)
    # -D: the synthetic list is in place during the run only; a read-only list (as copied from a CD) comes back read-only.
    os.chmod(tapes/"DEMOLIST.STR", stat.S_IREAD)
    demo = (f"if([BitConverter]::ToString([IO.File]::ReadAllBytes((Join-Path {quote(tapes)} 'DEMOLIST.STR'))) -ne '0B-00-00-00-01-00-06-00-44-45-4D-4F-31-00-00'){{throw 'no demo list'}}\n"
            + play)
    result, out = transaction("tape-demo", ["-D"], "DEMO1", demo)
    check("TAPES/DEMOLIST.STR" in result["changed"] and result["plan"]["mode"] == "demo", result)
    # A failing run restores the list and the tape files as well.
    transaction("tape-demo-fail", ["-D"], "DEMO1", demo + "\nthrow 'synthetic run failure'", ok=False)
    check(not os.access(tapes/"DEMOLIST.STR", os.W_OK))
    os.chmod(tapes/"DEMOLIST.STR", stat.S_IREAD | stat.S_IWRITE)
    # -r: the tape is reported by size and hash and removed; the launch files stay untouched.
    record = f"[IO.File]::WriteAllText((Join-Path {quote(install)} 'c1tape.tap'),'recorded-tape')"
    result, out = transaction("tape-record", ["-rc1tape"], "", record)
    check(result["tape"] == dict(size=13, sha256=hashlib.sha256(b"recorded-tape").hexdigest()) and result["changed"] == ["c1tape.tap"], result)
    check(not (out/"test-output").exists())
    # A tape left by an earlier run comes back, it is not deleted.
    (install/"c1tape.tap").write_bytes(b"older-tape")
    code = (f"$ErrorActionPreference='Stop'; Import-Module {quote(module)} -Force\n"
            f"$t=Invoke-SuiteTransaction -DataRoot {quote(data)} -InstallName test-clone -OutputDirectory {quote(root/'tape-record-again')} -RunName test -Mock -TapePlan (Get-SuiteTapePlan @('-rc1tape') '') -Action {{ {record}; '{{}}' }}\n"
            "$t.tape | ConvertTo-Json -Compress\n")
    check(json.loads(ps(code, command=True).stdout)["size"] == 13 and (install/"c1tape.tap").read_bytes() == b"older-tape")
    (install/"c1tape.tap").unlink()
    # A missing playback tape is refused before anything is backed up or deployed.
    for label, arguments, demo in (("demo9", ["-ptapes\\demo9"], ""), ("list9", ["-D"], "DEMO9"), ("root", ["-pc1tape"], "")):
        r, out = transaction("tape-missing-" + label, arguments, demo, "throw 'must not run'", ok=False)
        check("is missing in the clone" in r.stderr and not out.exists(), r.stderr)
    # run-prepared-suite.ps1: only -TapeTransaction or -DemoTape makes a plan (plain pass-through otherwise).
    prepared = quote(SETUP/"run-prepared-suite.ps1")
    args = (f"-DataRoot {quote(data)} -InstallName test-clone -OutputDirectory {quote(root/'never')} -RunName dry "
            f"-InputJournal {quote(journal)} -WatchFile {quote(watches)} -DryRun")
    dry = json.loads(ps(f"& {prepared} {args} -GameArguments '-ptapes\\demo1','-rrun'", command=True).stdout)
    check("tape_plan" not in dry, dry)
    dry = json.loads(ps(f"& {prepared} {args} -GameArguments '-ptapes\\demo2' -TapeTransaction", command=True).stdout)
    check(dry["tape_plan"]["mode"] == "play" and dry["tape_restore_files"] == ["DATA/restore.dat", "DATA/object.str", "TAPES/prefs.cfg", "TAPES/keyjoy.cfg"], dry)
    dry = json.loads(ps(f"& {prepared} {args} -GameArguments '-D' -DemoTape DEMO1", command=True).stdout)
    check(dry["tape_plan"]["demo_tape"] == "DEMO1" and dry["tape_restore_files"][-1] == "TAPES/DEMOLIST.STR", dry)
    # -HoldAtTicks reaches the tick-log runner's arguments; without it the key is absent.
    check("HoldAtTicks" not in dry["arguments"], dry)
    dry = json.loads(ps(f"& {prepared} {args} -GameArguments '-ptapes\\demo2' -TapeTransaction -HoldAtTicks", command=True).stdout)
    check(dry["arguments"]["HoldAtTicks"] is True and dry["tape_plan"]["mode"] == "play", dry)
    r = ps(f"& {prepared} {args} -GameArguments '-ptapes\\demo1','-rrun' -TapeTransaction", command=True, ok=False)
    check("more than one tape option" in r.stderr, r.stderr)
    outcome = quote(SETUP/"run-mission-outcome.ps1")
    oargs = (f"-DataRoot {quote(data)} -InstallName test-clone -OutputDirectory {quote(root/'never')} -RunName dry "
             f"-Script {quote(journal)} -WatchFile {quote(watches)} -DryRun")
    dry = json.loads(ps(f"& {outcome} {oargs} -GameArguments '-D' -DemoTape DEMO1", command=True).stdout)
    check(dry["tape_plan"]["mode"] == "demo" and "HoldAtTicks" not in dry["arguments"], dry)
    dry = json.loads(ps(f"& {outcome} {oargs} -GameArguments '-D' -DemoTape DEMO1 -HoldAtTicks", command=True).stdout)
    check(dry["arguments"]["HoldAtTicks"] is True, dry)
    check(not (root/"never").exists())
    # The regression runner: registry tape options and demo_tape make a tape plan; refused values name the suite.
    plain = json.loads((SETUP.parent/"oracle-suites.json").read_text())["suites"][0]
    plain = {k: v for k, v in plain.items() if k not in ("game_arguments", "launch", "demo_tape")}
    tape_suites = [dict(plain, name="t-record", game_arguments=["-rc1tape"], hold=False), dict(plain, name="t-play", game_arguments=["-ptapes\\demo2"], hold=True),
                   dict(plain, name="t-demo", game_arguments=["-D"], demo_tape="DEMO1", outcome=True), dict(plain, name="t-plain")]
    registry = draft_registry(root/"tapes.json", tape_suites)
    out = root/"never-written-tapes"
    dry = ["-DryRun", "-OutputDirectory", out, "-SuitesFile", registry]
    plans = {p["suite"]: p for p in json.loads(ps("run-oracle-regression.ps1", *common, *dry).stdout)["plans"]}
    check(plans["t-record"]["tape_plan"]["mode"] == "record" and plans["t-play"]["tape_plan"]["tape"] == "tapes/demo2.tap", plans)
    check(plans["t-demo"]["tape_plan"]["demo_tape"] == "DEMO1" and plans["t-demo"]["runner"] == "run-mission-outcome.ps1" and "tape_plan" not in plans["t-plain"], plans)
    # hold: true adds HoldAtTicks to the plan; false or absent leaves the plan as it was.
    check(plans["t-play"]["arguments"]["HoldAtTicks"] is True and all("HoldAtTicks" not in plans[n]["arguments"] for n in ("t-record", "t-demo", "t-plain")), plans)
    # Command-line tape options on a plain suite keep the plain pass-through (no plan); on a tape suite they are a second option.
    given = {p["suite"]: p for p in json.loads(ps("run-oracle-regression.ps1", *common, *dry, "-Suite", "t-plain", "-GameArgumentsJson", json.dumps(["-ptapes\\demo1"])).stdout)["plans"]}
    check("tape_plan" not in given["t-plain"] and given["t-plain"]["arguments"]["GameArguments"] == ["-ptapes\\demo1"], given)
    r = ps("run-oracle-regression.ps1", *common, *dry, "-Suite", "t-demo", "-GameArgumentsJson", json.dumps(["-rrun"]), ok=False)
    check("t-demo" in r.stderr and "more than one tape option" in r.stderr, r.stderr)
    for entry, word in ((dict(plain, name="t-bad", game_arguments=["-D"]), "time() % 3"), (dict(plain, name="t-bad", demo_tape=["DEMO1"]), "must be a string"),
                        (dict(plain, name="t-bad", demo_tape="DEMO1"), "needs the DBSIM argument -D"), (dict(plain, name="t-bad", game_arguments=["-dsave"]), "dump file"),
                        (dict(plain, name="t-bad", hold="yes"), "must be true or false"), (dict(plain, name="t-bad", hold=True, outcome=True), "outcome suite")):
        r = ps("run-oracle-regression.ps1", *common, "-DryRun", "-OutputDirectory", out, "-SuitesFile", draft_registry(root/"tapes-bad.json", [entry]), ok=False)
        check("t-bad" in r.stderr and word in r.stderr, (entry, r.stderr))
    check(not out.exists())
    # A mock check run of the tape suites goes through the transaction and leaves the clone unchanged.
    mock = root/"tape-mock"; golden = root/"tape-golden"
    for s in tape_suites:
        lines(mock/s["name"]/"tick.jsonl", events(n=s.get("frame_limit", 3)))
        write(mock/s["name"]/"report.json", dict(pid=123))
        write(golden/s["name"]/"normalized.json", oracle.normalize(mock/s["name"]/"tick.jsonl", None, frame_limit=s.get("frame_limit")))
        manifest = dict(commit="synthetic-baseline")
        if s.get("game_arguments"): manifest["game_arguments"] = s["game_arguments"]
        write(golden/s["name"]/"manifest.json", manifest)
    run = root/"tape-check"
    ps("run-oracle-regression.ps1", *common, "-SuitesFile", registry, "-MockDirectory", mock, "-GoldenRoot", golden, "-OutputDirectory", run, "-Python", sys.executable)
    def load(path): return json.loads(path.read_text(encoding="utf-8-sig"))
    summary = load(run/"summary.json")
    check([r["result"] for r in summary["results"]] == ["identical"]*4, summary)
    entry = load(run/"t-demo-entry.json")
    check(entry["tape_plan"]["demo_tape"] == "DEMO1" and entry["launch"]["tape_plan"]["mode"] == "demo", entry)
    check("tape_plan" not in load(run/"t-plain-entry.json") and "tape_plan" not in load(run/"t-plain-entry.json")["launch"])
    unchanged("tape-check")


def lane_exit_codes(root):
    """The exit code of the own DBSIM in entry and summary (the mock fixture own-exit.json describes the
    process), its comparison with the golden and the removal of empty hook mismatch logs."""
    fx = fixture(root)
    mock, golden, registry = legacy_inputs(root, fx, only="acceptance-60s")
    def load(path): return json.loads(path.read_text(encoding="utf-8-sig"))
    source = mock/"acceptance-60s"
    manifest_path = golden/"acceptance-60s/manifest.json"
    base_manifest = load(manifest_path)
    counter = [0]
    def describe(exit_code=0, stopped_by_launcher=False):
        # What Wait-CloneOwnedProcessExit would have returned, and the launch report of the mission runner.
        write(source/"own-exit.json", dict(status="exited", pid=123, waited_ms=0, exit_code=exit_code,
                                           exit_code_source="launcher-handle", stopped_by_runner=False))
        write(source/"report.json", dict(pid=123, **({"stopped_after_identity_check": True} if stopped_by_launcher else {})))
    def golden_with(own=None):
        manifest = dict(base_manifest)
        if own is not None: manifest["own_process_exit"] = own
        write(manifest_path, manifest)
    def run(ok=True, extra=(), golden_root=golden):
        counter[0] += 1
        out = root/f"run{counter[0]}"
        r = ps("run-oracle-regression.ps1", *fx.common, "-MockDirectory", mock, "-GoldenRoot", golden_root, "-Python", sys.executable,
               "-OutputDirectory", out, "-Suite", "acceptance-60s", *extra, ok=ok)
        return out, r
    def item(out): return load(out/"summary.json")["results"][0]
    exited = dict(status="exited", exit_code=0, exit_code_state="exited")
    none = dict(status="exited", exit_code=None, exit_code_state="none")

    # No process described (a mock run, or a run without an exit-code record): no exit-code keys anywhere.
    out, _ = run()
    check(load(out/"summary.json")["results"] == [dict(suite="acceptance-60s", result="identical")])
    check(load(out/"acceptance-60s-entry.json")["own_process_exit"] is None)

    # A golden without the field: reported, never a difference.
    describe(0)
    golden_with()
    out, _ = run()
    check(item(out) == dict(suite="acceptance-60s", result="identical", exit_code=0, exit_code_state="exited",
                            exit_code_check="no golden exit code"), item(out))
    entry = load(out/"acceptance-60s-entry.json")
    check(entry["own_process_exit"]["exit_code"] == 0 and entry["own_process_exit"]["exit_code_state"] == "exited"
          and entry["own_process_exit"]["stopped_by_runner"] is False and entry["exit_code_check"] == "no golden exit code"
          and "golden_exit_code_state" not in entry, entry)

    # A golden with the field: the same code is the same, another code makes the suite DIFFERENT.
    golden_with(exited)
    out, _ = run()
    check(item(out)["exit_code_check"] == "same" and item(out)["result"] == "identical", item(out))
    check(load(out/"acceptance-60s-entry.json")["golden_exit_code_state"] == "exited")
    describe(3)
    out, r = run(ok=False)
    check(item(out) == dict(suite="acceptance-60s", result="DIFFERENT", exit_code=3, exit_code_state="exited", exit_code_check="different"), item(out))
    entry = load(out/"acceptance-60s-entry.json")
    check(entry["result"] == "DIFFERENT" and entry["golden_exit_code"] == 0 and entry["golden_exit_code_state"] == "exited", entry)
    check("Exit code of the own DBSIM differs" in r.stdout, r.stdout)
    # The process ended by itself but no handle gave its code: nothing to confirm, DIFFERENT against a golden that has one.
    describe(None)
    out, _ = run(ok=False)
    check(item(out)["exit_code_state"] == "unknown" and item(out)["exit_code"] is None and item(out)["result"] == "DIFFERENT", item(out))
    # ... but only a golden with the field cares.
    golden_with()
    out, _ = run()
    check(item(out)["exit_code_check"] == "no golden exit code" and item(out)["result"] == "identical", item(out))

    # The runner stopped the game (launch report): there is no code of the game, whatever the stopped process returned.
    describe(1, stopped_by_launcher=True)
    golden_with(none)
    out, _ = run()
    check(item(out) == dict(suite="acceptance-60s", result="identical", exit_code=None, exit_code_state="none", exit_code_check="same"), item(out))
    check(load(out/"acceptance-60s-entry.json")["own_process_exit"]["stopped_by_runner"] is True)
    golden_with(exited)
    out, _ = run(ok=False)
    check(item(out)["exit_code_check"] == "different" and item(out)["result"] == "DIFFERENT", item(out))
    describe(0)
    golden_with(none)
    out, _ = run(ok=False)
    check(item(out)["exit_code_check"] == "different" and item(out)["exit_code"] == 0, item(out))

    # Record mode stores the record in the golden manifest; a check against it is the same.
    describe(0)
    recorded = root/"recorded-golden"
    shutil.copytree(golden, recorded)
    counter[0] += 1
    rec = root/f"run{counter[0]}"
    ps("run-oracle-regression.ps1", *fx.common, "-MockDirectory", mock, "-GoldenRoot", recorded, "-Python", sys.executable,
       "-OutputDirectory", rec, "-Suite", "acceptance-60s", "-Mode", "record", "-Force")
    check(load(rec/"summary.json")["results"] == [dict(suite="acceptance-60s", result="recorded", exit_code=0, exit_code_state="exited")])
    stored = load(recorded/"acceptance-60s/manifest.json")["own_process_exit"]
    check(stored["exit_code"] == 0 and stored["exit_code_state"] == "exited", stored)
    out, _ = run(golden_root=recorded)
    check(item(out)["exit_code_check"] == "same", item(out))

    # Empty hook mismatch log: removed once the game is gone; a log with content stays.
    describe(0)
    golden_with(exited)
    hook_log = source/"tick.jsonl.hooks.jsonl"
    hook_log.write_bytes(b"")
    out, _ = run()
    check(not (out/"acceptance-60s.jsonl.hooks.jsonl").exists() and load(out/"acceptance-60s-entry.json")["empty_hook_log_removed"] is True)
    check((out/"acceptance-60s.jsonl").is_file() and item(out)["result"] == "identical")
    hook_log.write_bytes(b'{"hook":"x"}\n')
    out, _ = run()
    check((out/"acceptance-60s.jsonl.hooks.jsonl").read_bytes() == b'{"hook":"x"}\n'
          and "empty_hook_log_removed" not in load(out/"acceptance-60s-entry.json"))
    hook_log.unlink()
    shutil.rmtree(root/"hook-log-unit", ignore_errors=True)
    unit = root/"hook-log-unit"; unit.mkdir()
    (unit/"a.jsonl.hooks.jsonl").write_bytes(b"")
    (unit/"b.jsonl.hooks.jsonl").write_bytes(b"1")
    module = quote(SETUP/"SuiteLaunch.psm1")
    r = ps(f"Import-Module {module} -Force; (Remove-SuiteEmptyHookLog {quote(unit)} 'a'), (Remove-SuiteEmptyHookLog {quote(unit)} 'b'), "
           f"(Remove-SuiteEmptyHookLog {quote(unit)} 'missing') -join ','", command=True)
    check(r.stdout.strip() == "True,False,False" and not (unit/"a.jsonl.hooks.jsonl").exists() and (unit/"b.jsonl.hooks.jsonl").exists(), r.stdout)


def lane_vshell_screens(root):
    """capture-vshell-screens.ps1 on a synthetic clone; the own mock game stands in for VSHELL.EXE."""
    name = os.environ.get("ES2_MOCK_GAME", "")
    mock = Path(name).resolve() if name else None
    manifest = os.environ.get("RUNFILES_MANIFEST_FILE")
    if name and not mock.is_file() and manifest:   # a runfiles path under Bazel (no runfiles tree on Windows)
        for entry in Path(manifest).read_text(encoding="utf-8").splitlines():
            if entry.startswith(f"_main/{name} "): mock = Path(entry.split(" ", 1)[1])
    check(mock is not None and mock.is_file(), "ES2_MOCK_GAME (the clone guard's mock game) is required")
    data = root/"data"; data.mkdir(parents=True)
    (data/'.es2-synthetic-fixture').write_text('synthetic suite fixtures only', encoding='ascii')
    install = data/"installs/vshell-clone"
    for d in ("DATA", "SAV"): (install/d).mkdir(parents=True)
    (install/"DATA/DRIVE.CFG").write_bytes(b"Q:\r\nsynthetic\r\n")
    (install/"DATA/PREFS.CFG").write_bytes(b"synthetic-prefs")
    sav = {"GAME_R.SAV": b"synthetic-r", "GAME_T.SAV": b"synthetic-t", "missn10.str": b"m10", "missn11.str": b"m11",
           "player10.mec": b"p10", "player11.mec": b"p11", "script10.dat": b"s10", "script11.dat": b"s11"}
    for name, payload in sav.items(): (install/"SAV"/name).write_bytes(payload)
    shutil.copyfile(mock, install/"VSHELL.EXE")
    (install/"ddraw.dll").write_bytes(b"synthetic-proxy")
    (install/"-eggplant").write_bytes(b"")   # the mock's stop file: with VSHELL's arguments it exits at once with 0
    vshell_sha = hashlib.sha256((install/"VSHELL.EXE").read_bytes()).hexdigest().upper()
    write(install/"installation-manifest.json", dict(hashes=[dict(file="VSHELL.EXE", sha256=vshell_sha)]))
    cd = root/"cd"; cd.mkdir()
    def tree():
        return {str(p.relative_to(install)): p.read_bytes() for d in ("DATA", "SAV") for p in sorted((install/d).rglob("*")) if p.is_file()}
    before = tree()
    tours = root/"tours"; tours.mkdir()
    lines(tours/"a.jsonl", [dict(op="mark", screen="main_menu", state="start"), dict(op="wait", ms=10),
                            dict(op="snap"), dict(op="click", control="main_menu.19", note="PREFERENCES"), dict(op="close")])
    lines(tours/"b.jsonl", [dict(op="snap")])
    lines(tours/"bad.jsonl", [dict(op="click", control="main_menu.99")])
    def tour(name, runs):
        write(tours/name, dict(schema=1, runs=runs))
        return tours/name
    copy_t = [dict(**{"from": "SAV/GAME_T.SAV", "to": "SAV/GAME_R.SAV"})]
    main = tour("tour.json", [dict(name="a", start_state="GAME_T as slot 10", copy=copy_t, arguments=["dummy", "-eggplant", "-X1", "-a"], journal="a.jsonl"),
                              dict(name="slow", start_state="own", copy=[], arguments=["30000", "-"], journal="b.jsonl"),
                              dict(name="box", start_state="own", copy=[], arguments=["box", "Hinweis", "synthetic-notice"], journal="b.jsonl"),
                              dict(name="off", enabled=False, copy=[], arguments=["dummy"], journal="b.jsonl")])
    common = ["-DataRoot", data, "-InstallName", "vshell-clone", "-CdRoot", cd]
    script = "capture-vshell-screens.ps1"
    def json_of(text):
        return json.JSONDecoder().raw_decode(text[text.index("{"):])[0]
    # Dry run: the plan with resolved controls (main_menu.19 = [317,223,470,238] -> centre 393,230); disabled runs skipped.
    plan = json_of(ps(script, *common, "-Tour", main, "-DryRun").stdout)
    check([r["name"] for r in plan["runs"]] == ["a", "slow", "box"] and plan["compatibility_layer"] == "256COLOR", plan)
    check(plan["confirm_notice_seconds"] == 0, plan)
    click = [json.loads(l) for l in plan["runs"][0]["lines"] if json.loads(l)["op"] == "click"][0]
    check(click["x"] == 393 and click["y"] == 230 and click["control"] == "main_menu.19", click)
    check(not (data/"logs").exists() and tree() == before)
    check([r["name"] for r in json_of(ps(script, *common, "-Tour", main, "-Run", "off", "-DryRun").stdout)["runs"]] == ["off"])
    # The shipped first tour (input-scripts/vshell/tour.json): every control resolves against vshell_layout.json.
    shipped = json_of(ps(script, *common, "-DryRun").stdout)
    check([r["name"] for r in shipped["runs"]] == ["game_r", "game_t", "new_career"], shipped["runs"])
    for r in shipped["runs"]:
        entries = [json.loads(l) for l in r["lines"]]
        check(all(0 <= e["x"] < 640 and 0 <= e["y"] < 480 for e in entries if e["op"] == "click") and entries[-1]["op"] == "close", r["name"])
    # Refusals before anything is touched.
    for args, word in (((*common[:2], "-InstallName", "native-r111", "-Tour", main, "-DryRun"), "primary"),
                       ((*common, "-Tour", tour("bad.json", [dict(name="x", copy=[], arguments=[], journal="bad.jsonl")]), "-DryRun"), "main_menu.99"),
                       ((*common, "-Tour", tour("escape.json", [dict(name="x", copy=[{"from": "SAV/../ES.EXE", "to": "SAV/X"}], arguments=[], journal="b.jsonl")]), "-DryRun"), "DATA or SAV"),
                       ((*common, "-Tour", main, "-Run", "nothing", "-DryRun"), "no run 'nothing'")):
        r = ps(script, *args, ok=False)
        check(word in r.stderr + r.stdout, (word, r.stderr))
    check(not (data/"logs").exists() and tree() == before)
    # A run: backup, start state, DRIVE.CFG for the CD, capture INI, resolved journal, restore; the environment is put back.
    r = ps(f"& {quote(SETUP/script)} -DataRoot {quote(data)} -InstallName vshell-clone -CdRoot {quote(cd)} -Tour {quote(main)} -Run a; "
           "'EXIT=' + $LASTEXITCODE; 'ENV=' + $env:ES2_VSHELL_CAPTURE_CONFIG + '|' + $env:__COMPAT_LAYER", command=True)
    check("EXIT=0" in r.stdout and "ENV=|" in r.stdout and "RESTORED" in r.stdout, r.stdout)
    check(tree() == before, "DATA/SAV not restored")
    sessions = sorted((data/"logs").glob("vshell-screens-*"))
    check(len(sessions) == 1)
    session = sessions[0]
    run = json.loads((session/"runs/a/run.json").read_text(encoding="utf-8"))
    check(run["sav_at_launch"]["GAME_R.SAV"] == hashlib.sha256(b"synthetic-t").hexdigest().upper(), run["sav_at_launch"])
    check(run["process"]["status"] == "exited" and run["process"]["exit_code"] == 0 and not run["process"]["stopped_by_runner"], run["process"])
    ini = (session/"runs/a/capture.ini").read_bytes()
    check(ini[:2] == b"\xff\xfe", "INI must be UTF-16 with BOM")
    ini = ini.decode("utf-16")
    check("[vshell_capture]" in ini and "capture=1" in ini and "hold_focus=1" in ini and "block_real_input=1" in ini and "every=0" in ini and "dump_dir" not in ini, ini)
    check(f"journal={session/'runs/a/journal.jsonl'}" in ini and f"log={session/'runs/a/capture.jsonl'}" in ini, ini)
    journal = [json.loads(l) for l in (session/"runs/a/journal.jsonl").read_text(encoding="utf-8").splitlines()]
    check(len(journal) == 5 and journal[3]["x"] == 393 and journal[3]["y"] == 230)
    check((session/"backup/.gitignore").is_file() and (session/"private/.gitignore").is_file())
    frames = json.loads((session/"frames.json").read_text(encoding="utf-8"))
    check(frames["schema"] == 1 and frames["vshell_sha256"] == vshell_sha and frames["runs"][0]["status"] == "no-capture-log", frames)
    # The proxy's records (synthetic here) become frames.json with -SummarizeOnly.
    h1, h2, p = "11"*32, "22"*32, "33"*32
    lines(session/"runs/a/capture.jsonl", [
        dict(kind="start", seq=0, schema=1, module_sha256_ok=True),
        dict(kind="palette", seq=1, palette_sha256=p),
        dict(kind="present", seq=2, **{"pass": 3}, step=1, screen="main_menu", state="start", site="0x406e57", dd=0, index_sha256=h1, palette_sha256=p),
        dict(kind="snap", seq=3, **{"pass": 9}, step=2, screen="main_menu", state="start", dd=0, index_sha256=h2, palette_sha256=p),
        dict(kind="step", seq=4, **{"pass": 9}, step=3, op="click"),
        dict(kind="end", seq=5, journal_done=True, presents=1)])
    ps(script, "-SummarizeOnly", session)
    frames = json.loads((session/"frames.json").read_text(encoding="utf-8"))
    a = frames["runs"][0]
    check(a["status"] == "complete" and a["counts"]["present"] == 1 and a["counts"]["snap"] == 1 and len(a["frames"]) == 2, a)
    check(frames["screens"] == [dict(run="a", screen="main_menu", state="start", step=2, index_sha256=h2, palette_sha256=p)], frames["screens"])
    # Record version 1 (no -Qpc): the start record says schema 1, the frames carry no "qpc", the INI has no key.
    check(a["capture_start"]["schema"] == 1 and all("qpc" not in f for f in a["frames"]), a)
    check("qpc" not in ini, ini)
    # Record version 2: -Qpc writes qpc=1; the same summary keeps each frame's counter value (and the start
    # record with its frequency); frames.json stays schema 1 and the screens list is the same.
    plan = json_of(ps(script, *common, "-Tour", main, "-Qpc", "-DryRun").stdout)
    check(plan["qpc"] is True and json_of(ps(script, *common, "-Tour", main, "-DryRun").stdout)["qpc"] is False, plan)
    r = ps(f"& {quote(SETUP/script)} -DataRoot {quote(data)} -InstallName vshell-clone -CdRoot {quote(cd)} -Tour {quote(main)} -Run a -Qpc; "
           "'EXIT=' + $LASTEXITCODE", command=True)
    check("EXIT=0" in r.stdout and tree() == before, r.stdout)
    session2 = sorted((data/"logs").glob("vshell-screens-*"))[-1]
    check(session2 != session, "a second session")
    ini2 = (session2/"runs/a/capture.ini").read_bytes().decode("utf-16")
    check("qpc=1" in ini2 and "every=0" in ini2 and "capture=1" in ini2, ini2)
    q0 = 123456789012
    lines(session2/"runs/a/capture.jsonl", [
        dict(kind="start", seq=0, qpc=q0, schema=2, qpc_frequency=10000000, module_sha256_ok=True),
        dict(kind="palette", seq=1, qpc=q0+5, palette_sha256=p),
        dict(kind="present", seq=2, qpc=q0+100, **{"pass": 3}, step=1, screen="main_menu", state="start", site="0x406e57", dd=0, index_sha256=h1, palette_sha256=p),
        dict(kind="snap", seq=3, qpc=q0+200, **{"pass": 9}, step=2, screen="main_menu", state="start", dd=0, index_sha256=h2, palette_sha256=p),
        dict(kind="end", seq=4, qpc=q0+300, journal_done=True, presents=1)])
    ps(script, "-SummarizeOnly", session2)
    frames2 = json.loads((session2/"frames.json").read_text(encoding="utf-8"))
    b = frames2["runs"][0]
    check(frames2["schema"] == 1 and b["status"] == "complete" and b["capture_start"]["schema"] == 2 and b["capture_start"]["qpc_frequency"] == 10000000, b)
    check([f["qpc"] for f in b["frames"]] == [q0+100, q0+200] and frames2["screens"] == frames["screens"], b["frames"])
    # A shell that does not end in time: only this script's own process is stopped, then DATA and SAV are restored.
    r = ps(script, *common, "-Tour", main, "-Run", "slow", "-RunTimeoutSeconds", 10)
    session = sorted((data/"logs").glob("vshell-screens-*"))[-1]
    slow = json.loads((session/"runs/slow/run.json").read_text(encoding="utf-8"))
    check(slow["process"]["status"] == "stopped" and slow["process"]["stopped_by_runner"], slow["process"])
    check(tree() == before and not list(install.glob("*.tmp")))
    check(json.loads((session/"frames.json").read_text(encoding="utf-8"))["runs"][0]["status"] == "no-capture-log")
    # The colour-depth notice (a message box of the run's own process) is answered by its button; the mock
    # exits 0 only when the box returned IDOK. (Start-Process does not quote arguments: no spaces in them.)
    r = ps(script, *common, "-Tour", main, "-Run", "box", "-ConfirmNoticeSeconds", 30, "-RunTimeoutSeconds", 60)
    session = sorted((data/"logs").glob("vshell-screens-*"))[-1]
    box = json.loads((session/"runs/box/run.json").read_text(encoding="utf-8"))
    notice = box["notice"]
    check(notice["found"] and notice["posted"] and notice["closed"] and notice["title"] == "Hinweis" and notice["text"] == "synthetic-notice", notice)
    check(box["process"]["status"] == "exited" and box["process"]["exit_code"] == 0 and not box["process"]["stopped_by_runner"], box["process"])
    check(json.loads((session/"session.json").read_text(encoding="utf-8"))["confirm_notice_seconds"] == 30)
    check(tree() == before)


def lane_python_only(root):
    """Mec tool and watch-set proposals: no PowerShell."""
    data = root/"data"; launch = make_launch(data)
    if len(sys.argv) > 2:
        tool=Path(sys.argv[2]).resolve()
        result=subprocess.run([str(tool),'show',str(launch/'player.mec')],capture_output=True,text=True)
        check(result.returncode==0 and 'vehicle=0 K=3 items=0,1,2 secondary=5,5,5' in result.stdout)
        def mec_diff(payload):
            alternate=root/'diff.mec'; alternate.write_bytes(payload)
            return subprocess.run([str(tool),'diff',str(launch/'player.mec'),str(alternate)],capture_output=True,text=True)
        check(mec_diff(synthetic_mec()).returncode==0)
        altered=bytearray(synthetic_mec());struct.pack_into('<h',altered,8,1);struct.pack_into('<h',altered,18,4)
        result=mec_diff(altered)
        check(result.returncode==1 and 'vehicle=1' in result.stdout and 'secondary=4,5,5' in result.stdout)
        altered=bytearray(synthetic_mec());altered[4]=1;altered[24]=1;altered[-1]=1;altered[0]=1
        result=mec_diff(altered)
        check(result.returncode==1 and all(s in result.stdout for s in ('pilot block: 1','opaque tail: 1','suffix flags: 1','header: 0 -> 1')))
        single=struct.pack('<hh',0,1)+synthetic_mec()[4:92]+synthetic_mec()[-35:]
        result=mec_diff(single)
        check(result.returncode==1 and 'removed record=1' in result.stdout and 'records: 2 -> 1' in result.stdout)
        check(mec_diff(synthetic_mec()[:-1]).returncode==2)
        cd=os.environ.get('ES2_DATA_ROOT')
        if cd:
            target=root/'generated.mec'
            cmd=[str(tool),'set',str(launch/'player.mec'),str(target),cd,'--record','0','1','1,2,3,0']
            edited=subprocess.run(cmd,capture_output=True,text=True)
            check(edited.returncode==0,edited.stderr)
            check(watch.mec_records(target.read_bytes())[0]==dict(vehicle=1,K=4,items=[1,2,3,0]))
            saved=target.read_bytes()
            check(subprocess.run(cmd,capture_output=True).returncode!=0 and target.read_bytes()==saved)
            invalid=cmd.copy();invalid[3]=str(root/'invalid.mec');invalid[-1]='999'
            check(subprocess.run(invalid,capture_output=True).returncode!=0 and not (root/'invalid.mec').exists())
    # Discovery log establishes pool membership and side without hardcoded indices.
    bootstrap,report=watch.propose(launch,bootstrap=True)
    check("actor_index 0x4a9af8@0 w0x4a9afe*4" in bootstrap and len(bootstrap)<=48)
    check(report['actor_count_upper_bound']==6 and 'actor5_side 0x4a9af8@20@0x45@0x12 1' in bootstrap)
    group=bytearray(122);struct.pack_into("<hh",group,0x10,2,0)
    group[0x13]=1  # Arrival flag is not part of the category byte.
    obs={"actor_index":struct.pack("<4I",100,200,300,400),"team_index":struct.pack("<2I",100,300),"player_group":group,
         "actor1_side":struct.pack("<b",1),"actor3_side":struct.pack("<b",1),"route":struct.pack("<hI",2,1234)}
    discovery=root/"discovery.jsonl"
    lines(discovery,[dict(event="tick",player_actor=100)]+[dict(event="watch",name=k,hex=bytes(v).hex()) for k,v in obs.items()])
    proposal,report=watch.propose(launch,discovery)
    check(report["selected_hostiles"]==[1,3] and "team1_pos 0x4d24e6@0x45@0xc@4@0x26 12" in proposal)
    check(not any(x.startswith(("actor_index ","team_index ")) for x in proposal))
    check(len(proposal)<=48 and any(x.startswith("h3_target ") for x in proposal))
    del obs["actor3_side"]
    lines(discovery,[dict(event="tick",player_actor=100)]+[dict(event="watch",name=k,hex=bytes(v).hex()) for k,v in obs.items()])
    try:watch.propose(launch,discovery)
    except ValueError:check(True)
    else:check(False,"missing side evidence accepted")
    check(watch.propose(launch,discovery,hostile_indices=[1])[1]["unknown_sides"]==[3])


def run_lane(lane, root, errors):
    LOCAL.session = None
    started = time.time()
    try:
        root.mkdir(parents=True)
        LOCAL.session = PwshSession(PWSH, scratch=root)
        lane(root)
    except BaseException as error:  # reported by main() after all lanes ended
        errors.append((lane.__name__, error))
    finally:
        if LOCAL.session is not None:
            LOCAL.session.dispose()
        print(f"lane {lane.__name__}: {time.time() - started:.1f} s", flush=True)


def main():
    temporary_root = Path(os.environ.get("TEST_TMPDIR", REPO/"build/codex/scratch-suite-infra"))
    temporary_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=temporary_root) as tmp:
        root = Path(tmp).resolve()
        # The sections are independent of each other (each owns a synthetic
        # DataRoot), so they run side by side, each with its own PowerShell process.
        # Bazel shards (shard_count in BUILD.bazel) split the lanes (lane i belongs to shard i % shard_count);
        # without sharding all run. The long lanes come first.
        lanes = shard_items((lane_record_verify, lane_legacy_runner, *legacy_group_lanes(), lane_dry, lane_suite_arguments,
                             lane_transactions, lane_outcome, lane_tapes, lane_exit_codes, lane_python_only, lane_environment,
                             lane_vshell_screens))
        errors = []
        threads = [threading.Thread(target=run_lane, args=(lane, root/lane.__name__, errors)) for lane in lanes]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        if errors:
            for name, error in errors[1:]:
                print(f"lane {name} also failed: {error!r}", flush=True)
            raise errors[0][1]
    print(f"suite infrastructure: {checks} checks passed in {steps} PowerShell steps; no game launched")


if __name__=="__main__":
    main()
