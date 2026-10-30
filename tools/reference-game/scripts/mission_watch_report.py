# Summarizes mission-state watches in a tick log (proxy/mem_watch.h) written
# with the watch set of es2-data/logs/c1_01-20261006: player track,
# route progress, order/completion bytes, objective status and variable-bank
# changes, keyed by frame counter.
#
# Usage: mission_watch_report.py LOG.jsonl [--json OUT.json] [--track N]
import argparse
import json
import struct
import sys

GLOBALS = 0x4A9E20


def i16(data, offset):
    return struct.unpack_from("<h", data, offset)[0]


def i32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def octagonal(dx, dy):
    # DBSIM 0x4928d0 approximation, docs/formats/mission.md, "Confirmed (spatial record)".
    a, b = abs(dx), abs(dy)
    return max(a, b) + min(a, b) // 2


def apply_watch(state, event):
    name = event["name"]
    if event.get("unreadable"):
        state[name] = None
        return
    if "hex" in event:
        state[name] = bytearray.fromhex(event["hex"])
        return
    data = state.get(name)
    if data is None:
        raise ValueError(f"diff before full record for {name} at sequence {event['sequence']}")
    for offset, text in event["diff"]:
        chunk = bytes.fromhex(text)
        data[offset:offset + len(chunk)] = chunk


def decode_globals(data):
    return {
        "trigger_count": i16(data, 0x4A9E24 - GLOBALS),
        "objective_count": i16(data, 0x4A9E3C - GLOBALS),
        "forced_status": i16(data, 0x4A9E4C - GLOBALS),
        "word_4a9e54": i16(data, 0x4A9E54 - GLOBALS),
        "result_pairs": i16(data, 0x4A9E6A - GLOBALS),
        # Periodic outcome policy state of 0x4131ac, raw, without the countdown
        # dword of timer object 0x4a9e62 (reported separately as re-arm frames).
        "raw_4a9e60_62": data[0x4A9E60 - GLOBALS:0x4A9E63 - GLOBALS].hex(),
        "raw_4a9e67_69": data[0x4A9E67 - GLOBALS:0x4A9E6A - GLOBALS].hex(),
    }


def decode_group(data):
    return {
        "route_index": i16(data, 4),
        "actor_count": i16(data, 0x10),
        "order_index": i32(data, 0x6C),
        "completion": list(data[0x70:0x7A]),
    }


def decode_objectives(data):
    result = []
    for base in range(0, len(data) - 75, 76):
        result.append({"required": i16(data, base), "type": i16(data, base + 2), "scope": i16(data, base + 4),
                       "latch_34": data[base + 34]})
    return result


def decode_triggers(data):
    return [{"type": i16(data, base), "latch_10": i16(data, base + 10)} for base in range(0, len(data) - 57, 58)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--json")
    parser.add_argument("--track", type=int, default=50, help="print the player track every N frames")
    args = parser.parse_args()

    state, points = {}, {}
    timeline, track, rearms = [], [], []
    previous = {}
    last_frame = None
    with open(args.log, encoding="utf-8") as stream:
        for line in stream:
            event = json.loads(line)
            kind = event.get("event")
            if kind == "tick":
                last_frame = event["frame_counter_before_increment"]
                if "position_raw_i32" in event:
                    track.append((last_frame, event["position_raw_i32"], event["angles_raw_u16"][2],
                                  event["speed_raw_i16"], event["throttle_raw_q10"]))
                continue
            if kind != "watch":
                continue
            apply_watch(state, event)
            frame = event["frame"]
            name = event["name"]
            data = state[name]
            if name.startswith("route_pt") and data is not None:
                points[int(name[8:])] = (i32(data, 0), i32(data, 4), i32(data, 8))
                continue
            decoded = None
            if data is None:
                decoded = "unreadable"
            elif name == "mission_globals":
                decoded = decode_globals(data)
                countdown = i32(data, 0x4A9E63 - GLOBALS)
                if countdown > previous.get("countdown", countdown):
                    rearms.append(frame)
                previous["countdown"] = countdown
            elif name == "player_group":
                decoded = decode_group(data)
            elif name == "objectives":
                decoded = decode_objectives(data)
            elif name == "triggers":
                decoded = decode_triggers(data)
            elif name == "player_flags":
                decoded = {f"+0x{0x99 + i:x}": b for i, b in enumerate(data) if b}
            elif name == "route":
                decoded = {"count": i16(data, 0)}
            elif name == "mission_vars":
                words = struct.unpack_from("<1000h", data)
                old = previous.get(name)
                if old is None:
                    decoded = {"nonzero": {i: w for i, w in enumerate(words) if w}}
                else:
                    decoded = {"changed": {i: [old[i], w] for i, w in enumerate(words) if w != old[i]}}
                previous[name] = words
            if decoded is not None and decoded != previous.get(name + "#decoded"):
                timeline.append({"frame": frame, "watch": name, "value": decoded})
                previous[name + "#decoded"] = decoded

    route = [points[i] for i in sorted(points)]
    print(f"frames logged up to {last_frame}; route points: {len(route)}")
    gaps = sorted({b - a for a, b in zip(rearms, rearms[1:])})
    print(f"periodic countdown 0x4a9e63 re-armed {len(rearms)} times; first {rearms[:3]}; intervals {gaps[:6]}")
    for entry in timeline:
        print(f"  frame {entry['frame']:6d} {entry['watch']:16s} {json.dumps(entry['value'])}")
    if track and route:
        print("player track (frame, position, heading, speed, throttle, octagonal distance to each route point):")
        for frame, position, heading, speed, throttle in track[::max(1, args.track)] + [track[-1]]:
            distances = [octagonal(position[0] - p[0], position[1] - p[1]) for p in route]
            print(f"  {frame:6d} {position} h={heading:5d} v={speed:4d} t={throttle:5d} d={distances}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"route_points": route, "timeline": timeline, "rearm_frames": rearms, "last_frame": last_frame}, out, indent=1)


if __name__ == "__main__":
    sys.exit(main())
