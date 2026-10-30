"""Unit test of divide_scenarios.py on synthetic files: a fake VOLN archive, script.dat, configurations and a DTS.
No original data."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import divide_scenarios as ds  # noqa: E402


def fake_vol(members):
    dirs = b"zones\0"
    out = bytearray(b"VOLN" + bytes(5) + b"\x01" + struct.pack("<H", len(dirs)) + dirs)
    out += struct.pack("<H", len(members)) + bytes(4)
    table = len(out)
    out += bytes(18 * len(members))
    for i, (name, payload) in enumerate(members):
        body = len(out)
        out += b"\0" + struct.pack("<I", len(payload)) + bytes(4) + payload
        row = table + 18 * i
        out[row:row + 13] = name.encode().ljust(13, b"\0")
        out[row + 13] = 0
        struct.pack_into("<I", out, row + 14, body)
    return bytes(out)


def fake_script(zone, points):
    head = bytearray(20)
    struct.pack_into("<hh", head, 0, 1, zone)
    return bytes(head) + struct.pack("<h", len(points)) + b"".join(struct.pack("<3i", *p) for p in points) + b"tail"


class DivideScenariosTest(unittest.TestCase):
    def test_zone_dat_keeps_three_fields(self):
        self.assertEqual(ds.zone_dat(struct.pack("<4i", 8, 8, 14, 256), 0), struct.pack("<4i", 8, 8, 14, 0))
        with self.assertRaises(ValueError):
            ds.zone_dat(bytes(15), 0)
        self.assertEqual(ds.zone_dat(struct.pack("<4i", 8, 8, 14, 256), -1, 31), struct.pack("<4i", 8, 8, 31, -1))

    def test_map_script_appends_two_points(self):
        script = fake_script(7, [(1, 2, 3)])
        out = ds.map_script(script, 0)
        self.assertEqual(struct.unpack_from("<h", out, 20)[0], 3)
        self.assertEqual(struct.unpack_from("<3i", out, 22), (1, 2, 3))
        self.assertEqual(struct.unpack_from("<3i", out, 34), (-2147358112, 2300000, 0))
        self.assertEqual(struct.unpack_from("<3i", out, 46), (0x7fffffff, 2300000, 0))
        self.assertTrue(out.endswith(b"tail"))
        rows = ds.map_script(script, 1)
        self.assertEqual(struct.unpack_from("<3i", rows, 34), (2300000, -2147358112, 0))
        with self.assertRaises(ValueError):
            ds.map_script(script[:30], 0)

    def test_columns_wrap_to_zero(self):
        # the map set-up's 32-bit count ((max + 60000) >> s) - ((min - 60000) >> s) + 1; s is the zone DAT's
        # third dword (14 for the c1_01 zone)
        def count(lo, hi, s):
            a = ((hi + 60000 + 2**31) % 2**32 - 2**31) >> s
            b = ((lo - 60000 + 2**31) % 2**32 - 2**31) >> s
            return (a - b + 1) % 2**32
        for s in range(13, 17):
            self.assertEqual(count(-2147358112, 0x7fffffff, s), 0, s)
        self.assertNotEqual(count(-2147358112, 0x7fffffff, 12), 0)

    def test_write_and_remove(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "DATA"))
            os.makedirs(os.path.join(d, "VOL"))
            with open(os.path.join(d, "DATA", "script.dat"), "wb") as f:
                f.write(fake_script(123, [(0, 0, 0)]))
            with open(os.path.join(d, "VOL", "ZONES.VOL"), "wb") as f:
                f.write(fake_vol([("ZONE22.DAT", struct.pack("<4i", 1, 1, 1, 1)),
                                  ("ZONE123.DAT", struct.pack("<4i", 8, 8, 14, 256))]))
            path = ds.write("zone-flat", d)
            self.assertEqual(os.path.basename(path), "zone123.dat")
            with open(path, "rb") as f:
                self.assertEqual(f.read(), struct.pack("<4i", 8, 8, 14, 16385))
            ds.remove("zone-flat", d)
            self.assertFalse(os.path.exists(path))
            with open(os.path.join(d, "VOL", "SIMVOL0.VOL"), "wb") as f:
                f.write(fake_vol([("MAT0.DAT", struct.pack("<i2i", 1, 0, 6) + b"rest")]))
            ds.write("zone-overflow", d)
            with open(path, "rb") as f:
                self.assertEqual(f.read(), struct.pack("<4i", 8, 8, 31, -1))
            with open(os.path.join(d, "dat", "mat0.dat"), "rb") as f:
                self.assertEqual(f.read(), struct.pack("<i2i", 1, 0, 22) + b"rest")
            ds.remove("zone-overflow", d)
            self.assertEqual(os.listdir(os.path.join(d, "dat")), [])
            ds.write("map-rows", d)
            with open(os.path.join(d, "DATA", "script.dat"), "rb") as f:
                self.assertEqual(struct.unpack_from("<h", f.read(), 20)[0], 3)

    def test_gau_changes_one_dword(self):
        layout = bytearray(ds.GAU_SIZE)
        struct.pack_into("<4i", layout, 0x4cc, 47, 42, 276, 142)
        struct.pack_into("<4i", layout, 0x3f8, 134, 175, 148, 224)
        struct.pack_into("<4i", layout, 0x450, 100, 14, 220, 31)
        layout = bytes(layout)
        for scenario, (target, value) in {"map-page-columns": (0x4dc, 55), "map-page-rows": (0x4d8, 46),
                                          "throttle": (0x404, 181), "heading-tape": (0x458, 100)}.items():
            out = ds.gau(layout, scenario)
            self.assertEqual(struct.unpack_from("<i", out, target)[0], value, scenario)
            self.assertEqual(out[:target] + out[target + 4:], layout[:target] + layout[target + 4:], scenario)
        with self.assertRaises(ValueError):
            ds.gau(layout[:-1], "throttle")

    def test_map_scale_script_sets_every_x(self):
        out = ds.map_scale_script(fake_script(7, [(5, 1, 2), (9, 3, 4), (-8, 5, 6)]))
        self.assertEqual([struct.unpack_from("<3i", out, 22 + 12 * i) for i in range(3)],
                         [(5, 1, 2), (5, 3, 4), (5, 5, 6)])
        self.assertTrue(out.endswith(b"tail"))
        with self.assertRaises(ValueError):
            ds.map_scale_script(fake_script(7, []))

    def test_mat0_levels(self):
        table = struct.pack("<i", 2) + struct.pack("<4i", 0, 6, 1, 5) + b"rest"
        self.assertEqual(ds.mat0(table), struct.pack("<i", 2) + struct.pack("<4i", 0, 22, 1, 22) + b"rest")
        with self.assertRaises(ValueError):
            ds.mat0(struct.pack("<i", 3) + bytes(16))

    def test_overflow_raster_stays_in_grid(self):
        # the base-mark raster 0x47136c: k = S - 13, u = 21 - level, t = 8 - level - k, shift counts mod 32 (CL);
        # x0 = (x >> u) << t, the last row ((y >> u) + 1 << t) - 1, the cell (row' << c) + x0 for a box
        # smaller than 2^18
        def cell(s, level, x, y, c=8):
            u, t = (21 - level) & 31, (8 - level - (s - 13)) & 31
            wrap = lambda v: (v + 2**31) % 2**32 - 2**31
            x0 = wrap((x >> u) << t)
            yl = wrap((((y >> u) + 1) << t) - 1)
            return wrap((yl << c) + x0)
        for x, y in ((0, 0), (1500000, 2300000), (2**31 - 1, 2**31 - 1)):
            self.assertEqual(cell(31, 22, x, y), 0)
        self.assertFalse(0 <= cell(31, 5, 1500000, 2300000) < 2**16)  # a VOL level: outside the grid, the NULL cell

    def test_write_and_remove_panels(self):
        layout = bytearray(ds.GAU_SIZE)
        struct.pack_into("<i", layout, 0x450, 100)
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "VOL"))
            with open(os.path.join(d, "VOL", "SIMVOL0.VOL"), "wb") as f:
                f.write(fake_vol([("OUTLAW.GAU", bytes(layout)), ("MAT0.DAT", struct.pack("<i3i", 1, 0, 6, 0)),
                                  ("RAZOR.GAU", bytes(layout))]))
            ds.write("heading-tape", d)
            self.assertEqual(sorted(os.listdir(os.path.join(d, "gau"))), ["OUTLAW.GAU", "RAZOR.GAU"])
            with open(os.path.join(d, "gau", "RAZOR.GAU"), "rb") as f:
                self.assertEqual(struct.unpack_from("<i", f.read(), 0x458)[0], 100)
            ds.remove("heading-tape", d)
            self.assertEqual(os.listdir(os.path.join(d, "gau")), [])

    def test_ladder_zone(self):
        def dba(top, n=4):
            return bytes(20) + struct.pack("<hh", 2, 2) + bytes(10) + bytes([1, top, 3, 0][:n])
        self.assertEqual(ds.dba_top(dba(151)), 151)
        with self.assertRaises(ValueError):
            ds.dba_top(dba(1)[:26])
        zones = fake_vol([("ZONE408.DBA", dba(151)), ("ZONE7.DBA", dba(100) + b"x"), ("ZONE508.DBA", dba(100))])
        self.assertEqual(ds.ladder_zone(zones, len(dba(151))), (508, 100))
        with self.assertRaises(KeyError):
            ds.ladder_zone(fake_vol([("ZONE408.DBA", dba(151))]), len(dba(151)))

    def test_write_ladder(self):
        heights = bytes(20) + struct.pack("<hh", 1, 2) + bytes(10)
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "DATA"))
            os.makedirs(os.path.join(d, "VOL"))
            with open(os.path.join(d, "DATA", "script.dat"), "wb") as f:
                f.write(fake_script(408, [(0, 0, 0)]))
            with open(os.path.join(d, "VOL", "ZONES.VOL"), "wb") as f:
                f.write(fake_vol([("ZONE408.DBA", heights + bytes([151, 3])),
                                  ("ZONE508.DBA", heights + bytes([100, 3])),
                                  ("ZONE508.DAT", struct.pack("<4i", 7, 7, 14, 256))]))
            path = ds.write("ladder", d)
            self.assertEqual(os.path.basename(path), "zone508.dat")
            with open(path, "rb") as f:
                self.assertEqual(f.read(), struct.pack("<4i", 7, 7, 14, -50))
            with open(os.path.join(d, "DATA", "script.dat"), "rb") as f:
                self.assertEqual(ds.zone_id(f.read()), 508)
            ds.remove("ladder", d)
            self.assertFalse(os.path.exists(path))

    def test_flight_model(self):
        model = bytearray(ds.FM_SIZE)
        struct.pack_into("<4i", model, 0x22, 60000, 6000, 1500, 250)
        model = bytes(model)
        for scenario, pair in {"flight-model-zero": (1500, 1500), "flight-model-overflow": (1500, 1499),
                               "hud-speed-zero": (0, -1250)}.items():
            out = ds.flight_model(model, scenario)
            self.assertEqual(struct.unpack_from("<2i", out, 0x2a), pair, scenario)
            self.assertEqual(out[:0x2a] + out[0x32:], model[:0x2a] + model[0x32:], scenario)
        with self.assertRaises(ValueError):
            ds.flight_model(model[:-1], "hud-speed-zero")

    def test_vehicle(self):
        config = bytes((i * 7 + 1) & 0xff for i in range(ds.VEHICLE_SIZE))
        for scenario, at in ds.VEHICLE_FIELDS.items():
            out = ds.vehicle(config, scenario)
            self.assertEqual(struct.unpack_from("<h", out, at)[0], 0, scenario)
            self.assertEqual(out[:at] + out[at + 2:], config[:at] + config[at + 2:], scenario)
        with self.assertRaises(ValueError):
            ds.vehicle(config + b"x", "shield-zero")

    def test_frustum_far_zone(self):
        self.assertEqual(ds.ZONE_FIELDS["frustum-far"], (18, None))
        self.assertEqual(ds.zone_dat(struct.pack("<4i", 8, 8, 14, 256), None, 18), struct.pack("<4i", 8, 8, 18, 256))
        # the far distance G+0x10c: the detail table word (6, 10 or 14) sar (third - 14); 0 for every detail level
        self.assertEqual([w >> (18 - 14) for w in (6, 10, 14)], [0, 0, 0])

    def test_cell_anim(self):
        def obj(tag, body):
            return struct.pack("<II", tag, len(body)) + body + (b"\0" if len(body) % 2 else b"")
        null = obj(0x0014000E, b"")
        cell = obj(ds.DTS_CELL_ANIM, bytes(12) + struct.pack("<H", 2) + null + null + struct.pack("<H", 5))
        group = obj(0x00140007, bytes(12) + struct.pack("<H", 1) + cell)
        dts = obj(0x001E0003, bytes(12) + struct.pack("<H", 2) + group + null)
        self.assertEqual(len(ds.dts_cell_parts(dts)), 1)
        out = ds.cell_anim_dts(dts)
        at = ds.dts_cell_parts(dts)[0]
        self.assertEqual(len(out), len(dts))
        self.assertEqual(struct.unpack_from("<HH", out, at), (0, 5))
        self.assertEqual(out[:at] + out[at + 4:], dts[:at] + dts[at + 4:])
        with self.assertRaises(ValueError):
            ds.dts_cell_parts(dts[:-4])
        vol = fake_vol([("A.DTS", dts), ("B.DAT", b"data"), ("C.DTS", null)])
        patched, changed = ds.cell_anim_vol(vol)
        self.assertEqual(changed, 1)
        self.assertEqual(ds.vol_member(patched, "A.DTS"), out)
        self.assertEqual(ds.vol_member(patched, "B.DAT"), b"data")
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "VOL"))
            path = os.path.join(d, "VOL", "SIMVOL0.VOL")
            with open(path, "wb") as f:
                f.write(vol)
            ds.write("cell-anim", d)
            with open(path, "rb") as f:
                self.assertEqual(f.read(), patched)
            with self.assertRaises(FileExistsError):
                ds.write("cell-anim", d)
            ds.remove("cell-anim", d)
            with open(path, "rb") as f:
                self.assertEqual(f.read(), vol)
            self.assertEqual(os.listdir(os.path.join(d, "VOL")), ["SIMVOL0.VOL"])

    def test_write_and_remove_loose_configs(self):
        config = bytes(ds.VEHICLE_SIZE - 2) + struct.pack("<h", 7)
        model = bytes(ds.FM_SIZE)
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "VOL"))
            with open(os.path.join(d, "VOL", "SIMVOL0.VOL"), "wb") as f:
                f.write(fake_vol([("OUTLAW.GAU", bytes(ds.GAU_SIZE)), ("OUTLAW.DAT", config),
                                  ("SPIDER.DAT", config), ("RAZOR.FM", model)]))
            ds.write("shield-zero", d)
            self.assertEqual(os.listdir(os.path.join(d, "dat")), ["OUTLAW.DAT"])
            ds.write("hud-speed-zero", d)
            self.assertEqual(os.listdir(os.path.join(d, "fm")), ["RAZOR.FM"])
            ds.remove("shield-zero", d)
            ds.remove("hud-speed-zero", d)
            self.assertEqual(os.listdir(os.path.join(d, "dat")) + os.listdir(os.path.join(d, "fm")), [])

    def test_vol_bounds(self):
        with self.assertRaises(ValueError):
            ds.vol_member(b"VOLN", "X")
        with self.assertRaises(KeyError):
            ds.vol_member(fake_vol([]), "ZONE1.DAT")


if __name__ == "__main__":
    unittest.main()
