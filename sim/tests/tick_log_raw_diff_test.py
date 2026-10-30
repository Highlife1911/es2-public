"""tick_log_raw_diff.py on synthetic tick logs (no game, no data).

  tick_log_raw_diff_test.py <tick_log_raw_diff.py>
"""
import contextlib
import importlib.util
import io
import os
import shutil
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("tick_log_raw_diff", os.path.abspath(sys.argv.pop(1)))
raw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raw)

START = '{"event":"start","schema":3,"mode":"replay","pid":%d,"tid":%d,"qpc_frequency":10000000,"watches":1}'
TICK = '{"event":"tick","sequence":%d,"qpc":%d,"ebx":"0x00000000","player_actor":"%s"}'
EXIT = '{"event":"exit","reason":"mission_end","heap_os_calls":%d,"dep_flags":%d,"calls":[]}'


def log(pid=10, tid=11, qpc=500, actor="0x00a1b2c4", heap_os_calls=1970, dep_flags=1, ticks=3, eol="\r\n"):
    lines = [START % (pid, tid)] + [TICK % (i, qpc + i, actor) for i in range(ticks)]
    return (eol.join(lines + [EXIT % (heap_os_calls, dep_flags)]) + eol).encode()


class RawDiff(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="raw-diff-test-")

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def write(self, rel, data):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as stream:
            stream.write(data)
        return path

    def run_main(self, argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = raw.main(argv)
        return code, out.getvalue()

    def test_host_fields_only(self):
        a = self.write("a.jsonl", log())
        b = self.write("b.jsonl", log(pid=20, tid=21, qpc=900, dep_flags=0, eol="\n"))
        lines, host, other = raw.compare(a, b)
        self.assertEqual((lines, other), (5, []))
        self.assertEqual(host, {"pid": 1, "tid": 1, "qpc": 3, "dep_flags": 1})
        code, out = self.run_main([a, b])
        self.assertEqual(code, 0, out)
        self.assertIn("host fields that differ: pid 1, tid 1, qpc 3, dep_flags 1; other differences: 0", out)

    def test_other_fields(self):
        a = self.write("a.jsonl", log())
        b = self.write("b.jsonl", log(actor="0x00a1b2d4", heap_os_calls=1986))
        _, host, other = raw.compare(a, b)
        self.assertEqual(sum(host.values()), 0)
        self.assertEqual(len(other), 4)
        self.assertIn('line 2 (tick): player_actor: "0x00a1b2c4" | "0x00a1b2d4"', other[0])
        self.assertIn("line 5 (exit): heap_os_calls: 1970 | 1986", other[3])
        self.assertEqual(self.run_main([a, b])[0], 1)

    def test_lines_keys_and_text(self):
        a = self.write("a.jsonl", log() + b"not json\r\n")
        b = self.write("b.jsonl", log(ticks=3).replace(b'"ebx"', b'"ebp"') + b"not json either\r\n" + log())
        _, _, other = raw.compare(a, b)
        self.assertTrue(any("keys:" in t for t in other), other)
        self.assertTrue(any(t.startswith("line 6: text:") for t in other), other)
        self.assertEqual(other[-1], "lines: 6 | 11")

    def test_dirs(self):
        self.write("n/s1/tick-log.jsonl", log())
        self.write("w/s1/tick-log.jsonl", log(pid=3, qpc=7))
        self.write("n/s2/tick-log.jsonl", log())
        self.write("w/s2/tick-log.jsonl", log(heap_os_calls=1))
        self.write("n/s3/tick-log.jsonl", log())
        dirs = [os.path.join(self.root, "n"), os.path.join(self.root, "w")]
        code, out = self.run_main(["--dirs"] + dirs)
        self.assertEqual(code, 1, out)
        self.assertIn("| `s1` | 5 | pid 1, qpc 3 | 0 |", out)
        self.assertIn("| `s2` | 5 | none | 1 |", out)
        self.assertIn("| s3 | - | - | missing ", out)
        self.assertIn("raw diff: 3 pairs, 2 with differences outside pid, tid, qpc, dep_flags", out)
        code, out = self.run_main(["--dirs"] + dirs + ["--suite", "s1"])
        self.assertEqual(code, 0, out)

    def test_usage(self):
        for argv in ([], ["a"], ["a", "b", "--dirs", "x", "y"], ["a", "b", "--suite", "s"]):
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                raw.main(argv)


if __name__ == "__main__":
    unittest.main()
