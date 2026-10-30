"""frame_dumps.py on synthetic ES2F dumps written here; no game data."""
import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import frame_dumps as fd

HEADER = Path(__file__).resolve().parents[2] / "proxy" / "frame_regions.h"


def grey():
    return [(i, i, i) for i in range(256)]


def dump_bytes(w, h, pixels, palette=None, frame=7, site=0x45fbe4, ordinal=0, uploads=3):
    palette = palette or grey()
    return (b"ES2F" + struct.pack("<7I", 1, frame, site, ordinal, w, h, uploads)
            + b"".join(bytes(c) for c in palette) + bytes(pixels))


def write(directory, name, data):
    path = os.path.join(directory, name)
    with open(path, "wb") as f:
        f.write(data)
    return path


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = fd.main(argv)
    return code, (json.loads(out.getvalue()) if out.getvalue() else None)


class FrameDumpsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_identical(self):
        p = [i % 251 for i in range(64 * 48)]
        a = write(self.dir, "a.es2f", dump_bytes(64, 48, p))
        b = write(self.dir, "b.es2f", dump_bytes(64, 48, p))
        code, report = run(["compare", a, b])
        self.assertEqual(code, 0)
        self.assertTrue(report["identical"])
        self.assertEqual(report["different"], [])

    def test_areas_bbox_first_same_colour(self):
        w, h = 32, 16
        pa = [5] * (w * h)
        pb = list(pa)
        # area 1: a 3 x 2 block at (4, 2); area 2: an L of 4 pixels at (20, 10); one pixel joined diagonally
        # to area 2 stays its own area (4-neighbours only)
        for y in (2, 3):
            for x in (4, 5, 6):
                pb[y * w + x] = 9
        for x, y in ((20, 10), (21, 10), (22, 10), (20, 11)):
            pb[y * w + x] = 9
        pb[12 * w + 21] = 9
        pal_b = grey()
        pal_b[9] = (5, 5, 5)          # index 9 in B shows A's colour 5: "same_colour"
        pal_b[200] = (1, 2, 3)        # an unused entry that differs
        a = write(self.dir, "a.es2f", dump_bytes(w, h, pa))
        b = write(self.dir, "b.es2f", dump_bytes(w, h, pb, palette=pal_b, uploads=4))
        code, report = run(["compare", a, b])
        self.assertEqual(code, 1)
        one = report["different"][0]
        self.assertEqual(one["header"], {"uploads": [3, 4]})
        self.assertEqual(one["palette"]["count"], 2)
        self.assertEqual([e["index"] for e in one["palette"]["entries"]], [9, 200])
        px = one["pixels"]
        self.assertEqual(px["count"], 11)
        self.assertEqual(px["same_colour"], 11)
        self.assertEqual(px["first"], {"x": 4, "y": 2, "a": 5, "b": 9})
        self.assertEqual(px["bbox"], [4, 2, 23, 13])
        self.assertEqual(px["areas"], 3)
        self.assertEqual(px["rects"][0], {"rect": [4, 2, 7, 4], "pixels": 6})
        self.assertEqual(px["rects"][1], {"rect": [20, 10, 23, 12], "pixels": 4})
        self.assertEqual(px["rects"][2], {"rect": [21, 12, 22, 13], "pixels": 1})
        code, report = run(["compare", a, b, "--rects", "1", "--palette", "0"])
        self.assertEqual(len(report["different"][0]["pixels"]["rects"]), 1)
        self.assertEqual(report["different"][0]["palette"]["entries"], [])

    def test_u_shape_is_one_area(self):
        w, h = 8, 4
        pa = [0] * (w * h)
        pb = list(pa)
        for x, y in ((1, 0), (1, 1), (1, 2), (2, 2), (3, 2), (3, 1), (3, 0)):
            pb[y * w + x] = 1
        a = write(self.dir, "a.es2f", dump_bytes(w, h, pa))
        b = write(self.dir, "b.es2f", dump_bytes(w, h, pb))
        _, report = run(["compare", a, b])
        px = report["different"][0]["pixels"]
        self.assertEqual(px["areas"], 1)
        self.assertEqual(px["rects"], [{"rect": [1, 0, 4, 3], "pixels": 7}])

    def test_regions_mode4_and_mode0(self):
        # 640 x 480, mode 4: rows 0..389 view, 390..479 panel
        w, h = 640, 480
        pa = bytes(w * h)
        pb = bytearray(pa)
        pb[100 * w + 10] = 1          # view
        pb[400 * w + 10] = 1          # panel
        pb[400 * w + 11] = 1
        a = write(self.dir, "a.es2f", dump_bytes(w, h, pa))
        b = write(self.dir, "b.es2f", dump_bytes(w, h, bytes(pb)))
        _, report = run(["compare", a, b, "--display", "4", "--regions-header", str(HEADER)])
        px = report["different"][0]["pixels"]
        self.assertEqual(px["display"], 4)
        self.assertEqual(px["regions"], {"view": 1, "hud": 0, "panel": 2, "mfd": 0, "rest": 0})
        # mode 0: (400, 300) lies in the MFD rectangle (318, 296, 562, 434), which wins over the panel
        pb = bytearray(pa)
        pb[300 * w + 400] = 1
        pb[470 * w + 10] = 1          # panel
        pb[10 * w + 10] = 1           # view
        b = write(self.dir, "b.es2f", dump_bytes(w, h, bytes(pb)))
        _, report = run(["compare", a, b, "--display", "0", "--regions-header", str(HEADER)])
        self.assertEqual(report["different"][0]["pixels"]["regions"], {"view": 1, "hud": 0, "panel": 1, "mfd": 1, "rest": 0})
        # an unknown mode: everything is rest
        _, report = run(["compare", a, b, "--display", "-1", "--regions-header", str(HEADER)])
        self.assertEqual(report["different"][0]["pixels"]["regions"]["rest"], 3)

    def test_region_table_scaling(self):
        table = fd.region_table(str(HEADER))
        self.assertIn(0, table)
        self.assertEqual(table[2], table[3])
        labels = fd.region_labels(table, 4, 16, 8)   # rows 0..5 view (390 * 8 // 480 = 6), 6..7 panel
        self.assertEqual(set(labels[:6 * 16]), {0})
        self.assertEqual(set(labels[6 * 16:]), {2})

    def test_size_difference_and_directories(self):
        da, db = os.path.join(self.dir, "a"), os.path.join(self.dir, "b")
        os.makedirs(da)
        os.makedirs(db)
        same = dump_bytes(4, 2, [1] * 8)
        write(da, "frame-000007-45fbe4-0.es2f", same)
        write(db, "frame-000007-45fbe4-0.es2f", same)
        write(da, "frame-000008-45fbe4-0.es2f", dump_bytes(4, 2, [1] * 8, frame=8))
        write(db, "frame-000008-45fbe4-0.es2f", dump_bytes(2, 4, [1] * 8, frame=8))
        write(da, "frame-000009-45fbe4-0.es2f", same)
        write(db, "frame-000010-45fbe4-0.es2f", same)
        write(db, "notes.txt", b"x")
        code, report = run(["compare", da, db])
        self.assertEqual(code, 1)
        self.assertEqual(report["compared"], 2)
        self.assertEqual(report["only_a"], ["frame-000009-45fbe4-0.es2f"])
        self.assertEqual(report["only_b"], ["frame-000010-45fbe4-0.es2f"])
        self.assertEqual(len(report["different"]), 1)
        one = report["different"][0]
        self.assertEqual(one["dump"], "frame-000008-45fbe4-0.es2f")
        self.assertEqual(one["header"], {"width": [4, 2], "height": [2, 4]})
        self.assertEqual(one["pixels"], {"compared": False})

    def test_unusable(self):
        a = write(self.dir, "a.es2f", dump_bytes(4, 2, [1] * 8))
        bad = write(self.dir, "bad.es2f", dump_bytes(4, 2, [1] * 8)[:-1])
        wrong = write(self.dir, "wrong.es2f", b"ES2X" + dump_bytes(4, 2, [1] * 8)[4:])
        self.assertEqual(run(["compare", a, bad])[0], 2)
        self.assertEqual(run(["compare", a, wrong])[0], 2)
        self.assertEqual(run(["compare", a, self.dir])[0], 2)
        self.assertEqual(run(["compare", a, os.path.join(self.dir, "missing.es2f")])[0], 2)

    def test_reads_what_es2sim_and_the_proxy_write(self):
        # the header layout of presenter.h pr_dump / present.c dump_write: version, frame, site, ordinal, w, h, uploads
        d = fd.read_dump(write(self.dir, "x.es2f", dump_bytes(3, 1, [7, 8, 9], frame=1091, site=0x45fbe4, ordinal=2, uploads=17)))
        self.assertEqual((d["frame"], d["site"], d["ordinal"], d["width"], d["height"], d["uploads"]), (1091, 0x45fbe4, 2, 3, 1, 17))
        self.assertEqual(d["pixels"], bytes([7, 8, 9]))


if __name__ == "__main__":
    unittest.main()
