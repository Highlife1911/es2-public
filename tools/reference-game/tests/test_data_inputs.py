"""data_inputs.py (the @es2_data helper) on synthetic files; no game data."""
import hashlib
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

HELPER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'data_inputs.py'
spec = importlib.util.spec_from_file_location('data_inputs', HELPER)
data_inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data_inputs)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Helper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get('TEST_TMPDIR'))
        base = Path(self.tmp.name)
        self.cd, self.out = base / 'cd', base / 'out'
        (self.cd / 'VER95').mkdir(parents=True)
        (self.cd / 'VOL').mkdir()
        self.out.mkdir()
        self.files = {'VER95/DBSIM.EXE': b'synthetic simulation', 'LIB.DLL': b'synthetic library',
                      'VOL/A.VOL': b'synthetic volume', 'README.TXT': b'text'}
        for relative, data in self.files.items():
            (self.cd / relative).write_bytes(data)
        self.table = base / 'reference_hashes.txt'
        self.pin({})

    def tearDown(self):
        self.tmp.cleanup()

    def pin(self, override):
        rows = ['# SHA-256; synthetic', '%s  archive/IMAGE.ISO' % ('0' * 64)]
        for relative in ('VER95/DBSIM.EXE', 'LIB.DLL', 'VER95/MISSING.EXE'):
            value = override.get(relative, sha(self.files.get(relative, b'absent')))
            rows.append('%s  extracted/cd/%s' % (value, relative))
        self.table.write_bytes(('\n'.join(rows) + '\n').encode())

    def run_helper(self):
        return data_inputs.main(['data_inputs.py', str(self.cd), str(self.table), str(self.out)])

    def test_table_rows_below_cd_only(self):
        self.assertEqual([r for r, _ in data_inputs.pinned(self.table)],
                         ['VER95/DBSIM.EXE', 'LIB.DLL', 'VER95/MISSING.EXE'])

    def test_copies_pins_and_lists_every_file(self):
        self.assertEqual(self.run_helper(), 0)
        self.assertEqual((self.out / 'VER95/DBSIM.EXE').read_bytes(), self.files['VER95/DBSIM.EXE'])
        self.assertEqual((self.out / 'LIB.DLL').read_bytes(), self.files['LIB.DLL'])
        self.assertFalse((self.out / 'VER95/MISSING.EXE').exists())
        self.assertFalse((self.out / 'VOL').exists())
        lines = (self.out / 'cd.sha256').read_text(encoding='utf-8').splitlines()
        self.assertTrue(lines[0].startswith('#'))
        self.assertEqual(lines[1:], ['%s  %s' % (sha(self.files[r]), r) for r in sorted(self.files)])

    def test_manifest_follows_content(self):
        self.assertEqual(self.run_helper(), 0)
        before = (self.out / 'cd.sha256').read_bytes()
        (self.cd / 'VOL/A.VOL').write_bytes(b'changed volume')
        self.assertEqual(self.run_helper(), 0)
        self.assertNotEqual((self.out / 'cd.sha256').read_bytes(), before)

    def test_mismatching_pin_refused(self):
        self.pin({'LIB.DLL': '1' * 64})
        self.assertEqual(self.run_helper(), 2)
        self.assertFalse((self.out / 'VER95/DBSIM.EXE').exists())
        self.assertFalse((self.out / 'cd.sha256').exists())


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
