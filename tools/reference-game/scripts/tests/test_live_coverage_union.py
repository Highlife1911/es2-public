"""live_coverage_union.py on synthetic regression trees; no game process required."""
import contextlib
import copy
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import live_coverage_column as column  # noqa: E402
import live_coverage_union as union  # noqa: E402
import suite_coverage_delta as delta  # noqa: E402

spec = importlib.util.spec_from_file_location('union_test_report', SCRIPTS / 'live_coverage_report.py')
report_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report_module)
MODULE_HASH = report_module.MODULE_HASH

NAMES = ['alpha', 'beta', 'gamma', 'delta']
# delta has no sites (branchless hook); the commented entry is not compiled.
SITES_H = """/* Derived branch addresses. BRANCH(alpha,0x401f00,2,je,0x401f08) is an example only. */
BRANCH(alpha,0x401000,2,je,0x401008)
BRANCH(alpha, 0x401010, 6, jne, 0x401100)
// BRANCH(beta,0x401f10,2,jl,0x401f18)
BRANCH(beta,0x401020,2,jl,0x401028)
PROVED(beta,0x401030,taken,"bounded by the caller")
BRANCH(gamma,0x401040,2,jbe,0x401048)
"""
SITES = [('alpha', '0x00401000', 'je'), ('alpha', '0x00401010', 'jne'), ('beta', '0x00401020', 'jl'), ('gamma', '0x00401040', 'jbe')]
# Per suite: (fall, taken) per site. Union: alpha 1000 both (split across suites),
# alpha 1010 fall only, beta taken only, gamma never.
COUNTS = {'one': [(3, 0), (1, 0), (0, 0), (0, 0)], 'two': [(0, 2), (4, 0), (0, 5), (0, 0)], 'three': [(0, 0), (0, 0), (0, 0), (0, 0)]}
LIMITS = {'one': 3, 'two': None, 'three': 3}
REPLACED = 'alpha=replaced,Beta = compare'


class Tree:
    """A run-oracle-parallel output tree as the real runner and proxy leave it."""

    def __init__(self, base, hooks='', mode_numbers=None, known=None, counts=None):
        """known: edge-mode reports with these known outcomes per site index ((fall, taken) booleans)."""
        self.base = Path(base)
        self.root = self.base / 'out'
        self.golden = self.base / 'golden'
        self.names = self.base / 'hook-names.txt'
        self.sites = self.base / 'branch_sites.h'
        self.hooks = hooks
        self.base.mkdir(parents=True, exist_ok=True)
        self.names.write_text('\n'.join(NAMES) + '\n', encoding='utf-8')
        self.sites.write_text(SITES_H, encoding='utf-8')
        modes = mode_numbers or {n: 0 for n in NAMES}
        for index, (suite, counts) in enumerate((counts or COUNTS).items()):
            limit = LIMITS[suite]
            ticks = [[0, 47], [1, 94], [2, 141]]
            normalized = dict(format=1, frame_limit=limit, tick_fields=['frame', 'clock'], ticks=ticks,
                              watches=[], outputs={}, last_frame=2)
            self.write(self.golden / suite / 'normalized.json', normalized)
            run = self.root / suite / 'run'
            self.write(run / f'{suite}-normalized.json', normalized)
            self.write(run / f'{suite}-entry.json', dict(suite=suite, mode='check', hooks=hooks, result='identical',
                                                          proxy_sha256='synthetic-proxy', launch=dict(pid=100 + index)))
            sites = [dict(hook=h, va=va, condition=c, fall=f, taken=t, restored=True)
                     for (h, va, c), (f, t) in zip(SITES, counts)]
            extra = {}
            if known is not None:
                for i, site in enumerate(sites):
                    site['known_fall'], site['known_taken'] = known.get(i, (False, False))
                extra = dict(edge_roles=dict(jcc=1, redirect=1, fall=1, taken=0),
                             known_outcomes=sum(f + t for f, t in known.values()))
            self.write(self.root / suite / 'coverage.json', dict(
                schema=1, module_sha256=MODULE_HASH, mode='edge' if known is not None else 'saturate', pid=100 + index,
                install_thread=7, first_thread=7,
                other_thread=0, tick_thread=7, error=0, complete=True, reason='frame_limit' if limit else 'exit',
                has_tick=True, last_frame=2, frame_limit=limit or 0, first_hit_ticks=False, **extra,
                hooks=[dict(name=n, mode=modes[n]) for n in NAMES], sites=sites))

    @staticmethod
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def report(self, suite):
        return json.loads((self.root / suite / 'coverage.json').read_text(encoding='utf-8'))

    def build(self, kind='original'):
        return union.build(self.root, list(COUNTS), self.golden, self.hooks, self.names, self.sites, kind)

    def main(self, kind='original', hooks=None):
        argv = ['live_coverage_union.py', str(self.root), '--output', str(self.root), '--kind', kind, '--suites',
                ','.join(COUNTS), '--golden-root', str(self.golden), f'--hooks={self.hooks if hooks is None else hooks}',
                '--names', str(self.names), '--sites', str(self.sites)]
        old = sys.argv
        sys.argv = argv
        try:
            with contextlib.redirect_stdout(io.StringIO()) as out:
                code = union.main()
        finally:
            sys.argv = old
        return code, out.getvalue()


class UnionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_inventory_and_modes(self):
        tree = Tree(self.base)
        sites = union.branch_sites(tree.sites, NAMES)
        self.assertEqual([(s['hook'], s['va'], s['condition']) for s in sites], SITES)
        self.assertEqual(union.hook_modes(NAMES, ''), dict(alpha=0, beta=0, gamma=0, delta=0))
        self.assertEqual(union.hook_modes(NAMES, REPLACED), dict(alpha=1, beta=2, gamma=0, delta=0))
        for bad in ('alpha=replaced,alpha=original', 'omega=replaced', 'alpha=on', ' ', 'alpha=replaced,'):
            with self.assertRaises(ValueError, msg=bad):
                union.hook_modes(NAMES, bad)
        (self.base / 'dup.h').write_text(SITES_H + 'BRANCH(beta,0x401000,2,je,0x401008)\n', encoding='utf-8')
        with self.assertRaises(ValueError):
            union.branch_sites(self.base / 'dup.h', NAMES)

    def test_original_union(self):
        tree = Tree(self.base)
        code, out = tree.main()
        self.assertEqual(code, 0, out)
        result = json.loads((tree.root / 'coverage-union.json').read_text(encoding='utf-8'))
        self.assertTrue(result['valid'] and result['all_hooks_original'] and result['kind'] == 'original')
        self.assertEqual(result['totals'], dict(sites=4, both_ways=1, fall_only=1, taken_only=1, never=1, hooks=4,
                                                hooks_with_sites=3, outcomes_seen=4, outcomes_total=8))
        self.assertEqual(result['hook_modes'], dict(original=4, replaced=0, compare=0))
        self.assertEqual([s['suite'] for s in result['suites']], list(COUNTS))
        alpha = result['sites'][0]
        self.assertEqual((alpha['fall'], alpha['taken'], alpha['first_fall_suite'], alpha['first_taken_suite']), (3, 2, 'one', 'two'))
        self.assertEqual(result['residual_hooks'], [])
        # Per-suite summaries beside each suite's run.
        one = json.loads((tree.root / 'one' / 'coverage-summary.json').read_text(encoding='utf-8'))
        self.assertTrue(one['valid'])
        self.assertEqual([h['never'] for h in one['hooks']], [0, 1, 1, 0])
        self.assertIn('| `alpha` | 2 | 0 | 2 | 0 | 0 |', (tree.root / 'one' / 'coverage-summary.md').read_text(encoding='utf-8'))
        # The pasteable column (live_coverage_column.py) with the totals row.
        paste = (tree.root / 'coverage-column.md').read_text(encoding='utf-8')
        self.assertIn('| `alpha` | 1 / 1 / 0 |', paste)
        self.assertIn('| `delta` | 0 / 0 / 0 |', paste)
        self.assertIn('| all 4 hooks (4 sites) | 1 / 2 / 1 |', paste)
        self.assertEqual(paste, column.column(result, totals=True))
        self.assertNotIn('all 4 hooks', column.column(result))
        text = (tree.root / 'coverage-union.md').read_text(encoding='utf-8')
        self.assertIn('**1 of 4 sites both ways, 1 fall only, 1 taken only, 1 never**', text)
        self.assertIn('| `gamma` | 1 | 0x00401040 |', text)
        self.assertIn('Every hook original', text)

    def test_run_union_counts_residual_original_execution(self):
        modes = dict(alpha=1, beta=2, gamma=0, delta=0)
        tree = Tree(self.base, hooks=REPLACED, mode_numbers=modes)
        result = tree.build('run')
        self.assertTrue(result['valid'] and not result['all_hooks_original'])
        self.assertEqual(result['hook_modes'], dict(original=2, replaced=1, compare=1))
        self.assertEqual(result['residual_hooks'], ['alpha', 'beta'])
        code, out = tree.main('run')
        self.assertEqual(code, 0, out)
        self.assertIn('residual original execution', (tree.root / 'coverage-union.md').read_text(encoding='utf-8'))
        # An original-mode union refuses a replaced selection.
        with self.assertRaises(ValueError):
            tree.build('original')
        # The report's hook modes must be the requested ones.
        self.assertFalse(Tree(self.base / 'other', hooks=REPLACED).build('run')['valid'])

    def test_invalid_suite_invalidates_union_only(self):
        tree = Tree(self.base)
        normalized = tree.root / 'two' / 'run' / 'two-normalized.json'
        changed = json.loads(normalized.read_text(encoding='utf-8'))
        changed['ticks'][1][1] = 95
        normalized.write_text(json.dumps(changed), encoding='utf-8')
        code, out = tree.main()
        self.assertEqual(code, 1)
        result = json.loads((tree.root / 'coverage-union.json').read_text(encoding='utf-8'))
        self.assertFalse(result['valid'])
        self.assertIn('two', result['error'])
        self.assertEqual([s['valid'] for s in result['suite_status']], [True, False, True])
        self.assertTrue(json.loads((tree.root / 'one' / 'coverage-summary.json').read_text(encoding='utf-8'))['valid'])
        self.assertFalse(json.loads((tree.root / 'two' / 'coverage-summary.json').read_text(encoding='utf-8'))['valid'])
        self.assertFalse((tree.root / 'coverage-column.md').exists())
        self.assertTrue((tree.root / 'coverage-union.md').read_text(encoding='utf-8').startswith('Coverage union invalid'))
        with self.assertRaises(ValueError):
            column.column(result)

    def test_held_suite_window(self):
        """A held run (launch hold_frame) has no exit: its report window is frame_limit = hold_frame."""
        def held(tree, hold, **changes):
            entry = tree.root / 'two' / 'run' / 'two-entry.json'
            data = json.loads(entry.read_text(encoding='utf-8'))
            data['launch']['hold_frame'] = hold
            tree.write(entry, data)
            tree.write(tree.root / 'two' / 'coverage.json', dict(tree.report('two'), **changes))
        window = dict(reason='frame_limit', frame_limit=3)
        tree = Tree(self.base / 'ok')
        self.assertTrue(tree.build()['valid'])  # the exit report validates unheld
        held(tree, 3, **window)
        result = tree.build()
        self.assertTrue(result['valid'], result.get('error'))
        self.assertEqual([s['valid'] for s in result['suite_status']], [True, True, True])
        for name, hold, changes in (('hold without the window', 3, {}), ('other hold', 4, window),
                                    ('hold below one', 0, window), ('hold as text', '3', window)):
            tree = Tree(self.base / name.replace(' ', '-'))
            held(tree, hold, **changes)
            result = tree.build()
            self.assertFalse(result['valid'], name)
            self.assertEqual([s['valid'] for s in result['suite_status']], [True, False, True], name)
        # Without a launch hold the same report is not an exit report.
        tree = Tree(self.base / 'nohold')
        tree.write(tree.root / 'two' / 'coverage.json', dict(tree.report('two'), **window))
        self.assertFalse(tree.build()['valid'])

    def test_rejected_runs(self):
        cases = {
            'missing report': lambda t: (t.root / 'three' / 'coverage.json').unlink(),
            'wrong process': lambda t: t.write(t.root / 'one' / 'coverage.json', dict(t.report('one'), pid=999)),
            'unfinished window': lambda t: t.write(t.root / 'two' / 'coverage.json', dict(t.report('two'), reason='snapshot')),
            'site missing': lambda t: t.write(t.root / 'one' / 'coverage.json', dict(t.report('one'), sites=t.report('one')['sites'][:-1])),
            'other hook selection': lambda t: t.write(t.root / 'one' / 'run' / 'one-entry.json',
                                                      dict(json.loads((t.root / 'one' / 'run' / 'one-entry.json').read_text()), hooks='alpha=replaced')),
            'no entry': lambda t: (t.root / 'three' / 'run' / 'three-entry.json').unlink(),
        }
        for index, (name, mutate) in enumerate(cases.items()):
            tree = Tree(self.base / str(index))
            mutate(tree)
            result = tree.build()
            self.assertFalse(result['valid'], name)
            self.assertEqual(sum(not s['valid'] for s in result['suite_status']), 1, name)
        # Every report must carry the requested [coverage] mode.
        tree = Tree(self.base / 'mode')
        self.assertTrue(tree.build().get('valid'))
        counted = union.build(tree.root, list(COUNTS), tree.golden, '', tree.names, tree.sites, 'original', 'count')
        self.assertFalse(counted['valid'])
        self.assertIn('not the requested count', counted['suite_status'][0]['error'])
        tree = Tree(self.base / 'suites')
        old = sys.argv
        sys.argv = ['live_coverage_union.py', str(tree.root), '--output', str(tree.root), '--kind', 'run', '--suites', 'one,one',
                    '--golden-root', str(tree.golden), '--names', str(tree.names), '--sites', str(tree.sites)]
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(union.main(), 1)
        finally:
            sys.argv = old

    def test_edge_mode_with_baseline(self):
        """An edge-mode pass that did not watch the baseline's outcomes adds only new ones."""
        base_tree = Tree(self.base / 'base')
        self.assertEqual(base_tree.main()[0], 0)
        baseline_path = base_tree.root / 'coverage-union.json'
        baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
        known = union.known_outcomes(baseline, union.branch_sites(base_tree.sites, NAMES))
        self.assertEqual(known, {('alpha', '0x00401000'): (True, True), ('alpha', '0x00401010'): (True, False),
                                 ('beta', '0x00401020'): (False, True)})
        self.assertEqual(union.known_text(known), '0x00401000 1 1\n0x00401010 1 0\n0x00401020 0 1\n')
        # The new pass observes gamma's fall (new) and alpha 1010's taken (new); nothing else is watched.
        bits = {0: (True, True), 1: (True, False), 2: (False, True)}
        counts = {'one': [(0, 0), (0, 1), (0, 0), (1, 0)], 'two': [(0, 0)] * 4, 'three': [(0, 0)] * 4}
        tree = Tree(self.base / 'edge', known=bits, counts=counts)
        result = union.build(tree.root, list(counts), tree.golden, '', tree.names, tree.sites, 'original', 'edge', baseline_path)
        self.assertTrue(result['valid'], result)
        self.assertEqual((result['mode'], result['baseline_outcomes']), ('edge', 4))
        self.assertEqual(result['totals'], dict(sites=4, both_ways=2, fall_only=1, taken_only=1, never=0, hooks=4,
                                                hooks_with_sites=3, outcomes_seen=6, outcomes_total=8))
        changes = delta.delta(baseline, result)
        self.assertEqual(changes['newly_observed_outcomes'], [dict(hook='alpha', va='0x00401010', outcomes=['taken']),
                                                              dict(hook='gamma', va='0x00401040', outcomes=['fall'])])
        self.assertEqual(changes['closed_site_gaps'], [dict(hook='alpha', va='0x00401010')])
        self.assertIn('known outcomes were not watched', union.union_markdown(result))
        # Rejected: other known bits, another baseline size, a baseline without edge mode, an invalid baseline.
        wrong = Tree(self.base / 'wrong', known={0: (True, False), 1: (True, False), 2: (False, True)}, counts=counts)
        self.assertFalse(union.build(wrong.root, list(counts), wrong.golden, '', wrong.names, wrong.sites, 'original', 'edge', baseline_path)['valid'])
        plain = Tree(self.base / 'plain')
        self.assertFalse(union.build(plain.root, list(COUNTS), plain.golden, '', plain.names, plain.sites, 'original', None, baseline_path)['valid'])
        invalid = self.base / 'invalid.json'
        invalid.write_text(json.dumps(dict(valid=False)), encoding='utf-8')
        with self.assertRaises(ValueError):
            union.build(tree.root, list(counts), tree.golden, '', tree.names, tree.sites, 'original', 'edge', invalid)
        # Without a baseline, an edge pass must report no known outcomes.
        bare = Tree(self.base / 'bare', known={}, counts=COUNTS)
        self.assertTrue(union.build(bare.root, list(COUNTS), bare.golden, '', bare.names, bare.sites, 'original', 'edge')['valid'])
        # --write-known for the runner.
        target = self.base / 'known.txt'
        old = sys.argv
        sys.argv = ['live_coverage_union.py', str(tree.root), '--output', str(tree.root), '--kind', 'original', '--suites', 'one',
                    '--golden-root', str(tree.golden), '--names', str(tree.names), '--sites', str(tree.sites),
                    '--baseline', str(baseline_path), '--write-known', str(target)]
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(union.main(), 0)
        finally:
            sys.argv = old
        self.assertEqual(target.read_text(encoding='ascii'), union.known_text(known))

    def test_delta_between_unions(self):
        before = Tree(self.base / 'before').build()
        later = Tree(self.base / 'after')
        report = later.report('three')
        report['sites'][3]['fall'] = 1
        later.write(later.root / 'three' / 'coverage.json', report)
        after = later.build()
        changes = delta.delta(before, after)
        self.assertEqual(changes['newly_observed_outcomes'], [dict(hook='gamma', va='0x00401040', outcomes=['fall'])])
        self.assertEqual(changes['closed_site_gaps'], [])
        widened = copy.deepcopy(after)
        widened['sites'].append(dict(hook='delta', va='0x00401050', condition='je', fall=1, taken=1))
        self.assertEqual(len(delta.delta(before, widened)['newly_instrumented_sites']), 1)


if __name__ == '__main__':
    unittest.main()
