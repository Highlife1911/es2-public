"""hook_candidates.py on a synthetic map, registry and verdict file; no game data required."""
import contextlib
import io
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hook_candidates as h4

SUITES = h4.SUITES
ROOT = Path(__file__).resolve().parents[4]


def row(va, instructions, callees=(), callers=(), count=5):
    return dict(va=f'{va:#x}', max=count, instructions=instructions,
                counts={s: count for s in SUITES}, callees_live=[f'{c:#x}' for c in callees],
                callees_replaced=0, callers=[f'{c:#x}' for c in callers])


# Synthetic map: 0x500100..0x500600 are candidates (ready or one live
# callee), 0x500700 is too small, 0x500800 is never entered, 0x500900 is
# a hook, 0x500a00 has two live original callees.
HOOK = 0x500900
ROWS = [
    row(0x500100, 300, [HOOK]),                 # ready, classified H4
    row(0x500200, 200),                         # ready, classified H3
    row(0x500300, 150, [0x500700]),             # one live callee, platform
    row(0x500400, 120),                         # ready, in-progress
    row(0x500500, 110, [0x500700]),             # one live callee, no verdict
    row(0x500600, 90),                          # ready, RTL
    row(0x500700, 20),
    row(0x500800, 400, count=0),
    row(HOOK, 500),
    row(0x500a00, 600, [0x500700, 0x500500]),
]
CLASSIFIED = """# Verdicts for the test

| VA | Verdict | Package | Reason |
|---|---|---|---|
| `0x500200` | H4 | outside the section | ignored: not under the Verdicts heading |

## Verdicts

| VA | Verdict | Package or document | Reason |
|---|---|---|---|
| `0x500100` | H4 | proposed H4-99 | simulation |
| `0x500200` | H3 | [doc](doc.md#x) | draws |
| `0x500300` | platform | V-01 | file I/O; a reason with a | bar |
| `0x500400` | in-progress | H4-98 | running |
| `0x500600` | RTL | - | runtime |
| `0x500700` | dead | D-99 | no caller |

## Proposed packages

| `0x500500` | H3 | ignored | another section |
"""


class Fixture:
    def __init__(self, root):
        self.root = Path(root)
        self.map = self.root / 'map.json'
        self.map.write_text(json.dumps(dict(summary={}, rows=ROWS)), encoding='utf-8')
        self.registry = self.root / 'hook_registry.h'
        self.registry.write_text(
            f'ES2_HOOK(fake_hook, {HOOK - h4.IMAGE_BASE:#x}, d[0], NULL, fake)\n', encoding='utf-8')
        self.classified = self.root / 'function_verdicts.md'
        self.classified.write_text(CLASSIFIED, encoding='utf-8')
        self.output = self.root / 'out.md'

    def run(self, classified=True):
        argv = ['--map', str(self.map), '--registry', str(self.registry), '--output', str(self.output)]
        if classified:
            argv += ['--classified', str(self.classified)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            h4.main(argv)
        return self.output.read_text(encoding='utf-8'), out.getvalue()


def table_vas(text):
    return [int(m.group(1), 16) for m in re.finditer(r'^\| `(0x[0-9a-f]+)` \|', text, re.M)]


class ReadClassifiedTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_only_the_verdict_section_counts(self):
        c = h4.read_classified(self.fx.classified)
        self.assertEqual(sorted(c), [0x500100, 0x500200, 0x500300, 0x500400, 0x500600, 0x500700])
        self.assertEqual(c[0x500200], ('H3', '[doc](doc.md#x)', 'draws'))
        self.assertEqual(c[0x500300][2], 'file I/O; a reason with a|bar')
        self.assertEqual(c[0x500700][0], 'dead')

    def test_refusals(self):
        bad = self.fx.root / 'bad.md'
        for text, message in [
                ('## Verdicts\n\n| `0x1` | H5 | x | y |\n', 'unknown verdict'),
                ('## Verdicts\n\n| `0x1` | H3 | x | y |\n| 0x1 | RTL | x | y |\n', 'classified twice'),
                ('## Verdicts\n\n| `0x1` | H3 |\n', 'expected VA'),
                ('# Nothing\n\n| `0x1` | H3 | x | y |\n', 'no "## Verdicts" section')]:
            bad.write_text(text, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, message):
                h4.read_classified(bad)


class CheckVerdictsTests(unittest.TestCase):
    """--check-verdicts: rows outside the Verdicts table that the table lacks."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def check(self):
        argv = ['--check-verdicts', '--registry', str(self.fx.registry),
                '--classified', str(self.fx.classified)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = h4.main(argv)
        return code, out.getvalue()

    def append(self, text):
        self.fx.classified.write_text(CLASSIFIED + text, encoding='utf-8')

    def test_outside_row_without_table_row_fails(self):
        code, out = self.check()
        self.assertEqual(code, 1)
        self.assertIn('0x500500 is classified H3 in section "Proposed packages"', out)
        self.assertIn('FAIL: 1 functions', out)

    def test_table_row_fixes_it(self):
        self.fx.classified.write_text(
            CLASSIFIED.replace('\n## Proposed packages',
                               '| `0x500500` | H3 | T-59 | draws |\n\n## Proposed packages'),
            encoding='utf-8')
        code, out = self.check()
        self.assertEqual((code, out.splitlines()[-1][:4]), (0, 'PASS'))

    def test_hooks_and_non_verdict_cells_do_not_count(self):
        self.fx.classified.write_text(
            CLASSIFIED.replace('| `0x500500` | H3 | ignored | another section |\n', '')
            + '\n## Later package\n\n'
            f'| `{HOOK:#x}` | H4, hooked | H4-99: `fake_hook` | a hook needs no row |\n'
            '| `0x500a00` | open | H4-99 | no verdict yet |\n'
            '| `0x500b00` | pure (computed in core) | H4-99 | not a verdict word |\n'
            '| `0x500c00` (`name`) | 500c10 | dead | branch-site table: edges in the second cell |\n',
            encoding='utf-8')
        code, out = self.check()
        self.assertEqual((code, out.splitlines()[-1][:4]), (0, 'PASS'))

    def test_verdict_words_are_normalised_and_the_newest_row_is_named(self):
        self.append('\n## Package A\n\n| `0x500d00` | H4 (stays original) | A | first |\n'
                    '\n## Package B\n\n| `0x500d00`, `0x500e00` | runtime (unchanged) | B | second |\n')
        missing, differing = h4.check_verdicts(self.fx.classified, {HOOK: 'fake_hook'})
        self.assertEqual(missing[0x500d00][2:], ('RTL', 'B'))
        self.assertEqual(sorted(missing), [0x500500, 0x500d00, 0x500e00])
        self.assertEqual(list(differing), [0x500200])  # the fixture's table before the heading

    def test_differing_verdict_words_are_only_noted(self):
        self.append('\n## Package A\n\n| `0x500200` | platform | A | now platform |\n')
        code, out = self.check()
        self.assertEqual(code, 1)  # 0x500500 is still missing
        self.assertIn('note: 0x500200 is H3 in the table but platform', out)

    def test_other_modes_still_need_map_and_output(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            h4.main(['--registry', str(self.fx.registry)])


class OutputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_without_classified_every_candidate_is_listed(self):
        text, _ = self.fx.run(classified=False)
        self.assertEqual(table_vas(text), [0x500100, 0x500200, 0x500400, 0x500600, 0x500300, 0x500500])
        self.assertNotIn('Verdict', text)
        self.assertNotIn('Left out', text)
        self.assertIn('4 ready and 2 one-callee candidates.', text)

    def test_classified_leaves_out_every_verdict_but_h4(self):
        text, stdout = self.fx.run()
        self.assertEqual(table_vas(text), [0x500100, 0x500500])
        self.assertIn('| `0x500100` | 300 |', text)
        self.assertTrue(re.search(r'^\| `0x500100` .* \| H4 \|$', text, re.M))
        self.assertTrue(re.search(r'^\| `0x500500` .* \| open \|$', text, re.M))
        self.assertIn('1 ready and 1 one-callee candidates.', text)
        section = text.split('## Left out (classified)', 1)[1]
        self.assertIn('4 further functions', section)
        self.assertIn('- in-progress (1): `0x500400`', section)
        self.assertIn('- H3 (1): `0x500200`', section)
        self.assertIn('- platform (1): `0x500300`', section)
        self.assertIn('- RTL (1): `0x500600`', section)
        self.assertNotIn('dead', section)  # 0x500700 is classified but no candidate
        self.assertIn('(4 classified ones left out)', stdout)
        self.assertNotIn('hooks now', stdout)

    def test_classified_hook_is_reported_as_stale(self):
        self.fx.classified.write_text(
            CLASSIFIED.replace('| `0x500600` | RTL |', f'| `{HOOK:#x}` | in-progress |'), encoding='utf-8')
        text, stdout = self.fx.run()
        self.assertIn(f'classified functions that are hooks now: {HOOK:#x} fake_hook', stdout)
        self.assertIn(0x500600, table_vas(text))


# Synthetic map for --frontier. 0x600100 (bridged, hook caller: class A),
# 0x600500 (calls the hook: B), 0x600700 (vtable root over a recursion
# pair: C), 0x600a00 (only under the rendering root: D) and 0x600b00 (only
# a hook calls it, no counter: S) have two or more live original callees;
# 0x600c00 has too, but is classified platform.
RENDER = h4.ROOTS['R']
F_ROWS = [
    row(0x600100, 30, [0x600200, 0x600300], callers=[HOOK]),
    row(0x600200, 10),
    row(0x600300, 12, [0x600400]),
    row(0x600400, 14),
    row(0x600500, 40, [0x600200, HOOK, 0x600600]),
    row(0x600600, 50),
    row(0x600700, 20, [0x600800, 0x600900], count=1000),
    row(0x600800, 16, [0x600900]),
    row(0x600900, 18, [0x600800]),
    row(RENDER, 22, [0x600a00]),
    row(0x600a00, 24, [0x600200, 0x600400], callers=[RENDER]),
    row(0x600b00, 26, [0x600200, 0x600400], callers=[HOOK]),
    row(0x600c00, 28, [0x600200, 0x600400]),
    row(0x600e00, 300, [0x600200, 0x600400], count=0),  # never entered
    row(HOOK, 500, [0x600200, 0x600400]),               # a hook now
]
F_ROWS[0]['callees_unprobed'] = ['0x600d00', f'{HOOK:#x}', '0x600d10']
F_CLASSIFIED = """## Verdicts

| VA | Verdict | Package | Reason |
|---|---|---|---|
| `0x600600` | H3 | - | draws |
| `0x600c00` | platform | - | file I/O |
"""
F_TAIL = '## Proposed packages (test)\n\nhand-written | kept\n'


def tiny_pe(entries):
    """A PE32 image with one section at VA 0x600000 (raw 0x200..0x1200); entries: va -> bytes."""
    data = bytearray(0x1200)
    data[0:2] = b'MZ'
    data[0x3c:0x40] = (0x40).to_bytes(4, 'little')
    data[0x40:0x44] = b'PE\0\0'
    data[0x46:0x48] = (1).to_bytes(2, 'little')        # sections
    data[0x54:0x56] = (0xe0).to_bytes(2, 'little')     # optional header size
    opt = 0x58
    data[opt:opt + 2] = (0x10b).to_bytes(2, 'little')
    data[opt + 28:opt + 32] = (h4.IMAGE_BASE).to_bytes(4, 'little')
    sec = opt + 0xe0
    data[sec:sec + 8] = b'CODE\0\0\0\0'
    for off, value in ((8, 0x1000), (12, 0x200000), (16, 0x1000), (20, 0x200)):
        data[sec + off:sec + off + 4] = value.to_bytes(4, 'little')
    for va, b in entries.items():
        off = 0x200 + va - 0x600000
        data[off:off + len(b)] = b
    return bytes(data)


class FrontierFixture(Fixture):
    def __init__(self, root):
        super().__init__(root)
        self.map.write_text(json.dumps(dict(summary={}, rows=F_ROWS)), encoding='utf-8')
        self.classified.write_text(F_CLASSIFIED, encoding='utf-8')
        self.proxy = self.root / 'proxy'
        self.proxy.mkdir()
        (self.proxy / 'x_hooks.h').write_text(
            'static callee_counter x[]={\n    {"call_600100",ES2_COLLIDE_CALL,0x600100,0},\n};\n',
            encoding='utf-8')
        self.image = self.root / 'image.exe'
        self.image.write_bytes(tiny_pe({0x600d00: b'\x55\x8b\xec', 0x600d10: b'\xc8\0\0\0'}))

    def frontier(self, classified=True, bridge=True, image=True):
        argv = ['--frontier', '--map', str(self.map), '--registry', str(self.registry),
                '--output', str(self.output)]
        if classified:
            argv += ['--classified', str(self.classified)]
        if bridge:
            argv += ['--bridge-calls', str(self.proxy)]
        if image:
            argv += ['--image', str(self.image)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            h4.main(argv)
        return self.output.read_text(encoding='utf-8'), out.getvalue()


def frontier_table(text):
    """VA -> cells of the frontier table (first table only)."""
    out = {}
    for line in text.split('\n## ', 1)[0].splitlines():
        m = re.match(r'^\| \d+ \| `(0x[0-9a-f]+)` \|', line)
        if m:
            out[int(m.group(1), 16)] = [c.strip() for c in line.strip().strip('|').split('|')]
    return out


class FrontierTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = FrontierFixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_selection_order_and_classes(self):
        text, stdout = self.fx.frontier()
        table = frontier_table(text)
        self.assertEqual(list(table), [0x600100, 0x600500, 0x600700, 0x600a00, 0x600b00])
        self.assertEqual([table[va][2] for va in table], ['A', 'B', 'C', 'D', 'S'])
        self.assertEqual([table[va][0] for va in table], ['1', '2', '3', '4', '5'])
        self.assertIn('5 frontier functions (A 1, B 1, C 1, D 1, S 1)', text)
        self.assertIn('(1 classified ones left out)', stdout)
        self.assertIn('- platform (1): `0x600c00`', text.split('## Left out (classified)', 1)[1])
        self.assertEqual(table[0x600a00][4], 'R')
        self.assertEqual(table[0x600700][4], 'V')

    def test_subtree_metrics(self):
        table = frontier_table(self.fx.frontier()[0])
        a = table[0x600100]
        # columns: #, VA, class, instr, reach, counts, callees, ready/one, to replace,
        # height, kept, unprobed, hook links, callers, verdict
        self.assertEqual(a[6], '0x600200 (10), 0x600300 (12)')
        self.assertEqual(a[7], '1/1')
        self.assertEqual(a[8], '4 / 66 (0x600200, 0x600300, 0x600400)')
        self.assertEqual(a[9], '2')
        self.assertEqual(a[12], '1/0')
        self.assertEqual(a[14], 'open')
        b = table[0x600500]
        self.assertEqual(b[6], '0x600200 (10), 0x600600 (H3)')  # the hook is not listed
        self.assertEqual(b[8], '2 / 50 (0x600200)')
        self.assertEqual(b[10], '0x600600')
        self.assertEqual(b[12], '0/1')
        c = table[0x600700]
        self.assertEqual(c[8], '3 / 54 (0x600800, 0x600900)')
        self.assertEqual(c[9], '1')  # the recursion pair counts as one level
        self.assertEqual(c[13], 'vtable/callback')

    def test_unprobed_callees(self):
        text, _ = self.fx.frontier()
        self.assertEqual(frontier_table(text)[0x600100][11], '0x600d00, 0x600d10')
        section = text.split('## Unprobed callees', 1)[1].split('\n## ', 1)[0]
        self.assertIn('2 distinct callees of live map rows have no probe row', section)
        self.assertIn('**1** of them begin with a one-byte push', section)
        self.assertIn('| `0x600d00` | `558bec00` | 1 | 1 | 0x600100 |', section)
        self.assertNotIn('`0x600d10`', section)
        text, _ = self.fx.frontier(image=False)
        self.assertIn('Pass `--image` to classify their first bytes.', text)

    def test_without_bridge_calls_a_hook_caller_means_class_a(self):
        text, _ = self.fx.frontier(bridge=False)
        table = frontier_table(text)
        self.assertEqual(table[0x600b00][2], 'A')
        self.assertEqual(table[0x600100][2], 'A')
        self.assertNotIn('Bridge calls from', text)

    def test_without_classified(self):
        text, _ = self.fx.frontier(classified=False)
        table = frontier_table(text)
        self.assertIn(0x600c00, table)
        self.assertNotIn('Left out', text)
        self.assertEqual(table[0x600500][8], '3 / 100 (0x600200, 0x600600)')

    def test_proposed_packages_tail_is_kept(self):
        first, _ = self.fx.frontier()
        self.fx.output.write_text(first + '\n' + F_TAIL, encoding='utf-8')
        second, stdout = self.fx.frontier()
        self.assertTrue(second.endswith('\n' + F_TAIL))
        self.assertEqual(second.split('\n## Proposed packages', 1)[0], first)
        self.assertIn('(proposed-packages section kept)', stdout)
        third, _ = self.fx.frontier()
        self.assertEqual(third, second)

    def test_default_mode_ignores_the_frontier_options(self):
        text, _ = self.fx.run()
        self.assertTrue(text.startswith('# H4 candidates from the DBSIM map'))


class HelperTests(unittest.TestCase):
    def test_heights_condense_cycles(self):
        graph = {1: [2], 2: [3], 3: [2, 4], 4: [], 5: [5]}
        self.assertEqual(h4.heights(graph), {1: 2, 2: 1, 3: 1, 4: 0, 5: 0})

    def test_read_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'a.exe'
            path.write_bytes(tiny_pe({0x600010: b'\x53\x56'}))
            at = h4.read_image(path)
            self.assertEqual(at(0x600010, 2), b'\x53\x56')
            self.assertIsNone(at(0x700000, 1))
            self.assertIsNone(at(0x600fff, 2))  # crosses the section end
            path.write_bytes(b'MZ' + bytes(0x3e))
            with self.assertRaisesRegex(ValueError, 'no PE signature'):
                h4.read_image(path)
            path.write_bytes(b'XX')
            with self.assertRaisesRegex(ValueError, 'no MZ header'):
                h4.read_image(path)


# Bridge table: two registered hooks call 0x500100 (original,
# classified H4), fake_hook calls the hook 0x500900 and slot +14,
# other_hook calls 0x500500 (computed in core per the notes); the header
# of new_hook (registered after the run) counts 0x500200 (classified H3),
# which has no live counter. gone_hook is not registered, so its counter is
# ignored.
OTHER = 0x500a00
NEW = 0x500b00
B_REGISTRY = (f'ES2_HOOK(fake_hook, {HOOK - h4.IMAGE_BASE:#x}, d[0], NULL, fake)\n'
              f'ES2_HOOK(other_hook, {OTHER - h4.IMAGE_BASE:#x}, d[1], NULL, fake)\n'
              f'ES2_HOOK(new_hook, {NEW - h4.IMAGE_BASE:#x}, d[2], NULL, fake)\n')


def b_record(scale):
    return {'hk_fake_hook_calls': 9 * scale, 'hk_other_hook_calls': 4 * scale,
            'hk_fake_hook_bypassed': 0,
            'hk_fake_hook_call_500100': 3 * scale, 'hk_other_hook_call_500100': 5 * scale,
            'hk_fake_hook_call_500900': 7 * scale, 'hk_fake_hook_slot_14': 2 * scale,
            'hk_other_hook_call_500500': 1 * scale, 'hk_gone_hook_call_500400': 99}


B_COUNTERS = {'source': 'test', 'suites': {
    s: {'common_ticks': 10, 'compared_tick': b_record(1), 'last_record': b_record(2),
        'exit_counters': b_record(3) if i % 2 else None}
    for i, s in enumerate(SUITES)}}
B_NOTES = """# Notes

### Bridge notes

| VA | Name | Kind | Note |
|---|---|---|---|
| `0x500100` | leaf helper | guest | a note |
| `0x500500` | slot helper | core | computed in core |

## Next section

| `0x500200` | ignored | maybe | another section, not read |
"""


class BridgeFixture(Fixture):
    def __init__(self, root):
        super().__init__(root)
        self.registry.write_text(B_REGISTRY, encoding='utf-8')
        self.proxy = self.root / 'proxy'
        self.proxy.mkdir()
        (self.proxy / 'fake_hooks.h').write_text(
            'static callee_counter fake_hook_counters[]={\n'
            '    {"call_500100",ES2_COLLIDE_CALL,0x500100,0},\n'
            '    {"call_500900",ES2_COLLIDE_CALL,0x500900,0},\n'
            '    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},\n};\n', encoding='utf-8')
        (self.proxy / 'new_hooks.h').write_text(
            'static callee_counter new_hook_counters[]={\n'
            '    {"call_500200",ES2_COLLIDE_CALL,0x500200,0},\n};\n', encoding='utf-8')
        (self.proxy / 'plain.h').write_text('/* no counters */\n', encoding='utf-8')
        self.counters = self.root / 'hook-counters.json'
        self.counters.write_text(json.dumps(B_COUNTERS), encoding='utf-8')
        self.notes = self.root / 'notes.md'
        self.notes.write_text(B_NOTES, encoding='utf-8')

    def bridge(self, notes=True):
        argv = ['--bridge-table', str(self.counters), '--bridge-calls', str(self.proxy),
                '--map', str(self.map), '--registry', str(self.registry),
                '--classified', str(self.classified), '--output', str(self.output)]
        if notes:
            argv += ['--bridge-notes', str(self.notes)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            h4.main(argv)
        return self.output.read_text(encoding='utf-8'), out.getvalue()


def bridge_rows(text, heading):
    """VA or slot -> cells of the table under `#### heading`."""
    out = {}
    part = text.split(f'#### {heading}\n', 1)[1].split('\n#### ', 1)[0]
    for line in part.splitlines():
        m = re.match(r'^\| `(\+?0x[0-9a-f]+)` \|', line)
        if m:
            out[m.group(1)] = [c.strip() for c in line.strip().strip('|').split('|')]
    return out


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = BridgeFixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_original_callees(self):
        text, stdout = self.fx.bridge()
        rows = bridge_rows(text, 'Original callees')
        self.assertEqual(list(rows), ['0x500100', '0x500500', '0x500200'])
        # exit_counters (scale 3) on odd suites, last_record (scale 2) otherwise;
        # summed over fake_hook (3) and other_hook (5)
        a = rows['0x500100']
        self.assertEqual(a[1:5], ['leaf helper', '300', 'guest', '`other_hook`, `fake_hook`'])
        self.assertEqual(a[5:7], ['16/24/16/24/16/24', '120'])
        self.assertEqual(a[7:], ['H4', 'proposed H4-99'])
        core = rows['0x500500']
        self.assertEqual(core[3], 'core')
        self.assertEqual(core[5:8], ['2/3/2/3/2/3', '15', 'own code'])
        static = rows['0x500200']
        self.assertEqual(static[1], 'draws')  # name from the verdict reason
        self.assertEqual(static[4], 'new_hooks.h (no live counts)')
        self.assertEqual(static[5:8], ['—', '0', 'H3'])
        self.assertIn('4 distinct callees are counted as `call_<va>`: 1 are hooks', text)
        self.assertIn('1 of these are computed in core', text)
        self.assertIn('acceptance-60s: last_record; c1_01-success: exit_counters', text)
        self.assertIn('Registered hooks absent from the record (no `hk_<hook>_calls`: added after '
                      'the run, so neither their calls nor their callee counters are live yet): '
                      '`new_hook`.', text)
        self.assertNotIn('gone_hook', text)
        self.assertIn('4 bridged callees (1 hooks, 3 original, 1 core, 1 live), 1 slots', stdout)
        self.assertNotIn('without a note or verdict', stdout)

    def test_hooks_and_slots(self):
        text, _ = self.fx.bridge()
        nested = bridge_rows(text, 'Callees that are hooks')
        self.assertEqual(nested['0x500900'][1:], ['`fake_hook`', '`fake_hook`',
                                                  '14/21/14/21/14/21', '105'])
        slots = bridge_rows(text, 'Slot calls')
        self.assertEqual(slots['+0x14'][1:], ['`fake_hook`', '4/6/4/6/4/6', '30'])

    def test_without_notes_every_callee_is_a_guest_call(self):
        text, stdout = self.fx.bridge(notes=False)
        rows = bridge_rows(text, 'Original callees')
        self.assertEqual(rows['0x500500'][3], 'guest')
        self.assertEqual(rows['0x500500'][7], 'open')
        self.assertIn('original callees without a note or verdict: 0x500500', stdout)

    def test_markers_replace_only_the_generated_part(self):
        self.fx.output.write_text(f'head\n{h4.BRIDGE_BEGIN}\nold\n{h4.BRIDGE_END}\ntail\n',
                                  encoding='utf-8')
        first, stdout = self.fx.bridge()
        self.assertTrue(first.startswith(f'head\n{h4.BRIDGE_BEGIN}\n'))
        self.assertTrue(first.endswith(f'{h4.BRIDGE_END}\ntail\n'))
        self.assertNotIn('\nold\n', first)
        self.assertIn('(between the markers)', stdout)
        second, _ = self.fx.bridge()
        self.assertEqual(second, first)

    def test_refusals(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            h4.main(['--bridge-table', str(self.fx.counters), '--map', str(self.fx.map),
                     '--registry', str(self.fx.registry), '--output', str(self.fx.output)])
        self.fx.notes.write_text('### Bridge notes\n\n| `0x500100` | x | maybe | y |\n',
                                 encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'kind'):
            self.fx.bridge()


SITE = 0x600a8d       # an indirect call `call dword ptr [ecx]` inside a hook's body
NOT_SITE = 0x600b00   # a function start (push ebp; mov ebp,esp) without a map row


class BridgeSiteFixture(BridgeFixture):
    """An adapter keys a call through a pointer by its call instruction."""
    def __init__(self, root):
        super().__init__(root)
        (self.proxy / 'site_hooks.h').write_text(
            'static callee_counter site_counters[]={\n'
            f'    {{"call_{SITE:06x}",ES2_COLLIDE_CALL,{SITE:#x},0}},\n'
            f'    {{"call_{NOT_SITE:06x}",ES2_COLLIDE_CALL,{NOT_SITE:#x},0}},\n}};\n', encoding='utf-8')
        counters = json.loads(json.dumps(B_COUNTERS))
        for suite in counters['suites'].values():
            for key, scale in (('last_record', 2), ('exit_counters', 3)):
                if suite[key]:
                    suite[key][f'hk_fake_hook_call_{SITE:06x}'] = 4 * scale
                    suite[key][f'hk_fake_hook_call_{NOT_SITE:06x}'] = 1 * scale
        self.counters.write_text(json.dumps(counters), encoding='utf-8')
        self.image = self.root / 'image.exe'
        self.image.write_bytes(tiny_pe({SITE: bytes([0xff, 0x11]), NOT_SITE: bytes([0x55, 0x8b, 0xec])}))

    def bridge(self, notes=True, image=True):
        if not image:
            return super().bridge(notes)
        argv = ['--bridge-table', str(self.counters), '--bridge-calls', str(self.proxy),
                '--map', str(self.map), '--registry', str(self.registry),
                '--classified', str(self.classified), '--output', str(self.output),
                '--bridge-notes', str(self.notes), '--image', str(self.image)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            h4.main(argv)
        return self.output.read_text(encoding='utf-8'), out.getvalue()


class BridgeSiteTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = BridgeSiteFixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_indirect_call_site(self):
        at = h4.read_image(self.fx.image)
        self.assertTrue(h4.indirect_call_site(at, SITE, {}, {}))
        self.assertFalse(h4.indirect_call_site(at, NOT_SITE, {}, {}))
        self.assertFalse(h4.indirect_call_site(at, SITE, {SITE: 'a_hook'}, {}))   # a hook is a callee
        self.assertFalse(h4.indirect_call_site(at, SITE, {}, {SITE: {}}))         # so is a map row
        self.assertFalse(h4.indirect_call_site(None, SITE, {}, {}))
        self.assertFalse(h4.indirect_call_site(at, 0x700000, {}, {}))             # outside the image
        pe = self.fx.root / 'modrm.exe'
        for modrm, call in ((0x10, True), (0x15, True), (0x55, True), (0xd0, True),
                            (0x25, False), (0x18, False), (0x30, False)):
            pe.write_bytes(tiny_pe({SITE: bytes([0xff, modrm])}))
            self.assertEqual(h4.indirect_call_site(h4.read_image(pe), SITE, {}, {}), call, hex(modrm))

    def test_site_is_listed_apart(self):
        text, stdout = self.fx.bridge()
        original = bridge_rows(text, 'Original callees')
        self.assertNotIn(f'{SITE:#x}', original)
        self.assertIn(f'{NOT_SITE:#x}', original)
        sites = bridge_rows(text, 'Indirect call sites')
        self.assertEqual(list(sites), [f'{SITE:#x}'])
        # last_record (scale 2) on even suites, exit_counters (scale 3) on odd ones
        self.assertEqual(sites[f'{SITE:#x}'][1:], ['`fake_hook`', '8/12/8/12/8/12', '60'])
        self.assertIn('5 distinct callees are counted as `call_<va>`', text)
        self.assertIn('1 further `call_<va>` keys are the call sites of indirect calls', text)
        self.assertIn('1 indirect call sites written', stdout)
        self.assertNotIn(f'{SITE:#x}', stdout)   # not among the callees without a note or verdict

    def test_without_image_the_key_stays_a_callee(self):
        text, stdout = self.fx.bridge(image=False)
        self.assertIn(f'{SITE:#x}', bridge_rows(text, 'Original callees'))
        self.assertNotIn('#### Indirect call sites', text)
        self.assertNotIn('call sites of indirect calls', text)
        self.assertIn('0 indirect call sites written', stdout)

    def test_frontier_leaves_sites_out_of_the_bridge_calls(self):
        (Path(self.tmp.name) / 'frontier').mkdir()
        fx = FrontierFixture(Path(self.tmp.name) / 'frontier')
        (fx.proxy / 'site_hooks.h').write_text(f'{{"call_{SITE:06x}",0}}\n', encoding='utf-8')
        fx.image.write_bytes(tiny_pe({0x600d00: bytes([0x55, 0x8b, 0xec]), 0x600d10: bytes([0xc8, 0, 0, 0]),
                                      SITE: bytes([0xff, 0x11])}))
        text, _ = fx.frontier()
        self.assertIn(': 1 distinct original or hooked callees', text)
        text, _ = fx.frontier(image=False)
        self.assertIn(': 2 distinct original or hooked callees', text)


class Committed(unittest.TestCase):
    def test_every_verdict_outside_the_table_has_a_table_row(self):
        """A per-package section may not classify a non-hook function the Verdicts
        table lacks (python -B hook_candidates.py --check-verdicts names the rows)."""
        missing, _ = h4.check_verdicts(ROOT / 'docs/reference-game/function_verdicts.md',
                                       h4.read_hooks(ROOT / 'tools/reference-game/proxy/hook_registry.h'))
        self.assertEqual({}, {f'{va:#x}': row[:3] for va, row in missing.items()},
                         'add a row to the "## Verdicts" table of function_verdicts.md')


if __name__ == '__main__':
    unittest.main()
