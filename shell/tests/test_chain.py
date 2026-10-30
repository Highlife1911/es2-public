"""Synthetic tests of the campaign chain loop (shell/tools/es2chain.py).

No CD data and no core: a fake engine stands in for the shell library, a
temporary directory for the game. Checks the shell-argument parser, the
decisions per launcher state, the continuation after the results path, the
DATA transaction (backup, write, restore), the stop paths and that a step
never answers 2 after a failure.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest


def load(path):
    spec = importlib.util.spec_from_file_location('es2chain_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def scratch_base():
    """TEST_TMPDIR, or under Bazel the output base: the execroot carries a
    .git link, and the chain refuses directories inside repositories."""
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


class FakeEngine:
    """SAV = b'SAV' + group, position, counter, player bay (four LE words);
    hangar bays 0..2, the one roster pilot in bay 1. Preparation returns files
    derived from the position and the RNG; the results path advances the
    position by one (ten per group) unless told otherwise."""

    def __init__(self):
        self.ending, self.advance_value, self.fail_prepare, self.raise_in = 5, 2, False, None
        self.lose_herc_at, self.bays = None, [0, 1, 2]
        self.calls = []

    @staticmethod
    def sav(group, position, counter=0, bay=2):
        return b'SAV' + struct.pack('<4h', group, position, counter, bay)

    def hangar(self, sav):
        return struct.unpack_from('<h', sav, 9)[0], [1, -1], self.bays

    def with_player_bay(self, sav, bay):
        return sav[:9] + struct.pack('<h', bay)

    def position(self, sav):
        if sav[:3] != b'SAV':
            raise ValueError('not a fake SAV')
        return struct.unpack_from('<2h', sav, 3)

    def mission(self, group, position):
        return f'C{group}_{position + 1:02d}'

    def prepare(self, sav, group, position, rng, selectors, language):
        self.calls.append(('prepare', group, position, rng[0], language, self.hangar(sav)[0]))
        if self.raise_in == 'prepare':
            raise RuntimeError('engine failure')
        if self.fail_prepare:
            return dict(error='not found 5')
        tag = f'{group}:{position}:{rng[0]}'.encode()
        out = {name: tag + b'|' + name.encode() for name in ('script.dat', 'MISSION.STR', 'player.mec')}
        out['mission.var'] = tag.ljust(2000, b'\0')
        out['rng'] = bytes([(rng[0] + 1) & 255]) + rng[1:]
        out['selectors'] = bytes([selectors[0] + 1]) + selectors[1:]
        return out

    def advance(self, sav, variables, results, rng, language, repair, restock):
        self.calls.append(('advance', results, repair, restock))
        if self.raise_in == 'advance':
            raise RuntimeError('engine failure')
        if results == b'reject':
            return dict(error='member not found 2')
        group, position, counter, bay = struct.unpack_from('<4h', sav, 3)
        position += 1
        if position >= 10:
            group, position = group + 1, 0
        if (group, position) == self.lose_herc_at:
            bay = -1
        return dict(ending=self.ending, advance=self.advance_value, group=group, position=position,
                    sav=self.sav(group, position, counter + 1, bay), rng=bytes([(rng[0] + 10) & 255]) + rng[1:])

    def summary(self, out, language):
        if 'error' in out:
            return dict(success=False, error=out['error'])
        return dict(success=True, ending=out['ending'], advance=out['advance'],
                    next=dict(group=out['group'], position=out['position']),
                    save=dict(size=len(out['sav'])))


class ChainTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='es2chain-test-', dir=scratch_base())
        self.root = Path(self.scratch.name)
        self.game = self.root / 'clone'
        (self.game / 'DATA').mkdir(parents=True)
        (self.game / 'DBSIM.EXE').write_bytes(b'stand-in')
        self.original = {'script.dat': b'old script', 'MISSION.STR': b'old strings',
                         'mission.var': b'old bank', 'player.mec': b'old mec', 'results.dat': b'old results'}
        for name, data in self.original.items():
            (self.game / 'DATA' / name).write_bytes(data)
        self.previous = os.getcwd()
        os.chdir(self.game)
        self.engine = FakeEngine()
        self.count = 0

    def tearDown(self):
        os.chdir(self.previous)
        self.scratch.cleanup()

    def init(self, missions=0, keep_data=False, language='GER', save=None, hangar='stop', exit_1=False):
        self.count += 1
        sav_path = self.root / f'start{self.count}.sav'
        sav_path.write_bytes(save or FakeEngine.sav(1, 0))
        rng = self.root / 'rng.bin'
        rng.write_bytes(bytes([7]) + bytes(113))
        args = argparse.Namespace(game_dir=self.game, save=sav_path, core=self.root / 'core.exe',
                                  cd_root=self.root, state=self.root / f'chain{self.count}', language=language,
                                  seed=0, rng_state=rng, reset_selectors=None, missions=missions,
                                  repair_option=2, restock_option=1, keep_data=keep_data, hangar=hangar,
                                  results_on_exit_1=exit_1)
        result = chain.init(args, self.engine)
        return Path(result['state']), result

    def step(self, state, *arguments):
        return chain.step(state, ['dummy', '-eggplant', *arguments], self.engine)

    def dbsim(self, results=b'mission results', bank=b'returned bank'):
        (self.game / 'DATA' / 'results.dat').write_bytes(results)
        (self.game / 'DATA' / 'mission.var').write_bytes(bank)

    def data(self, name):
        path = self.game / 'DATA' / name
        return path.read_bytes() if path.exists() else None

    def record(self, state):
        return json.loads((state / 'chain.json').read_text(encoding='utf-8'))

    def assert_restored(self):
        for name, data in self.original.items():
            self.assertEqual(self.data(name), data, name)
        self.assertIsNone(self.data('EXIT.CFG'))

    # ---- pure decisions --------------------------------------------------------
    def test_shell_arguments(self):
        parse = chain.parse_shell_arguments
        self.assertEqual(parse(['dummy', '-eggplant', '-X1', '-G']), (1, 'GER', []))
        self.assertEqual(parse(['dummy', '-eggplant', '-X3', '-s', '-a', '-@', '-F']), (3, 'FRE', ['-s', '-a', '-@']))
        self.assertEqual(parse(['dummy', '-eggplant', '-X4']), (4, 'ENG', []))
        self.assertEqual(parse(['-X-1']), (-1, 'ENG', []))
        for bad in ([], ['dummy'], ['-X1', '-X3'], ['-X1', '-Q'], ['-X1', 'extra'], ['-Xa']):
            with self.assertRaises(ValueError, msg=bad):
                parse(bad)

    def test_decide(self):
        idle, pending, done = dict(pending=None), dict(pending={'index': 0}), dict(pending=None, finished={'x': 1})
        self.assertEqual(chain.decide(idle, 1), 'prepare')
        self.assertEqual(chain.decide(dict(idle, results_on_exit_1=True), 1), 'prepare')
        self.assertEqual(chain.decide(dict(pending, results_on_exit_1=True), 1), 'advance-after-exit-1')
        self.assertEqual(chain.decide(pending, 1), 'stop-dbsim-without-results')
        for state in (3, 4):
            self.assertEqual(chain.decide(pending, state), 'advance')
            self.assertEqual(chain.decide(idle, state), 'stop-nothing-pending')
        self.assertEqual(chain.decide(pending, 6), 'stop-demo')
        for state in (0, 2, 5, 7, -1):
            self.assertEqual(chain.decide(pending, state), 'stop-unexpected-state')
        for state in (1, 3, 6):
            self.assertEqual(chain.decide(done, state), 'stop-finished')

    def test_continuation(self):
        go = dict(ending=5, advance=2)
        self.assertEqual(chain.continuation(go, 1, 0), chain.CONTINUE)
        self.assertEqual(chain.continuation(go, 1, 2), chain.CONTINUE)
        self.assertEqual(chain.continuation(go, 2, 2), 'stop-mission-limit')
        self.assertEqual(chain.continuation(dict(error='x'), 1, 0), 'stop-results-rejected')
        self.assertEqual(chain.continuation(dict(ending=3, advance=-1), 1, 0), 'stop-player-lost')
        self.assertEqual(chain.continuation(dict(ending=4, advance=-1), 1, 0), 'stop-other-mode')
        self.assertEqual(chain.continuation(dict(ending=5, advance=1), 1, 0), 'stop-return-1')
        self.assertEqual(chain.continuation(dict(ending=5, advance=0), 1, 0), 'stop-return-0')

    def test_freshness(self):
        before = dict(size=3, sha256='a', mtime_ns=1)
        self.assertFalse(chain.fresh(before, dict(before)))
        self.assertFalse(chain.fresh(None, None))
        self.assertTrue(chain.fresh(None, before))
        self.assertTrue(chain.fresh(before, dict(before, mtime_ns=2)))
        self.assertTrue(chain.fresh(before, dict(before, sha256='b')))

    def test_game_directory_guard(self):
        primary = self.root / 'installs' / 'Native-R111'
        (primary / 'DATA').mkdir(parents=True)
        (primary / 'DBSIM.EXE').write_bytes(b'x')
        with self.assertRaises(ValueError):
            chain.checked_game_dir(primary)
        with self.assertRaises(ValueError):
            chain.checked_game_dir(self.root)
        self.assertEqual(chain.checked_game_dir(self.game), self.game.resolve())

    # ---- whole steps -------------------------------------------------------------
    def test_three_missions_then_limit(self):
        state, result = self.init(missions=3)
        self.assertEqual(result['position'], [1, 0])
        self.assertIn('step --state', result['launch_shell'])
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.assertTrue(self.data('script.dat').startswith(b'1:0:7|'))
        self.assertEqual((state / 'data-backup' / 'results.dat').read_bytes(), b'old results')
        for number in range(1, 4):
            self.dbsim(results=f'results {number}'.encode())
            code = self.step(state, '-X3', '-G')
            self.assertEqual(code, 2 if number < 3 else 0)
            if number < 3:
                self.assertTrue(self.data('script.dat').startswith(f'1:{number}:'.encode()))
        record = self.record(state)
        self.assertEqual(record['finished']['reason'], 'stop-mission-limit')
        self.assertEqual(record['completed'], 3)
        self.assertEqual([m['position'] for m in record['missions']], [0, 1, 2, 3])
        self.assertEqual(record['save'], 'save-003.sav')
        self.assertEqual([m['input_save'] for m in record['missions']], [f'save-{i:03d}.sav' for i in range(4)])
        self.assertEqual([m['advance']['next']['position'] for m in record['missions'][:3]], [1, 2, 3])
        self.assertNotIn('advance', record['missions'][3])
        self.assertEqual(record['missions'][2]['advance']['dbsim']['results']['sha256'], chain.sha(b'results 3'))
        self.assertEqual(FakeEngine().position((state / 'save-003.sav').read_bytes()), (1, 3))
        # The next mission is prepared into the chain directory only; DATA is restored.
        self.assertTrue((state / 'mission-003' / 'player.mec').read_bytes().startswith(b'1:3:'))
        self.assertEqual((state / 'mission-001' / 'dbsim' / 'results.dat').read_bytes(), b'results 2')
        self.assert_restored()
        # RNG is carried: preparation +1, results path +10, per mission.
        self.assertEqual([c[3] for c in self.engine.calls if c[0] == 'prepare'], [7, 18, 29, 40])
        self.assertEqual({(c[2], c[3]) for c in self.engine.calls if c[0] == 'advance'}, {(2, 1)})
        self.assertEqual(record['selectors'][:2], '04')
        # A finished chain answers every further start with 0 and changes nothing.
        self.assertEqual(self.step(state, '-X1', '-G'), 0)
        self.assertEqual(self.record(state)['finished'], record['finished'])

    def test_dbsim_failure_and_stale_results(self):
        state, _ = self.init()
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.assertEqual(self.step(state, '-X1', '-G'), 0)     # DBSIM exit 1: launcher restarts the shell
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-dbsim-without-results')
        self.assert_restored()
        state, _ = self.init()
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.assertEqual(self.step(state, '-X3', '-G'), 0)     # results.dat untouched after the launch
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-results-rejected')
        self.assert_restored()
        # --results-on-exit-1: a fresh results.dat after exit 1 is read like after 3.
        state, _ = self.init(missions=1, exit_1=True)
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.dbsim()
        self.assertEqual(self.step(state, '-X1', '-G'), 0)
        record = self.record(state)
        self.assertEqual((record['finished']['reason'], record['completed']), ('stop-mission-limit', 1))
        self.assertEqual(record['missions'][0]['dbsim_state'], 1)
        state, _ = self.init(exit_1=True)
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.assertEqual(self.step(state, '-X1', '-G'), 0)     # nothing written: still no results
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-dbsim-without-results')
        self.assert_restored()

    def test_results_endings(self):
        for ending, advance, reason in ((3, -1, 'stop-player-lost'), (4, -1, 'stop-other-mode'),
                                        (5, 1, 'stop-return-1'), (5, 0, 'stop-return-0')):
            self.engine.ending, self.engine.advance_value = ending, advance
            state, _ = self.init()
            self.assertEqual(self.step(state, '-X1', '-G'), 2)
            self.dbsim()
            self.assertEqual(self.step(state, '-X4', '-G'), 0)
            record = self.record(state)
            self.assertEqual(record['finished']['reason'], reason)
            self.assertEqual(record['save'], 'save-001.sav')     # the state after the results path is kept
            self.assertEqual(len(record['missions']), 1)
            self.assert_restored()
        self.engine.ending, self.engine.advance_value = 5, 2
        state, _ = self.init()
        self.step(state, '-X1', '-G')
        self.dbsim(results=b'reject')
        self.assertEqual(self.step(state, '-X3', '-G'), 0)
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-results-rejected')
        self.assertEqual(self.record(state)['save'], 'save-000.sav')

    def test_stop_paths(self):
        cases = ((('-X6', '-G'), 'stop-demo'), (('-X3', '-G'), 'stop-nothing-pending'),
                 (('-X1', '-F'), 'stop-language-mismatch'), (('-X1', '-Z'), 'stop-bad-arguments'),
                 (('-X2', '-G'), 'stop-unexpected-state'))
        for arguments, reason in cases:
            state, _ = self.init()
            self.assertEqual(self.step(state, *arguments), 0)
            self.assertEqual(self.record(state)['finished']['reason'], reason)
            self.assertIsNone(self.record(state)['data_backup'])
            self.assert_restored()
        state, _ = self.init()
        os.chdir(self.root)
        self.assertEqual(self.step(state, '-X1', '-G'), 0)
        os.chdir(self.game)
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-wrong-directory')
        self.engine.fail_prepare = True
        state, _ = self.init()
        self.assertEqual(self.step(state, '-X1', '-G'), 0)
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-preparation-rejected')
        self.engine.fail_prepare = False
        for where in ('prepare', 'advance'):
            self.engine.raise_in = None
            state, _ = self.init()
            if where == 'advance':
                self.assertEqual(self.step(state, '-X1', '-G'), 2)
                self.dbsim()
            self.engine.raise_in = where
            self.assertEqual(self.step(state, '-X3' if where == 'advance' else '-X1', '-G'), 0)
            record = self.record(state)
            self.assertEqual(record['finished']['reason'], 'stop-exception')
            self.assertIn('engine failure', record['finished']['detail'])
            self.assert_restored()
        self.engine.raise_in = None

    def test_keep_data_and_restore_command(self):
        state, _ = self.init(missions=1, keep_data=True)
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.dbsim()
        self.assertEqual(self.step(state, '-X3', '-G'), 0)
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-mission-limit')
        self.assertTrue(self.data('script.dat').startswith(b'1:0:'))   # the played mission stays
        (self.game / 'DATA' / 'EXIT.CFG').write_bytes(b'written by DBSIM')
        restore = chain.Chain(state)
        self.assertEqual(sorted(restore.restore_data()), sorted([*self.original, 'EXIT.CFG']))
        restore.save()
        self.assert_restored()
        self.assertEqual(chain.Chain(state).restore_data(), [])        # only once

    def test_hangar_policy(self):
        self.assertIsNone(chain.hangar_choice(2, [1, -1], [0, 1, 2]))
        self.assertEqual(chain.hangar_choice(-1, [1, -1], [0, 1, 2]), 0)
        self.assertEqual(chain.hangar_choice(4, [0, 3], [0, 1, 3]), 1)       # bay 4 is gone
        self.assertEqual(chain.hangar_choice(-1, [0, 1], [0, 1]), -1)
        # Default: stop before preparing a mission for a player without a Herc.
        self.engine.lose_herc_at = (1, 2)
        state, _ = self.init()
        self.assertEqual(self.step(state, '-X1', '-G'), 2)
        self.dbsim()
        self.assertEqual(self.step(state, '-X3', '-G'), 2)
        self.dbsim(results=b'second')
        self.assertEqual(self.step(state, '-X3', '-G'), 0)
        record = self.record(state)
        self.assertEqual(record['finished']['reason'], 'stop-player-without-herc')
        self.assertEqual(record['save'], 'save-002.sav')
        self.assert_restored()
        # lowest-free: the player takes bay 0 (bay 1 is the roster pilot's); the
        # patched SAV is kept and used by the next results path.
        state, _ = self.init(hangar='lowest-free')
        for code in ('-X1', '-X3', '-X3', '-X3'):
            self.dbsim(results=code.encode() + bytes([len(self.engine.calls)]))
            self.assertEqual(self.step(state, code, '-G'), 2)
        record = self.record(state)
        self.assertEqual(record['missions'][2]['hangar'], dict(player_bay=-1, chosen=0, save='save-002-hangar.sav'))
        self.assertIsNone(record['missions'][3]['hangar'])
        self.assertEqual([c[5] for c in self.engine.calls if c[0] == 'prepare'][-4:], [2, 2, 0, 0])
        self.assertEqual(struct.unpack_from('<h', (state / 'save-002-hangar.sav').read_bytes(), 9)[0], 0)
        self.assertEqual(struct.unpack_from('<h', (state / 'save-003.sav').read_bytes(), 9)[0], 0)
        self.engine.bays = [1]
        state, _ = self.init(hangar='lowest-free')
        self.assertEqual(self.step(state, '-X1', '-G'), 0)                   # no free bay
        self.assertEqual(self.record(state)['finished']['reason'], 'stop-player-without-herc')

    def test_sav_layout(self):
        def pilot(name, bay):
            return bytes(4) + struct.pack('<h', len(name)) + name + struct.pack('<h', bay) + bytes(23)
        sav = bytearray()
        for i in range(33):
            sav += bytes([1]) + struct.pack('<h', i % 2) + bytes(10 * (i % 2))
        sav += bytes(22 + 152)
        sav += b''.join(pilot(b'P%d\0' % i, i - 30 if i > 30 else -1) for i in range(36))
        sav += bytes(12 + 4)
        offset = len(sav) + 6 + 3
        sav += pilot(b'Me\0', 4)
        sav += struct.pack('<h', 2)
        sav += struct.pack('<h', 0) + bytes(76) + struct.pack('<h', 1) + bytes(12)
        sav += struct.pack('<h', 5) + bytes(76) + struct.pack('<h', 0)
        sav += bytes(2044)
        sav = bytes(sav)
        player, bay, roster, bays = chain.sav_layout(sav)
        self.assertEqual((player, bay, bays), (offset, 4, [0, 5]))
        self.assertEqual(roster, [-1] * 31 + [1, 2, 3, 4, 5])
        changed = chain.with_player_bay(sav, -1)
        self.assertEqual(len(changed), len(sav))
        self.assertEqual(chain.sav_layout(changed)[1], -1)
        self.assertEqual(changed[:offset] + changed[offset + 2:], sav[:offset] + sav[offset + 2:])
        with self.assertRaises(ValueError):
            chain.sav_layout(sav[:offset])

    def test_step_entry_never_starts_dbsim_on_failure(self):
        self.assertEqual(chain.main(['step', '--state', str(self.root / 'missing')]), 0)
        self.assertEqual(chain.main(['step']), 0)
        state, _ = self.init()
        (state / 'chain.json').write_text('{"schema": 99}', encoding='utf-8')
        self.assertEqual(chain.main(['step', '--state', str(state), 'dummy', '-X1', '-G']), 0)

    def test_init_refusals(self):
        with self.assertRaises(ValueError):
            self.init(save=FakeEngine.sav(0, 3))             # training group 0 is not a campaign mission
        state, _ = self.init()
        with self.assertRaises(ValueError):                  # chain directories are new
            args = argparse.Namespace(game_dir=self.game, save=self.root / 'start1.sav', core=self.root,
                                      cd_root=self.root, state=state, language='GER', seed=0,
                                      rng_state=self.root / 'rng.bin', reset_selectors=None, missions=0,
                                      repair_option=0, restock_option=0, keep_data=False, hangar='stop',
                                      results_on_exit_1=False)
            chain.init(args, self.engine)


if __name__ == '__main__':
    chain = load(Path(sys.argv[1]).resolve())
    unittest.main(argv=[sys.argv[0]], verbosity=2)
