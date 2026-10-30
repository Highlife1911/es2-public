"""domain_vehicle_part.py on synthetic SIMVOL0/SHELL0 archives; the bound holds over the CD (real data)."""
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
import domain_vehicle_part as vp  # noqa: E402

CAPS = [100] * 12 + [0] * 10
REGIONS = [(50, [(10, 0)])] * 19 + [(1, [(20, 12)])] * 10
WEAPONS = [(0, [], 0), (1500, [(20, 12)], 500), (0, [], 0)]
NAMES = [(0, 'NONE', 1), (1, 'ATC', 1), (2, 'LAEW', 0)]


def tree(tmp, caps=CAPS, regions=REGIONS, attachments=2, flag=0, extra=None, names=NAMES):
    sim = fx.vol({'DAT\\WEAPONS.DAT': fx.sim_weapons(WEAPONS),
                  'DAT\\V.DAT': fx.vehicle_dat(attachments, flag),
                  'DMG\\V.DMG': fx.dmg(caps, regions),
                  'GL\\V.GL': fx.gun_list([0, 1])})
    shell = fx.vol({'GAM\\WEAPONS.DAT': fx.shell_weapons(names, [1])})
    return fx.tree(tmp, dict({'VOL/SIMVOL0.VOL': sim, 'VOL/SHELL0.VOL': shell}, **(extra or {})))


def scan(**kwargs):
    with tempfile.TemporaryDirectory() as tmp:
        return vp.scan(tree(tmp, **kwargs))


def cases(result):
    return [v['case'] for v in result['violations']]


def loadout(vehicle, items):
    out = fx.words(vehicle, 100, 0, len(items))
    for slot, item in enumerate(items):
        out += fx.words(slot, item, 100, 5)
    return out


class SyntheticTest(unittest.TestCase):
    def test_holds(self):
        result = scan()
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(result['zero_maximum_items'], [0, 2])
        self.assertEqual(result['mounted_items'], [1])

    def test_zero_used_capacity(self):
        caps = list(CAPS)
        caps[4] = 0
        self.assertEqual(cases(scan(caps=caps)), ['used capacity 0'])

    def test_flag_skips_the_first_two_capacities(self):
        caps = list(CAPS)
        caps[0] = caps[1] = 0
        self.assertEqual(cases(scan(caps=caps)), ['used capacity 0', 'used capacity 0'])
        self.assertTrue(scan(caps=caps, flag=1)['holds'])

    def test_four_attachments_use_capacities_10_and_11(self):
        caps = list(CAPS)
        caps[10] = 0
        self.assertTrue(scan(caps=caps)['holds'])
        self.assertEqual(cases(scan(caps=caps, attachments=4)), ['used capacity 0'])
        self.assertEqual(cases(scan(attachments=5)), ['attachment count'])

    def test_condition_table_divisors(self):
        regions = list(REGIONS)
        regions[3] = (0, [])
        regions[20] = (0, [])
        self.assertEqual(cases(scan(regions=regions)),
                         ['condition threshold 0', 'empty slot divisor 0'])

    def test_mounted_item_with_maximum_zero(self):
        result = scan(extra={'GAM/INI_V.DAT': loadout(0, [1, 2])})
        self.assertEqual(cases(result), ['mounted item with maximum 0'])
        self.assertEqual(result['violations'][0]['item'], 2)

    def test_purchasable_item_with_maximum_zero(self):
        names = NAMES[:2] + [(2, 'LAEW', 1)]
        self.assertEqual(cases(scan(names=names)), ['purchasable item with maximum 0'])


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = vp.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(len(result['vehicles']), 21)
        self.assertEqual(result['zero_maximum_items'], [0, 26, 27])
        self.assertNotIn(26, result['mounted_items'])


if __name__ == '__main__':
    unittest.main()
