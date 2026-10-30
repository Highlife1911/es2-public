"""areas.py: the module map as data, its structure, and its agreement with dbsim_gap.md."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import areas

ROOT = Path(__file__).resolve().parents[4]

DOC = """# Gap

## 1. Method

| Range | Area | Basis |
|---|---|---|
| `0x401000..0x401078` | RTL | thunks |
| `0x402000..0x428000`, `0x430000..0x430010` | SIM | modules |
| `0x428000..0x429660` | R3D | terrain |

Text after the table.

| Range | Area | Basis |
|---|---|---|
| `0x500000..0x500010` | HUD | a second table is not read |
"""


class MapTests(unittest.TestCase):
    def test_structure(self):
        self.assertEqual(areas.problems_in_map(), [])
        self.assertEqual(set(a for _, _, a in areas.MODULE_MAP), set(areas.AREAS))

    def test_area_of(self):
        self.assertEqual(areas.area_of(0x401000), 'RTL')
        self.assertEqual(areas.area_of(0x401077), 'RTL')
        self.assertEqual(areas.area_of(0x401078), 'R3D')
        self.assertEqual(areas.area_of(0x401d64), '')        # a gap
        self.assertEqual(areas.area_of(0x401dc0), 'R3D')
        self.assertEqual(areas.area_of(0x402000), 'SIM')
        self.assertEqual(areas.area_of(0x427fff), 'SIM')
        self.assertEqual(areas.area_of(0x428000), 'R3D')
        self.assertEqual(areas.area_of(0x461608), 'LOAD')
        self.assertEqual(areas.area_of(0x496a5f), 'RTL')
        self.assertEqual(areas.area_of(0x496a60), '')        # after the last range
        self.assertEqual(areas.area_of(0x400fff), '')        # before the first
        self.assertEqual(areas.area_of(0x100), '')

    def test_gaps(self):
        gaps = areas.gaps()
        self.assertIn((0x401d64, 0x401dc0), gaps)
        self.assertEqual(len(gaps), len(set(gaps)))
        for lo, hi in gaps:
            self.assertEqual(areas.area_of(lo), '')
            self.assertEqual(areas.area_of(hi - 1), '')
        self.assertEqual(areas.gaps(((1, 2, 'SIM'), (2, 4, 'R3D'), (6, 7, 'HUD'))), [(4, 6)])

    def test_structure_problems(self):
        self.assertTrue(areas.problems_in_map(((5, 5, 'SIM'),)))
        self.assertTrue(areas.problems_in_map(((1, 4, 'SIM'), (3, 6, 'R3D'))))
        self.assertTrue(areas.problems_in_map(((1, 2, 'NOPE'),)))
        self.assertEqual(areas.problems_in_map(((1, 2, 'SIM'), (2, 3, 'R3D'))), [])


class DocumentTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.doc = Path(self.tmp.name) / 'gap.md'
        self.doc.write_text(DOC, encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def test_read_document_map(self):
        self.assertEqual(areas.read_document_map(self.doc), [
            (0x401000, 0x401078, 'RTL'), (0x402000, 0x428000, 'SIM'),
            (0x428000, 0x429660, 'R3D'), (0x430000, 0x430010, 'SIM')])

    def test_differences_are_named(self):
        found = areas.check_document(self.doc)
        self.assertIn('only in the document: 0x430000..0x430010 SIM', found)
        self.assertIn('only in areas.py: 0x401078..0x401d64 R3D', found)

    def test_no_table_is_an_error(self):
        self.doc.write_text('# nothing\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'no "| Range | Area" table'):
            areas.read_document_map(self.doc)

    def test_cli(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(areas.main(['--check-document', str(self.doc)]), 1)
        self.assertIn('FAIL: only in the document', out.getvalue())


class Committed(unittest.TestCase):
    def test_module_map_agrees_with_dbsim_gap_md(self):
        """The ranges of areas.py are the table of dbsim_gap.md section 1."""
        self.assertEqual(areas.check_document(ROOT / 'docs/reference-game/dbsim_gap.md'), [])


if __name__ == '__main__':
    unittest.main()
