"""perf_suite.py on synthetic recorder files and a stub replay driver (no game, no data).

  perf_suite_test.py <perf_suite.py>
"""
import importlib.util
import io
import json
import os
import sys
import tempfile
from contextlib import redirect_stdout


def load(path):
    spec = importlib.util.spec_from_file_location("perf_suite", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check(condition, text):
    if not condition:
        print("FAIL: " + text)
        sys.exit(1)


def write_lines(path, records):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record) + "\n")


STUB_REPLAY = r'''
import json, os, sys
VERDICT_OK = "IDENTICAL"


class Skip(Exception):
    pass


def load_registry(path):
    with open(path) as stream:
        return {s["name"]: s for s in json.load(stream)["suites"]}


if __name__ == "__main__":
    args = sys.argv[1:]
    name = args[args.index("--suite") + 1]
    out = args[args.index("--output-dir") + 1]
    assert os.environ.get("ES2SIM_PERF") == "1"
    os.makedirs(os.path.join(out, name), exist_ok=True)
    with open(os.path.join(out, name, "perf.ndjson"), "w") as s:
        for record in ({"perf": "es2sim", "version": 1, "mode": "headless", "clock_hz": 1000000, "setup_us": 1000},
                       {"f": 0, "us": 50000, "work": 49000, "wait": 0, "traps": 4000, "calls": 4100},
                       {"end": "frame_limit", "frames": 1, "us": 50000, "traps": 4000, "calls": 4100, "hot": []}):
            print(json.dumps(record), file=s)
    print("replay %s: %s stub [1 ticks, 0.1 s]" % (name, "DIFFERENT" if name == "bad" else "IDENTICAL"))
'''


def main():
    perf = load(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix="perf-suite-test-", dir=os.environ.get("TEST_TMPDIR"))
    # Ten frames: 9 of 30 ms busy (window: 10 ms of it slept), one of 60 ms with no sleep.
    frames = [{"f": i, "us": 40000, "work": 28000, "wait": 10000, "traps": 1000 + i, "calls": 2000} for i in range(9)]
    frames.append({"f": 9, "us": 60000, "work": 58000, "wait": 0, "traps": 3000, "calls": 5000})
    header = {"perf": "es2sim", "version": 1, "mode": "window", "clock_hz": 10000000, "setup_us": 2500000}
    end = {"end": "frame_limit", "frames": 10, "us": 420000, "traps": 12036, "calls": 23000, "hot": [["0x0049630c", 900]]}
    path = os.path.join(tmp, "out", "suite-a", "perf-window.ndjson")
    write_lines(path, [header] + frames + [end])
    s = perf.summarize(path)
    check(s["suite"] == "suite-a/perf-window" and s["mode"] == "window" and s["frames"] == 10, "label, mode, frames: %r" % s)
    check(s["frame_ms"] == {"median": 40.0, "p95": 60.0, "max": 60.0}, "frame ms %r" % s["frame_ms"])
    check(s["busy_ms"] == {"median": 30.0, "p95": 60.0, "max": 60.0}, "busy ms %r" % s["busy_ms"])
    check(s["work_ms"]["median"] == 28.0 and s["work_ms"]["max"] == 58.0, "work ms %r" % s["work_ms"])
    check(s["traps_per_frame"] == {"median": 1004.5, "mean": 1203.6, "max": 3000}, "traps %r" % s["traps_per_frame"])
    check(s["over_budget"] == 1 and s["over_budget_share"] == 0.1, "over budget %r" % s)
    check(s["over_step"] == 1 and s["over_step_share"] == 0.1 and perf.summarize(path, step_ms=29.0)["over_step"] == 10, "over step %r" % s)
    # 12036 traps * 11.58 us over 330 ms busy.
    check(s["trap_share_of_busy"] == round(12036 * 11.58 / 330000.0, 3), "trap share %r" % s["trap_share_of_busy"])
    check(s["setup_s"] == 2.5 and s["end"] == "frame_limit" and s["hot"] == [["0x0049630c", 900]], "setup, end, hot %r" % s)
    check(perf.summarize(path, budget_ms=25.0)["over_budget"] == 10, "budget option")
    print("PASS: frame/busy/work percentiles, traps per frame, share over 40 ms, trap share, set-up and end")

    # A run that ended in a trapped refusal: no end line; a headless file named perf.ndjson; an empty run.
    write_lines(os.path.join(tmp, "out", "suite-b", "perf.ndjson"),
                [dict(header, mode="headless")] + [{"f": 0, "us": 1000, "work": 900, "wait": 0, "traps": 5, "calls": 9}])
    write_lines(os.path.join(tmp, "out", "suite-c", "perf.ndjson"),
                [{"end": "setup", "frames": 0, "us": 0, "traps": 0, "calls": 0, "hot": []}])
    b = perf.summarize(os.path.join(tmp, "out", "suite-b", "perf.ndjson"))
    check(b["suite"] == "suite-b" and b["end"] is None and b["over_budget"] == 0, "no end line %r" % b)
    c = perf.summarize(os.path.join(tmp, "out", "suite-c", "perf.ndjson"))
    check(c["frames"] == 0 and c["end"] == "setup" and "no frames" in perf.line_of(c), "no frames %r" % c)
    files = perf.expand([os.path.join(tmp, "out")])
    check([os.path.basename(os.path.dirname(f)) for f in files] == ["suite-a", "suite-b", "suite-c"], "expand %r" % files)
    out = io.StringIO()
    json_out = os.path.join(tmp, "summary.json")
    with redirect_stdout(out):
        code = perf.main(["summary", os.path.join(tmp, "out"), "--json", json_out])
    check(code == 0 and out.getvalue().count("\n") == 4 and "10.0%" in out.getvalue(), "summary text %r" % out.getvalue())
    with open(json_out, encoding="utf-8") as stream:
        check(len(json.load(stream)) == 3, "summary json")
    bad = os.path.join(tmp, "bad", "perf.ndjson")
    os.makedirs(os.path.dirname(bad))
    with open(bad, "w") as stream:
        stream.write("not json\n")
    with redirect_stdout(io.StringIO()):
        check(perf.main(["summary", bad]) == 1, "unreadable file exits 1")
        check(perf.main(["summary", os.path.join(tmp, "nothing-here")]) == 1, "no files exits 1")
    print("PASS: missing end line, empty run, directory search, text and JSON summary, unreadable input")

    # run: the headless half through a stub replay driver (ES2SIM_PERF=1, the verdict kept; DIFFERENT fails).
    stub = os.path.join(tmp, "stub_replay.py")
    with open(stub, "w") as stream:
        stream.write(STUB_REPLAY)
    registry = os.path.join(tmp, "suites.json")
    with open(registry, "w") as stream:
        json.dump({"suites": [{"name": "good"}, {"name": "bad"}]}, stream)
    sim = os.path.join(tmp, "es2sim.exe")
    common = ["run", "--sim", sim, "--output-dir", os.path.join(tmp, "runs"), "--registry", registry, "--replay", stub,
              "--oracle-script", stub]
    out = io.StringIO()
    with redirect_stdout(out):
        code = perf.main(common + ["--suite", "good"])
    check(code == 0 and "replay good: IDENTICAL" in out.getvalue() and "[replay IDENTICAL]" in out.getvalue()
          and "100.0%" in out.getvalue(), "run good %r" % out.getvalue())
    with redirect_stdout(io.StringIO()):
        check(perf.main(common + ["--suite", "bad"]) == 1, "a DIFFERENT replay fails the run")
    print("PASS: run: headless replays with ES2SIM_PERF=1, verdict kept, a changed verdict fails")
    return 0


if __name__ == "__main__":
    sys.exit(main())
