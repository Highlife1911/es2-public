"""dbsim_map.py on synthetic inputs: batch plan, batch reports, map rows, static walk of an own image."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(SCRIPTS / 'tests'))
import dbsim_map  # noqa: E402
import probe_instruction_inventory as inventory  # noqa: E402

SUITES = ['s1', 's2']


def report(entries, **extra):
    value = dict(schema=1, complete=True, error=0, other_thread=0,
                 entries=[dict(va=f'0x{va:08x}', receiver='', count=n, saturated=n >= 1000, overflow=0, receivers=[])
                          for va, n in entries])
    value.update(extra)
    return value


def write_batch(root, name, per_suite, summary='Probes valid\n'):
    directory = Path(root) / name
    for suite, entries in per_suite.items():
        (directory / suite).mkdir(parents=True)
        (directory / suite / 'probes.json').write_text(json.dumps(entries), encoding='utf-8')
    if summary is not None:
        (directory / 'probes-summary.md').write_text(summary, encoding='utf-8')
    return str(directory)


class PlanTest(unittest.TestCase):
    def test_thread_batch_first_then_address_order(self):
        starts = {0x401000: ('push', ['entry']), 0x401100: ('EP_ENTER', ['call']), 0x401200: ('EP_JMP_ABS', ['call']),
                  0x401300: ('EP_PUSH', ['operand_pointers', 'pointer']), 0x401400: ('push', ['call']),
                  0x401500: ('unprobed: int3 (the original byte is the breakpoint itself)', ['call'])}
        batches = dbsim_map.plan(starts, 2)
        self.assertEqual(batches, [('t', [0x401200, 0x401300]), ('00', [0x401000, 0x401100]), ('01', [0x401400])])
        with self.assertRaises(ValueError):
            dbsim_map.plan(starts, dbsim_map.MAX_BATCH + 1)

    def test_replaced_hooks_leave_their_span_out(self):
        # A hook start and a start inside its span are not probed; one at the span's end is.
        starts = {va: ('push', ['call']) for va in (0x401000, 0x401008, 0x401010, 0x401100)}
        self.assertEqual(dbsim_map.plan(starts, 512, [0x401000]), [('00', [0x401010, 0x401100])])


class BuildTest(unittest.TestCase):
    def test_reports_and_rows(self):
        with tempfile.TemporaryDirectory() as root:
            good = write_batch(root, 'batch-00', {
                's1': report([(0x401000, 5), (0x401100, 0), (0x401200, 1000)]),
                's2': report([(0x401000, 0), (0x401100, 0), (0x401200, 7)])})
            thread = write_batch(root, 'batch-t', {
                's1': report([(0x401300, 1)], other_thread=1234), 's2': report([(0x401300, 1)])})
            missing = write_batch(root, 'batch-01', {'s1': report([(0x401400, 1)])})
            invalid = write_batch(root, 'batch-02', {'s1': report([(0x401500, 1)]), 's2': report([(0x401500, 1)])},
                                  'Probes invalid: x\n')
            counts, saturated, load, ok, bad, suites = dbsim_map.read_batches([good, thread, missing, invalid])
        self.assertEqual((ok, sorted(bad), suites, load), (['batch-00'], ['batch-01', 'batch-02', 'batch-t'], SUITES, {}))
        starts = {va: ('push', ['call']) for va in (0x401000, 0x401100, 0x401200, 0x401300)}
        cfg = {0x401000: dict(instructions=10, calls=['0x401100', '0x401200'], tail_calls=['0x401300']),
               0x401100: dict(instructions=3, calls=[], tail_calls=[]),
               0x401200: dict(instructions=4, calls=[], tail_calls=[]),
               0x401300: dict(instructions=6, calls=[], tail_calls=[])}
        hooks = {0x401200: 'some_hook'}
        result = dbsim_map.build(starts, cfg, hooks, counts, saturated, ok, bad, suites)
        self.assertEqual([r['va'] for r in result['rows']], ['0x401000'])
        row = result['rows'][0]
        self.assertEqual((row['callees_replaced'], row['callees_live'], row['callees_unprobed']), (1, [], ['0x401300']))
        self.assertEqual((row['max'], row['suites'], row['hot'], row['subtree_functions'], row['subtree_instructions']),
                         (5, 1, False, 2, 16))
        self.assertEqual(result['hooks'], [dict(va='0x401200', name='some_hook', counts={'s1': 1000, 's2': 7}, live=True)])
        self.assertEqual(result['unprobed'], {'0x401300': 'push'})
        self.assertEqual({k: result['summary'][k] for k in ('probed', 'live', 'hot', 'dead', 'replaced_live')},
                         dict(probed=3, live=2, hot=1, dead=1, replaced_live=1))
        self.assertNotIn('load_only', result['summary'])
        self.assertNotIn('superseded', result)

    def test_first_tick_split_and_superseded(self):
        # probes-load.json per suite; a baseline row that no longer runs is superseded.
        with tempfile.TemporaryDirectory() as root:
            batch = write_batch(root, 'batch-00', {
                's1': report([(0x401000, 5), (0x401100, 0), (0x401200, 3)]),
                's2': report([(0x401000, 2), (0x401100, 0), (0x401200, 1000)])})
            for suite, entries in (('s1', [(0x401000, 5), (0x401100, 0), (0x401200, 1)]),
                                   ('s2', [(0x401000, 2), (0x401100, 0), (0x401200, 1000)])):
                (Path(batch) / suite / 'probes-load.json').write_text(json.dumps(
                    report(entries, complete=False, reason='first_tick')), encoding='utf-8')
            counts, saturated, load, ok, bad, suites = dbsim_map.read_batches([batch])
            self.assertEqual(load[0x401000], {'s1': 5, 's2': 2})
            (Path(batch) / 's2' / 'probes-load.json').write_text(json.dumps(
                report([(0x401000, 2)], complete=False, reason='exit')), encoding='utf-8')
            self.assertEqual(dbsim_map.read_batches([batch])[4], ['batch-00'])
        starts = {va: ('push', ['call']) for va in (0x401000, 0x401100, 0x401200)}
        cfg = {va: dict(instructions=1, calls=[], tail_calls=[]) for va in starts}
        baseline = dict(rows=[dict(va='0x401100', max=40, suites=2), dict(va='0x401000', max=9, suites=2),
                              dict(va='0x401300', max=1, suites=1)])
        result = dbsim_map.build(starts, cfg, {}, counts, saturated, ok, bad, suites, load, baseline)
        rows = {r['va']: r for r in result['rows']}
        self.assertEqual(rows['0x401000']['load'], {'s1': 5, 's2': 2})
        # 0x401000 ran only before the first frame; 0x401200 ran in frames (s1) and is saturated at load (s2).
        self.assertEqual((result['summary']['load_split'], result['summary']['load_only']), (2, 1))
        self.assertEqual(result['superseded'], [dict(va='0x401100', baseline_max=40, baseline_suites=2)])
        self.assertEqual(result['summary']['superseded'], 1)


@unittest.skipUnless(dbsim_map.OBJDUMP.exists(), 'pinned llvm-objdump missing')
class SummaryTest(unittest.TestCase):
    def test_static_walk_of_an_own_image(self):
        # Own bytes: a function with a two-case jump table and a tail jump to another start.
        code = bytes.fromhex(
            '55 8bec 8b4508 83f801 7709 ff248520104000 cc cc'  # 0x401000 .. 0x401014 (table at 0x401020)
            .replace(' ', '')).ljust(0x20, b'\xcc')
        code += struct.pack('<II', 0x401030, 0x401038)                            # 0x401020 table
        code = code.ljust(0x30, b'\xcc')
        code += bytes.fromhex('e80b000000 5d c3 cc')                              # 0x401030 case 0: call 0x401040
        code += bytes.fromhex('5d e902000000 cc cc')                              # 0x401038 case 1: tail jmp 0x401040
        code = code.ljust(0x40, b'\xcc') + bytes.fromhex('55 8bec a100204000 5d c3')  # 0x401040 reads a global
        relocations = {0x40100e, 0x401020, 0x401024, 0x401044}
        image_bytes = __import__('test_probe_instruction_inventory').synthetic_pe(code, struct.pack('<I', 5), relocations)
        details = {}
        _, _, reached = inventory.classify(image_bytes, dbsim_map.OBJDUMP, details)
        image, roots = details['image'], dict(details['roots'])
        reached, tables = inventory.extend(image, reached, details['tables'], roots, dbsim_map.OBJDUMP)
        starts = inventory.function_starts(image, reached, roots, tables)
        decoded = inventory.decode(image, set(reached), dbsim_map.OBJDUMP)
        cfg = dbsim_map.summarize(image, {va: decoded[va] for va in reached}, tables, starts)
        self.assertEqual(sorted(starts), [0x401000, 0x401040])
        first = cfg[0x401000]
        self.assertEqual((first['jump_tables'], first['calls'], first['tail_calls'], first['globals']),
                         (1, ['0x401040'], ['0x401040'], 0))
        self.assertEqual(first['instructions'], 6 + 1 + 3 + 2)  # the ja target is the int3 at 0x401014
        self.assertEqual((cfg[0x401040]['instructions'], cfg[0x401040]['globals']), (5, 1))


if __name__ == '__main__':
    unittest.main()
