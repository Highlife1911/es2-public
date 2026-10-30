# Oracle regression: normalizes a tick log (docs/reference-game/tick_log.md,
# mission watches of es2-data/logs/c1_01-20261006) into a form that
# is independent of heap addresses and real-time noise, and compares a run
# with a recorded golden reference. See docs/reference-game/oracle_regression.md.
#
#   oracle_regression.py normalize LOG.jsonl OUT.json [--outputs DIR] [--frame-limit N]
#   oracle_regression.py compare GOLDEN.json RUN.json [--report OUT.json]
#   oracle_regression.py normalize-compare LOG.jsonl OUT.json --golden GOLDEN.json [--report OUT.json]
#
# The proxy's final `exit_counters` record (docs/reference-game/tick_log.md,
# "Exit-time hook counters") is never compared; its hk_* counters (and the
# rtl_* counters of the CW3220 import shim, rtl_imports.md) are copied into
# the normalized run as `exit_counters` (and `exit_record`).
import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mission_watch_report import (apply_watch, decode_globals, decode_group,  # noqa: E402
                                  decode_objectives, decode_triggers, i16)

# Deterministic per-tick fields. Excluded on purpose: sequence, qpc and ebx
# (real time / constant), player_actor (heap address), in_dropped and
# in_focus_dropped (count real desktop input that a replay discards).
TICK_FIELDS = ("completed_clock_ms", "limiter_bypassed", "loop_pause_flag", "clock_raw_hex", "rng_a_hex",
               "rng_b_hex", "position_raw_i32", "angles_raw_u16", "speed_raw_i16", "throttle_raw_q10",
               "vt_limiter_polls", "vt_stall_advances", "in_injected", "in_fallbacks")


def normalize_watch(name, data):
    """Value of a watch without heap pointers."""
    if data is None:
        return "unreadable"
    if name == "mission_globals":
        return decode_globals(data)
    if name == "player_group":
        return decode_group(data)
    if name == "objectives":
        return decode_objectives(data)
    if name == "triggers":
        return decode_triggers(data)
    if name == "route":
        return {"count": i16(data, 0)}
    if name.endswith("_target"):
        return "set" if any(data) else "none"
    return data.hex()


def normalize(log_path, outputs_dir=None, frame_limit=None):
    """frame_limit: keep only ticks and watch changes with frame < limit. Runs
    that do not end by themselves are stopped by polling, so their length
    varies; the comparison window must be fixed."""
    ticks, watches, state, last = [], [], {}, {}
    header = exit_event = None
    with open(log_path, encoding="utf-8") as stream:
        for line in stream:
            event = json.loads(line)
            kind = event.get("event")
            if kind == "start":
                header = {k: event.get(k) for k in ("schema", "record_limit", "actor_raw", "watches")}
            elif kind == "tick":
                if frame_limit is not None and event["frame_counter_before_increment"] >= frame_limit:
                    continue
                ticks.append([event["frame_counter_before_increment"]] + [event.get(f) for f in TICK_FIELDS])
            elif kind == "watch":
                apply_watch(state, event)
                if frame_limit is not None and event["frame"] >= frame_limit:
                    continue
                value = normalize_watch(event["name"], state[event["name"]])
                if last.get(event["name"]) != value:
                    watches.append([event["frame"], event["name"], value])
                    last[event["name"]] = value
            elif kind == "exit_counters":
                # Process-exit snapshot, independent of frame_limit; the proxy
                # writes at most one, the last one would win.
                exit_event = event
    outputs = {}
    if outputs_dir and os.path.isdir(outputs_dir):
        for name in sorted(os.listdir(outputs_dir)):
            with open(os.path.join(outputs_dir, name), "rb") as stream:
                outputs[name] = hashlib.sha256(stream.read()).hexdigest()
    result = {"format": 1, "frame_limit": frame_limit, "tick_fields": ["frame"] + list(TICK_FIELDS), "header": header,
              "tick_count": len(ticks), "last_frame": ticks[-1][0] if ticks else None,
              "ticks": ticks, "watches": watches, "outputs": outputs}
    # Only present when the log has the record: older logs and runs without
    # [hooks] keep their exact previous normalized bytes. compare() never
    # reads these keys.
    if exit_event is not None:
        result["exit_record"] = {k: exit_event.get(k) for k in ("reason", "last_sequence", "frame_counter")}
        result["exit_counters"] = {k: v for k, v in exit_event.items() if k.startswith(("hk_", "rtl_"))}
    return result


def compare(golden, run, limit=20):
    problems = []
    if golden.get("frame_limit") != run.get("frame_limit"):
        problems.append({"kind": "frame_limit", "golden": golden.get("frame_limit"), "run": run.get("frame_limit")})
    if golden["tick_fields"] != run["tick_fields"]:
        problems.append({"kind": "tick_fields", "golden": golden["tick_fields"], "run": run["tick_fields"]})
    gt = {t[0]: t for t in golden["ticks"]}
    rt = {t[0]: t for t in run["ticks"]}
    common = sorted(set(gt) & set(rt))
    if set(gt) != set(rt):
        problems.append({"kind": "tick_frames", "golden_only": sorted(set(gt) - set(rt))[:limit],
                         "run_only": sorted(set(rt) - set(gt))[:limit],
                         "golden_count": len(gt), "run_count": len(rt)})
    fields = golden["tick_fields"]
    field_diffs = {}
    for frame in common:
        for i, name in enumerate(fields[1:], start=1):
            if gt[frame][i] != rt[frame][i]:
                entry = field_diffs.setdefault(name, {"count": 0, "first_frame": frame,
                                                      "golden": gt[frame][i], "run": rt[frame][i]})
                entry["count"] += 1
    for name, entry in field_diffs.items():
        problems.append(dict({"kind": "tick_field", "field": name}, **entry))
    gw = [tuple(map(json.dumps, w)) for w in golden["watches"]]
    rw = [tuple(map(json.dumps, w)) for w in run["watches"]]
    if gw != rw:
        first = next((i for i, (a, b) in enumerate(zip(gw, rw)) if a != b), min(len(gw), len(rw)))
        problems.append({"kind": "watches", "golden_count": len(gw), "run_count": len(rw), "first_index": first,
                         "golden": golden["watches"][first] if first < len(gw) else None,
                         "run": run["watches"][first] if first < len(rw) else None})
    if golden["outputs"] != run["outputs"]:
        problems.append({"kind": "outputs", "golden": golden["outputs"], "run": run["outputs"]})
    return {"identical": not problems, "common_ticks": len(common), "golden_ticks": len(gt), "run_ticks": len(rt),
            "golden_watch_changes": len(gw), "run_watch_changes": len(rw), "problems": problems}


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    n = sub.add_parser("normalize")
    n.add_argument("log")
    n.add_argument("out")
    n.add_argument("--outputs")
    n.add_argument("--frame-limit", type=int)
    c = sub.add_parser("compare")
    c.add_argument("golden")
    c.add_argument("run")
    c.add_argument("--report")
    nc = sub.add_parser("normalize-compare")
    nc.add_argument("log")
    nc.add_argument("out")
    nc.add_argument("--outputs")
    nc.add_argument("--frame-limit", type=int)
    nc.add_argument("--golden", required=True)
    nc.add_argument("--report")
    args = parser.parse_args()
    if args.command in ("normalize", "normalize-compare"):
        result = normalize(args.log, args.outputs, args.frame_limit)
        with open(args.out, "w", encoding="utf-8", newline="\n") as out:
            # dumps uses the standard library's C encoder; one write preserves
            # the exact previous compact JSON bytes without per-token writes.
            out.write(json.dumps(result, separators=(",", ":")))
        summary = {"tick_count": result["tick_count"], "last_frame": result["last_frame"],
                   "watch_changes": len(result["watches"]), "outputs": result["outputs"]}
        if "exit_counters" in result:
            summary["exit_counter_fields"] = len(result["exit_counters"])
        print(json.dumps(summary))
        if args.command == "normalize":
            return 0
        run = result
    with open(args.golden, encoding="utf-8") as stream:
        golden = json.load(stream)
    if args.command == "compare":
        with open(args.run, encoding="utf-8") as stream:
            run = json.load(stream)
    report = compare(golden, run)
    if args.report:
        with open(args.report, "w", encoding="utf-8", newline="\n") as out:
            json.dump(report, out, indent=1)
    print(json.dumps({k: v for k, v in report.items() if k != "problems"}))
    for problem in report["problems"][:10]:
        print(json.dumps(problem)[:400])
    return 0 if report["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
