"""domain_voice_names.py on synthetic radio tables; the CD's names stay within 11 characters (real data)."""
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
import domain_voice_names as vn  # noqa: E402


class SyntheticTest(unittest.TestCase):
    def test_name_lengths(self):
        self.assertEqual(vn.name_lengths(2, 1), (8, 8, 6))      # P1_02001, PB_02001, _02001
        self.assertEqual(vn.name_lengths(65535, 255), (11, 11, 9))
        self.assertEqual(vn.name_lengths(65535, 255, sign=10), (12, 11, 9))

    def test_scan(self):
        files = {'STR/PILOT1.STR': fx.str_file([[(b'HI', b'\x02\x01\x00'), (b'X', b'')]]),
                 'STR/PILOTS.STR': fx.str_file([[(b'NAME', b'')]]),
                 'VOL/V.VOL': fx.vol({'SIMVOICE\\P1_02001.WAV': b''})}
        with tempfile.TemporaryDirectory() as tmp:
            result = vn.scan(fx.tree(tmp, files))
        self.assertTrue(result['holds'])
        self.assertEqual((result['tables'], result['strings'], result['maximum_word']), (1, 1, 2))
        self.assertEqual((result['longest']['A'], result['voice_member_stem_maximum']), (8, 8))


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = vn.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'])
        self.assertLessEqual(max(result['longest'].values()), 11)
        self.assertEqual(result['voice_member_stem_maximum'], 8)


if __name__ == '__main__':
    unittest.main()
