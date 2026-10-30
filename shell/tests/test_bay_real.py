# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Service bay, WEAPONS screen: our operations byte for byte against
VSHELL's own functions in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/hangar.h and
es2_shell_launch_files through es2prep_core (--service-bay, --launch-files)
via es2loadout.py. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro/slots_oracle.py) with the
operations mount (0x411b10), guidance (0x43e346), rebalance (0x412a37),
launch (0x40f68c) and arm (0x41501f after the ARM loader 0x41508f), and the
original SAV writers (--emit-save). The worker starts from a SAV restored by
the original readers, which reverses every item list; a memory state S is
handed to it as es2_shell_loaded_save(S) (an involution, slots_real_test).

1. ARM: 0x41501f for every vehicle, every slot (and no slot) and every item
   0..32 against the CD's ARM tables (es2loadout.arm_admits) and the
   availability flags.
2. Launch files: for the SAV after every campaign preparation of a new
   career (50 missions) and of GAME_R (every seventh), our 0x40f68c equals
   the preparation's player.mec and mission.var; for eight of them also the
   original 0x40f68c.
3. Operations: seeded random sequences of clicks (with the ARM answer),
   mounts, guidance and rebalance (options 0 and 1) on GAME_R, GAME_T, a new
   career, three prepared states and a results-path SAV, plus targeted
   cases; the resulting SAV equals the original writers' after the same
   mounts (the clicks the click rule applied), guidance and rebalance, and
   for prepared states the launch files equal the original 0x40f68c. The
   click rule itself (0x43fdac) is UI code and not run in vitro; its branches
   are unit-tested (campaign_test).

Arguments: es2loadout.py es2menu_backend.py es2prep_core es2shellprep
slots_oracle.py reference_pins.py

Sharding (shard.py): the work is split over Bazel shards. Shard 0 is
the coverage shard: it steps every seeded sequence on our side and tallies
the coverage counters over all of them (they depend only on our side and are
asserted over the union). The other shards own the comparisons with the
original: ARM vehicle v, the launch-file state i and the sequence number k
belong to work shard k % (shards - 1) (counted from shard 1). Every shard
draws the seeded streams in full, so the cases are those of an unsharded run.
Without sharding one process does everything.
"""
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

import shard

RESERVE = 1            # shard 0 owns no comparison with the original (see the docstring)
B = '\\'


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Test:
    def __init__(self, argv):
        (loadout_py, backend_py, self.core, self.worker, oracle_py, pins_py) = [Path(a).resolve() for a in argv]
        self.lo, self.backend = load(loadout_py, 'bay_loadout'), load(backend_py, 'bay_backend')
        self.prep, self.oracle, self.pins = self.lo.prep, load(oracle_py, 'bay_oracle'), load(pins_py, 'bay_pins')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2bay-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        self.failures, self.report, self.runs = [], {}, 0
        self.catalog = self.lo.Catalog(self.core, self.cd)
        self.tables = self.prep.results_tables(self.cd)
        self.coverage = dict(clicks=0, clicks_applied=0, mounts=0, empty_list=0, unmount=0, guidance=0,
                             rebalance_0=0, rebalance_1=0, bank_changed=0, launches=0)

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def memory(self, sav):
        lines = subprocess.run([str(self.core), str(self.cd), '--loaded-save'], input=sav.hex() + '\n',
                               capture_output=True, text=True, check=True).stdout.splitlines()
        if lines[0] != 'OK':
            raise ValueError(lines[0])
        return bytes.fromhex(lines[1])

    def original(self, state, ops, emit=True):
        """The worker from memory state `state` (sav\\gamefile.str: the CD's)."""
        self.runs += 1
        return self.oracle.run(self.worker, self.cd, {'sav' + B + 'gamefile.str': self.gamefile}, ops,
                               self.tmp / f'run-{self.runs}', save=self.memory(state), emit=emit)

    @staticmethod
    def disk(result, name):
        return {k.lower(): v for k, v in result['disk'].items()}.get(name)

    # ---- 1. ARM ----------------------------------------------------------------
    def arm(self):
        cases = 0
        base = self.lo.default_save(self.core, self.cd, self.catalog, 'GER', 1, 0)
        for vehicle in range(9):
            if not shard.owns(vehicle, RESERVE):
                continue
            shard.ran(f'arm:{vehicle}')
            plan, _, _ = self.lo.resolve(self.catalog, dict(player={'herc': vehicle, 'weapons': []}),
                                         self.lo.describe(self.core, self.cd, base))
            sav = self.lo.build_sav(self.core, self.cd, self.catalog, 'GER', 1, 0, plan, base)
            description = self.lo.describe(self.core, self.cd, sav)
            capacity = self.catalog.vehicles[vehicle][1]
            ops = [f'arm:0:{slot}:{item}' for slot in list(range(capacity)) + [255] for item in range(33)]
            # build_sav writes a file image; the worker restores it as written
            self.runs += 1
            result = self.oracle.run(self.worker, self.cd, {'sav' + B + 'gamefile.str': self.gamefile}, ops,
                                     self.tmp / f'run-{self.runs}', save=sav)
            answers = result['metadata']['arm']
            self.check(len(answers) == len(ops), f'ARM vehicle {vehicle}: {len(answers)} answers')
            for bay, slot, item, answer in answers:
                ours = slot == 255 or (bool(description['flags'][item]) and
                                       self.lo.arm_admits(self.catalog, vehicle, item, slot))
                cases += 1
                self.check(bool(answer) == ours, f'ARM vehicle {vehicle} slot {slot} item {item}: original {answer}')
        self.report['arm_cases'] = cases

    # ---- 2. launch files ----------------------------------------------------------
    def prepared(self, base, group, position, rng):
        request = self.prep.request(group=group, position=position, mode=1, language='GER', rng=rng,
                                    selectors=bytes(60), tables=self.prep.default_tables(self.cd), save=base,
                                    prepared_save=True)
        out, = self.prep.prepare_batch(self.core, self.cd, [request])
        if 'error' in out:
            raise ValueError(f'preparation ({group},{position}): {out["error"]}')
        return out

    def launch_files(self, career, game_r, rng):
        missions = self.prep.campaign(self.core, self.cd)
        cases, worker = 0, 0
        chosen = [(career, g, p) for g, p, _ in missions if g >= 1]
        chosen += [(game_r, g, p) for i, (g, p, _) in enumerate(missions) if g >= 1 and i % 7 == 0]
        states = []
        needed = {0, 17, len(chosen) - 1}       # the prepared states the operations start from
        for i, (base, g, p) in enumerate(chosen):
            mine = shard.owns(i, RESERVE)
            if not (mine or i in needed):
                states.append(None)
                continue
            out = self.prepared(base, g, p, rng)
            states.append(out['prepared_save'])
            if not mine:
                continue
            shard.ran(f'launch:{i}')
            mec, var = self.lo.launch_files(self.core, self.cd, out['prepared_save'])
            cases += 1
            self.check(mec == out['player.mec'] and var == out['mission.var'],
                       f'launch files of ({g},{p}) differ from the preparation')
            if i % 7 == 3:
                result = self.original(out['prepared_save'], ['launch'], emit=False)
                worker += 1
                self.check(self.disk(result, 'data' + B + 'player.mec') == mec and
                           self.disk(result, 'data' + B + 'mission.var') == var,
                           f'launch files of ({g},{p}) differ from the original 0x40f68c')
        self.report['launch_files'] = dict(states=cases, original=worker)
        return states

    # ---- 3. operations ---------------------------------------------------------------
    def worker_ops(self, ops, applied):
        out = []
        for (kind, bay, slot, value, _), done in zip(ops, applied):
            if kind in (self.lo.BAY_MOUNT, self.lo.BAY_CLICK) and done:
                out.append(f'mount:{bay}:{slot}:{value}')
            elif kind == self.lo.BAY_GUIDANCE:
                out.append(f'guidance:{bay}:{slot}:{value}')
            elif kind == self.lo.BAY_REBALANCE:
                out.append(f'rebalance:{value}')
        return out

    def tally(self, ops, applied, trail):
        """Coverage of one accepted sequence; trail[k] is the state before operation k
        (the states of the step-by-step run, trail[-1] the result)."""
        for k, (op, done) in enumerate(zip(ops, applied)):
            kind, bay, slot, value, _ = op
            if kind == self.lo.BAY_CLICK:
                self.coverage['clicks'] += 1
                self.coverage['clicks_applied'] += done
            if kind in (self.lo.BAY_MOUNT, self.lo.BAY_CLICK) and done:
                self.coverage['mounts'] += 1
                self.coverage['unmount'] += value == 0
                self.coverage['empty_list'] += (value != 0 and
                                                self.lo.describe(self.core, self.cd, trail[k])['counts'][value] == 0)
            if kind == self.lo.BAY_GUIDANCE:
                self.coverage['guidance'] += 1
            if kind == self.lo.BAY_REBALANCE:
                before = self.lo.describe(self.core, self.cd, trail[k])
                self.coverage[f'rebalance_{min(value, 1)}'] += 1
                after = self.lo.describe(self.core, self.cd, trail[k + 1])
                self.coverage['bank_changed'] += after['bank5'] != before['bank5']

    def sequence(self, label, state, ops, launch):
        """Apply step by step (dropping refused operations), compare the batch,
        then the original."""
        number = self.sequence_number
        self.sequence_number += 1
        reserved = shard.reserved(RESERVE)
        if not (reserved or shard.owns(number, RESERVE)):
            return 0
        current, good, applied, trail = state, [], [], [state]
        for op in ops:
            try:
                current, done = self.lo.service_bay(self.core, self.cd, current, [op], self.tables)
            except ValueError:
                continue
            good.append(op)
            applied += done
            trail.append(current)
        if not good:
            return 0
        if reserved:     # the coverage shard: our side only, over every sequence
            if not self.worker_ops(good, applied) + (['launch'] if launch else []):
                return 0
            self.coverage['launches'] += bool(launch)
            self.tally(good, applied, trail)
            return 1
        shard.ran(f'sequence:{number}:{label}')
        batch, batch_applied = self.lo.service_bay(self.core, self.cd, state, good, self.tables)
        self.check(batch == current and batch_applied == applied, f'{label}: batch differs from single steps')
        wops = self.worker_ops(good, applied) + (['launch'] if launch else [])
        if not wops:
            return 0
        result = self.original(state, wops)
        if not self.check(result['emitted'] == current, f'{label}: SAV differs from the original after {wops}'):
            return 1
        if launch:
            mec, var = self.lo.launch_files(self.core, self.cd, current)
            self.coverage['launches'] += 1
            self.check(self.disk(result, 'data' + B + 'player.mec') == mec and
                       self.disk(result, 'data' + B + 'mission.var') == var,
                       f'{label}: launch files differ from the original after {wops}')
        if shard.TOTAL == 1:     # sharded runs tally in the coverage shard
            self.tally(good, applied, trail)
        return 1

    def random_ops(self, rng, state, count):
        description = self.lo.describe(self.core, self.cd, state)
        bays = [(b['index'], b['capacity']) for b in description['bays'] if b['capacity']]
        ops = []
        for _ in range(count):
            bay, capacity = rng.choice(bays)
            slot, r = rng.randrange(capacity), rng.random()
            if r < 0.45:
                item = rng.choice(self.catalog.rows)
                ops.append(self.lo.click_op(self.catalog, description, bay, slot, item) if rng.random() < 0.8
                           else (self.lo.BAY_CLICK, bay, slot, item, 0))
            elif r < 0.72:
                ops.append((self.lo.BAY_MOUNT, bay, slot, rng.randrange(33), 0))
            elif r < 0.9:
                ops.append((self.lo.BAY_MOUNT, bay, slot, rng.choice((13, 14, 15, 16)), 0))
                ops.append((self.lo.BAY_GUIDANCE, bay, slot, rng.randrange(4), 0))
            else:
                ops.append((self.lo.BAY_REBALANCE, 0, 0, rng.choice((0, 1, 7)), 0))
        return ops

    def operations(self, corpus, prepared):
        rng, cases = random.Random(1010), 0
        self.sequence_number = 0
        for label, state in list(corpus.items()) + [(f'prepared {i}', s) for i, s in enumerate(prepared)]:
            launch = label.startswith('prepared')
            for trial in range(10):
                cases += self.sequence(f'{label}/{trial}', state, self.random_ops(rng, state, rng.randint(1, 14)), launch)
        # Targeted: drain a list and keep mounting it (empty list), unmount
        # everything with item 0, re-click a mounted rack after a guidance
        # change, and both rebalance options on a poor and a rich career.
        state = corpus['GAME_R']
        description = self.lo.describe(self.core, self.cd, state)
        bay = description['bays'][0]
        item = next(i for i in self.catalog.rows if i and description['counts'][i] and description['flags'][i])
        slots = [(b['index'], s) for b in description['bays'] for s in range(b['capacity'])]
        ops = [(self.lo.BAY_MOUNT, b, s, item, 0) for b, s in slots[:description['counts'][item] + 2]]
        ops += [(self.lo.BAY_MOUNT, b, s, 0, 0) for b, s in slots]
        cases += self.sequence('targeted drain', state, ops, False)
        rack = next((i for i in (13, 14, 15, 16) if description['counts'][i]), None)
        if self.check(rack is not None, 'no missile rack in GAME_R'):
            ops = [(self.lo.BAY_MOUNT, bay['index'], 0, rack, 0), (self.lo.BAY_GUIDANCE, bay['index'], 0, 3, 0),
                   (self.lo.BAY_CLICK, bay['index'], 0, rack, 1), (self.lo.BAY_GUIDANCE, bay['index'], 0, 0, 0)]
            cases += self.sequence('targeted rack', state, ops, False)
        for label in ('new career', 'GAME_T'):
            for option in (0, 1):
                cases += self.sequence(f'targeted rebalance {label} {option}', corpus[label],
                                       [(self.lo.BAY_REBALANCE, 0, 0, option, 0)] * 2, False)
        self.report['operations'] = dict(sequences=cases, coverage=self.coverage)
        if shard.TOTAL == 1 or shard.reserved(RESERVE):     # the counters are the union's: see the docstring
            for key in ('clicks_applied', 'empty_list', 'unmount', 'guidance', 'rebalance_0', 'rebalance_1',
                        'bank_changed', 'launches'):
                self.check(self.coverage[key] > 0, f'coverage: no case of {key}')
            self.check(self.coverage['clicks_applied'] < self.coverage['clicks'], 'coverage: every click applied')

    def main(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        rng = self.prep.initial_rng(self.cd, 71)
        career, rng_after, _ = self.backend.new_game_request(self.core, self.cd, 'GER', 'TESTPILOT', 1, 1, 0, rng)
        corpus = {'GAME_R': self.memory(game_r), 'GAME_T': self.memory(game_t), 'new career': career}
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        if (golden / 'results.dat').is_file():
            chain = load(Path(self.backend.__file__).parent / 'es2chain.py', 'bay_chain')
            out = chain.Engine(self.core, self.cd).advance(
                game_r, (golden / 'mission.var').read_bytes(), (golden / 'results.dat').read_bytes(), rng, 'GER', 0, 0)
            corpus['after C1_01'] = out['sav']
        self.arm()
        states = self.launch_files(career, corpus['GAME_R'], rng_after)
        self.operations(corpus, [states[0], states[17], states[-1]])
        self.report['worker_runs'] = self.runs


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:7])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, 'bay-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    shard.finish('bay')
    print(json.dumps(dict(report=test.report, failures=test.failures), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
