"""cd_corpus.py on synthetic archives and files; the real CD's archives parse (real data)."""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SCRIPTS / 'tests'))
import cd_corpus as cd  # noqa: E402
import cd_fixtures as fx  # noqa: E402


class SyntheticTest(unittest.TestCase):
    def test_vol_members_and_entries(self):
        archive = fx.vol({'DAT\\A.DAT': b'abc', 'GL\\B.GL': b'xy'}, kinds={'GL\\B.GL': 7})
        members = cd.vol_members(archive)
        self.assertEqual([(m.path, m.kind, m.payload()) for m in members],
                         [('DAT\\A.DAT', 2, b'abc'), ('GL\\B.GL', 7, b'xy')])
        with tempfile.TemporaryDirectory() as tmp:
            fx.tree(tmp, {'VOL/X.VOL': archive, 'LOOSE/B.GL': b'zz'})
            found = dict(cd.entries(Path(tmp), cd.suffix('.GL')))
            self.assertEqual(found, {'LOOSE/B.GL': b'zz', 'VOL/X.VOL:GL\\B.GL': b'xy'})

    def test_bounds_are_checked(self):
        with self.assertRaises(cd.FormatError):
            cd.i16(b'\x01', 0)
        with self.assertRaises(cd.FormatError):
            cd.vol_members(fx.vol({'D\\A': b'abc'})[:-1])

    def test_str_groups(self):
        data = fx.str_file([[(b'HI', b'\x01\x02')], [(b'', b'')]])
        self.assertEqual(cd.str_groups(data), [[(b'HI', b'\x01\x02')], [(b'', b'')]])
        with self.assertRaises(cd.FormatError):
            cd.str_groups(data[:-1])
        with self.assertRaises(cd.FormatError):
            cd.str_groups(struct.pack('<I', len(data) - 5) + data[4:])

    def test_dmg_and_weapons(self):
        caps, regions = cd.dmg_file(fx.dmg([5, 0], [(10, [(20, 0), (30, 1)])]))
        self.assertEqual(caps, [5, 0])
        self.assertEqual((regions[0]['threshold'], regions[0]['links']), (10, [(20, 0), (30, 1)]))
        records = cd.sim_weapons(fx.sim_weapons([(1500, [(20, 12)], 500), (0, [], 0)]))
        self.assertEqual(records, [{'threshold': 1500, 'links': [(20, 12)], 'capacity': 500},
                                   {'threshold': 0, 'links': [], 'capacity': 0}])

    def test_mec_and_tap(self):
        data = fx.mec([(0, 3, 6, [25, 0, 32])])
        records, end = cd.mec_records(data)
        self.assertEqual(records, [(0, 3, 6, [25, 0, 32])])
        self.assertEqual(end, len(data) - 35)
        self.assertEqual(cd.tap_records(fx.tap([b'a', b'bc']), 2), [b'a', b'bc'])

    def test_data_root_accepts_both_layouts(self):
        with tempfile.TemporaryDirectory() as tmp:
            fx.tree(tmp, {'extracted/cd/VOL/X.VOL': b''})
            self.assertEqual(cd.data_root(tmp), Path(tmp) / 'extracted' / 'cd')
            self.assertEqual(cd.data_root(str(Path(tmp) / 'extracted' / 'cd')),
                             Path(tmp) / 'extracted' / 'cd')


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_every_archive_parses(self):
        root = cd.data_root(os.environ['ES2_DATA_ROOT'])
        archives = list(cd.archives(root))
        self.assertEqual(len(archives), 16)


if __name__ == '__main__':
    unittest.main()
