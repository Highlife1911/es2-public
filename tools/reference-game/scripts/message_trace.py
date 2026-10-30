"""The pump count of a replay, accounted for the messages no journal holds (main_loop.md, "the pump accounting").

  summary TRACE [--tick-log LOG] [--cap N]
        One message trace (the proxy's or es2sim's `<tick log>.messages.ndjson`, main_loop.md "format"): per
        frame the pump calls, the journal's injections, the unkeyed retrievals (the SOS voice notice 0xa000,
        MM_MCINOTIFY 0x3b9, the platform message 0x31f), and for every answer inside a modal screen (an
        injection with at least one finished pass before it) the modal passes before it. With --tick-log, each
        such frame's intra-frame clock advances (vt_intra_advances of its tick against the previous one)
        against the cap (step - 1, default 46).
  compare A B [--tick-log-a LOG] [--tick-log-b LOG] [--cap N]
        Two traces of the same journal (the original against itself or against es2sim). The model: the
        injections (frame, p, message, wParam, lParam), the other retrieved messages of each frame (as a
        multiset, without p) and each frame's fixed pump count (the calls minus the unkeyed retrievals after
        the frame's last injection) are the journal's and must be equal. The unkeyed retrievals follow the
        wall time of the run: each one before a frame's last injection costs one modal pass (one empty call,
        one dialog present), each one after it adds one call. The compare reports those per frame as the
        bounded part and fails only on a difference the model does not explain. Frames up to the last frame
        both contain.

Exit 0 = consistent (summary) or accounted (compare), 1 = not, 2 = unusable input. Standard library only.
"""
import argparse
import collections
import json
import sys

SOS_NOTICE = 0xA000
MCI_NOTIFY = 0x3B9
PLATFORM = 0x31F  # WM_DWMNCRENDERINGCHANGED, sent to a real top-level window only
UNKEYED = {SOS_NOTICE: "sos_notice", MCI_NOTIFY: "mci_notify", PLATFORM: "platform"}
DEFAULT_CAP = 46
FRAMES_LISTED = 200


def read_trace(path):
    """{"frames": {f: {...}}, "source", "complete", "problems"}; every frame record checked against its records."""
    start, end = None, None
    records = collections.defaultdict(lambda: {"inject": [], "msg": []})
    frame_records = {}
    problems = []
    with open(path, "r", encoding="utf-8-sig") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            if o.get("event") == "start":
                if o.get("kind") != "message_trace" or o.get("schema") != 1:
                    raise ValueError("%s: not a message trace of schema 1" % path)
                start = o
                continue
            if o.get("event") == "end":
                end = o
                continue
            if start is None:
                raise ValueError("%s: record before the start record (line %d)" % (path, number))
            kind = o.get("k")
            if kind in ("inject", "msg"):
                records[o["f"]][kind].append((o["p"], o["m"], o["w"], o["l"]))
            elif kind == "frame":
                if o["f"] in frame_records:
                    problems.append({"frame": o["f"], "problem": "two frame records"})
                frame_records[o["f"]] = o
    if start is None:
        raise ValueError("%s: no start record" % path)
    frames = {}
    for f in sorted(set(frame_records) | set(records)):
        r = frame_records.get(f)
        injects = sorted(records[f]["inject"])
        msgs = sorted(records[f]["msg"])
        if r is None:
            problems.append({"frame": f, "problem": "messages without a frame record"})
            continue
        if r["pumps"] != r["empty"] + r["msgs"] + r["inject"]:
            problems.append({"frame": f, "problem": "pumps != empty + msgs + inject"})
        if r["msgs"] != len(msgs) or r["inject"] != len(injects):
            problems.append({"frame": f, "problem": "frame record counts differ from its records"})
        last = injects[-1][0] if injects else -1
        unkeyed = [m for m in msgs if m[1] in UNKEYED]
        frames[f] = {
            "pumps": r["pumps"], "empty": r["empty"], "injects": injects,
            "others": sorted(m[1:] for m in msgs if m[1] not in UNKEYED),
            "unkeyed": collections.Counter(UNKEYED[m[1]] for m in unkeyed),
            "before_last": sum(1 for m in unkeyed if m[0] < last),
            "after_last": sum(1 for m in unkeyed if m[0] > last),
            "passes": passes_before_keyed(injects, msgs),
        }
        frames[f]["fixed"] = r["pumps"] - frames[f]["after_last"]
    return {"frames": frames, "source": start.get("source", "proxy"), "complete": end is not None,
            "problems": problems}


def passes_before_keyed(injects, msgs):
    """[(p, empty calls before it)] for the injections after the first call of their frame. An answer inside a
    modal screen has at least one empty call (one finished pass) before it; one in the frame's first pump call has
    none."""
    taken = sorted([i[0] for i in injects] + [m[0] for m in msgs])
    out = []
    for inject in injects:
        if inject[0] > 0:
            out.append((inject[0], inject[0] - sum(1 for p in taken if p < inject[0])))
    return out


def read_clock_advances(path):
    """{frame: vt_intra_advances of its tick minus the previous tick's} from a raw tick log."""
    cumulative = {}
    with open(path, "r", encoding="utf-8-sig") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            if o.get("event") == "tick" and "vt_intra_advances" in o:
                cumulative[o["frame_counter_before_increment"]] = o["vt_intra_advances"]
    return {f: v - cumulative[f - 1] for f, v in cumulative.items() if f - 1 in cumulative}


def keyed_report(trace, advances, cap):
    """The keyed frames: unkeyed retrievals, the fewest passes before an answer, the clock against the cap."""
    rows, below = [], []
    for f, r in sorted(trace["frames"].items()):
        waited = [n for _, n in r["passes"] if n > 0]
        if not waited:
            continue
        row = {"frame": f, "pumps": r["pumps"], "answers": len(waited), "fewest_passes": min(waited),
               "unkeyed_before_last": r["before_last"], "unkeyed_after_last": r["after_last"]}
        if advances is not None:
            # None: no tick follows the frame (the end dialog before the exit), so nothing compared reads it.
            row["clock_advances"] = advances.get(f)
            if f in advances and advances[f] != cap:
                below.append(f)
        rows.append(row)
    return rows, below


def totals(trace):
    count = collections.Counter()
    for r in trace["frames"].values():
        count.update(r["unkeyed"])
    return dict(sorted(count.items()))


def summary(path, tick_log=None, cap=DEFAULT_CAP):
    trace = read_trace(path)
    advances = read_clock_advances(tick_log) if tick_log else None
    rows, below = keyed_report(trace, advances, cap)
    return {"trace": path, "source": trace["source"], "complete": trace["complete"],
            "frames": len(trace["frames"]), "unkeyed": totals(trace), "keyed_frames": rows,
            "clock_below_cap": below if advances is not None else None, "problems": trace["problems"],
            "consistent": not trace["problems"] and not below}


def compare(path_a, path_b, tick_a=None, tick_b=None, cap=DEFAULT_CAP):
    a, b = read_trace(path_a), read_trace(path_b)
    last = min(max(a["frames"], default=-1), max(b["frames"], default=-1))
    unexplained = collections.defaultdict(list)
    bounded = []

    def note(kind, entry):
        if len(unexplained[kind]) < FRAMES_LISTED:
            unexplained[kind].append(entry)

    for f in sorted(set(a["frames"]) | set(b["frames"])):
        if f > last:
            continue
        ra, rb = a["frames"].get(f), b["frames"].get(f)
        if ra is None or rb is None:
            note("frame", {"frame": f, "only_in": "a" if rb is None else "b"})
            continue
        if ra["injects"] != rb["injects"]:
            note("injects", {"frame": f, "a": len(ra["injects"]), "b": len(rb["injects"])})
            continue
        if ra["others"] != rb["others"]:
            note("messages", {"frame": f, "a": ra["others"], "b": rb["others"]})
        if ra["fixed"] != rb["fixed"]:
            note("fixed_pumps", {"frame": f, "a": ra["fixed"], "b": rb["fixed"]})
        if ra["unkeyed"] != rb["unkeyed"] or ra["before_last"] != rb["before_last"]:
            entry = {"frame": f, "a": dict(ra["unkeyed"]), "b": dict(rb["unkeyed"]),
                     "passes_a_minus_b": rb["before_last"] - ra["before_last"],
                     "empty_a_minus_b": ra["empty"] - rb["empty"]}
            if ra["passes"]:
                entry["answer_shift"] = [pa[1] - pb[1] for pa, pb in zip(ra["passes"], rb["passes"])]
            bounded.append(entry)
            if entry["empty_a_minus_b"] != entry["passes_a_minus_b"]:
                note("empty", {"frame": f, "empty_a_minus_b": entry["empty_a_minus_b"],
                               "passes_a_minus_b": entry["passes_a_minus_b"]})
    keyed = {}
    for label, trace, tick in (("a", a, tick_a), ("b", b, tick_b)):
        advances = read_clock_advances(tick) if tick else None
        rows, below = keyed_report(trace, advances, cap)
        keyed[label] = {"rows": [r for r in rows if r["frame"] <= last], "clock_below_cap":
                        [f for f in below if f <= last] if advances is not None else None}
        if trace["problems"]:
            note("structure_" + label, trace["problems"][:FRAMES_LISTED])
        if advances is not None:
            for f in keyed[label]["clock_below_cap"]:
                note("clock_" + label, {"frame": f, "advances": advances.get(f), "cap": cap})
    return {"a": path_a, "b": path_b, "sources": [a["source"], b["source"]], "compared_through_frame": last,
            "unkeyed_totals": [totals(a), totals(b)], "bounded": bounded[:FRAMES_LISTED],
            "bounded_frames": len(bounded), "keyed": keyed, "unexplained": dict(unexplained),
            "accounted": not unexplained}


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("summary"); s.add_argument("trace"); s.add_argument("--tick-log")
    s.add_argument("--cap", type=int, default=DEFAULT_CAP)
    c = sub.add_parser("compare"); c.add_argument("a"); c.add_argument("b")
    c.add_argument("--tick-log-a"); c.add_argument("--tick-log-b"); c.add_argument("--cap", type=int, default=DEFAULT_CAP)
    args = parser.parse_args(argv)
    try:
        if args.command == "summary":
            result = summary(args.trace, args.tick_log, args.cap)
            ok = result["consistent"]
        else:
            result = compare(args.a, args.b, args.tick_log_a, args.tick_log_b, args.cap)
            ok = result["accounted"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("message_trace: %s" % error, file=sys.stderr)
        return 2
    print(json.dumps(result, indent=1))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
