"""The frame compare of one suite as a test (own_process.md, "the frame compare as tests").

  frames_suite.py --sim <es2sim.exe> --registry <oracle-suites.json> --oracle-script <oracle_regression.py>
                  --replay <replay_suite.py> --frames-script <dbsim_frames.py> --suite NAME
                  [--reference DIR] [--capture FILE] [--output-dir DIR] [--suggest] [--sim-argv ARG...]

  1. runs the suite through replay_suite.py --frames (es2sim --headless --frames: the frame capture
     with the region hashes of proxy/frame_regions.h); the replay itself must be IDENTICAL,
  2. compares the capture with the reference manifest by `dbsim_frames.py regions` (every kind of difference
     with its first frame: sequence, geometry, frame, palette, display, the regions view/hud/panel/mfd/rest,
     other_sites),
  3. checks the kinds against the known-differences list frames_known.py: a kind without a row, a row whose
     first frame moved and a row whose difference is gone fail; rows "original varies" exclude their frames
     and never fail when gone.

--reference: a manifest directory (default <es2-data>/logs/<frames_known.REFERENCE>/<suite>, or
ES2_FRAMES_REFERENCE/<suite> when that is set). --capture: an existing frames.ndjson instead of the run
(step 1 is skipped). --suggest prints the rows that would make the result pass (to update frames_known.py). --sim-argv, the last option,
takes the place of --sim and reaches replay_suite.py unchanged (the host's command prefix, replay_suite.py's docstring).
Without ES2_DATA_ROOT, the goldens or the reference the test is SKIPped (exit 0). Exit 0 = PASS or SKIP,
1 = FAIL, 2 = usage error.
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frames_known  # noqa: E402

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))   # dbsim_frames imports presenter_capture from its own directory
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def rows_of(suite, known=None):
    return [row for row in (frames_known.KNOWN if known is None else known) if row["suite"] == suite]


def excluded_frames(rows):
    frames = []
    for row in rows:
        if row.get("exclude"):
            if row["cause"] != frames_known.ORIGINAL_VARIES:
                raise ValueError("frames_known: %s/%s excludes frames without the cause %r" % (row["suite"], row["kind"], frames_known.ORIGINAL_VARIES))
            frames.extend(row["exclude"])
    return sorted(set(frames))


def check(report, rows):
    """(passed, lines) of a dbsim_frames.region_report against the suite's rows."""
    lines, passed = [], True
    by_kind = {}
    for row in rows:
        if row["cause"] not in frames_known.CAUSES:
            raise ValueError("frames_known: unknown cause %r" % row["cause"])
        if row["kind"] in by_kind:
            raise ValueError("frames_known: two rows for %s/%s" % (row["suite"], row["kind"]))
        by_kind[row["kind"]] = row
    kinds = report["kinds"]
    for kind, entry in kinds.items():
        first = entry["first"]["frame"]
        row = by_kind.get(kind)
        where = "%s from frame %d (%d records, first %s)" % (kind, first, entry["count"], json.dumps(entry["first"], sort_keys=True))
        if row is None or row.get("first") is None:
            passed = False
            lines.append("FAIL unlisted difference: " + where)
        elif first < row["first"]:
            passed = False
            lines.append("FAIL earlier than the row (frame %d, %s): %s" % (row["first"], row["cause"], where))
        elif first > row["first"] and row["cause"] != frames_known.ORIGINAL_VARIES:
            passed = False
            lines.append("FAIL later than the row (frame %d, %s): update the row: %s" % (row["first"], row["cause"], where))
        else:
            lines.append("known (%s): %s" % (row["cause"], where))
    for kind, row in by_kind.items():
        if kind in kinds or row.get("first") is None:
            continue
        if row["cause"] == frames_known.ORIGINAL_VARIES:
            lines.append("gone, original varies (%s): %s from frame %d" % (row["note"], kind, row["first"]))
        else:
            passed = False
            lines.append("FAIL gone (%s): %s from frame %d: drop the row" % (row["cause"], kind, row["first"]))
    return passed, lines


def suggest(suite, report, rows):
    """The rows that make this report pass: kept causes and notes, new rows with cause '?'."""
    by_kind = {row["kind"]: row for row in rows}
    out = [row for row in rows if row.get("first") is None]
    for kind, entry in report["kinds"].items():
        old = by_kind.get(kind, {})
        out.append({"suite": suite, "kind": kind, "first": entry["first"]["frame"], "cause": old.get("cause", "?"),
                    "note": old.get("note", "")})
    return out


def es2_data_root(root, repo=None):
    """es2-data: ES2_DATA_REPO, else two levels above an extracted\\cd ES2_DATA_ROOT (the older
    layout), else ES2_DATA_ROOT itself."""
    if repo:
        return os.path.normpath(repo)
    norm = os.path.normpath(root)
    parent, leaf = os.path.split(norm)
    grand, middle = os.path.split(parent)
    if leaf.lower() == "cd" and middle.lower() == "extracted":
        return grand
    return norm


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    sim_argv = None
    if "--sim-argv" in argv:
        at = argv.index("--sim-argv")
        argv, sim_argv = argv[:at], argv[at + 1:]
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--sim")
    parser.add_argument("--registry")
    parser.add_argument("--oracle-script")
    parser.add_argument("--replay", default=os.path.join(HERE, "replay_suite.py"))
    parser.add_argument("--frames-script", required=True)
    parser.add_argument("--suite", required=True)
    parser.add_argument("--reference")
    parser.add_argument("--capture")
    parser.add_argument("--output-dir")
    parser.add_argument("--suggest", action="store_true")
    args = parser.parse_args(argv)
    name = args.suite
    df = load_module("dbsim_frames_for_frames_suite", os.path.abspath(args.frames_script))
    root = os.environ.get("ES2_DATA_ROOT", "")
    reference = args.reference
    if not reference:
        base = os.environ.get("ES2_FRAMES_REFERENCE") or (os.path.join(es2_data_root(root, os.environ.get("ES2_DATA_REPO")), "logs", frames_known.REFERENCE) if root else "")
        reference = os.path.join(base, name) if base else ""
    if not reference or not os.path.isfile(os.path.join(reference, df.MANIFEST_NAME)):
        print("frames %s: SKIP no reference manifest (%s)" % (name, reference or "ES2_DATA_ROOT not set"))
        return 0
    capture = args.capture
    if not capture:
        if not ((args.sim or sim_argv) and args.registry and args.oracle_script):
            parser.error("--sim (or --sim-argv), --registry and --oracle-script are required without --capture")
        if args.sim and sim_argv is not None:
            parser.error("--sim-argv takes the place of --sim")
        scratch = os.environ.get("TEST_TMPDIR") or tempfile.gettempdir()
        out = args.output_dir or os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR") or os.path.join(scratch, "frames-results")
        command = [sys.executable, "-B", args.replay, "--registry", args.registry,
                   "--oracle-script", args.oracle_script, "--suite", name, "--frames", "--output-dir", out]
        command += ["--sim-argv"] + sim_argv if sim_argv is not None else ["--sim", args.sim]
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True,
                             creationflags=NO_WINDOW)
        print(run.stdout.rstrip())
        line = [l for l in run.stdout.splitlines() if l.startswith("replay %s:" % name)]
        if line and " SKIP " in line[-1]:
            print("frames %s: SKIP (the replay was skipped)" % name)
            return 0
        if run.returncode != 0 or not line or " IDENTICAL" not in line[-1]:
            print("frames %s: FAIL the replay is not IDENTICAL; the frames are not compared" % name)
            return 1
        capture = os.path.join(out, name, "frames.ndjson")
    if not os.path.isfile(capture):
        print("frames %s: FAIL no frame capture %s" % (name, capture))
        return 1
    rows = rows_of(name)
    report = df.regions(reference, capture, excluded_frames(rows))
    print("frames %s: compared through frame %d; sequence records %s, matched %d; region tables %s%s" % (
        name, report["compared_through_frame"], report["sequence_records"], report["matched_records"],
        report["region_tables"], "" if report["regions_compared"] else " (regions not compared)"))
    passed, lines = check(report, rows)
    for l in lines:
        print("  " + l)
    if args.suggest:
        print(json.dumps(suggest(name, report, rows), indent=1))
    print("frames %s: %s" % (name, "PASS" if passed else "FAIL"))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
