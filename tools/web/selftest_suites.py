"""Writes selftest_suites.json, the self-test's suite replays (docs/web_build.md section 16).

usage: selftest_suites.py --site SITE --node NODE --out FILE [--suite NAME]... [--game-host game_host.mjs]
                          [--registry oracle-suites.json] [--oracle-script oracle_regression.py] [--work DIR]

For each suite (DEFAULT_SUITES without --suite) the inputs of sim/tests/replay_site.py's run go into one entry:
the four launch files of its launch set in C:\\ES2\\DATA and TAPES\\DEMOLIST.STR for a demo tape (game_files), the
INI of replay_suite.config_text with the journal below C:\\ES2HOST and the journal itself (host_files), and es2sim's
arguments (--headless, --config, --tick-log). web/node/game_host.mjs --replay then runs the entry over SITE with the
site's es2sim.js and vfs.js, as the page's worker runs it, and the run is judged against the suite's golden as
replay_suite.py judges it. Only when every suite is IDENTICAL is FILE written, with each entry's exit code and masked
tick log hash (web/tick_log_hash.mjs) and the SHA-256 of SITE/es2sim.wasm, which the page compares with the
es2sim.wasm it serves. build_site.py --selftest --file selftest_suites.json=FILE takes it into a local site.

The launch sets and goldens are read from es2-data's oracle directory (ES2_ORACLE_DIR, ES2_DATA_REPO or beside
ES2_DATA_ROOT, as replay_suite.py finds it). FILE holds game files and journals: it belongs to a local build only.
"""
import argparse
import base64
import codecs
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
spec = importlib.util.spec_from_file_location("replay_suite", os.path.join(ROOT, "sim", "tests", "replay_suite.py"))
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)

# Short suites of different kinds: the quit box (the game's own exit), mouse input, another campaign's launch set,
# a tape playback and the acceptance flight.
DEFAULT_SUITES = ("c1_01-quit-dialog", "c1_01-mouse", "c4_10-probe", "c1_01-tape-play", "acceptance-60s")
GAME_DIR = "C:\\ES2"
HOST_DIR = "C:\\ES2HOST"
CONFIG_FILE = HOST_DIR + "\\suite.ini"
JOURNAL_FILE = HOST_DIR + "\\suite-journal.jsonl"
TICK_LOG = HOST_DIR + "\\suite-tick-log.jsonl"
LANGUAGE = "G"
TIMEOUT_SECONDS = 1200


def b64(data):
    return base64.b64encode(data).decode("ascii")


def suite_entry(name, plan, launch_dir):
    """The entry of one suite without the Node run's results."""
    game_files = []
    for member in rs.LAUNCH_FILES:
        with open(os.path.join(launch_dir, member), "rb") as stream:
            game_files.append(["%s\\DATA\\%s" % (GAME_DIR, member), b64(stream.read())])
    if plan.get("demo_tape"):
        game_files.append([GAME_DIR + "\\TAPES\\DEMOLIST.STR", b64(rs.demo_list(plan["demo_tape"]))])
    config = rs.config_text(plan, journal=JOURNAL_FILE)
    with open(plan["script"], "rb") as stream:
        journal = stream.read()
    return {
        "name": name,
        "ticks": plan["ticks"],
        "language": LANGUAGE,
        "game_files": game_files,
        "host_files": [[CONFIG_FILE, b64(codecs.BOM_UTF16_LE + config.encode("utf-16-le"))], [JOURNAL_FILE, b64(journal)]],
        "args": ["--headless", "--config", CONFIG_FILE, "--tick-log", TICK_LOG],
        "tick_log": TICK_LOG,
        "collect": [TICK_LOG] + ["%s\\DATA\\%s" % (GAME_DIR, f) for f in rs.RETURN_FILES],
    }


def run_entry(entry, args, work):
    """game_host.mjs --replay over the site: (record, stderr text, seconds, collect directory)."""
    suites = os.path.join(work, "suites.json")
    with open(suites, "w", encoding="utf-8") as stream:
        json.dump({"suites": [entry]}, stream)
    record = os.path.join(work, "record.json")
    collected = os.path.join(work, "collected")
    command = [args.node, args.game_host, "--site", args.site, "--codec", os.path.join(args.site, "vfs.js"),
               "--sim", os.path.join(args.site, "es2sim.js"), "--replay", suites, "--suite", entry["name"],
               "--record", record, "--collect-dir", collected]
    toc = os.path.join(args.site, "cd", "toc.json")
    if os.path.isfile(toc):
        command += ["--cd-toc", toc]
    begin = time.time()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=TIMEOUT_SECONDS,
                               creationflags=rs.NO_WINDOW)
    seconds = time.time() - begin
    stderr = completed.stderr.decode("utf-8", "replace")
    if not os.path.isfile(record):
        raise RuntimeError("game_host.mjs exit %d without a record: %s" % (completed.returncode, stderr.strip()[-400:]))
    with open(record, encoding="utf-8") as stream:
        return json.load(stream), stderr, seconds, collected


def judge(name, plan, entry, ctx, start_hashes, record, stderr, seconds, collected, work):
    """replay_suite's verdict of the Node run against the golden."""
    golden_dir = os.path.join(ctx["oracle_dir"], "golden", name)
    golden_path = os.path.join(golden_dir, "normalized.json")
    log_path = os.path.join(collected, TICK_LOG.split("\\")[-1])
    install = os.path.join(work, "install")
    os.makedirs(os.path.join(install, "DATA"))
    for member in rs.RETURN_FILES:
        source = os.path.join(collected, member)
        if os.path.isfile(source):
            shutil.copyfile(source, os.path.join(install, "DATA", member))
    outputs_dir = os.path.join(work, "outputs")
    rs.collect_outputs(install, outputs_dir, plan, start_hashes)
    scan = rs.scan_log(log_path) if os.path.isfile(log_path) else {"ticks": 0, "refused": None, "exit": None}
    code = record.get("replay", {}).get("sim")
    timing = {"run_seconds": round(seconds, 2), "ticks": scan["ticks"]}
    return rs._judge(name, plan, ctx, golden_dir, golden_path, outputs_dir, log_path, code, scan, stderr, timing)


def main(argv=None):
    registry_default, oracle_default = rs.default_paths()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--site", required=True, help="a built site with es2sim.js, es2sim.wasm and vfs.js")
    ap.add_argument("--node", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--suite", action="append", default=[])
    ap.add_argument("--game-host", default=os.path.join(ROOT, "web", "node", "game_host.mjs"))
    ap.add_argument("--registry", default=registry_default)
    ap.add_argument("--oracle-script", default=oracle_default)
    ap.add_argument("--work", help="keep each run's files below this directory")
    args = ap.parse_args(argv)
    args.site = os.path.abspath(args.site)
    for need in ("es2sim.js", "es2sim.wasm", "vfs.js", "manifest.json"):
        if not os.path.isfile(os.path.join(args.site, need)):
            ap.error("%s has no %s" % (args.site, need))
    suites = rs.load_registry(args.registry)
    names = args.suite or list(DEFAULT_SUITES)
    unknown = [n for n in names if n not in suites]
    if unknown:
        ap.error("not in the registry: %s" % ", ".join(unknown))
    root = os.environ.get("ES2_DATA_ROOT", "")
    oracle_dir = rs.find_oracle_dir(root, os.environ.get("ES2_ORACLE_DIR"), os.environ.get("ES2_DATA_REPO"))
    if not oracle_dir or not os.path.isdir(os.path.join(oracle_dir, "golden")):
        print("selftest_suites: no oracle directory (%s)" % oracle_dir, file=sys.stderr)
        return 1
    ctx = {"oracle_dir": oracle_dir, "oracle": rs.load_oracle_module(os.path.abspath(args.oracle_script)),
           "timeout": TIMEOUT_SECONDS}
    tool_root = os.path.dirname(os.path.abspath(args.registry))
    entries, failed = [], 0
    for name in names:
        plan = rs.build_plan(name, suites[name], tool_root)
        launch_dir, start_hashes = rs.read_launch_set(oracle_dir, plan["launch"])
        entry = suite_entry(name, plan, launch_dir)
        work = os.path.join(os.path.abspath(args.work), name) if args.work else tempfile.mkdtemp(prefix="selftest-suite-")
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(work)
        try:
            record, stderr, seconds, collected = run_entry(entry, args, work)
            verdict = judge(name, plan, entry, ctx, start_hashes, record, stderr, seconds, collected, work)
        finally:
            if not args.work:
                shutil.rmtree(work, ignore_errors=True)
        print(verdict["line"], flush=True)
        replay = record.get("replay") or {}
        if verdict["verdict"] != rs.VERDICT_OK or not replay.get("sha256"):
            failed += 1
            continue
        entry.update({"exit_code": replay["sim"], "sha256": replay["sha256"], "lines": replay["lines"],
                      "node_seconds": round(seconds, 1)})
        entries.append(entry)
    if failed:
        print("selftest_suites: %d of %d suites not IDENTICAL, %s not written" % (failed, len(names), args.out),
              file=sys.stderr)
        return 1
    with open(os.path.join(args.site, "es2sim.wasm"), "rb") as stream:
        wasm = hashlib.sha256(stream.read()).hexdigest()
    with open(args.out, "w", encoding="utf-8", newline="\n") as stream:
        json.dump({"format": 1, "es2sim_wasm_sha256": wasm, "suites": entries}, stream, indent=1)
        stream.write("\n")
    print("selftest_suites: %d suites, es2sim.wasm %s, written to %s" % (len(entries), wasm[:12], args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
