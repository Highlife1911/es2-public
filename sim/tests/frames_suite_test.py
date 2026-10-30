"""frames_suite.py and frames_known.py on synthetic captures (no game data).

  frames_suite_test.py <dbsim_frames.py> <oracle-suites.json>
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frames_known  # noqa: E402
import frames_suite  # noqa: E402

FRAMES_SCRIPT = REGISTRY = None
MAIN = "0x45fbe4"


def h(seed, digits=64):
    return ("%064x" % (seed * 0x9e3779b97f4a7c15 % (1 << 256)))[-digits:]


def record(frame, site=MAIN, ordinal=0, index=1, view=1, panel=1, display=0):
    return {"frame": frame, "site": site, "ordinal": ordinal, "mode": 0, "S": [0, 0], "R": [0, 0, 639, 479],
            "client": [0, 0, 640, 480], "src": [0, 0, 640, 480], "dst": [0, 0, 640, 480], "source": "dib",
            "index_sha256": h(index), "palette_sha256": h(7), "uploads": 1, "presented": "headless",
            "display": display, "regions": {"view": h(view, 16), "hud": h(2, 16), "panel": h(panel, 16), "mfd": h(3, 16), "rest": None}}


def records():
    main = [record(f, index=10 + f, view=20 + f, panel=30 + f) for f in range(1, 9)]
    return main[:5] + [record(5, site="0x45490b", index=99)] + main[5:]   # frame 5 also presents at a modal site


def write_capture(path, recs, regions="t66-1"):
    start = {"event": "start", "schema": 1, "key": "frame_counter_0x4d257b", "presenter": "headless", "dump_frames": 0, "pid": 0}
    if regions:
        start["regions"] = regions
    with open(path, "w", encoding="ascii", newline="") as f:
        f.write(json.dumps(start) + "\r\n")
        for r in recs:
            f.write(json.dumps(r) + "\r\n")
        f.write(json.dumps({"event": "stats", "frame": recs[-1]["frame"], "records": len(recs)}) + "\r\n")
    return path


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = frames_suite.main(argv)
    return code, out.getvalue()


class FramesSuiteTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.df = frames_suite.load_module("dbsim_frames_test", FRAMES_SCRIPT)
        self.reference = os.path.join(self.dir, "ref", "s")
        self.df.build(write_capture(os.path.join(self.dir, "ref.ndjson"), records()), self.reference, "s")

    def capture(self, name, recs, regions="t66-1"):
        return write_capture(os.path.join(self.dir, name), recs, regions)

    def report(self, recs, exclude=()):
        return self.df.regions(self.reference, self.capture("c.ndjson", recs), exclude)

    def row(self, kind, first, cause="H5-20", **kw):
        return dict({"suite": "s", "kind": kind, "first": first, "cause": cause, "note": "n"}, **kw)

    def changed(self):
        recs = records()
        recs[3] = record(4, index=1000, view=1000, panel=34)   # frame 4: the view
        recs[7] = record(7, index=1001, view=27, panel=1001)   # frame 7: the panel
        return recs

    def test_identical_passes(self):
        self.assertEqual(frames_suite.check(self.report(records()), []), (True, []))

    def test_listed_kinds(self):
        report = self.report(self.changed())
        self.assertEqual(list(report["kinds"]), ["frame", "view", "panel"])
        rows = [self.row("frame", 4), self.row("view", 4, "HD-14"), self.row("panel", 7, "PR-03")]
        passed, lines = frames_suite.check(report, rows)
        self.assertTrue(passed, lines)
        self.assertEqual(len(lines), 3)
        # unlisted, earlier, later, gone
        self.assertFalse(frames_suite.check(report, rows[:2])[0])
        self.assertIn("FAIL earlier", "\n".join(frames_suite.check(report, rows[:2] + [self.row("panel", 8)])[1]))
        self.assertIn("FAIL later", "\n".join(frames_suite.check(report, rows[:2] + [self.row("panel", 6)])[1]))
        passed, lines = frames_suite.check(report, rows + [self.row("hud", 2, "HD-13")])
        self.assertFalse(passed)
        self.assertIn("drop the row", "\n".join(lines))

    def test_original_varies(self):
        recs = records()
        recs[5] = record(5, site="0x45490b", index=98)    # the other site of frame 5
        rows = [self.row("other_sites", None, frames_known.ORIGINAL_VARIES, exclude=[5])]
        report = self.report(recs)
        self.assertEqual(list(report["kinds"]), ["other_sites"])
        self.assertFalse(frames_suite.check(report, rows)[0])          # without the exclusion: unlisted
        report = self.report(recs, frames_suite.excluded_frames(rows))
        self.assertEqual(frames_suite.check(report, rows), (True, []))
        gone = self.row("palette", 3, frames_known.ORIGINAL_VARIES)  # gone and moved: never a failure
        self.assertTrue(frames_suite.check(self.report(records()), rows + [gone])[0])
        with self.assertRaises(ValueError):
            frames_suite.excluded_frames([self.row("other_sites", None, "H5-20", exclude=[5])])
        with self.assertRaises(ValueError):
            frames_suite.check(report, [self.row("view", 1, "maybe")])
        with self.assertRaises(ValueError):
            frames_suite.check(report, [self.row("view", 1), self.row("view", 2)])

    def test_main_with_capture(self):
        good = self.capture("good.ndjson", records())
        code, text = run(["--frames-script", FRAMES_SCRIPT, "--suite", "s", "--reference", self.reference, "--capture", good])
        self.assertEqual(code, 0, text)
        self.assertIn("frames s: PASS", text)
        bad = self.capture("bad.ndjson", self.changed())
        code, text = run(["--frames-script", FRAMES_SCRIPT, "--suite", "s", "--reference", self.reference, "--capture", bad, "--suggest"])
        self.assertEqual(code, 1, text)
        self.assertIn("FAIL unlisted difference: view from frame 4", text)
        suggested = json.loads(text[text.index("\n[") + 1:text.rindex("]") + 1])
        self.assertEqual([(r["kind"], r["first"], r["cause"]) for r in suggested], [("frame", 4, "?"), ("view", 4, "?"), ("panel", 7, "?")])
        # No region table on one side: whole-frame kinds only.
        plain = self.capture("plain.ndjson", self.changed(), regions=None)
        code, text = run(["--frames-script", FRAMES_SCRIPT, "--suite", "s", "--reference", self.reference, "--capture", plain])
        self.assertIn("(regions not compared)", text)
        self.assertNotIn("view from", text)

    def test_skip_without_reference(self):
        saved = {k: os.environ.pop(k, None) for k in ("ES2_DATA_ROOT", "ES2_FRAMES_REFERENCE")}
        try:
            code, text = run(["--frames-script", FRAMES_SCRIPT, "--suite", "s"])
            self.assertEqual(code, 0)
            self.assertIn("SKIP", text)
            os.environ["ES2_FRAMES_REFERENCE"] = os.path.join(self.dir, "ref")
            code, text = run(["--frames-script", FRAMES_SCRIPT, "--suite", "s", "--capture", self.capture("g.ndjson", records())])
            self.assertEqual(code, 0, text)
        finally:
            os.environ.pop("ES2_FRAMES_REFERENCE", None)
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v
        self.assertEqual(frames_suite.es2_data_root(r"D:\x\es2-data\extracted\cd"), os.path.normpath(r"D:\x\es2-data"))
        self.assertEqual(frames_suite.es2_data_root(r"D:\x\es2\build\extracted\cd", r"D:\x\es2-data"),
                         os.path.normpath(r"D:\x\es2-data"))


class KnownListTest(unittest.TestCase):
    def test_rows(self):
        with open(REGISTRY, encoding="utf-8") as f:
            order = [s["name"] for s in json.load(f)["suites"]]
        suites = set(order)
        df = frames_suite.load_module("dbsim_frames_known", FRAMES_SCRIPT)
        seen = set()
        for row in frames_known.KNOWN:
            self.assertEqual(set(row) - {"exclude"}, {"suite", "kind", "first", "cause", "note"}, row)
            self.assertIn(row["suite"], suites)
            self.assertIn(row["kind"], df.KINDS)
            self.assertIn(row["cause"], frames_known.CAUSES)
            self.assertTrue(row["note"])
            self.assertNotIn((row["suite"], row["kind"]), seen)
            seen.add((row["suite"], row["kind"]))
            self.assertTrue(row["first"] is None or isinstance(row["first"], int))
            if row["first"] is None:
                self.assertTrue(row.get("exclude"), row)
            if row.get("exclude"):
                self.assertEqual(row["cause"], frames_known.ORIGINAL_VARIES)
        # Sorted by suite in registry order, then by kind in KINDS order.
        keys = [(order.index(r["suite"]), df.KINDS.index(r["kind"])) for r in frames_known.KNOWN]
        self.assertEqual(keys, sorted(keys))


if __name__ == "__main__":
    FRAMES_SCRIPT, REGISTRY = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    unittest.main(argv=sys.argv[:1])
