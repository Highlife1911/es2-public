"""vshell_present_times.py on synthetic VSHELL capture records of both versions; no game data."""
import contextlib
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import vshell_present_times as vp

FREQ = 10_000_000          # counts per second
MS = FREQ // 1000          # counts per millisecond
BASE = 5_000_000_000


def h(n):
    return hashlib.sha256(b"frame%d" % n).hexdigest()


def start(schema):
    r = {"kind": "start", "seq": 0, "pass": 0, "step": 0, "screen": "none", "state": "", "schema": schema, "pid": 1}
    if schema == 2:
        r["qpc"] = BASE
        r["qpc_frequency"] = FREQ
    return r


def frame(seq, step, kind, presents, index, at_ms=None):
    r = {"kind": kind, "seq": seq, "pass": seq, "step": step, "screen": "s", "state": "t", "site": "0x4a0000",
         "dst": [0, 0, 640, 400], "presents": presents, "uploads": 1, "dd": 1, "focus": 1,
         "index_sha256": h(index), "palette_sha256": h(99)}
    if at_ms is not None:
        r["qpc"] = BASE + int(at_ms * MS)
    return r


# Steps 34 and 35: the screen change (3 records), a long hold of the full frame, then one record per tick.
SHAPE = [(33, "present", 2, 1, 0.0), (34, "present", 1, 2, 5.0), (34, "present", 1, 3, 9.0),
         (34, "present", 1, 4, 12.0), (35, "present", 48, 5, 12.0 + 976.0),
         (35, "present", 4, 6, 12.0 + 976.0 + 16.0), (35, "present", 3, 7, 12.0 + 976.0 + 32.0),
         (35, "present", 5, 8, 12.0 + 976.0 + 48.0), (36, "snap", 0, 8, 12.0 + 976.0 + 49.0)]


def records(schema, scale=1.0):
    out = [start(schema)]
    for seq, (step, kind, presents, index, at) in enumerate(SHAPE, start=1):
        out.append(frame(seq, step, kind, presents, index, at * scale if schema == 2 else None))
    return out


def text(recs, tear=False):
    lines = [json.dumps(r, separators=(",", ":")) for r in recs]
    if tear:
        lines.append('{"kind":"present","seq":99,"pa')
    return "\r\n".join(lines) + "\r\n"


class VersionOneTest(unittest.TestCase):
    def test_reads_without_times(self):
        report = vp.analyse(records(1), (34, 35))
        self.assertEqual(report["schema"], 1)
        self.assertFalse(report["timed"])
        self.assertEqual(report["verdict"], "no-times")
        self.assertIn("qpc=1", report["reason"])
        self.assertEqual(len(report["rows"]), 7)
        self.assertEqual(report["presents"], 1 + 1 + 1 + 48 + 4 + 3 + 5)
        self.assertTrue(all(r["gap_ms"] is None and r["hold_ms"] is None for r in report["rows"]))

    def test_start_without_schema_is_version_one(self):
        recs = records(1)
        del recs[0]["schema"]
        self.assertEqual(vp.schema_of(recs), 1)

    def test_no_start_record_is_version_one(self):
        self.assertEqual(vp.schema_of(records(1)[1:]), 1)

    def test_format_names_the_missing_times(self):
        self.assertIn("verdict: no-times", vp.format_report(vp.analyse(records(1), (34, 35))))


class VersionTwoTest(unittest.TestCase):
    def test_gaps_per_present_and_holds(self):
        report = vp.analyse(records(2), (34, 35))
        self.assertEqual(report["schema"], 2)
        self.assertTrue(report["timed"])
        self.assertEqual(report["qpc_frequency"], FREQ)
        rows = {r["seq"]: r for r in report["rows"]}
        # seq 5 is the 48-present record after the hold: 976 ms since the full frame (seq 4).
        self.assertAlmostEqual(rows[5]["gap_ms"], 976.0, places=3)
        self.assertAlmostEqual(rows[5]["per_present_ms"], 976.0 / 48, places=3)
        self.assertAlmostEqual(rows[4]["hold_ms"], 976.0, places=3)
        self.assertAlmostEqual(rows[6]["gap_ms"], 16.0, places=3)
        self.assertAlmostEqual(rows[6]["per_present_ms"], 4.0, places=3)
        self.assertEqual(report["hold"]["seq"], 4)
        self.assertAlmostEqual(report["hold"]["hold_ticks"], 61.0, places=3)

    def test_first_record_of_the_range_has_a_gap_to_the_one_before(self):
        rows = vp.analyse(records(2), (34, 35))["rows"]
        self.assertEqual(rows[0]["seq"], 2)
        self.assertAlmostEqual(rows[0]["gap_ms"], 5.0, places=3)

    def test_whole_file_without_a_range(self):
        report = vp.analyse(records(2))
        self.assertEqual(len(report["rows"]), 9)
        self.assertIsNone(report["rows"][0]["gap_ms"])   # nothing before the first frame record

    def test_hold_hash_selects_a_record(self):
        report = vp.analyse(records(2), (34, 35), model_ticks=61, hold_hash=h(6)[:8])
        self.assertEqual(report["hold"]["seq"], 6)
        self.assertAlmostEqual(report["hold"]["hold_ms"], 16.0, places=3)
        self.assertEqual(report["verdict"], "no-wait")
        self.assertEqual(vp.analyse(records(2), (34, 35), model_ticks=61, hold_hash="deadbeef")["verdict"], "no-hold")

    def test_unknown_frequency_or_missing_stamp_is_refused(self):
        recs = records(2)
        del recs[0]["qpc_frequency"]
        with self.assertRaises(vp.CaptureError):
            vp.analyse(recs, (34, 35))
        recs = records(2)
        del recs[4]["qpc"]
        with self.assertRaises(vp.CaptureError):
            vp.analyse(recs, (34, 35))

    def test_decreasing_stamps_are_refused(self):
        recs = records(2)
        recs[5]["qpc"] = recs[3]["qpc"] - 1
        with self.assertRaises(vp.CaptureError):
            vp.analyse(recs, (34, 35))


class VerdictTest(unittest.TestCase):
    def verdict(self, scale):
        return vp.analyse(records(2, scale), (34, 35), model_ticks=61)

    def test_waits(self):
        report = self.verdict(1.0)
        self.assertEqual(report["verdict"], "waits")
        self.assertAlmostEqual(report["ratio"], 1.0, places=3)
        self.assertAlmostEqual(report["model_ms"], 976.0)

    def test_no_wait(self):
        # The same shape in a tenth of the time: the hold is 98 ms of the model's 976.
        self.assertEqual(self.verdict(0.1)["verdict"], "no-wait")

    def test_inconclusive(self):
        self.assertEqual(self.verdict(0.5)["verdict"], "inconclusive")

    def test_boundaries(self):
        def with_hold(ms):
            recs = records(2)
            shift = int((ms - 976.0) * MS)
            for r in recs[5:]:
                r["qpc"] += shift
            return vp.analyse(recs, (34, 35), model_ticks=61)["verdict"]
        self.assertEqual(with_hold(732.0), "waits")
        self.assertEqual(with_hold(731.0), "inconclusive")
        self.assertEqual(with_hold(244.0), "no-wait")
        self.assertEqual(with_hold(245.0), "inconclusive")

    def test_no_model_leaves_the_verdict_open(self):
        report = vp.analyse(records(2), (34, 35))
        self.assertEqual(report["verdict"], "unjudged")
        self.assertIn("hold", report)

    def test_median_time_per_present_elsewhere(self):
        report = vp.analyse(records(2), (34, 35), model_ticks=61)
        self.assertIn("median_per_present_ms", report)


class FilesTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, content):
        path = os.path.join(self.dir.name, name)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(content)
        return path

    def test_jsonl_both_versions_and_a_torn_last_line(self):
        for schema in (1, 2):
            path = self.write("capture%d.jsonl" % schema, text(records(schema), tear=True))
            report = vp.analyse(vp.load(path), (34, 35))
            self.assertEqual(report["schema"], schema)
            self.assertEqual(report["timed"], schema == 2)
            self.assertEqual(len(report["rows"]), 7)

    def frames_json(self, schema):
        recs = records(schema)
        run = {"name": "a", "capture_start": recs[0], "frames": [
            {k: r[k] for k in ("seq", "kind", "pass", "step", "screen", "state", "index_sha256", "palette_sha256") + (("qpc",) if "qpc" in r else ())}
            for r in recs[1:]]}
        return json.dumps({"schema": 1, "kind": "es2-vshell-screens", "runs": [run]})

    def test_frames_json_both_versions(self):
        for schema in (1, 2):
            path = self.write("frames%d.json" % schema, self.frames_json(schema))
            report = vp.analyse(vp.load(path), (34, 35), model_ticks=61)
            self.assertEqual(report["schema"], schema)
            self.assertEqual(report["timed"], schema == 2)
            if schema == 2:
                self.assertEqual(report["verdict"], "waits")
                self.assertEqual(report["hold"]["seq"], 4)

    def test_frames_json_run_choice(self):
        doc = json.loads(self.frames_json(2))
        doc["runs"].append(dict(doc["runs"][0], name="b"))
        path = self.write("two.json", json.dumps(doc))
        with self.assertRaises(vp.CaptureError):
            vp.load(path)
        self.assertEqual(len(vp.load(path, "b")), len(vp.load(path, "a")))
        with self.assertRaises(vp.CaptureError):
            vp.load(path, "c")

    def test_unknown_version_is_refused(self):
        recs = records(2)
        recs[0]["schema"] = 3
        with self.assertRaises(vp.CaptureError):
            vp.schema_of(recs)

    def test_main(self):
        path = self.write("c.jsonl", text(records(2)))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = vp.main([path, "--steps", "34", "35", "--model-ticks", "61"])
        self.assertEqual(code, 0)
        self.assertIn("verdict: waits", out.getvalue())
        self.assertIn("longest hold: record 4", out.getvalue())
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(vp.main([path, "--steps", "34", "35", "--model-ticks", "61", "--json"]), 0)
        self.assertEqual(json.loads(out.getvalue())["verdict"], "waits")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(vp.main([os.path.join(self.dir.name, "missing.jsonl")]), 2)
        self.assertIn("vshell_present_times", err.getvalue())

    def test_main_version_one(self):
        path = self.write("c1.jsonl", text(records(1)))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(vp.main([path, "--steps", "34", "35"]), 0)
        self.assertIn("verdict: no-times", out.getvalue())


if __name__ == "__main__":
    unittest.main()
