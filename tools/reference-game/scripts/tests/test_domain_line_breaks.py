"""domain_line_breaks.py: the breaker emulation and the worst-case measures on
synthetic widths; no CD text reaches the fatal cases (real data)."""
import os
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import cd_corpus as cd  # noqa: E402
import domain_line_breaks as lb  # noqa: E402

# Every printable byte 10 px wide, the space 5 px.
WIDTHS = {b: 10 for b in range(0x21, 0x7f)}
WIDTHS[0x20] = 5


class BreakLinesTest(unittest.TestCase):
    def test_fits_on_one_line(self):
        self.assertEqual(lb.break_lines(WIDTHS, b'AB CD', 100), ([b'AB CD'], False))

    def test_breaks_at_the_last_fitting_space(self):
        lines, fatal = lb.break_lines(WIDTHS, b'AAAA BBBB CCCC ', 100)
        self.assertEqual((lines, fatal), ([b'AAAA BBBB', b'CCCC '], False))

    def test_newline_starts_a_line(self):
        self.assertEqual(lb.break_lines(WIDTHS, b'AB\nCD', 100), ([b'AB', b'CD'], False))

    def test_a_word_as_wide_as_the_panel_is_fatal(self):
        self.assertTrue(lb.break_lines(WIDTHS, b'A ' + b'B' * 10 + b' C', 100)[1])

    def test_a_pair_wider_than_the_panel_is_fatal(self):
        # The break at "BBBBB" leaves "CCCCC DDDDD" (105 px) before the next space.
        self.assertTrue(lb.break_lines(WIDTHS, b'AAAAAAAA BBBBB CCCCC DDDDD E', 100)[1])


class WorstCaseTest(unittest.TestCase):
    def test_measures(self):
        worst = lb.worst_over_choices(lb.Breaker(WIDTHS), [[b'AB CD'], [b'EFG HI']])
        self.assertEqual(worst, {'word': 50, 'pair': 75, 'last_word': 20})

    def test_choices_include_the_empty_one(self):
        worst = lb.worst_over_choices(lb.Breaker(WIDTHS), [[b'AB'], [b'XYZ'], [b'CD ']])
        self.assertEqual(worst['word'], 70)  # "ABXYZCD"

    def test_newline_ends_the_pair(self):
        worst = lb.worst_over_choices(lb.Breaker(WIDTHS), [[b'AB\nCD EF']])
        self.assertEqual(worst['pair'], 0)

    def test_check(self):
        result = {'violations': []}
        lb.check(result, 'x', {'word': 100, 'pair': 101, 'last_word': 0}, 100)
        self.assertEqual([v['case'] for v in result['violations']], ['word', 'pair'])


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_holds_over_the_cd(self):
        result = lb.scan(cd.data_root(os.environ['ES2_DATA_ROOT']))
        self.assertTrue(result['holds'], result['violations'][:5])
        self.assertLess(result['mission_maximum']['pair'], lb.PANEL_WIDTH)
        self.assertEqual(result['bytes_without_glyph'], [])


if __name__ == '__main__':
    unittest.main()
