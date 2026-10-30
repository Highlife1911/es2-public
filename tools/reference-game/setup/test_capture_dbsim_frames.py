"""capture-dbsim-frames.ps1 with a mock run-oracle-regression.ps1 child on a synthetic DataRoot.

Nothing here starts a game: the mock child plays the part of the regression runner of one suite, reads the
presentation INI the script hands down (ES2_PRESENTATION_CONFIG) and writes the capture report the proxy
would write beside the tick log. argv[1] is capture-dbsim-frames.ps1, argv[2] run-oracle-parallel.ps1,
argv[3] dbsim_frames.py; the other scripts are taken from their directories (setup/, scripts/)."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SCRIPT = Path(sys.argv[1]).resolve()
PARALLEL = Path(sys.argv[2]).resolve()
FRAMES_PY = Path(sys.argv[3]).resolve()
SETUP = SCRIPT.parent
SCRIPTS = FRAMES_PY.parent
sys.path.insert(0, str(SETUP))
sys.path.insert(0, str(SCRIPTS))
from pwsh_session import PwshSession  # noqa: E402
import dbsim_frames as df  # noqa: E402

WINPS = str(Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe")
PWSH7 = os.environ.get("ES2_PWSH")
SUITES = ("one", "two", "three")
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def q(value):
    return "'" + str(value).replace("'", "''") + "'"


def lit(values):
    return ",".join(q(v) for v in values)


MOCK = r'''
param($Mode,$Suite,$InstallName,$OutputDirectory,$DataRoot,$GoldenRoot,$RepositoryRoot,$Python,$Hooks='',$GameArgumentsJson='',$SuitesFile='')
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be fresh.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$ini=$env:ES2_PRESENTATION_CONFIG
if(!$ini -or !(Test-Path -LiteralPath $ini)) { throw 'no presentation config handed down' }
$text=[IO.File]::ReadAllText($ini)
Set-Content (Join-Path $DataRoot ('ini-'+$Suite+'.txt')) $text
Set-Content (Join-Path $DataRoot ('ini-path-'+$Suite+'.txt')) $ini
Set-Content (Join-Path $DataRoot ('hooks-'+$Suite+'.txt')) ('value='+$Hooks)
Set-Content (Join-Path $DataRoot ('args-'+$Suite+'.txt')) ('value='+$GameArgumentsJson)
Set-Content (Join-Path $DataRoot ('clone-'+$Suite+'.txt')) $InstallName
$result=if(Test-Path (Join-Path $DataRoot ($Suite+'.fail'))){'DIFFERENT'}else{'identical'}
# What the proxy does for capture=1 / capture_report=beside_tick_log: the report beside the tick log.
$report=Join-Path $OutputDirectory ($Suite+'.jsonl.capture.ndjson')
if($text -match 'capture=1' -and $text -match 'capture_report=beside_tick_log' -and !(Test-Path (Join-Path $DataRoot ($Suite+'.nocapture')))) {
    # With capture_qpc=1 the proxy writes capture schema 2 (a stamp per record): the same records, stamped.
    $source=Join-Path $DataRoot ($Suite+$(if($text -match 'capture_qpc=1'){'.capture-qpc'}else{'.capture'}))
    Copy-Item -LiteralPath $source -Destination $report
    if($text -match 'capture_dump_dir=([^\r\n]+)') {
        $frames=Join-Path $Matches[1].Trim() (($Suite+'.jsonl.capture.ndjson')+'.frames')
        New-Item -ItemType Directory -Force -Path $frames | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $frames 'frame-000003-45fbe4-0.es2f'),[byte[]](1,2,3))
    }
}
# What the proxy does for [call_probe] enabled=1 / report=beside_tick_log (proxy/call_probe.h).
if($text -match '\[call_probe\]' -and $text -match 'enabled=1' -and $text -match 'report=beside_tick_log') {
    $probe='{"event":"start","schema":1,"kind":"es2-call-probe","va":"0x004911c0"}'+"`r`n"+
        '{"event":"dump","frame":652,"call":3,"pointer":"0x2032ff0e","bytes":"51000000"}'+"`r`n"+
        '{"event":"end","reason":"frame_range_done","dumps":1}'+"`r`n"
    [IO.File]::WriteAllText((Join-Path $OutputDirectory ($Suite+'.jsonl.call-probe.ndjson')),$probe)
}
$entry=@{suite=$Suite;proxy_sha256=('ab'*32);hidden_window=$true}
if($GameArgumentsJson) { $entry.game_arguments=[string[]](ConvertFrom-Json $GameArgumentsJson) }
$entry | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory ($Suite+'-entry.json'))
$summary=@{mode=$Mode;output=$OutputDirectory;commit='synthetic-commit';dirty_tree=$false;hooks=$Hooks;results=@(@{suite=$Suite;result=$result})}
if($GameArgumentsJson) { $summary.game_arguments=[string[]](ConvertFrom-Json $GameArgumentsJson) }   # run-oracle-regression.ps1 reports the list it ran with
$summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'summary.json')
if($result -ne 'identical'){exit 1}
'''


def record(frame, site, index, palette, ordinal=0, page=(0, 0)):
    return {"frame": frame, "site": "0x%06x" % site, "ordinal": ordinal, "mode": 0, "S": list(page), "R": [0, 0, 639, 479],
            "client": [0, 0, 640, 480], "src": [page[0], page[1], 640, 480], "dst": [0, 0, 640, 480], "source": "dib",
            "index_sha256": "%064x" % index, "palette_sha256": "%064x" % palette, "uploads": 1, "presented": "original"}


QPC_FREQUENCY = 10_000_000


def capture_text(seed, frames=4, qpc=False):
    lines = [{"event": "start", "schema": 1, "key": "frame_counter_0x4d257b", "presenter": "original", "dump_frames": 0, "pid": 100 + seed}]
    records = [record(f, 0x45FBE4, seed * 1000 + f, seed) for f in range(1, frames + 1)]
    if qpc:   # schema 2, the rate in the start line and a stamp per record, 40 ms apart
        lines[0].update(schema=2, qpc_frequency=QPC_FREQUENCY)
        records = [dict(r, qpc=9_000_000_000 + (r["frame"] - 1) * 400_000) for r in records]
    lines += records
    lines.append({"event": "stats", "frame": frames, "records": len(records), "presents": len(records), "forwarded": 0})
    return "".join(json.dumps(l, separators=(",", ":")) + "\r\n" for l in lines)


class Tree:
    """A synthetic repository, DataRoot, mock child and the scripts under test copied into it."""

    def __init__(self, engine):
        self.session = PwshSession(engine)
        self.root = Path(tempfile.mkdtemp(prefix="dbsim-frames-", dir=os.environ.get("TEST_TMPDIR")))
        self.repo = self.root / "repo"
        setup = self.repo / "tools/reference-game/setup"
        scripts = self.repo / "tools/reference-game/scripts"
        setup.mkdir(parents=True); scripts.mkdir(parents=True)
        for name in ("capture-dbsim-frames.ps1", "run-oracle-parallel.ps1", "CloneGuard.psm1", "SuiteLaunch.psm1"):
            shutil.copyfile(SETUP / name, setup / name)
        for name in ("OracleSpeed.psm1", "dbsim_frames.py", "presenter_capture.py"):
            shutil.copyfile(SCRIPTS / name, scripts / name)
        (self.repo / "tools/reference-game/oracle-suites.json").write_text(
            json.dumps({"suites": [{"name": n, "ticks": 100, "frame_limit": 50 + i} for i, n in enumerate(SUITES)]}), encoding="utf-8")
        self.script = setup / "capture-dbsim-frames.ps1"
        self.data = self.root / "data"
        (self.data / "logs").mkdir(parents=True)
        (self.data / ".es2-synthetic-fixture").write_text("synthetic suite fixtures only", encoding="ascii")
        self.mock = self.root / "mock"
        self.mock.mkdir()
        (self.mock / "run-oracle-regression.ps1").write_text(MOCK, encoding="utf-8")
        self.evidence = self.root / "evidence"
        self.scratch = self.root / "scratch"
        for i, n in enumerate(SUITES, 1):
            (self.data / f"{n}.capture").write_text(capture_text(i, 3 + i), encoding="ascii", newline="")
            (self.data / f"{n}.capture-qpc").write_text(capture_text(i, 3 + i, qpc=True), encoding="ascii", newline="")
        self.common = (f"-DataRoot {q(self.data)} -MockDirectory {q(self.mock)} -EvidenceRoot {q(self.evidence)} "
                       f"-ScratchRoot {q(self.scratch)} -GoldenRoot {q(self.root / 'golden')} -Python {q(sys.executable)}")

    def run(self, extra="", ok=True, prefix="", suffix=""):
        if "-InstallName" not in extra:
            extra += f" -InstallName {lit(['timing-r111-p1', 'timing-r111-p2'])}"
        code = f"$ErrorActionPreference = 'Stop'\n{prefix}\n& {q(self.script)} {self.common} {extra}\n{suffix}\n"
        p = self.session.run(code=code, timeout=240)
        if ok and p.returncode:
            raise AssertionError((extra, p.returncode, p.stdout, p.stderr))
        if not ok and not p.returncode:
            raise AssertionError(("expected failure", extra, p.stdout))
        return p

    def manifest(self, folder, suite):
        return json.loads((self.evidence / folder / suite / "manifest.json").read_text(encoding="utf-8"))

    def listing(self):
        return sorted(str(p.relative_to(self.root)) for p in self.root.rglob("*") if "evidence" in p.parts or "scratch" in p.parts
                      or p.parent == self.data / "logs")

    def close(self):
        self.session.dispose()
        shutil.rmtree(self.root, ignore_errors=True)


def baseline(t):
    folder = "dbsim-frames-20261014"
    leak = "if ($env:ES2_PRESENTATION_CONFIG) { throw 'ES2_PRESENTATION_CONFIG leaked' }"
    # The plan: nothing is created.
    before = t.listing()
    text = t.run("-Date 20261014 -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["suites"] == list(SUITES) and plan["clones"] == ["timing-r111-p1", "timing-r111-p2"] and plan["dump"] is None, plan)
    check(plan["presentation_ini"] == "[presentation]\r\ncapture=1\r\ncapture_report=beside_tick_log\r\n", plan["presentation_ini"])
    # -Regions adds the region hashes (proxy/frame_regions.h) to the capture INI.
    text = t.run("-Date 20261014 -Label regions -Regions -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "[presentation]\r\ncapture=1\r\ncapture_report=beside_tick_log\r\ncapture_regions=1\r\n",
          plan["presentation_ini"])
    check(t.listing() == before and not (t.evidence / folder).exists(), "dry run created something")
    # The baseline run.
    p = t.run("-Date 20261014", suffix=leak)
    for i, name in enumerate(SUITES, 1):
        m = t.manifest(folder, name)
        check(m["status"] == "ok" and m["suite"] == name and m["date"] == "20261014", (name, m))
        run = m["run"]
        check(run["result"] == "identical" and run["hooks"] == "" and run["label"] == "" and run["commit"] == "synthetic-commit"
              and run["dirty_tree"] is False and run["proxy_sha256"] == "ab" * 32 and run["hidden_window"] is True
              and run["ticks"] == 100 and run["frame_limit"] == 49 + i and run["clone"] in ("timing-r111-p1", "timing-r111-p2"), run)
        check(m["frames"]["records"] == 3 + i and m["frames"]["last_frame"] == 3 + i and m["frames"]["main_frames_not_one_count"] == 0, m["frames"])
        check(m["capture"]["closed"] is True and m["capture"]["start"]["pid"] == 100 + i, m["capture"])
        check(df.verify(str(t.evidence / folder / name)) == [], name)
        text = (t.data / f"ini-{name}.txt").read_text()
        check(text.replace("\r", "") .strip().splitlines() == ["[presentation]", "capture=1", "capture_report=beside_tick_log"], text)
        check((t.data / f"hooks-{name}.txt").read_text().strip() == "value=", "hooks")
        check("dumps" not in run, "no dumps requested")
    check({t.manifest(folder, n)["run"]["clone"] for n in SUITES} == {"timing-r111-p1", "timing-r111-p2"}, "both clones used")
    check(len(list((t.evidence / folder).glob("session-*.json"))) == 1, "session record")
    session = json.loads(next((t.evidence / folder).glob("session-*.json")).read_text(encoding="utf-8"))
    check(session["suites"]["two"]["status"] == "ok" and session["suites"]["two"]["records"] == 5, session)
    check(not list((t.data / "logs").glob("dbsim-frames-work-*")), "work directory not removed after an ok run")
    check(not t.scratch.exists(), "no scratch without -Dump")
    # What is in the evidence: the manifests, the tables and the session record; no capture report, no dump.
    names = sorted(p.name for p in (t.evidence / folder).rglob("*") if p.is_file())
    check(sorted(set(names)) == sorted({"manifest.json", "frames.tsv.gz"} | {p.name for p in (t.evidence / folder).glob("session-*.json")}), names)
    # A second run for the same suites is refused before anything starts.
    before = t.listing()
    refused = t.run("-Date 20261014", ok=False)
    check("Already in" in refused.stderr and t.listing() == before, refused.stderr)
    # The work directory is kept with -KeepWork, and the other suites can still be added to a date.
    t.run("-Date 20261015 -Suite two -KeepWork -InstallName 'timing-r111-p1,timing-r111-p2'")   # the form pwsh -File hands over
    check(len(list((t.data / "logs").glob("dbsim-frames-work-*"))) == 1 and (t.evidence / "dbsim-frames-20261015/two/manifest.json").exists(), "keep work")
    shutil.rmtree(next((t.data / "logs").glob("dbsim-frames-work-*")))
    t.run("-Date 20261015 -Suite 'one,three'")
    check(sorted(p.name for p in (t.evidence / "dbsim-frames-20261015").iterdir() if p.is_dir()) == ["one", "three", "two"], "second invocation adds suites")
    check(len(list((t.evidence / "dbsim-frames-20261015").glob("session-*.json"))) == 2, "one session record per invocation")


def failures(t):
    folder = "dbsim-frames-20261016"
    (t.data / "two.fail").touch()
    (t.data / "three.nocapture").touch()
    p = t.run("-Date 20261016", ok=False)
    check(t.manifest(folder, "one")["status"] == "ok", "one")
    two = t.manifest(folder, "two")
    check(two["status"] == "run-different" and two["run"]["result"] == "DIFFERENT" and two["frames"]["records"] == 5, two)
    three = t.manifest(folder, "three")
    check(three["status"] == "no-capture" and "frames" not in three and not (t.evidence / folder / "three/frames.tsv.gz").exists(), three)
    session = json.loads(next((t.evidence / folder).glob("session-*.json")).read_text(encoding="utf-8"))
    check({n: s["status"] for n, s in session["suites"].items()} == {"one": "ok", "two": "run-different", "three": "no-capture"}, session)
    work = list((t.data / "logs").glob("dbsim-frames-work-*"))
    check(len(work) == 1 and (work[0] / "run/summary.json").exists(), "work directory kept after a failure")
    check("not ok: two, three" in p.stdout, p.stdout)
    shutil.rmtree(work[0])
    (t.data / "two.fail").unlink()
    (t.data / "three.nocapture").unlink()


def dumps(t):
    folder = "dbsim-frames-20261017"
    t.run("-Date 20261017 -Suite one -Dump -DumpFrames '3,4' -DumpRange 10-20/2")
    ini = (t.data / "ini-one.txt").read_text().replace("\r", "").strip().splitlines()
    check(ini == ["[presentation]", "capture=1", "capture_report=beside_tick_log", "capture_dump_frames=3 4",
                  "capture_dump_range=10-20/2", f"capture_dump_dir={t.scratch / 'dumps'}"], ini)
    m = t.manifest(folder, "one")
    d = m["run"]["dumps"]
    check(d["files"] == 1 and d["bytes"] == 3 and d["committed"] is False and d["requested"]["range"] == "10-20/2"
          and d["requested"]["frames"] == [3, 4] and d["directory"] == str(t.scratch / "dumps" / "one.jsonl.capture.ndjson.frames"), d)
    check((t.scratch / ".gitignore").read_text().strip() == "*", "scratch ignores everything")
    check(not [p for p in (t.evidence / folder).rglob("*.es2f")], "no dump in the evidence")
    shutil.rmtree(t.scratch)


PROBE = ("-CallProbeVa 0x4911c0 -CallProbeFrames '652,653,709,715' "
         "-CallProbeWhen '[esp+8]-[0x6b7b68]==0x40 && [[0x6b7b68]-4]==0x51' -CallProbePointer '[0x6b7b68]' "
         "-CallProbeOffset -4 -CallProbeLength 0x64")
PROBE_INI = ["[call_probe]", "enabled=1", "va=0x4911c0", "frames=652,653,709,715", "when=[esp+8]-[0x6b7b68]==0x40 && [[0x6b7b68]-4]==0x51",
             "pointer=[0x6b7b68]", "offset=-4", "length=0x64", "report=beside_tick_log"]


def call_probe(t):
    # -CallProbeVa adds [call_probe] to the capture INI; the report is copied into the evidence.
    folder = "dbsim-frames-20261019-probe"
    before = t.listing()
    text = t.run("-Date 20261019 -Suite one -Label probe -DryRun " + PROBE).stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "[presentation]\r\ncapture=1\r\ncapture_report=beside_tick_log\r\n" + "".join(l + "\r\n" for l in PROBE_INI),
          plan["presentation_ini"])
    check(plan["call_probe"]["va"] == "0x4911c0" and plan["call_probe"]["offset"] == "-4" and plan["call_probe"]["max_dumps"] == "", plan["call_probe"])
    check(t.listing() == before, "dry run created something")
    t.run("-Date 20261019 -Suite one -Label probe -CallProbeMaxDumps 8 " + PROBE)
    ini = (t.data / "ini-one.txt").read_text().replace("\r", "").strip().splitlines()
    check(ini == ["[presentation]", "capture=1", "capture_report=beside_tick_log"] + PROBE_INI[:8] + ["max_dumps=8", "report=beside_tick_log"], ini)
    m = t.manifest(folder, "one")
    probe = m["run"]["call_probe"]
    check(m["status"] == "ok" and probe["report"] == "call-probe.ndjson" and probe["dumps"] == 1 and probe["refused"] == 0
          and probe["ended"] == 1 and probe["requested"]["frames"] == "652,653,709,715" and probe["requested"]["max_dumps"] == "8", probe)
    copied = (t.evidence / folder / "one" / "call-probe.ndjson").read_bytes().decode("ascii")   # copied byte for byte
    check(copied.count("\r\n") == 3 and '"pointer":"0x2032ff0e"' in copied, copied)
    session = json.loads(next((t.evidence / folder).glob("session-*.json")).read_text(encoding="utf-8"))
    check(session["suites"]["one"]["call_probe_dumps"] == 1 and session["suites"]["one"]["call_probe_report"] is True
          and session["call_probe"]["pointer"] == "[0x6b7b68]", session)
    # Without the probe nothing of it is written (the baseline's evidence has no call-probe.ndjson either).
    check(not list((t.evidence / "dbsim-frames-20261014").rglob("call-probe.ndjson")), "probe report without -CallProbeVa")
    check("call_probe" not in t.manifest("dbsim-frames-20261014", "one")["run"], "call_probe recorded without -CallProbeVa")


def qpc(t):
    # -Qpc adds capture_qpc=1 to the capture INI; the schema-2 capture is kept beside the manifest.
    folder = "dbsim-frames-20261020-qpc"
    ini_lines = ["[presentation]", "capture=1", "capture_report=beside_tick_log", "capture_qpc=1"]
    before = t.listing()
    text = t.run("-Date 20261020 -Suite one -Label qpc -Qpc -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "".join(l + "\r\n" for l in ini_lines) and plan["qpc"] is True, plan)
    text = t.run("-Date 20261020 -Suite one -Label qpc -Qpc -Regions -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "".join(l + "\r\n" for l in ini_lines[:3] + ["capture_regions=1", "capture_qpc=1"]), plan)
    text = t.run("-Date 20261020 -Suite one -Label plain -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["qpc"] is False and "capture_qpc" not in plan["presentation_ini"], plan)   # off by default
    check(t.listing() == before and not (t.evidence / folder).exists(), "dry run created something")
    t.run("-Date 20261020 -Suite one -Label qpc -Qpc")
    ini = (t.data / "ini-one.txt").read_text().replace("\r", "").strip().splitlines()
    check(ini == ini_lines, ini)
    m = t.manifest(folder, "one")
    check(m["status"] == "ok" and m["run"]["qpc"] == {"capture_qpc": True, "capture": "capture.ndjson", "tool": "scripts/dbsim_present_times.py"}, m["run"])
    check(m["capture"]["start"] == {"presenter": "original", "dump_frames": 0, "pid": 101, "schema": 2, "qpc_frequency": QPC_FREQUENCY}, m["capture"])
    kept = t.evidence / folder / "one" / "capture.ndjson"
    check(kept.read_bytes() == (t.data / "one.capture-qpc").read_bytes(), "the raw capture is kept byte for byte")
    check(m["capture"]["sha256"] == df.sha256_file(str(kept)) and df.verify(str(t.evidence / folder / "one")) == [], "verify")
    # The table is the baseline's: the stamp is no part of it.
    check((t.evidence / folder / "one" / "frames.tsv.gz").read_bytes() == (t.evidence / "dbsim-frames-20261014" / "one" / "frames.tsv.gz").read_bytes(),
          "the table of a -Qpc run differs from the baseline's")
    session = json.loads(next((t.evidence / folder).glob("session-*.json")).read_text(encoding="utf-8"))
    check(session["qpc"] is True, session)
    # A run without the switch keeps no raw capture and has no qpc entry.
    check(not list((t.evidence / "dbsim-frames-20261014").rglob("capture.ndjson")), "raw capture kept without -Qpc")
    check("qpc" not in t.manifest("dbsim-frames-20261014", "one")["run"], "qpc recorded without -Qpc")


def windowed_directdraw(t):
    # -WindowedDirectDraw adds directdraw=windowed to the capture INI and hands -Z1 to the game; the rest of the run is the same.
    folder = "dbsim-frames-20261024-dd"
    base = ["[presentation]", "capture=1", "capture_report=beside_tick_log"]
    before = t.listing()
    text = t.run("-Date 20261024 -Suite one -Label dd -WindowedDirectDraw -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "".join(l + "\r\n" for l in base + ["directdraw=windowed"])
          and plan["windowed_directdraw"] is True and plan["game_arguments"] == ["-Z1"], plan)
    text = t.run("-Date 20261024 -Suite one -Label dd -WindowedDirectDraw -Qpc -Regions -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["presentation_ini"] == "".join(l + "\r\n" for l in base + ["capture_regions=1", "capture_qpc=1", "directdraw=windowed"])
          and plan["qpc"] is True, plan)
    text = t.run("-Date 20261024 -Suite one -Label plain -DryRun").stdout
    plan = json.loads(text[text.index("{"):text.rindex("}") + 1])
    check(plan["windowed_directdraw"] is False and plan["game_arguments"] == [] and "directdraw" not in plan["presentation_ini"], plan)   # off by default
    check(t.listing() == before and not (t.evidence / folder).exists(), "dry run created something")
    leak = "if ($env:ES2_PRESENTATION_CONFIG) { throw 'ES2_PRESENTATION_CONFIG leaked' }"
    t.run("-Date 20261024 -Suite one -Label dd -WindowedDirectDraw -Qpc", suffix=leak)
    ini = (t.data / "ini-one.txt").read_text().replace("\r", "").strip().splitlines()
    check(ini == base + ["capture_qpc=1", "directdraw=windowed"], ini)
    check(json.loads((t.data / "args-one.txt").read_text().strip()[len("value="):]) == ["-Z1"], (t.data / "args-one.txt").read_text())
    m = t.manifest(folder, "one")
    check(m["status"] == "ok" and m["run"]["directdraw"] == "windowed" and m["run"]["game_arguments"] == ["-Z1"]
          and m["run"]["qpc"]["capture_qpc"] is True, m["run"])
    check(m["capture"]["start"]["schema"] == 2 and df.verify(str(t.evidence / folder / "one")) == [], m["capture"])
    session = json.loads(next((t.evidence / folder).glob("session-*.json")).read_text(encoding="utf-8"))
    check(session["windowed_directdraw"] is True, session)
    # Without the switch nothing of it reaches the child, the manifest or the session record.
    t.run("-Date 20261024 -Suite one -Label plain")
    check((t.data / "args-one.txt").read_text().strip() == "value=", (t.data / "args-one.txt").read_text())
    check("directdraw" not in (t.data / "ini-one.txt").read_text() and "directdraw" not in t.manifest("dbsim-frames-20261024-plain", "one")["run"], "directdraw without the switch")
    check("game_arguments" not in t.manifest("dbsim-frames-20261024-plain", "one")["run"], "game_arguments without the switch")
    session = json.loads(next((t.evidence / "dbsim-frames-20261024-plain").glob("session-*.json")).read_text(encoding="utf-8"))
    check(session["windowed_directdraw"] is False, session)


def refusals(t):
    def refused(extra, text, **kw):
        before = t.listing()
        p = t.run(extra, ok=False)
        check(text in p.stderr, (extra, p.stderr))
        check(t.listing() == before, ("created something", extra))
    refused("-Dump", "needs a selection")
    refused("-DumpFrames 1", "need -Dump")
    refused("-DumpRange 1-5", "need -Dump")
    refused("-Dump -DumpFrames '" + ",".join(str(i) for i in range(65)) + "'", "at most 64")
    refused("-Dump -DumpFrames 1,x", "frame numbers")
    refused("-Dump -DumpRange 9-3", "empty")
    refused("-Dump -DumpRange 0-4096", "more than 4096")
    refused("-Dump -DumpRange abc", "from-to")
    refused("-Dump -DumpRange 5-9/0", "empty")
    refused("-Hooks 'a=original'", "needs a -Label")
    refused("-Label 'bad name'", "plain name")
    refused("-Qpc", "needs a -Label")
    refused("-WindowedDirectDraw", "needs a -Label")
    refused("-Date 2026", "yyyyMMdd")
    refused("-Suite missing", "Unknown suite")
    refused("-InstallName native-r111", "native-r111")
    refused(PROBE, "needs a -Label")
    refused("-Label p " + PROBE.replace("0x4911c0", "4911c0"), "one 0x hex address")
    refused("-Label p -CallProbeVa 0x4911c0 -CallProbeFrames 652 -CallProbeLength 4", "needs -CallProbeFrames, -CallProbePointer")
    refused("-Label p -CallProbeFrames 652", "need -CallProbeVa")
    refused("-Label p -CallProbeOffset -4", "need -CallProbeVa")
    # A DataRoot without the marker is no mock root.
    other = t.root / "unmarked"
    (other / "logs").mkdir(parents=True)
    p = t.session.run(code=f"& {q(t.script)} -DataRoot {q(other)} -MockDirectory {q(t.mock)} -EvidenceRoot {q(t.evidence)} -Python {q(sys.executable)}\n")
    check(p.returncode and "synthetic DataRoot" in p.stderr, p.stderr)
    # A labelled run with hooks: its own directory, the hooks recorded and handed down.
    t.run("-Date 20261018 -Suite one -Hooks 'heap=replaced' -Label replaced")
    m = t.manifest("dbsim-frames-20261018-replaced", "one")
    check(m["run"]["hooks"] == "heap=replaced" and m["run"]["label"] == "replaced" and m["status"] == "ok", m["run"])
    check((t.data / "hooks-one.txt").read_text().strip() == "value=heap=replaced", "hooks reach the child")


def main():
    t = Tree(WINPS)
    try:
        baseline(t)
        failures(t)
        dumps(t)
        call_probe(t)
        qpc(t)
        windowed_directdraw(t)
        refusals(t)
    finally:
        t.close()
    if PWSH7 and Path(PWSH7).exists():
        t = Tree(PWSH7)
        try:
            baseline(t)
            failures(t)
        finally:
            t.close()
    print(f"PASS capture-dbsim-frames.ps1 ({checks} checks)")


if __name__ == "__main__":
    main()
