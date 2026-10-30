"""domain_region_spread.py on synthetic part tables; the precondition holds over the CD (real data)."""
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
import domain_region_spread as rs  # noqa: E402


def scan(files):
    with tempfile.TemporaryDirectory() as tmp:
        return rs.scan(fx.tree(tmp, files))


class SyntheticTest(unittest.TestCase):
    def test_holds(self):
        result = scan({'DMG/A.DMG': fx.dmg([1, 2], [(5, [(10, 0), (20, 1)])]),
                       'DAT/WEAPONS.DAT': fx.sim_weapons([(1500, [(20, 12)], 500)])})
        self.assertTrue(result['holds'])
        self.assertEqual((result['files'], result['parts'], result['maximum_total']), (2, 2, 30))

    def test_negative_weight(self):
        result = scan({'DMG/A.DMG': fx.dmg([1], [(5, [(-1, 0)])])})
        self.assertEqual(result['violations'][0]['case'], 'negative weight')

    def test_total_at_0x8000(self):
        result = scan({'DMG/A.DMG': fx.dmg([1], [(5, [(0x4000, 0), (0x4000, 0)])])})
        self.assertEqual(result['violations'][0]['case'], 'total')

    def test_shell_weapons_layout_is_skipped(self):
        result = scan({'DMG/A.DMG': fx.dmg([1], [(5, [(1, 0)])]),
                       'GAM/WEAPONS.DAT': fx.shell_weapons([(0, 'NONE', 1)], [])})
        self.assertEqual(result['files'], 1)


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = rs.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(result['files'], 23)
        self.assertLess(result['maximum_total'], 0x8000)


if __name__ == '__main__':
    unittest.main()
