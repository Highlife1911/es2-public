"""sim_gap.py on synthetic sources, rows, registry, adapter headers and call graph; no game data required."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sim_gap as sg

UTIL_H = r'''
#ifndef UTIL_H
#define UTIL_H
#define BASE 0x401000u
#define OFS_A (BASE + 0x10)
enum { K_B = 0x401020, K_C = K_B + 0x10 };
static const unsigned K_D = (unsigned)0x401040 | 0;
static int call(es2_sim *d, unsigned va, int n)
{
    return es2_sim_call(d, va, 0, n, 0, 0);   /* the base wrapper's VA is the second argument */
}
#define HCALL(d, va) call((d), (va), 0)
static int forward(es2_sim *d, int n, unsigned target) { return call(d, target, n); }
#endif
'''

MAIN_C = r'''
#include "util.h"
/* es2_sim_call(d, 0x4099ff, ...) in a comment is not a call */
void f(es2_sim *d, int down)
{
    const char *s = "es2_sim_call(d, 0x4099fe)";
    call(d, OFS_A, 1);
    HCALL(d, K_C);
    call(d, down ? K_B : 0x401044, 2);
    call(d, d->vtable[3], 1);
    forward(d, 1, K_D);
    forward(d, 2, 0x401050 + 0x10);
    es2_sim_call(d, 0x401050, 0, 0, 0, 0);
    d->table = 0x402000;
    d->other = 0x401090;
    d->data = 0x402040;
}
'''

ROWS_H = '''
ES2_SIM_SERVICE(row_a, 0x00401050u, CDECL, EAX, 2, es2_sim_service_row_a)
ES2_SIM_SERVICE(row_b, 0x00401120u, STDCALL, EAX, 1, es2_sim_service_row_b)
'''

REGISTRY_H = '''
ES2_HOOK(hook_a, 0x00001044, hook_descriptors[0], NULL, group)
'''

PROXY_H = '''
static const char *counters[] = { "call_401130", "call_401050", "call_401010" };
'''


def call_graph():
    """Starts with (instructions, calls, tail calls, indirect calls)."""
    def row(n, calls=(), tails=(), indirect=0):
        return dict(instructions=n, x87=0, indirect_calls=indirect, jump_tables=0, globals=0,
                    calls=[f'{c:#x}' for c in calls], tail_calls=[f'{c:#x}' for c in tails])
    return {
        0x401010: row(10, calls=[0x401070], tails=[0x401080]),
        0x401020: row(4),
        0x401030: row(6, calls=[0x401050]),
        0x401044: row(2),
        0x401050: row(8, calls=[0x401140]),
        0x401060: row(3, indirect=2),
        0x401070: row(5, calls=[0x401090, 0x401010]),
        0x401080: row(7),
        0x401090: row(9),
        0x401100: row(11, calls=[0x401110]),
        0x401110: row(12),
        0x401120: row(1),
        0x401130: row(13, calls=[0x401140]),
        0x401140: row(14),
    }


class FakeImage:
    """The parts of the PE image that sim_gap reads: code window, sections, relocations, dwords."""
    code_va = 0x401000
    code = bytes(0x200)
    sections = [(0x401000, 0x200, 0, 0x200, 0x60000020), (0x402000, 0x100, 0, 0x100, 0xc0000040)]
    relocations = {0x402000, 0x402004, 0x402008, 0x402040}
    words = {0x402000: 0x401100, 0x402004: 0x401050, 0x402008: 0x402000, 0x402040: 0x401100}

    def dword(self, va):
        return self.words.get(va)


class Args:
    pass


def make_tree(root):
    root = Path(root)
    (root / 'sim' / 'src').mkdir(parents=True)
    (root / 'sim' / 'dispatch' / 'services').mkdir(parents=True)
    (root / 'proxy').mkdir()
    (root / 'sim' / 'src' / 'util.h').write_bytes(UTIL_H.encode())
    (root / 'sim' / 'src' / 'main.c').write_bytes(MAIN_C.encode())
    (root / 'sim' / 'dispatch' / 'services' / 'x.h').write_bytes(ROWS_H.encode())
    (root / 'registry.h').write_bytes(REGISTRY_H.encode())
    (root / 'proxy' / 'a_hooks.h').write_bytes(PROXY_H.encode())
    args = Args()
    args.sim, args.registry, args.proxy = str(root / 'sim'), str(root / 'registry.h'), str(root / 'proxy')
    args.executable, args.map = '', None
    return args


def build(args):
    return sg.build(args, call_graph(), FakeImage())


class TextTests(unittest.TestCase):
    def test_strip_keeps_lines_and_blanks_strings(self):
        text = 'a /* x\ny */ b "q(1)" // c\n d'
        out = sg.strip_c(text)
        self.assertEqual(out.count('\n'), text.count('\n'))
        self.assertNotIn('q(1)', out)
        self.assertNotIn('x', out.replace('\n', ''))

    def test_evaluate(self):
        ev = lambda e, names=None: sg.evaluate(e, (names or {}).get)
        self.assertEqual(ev('0x10 | 3'), 0x13)
        self.assertEqual(ev('(unsigned)BASE + 0x10 * 2', {'BASE': 0x400000}), 0x400020)
        self.assertEqual(ev('1u << 4'), 16)
        self.assertIsNone(ev('x + 1'))
        self.assertIsNone(ev('d->vtable[3]'))

    def test_split_args_respects_nesting(self):
        self.assertEqual(sg.split_args('a, f(b, c), (d ? e : g)'), ['a', 'f(b, c)', '(d ? e : g)'])


class ScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.args = make_tree(cls.tmp.name)
        cls.model, cls.scan, cls.rows, cls.hooks, cls.text, _ = build(cls.args)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_sources_rows_hooks_bridges(self):
        self.assertEqual([r['va'] for r in self.rows], [0x401050, 0x401120])
        self.assertEqual(self.rows[1]['abi'], 'STDCALL')
        self.assertEqual(self.hooks, {0x401044: 'hook_a'})
        self.assertEqual(sorted(sg.read_bridges(self.args.proxy)), [0x401010, 0x401050, 0x401130])

    def test_wrappers_found_by_fixpoint(self):
        names = {name for _, name in self.scan.wrappers}
        self.assertEqual(names, {'call', 'HCALL', 'forward'})
        self.assertEqual(self.scan.wrapper_slot('main.c', 'call'), 1)
        self.assertEqual(self.scan.wrapper_slot('main.c', 'forward'), 2)
        self.assertEqual(self.scan.wrapper_slot('main.c', 'es2_sim_call'), 1)

    def test_constant_sites(self):
        seen = sorted((s['va'], s.get('conditional', False)) for s in self.scan.sites)
        self.assertEqual(seen, [(0x401010, False), (0x401020, True), (0x401030, False), (0x401040, False),
                                (0x401044, True), (0x401050, False), (0x401060, False)])
        self.assertNotIn(0x4099ff, [s['va'] for s in self.scan.sites])   # comment
        self.assertNotIn(0x4099fe, [s['va'] for s in self.scan.sites])   # string

    def test_dynamic_sites_are_counted_not_listed_as_roots(self):
        # The object slot and the `down ? K_B : 0x401044` argument; the latter also yields two conditional sites.
        self.assertEqual(self.scan.dynamic, {'main.c': 2})
        self.assertEqual(self.scan.warnings, [])

    def test_mentions(self):
        self.assertEqual(sorted(self.scan.mentions)[-1], 0x401090)
        self.assertIn(0x402000, self.scan.data_mentions)
        self.assertIn(0x402040, self.scan.data_mentions)

    def test_tables(self):
        tables = sg.read_tables(FakeImage(), self.scan.data_mentions, set(call_graph()))
        # The run at 0x402000 ends at the third dword (a relocated pointer that is no function start).
        self.assertEqual(tables, {0x402000: [0x401100, 0x401050], 0x402040: [0x401100]})
        self.assertEqual(sg.read_tables(FakeImage(), {0x402100}, set(call_graph())), {})


class ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.args = make_tree(cls.tmp.name)
        cls.model, cls.scan, cls.rows, cls.hooks, cls.text, _ = build(cls.args)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_roots_exclude_rows_and_hooks(self):
        roots = self.model['roots']
        self.assertEqual(sorted(roots), [0x401010, 0x401020, 0x401030, 0x401040, 0x401060, 0x401130])
        self.assertEqual(roots[0x401010]['via'], 'call+bridge')
        self.assertEqual(roots[0x401130]['via'], 'bridge')
        self.assertEqual(roots[0x401030]['via'], 'call')
        self.assertEqual(roots[0x401030]['sites'], 1)
        self.assertEqual(roots[0x401030]['files'], {'main.c': 1})

    def test_behind_stops_at_rows_and_hooks(self):
        nodes = self.model['nodes']
        self.assertEqual({v for v, n in nodes.items() if n['kind'] == 'behind'},
                         {0x401070, 0x401080, 0x401090, 0x401140})
        self.assertNotIn(0x401050, nodes)                       # a row
        self.assertEqual(nodes[0x401070]['depth'], 1)
        self.assertEqual(nodes[0x401090]['depth'], 2)
        self.assertEqual(nodes[0x401090]['root'], 0x401010)
        self.assertEqual(nodes[0x401140]['root'], 0x401130)

    def test_virtual_targets(self):
        nodes = self.model['nodes']
        self.assertEqual(sorted(self.model['virtual']), [0x401100])   # 0x401050 is a row
        self.assertEqual(nodes[0x401100]['kind'], 'virtual')
        self.assertEqual(nodes[0x401110]['kind'], 'vbehind')
        self.assertEqual(self.model['virtual'][0x401100]['tables'], [[0x402000, 0], [0x402040, 0]])

    def test_reach_and_instructions(self):
        self.assertEqual(self.model['reach'][0x401010], [0x401010, 0x401070, 0x401080, 0x401090])
        self.assertEqual(self.model['reach'][0x401130], [0x401130, 0x401140])
        self.assertEqual(self.model['nodes'][0x401060]['indirect'], 2)

    def test_referenced_only(self):
        # 0x401090 is named but also behind a root, so only the image-base constant of util.h is left.
        self.assertEqual(self.model['referenced'], {0x401000: ['util.h']})

    def test_referenced_only_lists_the_rest(self):
        graph = call_graph()
        graph[0x401070] = dict(graph[0x401070], calls=[])   # nothing reaches 0x401090 any more
        model = sg.compute(self.scan, self.rows, self.hooks, {}, graph)
        self.assertEqual(sorted(model['referenced']), [0x401000, 0x401090])
        self.assertEqual(model['referenced'][0x401090], ['main.c'])


class DocumentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.args = make_tree(cls.tmp.name)
        cls.model, cls.scan, cls.rows, cls.hooks, cls.text, _ = build(cls.args)
        cls.output = Path(cls.tmp.name) / 'sim_gap.md'

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_document_is_deterministic_and_has_the_markers(self):
        again = build(self.args)[4]
        self.assertEqual(again, self.text)
        self.assertIn('<!-- gap-roots:begin -->', self.text)
        self.output.write_bytes(self.text.encode())
        self.assertEqual(sg.committed_roots(self.output), set(self.model['roots']))

    def test_per_area_totals_count_the_model(self):
        line = next(l for l in self.text.split('\n') if l.startswith('| **all** |'))
        cells = [c.strip() for c in line.strip('|').split('|')]
        self.assertEqual(cells[1], str(len(self.model['roots'])))
        self.assertEqual(cells[2], str(sum(self.model['instructions'].get(v, 0) for v in self.model['roots'])))

    def test_check_passes_on_current_document(self):
        self.output.write_bytes(self.text.encode())
        self.assertEqual(sg.check(self.model, self.scan, self.rows, self.text, self.output), [])

    def test_check_names_new_and_closed_roots(self):
        stale = self.text.replace('| 401030 |', '| 401f00 |')
        self.output.write_bytes(stale.encode())
        problems = sg.check(self.model, self.scan, self.rows, self.text, self.output)
        self.assertEqual(len(problems), 1)
        self.assertIn('stale', problems[0])
        self.assertIn('new: 401030', problems[0])
        self.assertIn('closed: 401f00', problems[0])

    def test_check_reports_missing_document_and_duplicate_rows(self):
        missing = Path(self.tmp.name) / 'none.md'
        self.assertIn('missing', sg.check(self.model, self.scan, self.rows, self.text, missing)[0])
        twice = self.rows + [dict(self.rows[0], name='again')]
        self.output.write_bytes(self.text.encode())
        problems = sg.check(self.model, self.scan, twice, self.text, self.output)
        self.assertEqual(len(problems), 1)
        self.assertIn('twice', problems[0])

    def test_sim_info(self):
        info = sg.sim_info(self.model, self.scan, self.rows)
        self.assertEqual(info['rows']['401050']['name'], 'row_a')
        self.assertEqual(info['rows']['401050']['fragment'], 'x.h')
        self.assertEqual(info['gap']['401090'], dict(kind='behind', root='401010', depth=2))
        self.assertEqual(info['gap']['401100']['kind'], 'virtual')
        self.assertEqual(json.loads(json.dumps(info)), info)

    def test_main_writes_and_checks(self):
        argv = ['--executable', str(Path(self.tmp.name) / 'registry.h'), '--sim', self.args.sim,
                '--registry', self.args.registry, '--proxy', self.args.proxy, '--output', str(self.output),
                '--verdicts', '']
        original = sg.build
        sg.build = lambda a: original(a, call_graph(), FakeImage())
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(sg.main(argv), 0)
                self.assertTrue(self.output.is_file())
                self.assertEqual(sg.main(argv + ['--check']), 0)
                self.output.write_bytes(self.output.read_bytes() + b'x')
                self.assertEqual(sg.main(argv + ['--check']), 1)
        finally:
            sg.build = original


class MacroHeadTests(unittest.TestCase):
    """Bodies behind function-like macros of the file are functions of their own, with their call sites."""
    ROWS_C = r'''
#include "util.h"
#define ROW(name) void es2_sim_service_##name(es2_sim *d, void *user)
#define TRY(x) do { if (x) { return; } } while (0)
#define LINE(name, body) \
    void es2_sim_service_##name(es2_sim *d, void *user) { if (body) return; }
ROW(first) {
    TRY(call(d, 0x401020, 1));
}
ROW(second) {
    TRY(call(d, 0x401030,
             2));
}
LINE(third, call(d, 0x401060, 3))
LINE(fourth, forward(d, 1, 0x401100))
'''

    def test_rows_behind_macros(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = make_tree(tmp)
            (Path(args.sim) / 'src' / 'rows.c').write_bytes(self.ROWS_C.encode())
            scan = build(args)[1]
        sites = sorted((s['caller'], s['va'], s['line']) for s in scan.sites if s['file'] == 'rows.c')
        self.assertEqual(sites, [('es2_sim_service_first', 0x401020, 8), ('es2_sim_service_fourth', 0x401100, 15),
                                 ('es2_sim_service_second', 0x401030, 11), ('es2_sim_service_third', 0x401060, 14)])
        self.assertEqual(scan.warnings, [])

    def test_expand_macro(self):
        macro = dict(params=['a', 'b'], body='f_##a(#b, b + a)')
        self.assertEqual(sg.expand_macro(macro, ['x', '1']), 'f_x("", 1 + x)')
        rest = dict(params=['a', '...'], body='g(a, __VA_ARGS__)')
        self.assertEqual(sg.expand_macro(rest, ['x', '1', '2']), 'g(x, 1, 2)')

    def test_only_top_level_invocations_expand(self):
        macros = dict(M=dict(params=['x'], body='int m_##x(void)', line=1))
        text = 'M(a) { M(b); }\nM(\nc) {}'
        self.assertEqual(sg.expand_top_level(text, macros), 'int m_a(void) { M(b); }\nint m_c(void)\n {}')


DOC_MD = '''# Doc

## Called gap roots

### Refusals

## Other

### Refusals
'''


class VerdictTests(unittest.TestCase):
    """The verdict list: links, open counts and the stale-verdict checks."""

    def run_with(self, entries):
        with tempfile.TemporaryDirectory() as tmp:
            args = make_tree(tmp)
            Path(tmp, 'docs', 'reference-game').mkdir(parents=True)
            Path(tmp, 'docs', 'reference-game', 'doc.md').write_bytes(DOC_MD.encode())
            args.verdicts = str(Path(tmp, 'verdicts.json'))
            Path(args.verdicts).write_bytes(json.dumps(dict(verdicts=entries)).encode())
            args.root = tmp
            return build(args)

    @staticmethod
    def entry(kind, vas, anchor='called-gap-roots', path='docs/reference-game/doc.md'):
        return dict(kind=kind, doc=f'{path}#{anchor}', reason='r', vas=[f'{v:x}' for v in vas])

    def test_slugs(self):
        self.assertEqual(sg.slugs(DOC_MD), {'doc', 'called-gap-roots', 'refusals', 'other', 'refusals-1'})
        self.assertIn('the-huds-remaining-virtual-targets', sg.slugs("## The HUD's remaining virtual targets"))
        self.assertIn('ts-and-an-part-tables-0x49a494-0x4a6250',
                      sg.slugs('## TS and AN part tables (`0x49a494`, `0x4a6250`)'))

    def test_marks_and_counts(self):
        text = self.run_with([self.entry('dead', [0x401010, 0x401100]),
                              self.entry('behind', [0x401070, 0x401080, 0x401090, 0x401110], 'refusals-1')])[4]
        lines = text.splitlines()

        def row(va):
            return next(l for l in lines if l.startswith(f'| {va:06x} |'))
        self.assertTrue(row(0x401010).endswith('| [dead](doc.md#called-gap-roots) |'), row(0x401010))
        self.assertTrue(row(0x401090).endswith('| [behind](doc.md#refusals-1) |'), row(0x401090))
        self.assertTrue(row(0x401100).endswith('| [dead](doc.md#called-gap-roots) |'), row(0x401100))
        self.assertTrue(row(0x401030).endswith('| open |'), row(0x401030))
        self.assertIn('| called | 6 | 1 | 0 | 0 | 0 | 5 |', text)
        self.assertIn('| behind | 4 | 0 | 0 | 3 | 0 | 1 |', text)
        self.assertIn('| virtual | 1 | 1 | 0 | 0 | 0 | 0 |', text)
        self.assertIn('Open: called `401020`, `401030`, `401040`, `401060`, `401130`; behind `401140`.', text)

    def test_link_to_another_directory(self):
        self.assertEqual(sg.verdict_link(dict(kind='dead', doc='docs/reverse-engineering/hud.md#x')),
                         '[dead](../reverse-engineering/hud.md#x)')

    def test_sim_info_names_the_verdict(self):
        model, scan, rows, _, _, _ = self.run_with([self.entry('dead', [0x401010])])
        info = sg.sim_info(model, scan, rows)
        self.assertEqual(info['gap']['401010']['verdict'], 'dead')
        self.assertNotIn('verdict', info['gap']['401020'])

    def test_stale_verdicts(self):
        model = self.run_with([self.entry('dead', [0x401050, 0x401044, 0x401f00, 0x401020])])[0]
        self.assertEqual(model['verdict_problems'], [
            'stale verdict 401044: it has a hook now',
            'stale verdict 401050: it has a row now',
            'stale verdict 401f00: no longer in the gap'])

    def test_missing_document_and_heading(self):
        model = self.run_with([self.entry('dead', [0x401020], 'nowhere'),
                               self.entry('dead', [0x401030], path='docs/none.md')])[0]
        self.assertEqual(model['verdict_problems'], [
            'verdict 401020: docs/reference-game/doc.md has no heading #nowhere',
            'verdict 401030: docs/none.md missing'])

    def test_behind_reached_by_an_open_root(self):
        model = self.run_with([self.entry('behind', [0x401140])])[0]
        self.assertEqual(model['verdict_problems'], ['verdict 401140 (behind): reached by the open root(s) 401130'])
        model = self.run_with([self.entry('behind', [0x401140]), self.entry('dead', [0x401130])])[0]
        self.assertEqual(model['verdict_problems'], [])

    def test_check_fails_on_a_stale_verdict(self):
        model, scan, rows, _, text, _ = self.run_with([self.entry('dead', [0x401050])])
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp, 'sim_gap.md')
            out.write_bytes(text.encode())
            self.assertEqual(sg.check(model, scan, rows, text, out), ['stale verdict 401050: it has a row now'])

    def test_read_verdicts_problems(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'v.json')
            path.write_bytes(json.dumps(dict(verdicts=[
                dict(kind='gone', doc='a.md#x', vas=['401020']),
                dict(kind='dead', doc='a.md', vas=['401020'])])).encode())
            verdicts, problems = sg.read_verdicts(path)
        self.assertEqual(sorted(verdicts), [0x401020])
        self.assertEqual(len(problems), 3)
        self.assertIn("kind 'gone'", problems[0])
        self.assertIn('no anchor', problems[1])
        self.assertIn('401020 twice', problems[2])

    def test_committed_verdict_list(self):
        verdicts, problems = sg.read_verdicts(Path(sg.__file__).resolve().parent / 'sim_gap_verdicts.json')
        self.assertEqual(problems, [])
        self.assertTrue(verdicts)
        for va, v in verdicts.items():
            self.assertTrue(0x401000 <= va < 0x497000, hex(va))
            self.assertTrue(v['doc'].startswith('docs/') and v['reason'], hex(va))


class CallSiteImage(FakeImage):
    """FakeImage with code bytes: indirect calls at 0x4011f0 (no start) and 0x401140 (a start), a jump at 0x4011d0."""
    code = bytearray(0x200)
    code[0x1f0:0x1f2] = bytes([0xff, 0x11])      # call dword ptr [ecx]
    code[0x140:0x142] = bytes([0xff, 0x15])      # call dword ptr [abs32] at a function start
    code[0x1d0:0x1d2] = bytes([0xff, 0x25])      # jmp dword ptr [abs32]: FF /4, no call
    code = bytes(code)


class CallSiteTests(unittest.TestCase):
    """A bridge key that is the address of an indirect call (no function start) is no root."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.args = make_tree(cls.tmp.name)
        (Path(cls.args.proxy) / 'b_hooks.h').write_bytes(
            b'static callee_counter c[]={{"call_4011f0",0},{"call_4011d0",0},{"call_401140",0}};')
        cls.model, cls.scan, cls.rows, cls.hooks, cls.text, _ = sg.build(cls.args, call_graph(), CallSiteImage())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_indirect_call_site(self):
        image = CallSiteImage()
        self.assertTrue(sg.indirect_call_site(image, 0x4011f0))
        self.assertTrue(sg.indirect_call_site(image, 0x401140))
        self.assertFalse(sg.indirect_call_site(image, 0x4011d0))     # FF /4
        self.assertFalse(sg.indirect_call_site(image, 0x4011f1))
        self.assertFalse(sg.indirect_call_site(image, 0x4011ff))     # the ModRM byte is past the code
        self.assertFalse(sg.indirect_call_site(image, 0x400ff0))
        self.assertFalse(sg.indirect_call_site(None, 0x4011f0))
        for modrm, call in ((0x10, True), (0x55, True), (0xd0, True), (0x18, False), (0x30, False)):
            fake = FakeImage()
            fake.code = bytes([0xff, modrm])
            fake.code_va = 0x401000
            self.assertEqual(sg.indirect_call_site(fake, 0x401000), call, hex(modrm))

    def test_split_bridges(self):
        bridges = {0x4011f0: {'b.h'}, 0x401140: {'b.h'}, 0x4011d0: {'b.h'}}
        callees, sites = sg.split_bridges(bridges, set(call_graph()), CallSiteImage())
        self.assertEqual(sorted(sites), [0x4011f0])
        self.assertEqual(sorted(callees), [0x401140, 0x4011d0])
        self.assertEqual(sg.split_bridges(bridges, set(), None), (bridges, {}))

    def test_call_site_is_no_root(self):
        roots = self.model['roots']
        self.assertNotIn(0x4011f0, roots)
        self.assertNotIn(0x4011f0, self.model['nodes'])
        self.assertEqual(roots[0x401140]['via'], 'bridge')    # a start stays a callee whatever its bytes
        self.assertEqual(roots[0x4011d0]['via'], 'bridge')    # no call instruction: still listed (Instr. ?)
        self.assertEqual(self.model['call_sites'], {0x4011f0: ['b_hooks.h']})

    def test_document_names_the_call_site(self):
        line = next(l for l in self.text.splitlines() if l.startswith('- Bridged calls'))
        self.assertIn('1 of them are no function', line)
        self.assertIn('`4011f0` (b_hooks.h)', line)
        self.assertNotIn('| 4011f0 |', self.text)
        self.assertIn('| 4011d0 |', self.text)

    def test_without_call_sites_the_line_is_unchanged(self):
        line = next(l for l in build(make_tree(Path(self.tmp.name) / 'plain'))[4].splitlines()
                    if l.startswith('- Bridged calls'))
        self.assertTrue(line.endswith('`tools/reference-game/proxy/*.h`).'), line)


if __name__ == '__main__':
    unittest.main()
