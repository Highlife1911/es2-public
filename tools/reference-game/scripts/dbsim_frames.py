"""The per-suite frame manifest of DBSIM's presentation capture.

  build CAPTURE OUTDIR --suite NAME [--run-info FILE] [--date DATE]
        Reads one capture report of proxy/presenter.h (capture.ndjson, presentation.md "capture, own GDI presenter"),
        writes OUTDIR/frames.tsv.gz (one line per present: frame, site, ordinal, mode, page origin,
        source kind, the SHA-256 of the 8-bit frame and of the colour table) and OUTDIR/manifest.json
        (the run's identity, the capture's counters, the frame summary, the digest of the table).
        --run-info is a JSON object stored under "run" (result against the golden, commit, clone, ...).
        A missing or unusable capture still writes the manifest, with a status that says so.
        Exit 0 = status "ok", 1 = any other status, 2 = bad arguments or unwritable output.
  verify DIR
        Re-reads DIR/manifest.json and DIR/frames.tsv.gz and checks every recorded size, hash and
        count. Exit 0 = consistent, 1 = not, 2 = unusable input.
  compare A B
        A and B are a manifest directory, a frames.tsv(.gz) or a capture report. The sequence rules
        of presenter_capture.py: the records of the main loop and of the display-mode slides must
        agree one by one, the records of every other site (modal screens, loading, WM_PAINT)
        are compared per frame as the set of distinct (index, palette) pairs, and only the frames
        up to the last frame both contain are compared. Exit 0 = identical, 1 = different, 2 = unusable.
  regions A B [--exclude-frames N,N]
        (own_process.md "the frame compare as tests"): every kind of difference with its first frame and its count instead
        of the first difference only. The records of the sequence sites are matched by (frame, site,
        ordinal); a key on one side only is kind "sequence", a different mode, page origin or source
        "geometry", a different frame "frame", a different colour table "palette"; when both sides carry
        region hashes of the same table (proxy/frame_regions.h) a different display mode is "display"
        and a region whose hash differs is its name ("view", "hud", "panel", "mfd", "rest"). The other
        sites' per-frame sets are "other_sites"; --exclude-frames leaves frames out of that compare
        (the original varies there). Exit 0 = no difference, 1 = differences, 2 = unusable.

The hashes are those of the capture: the w x h index bytes of the DIB window at the page origin as DBSIM
wrote them (before any conversion) and the 256 RGB triples of the last colour table (reserved byte left out).
A capture with region hashes (the proxy's [presentation] capture_regions=1, es2sim's --frames) gives a
table with six more columns (display mode, five 16-digit region hashes) and a second header line naming the
region table. No pixels are stored here. Standard library only.
"""
import argparse
import datetime
import gzip
import hashlib
import io
import json
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import presenter_capture as pc  # noqa: E402

SCHEMA = 1
KIND = "es2-dbsim-frames"
FRAMES_NAME = "frames.tsv.gz"
MANIFEST_NAME = "manifest.json"
COLUMNS = ("frame", "site", "ordinal", "mode", "sx", "sy", "source", "index_sha256", "palette_sha256")
HEADER = "#" + "\t".join(COLUMNS)
# The sequence comparison needs only what the table keeps (presenter_capture.FIELDS also has R and src).
COMPARE_FIELDS = ("frame", "site", "ordinal", "mode", "S", "source", "index_sha256", "palette_sha256")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
# The region columns (proxy/frame_regions.h: the display mode and the 16-digit region hashes).
REGION_NAMES = ("view", "hud", "panel", "mfd", "rest")
REGION_HEADER = HEADER + "\t" + "\t".join(("display",) + REGION_NAMES)
REGION_TABLE_LINE = "#regions\t"
HEX16 = re.compile(r"^[0-9a-f]{16}$")
TABLE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,31}$")


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---- the capture report ---------------------------------------------------------------------------
CAPTURE_SCHEMAS = (1, 2)   # the start line's "schema": 2 = capture_qpc=1


def _is_count(value, minimum):
    return isinstance(value, int) and not isinstance(value, bool) and value >= minimum


def read_capture(path):
    """(records, events, closed, bad_lines) of a capture report.

    closed: the last line is a stats event that counts exactly the records read, i.e. the proxy
    ended orderly (presenter_shutdown writes it after the last frame; the periodic stats lines
    are followed by more records). A line that is not JSON is skipped and counted: a stopped
    process may leave a torn last line. Accepts the start schemas 1 and 2 (`capture_qpc=1`, a
    "qpc_frequency" in the start line and a "qpc" in every record, which are checked here); raises ValueError
    for any other start line."""
    records, events, bad = [], [], 0
    last_is_event = False
    with open(path, "r", encoding="ascii", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except ValueError:
                bad += 1
                continue
            if "event" in value:
                events.append(value)
                last_is_event = True
            else:
                records.append(value)
                last_is_event = False
    if not events or events[0].get("event") != "start" or events[0].get("schema") not in CAPTURE_SCHEMAS:
        raise ValueError("%s: no schema-1 or schema-2 start line" % path)
    timed = events[0]["schema"] == 2
    if timed and not _is_count(events[0].get("qpc_frequency"), 1):
        raise ValueError("%s: a schema-2 start line without a positive qpc_frequency" % path)
    for r in records:
        r["site_value"] = int(r["site"], 16)
        if timed and not _is_count(r.get("qpc"), 0):
            raise ValueError("%s: a schema-2 record without a qpc value (frame %s)" % (path, r.get("frame")))
        if "regions" in r and not isinstance(r.get("display"), int):
            raise ValueError("%s: a record with regions but no display mode" % path)
    stats = events[-1] if events[-1].get("event") == "stats" else None
    closed = bool(last_is_event and stats and stats.get("records") == len(records))
    return records, events, closed, bad


# ---- the table ------------------------------------------------------------------------------------
def _cell(value):
    return "-" if value is None else str(value)


def records_to_tsv(records, region_table=None):
    """The table; with region_table the region columns and the table line."""
    lines = [HEADER] if region_table is None else [REGION_HEADER, REGION_TABLE_LINE + region_table]
    for r in records:
        cells = [str(r["frame"]), r["site"], str(r["ordinal"]), str(r["mode"]), str(r["S"][0]), str(r["S"][1]),
                 r["source"], _cell(r["index_sha256"]), _cell(r["palette_sha256"])]
        if region_table is not None:
            regions = r.get("regions")
            cells.append(str(r["display"]) if regions is not None else "-")
            cells.extend(_cell(regions.get(name)) if regions is not None else "-" for name in REGION_NAMES)
        lines.append("\t".join(cells))
    return ("\n".join(lines) + "\n").encode("ascii")


def region_table_of(data):
    """The region table id of a table (None for a table without region columns)."""
    lines = data.decode("ascii").split("\n", 2)
    if lines[0] == REGION_HEADER and len(lines) > 1 and lines[1].startswith(REGION_TABLE_LINE):
        return lines[1][len(REGION_TABLE_LINE):]
    return None


def parse_tsv(data):
    """Record dicts (the fields of COMPARE_FIELDS plus site_value) of a table; strict."""
    text = data.decode("ascii")
    lines = text.split("\n")
    if not lines or lines[0] not in (HEADER, REGION_HEADER):
        raise ValueError("frames table: missing or unknown header")
    if lines[-1] != "":
        raise ValueError("frames table: no final newline")
    with_regions = lines[0] == REGION_HEADER
    first = 1
    if with_regions:
        if len(lines) < 3 or not lines[1].startswith(REGION_TABLE_LINE) or not TABLE_ID.match(lines[1][len(REGION_TABLE_LINE):]):
            raise ValueError("frames table: region columns without a region table line")
        first = 2
    width = len(COLUMNS) + (1 + len(REGION_NAMES) if with_regions else 0)
    records = []
    for number, line in enumerate(lines[first:-1], first + 1):
        cells = line.split("\t")
        if len(cells) != width:
            raise ValueError("frames table line %d: %d columns" % (number, len(cells)))
        region_cells = cells[len(COLUMNS):]
        frame, site, ordinal, mode, sx, sy, source, index, palette = cells[:len(COLUMNS)]
        for name, value in (("index_sha256", index), ("palette_sha256", palette)):
            if value != "-" and not HEX64.match(value):
                raise ValueError("frames table line %d: bad %s" % (number, name))
        if not re.match(r"^0x[0-9a-f]{6}$", site):
            raise ValueError("frames table line %d: bad site" % number)
        try:
            row = {"frame": int(frame), "site": site, "site_value": int(site, 16), "ordinal": int(ordinal), "mode": int(mode),
                   "S": [int(sx), int(sy)], "source": source,
                   "index_sha256": None if index == "-" else index, "palette_sha256": None if palette == "-" else palette}
        except ValueError:
            raise ValueError("frames table line %d: bad number" % number)
        if region_cells and region_cells[0] != "-":
            if not re.match(r"^-?[0-9]{1,10}$", region_cells[0]):
                raise ValueError("frames table line %d: bad display mode" % number)
            for value in region_cells[1:]:
                if value != "-" and not HEX16.match(value):
                    raise ValueError("frames table line %d: bad region hash" % number)
            row["display"] = int(region_cells[0])
            row["regions"] = {name: None if value == "-" else value for name, value in zip(REGION_NAMES, region_cells[1:])}
        elif region_cells and any(value != "-" for value in region_cells[1:]):
            raise ValueError("frames table line %d: region hashes without a display mode" % number)
        records.append(row)
    return records


def gzip_bytes(data):
    """Deterministic gzip (no name, no time): the same table always gives the same file."""
    out = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as g:
        g.write(data)
    return out.getvalue()


def load_frames(path):
    """Records of a manifest directory, a frames.tsv(.gz) or a capture report."""
    return load_frames_with_regions(path)[0]


def load_frames_with_regions(path):
    """(records, region table id or None) of a manifest directory, a frames.tsv(.gz) or a capture report."""
    if os.path.isdir(path):
        manifest = read_manifest(path)
        path = os.path.join(path, manifest["frames"]["file"])
    if path.endswith(".gz"):
        with gzip.open(path, "rb") as f:
            data = f.read()
        return parse_tsv(data), region_table_of(data)
    with open(path, "rb") as f:
        head = f.read(1)
        f.seek(0)
        data = f.read()
    if head == b"#":
        return parse_tsv(data), region_table_of(data)
    records, events, _, _ = read_capture(path)
    table = events[0].get("regions")
    return records, table if isinstance(table, str) else None


# ---- the manifest ---------------------------------------------------------------------------------
def frame_summary(records, region_table=None):
    sites, modes, main_per_frame = {}, {}, {}
    page_moved, palette_changes, last_palette = 0, 0, None
    for r in records:
        sites[r["site"]] = sites.get(r["site"], 0) + 1
        modes[str(r["mode"])] = modes.get(str(r["mode"]), 0) + 1
        if r["site_value"] == pc.MAIN_SITE:
            main_per_frame[r["frame"]] = main_per_frame.get(r["frame"], 0) + 1
        if r["S"] != [0, 0]:
            page_moved += 1
        if r["palette_sha256"] != last_palette:
            palette_changes += last_palette is not None
            last_palette = r["palette_sha256"]
    frames = sorted(main_per_frame)
    not_one = [f for f in range(frames[0], frames[-1] + 1) if main_per_frame.get(f, 0) != 1] if frames else []
    return {
        "records": len(records),
        "first_frame": records[0]["frame"] if records else None,
        "last_frame": records[-1]["frame"] if records else None,
        "main_frames": len(frames),
        "main_frames_not_one_count": len(not_one),
        "main_frames_not_one_record": not_one[:50],
        "sites": dict(sorted(sites.items())),
        "modes": dict(sorted(modes.items())),
        "page_origin_moved_records": page_moved,
        "palette_changes": palette_changes,
        "distinct_palettes": len({r["palette_sha256"] for r in records}),
        "distinct_frames": len({r["index_sha256"] for r in records}),
        "records_without_hash": sum(1 for r in records if r["index_sha256"] is None or r["palette_sha256"] is None),
    } | ({} if region_table is None else {
        "region_table": region_table,
        "records_with_regions": sum(1 for r in records if r.get("regions") is not None),
    })


STATS_KEYS = ("frame", "records", "presents", "presented_gdi", "forwarded", "clear_blits", "other_blits", "foreign_thread",
              "bad_geometry", "no_table", "not_full_screen", "draw_failures", "uploads", "dumps", "dump_failures", "write_failures",
              "announced_sites", "unattributed_sites")


def build(capture, out_dir, suite, run_info=None, date=None):
    """Writes the table and the manifest; returns the manifest."""
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"schema": SCHEMA, "kind": KIND, "suite": suite,
                "created_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    if date:
        manifest["date"] = date
    manifest["run"] = run_info or {}
    status = "ok"
    records = []
    try:
        records, events, closed, bad = read_capture(capture)
        start = events[0]
        stats = events[-1] if events[-1].get("event") == "stats" else None
        manifest["capture"] = {
            "file": os.path.basename(capture), "bytes": os.path.getsize(capture), "sha256": sha256_file(capture),
            "start": {k: start[k] for k in ("presenter", "dump_frames", "dump_range", "pid", "regions") if k in start},
            "closed": closed, "bad_lines": bad,
            "last_stats": {k: stats[k] for k in STATS_KEYS if k in stats} if stats else None,
        }
        if start["schema"] != 1:   # a timed capture says so; a schema-1 manifest keeps its shape
            manifest["capture"]["start"]["schema"] = start["schema"]
            manifest["capture"]["start"]["qpc_frequency"] = start["qpc_frequency"]
    except (OSError, ValueError) as error:
        manifest["capture"] = {"file": os.path.basename(capture), "error": str(error)}
        status = "no-capture" if not os.path.exists(capture) else "unusable-capture"
    if status == "ok":
        region_table = manifest["capture"]["start"].get("regions")
        if region_table is not None and not (isinstance(region_table, str) and TABLE_ID.match(region_table)):
            region_table = None
            status = "unusable-capture"
            manifest["capture"]["error"] = "bad region table id"
    if status == "ok":
        table = records_to_tsv(records, region_table)
        packed = gzip_bytes(table)
        with open(os.path.join(out_dir, FRAMES_NAME), "wb") as f:
            f.write(packed)
        summary = frame_summary(records, region_table)
        summary.update({"file": FRAMES_NAME, "bytes": len(packed), "sha256": sha256_bytes(packed),
                        "table_sha256": sha256_bytes(table)})
        manifest["frames"] = summary
        if not records:
            status = "no-records"
        elif manifest["run"].get("result") not in (None, "identical"):
            status = "run-different"
    else:
        stale = os.path.join(out_dir, FRAMES_NAME)
        if os.path.exists(stale):
            os.remove(stale)
    manifest["status"] = status
    with open(os.path.join(out_dir, MANIFEST_NAME), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(manifest, indent=1, sort_keys=False) + "\n")
    return manifest


def read_manifest(directory):
    path = os.path.join(directory, MANIFEST_NAME)
    with open(path, "r", encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("schema") != SCHEMA or manifest.get("kind") != KIND:
        raise ValueError("%s: not a schema-%d %s manifest" % (path, SCHEMA, KIND))
    if "frames" not in manifest:
        raise ValueError("%s: status %s, the manifest has no frame table" % (path, manifest.get("status")))
    return manifest


def verify(directory):
    """List of problems of a manifest directory (empty = consistent)."""
    problems = []
    manifest = read_manifest(directory)
    frames = manifest["frames"]
    path = os.path.join(directory, frames["file"])
    packed = open(path, "rb").read()
    if len(packed) != frames["bytes"]:
        problems.append("frames file size %d, manifest %d" % (len(packed), frames["bytes"]))
    if sha256_bytes(packed) != frames["sha256"]:
        problems.append("frames file hash differs")
    table = gzip.decompress(packed)
    if sha256_bytes(table) != frames["table_sha256"]:
        problems.append("table hash differs")
    records = parse_tsv(table)
    again = frame_summary(records, region_table_of(table))
    for key in again:
        if again[key] != frames.get(key):
            problems.append("%s: table says %r, manifest %r" % (key, again[key], frames.get(key)))
    return problems


def compare(path_a, path_b):
    a, b = load_frames(path_a), load_frames(path_b)
    return pc.compare_records(a, b, COMPARE_FIELDS)


# ---- every kind of difference, per region ----------------------------------------------------------
KINDS = ("sequence", "geometry", "frame", "palette", "display") + REGION_NAMES + ("other_sites",)
FRAMES_LISTED = 200


def region_report(a, b, table_a=None, table_b=None, exclude_frames=()):
    """The differences of B against A by kind: {kind: {"first": ..., "count": n, "frames": [...]}}.

    a and b are loaded records (each with "site_value"); table_a/table_b their region table ids. Only the
    frames up to the last frame both contain are compared (as compare_records). "count" counts records
    (other_sites: frames); "frames" lists the first FRAMES_LISTED differing frames."""
    last = min(a[-1]["frame"] if a else -1, b[-1]["frame"] if b else -1)
    excluded = set(exclude_frames)

    def split(records):
        sequence, other = {}, {}
        for r in records:
            if r["frame"] > last:
                continue
            if r["site_value"] in pc.SEQUENCE_SITES:
                sequence[(r["frame"], r["site_value"], r["ordinal"])] = r
            elif r["frame"] not in excluded:
                other.setdefault(r["frame"], set()).add((r["index_sha256"], r["palette_sha256"]))
        return sequence, other

    seq_a, other_a = split(a)
    seq_b, other_b = split(b)
    with_regions = table_a is not None and table_a == table_b
    kinds = {}

    def note(kind, frame, detail):
        entry = kinds.get(kind)
        if entry is None:
            entry = kinds[kind] = {"first": dict(detail, frame=frame), "count": 0, "frames": []}
        entry["count"] += 1
        if (not entry["frames"] or entry["frames"][-1] != frame) and len(entry["frames"]) < FRAMES_LISTED:
            entry["frames"].append(frame)

    matched = 0
    for key in sorted(set(seq_a) | set(seq_b)):
        ra, rb = seq_a.get(key), seq_b.get(key)
        detail = {"site": "0x%06x" % key[1], "ordinal": key[2]}
        if ra is None or rb is None:
            note("sequence", key[0], dict(detail, only_in="a" if rb is None else "b"))
            continue
        matched += 1
        if (ra["mode"], list(ra["S"]), ra["source"]) != (rb["mode"], list(rb["S"]), rb["source"]):
            note("geometry", key[0], dict(detail, a=[ra["mode"], list(ra["S"]), ra["source"]],
                                          b=[rb["mode"], list(rb["S"]), rb["source"]]))
        if ra["index_sha256"] != rb["index_sha256"]:
            note("frame", key[0], detail)
        if ra["palette_sha256"] != rb["palette_sha256"]:
            note("palette", key[0], detail)
        if not with_regions or ra.get("regions") is None or rb.get("regions") is None:
            continue
        if ra["display"] != rb["display"]:
            note("display", key[0], dict(detail, a=ra["display"], b=rb["display"]))
            continue
        for name in REGION_NAMES:
            if ra["regions"].get(name) != rb["regions"].get(name):
                note(name, key[0], dict(detail, display=ra["display"]))
    for frame in sorted(set(other_a) | set(other_b)):
        if other_a.get(frame) != other_b.get(frame):
            note("other_sites", frame, {"a": len(other_a.get(frame, ())), "b": len(other_b.get(frame, ()))})
    ordered = {k: kinds[k] for k in KINDS if k in kinds}
    first = min(ordered.items(), key=lambda item: (item[1]["first"]["frame"], KINDS.index(item[0])))[0] if ordered else None
    return {"identical": not ordered, "compared_through_frame": last, "region_tables": [table_a, table_b],
            "regions_compared": with_regions, "sequence_records": [len(seq_a), len(seq_b)], "matched_records": matched,
            "other_frames": [len(other_a), len(other_b)], "excluded_frames": sorted(excluded),
            "first_kind": first, "kinds": ordered}


def regions(path_a, path_b, exclude_frames=()):
    a, table_a = load_frames_with_regions(path_a)
    b, table_b = load_frames_with_regions(path_b)
    return region_report(a, b, table_a, table_b, exclude_frames)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build"); b.add_argument("capture"); b.add_argument("outdir"); b.add_argument("--suite", required=True)
    b.add_argument("--run-info", default=None); b.add_argument("--date", default=None)
    v = sub.add_parser("verify"); v.add_argument("directory")
    c = sub.add_parser("compare"); c.add_argument("a"); c.add_argument("b")
    g = sub.add_parser("regions"); g.add_argument("a"); g.add_argument("b"); g.add_argument("--exclude-frames", default="")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            info = None
            if args.run_info:
                with open(args.run_info, "r", encoding="utf-8-sig") as f:
                    info = json.load(f)
            manifest = build(args.capture, args.outdir, args.suite, info, args.date)
            print(json.dumps({"suite": args.suite, "status": manifest["status"],
                              "records": manifest.get("frames", {}).get("records")}))
            return 0 if manifest["status"] == "ok" else 1
        if args.command == "verify":
            problems = verify(args.directory)
            print(json.dumps({"directory": args.directory, "consistent": not problems, "problems": problems}, indent=1))
            return 0 if not problems else 1
        if args.command == "regions":
            frames = [int(v) for v in re.split(r"[,\s]+", args.exclude_frames) if v]
            result = regions(args.a, args.b, frames)
            print(json.dumps(result, indent=1))
            return 0 if result["identical"] else 1
        result = compare(args.a, args.b)
        print(json.dumps(result, indent=1))
        return 0 if result["identical"] else 1
    except (OSError, ValueError, KeyError, EOFError, zlib.error) as error:
        print("dbsim_frames: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
