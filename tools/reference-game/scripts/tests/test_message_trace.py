"""message_trace.py on synthetic message traces and tick logs; no game data required."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import message_trace as mt

WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_TIMER = 0x100, 0x101, 0x102, 0x113


def frame_calls(f, calls):
    """Records of one frame. calls: "e" (an empty call) or (kind, m, w) with kind "i" (inject) or "m" (msg);
    the pump ordinal p is the call's index in the frame."""
    out, counts = [], {"empty": 0, "msgs": 0, "inject": 0}
    for p, call in enumerate(calls):
        if call == "e":
            counts["empty"] += 1
            continue
        kind, m, w = call
        out.append({"k": "inject" if kind == "i" else "msg", "f": f, "p": p, "m": m, "w": w, "l": 0, "h": 7,
                    "t": 0 if kind == "i" else 1000 + p, "c": "0x464ca2", "r": "0x464ca7"})
        counts["inject" if kind == "i" else "msgs"] += 1
    out.append(dict({"k": "frame", "f": f, "pumps": len(calls), "drop": 0, "noremove": 0}, **counts))
    return out


def modal(keys, passes, notices_at=(), tail_notices=0):
    """A dialog frame: the opening key at p 0, then `passes` modal passes (one empty call each) with an answer
    at every pass listed in keys; a notice replaces the empty call of each pass in notices_at; after the last
    answer two closing passes, each preceded by tail_notices notices."""
    calls = [("i", WM_KEYDOWN, 0x75), "e"]
    for n in range(passes):
        if n in keys:
            calls.append(("i", WM_KEYDOWN, 0x1b))
        calls.append(("m", mt.SOS_NOTICE, 0xA003) if n in notices_at else "e")
    for _ in range(2):
        calls.extend([("m", mt.SOS_NOTICE, 0xA003)] * tail_notices + ["e"])
    return calls


def trace(frames, source=None, end=True):
    start = {"event": "start", "schema": 1, "kind": "message_trace", "key": "frame_counter_0x4d257b",
             "pump": "pm_remove_calls_in_frame"}
    if source:
        start["source"] = source
    records = [start]
    for f, calls in frames:
        records += frame_calls(f, calls)
    if end:
        records.append({"event": "end", "other_thread": 0})
    return records


def ticks(advances):
    """A raw tick log whose vt_intra_advances grow by advances[f] in frame f."""
    total, out = 0, [{"event": "start"}]
    for f, n in enumerate(advances):
        total += n
        out.append({"event": "tick", "frame_counter_before_increment": f, "vt_intra_advances": total})
    return out


def write(directory, name, records):
    path = Path(directory) / name
    path.write_bytes("".join(json.dumps(r) + "\r\n" for r in records).encode("ascii"))
    return str(path)


def journal_frames(notices_at=(), tail_notices=0, extra=None):
    frames = [(0, [("i", 0x200, 0), "e"]), (1, ["e"]), (2, [("i", WM_KEYDOWN, 0x41), ("m", WM_CHAR, 0x61), "e"]),
              (3, modal({60, 70}, 80, notices_at, tail_notices)), (4, ["e"])]
    if extra:
        frames[1] = (1, [("m",) + extra, "e"])
    return frames


class SummaryTest(unittest.TestCase):
    def test_counts_passes_and_clock(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "a.ndjson", trace(journal_frames(notices_at={3, 5}, tail_notices=1)))
            log = write(d, "a.jsonl", ticks([5, 9, 9, 46, 9]))
            result = mt.summary(path, log)
        self.assertTrue(result["consistent"])
        self.assertEqual(result["source"], "proxy")
        self.assertEqual(result["unkeyed"], {"sos_notice": 4})
        (row,) = result["keyed_frames"]
        self.assertEqual(row["frame"], 3)
        self.assertEqual(row["answers"], 2)
        # The answer of pass 60: the first pump call and 60 passes end before it; two notices took two empty calls.
        self.assertEqual(row["fewest_passes"], 59)
        self.assertEqual((row["unkeyed_before_last"], row["unkeyed_after_last"]), (2, 2))
        self.assertEqual(row["clock_advances"], 46)
        self.assertEqual(result["clock_below_cap"], [])

    def test_clock_below_cap_and_frame_without_tick(self):
        with tempfile.TemporaryDirectory() as d:
            path = write(d, "a.ndjson", trace(journal_frames() + [(5, modal({60}, 70))]))
            log = write(d, "a.jsonl", ticks([5, 9, 9, 30, 9]))
            result = mt.summary(path, log)
        self.assertFalse(result["consistent"])
        self.assertEqual(result["clock_below_cap"], [3])
        self.assertEqual([r["clock_advances"] for r in result["keyed_frames"]], [30, None])

    def test_structure_problems(self):
        records = trace(journal_frames())
        bad = [r for r in records if r.get("k") == "frame" and r["f"] == 2][0]
        bad["pumps"] += 1
        records.insert(1, {"k": "msg", "f": 9, "p": 0, "m": WM_TIMER, "w": 0, "l": 0, "h": 7, "t": 1})
        with tempfile.TemporaryDirectory() as d:
            result = mt.summary(write(d, "a.ndjson", records))
        self.assertFalse(result["consistent"])
        self.assertEqual(sorted((p["frame"], p["problem"]) for p in result["problems"]),
                         [(2, "pumps != empty + msgs + inject"), (9, "messages without a frame record")])

    def test_mci_notify_and_platform_are_unkeyed(self):
        frames = journal_frames()
        frames[0] = (0, [("i", 0x200, 0), ("m", mt.PLATFORM, 1), ("m", mt.MCI_NOTIFY, 1), "e"])
        with tempfile.TemporaryDirectory() as d:
            result = mt.summary(write(d, "a.ndjson", trace(frames)))
        self.assertEqual(result["unkeyed"], {"mci_notify": 1, "platform": 1})


class CompareTest(unittest.TestCase):
    def compare(self, frames_a, frames_b, ticks_a=None, ticks_b=None, end_b=True):
        with tempfile.TemporaryDirectory() as d:
            a = write(d, "a.ndjson", trace(frames_a))
            b = write(d, "b.ndjson", trace(frames_b, source="es2sim", end=end_b))
            ta = write(d, "a.jsonl", ticks(ticks_a)) if ticks_a else None
            tb = write(d, "b.jsonl", ticks(ticks_b)) if ticks_b else None
            return mt.compare(a, b, ta, tb)

    def test_notices_are_the_bounded_part(self):
        result = self.compare(journal_frames(notices_at={3, 5, 65}, tail_notices=1), journal_frames(),
                              [5, 9, 9, 46, 9], [5, 9, 9, 46, 9])
        self.assertTrue(result["accounted"], result["unexplained"])
        self.assertEqual(result["sources"], ["proxy", "es2sim"])
        (entry,) = result["bounded"]
        self.assertEqual(entry["frame"], 3)
        self.assertEqual((entry["a"], entry["b"]), ({"sos_notice": 5}, {}))
        # Three notices before the last answer: three passes fewer, three empty calls fewer.
        self.assertEqual((entry["passes_a_minus_b"], entry["empty_a_minus_b"]), (-3, -3))
        self.assertEqual(entry["answer_shift"], [-2, -3])

    def test_notices_outside_dialogs(self):
        frames = journal_frames()
        frames[1] = (1, [("m", mt.SOS_NOTICE, 0xA003), ("m", mt.SOS_NOTICE, 0xA003), "e"])
        result = self.compare(frames, journal_frames())
        self.assertTrue(result["accounted"], result["unexplained"])
        self.assertEqual(result["bounded"][0]["frame"], 1)
        self.assertEqual(result["bounded"][0]["passes_a_minus_b"], 0)

    def test_an_other_message_is_unexplained(self):
        result = self.compare(journal_frames(extra=(WM_TIMER, 1)), journal_frames())
        self.assertFalse(result["accounted"])
        self.assertEqual(sorted(result["unexplained"]), ["fixed_pumps", "messages"])
        self.assertEqual(result["unexplained"]["messages"][0]["frame"], 1)

    def test_a_moved_answer_is_unexplained(self):
        frames_b = journal_frames()
        frames_b[3] = (3, modal({61, 70}, 80))
        result = self.compare(journal_frames(), frames_b)
        self.assertEqual(list(result["unexplained"]), ["injects"])

    def test_extra_modal_pass_is_unexplained(self):
        frames_b = journal_frames()
        frames_b[3] = (3, modal({60, 70}, 81))
        result = self.compare(journal_frames(), frames_b)
        self.assertEqual(list(result["unexplained"]), ["fixed_pumps"])

    def test_clock_below_cap_is_unexplained(self):
        result = self.compare(journal_frames(), journal_frames(), [5, 9, 9, 46, 9], [5, 9, 9, 45, 9])
        self.assertEqual(list(result["unexplained"]), ["clock_b"])
        self.assertEqual(result["keyed"]["b"]["clock_below_cap"], [3])

    def test_frames_up_to_the_shorter_trace(self):
        result = self.compare(journal_frames() + [(5, ["e"]), (6, ["e", "e"])], journal_frames(), end_b=False)
        self.assertTrue(result["accounted"])
        self.assertEqual(result["compared_through_frame"], 4)

    def test_missing_frame(self):
        frames_b = [f for f in journal_frames() if f[0] != 1]
        result = self.compare(journal_frames(), frames_b)
        self.assertEqual(result["unexplained"]["frame"], [{"frame": 1, "only_in": "a"}])


class MainTest(unittest.TestCase):
    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            status = mt.main(list(argv))
        return status, out.getvalue()

    def test_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            a = write(d, "a.ndjson", trace(journal_frames(notices_at={4})))
            b = write(d, "b.ndjson", trace(journal_frames()))
            c = write(d, "c.ndjson", trace(journal_frames(extra=(WM_TIMER, 1))))
            junk = write(d, "junk.ndjson", [{"event": "start", "kind": "heap_trace", "schema": 1}])
            status, text = self.run_main("compare", a, b)
            self.assertEqual(status, 0)
            self.assertTrue(json.loads(text)["accounted"])
            self.assertEqual(self.run_main("compare", a, c)[0], 1)
            self.assertEqual(self.run_main("summary", a)[0], 0)
            self.assertEqual(self.run_main("summary", junk)[0], 2)
            self.assertEqual(self.run_main("summary", str(Path(d) / "missing.ndjson"))[0], 2)


if __name__ == "__main__":
    unittest.main()
