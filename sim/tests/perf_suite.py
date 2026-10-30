"""es2sim's frame times per suite (own_process.md, "es2sim's frame times").

  perf_suite.py summary PATH... [--budget-ms 40] [--json OUT]
  perf_suite.py run --sim <es2sim.exe> [--window <es2sim_window.exe>] --suite NAME [--suite NAME ...]
                    --output-dir DIR [--registry <oracle-suites.json>] [--oracle-script <oracle_regression.py>]
                    [--replay <replay_suite.py>] [--no-headless] [--window-frames N] [--real-audio] [--json OUT]

summary reads the recorder files es2sim writes with ES2SIM_PERF (sim/include/es2/sim/perf.h; a directory is searched
for perf*.ndjson) and prints one line per file: frames, the frame time (median, p95, max), the busy time (frame time
minus the window's sleeps), traps and dispatched calls per frame, the share of frames whose busy time exceeds the
budget (the original's limiter: 40 ms) and the virtual-time step (47 ms: windowed, a fast frame lasts the step),
and the trap share of the busy time at the measured cost of 11.58 us per trap.

run measures suites:
  headless  replay_suite.py --sim <es2sim> --suite NAME --output-dir DIR with ES2SIM_PERF=1, so the recorder writes
            DIR/<suite>/perf.ndjson next to the tick log; the replay's verdict is kept (the recorder must not change
            it: IDENTICAL stays IDENTICAL);
  window    (with --window) the windowed build in the suite's working directory (replay_suite.py's preparation) on
            SDL's dummy video driver (SDL_VIDEODRIVER=dummy, no visible window) and, unless --real-audio, the dummy
            audio driver, as a windowed replay of the suite's journal (virtual time unpaced, so the frame time is
            the busy time) for --window-frames frames (default: the suite's frame_limit, else its ticks);
            ES2SIM_PERF writes DIR/<suite>/perf-window.ndjson.
Environment as for replay_suite.py (ES2_DATA_ROOT, ES2_DATA_REPO, ES2_ORACLE_DIR, ES2_REPLAY_TIMEOUT, ES2_REPLAY_PREFS).
Exit status: 0 when every file could be read (summary) or every run produced a recorder file and every headless
replay was IDENTICAL or SKIP (run); 1 otherwise; 2 for a usage error.
"""
import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile

# Every child process starts without a console window (//tools:process_starts_test).
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
HERE = os.path.dirname(os.path.abspath(__file__))
TRAP_COST_US = 11.58     # measured (own_process.md, "How a call reaches the dispatcher")
BUDGET_MS = 40.0         # the limiter 0x4678c0: previous + 40
STEP_MS = 47.0           # the virtual-time step (ES2_SIM_VT_STEP): windowed, a fast frame lasts the step (own_process.md, "the windowed platform layer")


# ---------------------------------------------------------------- reading

def read_perf(path):
    """(header, frames, end) of one recorder file; end is None when the run ended without one (a trapped refusal ends
    the process in the trap, no end line)."""
    header, frames, end = None, [], None
    with open(path, encoding="utf-8") as stream:
        for number, text in enumerate(stream, start=1):
            text = text.strip()
            if not text:
                continue
            try:
                record = json.loads(text)
            except ValueError:
                raise ValueError("%s line %d: not JSON" % (path, number))
            if "perf" in record:
                header = record
            elif "f" in record:
                frames.append(record)
            elif "end" in record:
                end = record
    if header is None and end is None:
        raise ValueError("%s: no es2sim recorder header" % path)
    return header or {}, frames, end


def percentile(values, q):
    """Nearest-rank percentile of a non-empty list (q in 0..100)."""
    ordered = sorted(values)
    rank = max(1, -(-len(ordered) * q // 100))
    return ordered[min(len(ordered), int(rank)) - 1]


def median(values):
    ordered = sorted(values)
    n = len(ordered)
    return ordered[n // 2] if n % 2 else (ordered[n // 2 - 1] + ordered[n // 2]) / 2.0


def label_of(path):
    """The suite: the file's directory name; a file other than perf.ndjson adds its stem (perf-window)."""
    directory = os.path.basename(os.path.dirname(os.path.abspath(path)))
    stem = os.path.splitext(os.path.basename(path))[0]
    return directory if stem == "perf" else "%s/%s" % (directory, stem)


def summarize(path, budget_ms=BUDGET_MS, step_ms=STEP_MS):
    header, frames, end = read_perf(path)
    out = {"file": path, "suite": label_of(path), "mode": header.get("mode", "?"), "frames": len(frames),
           "setup_s": round(header.get("setup_us", 0) / 1e6, 2), "end": end["end"] if end else None,
           "hot": (end or {}).get("hot", [])}
    if not frames:
        return out
    wall = [f["us"] / 1000.0 for f in frames]
    busy = [max(0, f["us"] - f.get("wait", 0)) / 1000.0 for f in frames]
    work = [f.get("work", 0) / 1000.0 for f in frames]
    traps = [f["traps"] for f in frames]
    calls = [f["calls"] for f in frames]
    over = sum(1 for b in busy if b > budget_ms)
    over_step = sum(1 for b in busy if b > step_ms)
    total_busy_us = sum(busy) * 1000.0
    out.update({
        "frame_ms": {"median": round(median(wall), 2), "p95": round(percentile(wall, 95), 2), "max": round(max(wall), 2)},
        "busy_ms": {"median": round(median(busy), 2), "p95": round(percentile(busy, 95), 2), "max": round(max(busy), 2)},
        "work_ms": {"median": round(median(work), 2), "p95": round(percentile(work, 95), 2), "max": round(max(work), 2)},
        "traps_per_frame": {"median": median(traps), "mean": round(sum(traps) / len(traps), 1), "max": max(traps)},
        "calls_per_frame": {"median": median(calls), "mean": round(sum(calls) / len(calls), 1), "max": max(calls)},
        "budget_ms": budget_ms, "over_budget": over, "over_budget_share": round(over / len(frames), 4),
        "step_ms": step_ms, "over_step": over_step, "over_step_share": round(over_step / len(frames), 4),
        "trap_share_of_busy": round(sum(traps) * TRAP_COST_US / total_busy_us, 3) if total_busy_us else None,
        "run_s": round(sum(wall) / 1000.0, 2),
    })
    return out


def line_of(s):
    if not s["frames"]:
        return "%-34s %-8s no frames (end %s)" % (s["suite"], s["mode"], s["end"])
    f, b, t, c = s["frame_ms"], s["busy_ms"], s["traps_per_frame"], s["calls_per_frame"]
    return ("%-34s %-8s %6d frames  frame ms %6.1f/%6.1f/%7.1f  busy ms %6.1f/%6.1f/%7.1f  traps/frame %7d/%9.1f/%8d  "
            "calls/frame %9.1f  over %g ms %5.1f%%  over %g ms %5.1f%%  trap share %s  setup %.1f s" % (
                s["suite"], s["mode"], s["frames"], f["median"], f["p95"], f["max"], b["median"], b["p95"], b["max"],
                t["median"], t["mean"], t["max"], c["mean"], s["budget_ms"], 100.0 * s["over_budget_share"],
                s["step_ms"], 100.0 * s["over_step_share"],
                "-" if s["trap_share_of_busy"] is None else "%.0f%%" % (100.0 * s["trap_share_of_busy"]), s["setup_s"]))


HEADER = "suite / mode / frames / frame ms median/p95/max / busy ms median/p95/max / traps per frame median/mean/max / calls per frame mean / share over the budget (40 ms) and the step (47 ms) / trap share of busy (11.58 us per trap) / set-up"


def expand(paths):
    files = []
    for path in paths:
        if os.path.isdir(path):
            for directory, _, names in sorted(os.walk(path)):
                files += [os.path.join(directory, n) for n in sorted(names) if n.startswith("perf") and n.endswith(".ndjson")]
        else:
            files.append(path)
    return files


def print_summaries(files, budget_ms, json_out=None, extra=None):
    print(HEADER)
    results, bad = [], 0
    for path in files:
        try:
            s = summarize(path, budget_ms)
        except (OSError, ValueError) as error:
            print("%s: unreadable: %s" % (path, error))
            bad += 1
            continue
        if extra and s["suite"] in extra:
            s.update(extra[s["suite"]])
        results.append(s)
        print(line_of(s) + ("  [replay %s]" % s["replay"] if s.get("replay") else ""))
    if json_out:
        with open(json_out, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(results, stream, indent=1)
    return results, bad


# ---------------------------------------------------------------- running

def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_headless(args, name, out_dir):
    env = dict(os.environ, ES2SIM_PERF="1")
    command = [sys.executable, "-B", args.replay, "--sim", args.sim, "--suite", name, "--output-dir", out_dir,
               "--registry", args.registry, "--oracle-script", args.oracle_script]
    completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
    text = completed.stdout.decode("utf-8", "replace").strip()
    print(text, flush=True)
    verdict = "?"
    for line in text.splitlines():
        if line.startswith("replay %s: " % name):
            verdict = line.split(": ", 1)[1].split(" ", 1)[0]
    return os.path.join(out_dir, name, "perf.ndjson"), verdict


def run_window(args, replay, name, suite, out_dir):
    root = os.environ.get("ES2_DATA_ROOT", "")
    cd = replay.find_cd(root)
    oracle_dir = replay.find_oracle_dir(root, os.environ.get("ES2_ORACLE_DIR"), os.environ.get("ES2_DATA_REPO"))
    if not cd or not oracle_dir:
        print("window %s: SKIP no CD below ES2_DATA_ROOT or no oracle directory" % name)
        return None
    plan = replay.build_plan(name, suite, os.path.dirname(os.path.abspath(args.registry)))
    frames = args.window_frames or plan["frame_limit"] or plan["ticks"]
    plan = dict(plan, frame_limit=frames)
    try:
        launch_dir, _ = replay.read_launch_set(oracle_dir, plan["launch"])
    except replay.Skip as skip:
        print("window %s: SKIP %s" % (name, skip))
        return None
    suite_dir = os.path.join(out_dir, name)
    os.makedirs(suite_dir, exist_ok=True)
    perf = os.path.join(suite_dir, "perf-window.ndjson")
    work = tempfile.mkdtemp(prefix="perf-window-%s-" % name, dir=os.environ.get("TEST_TMPDIR") or tempfile.gettempdir())
    install = os.path.join(work, "install")
    try:
        replay.prepare_workdir(install, cd, launch_dir, plan, os.environ.get("ES2_REPLAY_PREFS") or None)
        ini = os.path.join(suite_dir, "es2-window.ini")
        replay.write_unicode(ini, replay.config_text(plan))
        log = os.path.join(suite_dir, "tick-log-window.jsonl")
        if os.path.exists(log):
            os.remove(log)
        env = dict(os.environ, ES2SIM_PERF=perf, SDL_VIDEODRIVER="dummy")
        if not args.real_audio:
            env["SDL_AUDIODRIVER"] = "dummy"
        command = [args.window, "--config", ini, "--tick-log", log]
        timeout = int(frames * BUDGET_MS / 1000.0 * 4) + 300
        try:
            completed = subprocess.run(command, cwd=install, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       timeout=timeout, creationflags=NO_WINDOW)
            code, out, err = completed.returncode, completed.stdout, completed.stderr
        except subprocess.TimeoutExpired as expired:
            code, out, err = None, expired.stdout or b"", expired.stderr or b""
        with open(os.path.join(suite_dir, "window-stdout.txt"), "wb") as stream:
            stream.write(out or b"")
        with open(os.path.join(suite_dir, "window-stderr.txt"), "wb") as stream:
            stream.write(err or b"")
        print("window %s: exit %s after %d frames requested" % (name, code, frames), flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return perf


def main(argv=None):
    root = os.path.normpath(os.path.join(HERE, "..", ".."))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p_sum = sub.add_parser("summary")
    p_sum.add_argument("paths", nargs="+")
    p_sum.add_argument("--budget-ms", type=float, default=BUDGET_MS)
    p_sum.add_argument("--json")
    p_run = sub.add_parser("run")
    p_run.add_argument("--sim")
    p_run.add_argument("--window")
    p_run.add_argument("--suite", action="append", default=[], required=True)
    p_run.add_argument("--output-dir", required=True)
    p_run.add_argument("--registry", default=os.path.join(root, "tools", "reference-game", "oracle-suites.json"))
    p_run.add_argument("--oracle-script", default=os.path.join(root, "tools", "reference-game", "scripts", "oracle_regression.py"))
    p_run.add_argument("--replay", default=os.path.join(HERE, "replay_suite.py"))
    p_run.add_argument("--no-headless", action="store_true")
    p_run.add_argument("--window-frames", type=int)
    p_run.add_argument("--real-audio", action="store_true")
    p_run.add_argument("--budget-ms", type=float, default=BUDGET_MS)
    p_run.add_argument("--json")
    args = parser.parse_args(argv)
    if args.command == "summary":
        files = expand(args.paths)
        if not files:
            print("no recorder files in %s" % " ".join(args.paths))
            return 1
        _, bad = print_summaries(files, args.budget_ms, args.json)
        return 1 if bad else 0
    if args.no_headless and not args.window:
        parser.error("--no-headless needs --window")
    if not args.no_headless and not args.sim:
        parser.error("--sim is required for the headless runs")
    out_dir = os.path.abspath(args.output_dir)
    os.makedirs(out_dir, exist_ok=True)
    replay = load_module("replay_suite_for_perf", args.replay)
    suites = replay.load_registry(args.registry)
    unknown = [n for n in args.suite if n not in suites]
    if unknown:
        parser.error("not in the registry: %s" % ", ".join(unknown))
    files, extra, failed = [], {}, 0
    for name in args.suite:
        if not args.no_headless:
            path, verdict = run_headless(args, name, out_dir)
            extra[name] = {"replay": verdict}
            if verdict not in (replay.VERDICT_OK, "SKIP"):
                failed += 1
            if os.path.isfile(path):
                files.append(path)
            elif verdict != "SKIP":
                print("%s: no recorder file %s" % (name, path))
                failed += 1
        if args.window:
            path = run_window(args, replay, name, suites[name], out_dir)
            if path and os.path.isfile(path):
                files.append(path)
            elif path:
                print("%s: no recorder file %s" % (name, path))
                failed += 1
    if files:
        _, bad = print_summaries(files, args.budget_ms, args.json, extra)
        failed += bad
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
