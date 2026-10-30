"""domain_hud_strings.py on synthetic string sets; the CD's HUD tables and range text fit (real data)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SCRIPTS / 'tests'))
import cd_corpus as cd  # noqa: E402
import cd_fixtures as fx  # noqa: E402
import domain_hud_strings as hs  # noqa: E402


def groups(range_text=b'DIST:  ', extra_in=None):
    out = [[(b'T', b'')] * size for _, size in hs.TABLES]
    out[20] = [(b'A', b''), (b'B', b''), (range_text, b'')]
    if extra_in is not None:
        out[extra_in] = out[extra_in] + [(b'X', b'')]
    return out


def scan(data):
    with tempfile.TemporaryDirectory() as tmp:
        return hs.scan(fx.tree(tmp, {'VOL/SIMVOL0.VOL': fx.vol({'str\\STRINGS0.STR': data})}))


class SyntheticTest(unittest.TestCase):
    def test_range_text_location(self):
        self.assertEqual(hs.locate(hs.RANGE_TEXT), (20, 2))

    def test_holds(self):
        result = scan(fx.str_file(groups()))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(result['files']['VOL/SIMVOL0.VOL:str\\STRINGS0.STR']['range_text_length'], 7)

    def test_long_range_text(self):
        result = scan(fx.str_file(groups(range_text=b'R' * 0x100)))
        self.assertEqual([v['case'] for v in result['violations']], ['range text'])

    def test_group_longer_than_its_table(self):
        result = scan(fx.str_file(groups(extra_in=3)))
        self.assertEqual([v['case'] for v in result['violations']], ['group longer than its table'])

    def test_missing_groups(self):
        result = scan(fx.str_file(groups()[:20]))
        self.assertEqual([v['case'] for v in result['violations']], ['fewer groups than tables', 'no range text'])


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = hs.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(len(result['files']), 3)


if __name__ == '__main__':
    unittest.main()
