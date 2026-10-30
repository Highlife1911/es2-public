"""bisect_hooks.py on synthetic regression outputs; no game process required."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bisect_hooks as bisect

# Synthetic registry: alpha is split (a3 comes after gamma), d2 stays
# original in the failed run, b3 runs in compare mode. c1 is only entered
# through b1's original code, so with b1 replaced it is never entered.
HOOKS = [('a1', 'alpha'), ('a2', 'alpha'), ('b1', 'beta'), ('b2', 'beta'), ('b3', 'beta'),
         ('c1', 'gamma'), ('a3', 'alpha'), ('d1', 'delta'), ('d2', 'delta')]
SELECTION = 'a1=replaced,a2=replaced,b1=replaced,b2=replaced,b3=compare,c1=replaced,a3=replaced,d1=replaced,d2=original'
SUITES = ['acceptance', 'success', 'collision']


def entered(replaced):
    """Synthetic call counters: every non-original hook is entered except c1 under a replaced b1."""
    return {h: (0 if h == 'c1' and 'b1' in replaced else 100 + i) for i, (h, _) in enumerate(HOOKS)}


class Fixture:
    def __init__(self, root):
        self.root = Path(root)
        self.registry = self.root / 'hook_registry.h'
        self.names = self.root / 'hook-names.txt'
        lines = ['/* synthetic */'] + [f'ES2_HOOK({h}, 0x{0x1000 + 16 * i:05x}, d[{i}], NULL, {g})'
                                        for i, (h, g) in enumerate(HOOKS)]
        self.registry.write_bytes(('\n'.join(lines) + '\n').encode())
        self.names.write_bytes(''.join(f'{h}\n' for h, _ in HOOKS).encode())
        self.logs = self.root / 'logs'
        self.logs.mkdir()
        self.count = 0

    def run(self, hooks, outcomes, commit='abc123', layout='parallel', counters=True, entry_hooks=None, name=None, zero=()):
        """Write a regression output directory. outcomes: suite -> identical|different|error."""
        self.count += 1
        directory = self.logs / (name or f'run{self.count:02d}')
        directory.mkdir()
        # Lenient on purpose: refusal tests write malformed selections.
        replaced = {e.split('=')[0] for e in hooks.split(',') if e and not e.endswith('=original')} & {h for h, _ in HOOKS}
        calls = {h: 0 if h in zero else n for h, n in entered(replaced).items()}
        results = []
        for suite, outcome in outcomes.items():
            base = directory / suite / 'run' if layout == 'parallel' else directory
            base.mkdir(parents=True, exist_ok=True)
            results.append(dict(suite=suite, result='identical' if outcome == 'identical' else 'DIFFERENT'))
            if outcome != 'error':
                problems = [] if outcome == 'identical' else [dict(kind='watch', frame=7)]
                (base / f'{suite}-comparison.json').write_text(json.dumps(dict(identical=not problems, problems=problems)))
            (base / f'{suite}-entry.json').write_text(json.dumps(dict(suite=suite, hooks=hooks if entry_hooks is None else entry_hooks)))
            if counters:
                tick = {'event': 'tick', 'sequence': 9}
                for hook in sorted(replaced | {'a1'}):
                    tick[f'hk_{hook}_calls'] = calls[hook] if hook in replaced else 0
                    tick[f'hk_{hook}_bypassed'] = 0
                watch = json.dumps({'event': 'watch', 'sequence': 9})
                (base / f'{suite}.jsonl').write_text(json.dumps({'event': 'tick', 'sequence': 8}) + '\n' + json.dumps(tick) + '\n' + watch + '\n')
            if layout == 'parallel':
                (base / 'summary.json').write_text(json.dumps(dict(mode='check', hooks=hooks, results=[results[-1]])))
        summary = dict(mode='check', output=str(directory), commit=commit, dirty_tree=False, hooks=hooks, results=results)
        # PowerShell's UTF8 encoding writes a byte order mark.
        (directory / 'summary.json').write_bytes(b'\xef\xbb\xbf' + json.dumps(summary).encode())
        return directory

    def plan(self, outcomes=None, **options):
        failed = self.run(SELECTION, outcomes or {'acceptance': 'identical', 'success': 'different', 'collision': 'different'})
        state = self.root / 'state.json'
        return bisect.plan(failed, state, self.registry, self.names, **options), state

    def drive(self, state_path, fails, limit=20):
        """Answer every pending step with the synthetic oracle `fails(replaced) -> bool`."""
        state = bisect.load_state(state_path)
        while state['pending'] and limit:
            step = state['pending']
            outcome = 'different' if fails(set(step['replaced'])) else 'identical'
            result = self.run(step['hooks'], {s: outcome for s in step['suites']})
            state = bisect.next_step(state_path, result)
            limit -= 1
        return state


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_plan_keeps_hooks_never_entered_and_groups_by_registry_group(self):
        state, _ = self.fx.plan()
        self.assertEqual(state['baseline']['failing'], ['success', 'collision'])
        self.assertEqual(state['kept'], ['c1'])
        groups = [(i['label'], i['hooks']) for i in state['search']['items']]
        self.assertEqual(groups, [('alpha', ['a1', 'a2', 'a3']), ('beta', ['b1', 'b2', 'b3']), ('delta', ['d1'])])
        # Kept hooks are unverified on their own: prefix 0 is still open.
        self.assertEqual((state['search']['lo'], state['search']['hi']), (-1, 3))
        step = state['pending']
        self.assertEqual(step['suites'], ['success', 'collision'])
        self.assertEqual(step['replaced'], ['a1', 'a2', 'c1', 'a3'])
        # Explicit mode for every hook of the failed selection; compare stays compare.
        self.assertEqual(step['hooks'], 'a1=replaced,a2=replaced,b1=original,b2=original,b3=original,'
                                        'c1=replaced,a3=replaced,d1=original,d2=original')
        self.assertTrue(step['output'].endswith('bisect-run01-step01'))

    def test_no_keep_searches_every_selected_hook(self):
        state, _ = self.fx.plan(keep_dormant=False)
        self.assertEqual(state['kept'], [])
        self.assertIn('c1', [h for i in state['search']['items'] for h in i['hooks']])
        self.assertEqual(state['search']['lo'], 0)

    def test_missing_tick_log_keeps_nothing(self):
        failed = self.fx.run(SELECTION, {'success': 'different'}, counters=False)
        state = bisect.plan(failed, self.fx.root / 's.json', self.fx.registry, self.fx.names)
        self.assertEqual(state['kept'], [])
        self.assertTrue(any('no tick log' in n for n in state['notes']))

    def test_plan_refusals(self):
        cases = [(self.fx.run(SELECTION, {'success': 'identical'}), 'no failing suite'),
                 (self.fx.run('', {'success': 'different'}), 'no non-original hook'),
                 (self.fx.run('a1=original', {'success': 'different'}), 'no non-original hook'),
                 (self.fx.run('zz=replaced', {'success': 'different'}), 'unknown hook'),
                 (self.fx.run('a1=replaced,a1=original', {'success': 'different'}), 'Duplicate')]
        for i, (directory, message) in enumerate(cases):
            with self.assertRaisesRegex(bisect.BisectError, message):
                bisect.plan(directory, self.fx.root / f'refused{i}.json', self.fx.registry, self.fx.names)
            self.assertFalse((self.fx.root / f'refused{i}.json').exists())
        state, path = self.fx.plan()
        with self.assertRaisesRegex(bisect.BisectError, 'exists'):
            bisect.plan(Path(state['baseline']['directory']), path, self.fx.registry, self.fx.names)

    def test_registry_must_match_name_list(self):
        self.fx.names.write_bytes(b'a1\na2\n')
        with self.assertRaisesRegex(bisect.BisectError, 'disagree'):
            bisect.load_registry(self.fx.registry, self.fx.names)

    def test_error_without_comparison_in_failed_run_counts_as_failure(self):
        failed = self.fx.run(SELECTION, {'success': 'error'})
        state = bisect.plan(failed, self.fx.root / 's.json', self.fx.registry, self.fx.names)
        self.assertTrue(state['errors_fail'])
        self.assertEqual(state['baseline']['outcomes'], {'success': 'error'})


class SearchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_every_single_entered_culprit_is_found(self):
        for culprit in ['a1', 'a2', 'a3', 'b1', 'b2', 'b3', 'd1']:
            with self.subTest(culprit=culprit), tempfile.TemporaryDirectory() as tmp:
                fx = Fixture(tmp)
                _, path = fx.plan()
                state = fx.drive(path, lambda replaced: culprit in replaced)
                self.assertEqual(state['result']['hook'], culprit)
                self.assertTrue(state['result']['standalone'])
                self.assertLessEqual(len(state['steps']), 5)
                self.assertEqual(state['result']['baseline_calls'].keys(), {'success', 'collision'})

    def test_first_group_culprit_verifies_kept_hooks_alone(self):
        _, path = self.fx.plan()
        state = self.fx.drive(path, lambda replaced: 'a1' in replaced)
        prefixes = [(s['description'], s['verdict']) for s in state['steps']]
        self.assertIn(('group prefix 0/3 of the entered hooks: none', 'pass'), prefixes)
        self.assertEqual(state['result']['va'], '0x401000')

    def test_interaction_names_the_completing_hook(self):
        _, path = self.fx.plan()
        state = self.fx.drive(path, lambda replaced: {'a2', 'b2'} <= replaced)
        result = state['result']
        self.assertEqual((result['hook'], result['group'], result['standalone']), ('b2', 'beta', False))
        self.assertEqual(result['needs_prefix'], ['a1', 'a2', 'a3', 'b1'])
        self.assertEqual(state['steps'][-1]['kind'], 'confirm')
        self.assertEqual(state['steps'][-1]['replaced'], ['b2', 'c1'])

    def test_no_confirm_leaves_isolation_open(self):
        _, path = self.fx.plan(confirm=False)
        state = self.fx.drive(path, lambda replaced: 'b2' in replaced)
        self.assertEqual(state['result']['hook'], 'b2')
        self.assertIsNone(state['result']['standalone'])
        self.assertNotIn('confirm', [s['kind'] for s in state['steps']])

    def test_kept_hook_entered_under_original_caller_is_searched(self):
        # c1 is wrong but only entered when b1 runs original code.
        _, path = self.fx.plan()
        state = self.fx.drive(path, lambda replaced: 'c1' in replaced and 'b1' not in replaced)
        self.assertEqual(state['result']['hook'], 'c1')
        self.assertTrue(any('kept hooks alone' in n for n in state['notes']))
        self.assertTrue(any('kept hook c1 was entered' in w for s in state['steps'] for w in s['warnings']))
        self.assertIn('check the warnings', bisect.render(state, path))

    def test_small_selection(self):
        failed = self.fx.run('c1=replaced,b1=replaced', {'success': 'different'})
        path = self.fx.root / 's.json'
        state = bisect.plan(failed, path, self.fx.registry, self.fx.names)
        self.assertEqual(state['kept'], ['c1'])
        state = self.fx.drive(path, lambda replaced: 'b1' in replaced)
        self.assertEqual(state['result']['hook'], 'b1')
        self.assertTrue(all(s['hooks'].count('=') == 2 for s in state['steps']))

    def test_no_selected_hook_entered_searches_them_all(self):
        failed = self.fx.run('a1=replaced,a2=replaced', {'success': 'different'}, zero={'a1', 'a2'})
        path = self.fx.root / 's.json'
        state = bisect.plan(failed, path, self.fx.registry, self.fx.names)
        self.assertEqual(state['kept'], [])
        self.assertTrue(any('No selected hook was entered' in n for n in state['notes']))
        state = self.fx.drive(path, lambda replaced: 'a2' in replaced)
        self.assertEqual(state['result']['hook'], 'a2')

    def test_serial_layout(self):
        failed = self.fx.run(SELECTION, {'success': 'different', 'collision': 'identical'}, layout='serial')
        run = bisect.read_run(failed, [h for h, _ in HOOKS])
        self.assertEqual({s: r['outcome'] for s, r in run['suites'].items()}, {'success': 'different', 'collision': 'identical'})
        self.assertEqual(run['suites']['success']['counters']['c1'], 0)
        self.assertEqual(run['suites']['success']['detail'], 'watch')


class StepValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(self.tmp.name)
        self.state, self.path = self.fx.plan()
        self.step = self.state['pending']

    def tearDown(self):
        self.tmp.cleanup()

    def unchanged(self):
        self.assertEqual(bisect.load_state(self.path), self.state)

    def test_wrong_selection_commit_reuse_and_missing_suite_are_refused(self):
        outcomes = {s: 'identical' for s in self.step['suites']}
        cases = [(self.fx.run(SELECTION, outcomes), 'different hook selection'),
                 (self.fx.run(self.step['hooks'], outcomes, commit='other'), 'same build'),
                 (self.fx.run(self.step['hooks'], {'success': 'identical'}), 'no result for suite collision'),
                 (Path(self.state['baseline']['directory']), 'already used'),
                 (self.fx.logs / 'missing', 'no check-mode summary')]
        for directory, message in cases:
            with self.assertRaisesRegex(bisect.BisectError, message):
                bisect.next_step(self.path, directory)
            self.unchanged()

    def test_entry_with_other_hooks_is_an_error(self):
        result = self.fx.run(self.step['hooks'], {s: 'identical' for s in self.step['suites']}, entry_hooks='a1=replaced')
        with self.assertRaises(bisect.Inconclusive):
            bisect.next_step(self.path, result)
        self.unchanged()

    def test_error_step_is_inconclusive_unless_errors_fail(self):
        result = self.fx.run(self.step['hooks'], {'success': 'identical', 'collision': 'error'})
        with self.assertRaisesRegex(bisect.Inconclusive, 'collision'):
            bisect.next_step(self.path, result)
        self.unchanged()
        state = bisect.next_step(self.path, result, errors_fail=True)
        self.assertEqual(state['steps'][0]['verdict'], 'fail')
        self.assertEqual(state['steps'][0]['result']['suites']['collision']['outcome'], 'error')

    def test_finished_state_refuses_more_results(self):
        state = self.fx.drive(self.path, lambda replaced: 'd1' in replaced)
        self.assertIsNotNone(state['result'])
        with self.assertRaisesRegex(bisect.BisectError, 'finished'):
            bisect.next_step(self.path, self.fx.run(SELECTION, {'success': 'identical'}))


class CommandLineTests(unittest.TestCase):
    def test_plan_next_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            fx = Fixture(tmp)
            failed = fx.run(SELECTION, {'success': 'different'})
            path = fx.root / 'state.json'

            def call(*args):
                out, err = io.StringIO(), io.StringIO()
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    code = bisect.main(list(args))
                return code, out.getvalue(), err.getvalue()

            code, out, _ = call('plan', '--regression', str(failed), '--state', str(path),
                                '--registry', str(fx.registry), '--names', str(fx.names), '--step-root', str(fx.logs))
            self.assertEqual(code, 0)
            self.assertIn('run-oracle-parallel.ps1', out)
            self.assertIn("-Suite 'success'", out)
            self.assertIn(' next --state ', out)
            self.assertEqual(path.read_bytes().count(b'\r'), 0)
            while True:
                step = bisect.load_state(path)['pending']
                if not step:
                    break
                outcome = 'different' if 'b3' in step['replaced'] else 'identical'
                result = fx.run(step['hooks'], {'success': outcome})
                code, out, _ = call('next', '--state', str(path), '--result', str(result))
                self.assertEqual(code, 0)
            self.assertIn('First offending hook: b3 (group beta, 0x401040)', out)
            code, out, _ = call('status', '--state', str(path), '--json')
            self.assertEqual((code, json.loads(out)['result']['hook']), (0, 'b3'))
            code, _, err = call('next', '--state', str(path), '--result', str(failed))
            self.assertEqual(code, 2)
            self.assertIn('finished', err)

    def test_inconclusive_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            fx = Fixture(tmp)
            state, path = fx.plan()
            result = fx.run(state['pending']['hooks'], {s: 'error' for s in state['pending']['suites']})
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(bisect.main(['next', '--state', str(path), '--result', str(result)]), 3)


class TickLogTests(unittest.TestCase):
    def test_last_tick_found_across_chunk_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'run.jsonl'
            tick = json.dumps({'event': 'tick', 'hk_a1_calls': 5, 'hk_a1_bypassed': 2, 'hk_b1_calls': 0, 'pad': 'x' * 20000})
            watch = '{"event":"watch","name":"w","hex":"' + 'ab' * 300 + '"}\n'
            path.write_bytes((json.dumps({'event': 'tick', 'hk_a1_calls': 1}) + '\n' + tick + '\n'
                              + watch * 4000).encode())
            self.assertGreater(path.stat().st_size, 2 << 20)
            self.assertEqual(bisect.last_tick_counters(path, ['a1', 'b1', 'c1']), {'a1': 7, 'b1': 0})
            self.assertIsNone(bisect.last_tick_counters(Path(tmp) / 'missing.jsonl', ['a1']))


class RepositoryRegistryTests(unittest.TestCase):
    def test_repository_registry_parses(self):
        hooks = bisect.load_registry()
        names = [h[0] for h in hooks]
        self.assertGreaterEqual(len(hooks), 121)
        self.assertEqual((names[0], hooks[0][1], hooks[0][2]), ('rng_next', 'rng', 0x492ee0))
        self.assertEqual(bisect.parse_selection('RNG_NEXT = Replaced', set(names)), {'rng_next': 'replaced'})


if __name__ == '__main__':
    unittest.main()
