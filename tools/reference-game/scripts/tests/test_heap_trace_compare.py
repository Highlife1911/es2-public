"""heap_trace_compare.py on synthetic traces; no game data required."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import heap_trace_compare as htc


def attach(blocks=0):
    return {'kind': 'attach', 'schema': 1, 'frame': 0, 'sentinel': '00000000', 'rover': '00000000',
            'committed': '00000000', 'reserved': '00000000', 'granularity': 65536, 'platform': 2,
            'blocks': blocks, 'regions': [], 'env_lengths': [5, 7]}


def op(index, fn, a0, a1=0, result=0, frame=0, origin='import', caller=0x401234):
    return {'op': index, 'frame': frame, 'fn': fn, 'from': origin, 'caller': '%08x' % caller, 'a0': '%08x' % a0,
            'a1': '%08x' % a1, 'result': '%08x' % result, 'rover': '00000000'}


def write(directory, name, records):
    path = Path(directory) / name
    path.write_text(''.join(json.dumps(r) + '\n' for r in records), encoding='ascii')
    return str(path)


class CompareTest(unittest.TestCase):
    def run_main(self, original, simulated, *extra):
        with tempfile.TemporaryDirectory() as d:
            o = write(d, 'o.ndjson', original)
            s = write(d, 's.ndjson', simulated)
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                status = htc.main([o, s, '--json', *extra])
            return status, (json.loads(out.getvalue()) if out.getvalue() else None)

    def test_equal_low_words_pass(self):
        original = [attach(), op(0, 'calloc', 1, 0x20, 0x02b80010), op(1, 'free', 0x02b80010),
                    {'kind': 'end', 'ops': 2, 'lost': 0}]
        simulated = [attach(), op(0, 'calloc', 1, 0x20, 0x20000010), op(1, 'free', 0x20000010),
                     {'kind': 'end', 'ops': 2, 'lost': 0}]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 0)
        self.assertEqual(result['ops']['common'], 2)
        self.assertEqual(result['delta_runs'], [{'delta': 0, 'first': 0, 'last': 0, 'ops': 1, 'frame': 0}])

    def test_low_word_runs_and_first_difference(self):
        original = [attach(), op(0, 'calloc', 1, 0x20, 0x02b80010), op(1, 'calloc', 2, 0x10, 0x02b80040, frame=3),
                    op(2, 'calloc', 1, 0x8, 0x02b80070, frame=4)]
        simulated = [attach(), op(0, 'calloc', 1, 0x20, 0x20000010), op(1, 'calloc', 2, 0x10, 0x20000050, frame=3),
                     op(2, 'calloc', 1, 0x8, 0x20000080, frame=4)]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 1)
        self.assertEqual([(r['delta'], r['first'], r['last']) for r in result['delta_runs']], [(0, 0, 0), (16, 1, 2)])
        self.assertEqual(result['first_low_difference']['op'], 1)
        self.assertEqual(result['first_low_difference']['request'], 0x20)

    def test_call_divergence_and_internal_callers(self):
        # CW3220's internal callers differ between the sides and are not compared.
        original = [attach(), op(0, 'malloc', 0x200, result=0x02b80010, origin='internal', caller=0x0b001234),
                    op(1, 'calloc', 1, 0x20, 0x02b80220)]
        simulated = [attach(), op(0, 'malloc', 0x200, result=0x20000010, origin='internal', caller=0),
                     op(1, 'calloc', 1, 0x24, 0x20000220)]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 1)
        self.assertEqual(result['ops']['common'], 1)
        self.assertEqual(result['divergence']['op'], 1)
        self.assertEqual(result['divergence']['es2sim']['request'], 0x24)

    def test_pairing_mismatch(self):
        original = [attach(), op(0, 'calloc', 1, 0x20, 0x02b80010), op(1, 'calloc', 1, 0x20, 0x02b80040),
                    op(2, 'free', 0x02b80010)]
        simulated = [attach(), op(0, 'calloc', 1, 0x20, 0x20000010), op(1, 'calloc', 1, 0x20, 0x20000040),
                     op(2, 'free', 0x20000040)]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 1)
        self.assertEqual(result['pairing'], {'op': 2, 'original': '02b80010', 'es2sim': '20000040',
                                             'expected': '20000010'})

    def test_double_free_on_both_sides(self):
        # The same block freed twice on both sides is listed, not a pairing difference; a block freed again
        # after the other side handed it out again is one.
        original = [attach(), op(0, 'calloc', 1, 0x20, 0x02b80010), op(1, 'free', 0x02b80010), op(2, 'free', 0x02b80010)]
        simulated = [attach(), op(0, 'calloc', 1, 0x20, 0x20000010), op(1, 'free', 0x20000010), op(2, 'free', 0x20000010)]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 0)
        self.assertIsNone(result['pairing'])
        self.assertEqual(result['double_frees'], [{'op': 2, 'frame': 0, 'original': '02b80010', 'es2sim': '20000010'}])
        original[3:] = [op(2, 'calloc', 1, 0x20, 0x02b80010), op(3, 'free', 0x02b80010), op(4, 'free', 0x02b80010)]
        simulated[3:] = [op(2, 'calloc', 1, 0x20, 0x20000040), op(3, 'free', 0x20000040), op(4, 'free', 0x20000010)]
        status, result = self.run_main(original, simulated)
        self.assertEqual(status, 1)
        self.assertEqual(result['pairing']['op'], 4)
        self.assertEqual(result['double_frees'], [])

    def test_find_names_the_partner(self):
        original = [attach(), op(0, 'calloc', 1, 0x100, 0x02bdd900), op(1, 'calloc', 1, 0x20, 0x02be0000)]
        simulated = [attach(), op(0, 'calloc', 1, 0x100, 0x2032ff00), op(1, 'calloc', 1, 0x20, 0x20340000)]
        status, result = self.run_main(original, simulated, '--find', 'original:02bdd95c')
        self.assertEqual(result['find']['op'], 0)
        self.assertEqual(result['find']['offset'], 0x5c)
        self.assertEqual(result['find']['partner_address'], '2032ff5c')

    def test_unreadable_input(self):
        with tempfile.TemporaryDirectory() as d:
            o = write(d, 'o.ndjson', [op(0, 'free', 0)])
            s = write(d, 's.ndjson', [attach()])
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(htc.main([o, s]), 2)

    def test_text_report(self):
        original = [attach(), op(0, 'calloc', 1, 0x20, 0x02b80010)]
        simulated = [attach(), op(0, 'calloc', 1, 0x20, 0x20000014)]
        report = htc.text_report(htc.compare({'attach': original[0], 'blocks': [], 'ops': original[1:], 'end': None},
                                             {'attach': simulated[0], 'blocks': [], 'ops': simulated[1:], 'end': None}))
        self.assertIn('common prefix: 1 operations', report)
        self.assertIn('+4', report)


if __name__ == '__main__':
    unittest.main()
