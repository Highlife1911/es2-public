"""Wall time per present and per sim tick of a DBSIM presentation capture taken with `capture_qpc=1`.

Reads the capture report of proxy/presenter.h (`<tick log>.capture.ndjson`, or the `capture.ndjson` that
`capture-dbsim-frames.ps1 -Qpc` keeps beside the manifest; a manifest directory is accepted too):

  schema 1  no time values: the report says so and gives no figures.
  schema 2  the start record has "qpc_frequency" (QueryPerformanceCounter counts per second) and every record
            has "qpc", the counter read on entry to the hooked present (the StretchBlt call returning to 0x4659b2).

Figures (milliseconds of wall time):
  present gap   the time from one present's entry to the next record's entry, over all records and over the
                main-loop presents (site 0x45fbe4, ordinal 0) alone. Work between presents is part of it.
  tick          a tick is one step of the frame counter [0x4d257b]. Its time is the gap between the first record
                of frame N and the first record of frame N + 1 (and, as a second figure, between the main-loop
                presents of the two frames). Only steps of exactly one frame enter the figures; a jump, or a
                counter that goes back, is counted under "irregular_steps" and left out.
  budget        the original's limiter holds a tick to 40 ms (0x4678c0: previous + 40); the report counts the ticks
                over --budget-ms.

    python dbsim_present_times.py capture.ndjson [--frames FIRST LAST] [--budget-ms 40] [--top 5] [--json]

Exit 0 = report written (also for a schema-1 capture, which only says it has no times), 2 = unusable input.
Standard library only.
"""
import argparse
import json
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbsim_frames as df  # noqa: E402
import presenter_capture as pc  # noqa: E402

BUDGET_MS = 40.0   # the original's tick limiter (0x4678c0)
TOP = 5


class CaptureError(ValueError):
    pass


def load(path):
    """(records, start event, closed, bad lines) of a capture report or of a manifest directory beside one."""
    if os.path.isdir(path):
        path = os.path.join(path, "capture.ndjson")
    records, events, closed, bad = df.read_capture(path)
    return records, events[0], closed, bad


def percentile(sorted_values, fraction):
    """Nearest rank: the smallest value with at least `fraction` of the values at or below it."""
    if not sorted_values:
        return None
    rank = max(1, math.ceil(fraction * len(sorted_values)))
    return sorted_values[rank - 1]


def stats_of(values):
    """min, median, mean, p95, max of a list of numbers (None for an empty list)."""
    if not values:
        return None
    ordered = sorted(values)
    return {"count": len(ordered), "min": ordered[0], "median": statistics.median(ordered),
            "mean": sum(ordered) / len(ordered), "p95": percentile(ordered, 0.95), "max": ordered[-1]}


def gaps_ms(stamps, frequency):
    return [(b - a) * 1000.0 / frequency for a, b in zip(stamps, stamps[1:])]


def tick_steps(entries, frequency):
    """entries: [(frame, qpc)] of one record per frame run, in file order. Returns (steps, irregular) where a
    step is (frame, ms) for a counter that went up by exactly one."""
    steps, irregular = [], 0
    for (frame_a, qpc_a), (frame_b, qpc_b) in zip(entries, entries[1:]):
        if frame_b - frame_a == 1:
            steps.append((frame_a, (qpc_b - qpc_a) * 1000.0 / frequency))
        else:
            irregular += 1
    return steps, irregular


def first_of_runs(records, accept=lambda r: True):
    """[(frame, qpc, count)]: the first accepted record of each run of equal frame numbers, with the number of
    accepted records of the run."""
    out = []
    for r in records:
        if not accept(r):
            continue
        if out and out[-1][0] == r["frame"]:
            out[-1][2] += 1
        else:
            out.append([r["frame"], r["qpc"], 1])
    return out


def tick_figures(steps, irregular, budget_ms):
    values = [ms for _, ms in steps]
    figures = {"steps": len(values), "irregular_steps": irregular, "ms": stats_of(values),
               "budget_ms": budget_ms, "over_budget": sum(1 for v in values if v > budget_ms)}
    if values:
        figures["ticks_per_second"] = 1000.0 * len(values) / sum(values)
    return figures


def analyse(records, start, closed=True, bad_lines=0, frames=None, budget_ms=BUDGET_MS, top=TOP):
    """The report (a dict) of a capture's records. frames: (first, last) frame numbers, inclusive, or None."""
    schema = start.get("schema")
    report = {"schema": schema, "timed": False, "records": len(records), "closed": closed, "bad_lines": bad_lines,
              "frames_range": list(frames) if frames else None, "reason": ""}
    if schema != 2:
        report["reason"] = "schema %s records carry no time; capture with [presentation] capture_qpc=1 (capture-dbsim-frames.ps1 -Qpc)" % schema
        return report
    frequency = start.get("qpc_frequency")
    if not isinstance(frequency, int) or isinstance(frequency, bool) or frequency <= 0:
        raise CaptureError("a schema 2 capture has no usable qpc_frequency in its start record")
    for r in records:
        if not isinstance(r.get("qpc"), int) or isinstance(r.get("qpc"), bool) or r["qpc"] < 0:
            raise CaptureError("a schema 2 capture has a record without a qpc value (frame %s)" % r.get("frame"))
    if any(b["qpc"] < a["qpc"] for a, b in zip(records, records[1:])):
        raise CaptureError("qpc values are not monotonic")
    if frames:
        records = [r for r in records if frames[0] <= r["frame"] <= frames[1]]
    report["timed"] = True
    report["qpc_frequency"] = frequency
    report["records_in_range"] = len(records)
    if not records:
        report["reason"] = "no record in the range"
        return report
    stamps = [r["qpc"] for r in records]
    main = [r for r in records if r["site_value"] == pc.MAIN_SITE and r["ordinal"] == 0]
    runs = first_of_runs(records)
    main_runs = first_of_runs(main)
    report.update({
        "first_frame": records[0]["frame"], "last_frame": records[-1]["frame"], "frame_runs": len(runs),
        "span_ms": (stamps[-1] - stamps[0]) * 1000.0 / frequency,
        "present_gap_ms": stats_of(gaps_ms(stamps, frequency)),
        "main_loop_gap_ms": stats_of(gaps_ms([r["qpc"] for r in main], frequency)),
        "presents_per_frame": stats_of([run[2] for run in runs]),
    })
    steps, irregular = tick_steps([(run[0], run[1]) for run in runs], frequency)
    report["ticks"] = tick_figures(steps, irregular, budget_ms)
    main_steps, main_irregular = tick_steps([(run[0], run[1]) for run in main_runs], frequency)
    report["main_loop_ticks"] = tick_figures(main_steps, main_irregular, budget_ms)
    report["slowest_ticks"] = [{"frame": frame, "ms": ms} for frame, ms in sorted(steps, key=lambda s: (-s[1], s[0]))[:top]]
    return report


def _ms(stats, key):
    return "%8.2f" % stats[key] if stats else "       -"


def format_report(report):
    out = ["capture schema %s, %d record(s)%s%s" % (report["schema"], report["records"],
           "" if report["closed"] else ", not closed (the process was stopped)",
           ", %d torn line(s)" % report["bad_lines"] if report["bad_lines"] else "")]
    if not report["timed"]:
        out.append("no times: %s" % report["reason"])
        return "\n".join(out)
    if report["reason"]:
        out.append("no times: %s" % report["reason"])
        return "\n".join(out)
    out.append("qpc frequency %d per second; frames %d..%d (%d record(s) in range, %d frame run(s)), span %.1f ms" %
               (report["qpc_frequency"], report["first_frame"], report["last_frame"], report["records_in_range"],
                report["frame_runs"], report["span_ms"]))
    out.append("                         n      min   median     mean      p95      max")
    for label, key in (("present gap, all records", "present_gap_ms"), ("present gap, main loop", "main_loop_gap_ms")):
        s = report[key]
        out.append("%-24s %5s %s %s %s %s %s" % (label, s["count"] if s else 0, _ms(s, "min"), _ms(s, "median"), _ms(s, "mean"),
                                                _ms(s, "p95"), _ms(s, "max")))
    for label, key in (("tick, first record", "ticks"), ("tick, main loop", "main_loop_ticks")):
        t = report[key]
        s = t["ms"]
        out.append("%-24s %5s %s %s %s %s %s" % (label, t["steps"], _ms(s, "min"), _ms(s, "median"), _ms(s, "mean"),
                                                _ms(s, "p95"), _ms(s, "max")))
        if s:
            out.append("  over %.0f ms: %d of %d; %.2f ticks per second; irregular steps left out: %d" %
                       (t["budget_ms"], t["over_budget"], t["steps"], t["ticks_per_second"], t["irregular_steps"]))
        elif t["irregular_steps"]:
            out.append("  no step of one frame; irregular steps: %d" % t["irregular_steps"])
    p = report["presents_per_frame"]
    out.append("presents per frame: mean %.2f, max %d" % (p["mean"], p["max"]))
    if report["slowest_ticks"]:
        out.append("slowest ticks: " + ", ".join("frame %d %.1f ms" % (s["frame"], s["ms"]) for s in report["slowest_ticks"]))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("path", help="capture.ndjson (or a manifest directory holding one)")
    ap.add_argument("--frames", nargs=2, type=int, metavar=("FIRST", "LAST"), help="frame numbers, inclusive")
    ap.add_argument("--budget-ms", type=float, default=BUDGET_MS, help="count ticks over this (default 40)")
    ap.add_argument("--top", type=int, default=TOP, help="slowest ticks listed (default 5)")
    ap.add_argument("--json", action="store_true", help="the report as JSON")
    args = ap.parse_args(argv)
    try:
        records, start, closed, bad = load(args.path)
        report = analyse(records, start, closed, bad, tuple(args.frames) if args.frames else None, args.budget_ms, args.top)
    except (CaptureError, OSError, ValueError, KeyError) as e:
        print("dbsim_present_times: %s" % e, file=sys.stderr)
        return 2
    print(json.dumps(report, indent=1) if args.json else format_report(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
