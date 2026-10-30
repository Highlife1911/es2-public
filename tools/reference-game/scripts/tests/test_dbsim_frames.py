"""dbsim_frames.py on synthetic capture reports in the format of proxy/presenter.h; no game data."""
import contextlib
import gzip
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dbsim_frames as df
import presenter_capture as pc

START = {"event": "start", "schema": 1, "key": "frame_counter_0x4d257b", "presenter": "original", "dump_frames": 0, "pid": 4242}
SLIDE = pc.SLIDE_CALLS[3] + 5


def h(seed):
    return ("%064x" % (seed * 0x9e3779b97f4a7c15 % (1 << 256)))


def record(frame, site, ordinal=0, index=1, palette=1, page=(0, 0), source="dib"):
    return {"frame": frame, "site": "0x%06x" % site, "ordinal": ordinal, "mode": 0, "S": list(page),
            "R": [0, 0, 639, 479], "client": [0, 0, 640, 480], "src": [page[0], page[1], 640, 480],
            "dst": [0, 0, 640, 480], "source": source, "index_sha256": h(index) if index else None,
            "palette_sha256": h(palette) if palette else None, "uploads": 3, "presented": "original"}


def base_records():
    return [record(1, pc.MAIN_SITE, index=11), record(2, pc.MAIN_SITE, index=12),
            record(3, pc.MAIN_SITE, index=13, palette=2, page=(6, 1)), record(3, SLIDE, index=14, palette=2, page=(6, 1)),
            record(3, 0x45490B, index=15, palette=2), record(3, 0x45490B, ordinal=1, index=16, palette=2),
            record(4, pc.MAIN_SITE, index=None, palette=2, source="other"), record(5, pc.MAIN_SITE, index=17, palette=2)]


def write_capture(directory, name, records, start=START, close=True, torn=False, wrong_count=False):
    path = os.path.join(directory, name)
    with open(path, "w", encoding="ascii", newline="") as f:
        f.write(json.dumps(start, separators=(",", ":")) + "\r\n")
        for i, r in enumerate(records):
            f.write(json.dumps(r, separators=(",", ":")) + "\r\n")
            if i == 1:   # a periodic stats line: it counts what is buffered, not the end
                f.write('{"event":"stats","frame":3,"records":%d,"presents":9,"forwarded":0,"capture_qpc":%d}\r\n' % (3, 1234 + i))
        if torn:
            f.write('{"frame":6,"site":"0x45fb')
        elif close:
            f.write('{"event":"stats","frame":%d,"records":%d,"presents":%d,"presented_gdi":0,"forwarded":0,"clear_blits":2,'
                    '"other_blits":0,"foreign_thread":0,"bad_geometry":0,"no_table":0,"not_full_screen":0,"draw_failures":0,'
                    '"uploads":3,"dumps":0,"dump_failures":0,"write_failures":0,"announced_sites":1,"unattributed_sites":0,"capture_qpc":99}\r\n'
                    % (records[-1]["frame"], len(records) + (1 if wrong_count else 0), len(records)))
    return path


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = df.main(argv)
    return code, out.getvalue(), err.getvalue()


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.capture = write_capture(self.dir, "s.jsonl.capture.ndjson", base_records())

    def build(self, name="out", capture=None, info=None):
        return df.build(capture or self.capture, os.path.join(self.dir, name), "suite-x", info, "20261014")

    def test_manifest_and_table(self):
        m = self.build(info={"result": "identical", "commit": "abc"})
        self.assertEqual(m["status"], "ok")
        self.assertEqual((m["schema"], m["kind"], m["suite"], m["date"]), (1, "es2-dbsim-frames", "suite-x", "20261014"))
        self.assertEqual(m["run"], {"result": "identical", "commit": "abc"})
        c = m["capture"]
        self.assertTrue(c["closed"])
        self.assertEqual(c["bad_lines"], 0)
        self.assertEqual(c["start"], {"presenter": "original", "dump_frames": 0, "pid": 4242})
        self.assertEqual(c["last_stats"]["records"], 8)
        self.assertEqual((c["last_stats"]["announced_sites"], c["last_stats"]["unattributed_sites"]), (1, 0))
        self.assertEqual(c["sha256"], df.sha256_file(self.capture))
        f = m["frames"]
        self.assertEqual((f["records"], f["first_frame"], f["last_frame"], f["main_frames"]), (8, 1, 5, 5))
        self.assertEqual(f["main_frames_not_one_count"], 0)
        self.assertEqual(f["sites"], {"0x%06x" % SLIDE: 1, "0x45490b": 2, "0x45fbe4": 5})
        self.assertEqual(f["page_origin_moved_records"], 2)
        self.assertEqual(f["palette_changes"], 1)
        self.assertEqual(f["distinct_palettes"], 2)
        self.assertEqual(f["records_without_hash"], 1)
        self.assertEqual(f["distinct_frames"], 8)   # seven hashes and the null
        out = os.path.join(self.dir, "out")
        self.assertEqual(df.verify(out), [])
        again = df.parse_tsv(gzip.decompress(open(os.path.join(out, df.FRAMES_NAME), "rb").read()))
        self.assertEqual([(r["frame"], r["site"], r["ordinal"], r["S"], r["index_sha256"], r["palette_sha256"], r["source"]) for r in again],
                         [(r["frame"], r["site"], r["ordinal"], r["S"], r["index_sha256"], r["palette_sha256"], r["source"]) for r in base_records()])
        self.assertEqual(json.loads((Path(out) / df.MANIFEST_NAME).read_text(encoding="utf-8")), m)
        self.assertNotIn(b"\r", (Path(out) / df.MANIFEST_NAME).read_bytes())

    def test_deterministic_files(self):
        first, second = self.build("a"), self.build("b")
        self.assertEqual(first["frames"]["sha256"], second["frames"]["sha256"])
        self.assertEqual(Path(self.dir, "a", df.FRAMES_NAME).read_bytes(), Path(self.dir, "b", df.FRAMES_NAME).read_bytes())

    def test_not_closed(self):
        self.assertFalse(self.build(capture=write_capture(self.dir, "open.ndjson", base_records(), close=False))["capture"]["closed"])
        # A stats line whose count does not match the records is not an orderly end either.
        self.assertFalse(self.build("w", capture=write_capture(self.dir, "wrong.ndjson", base_records(), wrong_count=True))["capture"]["closed"])

    def test_torn_last_line(self):
        m = self.build(capture=write_capture(self.dir, "torn.ndjson", base_records(), torn=True))
        self.assertEqual((m["status"], m["capture"]["closed"], m["capture"]["bad_lines"], m["frames"]["records"]), ("ok", False, 1, 8))

    def test_missing_and_unusable(self):
        m = self.build("m", capture=os.path.join(self.dir, "absent.ndjson"))
        self.assertEqual(m["status"], "no-capture")
        self.assertNotIn("frames", m)
        self.assertFalse(os.path.exists(os.path.join(self.dir, "m", df.FRAMES_NAME)))
        bad = os.path.join(self.dir, "bad.ndjson")
        Path(bad).write_text('{"frame":1}\r\n', encoding="ascii")
        m = self.build("u", capture=bad)
        self.assertEqual(m["status"], "unusable-capture")
        self.assertIn("start line", m["capture"]["error"])
        self.assertTrue(os.path.exists(os.path.join(self.dir, "u", df.MANIFEST_NAME)))
        empty = write_capture(self.dir, "empty.ndjson", [], close=False)
        self.assertEqual(self.build("e", capture=empty)["status"], "no-records")

    def test_stale_table_removed(self):
        self.build("r")
        self.assertTrue(os.path.exists(os.path.join(self.dir, "r", df.FRAMES_NAME)))
        self.assertEqual(self.build("r", capture=os.path.join(self.dir, "absent.ndjson"))["status"], "no-capture")
        self.assertFalse(os.path.exists(os.path.join(self.dir, "r", df.FRAMES_NAME)))

    def test_run_different(self):
        m = self.build(info={"result": "DIFFERENT"})
        self.assertEqual((m["status"], m["frames"]["records"]), ("run-different", 8))

    def test_main_loop_gap_is_reported(self):
        records = [record(1, pc.MAIN_SITE), record(2, pc.MAIN_SITE), record(2, pc.MAIN_SITE, ordinal=1), record(4, pc.MAIN_SITE)]
        f = self.build(capture=write_capture(self.dir, "gap.ndjson", records))["frames"]
        self.assertEqual((f["main_frames_not_one_count"], f["main_frames_not_one_record"]), (2, [2, 3]))

    def test_verify_finds_changes(self):
        self.build("v")
        out = os.path.join(self.dir, "v")
        manifest_path = Path(out, df.MANIFEST_NAME)
        original = manifest_path.read_text(encoding="utf-8")
        for key, value in (("records", 9), ("palette_changes", 4), ("bytes", 5), ("sha256", "0" * 64), ("table_sha256", "0" * 64)):
            m = json.loads(original)
            m["frames"][key] = value
            manifest_path.write_text(json.dumps(m), encoding="utf-8")
            self.assertTrue(df.verify(out), key)
        manifest_path.write_text(original, encoding="utf-8")
        self.assertEqual(df.verify(out), [])
        packed = Path(out, df.FRAMES_NAME)
        table = gzip.decompress(packed.read_bytes()).replace(h(13).encode(), h(99).encode())
        packed.write_bytes(df.gzip_bytes(table))
        problems = df.verify(out)
        self.assertTrue(any("hash" in p or "size" in p for p in problems), problems)
        m = json.loads(original)
        m["frames"].update({"bytes": packed.stat().st_size, "sha256": df.sha256_bytes(packed.read_bytes()),
                            "table_sha256": df.sha256_bytes(table)})
        manifest_path.write_text(json.dumps(m), encoding="utf-8")
        self.assertEqual(df.verify(out), [])   # a consistent change is not detectable without the capture: its hash is in the manifest
        self.assertEqual(json.loads(original)["capture"]["sha256"], m["capture"]["sha256"])


class TableTest(unittest.TestCase):
    def table(self, *lines):
        return (df.HEADER + "\n" + "".join(l + "\n" for l in lines)).encode("ascii")

    def test_strict(self):
        good = "\t".join(("1", "0x45fbe4", "0", "0", "0", "0", "dib", h(1), "-"))
        self.assertEqual(df.parse_tsv(self.table(good))[0]["palette_sha256"], None)
        for bad in ("1\t0x45fbe4\t0\t0\t0\t0\tdib\t%s" % h(1),                                   # columns
                    "1\t0x45fbe4\t0\t0\t0\t0\tdib\t%s\tzz" % h(1),                               # hash
                    "1\t45fbe4\t0\t0\t0\t0\tdib\t%s\t-" % h(1),                                  # site
                    "1\t0x10023387\t0\t0\t0\t0\tdib\t%s\t-" % h(1),                              # unattributed site
                    "x\t0x45fbe4\t0\t0\t0\t0\tdib\t%s\t-" % h(1),                                # number
                    "1\t0x45fbe4\t0\t0\t0\t0\tdib\t%s\t%s" % (h(1).upper(), h(2))):             # upper case
            with self.assertRaises(ValueError, msg=bad):
                df.parse_tsv(self.table(bad))
        for data in (b"", b"#frame\n", (df.HEADER + "\n" + good).encode()):                      # header, final newline
            with self.assertRaises(ValueError):
                df.parse_tsv(data)


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def made(self, name, records, **kw):
        capture = write_capture(self.dir, name + ".ndjson", records, **kw)
        out = os.path.join(self.dir, name)
        df.build(capture, out, "s", None, None)
        return capture, out

    def test_identical_across_forms(self):
        capture_a, dir_a = self.made("a", base_records())
        capture_b, dir_b = self.made("b", base_records())
        tsv = os.path.join(dir_a, df.FRAMES_NAME)
        for x, y in ((dir_a, dir_b), (capture_a, dir_b), (tsv, capture_b), (dir_a, capture_b)):
            r = df.compare(x, y)
            self.assertTrue(r["identical"], (x, y, r))
            self.assertEqual(r["compared_through_frame"], 5)
        code, out, _ = run_main(["compare", dir_a, dir_b])
        self.assertEqual(code, 0)
        self.assertTrue(json.loads(out)["identical"])

    def test_main_loop_difference(self):
        _, a = self.made("a", base_records())
        changed = base_records()
        changed[2]["index_sha256"] = h(99)
        _, b = self.made("b", changed)
        r = df.compare(a, b)
        self.assertFalse(r["identical"])
        d = r["first_difference"][0]
        self.assertEqual((d["kind"], d["index"], d["a"]["frame"]), ("sequence", 2, 3))
        self.assertNotEqual(d["a"]["index_sha256"], d["b"]["index_sha256"])
        self.assertEqual(run_main(["compare", a, b])[0], 1)

    def test_palette_and_origin_differences(self):
        _, a = self.made("a", base_records())
        for field, value in (("palette_sha256", h(77)), ("S", [7, 1]), ("ordinal", 4), ("mode", 1)):
            changed = base_records()
            changed[1][field] = value
            _, b = self.made("b" + field, changed)
            self.assertFalse(df.compare(a, b)["identical"], field)

    def test_modal_sites_compare_as_sets(self):
        _, a = self.made("a", base_records())
        swapped = base_records()
        swapped[4], swapped[5] = swapped[5], swapped[4]   # the two modal presents of frame 3 in the other order
        swapped[4]["ordinal"], swapped[5]["ordinal"] = 0, 1
        _, b = self.made("b", swapped)
        self.assertTrue(df.compare(a, b)["identical"])
        changed = base_records()
        changed[5]["index_sha256"] = h(55)
        _, c = self.made("c", changed)
        r = df.compare(a, c)
        self.assertEqual((r["identical"], r["first_difference"][0]["kind"], r["first_difference"][0]["frame"]), (False, "frame_set", 3))

    def test_only_the_common_frames(self):
        _, a = self.made("a", base_records())
        _, b = self.made("b", base_records()[:6])   # a run that stopped in frame 3
        r = df.compare(a, b)
        self.assertTrue(r["identical"] and r["compared_through_frame"] == 3, r)

    def test_unusable(self):
        _, a = self.made("a", base_records())
        self.assertEqual(run_main(["compare", a, os.path.join(self.dir, "none")])[0], 2)
        bad = os.path.join(self.dir, "bad.tsv")
        Path(bad).write_text("#nope\n")
        self.assertEqual(run_main(["compare", a, bad])[0], 2)
        broken = os.path.join(self.dir, "broken.tsv.gz")
        Path(broken).write_bytes(b"not gzip")
        self.assertEqual(run_main(["compare", a, broken])[0], 2)


class CommandTest(unittest.TestCase):
    def test_build_and_verify_commands(self):
        d = tempfile.mkdtemp()
        capture = write_capture(d, "c.ndjson", base_records())
        info = os.path.join(d, "info.json")
        Path(info).write_text("﻿" + json.dumps({"result": "identical", "clone": "timing-r111-p1"}), encoding="utf-8")
        out = os.path.join(d, "o")
        code, text, _ = run_main(["build", capture, out, "--suite", "x", "--run-info", info, "--date", "20261014"])
        self.assertEqual((code, json.loads(text)), (0, {"suite": "x", "status": "ok", "records": 8}))
        self.assertEqual(json.loads(Path(out, df.MANIFEST_NAME).read_text())["run"]["clone"], "timing-r111-p1")
        code, text, _ = run_main(["verify", out])
        self.assertEqual((code, json.loads(text)["consistent"]), (0, True))
        code, text, _ = run_main(["build", os.path.join(d, "none"), os.path.join(d, "o2"), "--suite", "x"])
        self.assertEqual((code, json.loads(text)["status"]), (1, "no-capture"))
        self.assertEqual(run_main(["verify", os.path.join(d, "o2")])[0], 2)   # no frame table to verify
        self.assertEqual(run_main(["verify", os.path.join(d, "missing")])[0], 2)



# ---- region hashes and the per-kind report -------------------------------------------------------
REGION_START = dict(START, regions="t66-1")


def r16(seed):
    return h(seed)[-16:]


def with_regions(r, display=0, seeds=(1, 2, 3, 4, None)):
    r = dict(r)
    r["display"] = display
    r["regions"] = {name: (r16(seed) if seed else None) for name, seed in zip(df.REGION_NAMES, seeds)}
    return r


def region_records():
    return [with_regions(r) if r["index_sha256"] else r for r in base_records()]


class RegionTableTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def test_build_verify_and_read_back(self):
        capture = write_capture(self.dir, "r.ndjson", region_records(), start=REGION_START)
        out = os.path.join(self.dir, "out")
        m = df.build(capture, out, "s", None, None)
        self.assertEqual(m["status"], "ok")
        self.assertEqual(m["capture"]["start"]["regions"], "t66-1")
        self.assertEqual((m["frames"]["region_table"], m["frames"]["records_with_regions"]), ("t66-1", 7))
        self.assertEqual(df.verify(out), [])
        table = gzip.decompress(Path(out, df.FRAMES_NAME).read_bytes())
        self.assertTrue(table.startswith((df.REGION_HEADER + "\n#regions\tt66-1\n").encode()))
        self.assertEqual(df.region_table_of(table), "t66-1")
        records, region_table = df.load_frames_with_regions(out)
        self.assertEqual(region_table, "t66-1")
        self.assertEqual([(r.get("display"), r.get("regions")) for r in records],
                         [(r.get("display"), r.get("regions")) for r in region_records()])
        self.assertEqual(df.load_frames_with_regions(capture)[1], "t66-1")
        # A capture without the table id keeps the table without region columns, whatever the records carry.
        plain = df.build(write_capture(self.dir, "p.ndjson", region_records()), os.path.join(self.dir, "p"), "s", None, None)
        self.assertNotIn("region_table", plain["frames"])
        self.assertTrue(gzip.decompress(Path(self.dir, "p", df.FRAMES_NAME).read_bytes()).startswith((df.HEADER + "\n").encode()))
        bad = df.build(write_capture(self.dir, "b.ndjson", region_records(), start=dict(START, regions="a b")),
                       os.path.join(self.dir, "b"), "s", None, None)
        self.assertEqual(bad["status"], "unusable-capture")

    def test_strict(self):
        good = "\t".join(("1", "0x45fbe4", "0", "0", "0", "0", "dib", h(1), "-", "0", r16(1), "-", "-", "-", "-"))
        table = lambda *lines, second="#regions\tt66-1": (df.REGION_HEADER + "\n" + second + "\n" + "".join(l + "\n" for l in lines)).encode()
        row = df.parse_tsv(table(good))[0]
        self.assertEqual((row["display"], row["regions"]["view"], row["regions"]["hud"]), (0, r16(1), None))
        none = "\t".join(("1", "0x45fbe4", "0", "0", "0", "0", "other", "-", "-", "-", "-", "-", "-", "-", "-"))
        self.assertNotIn("regions", df.parse_tsv(table(none))[0])
        for bad in (good[:-2], good.replace(r16(1), r16(1)[:15]), good.replace("\t0\t" + r16(1), "\tx\t" + r16(1)),
                    none[:-1] + r16(2)):
            with self.assertRaises(ValueError, msg=bad):
                df.parse_tsv(table(bad))
        with self.assertRaises(ValueError):
            df.parse_tsv(table(good, second="#regions\t"))
        with self.assertRaises(ValueError):
            df.parse_tsv((df.REGION_HEADER + "\n" + good + "\n").encode())


class RegionReportTest(unittest.TestCase):
    def report(self, a, b, tables=("t66-1", "t66-1"), exclude=()):
        for r in a + b:
            r["site_value"] = int(r["site"], 16)
        return df.region_report(a, b, tables[0], tables[1], exclude)

    def test_identical(self):
        result = self.report(region_records(), region_records())
        self.assertTrue(result["identical"])
        self.assertTrue(result["regions_compared"])
        self.assertEqual((result["matched_records"], result["first_kind"], result["kinds"]), (6, None, {}))

    def test_region_and_frame_kinds(self):
        b = region_records()
        b[1] = with_regions(b[1], seeds=(1, 9, 3, 4, None))   # frame 2: the HUD
        b[1]["index_sha256"] = h(99)
        b[7] = with_regions(b[7], seeds=(8, 2, 3, 7, None))   # frame 5: view and MFD
        b[7]["index_sha256"] = h(98)
        result = self.report(region_records(), b)
        self.assertFalse(result["identical"])
        self.assertEqual(list(result["kinds"]), ["frame", "view", "hud", "mfd"])
        self.assertEqual(result["first_kind"], "frame")   # frame 2: the whole frame before its region (KINDS order)
        self.assertEqual(result["kinds"]["frame"]["frames"], [2, 5])
        self.assertEqual(result["kinds"]["hud"]["first"], {"site": "0x45fbe4", "ordinal": 0, "display": 0, "frame": 2})
        self.assertEqual((result["kinds"]["view"]["count"], result["kinds"]["view"]["first"]["frame"]), (1, 5))
        # A different display mode is one kind, its regions are not compared.
        c = region_records()
        c[0] = with_regions(c[0], display=4, seeds=(5, 5, 5, 5, 5))
        self.assertEqual(list(self.report(region_records(), c)["kinds"]), ["display"])
        # Different region tables (or none): the regions are not compared.
        for tables in (("t66-1", "t66-2"), (None, "t66-1"), (None, None)):
            result = self.report(region_records(), b, tables)
            self.assertFalse(result["regions_compared"])
            self.assertEqual(list(result["kinds"]), ["frame"])

    def test_sequence_gap_does_not_cascade(self):
        b = region_records()
        b[7]["palette_sha256"] = h(77)                    # frame 5
        del b[1]                                          # frame 2's main-loop present missing
        result = self.report(region_records(), b)
        self.assertEqual(result["kinds"]["sequence"]["first"], {"site": "0x45fbe4", "ordinal": 0, "only_in": "a", "frame": 2})
        self.assertEqual(result["kinds"]["sequence"]["count"], 1)
        self.assertEqual(result["kinds"]["palette"]["frames"], [5])
        self.assertEqual(result["matched_records"], 5)
        c = region_records()
        c[2]["S"] = [0, 0]
        self.assertEqual(self.report(region_records(), c)["kinds"]["geometry"]["first"]["a"], [0, [6, 1], "dib"])

    def test_other_sites_and_excluded_frames(self):
        b = region_records()
        b[4]["index_sha256"] = h(55)                      # frame 3, a modal site
        result = self.report(region_records(), b)
        self.assertEqual(list(result["kinds"]), ["other_sites"])
        self.assertEqual(result["kinds"]["other_sites"]["first"], {"a": 2, "b": 2, "frame": 3})
        self.assertTrue(self.report(region_records(), b, exclude=[3])["identical"])

    def test_only_the_common_frames(self):
        b = region_records()[:6]
        self.assertEqual(self.report(region_records(), b)["compared_through_frame"], 3)
        self.assertTrue(self.report(region_records(), b)["identical"])

    def test_command(self):
        d = tempfile.mkdtemp()
        a = write_capture(d, "a.ndjson", region_records(), start=REGION_START)
        b_records = region_records()
        b_records[4]["index_sha256"] = h(55)
        b = write_capture(d, "b.ndjson", b_records, start=REGION_START)
        code, text, _ = run_main(["regions", a, b])
        self.assertEqual((code, json.loads(text)["first_kind"]), (1, "other_sites"))
        code, text, _ = run_main(["regions", a, b, "--exclude-frames", "3"])
        self.assertEqual((code, json.loads(text)["excluded_frames"]), (0, [3]))
        self.assertEqual(run_main(["regions", a, os.path.join(d, "missing")])[0], 2)


FREQUENCY = 10_000_000
START2 = dict(START, schema=2, qpc_frequency=FREQUENCY)


def stamped(records, step_ms=40):
    """The same records with the qpc stamp: one per record, step_ms apart."""
    return [dict(r, qpc=5_000_000_000 + i * step_ms * (FREQUENCY // 1000)) for i, r in enumerate(records)]


class QpcSchemaTest(unittest.TestCase):
    """The start schema 2 (capture_qpc=1) is read like schema 1; the table and the compare do not see the stamp."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def capture(self, name, schema, records=None, **kw):
        records = base_records() if records is None else records
        if schema == 2:
            return write_capture(self.dir, name, stamped(records), start=START2, **kw)
        return write_capture(self.dir, name, records, **kw)

    def test_read_capture_accepts_both_schemas(self):
        for schema in (1, 2):
            records, events, closed, bad = df.read_capture(self.capture("c%d.ndjson" % schema, schema))
            self.assertEqual((len(records), events[0]["schema"], closed, bad), (8, schema, True, 0))
        records, events, _, _ = df.read_capture(self.capture("t.ndjson", 2))
        self.assertEqual(events[0]["qpc_frequency"], FREQUENCY)
        self.assertEqual([r["qpc"] for r in records][:2], [5_000_000_000, 5_000_400_000])
        self.assertEqual(records[0]["site_value"], pc.MAIN_SITE)

    def test_the_table_does_not_depend_on_the_stamp(self):
        one, two = (df.build(self.capture("c%d.ndjson" % s, s), os.path.join(self.dir, "o%d" % s), "x", None, "20261022") for s in (1, 2))
        self.assertEqual((one["status"], two["status"]), ("ok", "ok"))
        self.assertEqual(Path(self.dir, "o1", df.FRAMES_NAME).read_bytes(), Path(self.dir, "o2", df.FRAMES_NAME).read_bytes())
        self.assertEqual(one["frames"], two["frames"])
        self.assertEqual(df.verify(os.path.join(self.dir, "o2")), [])

    def test_the_manifest_names_a_timed_capture_only(self):
        one = df.build(self.capture("c1.ndjson", 1), os.path.join(self.dir, "o1"), "x")
        two = df.build(self.capture("c2.ndjson", 2), os.path.join(self.dir, "o2"), "x")
        self.assertEqual(one["capture"]["start"], {"presenter": "original", "dump_frames": 0, "pid": 4242})
        self.assertEqual(two["capture"]["start"], {"presenter": "original", "dump_frames": 0, "pid": 4242,
                                                   "schema": 2, "qpc_frequency": FREQUENCY})

    def test_compare_across_schemas(self):
        a, b = self.capture("a.ndjson", 1), self.capture("b.ndjson", 2)
        self.assertTrue(df.compare(a, b)["identical"])
        changed = base_records()
        changed[2]["index_sha256"] = h(99)
        c = self.capture("c.ndjson", 2, changed)
        self.assertFalse(df.compare(a, c)["identical"])
        self.assertEqual(run_main(["compare", a, b])[0], 0)

    def test_bad_schema_2_is_unusable(self):
        cases = {"no frequency": (dict(START2, qpc_frequency=None), stamped(base_records())),
                 "zero frequency": (dict(START2, qpc_frequency=0), stamped(base_records())),
                 "text frequency": (dict(START2, qpc_frequency="10000000"), stamped(base_records())),
                 "schema 3": (dict(START2, schema=3), stamped(base_records())),
                 "no stamp": (START2, [dict(r) for r in stamped(base_records())[:3]] + base_records()[3:]),
                 "negative stamp": (START2, [dict(r, qpc=-1) for r in stamped(base_records())])}
        for name, (start, records) in cases.items():
            path = write_capture(self.dir, name.replace(" ", "_") + ".ndjson", records, start=start)
            with self.assertRaises(ValueError, msg=name):
                df.read_capture(path)
            self.assertEqual(df.build(path, os.path.join(self.dir, "u"), "x")["status"], "unusable-capture", name)

    def test_a_stamp_in_a_schema_1_capture_is_not_checked(self):
        records = [dict(r, qpc="x") for r in base_records()]   # schema 1 never had the field; readers ignore it
        self.assertEqual(len(df.read_capture(write_capture(self.dir, "s1.ndjson", records))[0]), 8)


if __name__ == "__main__":
    unittest.main()
