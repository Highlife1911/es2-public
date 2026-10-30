"""domain_default_debris.py on synthetic debris files; DEF_DEB has more than 3 groups (real data)."""
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
import domain_default_debris as dd  # noqa: E402


def debris(children):
    out = fx.words(len(children))
    for ident, k in enumerate(children):
        out += fx.words(ident, k) + bytes(14 * k)
    return out


def scan(files):
    with tempfile.TemporaryDirectory() as tmp:
        return dd.scan(fx.tree(tmp, files))


class SyntheticTest(unittest.TestCase):
    def test_four_groups_hold(self):
        result = scan({'DAT/DEF_DEB.DAT': debris([1, 2, 0, 1]), 'DAT/X_DEB.DAT': debris([1])})
        self.assertTrue(result['holds'])
        self.assertEqual(result['minimum_groups'], 4)

    def test_three_groups_fail(self):
        self.assertFalse(scan({'DAT/DEF_DEB.DAT': debris([1, 1, 1])})['holds'])

    def test_no_file_does_not_hold(self):
        self.assertFalse(scan({'DAT/X_DEB.DAT': debris([1] * 7)})['holds'])

    def test_unconsumed_bytes_are_refused(self):
        with self.assertRaises(cd.FormatError):
            scan({'DAT/DEF_DEB.DAT': debris([1] * 4) + b'\0'})


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = dd.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertEqual(result['minimum_groups'], 6)


if __name__ == '__main__':
    unittest.main()
