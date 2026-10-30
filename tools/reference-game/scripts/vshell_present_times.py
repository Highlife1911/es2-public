"""Wall time per present of a VSHELL capture.

Reads the record file of the VSHELL capture proxy (proxy/vshell_capture.h): `capture.jsonl` in either record
version, or the `frames.json` that capture-vshell-screens.ps1 builds from it (one run, --run).

  schema 1  no time values; the report lists the records and says that no time can be read.
  schema 2  every record has "qpc", the QueryPerformanceCounter value (start record: "qpc_frequency", the
            counts per second). A present or clear record is stamped on entry to the blit that wrote it, so
            the gap between two frame records is the time from one such blit to the next.

For a range of journal steps the report gives, per frame record, the presents since the previous record, the
gap and the mean time per present, and the hold of each record (the time until the next frame record).
`--model-ticks` states how long es2shell shows the same frame, in 16 ms ticks (vshell_ui.md 42.2: 61);
the verdict compares the longest hold in the range (or the hold of --hold-hash) with it.

    python vshell_present_times.py capture.jsonl --steps 34 35 [--hold-hash 832f906f] [--model-ticks 61]
"""
import argparse
import json
import statistics
import sys

SCHEMAS = (1, 2)
FRAME_KINDS = ("present", "clear", "snap")
TICK_MS = 16.0   # one VSHELL tick (vshell_ui.md 42.2)
WAITS = 0.75     # a hold of at least this part of the model: the wait takes wall time
NO_WAIT = 0.25   # a hold of at most this part of the model: it does not


class CaptureError(ValueError):
    pass


def parse_lines(text):
    """The JSON records of a capture.jsonl; a line the proxy did not finish (a stopped process) is skipped."""
    records = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict):
            records.append(record)
    return records


def load(path, run=None):
    """The records of capture.jsonl or of one run of frames.json, with the start record, if any."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    stripped = text.lstrip()
    if stripped.startswith("{") and '"es2-vshell-screens"' in stripped[:400]:
        return load_frames(json.loads(text), run)
    return parse_lines(text)


def load_frames(doc, run):
    runs = doc.get("runs") or []
    if run is None:
        if len(runs) != 1:
            raise CaptureError("frames.json has %d runs; name one with --run" % len(runs))
        chosen = runs[0]
    else:
        found = [r for r in runs if r.get("name") == run]
        if not found:
            raise CaptureError("frames.json has no run %r" % run)
        chosen = found[0]
    start = chosen.get("capture_start") or {}
    records = [dict(start)] if start else []
    records.extend(dict(frame) for frame in chosen.get("frames") or [])
    return records


def schema_of(records):
    """The record version: the start record's "schema" (1 when it has none); CaptureError for an unknown one."""
    start = next((r for r in records if r.get("kind") in ("start", "refused")), None)
    if start is None:
        return 1
    value = start.get("schema", 1)
    if value not in SCHEMAS:
        raise CaptureError("unknown record version %r (this reader knows %s)" % (value, ", ".join(map(str, SCHEMAS))))
    return value


def frequency_of(records):
    start = next((r for r in records if r.get("kind") == "start"), None)
    value = (start or {}).get("qpc_frequency")
    return int(value) if isinstance(value, int) and value > 0 else None


def has_times(records, frames):
    return bool(frames) and all(isinstance(r.get("qpc"), int) for r in frames)


def analyse(records, steps=None, model_ticks=None, tick_ms=TICK_MS, hold_hash=None):
    """The report of a record list (a dict). steps: (first, last) journal steps, inclusive, or None."""
    version = schema_of(records)
    frames_all = [r for r in records if r.get("kind") in FRAME_KINDS]
    report = {"schema": version, "frames_total": len(frames_all), "steps": list(steps) if steps else None,
              "rows": [], "timed": False, "verdict": "no-times", "reason": ""}
    frequency = frequency_of(records)
    timed = has_times(records, frames_all)
    if version == 2 and not timed:
        raise CaptureError("a version 2 capture has frame records without a qpc value")
    if version == 2 and frequency is None:
        raise CaptureError("a version 2 capture has no qpc_frequency in its start record")
    if timed:
        stamps = [r["qpc"] for r in frames_all]
        if any(b < a for a, b in zip(stamps, stamps[1:])):
            raise CaptureError("qpc values are not monotonic")
    report["timed"] = timed
    report["qpc_frequency"] = frequency
    selected = [i for i, r in enumerate(frames_all)
                if steps is None or steps[0] <= r.get("step", -1) <= steps[1]]
    rows = []
    for i in selected:
        r = frames_all[i]
        row = {"seq": r.get("seq"), "step": r.get("step"), "kind": r.get("kind"), "presents": r.get("presents"),
               "index": (r.get("index_sha256") or "")[:8], "gap_ms": None, "per_present_ms": None, "hold_ms": None}
        if timed:
            if i > 0:
                row["gap_ms"] = (r["qpc"] - frames_all[i - 1]["qpc"]) * 1000.0 / frequency
                presents = r.get("presents")
                if isinstance(presents, int) and presents > 0:
                    row["per_present_ms"] = row["gap_ms"] / presents
            if i + 1 < len(frames_all):
                row["hold_ms"] = (frames_all[i + 1]["qpc"] - r["qpc"]) * 1000.0 / frequency
        rows.append(row)
    report["rows"] = rows
    report["presents"] = sum(r["presents"] for r in rows if isinstance(r["presents"], int))
    if not timed:
        report["reason"] = ("version 1 records carry no time; run the capture with qpc=1 (capture-vshell-screens.ps1 -Qpc)"
                            if version == 1 else "no frame records")
        return report
    holds = [r for r in rows if r["hold_ms"] is not None]
    if hold_hash:
        holds = [r for r in holds if r["index"].startswith(hold_hash[:8].lower())]
    if not holds:
        report["verdict"] = "no-hold"
        report["reason"] = "no frame record with a following record in the range" + (" and hash %s" % hold_hash if hold_hash else "")
        return report
    longest = max(holds, key=lambda r: r["hold_ms"])
    report["hold"] = {"seq": longest["seq"], "index": longest["index"], "hold_ms": longest["hold_ms"],
                      "hold_ticks": longest["hold_ms"] / tick_ms}
    steady = [r["per_present_ms"] for r in rows if r["per_present_ms"] is not None and r is not longest]
    if steady:
        report["median_per_present_ms"] = statistics.median(steady)
    if model_ticks is None:
        report["verdict"] = "unjudged"
        report["reason"] = "no --model-ticks given"
        return report
    model_ms = model_ticks * tick_ms
    ratio = longest["hold_ms"] / model_ms
    report["model_ms"] = model_ms
    report["ratio"] = ratio
    if ratio >= WAITS:
        report["verdict"] = "waits"
        report["reason"] = "the hold takes %.0f%% of the model's %d ticks in wall time: the host has to sleep" % (ratio * 100, model_ticks)
    elif ratio <= NO_WAIT:
        report["verdict"] = "no-wait"
        report["reason"] = "the hold takes %.0f%% of the model's %d ticks: the wait is not in wall time" % (ratio * 100, model_ticks)
    else:
        report["verdict"] = "inconclusive"
        report["reason"] = "the hold takes %.0f%% of the model's %d ticks; repeat the run (3 runs, same load)" % (ratio * 100, model_ticks)
    return report


def format_report(report):
    out = ["record version %d, %d frame record(s), %d in range, %d present(s) in range" %
           (report["schema"], report["frames_total"], len(report["rows"]), report["presents"])]
    if report["timed"]:
        out.append("qpc frequency %s per second" % report["qpc_frequency"])
        out.append("   seq step kind     presents   gap_ms  ms/present  hold_ms  index")
        for r in report["rows"]:
            def f(value):
                return "%9.2f" % value if value is not None else "        -"
            out.append("%6s %4s %-8s %8s %s %10s %s  %s" % (r["seq"], r["step"], r["kind"], r["presents"], f(r["gap_ms"]),
                       f(r["per_present_ms"]).strip().rjust(10), f(r["hold_ms"]), r["index"]))
        if "hold" in report:
            h = report["hold"]
            out.append("longest hold: record %s (%s) %.1f ms = %.1f ticks" % (h["seq"], h["index"], h["hold_ms"], h["hold_ticks"]))
        if "median_per_present_ms" in report:
            out.append("median time per present elsewhere: %.2f ms" % report["median_per_present_ms"])
    out.append("verdict: %s%s" % (report["verdict"], " (%s)" % report["reason"] if report["reason"] else ""))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("path", help="capture.jsonl, or frames.json with --run")
    ap.add_argument("--run", help="the run of a frames.json")
    ap.add_argument("--steps", nargs=2, type=int, metavar=("FIRST", "LAST"), help="journal steps, inclusive (34 35)")
    ap.add_argument("--hold-hash", help="judge the hold of the frame record with this index hash prefix")
    ap.add_argument("--model-ticks", type=int, help="how long es2shell shows that frame, in ticks (61)")
    ap.add_argument("--tick-ms", type=float, default=TICK_MS)
    ap.add_argument("--json", action="store_true", help="the report as JSON")
    args = ap.parse_args(argv)
    try:
        report = analyse(load(args.path, args.run), tuple(args.steps) if args.steps else None, args.model_ticks,
                         args.tick_ms, args.hold_hash)
    except (CaptureError, OSError, ValueError) as e:
        print("vshell_present_times: %s" % e, file=sys.stderr)
        return 2
    print(json.dumps(report, indent=1) if args.json else format_report(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
