"""domain_member_kinds.py on synthetic archives; every CD member is kind 2 (real data)."""
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
import domain_member_kinds as mk  # noqa: E402


def scan(files):
    with tempfile.TemporaryDirectory() as tmp:
        return mk.scan(fx.tree(tmp, files))


class SyntheticTest(unittest.TestCase):
    def test_kind_2_holds(self):
        result = scan({'VOL/A.VOL': fx.vol({'D\\X': b'1', 'D\\Y': b'2'}), 'LOOSE.TXT': b'VOL'})
        self.assertTrue(result['holds'])
        self.assertEqual(result['kinds'], {'2': 2})

    def test_kinds_7_and_9_are_reported(self):
        result = scan({'VOL/A.VOL': fx.vol({'D\\X': b'1', 'D\\Y': b'2', 'D\\Z': b''},
                                           kinds={'D\\X': 7, 'D\\Y': 9})})
        self.assertFalse(result['holds'])
        self.assertEqual([m['kind'] for m in result['decoder_members']], [7, 9])


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = mk.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['decoder_members'])
        self.assertEqual(result['archive_count'], 16)
        self.assertEqual(list(result['kinds']), ['2'])


if __name__ == '__main__':
    unittest.main()
