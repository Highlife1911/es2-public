"""domain_gun_lists.py on synthetic GL files; the bound holds over the CD (real data)."""
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
import domain_gun_lists as gl  # noqa: E402


def scan(files):
    with tempfile.TemporaryDirectory() as tmp:
        return gl.scan(fx.tree(tmp, files))


class SyntheticTest(unittest.TestCase):
    def test_ten_weapons_hold(self):
        result = scan({'GL/A.GL': fx.gun_list(list(range(10))), 'GL/B.GL': fx.gun_list([])})
        self.assertTrue(result['holds'])
        self.assertEqual((result['maximum_count'], result['maximum_index']), (10, 9))

    def test_eleven_weapons_fail(self):
        result = scan({'GL/A.GL': fx.gun_list([0] * 11)})
        self.assertFalse(result['holds'])
        self.assertEqual(result['violations'][0]['case'], 'count')

    def test_slot_outside_the_table_fails(self):
        result = scan({'GL/A.GL': fx.gun_list([10])})
        self.assertEqual(result['violations'][0]['case'], 'slot')

    def test_trailing_bytes_are_refused(self):
        with self.assertRaises(cd.FormatError):
            scan({'GL/A.GL': fx.gun_list([0]) + b'\0'})


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = gl.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual((result['file_count'], result['maximum_count']), (21, 10))


if __name__ == '__main__':
    unittest.main()
