"""Propose mission watches from an immutable launch and a discovery tick log.

--bootstrap creates discovery-only pointer/side watches. Never use these raw
pointer watches as golden watches. Without --bootstrap, runtime membership is
required; no C1_01 actor indices or heap addresses are reused.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mission_watch_report import apply_watch

FILES = ("script.dat", "player.mec", "mission.var", "MISSION.STR")


def launch_files(directory):
    directory = Path(directory)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
    entries = manifest.get("files", [])
    if manifest.get("schema") != 1 or len(entries) != 4 or {e["file"] for e in entries} != set(FILES):
        raise ValueError("invalid four-file launch manifest")
    data = {}
    for entry in entries:
        content = (directory / entry["file"]).read_bytes()
        if len(content) != entry["size"] or hashlib.sha256(content).hexdigest() != entry["sha256"].lower():
            raise ValueError(f"launch hash mismatch: {entry['file']}")
        data[entry["file"]] = content
    return data


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, n):
        if n < 0 or n > len(self.data) - self.at:
            raise ValueError(f"truncated launch at {self.at}")
        result = self.data[self.at:self.at+n]
        self.at += n
        return result

    def count(self):
        n, = struct.unpack("<h", self.take(2))
        if n < 0:
            raise ValueError("negative launch count")
        return n

    def end(self):
        if self.at != len(self.data):
            raise ValueError("unexpected launch trailer")


def mec_records(data):
    r = Reader(data)
    r.take(2)
    records = []
    for _ in range(r.count()):
        r.take(4)
        vehicle = r.count()
        count = r.count()
        items = list(struct.unpack(f"<{count}h", r.take(2*count)))
        r.take(2*count + 68)
        records.append({"vehicle": vehicle, "K": count, "items": items})
    if r.count() != 33:
        raise ValueError("MEC flag count is not 33")
    r.take(33); r.end()
    return records


def script_counts(data):
    r = Reader(data)
    r.take(20)
    counts = []
    # Prepared script, not authored MSN. mission.md lists all 13 strides.
    for stride in (12, 2, None, 6, 74, 24, 134, 92, 52, 14, 156, 54, 2):
        count = r.count(); counts.append(count)
        if stride is None:
            for _ in range(count):
                r.take(2*r.count())
        else:
            r.take(count*stride)
    r.end()
    return counts


def base_watches(counts):
    result = ["mission_globals 0x4a9e20 80", "mission_vars 0x4a9e70 2000",
              "player_flags 0x4d24e6@0x99 13", "player_group 0x4d24e6@0x45@0 122"]
    if counts[2]:
        result.append("route 0x4d24e6@0x45@6@0 6")
    if counts[11]:
        result.append("objectives 0x4a9e40@0 w0x4a9e3c*76")
    if counts[4]:
        result.append("triggers 0x4a9e28@0 w0x4a9e24*58")
    return result


def indices(text):
    values = [int(x) for x in text.split(",")] if text else []
    if len(set(values)) != len(values) or any(v < 0 for v in values):
        raise ValueError("indices must be unique and nonnegative")
    return values


def propose(directory, log=None, bootstrap=False, actor_indices=None, hostile_indices=None):
    data = launch_files(directory)
    records = mec_records(data["player.mec"])
    if not records:
        raise ValueError("no player/team records")
    counts = script_counts(data["script.dat"])
    lines = base_watches(counts)
    report = {"launch": str(directory), "bootstrap": bootstrap, "loadouts": records,
              "review_required": True, "hypotheses": ["different side denotes hostile", "actions close the intended coverage gaps"]}
    if bootstrap:
        # Static reader adds MEC actors and selects members from all three
        # actor classes. This is an upper bound, not the compacted runtime count.
        pool_count = sum(counts[6:9]) + len(records)
        selected = list(range(min(pool_count, 48-len(lines)-2))) if actor_indices is None else actor_indices
        if any(i >= pool_count for i in selected):
            raise ValueError("bootstrap actor index exceeds prepared actor counts plus MEC records")
        lines += ["actor_index 0x4a9af8@0 w0x4a9afe*4",
                  f"team_index 0x4d24e6@0x45@0xc@0 {4*len(records)}"]
        lines += [f"actor{i}_side 0x4a9af8@{4*i}@0x45@0x12 1" for i in selected]
        report["actor_count_upper_bound"] = pool_count
        report["hypotheses"] += ["runtime pool respects the prepared actor/MEC upper bound", "MEC count equals runtime group count"]
        report["unobserved_actor_indices"] = sorted(set(range(pool_count)) - set(selected))
        report["warning"] = "Discovery only: actor_index/team_index contain heap pointers. Record final watches in a new run."
    else:
        if not log:
            raise ValueError("a discovery log is required")
        state, observations = {}, {}
        player = None
        for line in Path(log).read_text(encoding="utf-8-sig").splitlines():
            event = json.loads(line)
            if event.get("event") == "tick" and player is None and event.get("player_actor"):
                p = event["player_actor"]
                player = int(p, 0) if isinstance(p, str) else p
            if event.get("event") == "watch":
                apply_watch(state, event)
                if state[event["name"]] is not None and event["name"] not in observations:
                    observations[event["name"]] = bytes(state[event["name"]])
        try:
            pool = observations["actor_index"]
            team = observations["team_index"]
            group = observations["player_group"]
        except KeyError as error:
            raise ValueError(f"missing discovery watch: {error}") from error
        if len(pool) % 4 or len(team) != 4*len(records) or len(group) != 122:
            raise ValueError("discovery roster spans have wrong lengths")
        pool = list(struct.unpack(f"<{len(pool)//4}I", pool))
        team = list(struct.unpack(f"<{len(team)//4}I", team))
        count, = struct.unpack_from("<h", group, 0x10)
        side, = struct.unpack_from("<b", group, 0x12)
        if count != len(records) or not team or team[0] != player or not player or any(not p or p not in pool for p in team):
            raise ValueError("MEC/runtime team count, first-player or roster membership mismatch")
        if len(set(pool)) != len(pool) or len(set(team)) != len(team):
            raise ValueError("duplicate runtime actor pointers")
        candidates, unknown = [], []
        for i, ptr in enumerate(pool):
            if ptr in team:
                continue
            value = observations.get(f"actor{i}_side")
            if value is None or len(value) != 1:
                unknown.append(i)
            elif struct.unpack("<b", value)[0] != side:
                candidates.append(i)
        if hostile_indices is None:
            if unknown:
                raise ValueError(f"missing side observations for {unknown}; extend discovery or supply reviewed --hostiles")
            selected = candidates
        else:
            selected = hostile_indices
            if any(i >= len(pool) or pool[i] in team for i in selected):
                raise ValueError("reviewed hostile index is outside pool or belongs to team")
        lines += ["player_motion 0x4d24e6@0x28e 4"]
        for i in range(1, count):
            for suffix, offset, size in (("pos", "0x26", 12), ("flags", "0x99", 13), ("target", "0x1a4", 4)):
                lines.append(f"team{i}_{suffix} 0x4d24e6@0x45@0xc@{4*i}@{offset} {size}")
        for i in selected:
            for suffix, offset, size in (("pos", "0x26", 12), ("flags", "0x99", 13), ("target", "0x1a4", 4)):
                lines.append(f"h{i}_{suffix} 0x4a9af8@{4*i}@{offset} {size}")
        route = observations.get("route")
        if route is None:
            lines = [line for line in lines if not line.startswith("route ")]
            report["route_warning"] = "No readable player route observed; route watch omitted. Review this mission."
        route_count = struct.unpack_from("<h", route)[0] if route and len(route) == 6 else 0
        if route_count < 0:
            raise ValueError("negative runtime route count")
        available = max(0, 48-len(lines))
        for i in range(min(route_count, available)):
            lines.append(f"route_pt{i} 0x4d24e6@0x45@6@2@{4*i}@0 12")
        report.update(team_records=count, observed_actors=len(pool), hostile_candidates=candidates,
                      selected_hostiles=selected, unknown_sides=unknown, omitted_hostiles=sorted(set(candidates)-set(selected)),
                      route_points=route_count, omitted_route_points=max(0, route_count-available))
        report["log_sha256"] = hashlib.sha256(Path(log).read_bytes()).hexdigest()
    if len(lines) > 48:
        raise ValueError(f"proposal needs {len(lines)} watches, limit 48; select reviewed actor/hostile indices")
    report["watch_count"] = len(lines)
    return lines, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--launch", required=True); p.add_argument("--log")
    p.add_argument("--bootstrap", action="store_true"); p.add_argument("--actors"); p.add_argument("--hostiles")
    p.add_argument("--output", required=True); p.add_argument("--report", required=True)
    a = p.parse_args()
    lines, report = propose(a.launch, a.log, a.bootstrap, indices(a.actors) if a.actors is not None else None,
                            indices(a.hostiles) if a.hostiles is not None else None)
    for path in (a.output, a.report):
        if Path(path).exists():
            raise ValueError(f"output already exists: {path}")
    Path(a.output).write_text("\n".join(lines) + "\n", encoding="ascii")
    Path(a.report).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
