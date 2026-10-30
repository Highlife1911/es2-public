"""dbsim_present_times.py on synthetic DBSIM capture records of both start schemas; no game data."""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dbsim_present_times as dp

FREQ = 10_000_000          # counts per second
MS = FREQ // 1000          # counts per millisecond
BASE = 7_000_000_000
MAIN = "0x45fbe4"
SLIDE = "0x42bb84"

# (frame, site, ordinal, at ms). Frame 2 shows a slide before its main-loop present, so the first record of the
# frame and the main-loop present differ; the counter jumps from 4 to 6 (an irregular step); frame 3 takes 55 ms.
SHAPE = [(1, MAIN, 0, 0.0), (2, SLIDE, 0, 35.0), (2, MAIN, 0, 40.0), (3, MAIN, 0, 80.0), (4, MAIN, 0, 135.0),
         (6, MAIN, 0, 175.0), (7, MAIN, 0, 215.0)]


def start(schema):
    r = {"event": "start", "schema": schema, "key": "frame_counter_0x4d257b", "presenter": "original",
         "dump_frames": 0, "pid": 4242}
    if schema == 2:
        r["qpc_frequency"] = FREQ
    return r


def record(frame, site, ordinal, at, schema):
    r = {"frame": frame, "site": site, "ordinal": ordinal, "mode": 1, "S": [0, 0], "R": [640, 480],
         "client": [640, 480], "src": [0, 0, 640, 480], "dst": [0, 0, 640, 480], "source": "dib",
         "index_sha256": "%064x" % (frame * 16 + ordinal), "palette_sha256": "%064x" % 3, "uploads": 1,
         "presented": "original"}
    if schema == 2:
        r["qpc"] = BASE + int(at * MS)
    return r


def stats(count):
    return {"event": "stats", "frame": 7, "records": count, "presents": count, "qpc_frequency": FREQ}


def records(schema, shape=SHAPE):
    return [dict(record(f, s, o, at, schema), site_value=int(s, 16)) for f, s, o, at in shape]


def text(schema, shape=SHAPE, tear=False, closed=True):
    lines = [start(schema)] + [record(f, s, o, at, schema) for f, s, o, at in shape]
    if closed:
        lines.append(stats(len(shape)))
    out = "\r\n".join(json.dumps(r, separators=(",", ":")) for r in lines) + "\r\n"
    return out + ('{"frame":9,"si' if tear else "")


def run(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = dp.main(argv)
    return code, out.getvalue(), err.getvalue()


class VersionOneTest(unittest.TestCase):
    def test_a_schema_1_capture_has_no_times(self):
        report = dp.analyse(records(1), start(1))
        self.assertEqual(report["schema"], 1)
        self.assertFalse(report["timed"])
        self.assertEqual(report["records"], 7)
        self.assertIn("capture_qpc=1", report["reason"])
        self.assertNotIn("ticks", report)

    def test_the_text_names_the_missing_times(self):
        self.assertIn("no times:", dp.format_report(dp.analyse(records(1), start(1))))


class VersionTwoTest(unittest.TestCase):
    def setUp(self):
        self.report = dp.analyse(records(2), start(2))

    def test_present_gaps(self):
        r = self.report
        self.assertTrue(r["timed"])
        self.assertEqual(r["qpc_frequency"], FREQ)
        self.assertEqual(r["present_gap_ms"]["count"], 6)
        self.assertAlmostEqual(r["present_gap_ms"]["min"], 5.0, places=6)
        self.assertAlmostEqual(r["present_gap_ms"]["max"], 55.0, places=6)
        self.assertAlmostEqual(r["present_gap_ms"]["median"], 40.0, places=6)
        self.assertAlmostEqual(r["present_gap_ms"]["mean"], 215.0 / 6, places=6)
        self.assertAlmostEqual(r["span_ms"], 215.0, places=6)
        self.assertEqual(r["main_loop_gap_ms"]["count"], 5)
        self.assertAlmostEqual(r["main_loop_gap_ms"]["min"], 40.0, places=6)

    def test_ticks_from_the_first_record_of_each_frame(self):
        t = self.report["ticks"]
        self.assertEqual(t["steps"], 4)
        self.assertEqual(t["irregular_steps"], 1)   # 4 -> 6
        self.assertAlmostEqual(t["ms"]["min"], 35.0, places=6)    # the slide counts as the frame's first record
        self.assertAlmostEqual(t["ms"]["max"], 55.0, places=6)
        self.assertEqual(t["over_budget"], 2)   # 45 and 55 ms
        self.assertAlmostEqual(t["ticks_per_second"], 1000.0 * 4 / 175.0, places=6)

    def test_ticks_from_the_main_loop_presents(self):
        t = self.report["main_loop_ticks"]
        self.assertEqual(t["steps"], 4)
        self.assertEqual(t["irregular_steps"], 1)
        self.assertAlmostEqual(t["ms"]["min"], 40.0, places=6)
        self.assertEqual(t["over_budget"], 1)   # 55 ms only

    def test_presents_per_frame_and_slowest_ticks(self):
        self.assertEqual(self.report["frame_runs"], 6)
        self.assertEqual(self.report["presents_per_frame"]["max"], 2)
        slow = self.report["slowest_ticks"]
        self.assertEqual(slow[0]["frame"], 3)
        self.assertAlmostEqual(slow[0]["ms"], 55.0, places=6)
        self.assertEqual([s["frame"] for s in slow], [3, 2, 6, 1])

    def test_budget_and_top_options(self):
        report = dp.analyse(records(2), start(2), budget_ms=50.0, top=2)
        self.assertEqual(report["ticks"]["over_budget"], 1)
        self.assertEqual(len(report["slowest_ticks"]), 2)

    def test_frame_range(self):
        report = dp.analyse(records(2), start(2), frames=(2, 4))
        self.assertEqual(report["records_in_range"], 4)
        self.assertEqual((report["first_frame"], report["last_frame"]), (2, 4))
        self.assertEqual(report["ticks"]["steps"], 2)
        self.assertEqual(report["ticks"]["irregular_steps"], 0)
        self.assertEqual(dp.analyse(records(2), start(2), frames=(50, 60))["reason"], "no record in the range")

    def test_the_stamp_is_in_counts_of_the_given_frequency(self):
        slow = dp.analyse(records(2), dict(start(2), qpc_frequency=FREQ // 2))
        self.assertAlmostEqual(slow["span_ms"], 430.0, places=6)

    def test_bad_input_is_refused(self):
        for bad in (dict(start(2), qpc_frequency=0), {k: v for k, v in start(2).items() if k != "qpc_frequency"},
                    dict(start(2), qpc_frequency="10000000")):
            with self.assertRaises(dp.CaptureError):
                dp.analyse(records(2), bad)
        recs = records(2)
        del recs[3]["qpc"]
        with self.assertRaises(dp.CaptureError):
            dp.analyse(recs, start(2))
        recs = records(2)
        recs[4]["qpc"] = recs[2]["qpc"] - 1
        with self.assertRaises(dp.CaptureError):
            dp.analyse(recs, start(2))

    def test_the_text_report(self):
        out = dp.format_report(self.report)
        self.assertIn("present gap, main loop", out)
        self.assertIn("tick, main loop", out)
        self.assertIn("slowest ticks: frame 3 55.0 ms", out)
        self.assertIn("irregular steps left out: 1", out)


class FileTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, content):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(content.encode("ascii"))
        return path

    def test_schema_2_file(self):
        code, out, err = run([self.write("capture.ndjson", text(2))])
        self.assertEqual((code, err), (0, ""))
        self.assertIn("capture schema 2, 7 record(s)", out)
        self.assertIn("55.0 ms", out)

    def test_schema_1_file_says_no_times(self):
        code, out, _ = run([self.write("capture.ndjson", text(1))])
        self.assertEqual(code, 0)
        self.assertIn("no times:", out)

    def test_a_manifest_directory_is_accepted(self):
        self.write("capture.ndjson", text(2))
        code, out, _ = run([self.tmp.name])
        self.assertEqual(code, 0)
        self.assertIn("qpc frequency 10000000", out)

    def test_a_torn_last_line_and_a_missing_stats_line_are_reported(self):
        code, out, _ = run([self.write("c.ndjson", text(2, tear=True, closed=False))])
        self.assertEqual(code, 0)
        self.assertIn("not closed", out)
        self.assertIn("1 torn line(s)", out)

    def test_json_output(self):
        code, out, _ = run([self.write("c.ndjson", text(2)), "--json", "--frames", "1", "4", "--budget-ms", "50"])
        self.assertEqual(code, 0)
        report = json.loads(out)
        self.assertEqual(report["frames_range"], [1, 4])
        self.assertEqual(report["ticks"]["budget_ms"], 50.0)
        self.assertTrue(report["closed"])

    def test_unusable_input_exits_2(self):
        code, _, err = run([os.path.join(self.tmp.name, "absent.ndjson")])
        self.assertEqual(code, 2)
        self.assertIn("dbsim_present_times:", err)
        bad = text(2).replace(',"qpc":%d' % (BASE + 40 * MS), "", 1)
        code, _, err = run([self.write("bad.ndjson", bad)])
        self.assertEqual(code, 2)
        self.assertIn("qpc", err)
        code, _, _ = run([self.write("nostart.ndjson", text(2).split("\r\n", 1)[1])])
        self.assertEqual(code, 2)
        code, _, _ = run([self.write("v3.ndjson", text(2).replace('"schema":2', '"schema":3', 1))])
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
