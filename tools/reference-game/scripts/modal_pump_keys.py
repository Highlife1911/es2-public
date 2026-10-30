"""Derive the replay journal of a manual recording for modal screens.

Record mode stores no pump positions (input_journal.md). In a modal screen the
frame counter stands still while the screen's own loop keeps peeking messages,
so a replay that injects the answer at the start of that frame delivers it
before the dialog exists and the dialog waits forever. Messages of modal frames
therefore get pump keys `p` in the derived journal: they are injected only
after that many PM_REMOVE peeks in their frame, in recorded order.

Modal frames are frames that took at least --min-ms of wall time in the
recording (qpc distance to the previous tick) and the frame after the last
logged tick (the end dialog before DBSIM exits). The recording journal is never
changed; the verification replays both runs from the derived journal and still
compares them with the recording.

A screen the player opens with a key (--opener-vk, e.g. 0x51 Q for the
EINSATZALARM dialog) is opened by the ordinary pump: its key-down and the
messages before it must arrive at the start of the frame like in any other
frame. In a frame with such a key-down, keying therefore starts after the
first one, and the frame counts as modal whatever its wall time (the screen
closes before the frame counter moves on, so its answers share the frame).
"""
import argparse
import hashlib
import json
from pathlib import Path

PUMPED_APIS = {"msg", "keydown", "keyup", "mousedown", "mouseup", "mousemove"}
KEY_DOWN_MESSAGES = {0x100, 0x104}  # WM_KEYDOWN, WM_SYSKEYDOWN


def read_jsonl(path):
    return [json.loads(line) for line in Path(path).read_text(encoding="utf-8-sig").splitlines() if line.strip()]


def modal_frames(record_log, min_ms):
    frequency, qpc, last = None, {}, None
    for event in read_jsonl(record_log):
        if event.get("event") == "start":
            frequency = event.get("qpc_frequency")
        elif event.get("event") in ("tick", "tick_compact"):
            frame = event["frame_counter_before_increment"]
            last = frame if last is None else max(last, frame)
            if "qpc" in event:
                qpc[frame] = event["qpc"]
    frames = set()
    if frequency:
        for frame in sorted(qpc):
            if frame - 1 in qpc and (qpc[frame] - qpc[frame - 1]) * 1000 >= min_ms * frequency:
                frames.add(frame)
    if last is not None:
        frames.add(last + 1)
    return frames


def opener_key(event, opener_vks):
    if event.get("api") == "msg":
        return event.get("a") in KEY_DOWN_MESSAGES and event.get("b") in opener_vks
    return event.get("api") == "keydown" and event.get("vk") in opener_vks


def opener_frames(events, opener_vks):
    return {event.get("f") for event in events if opener_key(event, opener_vks)}


def derive(journal, frames, start, step, opener_vks=frozenset()):
    events = read_jsonl(journal)
    waiting = opener_frames(events, opener_vks)   # frames whose opener has not been seen yet
    frames = set(frames) | waiting
    keyed, result = {}, []
    for event in events:
        frame = event.get("f")
        if frame in waiting:
            if opener_key(event, opener_vks):
                waiting.discard(frame)
        elif frame in frames and event.get("api") in PUMPED_APIS and "p" not in event:
            index = keyed.get(frame, 0)
            event = dict(event, p=start + step * index)
            keyed[frame] = index + 1
        result.append(event)
    return result, keyed


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record_log")
    parser.add_argument("journal")
    parser.add_argument("--output", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--min-ms", type=int, default=500)
    parser.add_argument("--start", type=int, default=300)
    parser.add_argument("--step", type=int, default=10)
    parser.add_argument("--opener-vk", type=lambda text: int(text, 0), action="append", default=[],
                        help="virtual-key code that opens a modal screen (repeatable; 0x51 or 81)")
    args = parser.parse_args()
    if Path(args.output).exists() or Path(args.report).exists():
        raise SystemExit("output or report already exists")
    if args.min_ms < 1 or args.start < 1 or args.step < 1:
        raise SystemExit("min-ms, start and step must be positive")
    if any(not 0 < vk < 256 for vk in args.opener_vk):
        raise SystemExit("opener-vk must be a virtual-key code 1..255")
    opener_vks = frozenset(args.opener_vk)
    openers = opener_frames(read_jsonl(args.journal), opener_vks)
    frames = modal_frames(args.record_log, args.min_ms) | openers
    events, keyed = derive(args.journal, frames, args.start, args.step, opener_vks)
    Path(args.output).write_bytes("".join(json.dumps(e, separators=(",", ":")) + "\n" for e in events).encode())
    report = {"schema": 1, "min_ms": args.min_ms, "start": args.start, "step": args.step,
              "opener_vks": sorted(opener_vks), "opener_frames": sorted(openers),
              "modal_frames": sorted(frames), "pump_keyed": {str(k): v for k, v in sorted(keyed.items())},
              "record_sha256": sha256(args.record_log), "journal_sha256": sha256(args.journal),
              "replay_journal_sha256": sha256(args.output)}
    Path(args.report).write_bytes((json.dumps(report, indent=2) + "\n").encode())
    print(json.dumps({k: report[k] for k in ("modal_frames", "pump_keyed")}))


if __name__ == "__main__":
    main()
