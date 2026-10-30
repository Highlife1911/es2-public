"""The windowed replay through the web platform, against es2sim_window's (docs/web_build.md section 16, "Window mode").

  replay_window_wasm.py --native <es2sim_window.exe> --registry <oracle-suites.json>
                        --oracle-script <oracle_regression.py> --message-trace-script <message_trace.py>
                        --suite NAME [--replay <replay_suite.py>] [--output-dir DIR] --sim-argv ARG...

  1. runs the suite through replay_suite.py --window --message-trace with es2sim_window natively (SDL's dummy
     drivers); the replay must be IDENTICAL,
  2. reads the desktop and frame metrics that run printed ("windowed (SDL3): screen ..."), the GetSystemMetrics
     answers the web platform gets from its page,
  3. runs it again with the host command of --sim-argv (`NODE web/node/sim_window_host.mjs es2sim_window_web.js`)
     and `--metrics` with those values inserted after the host script; that replay must be IDENTICAL as well,
  4. compares the two message traces with `message_trace.py compare --tick-log-a --tick-log-b`: the injections, the
     other retrievals and the fixed pump counts equal, the unkeyed wall-time retrievals the bounded part.

Each run's files go to <output-dir>/native and <output-dir>/web. Without ES2_DATA_ROOT or the goldens the test is
SKIPped (exit 0). Exit 0 = PASS or SKIP, 1 = FAIL, 2 = usage error.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
METRICS = re.compile(r"^es2sim: windowed \(([^)]*)\): screen (\d+)x(\d+), frame (\d+)/(\d+), caption (\d+)\s*$", re.M)


def replay(args, name, out, sim):
    """(verdict line or None, the driver's output, return code) of one replay_suite.py run."""
    command = [sys.executable, "-B", args.replay, "--registry", args.registry, "--oracle-script", args.oracle_script,
               "--suite", name, "--window", "--message-trace", "--output-dir", out] + sim
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True,
                         creationflags=NO_WINDOW)
    lines = [l for l in run.stdout.splitlines()
             if l.startswith("replay %s:" % name) and not l.startswith("replay %s: notices" % name)]
    return (lines[-1] if lines else None), run.stdout.rstrip(), run.returncode


def metrics_of(stdout_path):
    """(platform, [cx, cy, frame_cx, frame_cy, caption]) from a windowed run's stdout, or None."""
    try:
        with open(stdout_path, "r", encoding="utf-8", errors="replace") as f:
            m = METRICS.search(f.read())
    except OSError:
        return None
    return (m.group(1), [int(m.group(i)) for i in range(2, 7)]) if m else None


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--sim-argv" not in argv:
        print("usage: --sim-argv NODE HOST.mjs MODULE.js is required", file=sys.stderr)
        return 2
    at = argv.index("--sim-argv")
    argv, sim_argv = argv[:at], argv[at + 1:]
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--native", required=True)
    parser.add_argument("--registry", required=True)
    parser.add_argument("--oracle-script", required=True)
    parser.add_argument("--message-trace-script", required=True)
    parser.add_argument("--replay", default=os.path.join(HERE, "replay_suite.py"))
    parser.add_argument("--suite", required=True)
    parser.add_argument("--output-dir")
    args = parser.parse_args(argv)
    if len(sim_argv) < 3:
        parser.error("--sim-argv needs NODE, the host script and the module")
    name = args.suite
    scratch = os.environ.get("TEST_TMPDIR") or tempfile.gettempdir()
    out = args.output_dir or os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR") or os.path.join(scratch, "replay-window-wasm")
    native_out, web_out = os.path.join(out, "native"), os.path.join(out, "web")

    line, text, code = replay(args, name, native_out, ["--sim", args.native])
    print(text)
    if line and " SKIP " in line:
        print("window-wasm %s: SKIP (the replay was skipped)" % name)
        return 0
    if code != 0 or not line or " IDENTICAL" not in line:
        print("window-wasm %s: FAIL the native windowed replay is not IDENTICAL" % name)
        return 1
    found = metrics_of(os.path.join(native_out, name, "host-stdout.txt"))
    if not found:
        print("window-wasm %s: FAIL no metrics line in the native run's stdout" % name)
        return 1
    platform, metrics = found
    print("window-wasm %s: the native run's platform %s, metrics %s" % (name, platform, ",".join(map(str, metrics))))

    web_sim = ["--sim-argv"] + sim_argv[:2] + ["--metrics", ",".join(map(str, metrics))] + sim_argv[2:]
    line, text, code = replay(args, name, web_out, web_sim)
    print(text)
    if code != 0 or not line or " IDENTICAL" not in line:
        print("window-wasm %s: FAIL the web platform's windowed replay is not IDENTICAL" % name)
        return 1
    found = metrics_of(os.path.join(web_out, name, "host-stdout.txt"))
    if not found or found[0] != "web" or found[1] != metrics:
        print("window-wasm %s: FAIL the web run did not report the web platform with the native metrics (%s)" % (name, found))
        return 1

    log_a, log_b = os.path.join(native_out, name, "tick-log.jsonl"), os.path.join(web_out, name, "tick-log.jsonl")
    command = [sys.executable, "-B", args.message_trace_script, "compare", log_a + ".messages.ndjson",
               log_b + ".messages.ndjson", "--tick-log-a", log_a, "--tick-log-b", log_b]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True,
                         creationflags=NO_WINDOW)
    print(run.stdout.rstrip())
    if run.returncode != 0:
        print("window-wasm %s: FAIL the message traces differ beyond the unkeyed retrievals" % name)
        return 1
    print("window-wasm %s: PASS" % name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
