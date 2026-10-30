"""Strict record/replay acceptance, separate from legacy oracle normalization.

Reuse the project's pointer normalization. Replay/replay compares every legacy
field. Record/replay omits ONLY in_injected and in_fallbacks from those fields.
The legacy exclusions (QPC, pointers, desktop traffic, hook diagnostics and
vt_late_polls) still apply. No common-prefix or tolerance comparison is allowed.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle_regression as oracle

RECORD_ONLY_EXCLUSIONS = {"in_injected", "in_fallbacks"}


def validate_journal(path):
    count = 0
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        event = json.loads(line)
        api = event.get("api")
        if api is None:
            continue
        if type(event.get("f")) is not int or event["f"] < 0:
            raise ValueError("input entry has no nonnegative frame")
        if api not in {"msg", "key", "joypos", "joycaps", "cursor", "keydown", "keyup", "mousedown", "mouseup", "mousemove"}:
            raise ValueError(f"unsupported recording API: {api}")
        # Keyboard and mouse messages (WM_MOUSEFIRST..WM_MOUSEHWHEEL). Mouse
        # replay was verified on the c3_09-loadout-b recording.
        message = event.get("a")
        if api == "msg" and message not in (0x100, 0x101, 0x104, 0x105) and not (isinstance(message, int) and 0x200 <= message <= 0x20e):
            raise ValueError("non-keyboard, non-mouse message in recording")
        if api in ("joypos", "joycaps") and event.get("b") == 0:
            raise ValueError("successful joystick input is not verified for these suites")
        count += 1
    if not count:
        raise ValueError("journal contains no input entries")


def read_run(log, outputs=None, frame_limit=None):
    if frame_limit is not None and frame_limit <= 0:
        raise ValueError("frame_limit must be positive")
    events = [json.loads(line) for line in Path(log).read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    starts = [e for e in events if e.get("event") == "start"]
    if len(starts) != 1 or not events or events[0] != starts[0] or starts[0].get("schema") != 2:
        raise ValueError("exactly one leading schema-2 start required")
    if any(k not in starts[0] for k in ("record_limit", "actor_raw", "watches")) or not 0 <= starts[0]["watches"] <= 48:
        raise ValueError("missing/invalid start configuration")
    if starts[0].get("actor_raw"):
        raise ValueError("actor_raw contains pointers; use reviewed named watches")
    frames = []
    state = {}
    seen, readable = set(), set()
    for event in events[1:]:
        kind = event.get("event")
        if kind == "tick":
            frame = event["frame_counter_before_increment"]
            if type(frame) is not int or frame != len(frames):
                raise ValueError("tick frames must be contiguous, unique and start at zero")
            if any(f not in event or event[f] is None for f in oracle.TICK_FIELDS):
                raise ValueError(f"missing deterministic tick field at {frame}")
            frames.append(frame)
        elif kind == "watch":
            if not frames or event.get("frame") != frames[-1]:
                raise ValueError("watch must follow its tick")
            name, length = event["name"], event.get("length")
            seen.add(name)
            if event.get("unreadable"):
                state[name] = None
                continue
            if type(length) is not int or not 0 <= length <= 16384:
                raise ValueError("invalid watch length")
            if "hex" in event:
                data = bytes.fromhex(event["hex"])
                if len(data) != length:
                    raise ValueError("watch byte length mismatch")
                state[name] = bytearray(data)
            elif "diff" in event:
                data = state.get(name)
                if data is None or len(data) != length:
                    raise ValueError("watch diff without same-length full value")
                end = 0
                for offset, value in event["diff"]:
                    chunk = bytes.fromhex(value)
                    if type(offset) is not int or offset < end or offset + len(chunk) > length or not chunk:
                        raise ValueError("out-of-bounds/overlapping watch diff")
                    data[offset:offset+len(chunk)] = chunk
                    end = offset + len(chunk)
            else:
                raise ValueError("watch has no value")
            if frame_limit is None or event["frame"] < frame_limit:
                readable.add(name)
        elif kind == "limit_reached":
            raise ValueError("proxy record limit reached; incomplete capture")
        else:
            raise ValueError(f"unknown log event: {kind}")
    if not frames or (frame_limit and len(frames) < frame_limit):
        raise ValueError("empty or shorter-than-required log")
    if len(seen) != starts[0].get("watches") or seen != readable:
        raise ValueError("missing or permanently unreadable watch; review the watch set")
    result = oracle.normalize(str(log), str(outputs) if outputs else None, frame_limit)
    return result


def strip_input_counts(run):
    result = copy.deepcopy(run)
    indices = [i for i, f in enumerate(result["tick_fields"]) if f not in RECORD_ONLY_EXCLUSIONS]
    result["tick_fields"] = [result["tick_fields"][i] for i in indices]
    result["ticks"] = [[row[i] for i in indices] for row in result["ticks"]]
    return result


def compare_three(record, first, second):
    if not (record["header"] == first["header"] == second["header"]):
        raise ValueError("start configuration differs")
    reports = {
        "replay1_vs_replay2": oracle.compare(first, second),
        "record_vs_replay1": oracle.compare(strip_input_counts(record), strip_input_counts(first)),
        "record_vs_replay2": oracle.compare(strip_input_counts(record), strip_input_counts(second)),
    }
    return {"schema": 1, "identical": all(r["identical"] for r in reports.values()),
            "record_replay_excluded_fields": sorted(RECORD_ONLY_EXCLUSIONS), "comparisons": reports}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("record"); p.add_argument("replay1"); p.add_argument("replay2")
    p.add_argument("--record-outputs"); p.add_argument("--replay1-outputs"); p.add_argument("--replay2-outputs")
    p.add_argument("--frame-limit", type=int); p.add_argument("--report", required=True)
    p.add_argument("--journal")
    a = p.parse_args()
    try:
        if a.journal:
            validate_journal(a.journal)
        result = compare_three(*[read_run(log, out, a.frame_limit) for log, out in (
            (a.record, a.record_outputs), (a.replay1, a.replay1_outputs), (a.replay2, a.replay2_outputs))])
    except (ValueError, KeyError, OSError, IndexError, struct.error) as error:
        result = {"schema": 1, "identical": False, "error": str(error)}
    Path(a.report).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
