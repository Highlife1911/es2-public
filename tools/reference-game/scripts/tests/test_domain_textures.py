"""domain_textures.py on synthetic DBM records; no CD bitmap is 0x8000 wide (real data)."""
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
import domain_textures as tx  # noqa: E402


def dbm(width, height, pixels=None):
    p = width * height if pixels is None else pixels
    return (tx.TAG + struct.pack('<IHHBBBIH', 13 + p, height, width, 8, 0, 0, p, 0) + bytes(p)
            + bytes((13 + p) & 1))


def scan(files):
    with tempfile.TemporaryDirectory() as tmp:
        return tx.scan(fx.tree(tmp, files))


class SyntheticTest(unittest.TestCase):
    def test_widths_inside_archives_and_arrays(self):
        array = b'\x01\x00\x28\x00' + bytes(8) + dbm(3, 2) + dbm(640, 1)
        result = scan({'VOL/A.VOL': fx.vol({'DBA\\A.DBA': array}), 'X.DBM': dbm(5, 5)})
        self.assertTrue(result['holds'])
        self.assertEqual((result['bitmaps'], result['maximum_width']), (3, 640))

    def test_inconsistent_tag_is_not_a_bitmap(self):
        self.assertEqual(scan({'X.BIN': dbm(4, 4, pixels=3)[:-2]})['bitmaps'], 0)

    def test_0x8000_wide_fails(self):
        result = scan({'X.DBM': dbm(0x8000, 1)})
        self.assertFalse(result['holds'])
        self.assertEqual(result['violations'][0]['width'], 0x8000)


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = tx.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(result['maximum_width'], 640)


if __name__ == '__main__':
    unittest.main()
