"""domain_demo_weapons.py on synthetic tapes; DEMO2's player carries ENRG (real data)."""
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
import domain_demo_weapons as dw  # noqa: E402

NAMES = [(0, 'NONE', 1), (3, 'ATC50', 1), (32, 'ENRG', 0)]


def scan(items):
    shell = fx.vol({'GAM\\WEAPONS.DAT': fx.shell_weapons(NAMES, [])})
    tape = fx.tap([b'script', fx.mec([(0, 3, 6, items)])] + [b''] * 5)
    with tempfile.TemporaryDirectory() as tmp:
        return dw.scan(fx.tree(tmp, {'VOL/SHELL0.VOL': shell, 'TAPES/DEMO2.TAP': tape}))


class SyntheticTest(unittest.TestCase):
    def test_lists_and_names(self):
        result = scan([3, 0, 32])
        self.assertTrue(result['holds'])
        record = result['tapes']['TAPES/DEMO2.TAP'][0]
        self.assertEqual((record['vehicle_name'], record['item_names']), ('OGRE', ['ATC50', 'ENRG']))

    def test_without_energy(self):
        self.assertFalse(scan([3, 3])['holds'])


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_demo2_list(self):
        result = dw.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'])
        player = result['tapes']['TAPES/DEMO2.TAP'][0]
        self.assertEqual(player['item_names'], ['PLAS', 'ATC50', 'PLAS', 'ATC50', 'PLAS', 'SHLD',
                                                'ENRG', 'ATC50', 'ATC50', 'TURB'])


if __name__ == '__main__':
    unittest.main()
