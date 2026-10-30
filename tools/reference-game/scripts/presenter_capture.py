# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""The acceptance passes of docs/reverse-engineering/presentation.md
("Acceptance", "capture, own GDI presenter") on the presentation capture of proxy/presenter.h.

  summary  CAPTURE              counts, sites, modes, page origins, palette changes
  sessions PATTERN              the per-process reports of one interactive session
  compare  A B                  pass 1/2: two captures of the same suite
  reference DUMP W H OUT        the reference rendering of one frame dump
  check-dumps DIR --offline EXE [--client WxH ...]
                                pass 3: presenter_offline (the DLL's code) against
                                the reference rendering, and 1:1 = table lookup

sessions: capture_report may contain "{pid}" (replaced by the process id of each DBSIM process,
so the processes of one session -- VSHELL, DBSIM, VSHELL, DBSIM -- write one report each).
PATTERN is that capture_report value (or a shell glob); "{pid}" matches any text. One entry per
file, ordered by modification time then name: pid (the start line's "pid", else the one in the
name), presenter, records, frame range. Exit 0 = at least one report, 1 = none; an unusable file is listed with
an "error" and does not stop the listing.

compare: records of the main-loop present and of the display-mode slides
(SEQUENCE_SITES) must agree one by one in FIELDS; records of every other site
(modal screens, mission load, WM_PAINT) follow message traffic and are compared
per frame as the set of distinct (index_sha256, palette_sha256). Only frames up
to the last frame both captures contain are compared (a stopped process loses
its current frame). Event lines (start, stats) are ignored. Exit 0 = identical,
1 = different, 2 = unusable input. Standard library only.
"""
import argparse
import glob
import json
import os
import re
import struct
import subprocess
import sys

MAIN_SITE = 0x45FBE4
# Return addresses of the 21 calls of 0x464a14 inside the transition 0x42a9c0.
SLIDE_CALLS = (0x42B135, 0x42B227, 0x42B28F, 0x42B339, 0x42B71B, 0x42B817, 0x42B87A, 0x42B912,
               0x42BB7F, 0x42BF02, 0x42BFFE, 0x42C061, 0x42C0F9, 0x42C663, 0x42C75F, 0x42C7C2,
               0x42C85A, 0x42CE49, 0x42CF3F, 0x42CFAE, 0x42D040)
SEQUENCE_SITES = frozenset([MAIN_SITE] + [c + 5 for c in SLIDE_CALLS])
FIELDS = ("frame", "site", "ordinal", "mode", "S", "R", "src", "source", "index_sha256", "palette_sha256")


def load(path):
    records, events = [], []
    with open(path, "r", encoding="ascii") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except ValueError as error:
                raise ValueError("%s:%d: %s" % (path, number, error))
            (events if "event" in value else records).append(value)
    if not events or events[0].get("event") != "start" or events[0].get("schema") not in (1, 2):
        raise ValueError("%s: no schema-1 or schema-2 start line" % path)   # 2 = capture_qpc=1
    for r in records:
        r["site_value"] = int(r["site"], 16)
    return records, events


def summary(path):
    records, events = load(path)
    sites, modes = {}, {}
    main_per_frame = {}
    page_moved, palette_changes, last_palette = 0, 0, None
    for r in records:
        sites[r["site"]] = sites.get(r["site"], 0) + 1
        modes[str(r["mode"])] = modes.get(str(r["mode"]), 0) + 1
        if r["site_value"] == MAIN_SITE:
            main_per_frame[r["frame"]] = main_per_frame.get(r["frame"], 0) + 1
        if r["S"] != [0, 0]:
            page_moved += 1
        if r["palette_sha256"] != last_palette:
            palette_changes += last_palette is not None
            last_palette = r["palette_sha256"]
    frames = sorted(main_per_frame)
    not_one = []
    if frames:
        not_one = [f for f in range(frames[0], frames[-1] + 1) if main_per_frame.get(f, 0) != 1]
    stats = [e for e in events if e.get("event") == "stats"]
    return {
        "records": len(records),
        "first_frame": records[0]["frame"] if records else None,
        "last_frame": records[-1]["frame"] if records else None,
        "sites": dict(sorted(sites.items())),
        "modes": modes,
        "main_frames": len(frames),
        "main_frames_not_one_record": not_one[:50],
        "main_frames_not_one_count": len(not_one),
        "page_origin_moved_records": page_moved,
        "palette_changes": palette_changes,
        "distinct_palettes": len({r["palette_sha256"] for r in records}),
        "distinct_frames": len({r["index_sha256"] for r in records}),
        "presented": {k: sum(1 for r in records if r.get("presented") == k) for k in ("original", "gdi", "forwarded")},
        "last_stats": stats[-1] if stats else None,
    }


def sessions(pattern):
    files = glob.glob(pattern.replace("{pid}", "*"))
    # Plain reports only: the dump directories are named "<report>.frames".
    files = sorted((f for f in files if os.path.isfile(f)), key=lambda f: (os.path.getmtime(f), f))
    parts = [re.escape(part).replace(r"\*", ".*") for part in pattern.split("{pid}")]
    name_pid = re.compile(r"(\d+)".join(parts), re.IGNORECASE)
    entries = []
    for f in files:
        entry = {"file": f}
        try:
            records, events = load(f)
            start = events[0]
            named = name_pid.fullmatch(f)
            entry.update({
                "pid": start.get("pid", int(named.group(1)) if named and named.groups() else None),
                "presenter": start.get("presenter"),
                "records": len(records),
                "first_frame": records[0]["frame"] if records else None,
                "last_frame": records[-1]["frame"] if records else None,
            })
        except (OSError, ValueError) as error:
            entry["error"] = str(error)
        entries.append(entry)
    return entries


def compare(path_a, path_b, fields=FIELDS):
    a, _ = load(path_a)
    b, _ = load(path_b)
    return compare_records(a, b, fields)


def compare_records(a, b, fields=FIELDS):
    """compare() on loaded records (each with "site_value"); also used by dbsim_frames.py."""
    last = min(a[-1]["frame"] if a else -1, b[-1]["frame"] if b else -1)

    def split(records):
        sequence, other = [], {}
        for r in records:
            if r["frame"] > last:
                continue
            if r["site_value"] in SEQUENCE_SITES:
                sequence.append(tuple(json.dumps(r.get(k), sort_keys=True) for k in fields))
            else:
                other.setdefault(r["frame"], set()).add((r["index_sha256"], r["palette_sha256"]))
        return sequence, other

    seq_a, other_a = split(a)
    seq_b, other_b = split(b)
    differences = []
    for i in range(max(len(seq_a), len(seq_b))):
        ra = seq_a[i] if i < len(seq_a) else None
        rb = seq_b[i] if i < len(seq_b) else None
        if ra != rb:
            differences.append({"kind": "sequence", "index": i,
                                "a": dict(zip(fields, map(json.loads, ra))) if ra else None,
                                "b": dict(zip(fields, map(json.loads, rb))) if rb else None})
            break
    for frame in sorted(set(other_a) | set(other_b)):
        if other_a.get(frame) != other_b.get(frame):
            differences.append({"kind": "frame_set", "frame": frame,
                                "a": sorted(map(list, other_a.get(frame, ()))), "b": sorted(map(list, other_b.get(frame, ())))})
            break
    return {"identical": not differences, "compared_through_frame": last,
            "sequence_records": [len(seq_a), len(seq_b)],
            "other_frames": [len(other_a), len(other_b)], "first_difference": differences}


# ---- frame dumps (presenter.h pr_dump) ----------------------------------------
def read_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 800 or data[:4] != b"ES2F":
        raise ValueError("%s: not an ES2F dump" % path)
    version, frame, site, ordinal, width, height, uploads = struct.unpack_from("<7I", data, 4)
    if version != 1 or not (0 < width <= 4096 and 0 < height <= 4096) or len(data) != 800 + width * height:
        raise ValueError("%s: bad header or size" % path)
    palette = [tuple(data[32 + 3 * i:35 + 3 * i]) for i in range(256)]
    return {"frame": frame, "site": site, "ordinal": ordinal, "width": width, "height": height,
            "uploads": uploads, "palette": palette, "pixels": data[800:]}


def layout(w, h, cw, ch):
    """The presenter's placement: largest integer scale, else aspect-correct fit; centred."""
    if w <= 0 or h <= 0 or cw <= 0 or ch <= 0 or cw * ch > 4096 * 4096:
        raise ValueError("bad sizes")
    k = min(cw // w, ch // h)
    if k >= 1:
        width, height = w * k, h * k
    elif cw * h <= ch * w:
        width, height = cw, h * cw // w
    else:
        width, height = w * ch // h, ch
    width, height = max(width, 1), max(height, 1)
    return {"scale": k if k >= 1 else 0, "x": (cw - width) // 2, "y": (ch - height) // 2, "width": width, "height": height}


def reference_render(dump, cw, ch):
    """Client image, rows top-down, BI_RGB bytes (blue, green, red, 0)."""
    w, h = dump["width"], dump["height"]
    p = layout(w, h, cw, ch)
    colours = [bytes((b, g, r, 0)) for (r, g, b) in dump["palette"]]
    black = b"\0\0\0\0"
    left, right = black * p["x"], black * (cw - p["x"] - p["width"])
    columns = [x * w // p["width"] for x in range(p["width"])]
    rows, cache = [], {}
    for y in range(ch):
        if y < p["y"] or y >= p["y"] + p["height"]:
            rows.append(black * cw)
            continue
        v = (y - p["y"]) * h // p["height"]
        if v not in cache:
            line = dump["pixels"][v * w:(v + 1) * w]
            cache[v] = left + b"".join(colours[line[u]] for u in columns) + right
        rows.append(cache[v])
    return b"".join(rows), p


def table_lookup(dump):
    colours = [bytes((b, g, r, 0)) for (r, g, b) in dump["palette"]]
    return b"".join(colours[i] for i in dump["pixels"])


def check_dumps(directory, offline, clients, scratch):
    files = sorted(n for n in os.listdir(directory) if n.endswith(".es2f"))
    results, failures = [], 0
    os.makedirs(scratch, exist_ok=True)
    for name in files:
        path = os.path.join(directory, name)
        dump = read_dump(path)
        sizes = [(dump["width"], dump["height"])] + clients
        for cw, ch in sizes:
            out = os.path.join(scratch, "%s-%dx%d.bgrx" % (name, cw, ch))
            run = subprocess.run([offline, path, str(cw), str(ch), out], capture_output=True, text=True)
            if run.returncode:
                results.append({"dump": name, "client": [cw, ch], "ok": False, "error": run.stderr.strip()})
                failures += 1
                continue
            with open(out, "rb") as f:
                produced = f.read()
            os.remove(out)
            expected, placement = reference_render(dump, cw, ch)
            ok = produced == expected and json.loads(run.stdout)["image"] == [placement["width"], placement["height"]]
            if (cw, ch) == (dump["width"], dump["height"]):
                ok = ok and produced == table_lookup(dump)
            failures += not ok
            results.append({"dump": name, "client": [cw, ch], "ok": ok, "scale": placement["scale"]})
    return {"dumps": len(files), "checks": len(results), "failures": failures, "results": results}


def parse_client(text):
    w, _, h = text.lower().partition("x")
    return int(w), int(h)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("summary"); s.add_argument("capture")
    n = sub.add_parser("sessions"); n.add_argument("pattern")
    c = sub.add_parser("compare"); c.add_argument("a"); c.add_argument("b")
    c.add_argument("--fields", default=",".join(FIELDS), help="comma-separated record fields of the sequence comparison")
    r = sub.add_parser("reference"); r.add_argument("dump"); r.add_argument("width", type=int)
    r.add_argument("height", type=int); r.add_argument("output")
    d = sub.add_parser("check-dumps"); d.add_argument("directory"); d.add_argument("--offline", required=True)
    d.add_argument("--client", action="append", default=[], type=parse_client)
    d.add_argument("--scratch", default=None)
    args = parser.parse_args(argv)
    try:
        if args.command == "summary":
            print(json.dumps(summary(args.capture), indent=1))
            return 0
        if args.command == "sessions":
            entries = sessions(args.pattern)
            print(json.dumps({"reports": len(entries), "sessions": entries}, indent=1))
            return 0 if entries else 1
        if args.command == "compare":
            result = compare(args.a, args.b, tuple(args.fields.split(",")))
            print(json.dumps(result, indent=1))
            return 0 if result["identical"] else 1
        if args.command == "reference":
            image, _ = reference_render(read_dump(args.dump), args.width, args.height)
            with open(args.output, "wb") as f:
                f.write(image)
            return 0
        result = check_dumps(args.directory, args.offline, args.client,
                             args.scratch or os.path.join(args.directory, "check-scratch"))
        print(json.dumps(result, indent=1))
        return 0 if result["dumps"] and not result["failures"] else 1
    except (OSError, ValueError, KeyError) as error:
        print("presenter_capture: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
