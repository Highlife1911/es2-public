"""presenter_capture.py on synthetic captures and frame dumps (the formats of
proxy/presenter.h); no game data. argv[1] is the presenter_offline binary,
which runs the DLL's conversion and scaler (presenter_frame.h)."""
import contextlib
import io
import json
import os
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import presenter_capture as pc

OFFLINE = None
START = {"event": "start", "schema": 1, "key": "frame_counter_0x4d257b", "presenter": "original", "dump_frames": 0}


def record(frame, site, ordinal=0, index="a" * 64, palette="b" * 64, page=(0, 0), presented="original"):
    return {"frame": frame, "site": "0x%06x" % site, "ordinal": ordinal, "mode": 0, "S": list(page),
            "R": [0, 0, 639, 479], "client": [0, 0, 640, 480], "src": [page[0], page[1], 640, 480],
            "dst": [0, 0, 640, 480], "source": "dib", "index_sha256": index, "palette_sha256": palette,
            "uploads": 3, "presented": presented}


def write_capture(directory, name, records, start=START, stats=True):
    path = os.path.join(directory, name)
    with open(path, "w", encoding="ascii", newline="") as f:
        f.write(json.dumps(start, separators=(",", ":")) + "\r\n")
        for i, r in enumerate(records):
            f.write(json.dumps(r, separators=(",", ":")) + "\r\n")
            if stats and i == 1:
                f.write('{"event":"stats","frame":%d,"capture_qpc":%d}\r\n' % (r["frame"], random.randrange(10**6)))
    return path


def write_dump(path, width, height, seed, frame=1):
    rnd = random.Random(seed)
    palette = bytes(rnd.randrange(256) for _ in range(768))
    pixels = bytes(rnd.randrange(256) for _ in range(width * height))
    with open(path, "wb") as f:
        f.write(b"ES2F" + struct.pack("<7I", 1, frame, pc.MAIN_SITE, 0, width, height, 2) + palette + pixels)
    return palette, pixels


def run_main(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = pc.main(argv)
    return code, out.getvalue()


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        slide = pc.SLIDE_CALLS[3] + 5
        self.base = [record(1, pc.MAIN_SITE), record(2, pc.MAIN_SITE, index="c" * 64),
                     record(2, slide), record(2, slide, ordinal=1, page=(0, 4)),
                     record(3, 0x45490B, index="d" * 64), record(3, 0x45490B, ordinal=1, index="d" * 64),
                     record(3, pc.MAIN_SITE, palette="e" * 64), record(4, pc.MAIN_SITE)]

    def compare(self, a, b, fields=None):
        pa, pb = write_capture(self.dir, "a.jsonl", a), write_capture(self.dir, "b.jsonl", b, stats=False)
        argv = ["compare", pa, pb] + (["--fields", fields] if fields else [])
        code, out = run_main(argv)
        return code, json.loads(out)

    def test_identical_ignores_events_and_presented(self):
        other = [dict(r, presented="gdi", client=[0, 0, 1920, 1080], dst=[0, 0, 1920, 1080]) for r in self.base]
        code, result = self.compare(self.base, other)
        self.assertEqual(code, 0)
        self.assertTrue(result["identical"])
        self.assertEqual(result["sequence_records"], [6, 6])
        self.assertEqual(result["compared_through_frame"], 4)

    def test_main_record_difference(self):
        changed = [dict(r) for r in self.base]
        changed[1]["index_sha256"] = "f" * 64
        code, result = self.compare(self.base, changed)
        self.assertEqual(code, 1)
        self.assertEqual(result["first_difference"][0]["kind"], "sequence")
        self.assertEqual(result["first_difference"][0]["index"], 1)

    def test_slide_page_origin_is_compared(self):
        changed = [dict(r) for r in self.base]
        changed[3]["S"] = [0, 5]
        self.assertEqual(self.compare(self.base, changed)[0], 1)

    def test_modal_records_compared_as_sets(self):
        fewer = [r for i, r in enumerate(self.base) if i != 5]   # one modal present less: same set
        code, result = self.compare(self.base, fewer)
        self.assertEqual(code, 0, result)
        changed = [dict(r) for r in self.base]
        changed[5]["index_sha256"] = "9" * 64
        code, result = self.compare(self.base, changed)
        self.assertEqual(code, 1)
        self.assertEqual(result["first_difference"][0]["kind"], "frame_set")
        self.assertEqual(result["first_difference"][0]["frame"], 3)

    def test_lost_last_frame_is_not_compared(self):
        code, result = self.compare(self.base, self.base[:-1])
        self.assertEqual(code, 0)
        self.assertEqual(result["compared_through_frame"], 3)

    def test_missing_record_inside_compared_range(self):
        self.assertEqual(self.compare(self.base, self.base[:1] + self.base[2:])[0], 1)

    def test_field_selection(self):
        changed = [dict(r) for r in self.base]
        changed[0]["mode"] = 1
        self.assertEqual(self.compare(self.base, changed)[0], 1)
        self.assertEqual(self.compare(self.base, changed, "frame,site,ordinal,index_sha256")[0], 0)

    def test_unusable_input(self):
        path = os.path.join(self.dir, "bad.jsonl")
        with open(path, "w") as f:
            f.write('{"frame":1}\n')
        self.assertEqual(run_main(["compare", path, path])[0], 2)

    def test_summary(self):
        path = write_capture(self.dir, "s.jsonl", self.base[:4] + [record(4, pc.MAIN_SITE, palette="e" * 64)])
        code, out = run_main(["summary", path])
        s = json.loads(out)
        self.assertEqual(code, 0)
        self.assertEqual(s["records"], 5)
        self.assertEqual(s["main_frames"], 3)
        self.assertEqual(s["main_frames_not_one_record"], [3])
        self.assertEqual(s["page_origin_moved_records"], 1)
        self.assertEqual(s["palette_changes"], 1)
        self.assertEqual(s["modes"], {"0": 5})
        self.assertEqual(s["last_stats"]["frame"], 2)


class LayoutTest(unittest.TestCase):
    def test_cases(self):
        self.assertEqual(pc.layout(640, 480, 1920, 1080), {"scale": 2, "x": 320, "y": 60, "width": 1280, "height": 960})
        self.assertEqual(pc.layout(640, 480, 640, 480)["scale"], 1)
        self.assertEqual(pc.layout(640, 480, 1024, 400), {"scale": 0, "x": 245, "y": 0, "width": 533, "height": 400})
        self.assertEqual(pc.layout(320, 240, 1366, 768)["x"], 203)
        with self.assertRaises(ValueError):
            pc.layout(640, 480, 0, 10)

    def test_one_to_one_is_table_lookup(self):
        with tempfile.TemporaryDirectory() as d:
            write_dump(os.path.join(d, "f.es2f"), 13, 7, 1)
            dump = pc.read_dump(os.path.join(d, "f.es2f"))
            image, placement = pc.reference_render(dump, 13, 7)
            self.assertEqual(image, pc.table_lookup(dump))
            self.assertEqual(placement["scale"], 1)

    def test_bad_dump(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "f.es2f")
            write_dump(path, 4, 4, 1)
            with open(path, "ab") as f:
                f.write(b"x")
            with self.assertRaises(ValueError):
                pc.read_dump(path)


class SessionsTest(unittest.TestCase):
    """One report per DBSIM process of an interactive session (capture_report with "{pid}")."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.pattern = os.path.join(self.dir, "cap.{pid}.ndjson")

    def report(self, pid, records, **extra):
        start = dict(START, pid=pid, **extra)
        return write_capture(self.dir, "cap.%d.ndjson" % pid, records, start=start, stats=False)

    def test_two_processes_listed_by_age(self):
        first = self.report(4100, [record(1, pc.MAIN_SITE), record(2, pc.MAIN_SITE)])
        second = self.report(5200, [record(7, pc.MAIN_SITE)], presenter="gdi")
        os.utime(first, (1000, 1000))
        os.utime(second, (2000, 2000))
        os.makedirs(os.path.join(self.dir, "cap.4100.ndjson.frames"))   # a dump directory is not a report
        code, out = run_main(["sessions", self.pattern])
        result = json.loads(out)
        self.assertEqual(code, 0)
        self.assertEqual(result["reports"], 2)
        a, b = result["sessions"]
        self.assertEqual((a["pid"], a["records"], a["first_frame"], a["last_frame"], a["presenter"]), (4100, 2, 1, 2, "original"))
        self.assertEqual((b["pid"], b["records"], b["first_frame"], b["last_frame"], b["presenter"]), (5200, 1, 7, 7, "gdi"))

    def test_pid_from_the_name_when_the_start_line_has_none(self):
        write_capture(self.dir, "cap.6300.ndjson", [record(1, pc.MAIN_SITE)])   # a capture without "pid"
        code, out = run_main(["sessions", self.pattern])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out)["sessions"][0]["pid"], 6300)

    def test_unusable_file_is_listed_not_fatal(self):
        self.report(4100, [record(1, pc.MAIN_SITE)])
        with open(os.path.join(self.dir, "cap.4200.ndjson"), "w") as f:
            f.write("{}\n")
        code, out = run_main(["sessions", self.pattern])
        entries = json.loads(out)["sessions"]
        self.assertEqual(code, 0)
        self.assertEqual(sorted("error" in e for e in entries), [False, True])

    def test_no_report(self):
        code, out = run_main(["sessions", self.pattern])
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(out)["reports"], 0)

    def test_each_report_summarizes_on_its_own(self):
        path = self.report(4100, [record(1, pc.MAIN_SITE), record(2, pc.MAIN_SITE)])
        code, out = run_main(["summary", path])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out)["records"], 2)


@unittest.skipUnless(OFFLINE or len(sys.argv) > 1, "presenter_offline path not given")
class OfflineTest(unittest.TestCase):
    """Pass 3 on synthetic dumps: the DLL's code against the reference rendering."""

    def test_check_dumps(self):
        with tempfile.TemporaryDirectory() as d:
            frames = os.path.join(d, "frames")
            os.mkdir(frames)
            write_dump(os.path.join(frames, "a.es2f"), 16, 12, 2)
            write_dump(os.path.join(frames, "b.es2f"), 37, 23, 3, frame=7)
            code, out = run_main(["check-dumps", frames, "--offline", OFFLINE, "--scratch", os.path.join(d, "s"),
                                  "--client", "40x30", "--client", "100x31", "--client", "9x50", "--client", "1x1",
                                  "--client", "160x120"])
            result = json.loads(out)
            self.assertEqual(code, 0, result)
            self.assertEqual(result["dumps"], 2)
            self.assertEqual(result["checks"], 12)
            self.assertEqual(result["failures"], 0)
            self.assertEqual({r["scale"] for r in result["results"]}, {0, 1, 2, 4, 10})

    def test_offline_reports_corrupt_dump(self):
        with tempfile.TemporaryDirectory() as d:
            frames = os.path.join(d, "frames")
            os.mkdir(frames)
            path = os.path.join(frames, "a.es2f")
            write_dump(path, 8, 8, 4)
            with open(path, "r+b") as f:
                f.seek(4)
                f.write(struct.pack("<I", 2))   # version 2: both readers refuse
            with self.assertRaises(ValueError):
                pc.check_dumps(frames, OFFLINE, [], os.path.join(d, "s"))


class QpcSchemaTest(unittest.TestCase):
    """A schema-2 capture (capture_qpc=1) is read and compared like a schema-1 one."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.base = [record(1, pc.MAIN_SITE), record(2, pc.MAIN_SITE, index="c" * 64), record(3, pc.MAIN_SITE)]
        self.start2 = dict(START, schema=2, qpc_frequency=10_000_000)

    def timed(self, name):
        records = [dict(r, qpc=1_000_000 * (i + 1)) for i, r in enumerate(self.base)]
        return write_capture(self.dir, name, records, start=self.start2)

    def test_load_accepts_both(self):
        plain = write_capture(self.dir, "a.jsonl", self.base)
        for path, schema in ((plain, 1), (self.timed("b.jsonl"), 2)):
            records, events = pc.load(path)
            self.assertEqual((len(records), events[0]["schema"]), (3, schema))
        path = write_capture(self.dir, "c.jsonl", self.base, start=dict(START, schema=3))
        with self.assertRaises(ValueError):
            pc.load(path)

    def test_compare_and_summary_do_not_see_the_stamp(self):
        plain = write_capture(self.dir, "a.jsonl", self.base)
        code, out = run_main(["compare", plain, self.timed("b.jsonl")])
        self.assertEqual(code, 0)
        self.assertTrue(json.loads(out)["identical"])
        code, out = run_main(["summary", self.timed("c.jsonl")])
        self.assertEqual((code, json.loads(out)["records"]), (0, 3))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        OFFLINE = sys.argv.pop(1)
    unittest.main()
