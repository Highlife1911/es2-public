"""Where two ES2F frame dumps differ.

  compare A B [--display M] [--regions-header PATH] [--rects N] [--palette N]
        A and B are two dumps (*.es2f) or two directories of dumps. Directories are paired by file
        name (frame-<frame>-<site>-<ordinal>.es2f); a name on one side only is reported. For every pair:
        the header fields that differ (frame, site, ordinal, width, height, uploads), the colour-table
        entries that differ, and, when both have the same size, the pixels whose index bytes differ:
        their count, how many of them still show the same colour (equal RGB in the two tables), the
        first one in row-major order, the bounding box and the connected areas of differing pixels
        (4-neighbour components, each as its bounding box and pixel count, largest first, at most
        --rects of them). With --display M (the display mode [[0x4d24c0]+0x14] of the frame, which the
        dump does not carry) the differing pixels are also counted per region of the region table
        (proxy/frame_regions.h, read from --regions-header). Prints one JSON object.
        Exit 0 = identical, 1 = different, 2 = unusable input.

The dumps are those of the proxy (proxy/presenter.h pr_dump, [presentation] dump_frames) and of es2sim
(--dump-frames LIST --dump-dir DIR, sim/src/present.c); both write the same format (presentation.md
"capture, own GDI presenter"): "ES2F", seven little-endian u32 (version 1, frame, site, ordinal, width, height, uploads),
256 RGB triples, width x height index bytes with the top row first. Rectangles in the report are
(left, top, right, bottom) with right and bottom exclusive, in the dump's own pixel coordinates.
Dumps are decoded game frames: keep them in ignored scratch and delete them after use; only this report's counts and rectangles may be kept. Standard library only.
"""
import argparse
import json
import os
import re
import struct
import sys

HEADER_FIELDS = ("version", "frame", "site", "ordinal", "width", "height", "uploads")
REGION_NAMES = ("view", "hud", "panel", "mfd", "rest")
DEFAULT_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "proxy", "frame_regions.h")


def read_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 800 or data[:4] != b"ES2F":
        raise ValueError("%s: not an ES2F dump" % path)
    fields = dict(zip(HEADER_FIELDS, struct.unpack_from("<7I", data, 4)))
    w, h = fields["width"], fields["height"]
    if fields["version"] != 1 or not (0 < w <= 4096 and 0 < h <= 4096) or len(data) != 800 + w * h:
        raise ValueError("%s: bad header or size" % path)
    fields["palette"] = [tuple(data[32 + 3 * i:35 + 3 * i]) for i in range(256)]
    fields["pixels"] = data[800:]
    return fields


def region_table(path):
    """{mode: [(region index, left, top, right, bottom), ...]} from frame_regions.h's fr_mode<N> arrays
    and the case labels of fr_table (modes 2 and 3 share fr_mode2)."""
    with open(path, "r", encoding="ascii") as f:
        text = f.read()
    names = {"FR_" + n.upper(): i for i, n in enumerate(REGION_NAMES)}
    arrays = {}
    for m in re.finditer(r"static const fr_rect (fr_mode\d+)\[\] = \{(.*?)\n\};", text, re.S):
        rows = re.findall(r"\{(FR_[A-Z]+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\}", m.group(2))
        arrays[m.group(1)] = [(names[r[0]],) + tuple(int(v) for v in r[1:]) for r in rows]
    body = re.search(r"fr_table\(int32_t mode, size_t \*count\) \{(.*?)\n\}", text, re.S)
    if not arrays or not body:
        raise ValueError("%s: no region table found" % path)
    table, pending = {}, []
    for line in body.group(1).splitlines():
        for case in re.findall(r"case (\d+):", line):
            pending.append(int(case))
        use = re.search(r"return (fr_mode\d+);", line)
        if use:
            for mode in pending:
                table[mode] = arrays[use.group(1)]
            pending = []
    return table


def region_labels(table, mode, w, h):
    """Per pixel (row-major) the region index, with frame_regions.h's scaling and priority."""
    rects = table.get(mode, [])
    labels = bytearray([4]) * (w * h)
    for y in range(h):
        row = bytearray([4]) * w
        for region, left, top, right, bottom in reversed(rects):
            if top * h // 480 <= y < bottom * h // 480:
                l, r = left * w // 640, right * w // 640
                if r > l:
                    row[l:r] = bytes([region]) * (r - l)
        labels[y * w:(y + 1) * w] = row
    return labels


def components(mask_rows, w, h):
    """4-neighbour components of the differing pixels from per-row runs: [(left, top, right, bottom, pixels)]."""
    parent = []

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    runs, previous = [], []
    for y in range(h):
        current = []
        for x0, x1 in mask_rows[y]:
            index = len(runs)
            runs.append((y, x0, x1))
            parent.append(index)
            for p in previous:
                _, p0, p1 = runs[p]
                if p0 < x1 and x0 < p1:
                    a, b = find(p), find(index)
                    if a != b:
                        parent[b] = a
            current.append(index)
        previous = current
    boxes = {}
    for i, (y, x0, x1) in enumerate(runs):
        root = find(i)
        box = boxes.get(root)
        if box is None:
            boxes[root] = [x0, y, x1, y + 1, x1 - x0]
        else:
            box[0], box[1], box[2], box[3] = min(box[0], x0), min(box[1], y), max(box[2], x1), max(box[3], y + 1)
            box[4] += x1 - x0
    return sorted((tuple(b) for b in boxes.values()), key=lambda b: (-b[4], b[1], b[0]))


def compare_dumps(a, b, display=None, table=None, rects=16, palette_limit=16):
    report = {"header": {}, "palette": {"count": 0, "entries": []}, "pixels": None}
    for field in HEADER_FIELDS[1:]:
        if a[field] != b[field]:
            report["header"][field] = [a[field], b[field]]
    differing = [i for i in range(256) if a["palette"][i] != b["palette"][i]]
    report["palette"]["count"] = len(differing)
    report["palette"]["entries"] = [{"index": i, "a": list(a["palette"][i]), "b": list(b["palette"][i])}
                                    for i in differing[:palette_limit]]
    identical = not report["header"] and not differing
    if a["width"] != b["width"] or a["height"] != b["height"]:
        report["pixels"] = {"compared": False}
        return report, False
    w, h = a["width"], a["height"]
    pa, pb = a["pixels"], b["pixels"]
    count = same_colour = 0
    first = None
    mask_rows = []
    labels = region_labels(table, display, w, h) if table is not None and display is not None else None
    per_region = [0] * len(REGION_NAMES)
    for y in range(h):
        runs, x, base = [], 0, y * w
        ra, rb = pa[base:base + w], pb[base:base + w]
        if ra == rb:
            mask_rows.append(runs)
            continue
        while x < w:
            if ra[x] == rb[x]:
                x += 1
                continue
            start = x
            while x < w and ra[x] != rb[x]:
                if a["palette"][ra[x]] == b["palette"][rb[x]]:
                    same_colour += 1
                if labels is not None:
                    per_region[labels[base + x]] += 1
                x += 1
            runs.append((start, x))
            count += x - start
            if first is None:
                first = {"x": start, "y": y, "a": ra[start], "b": rb[start]}
        mask_rows.append(runs)
    pixels = {"compared": True, "count": count, "same_colour": same_colour, "first": first}
    if count:
        boxes = components(mask_rows, w, h)
        pixels["bbox"] = [min(c[0] for c in boxes), min(c[1] for c in boxes),
                          max(c[2] for c in boxes), max(c[3] for c in boxes)]
        pixels["areas"] = len(boxes)
        pixels["rects"] = [{"rect": list(c[:4]), "pixels": c[4]} for c in boxes[:rects]]
    if labels is not None:
        pixels["display"] = display
        pixels["regions"] = {REGION_NAMES[i]: per_region[i] for i in range(len(REGION_NAMES))}
    report["pixels"] = pixels
    return report, identical and not count


def compare_paths(a, b, display=None, table=None, rects=16, palette_limit=16):
    if os.path.isdir(a) and os.path.isdir(b):
        names_a = {n for n in os.listdir(a) if n.endswith(".es2f")}
        names_b = {n for n in os.listdir(b) if n.endswith(".es2f")}
        pairs, identical = [], True
        for name in sorted(names_a & names_b):
            one, same = compare_dumps(read_dump(os.path.join(a, name)), read_dump(os.path.join(b, name)),
                                      display, table, rects, palette_limit)
            if not same:
                one["dump"] = name
                pairs.append(one)
                identical = False
        only_a, only_b = sorted(names_a - names_b), sorted(names_b - names_a)
        if only_a or only_b:
            identical = False
        return {"compared": len(names_a & names_b), "only_a": only_a, "only_b": only_b,
                "different": pairs}, identical
    if os.path.isfile(a) and os.path.isfile(b):
        one, same = compare_dumps(read_dump(a), read_dump(b), display, table, rects, palette_limit)
        return {"compared": 1, "only_a": [], "only_b": [], "different": [] if same else [one]}, same
    raise ValueError("A and B must both be dumps or both be directories")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    c.add_argument("--display", type=int, default=None, help="the display mode of the frames (region counts)")
    c.add_argument("--regions-header", default=DEFAULT_HEADER, help="proxy/frame_regions.h")
    c.add_argument("--rects", type=int, default=16, help="at most this many areas per dump")
    c.add_argument("--palette", type=int, default=16, help="at most this many palette entries per dump")
    args = parser.parse_args(argv)
    try:
        table = region_table(args.regions_header) if args.display is not None else None
        report, identical = compare_paths(args.a, args.b, args.display, table, max(args.rects, 0), max(args.palette, 0))
    except (OSError, ValueError) as e:
        print("frame_dumps: %s" % e, file=sys.stderr)
        return 2
    report["identical"] = identical
    print(json.dumps(report, sort_keys=True))
    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())
