# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Launch checks and the preferences blocks against VSHELL's own
functions in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2_shell_launch_check and the
es2_shell_prefs_* session through the test driver launch_prefs_tool. The original:
the memory worker's slot mode (tools/reference-game/shell-invitro,
slots_oracle.py) with the operations `restore:N` (a --state SAV through
the original readers), `checks` (the player bay word and the answers of
0x4110c1, 0x411135 and 0x40fc7e), `rockroll` (the handler 0x445b99 itself:
a refusal is the warning box call 0x44d90c(n)), `prefs-load`, `prefs:NAME`
(the Preferences controls, Accept with the writer 0x40dd0a, Cancel) and
`mode:M` (the main menu's store 0x40ec56 with the read-modify-write
0x40dfa3).

1. Corpus: GAME_R and GAME_T as loaded, a new career, the SAV after the
   C1_01 results path, the SAV after every campaign preparation of the new
   career and every seventh of GAME_R, and service-bay states (all slots of
   a Herc emptied with item 0).
2. Synthetic sweep: seeded edits of those SAVs (player bay, build progress,
   the four condition words and others, slot item IDs, emptied hangar
   records, team capacity, the selected pilots' activity, order and bay).
   States on which the original would read outside the hangar (an active
   squad pilot without a bay, reached by Rock & Roll) are refused by ours
   and not run in the original; where an earlier rule stops Rock & Roll
   only the handler runs (not `checks`, which calls 0x40fc7e). Both counted.
3. Preferences: seeded sequences of controls, Accept, Cancel and mode
   stores on synthetic prefs files (missing, short, 54 bytes, longer); both
   blocks and the file after the sequence.

Arguments: launch_prefs_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2loadout.py

Sharding (shard.py): the units that run the original are numbered in one fixed order (the
worker chunks of the corpus and of the sweep, then the 40 preference sequences) and unit n runs in
Bazel shard n % shards. Everything that depends on our side only (the corpus, the sweep states,
ours' answers, the coverage counters and the set of preference control kinds) is drawn and tallied
in every shard, so the coverage checks are those of the union. Without sharding every unit runs.
"""
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

import shard

B = '\\'
BOX = 0x44d90c          # launch warning box 0x44d90c(n)
STATES_PER_RUN = 150


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Layout:
    """Offsets of the fields the launch checks read, by walking the SAV
    framing of docs/formats/mission.md (own walker)."""
    def __init__(self, sav):
        self.sav = sav
        at = 0
        for _ in range(33):
            at += 3 + 10 * self.word(at + 1)
        at += 22 + 152
        self.roster = []
        for _ in range(36):
            at = self.pilot(at, self.roster)
        self.selection = at
        self.team = at + 12
        at += 16
        players = []
        at = self.pilot(at, players)
        self.player = players[0]
        self.records = {}
        count = self.word(at)
        at += 2
        for _ in range(count):
            items = self.word(at + 78)
            self.records[self.word(at)] = (at, items)
            at += 80 + 12 * items
        self.end = at

    def word(self, offset):
        return struct.unpack_from('<h', self.sav, offset)[0]

    def pilot(self, at, out):
        suffix = at + 6 + self.word(at + 4)
        out.append(suffix)
        return suffix + 25


def put(sav, offset, value):
    out = bytearray(sav)
    struct.pack_into('<h', out, offset, value)
    return bytes(out)


class Test:
    def __init__(self, argv):
        (self.tool, self.core, self.worker, oracle_py, pins_py, backend_py, loadout_py) = [Path(a).resolve() for a in argv]
        self.oracle, self.pins = load(oracle_py, 'launch_oracle'), load(pins_py, 'launch_pins')
        self.backend, self.lo = load(backend_py, 'launch_backend'), load(loadout_py, 'launch_loadout')
        self.prep = self.lo.prep
        self.scratch = tempfile.TemporaryDirectory(prefix='es2launch-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        self.failures, self.report, self.runs = [], {}, 0
        self.numbered = 0

    def mine(self):
        """Numbers the next unit that runs the original; True when this shard runs it."""
        number = self.numbered
        self.numbered += 1
        return shard.owns(number)

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

    def ours(self, states):
        run = subprocess.run([str(self.tool), 'check'], input=''.join(s.hex() + '\n' for s in states),
                             capture_output=True, text=True, check=True)
        out = []
        for line in run.stdout.splitlines():
            parts = line.split()
            out.append(tuple(map(int, parts[1:])) if parts[0] == 'CHECK' else None)
        return out

    def worker_run(self, ops, states=(), disk=None, save=None):
        self.runs += 1
        scratch = self.tmp / f'run-{self.runs}'
        scratch.mkdir()
        # slots_oracle.run has no --state; the same argv here, decoded by it.
        argv = [str(self.worker), '--cd-root', str(self.cd), '--language', 'G', '--mode', '1', '--pipe']
        files = {'sav' + B + 'gamefile.str': self.gamefile}
        files.update(disk or {})
        for index, (name, data) in enumerate(files.items()):
            path = scratch / f'disk-{index:02d}.bin'
            path.write_bytes(data)
            argv += ['--file', name, str(path)]
        for index, state in enumerate(states):
            path = scratch / f'state-{index:03d}.sav'
            path.write_bytes(state)
            argv += ['--state', str(path)]
        if save is not None:
            (scratch / 'save.sav').write_bytes(save)
            argv += ['--save', str(scratch / 'save.sav')]
        for op in ops:
            argv += ['--op', op]
        run = subprocess.run(argv, capture_output=True, timeout=300)
        if run.returncode:
            raise RuntimeError(f'worker exited {run.returncode}: {run.stderr.decode("utf-8", "replace")[-2000:]}')
        return self.oracle.decode(run.stdout)

    # ---- launch checks ------------------------------------------------------------
    def compare(self, label, states):
        """Ours against checks + rockroll of the original for every state."""
        mine = self.ours(states)
        runnable = [(s, m) for s, m in zip(states, mine) if m is not None]
        refused = len(states) - len(runnable)
        for _, m in runnable:       # ours only: tallied for every state in every shard
            if m[3] < 0:
                self.outside += 1
            self.tally(m)
        for start in range(0, len(runnable), STATES_PER_RUN):
            if not self.mine():
                continue
            chunk = runnable[start:start + STATES_PER_RUN]
            for k in range(len(chunk)):
                shard.ran(f'{label} state {start + k}')
            ops = []
            for i, (_, m) in enumerate(chunk):
                # squad -1 (ours: an active squad pilot without a bay, reached
                # only when an earlier rule stops Rock & Roll): 0x40fc7e would
                # read outside the hangar, so `checks` (which calls it) is not
                # run there; the handler itself stops before it.
                ops += [f'restore:{i}'] + (['checks'] if m[3] >= 0 else []) + ['rockroll']
            # Every state goes through the original readers: hand over the
            # file whose restore gives the memory state (lists reversed).
            result = self.worker_run(ops, [self.memory(s) for s, _ in chunk])
            answers = iter(result['metadata']['checks'])
            boxes = [call[1] for call in result['metadata']['ui_calls'] if call[0] == BOX]
            checked = sum(m[3] >= 0 for _, m in chunk)
            self.check(len(result['metadata']['checks']) == checked,
                       f'{label}: {len(result["metadata"]["checks"])} answers for {checked} states')
            want_boxes = []
            for k, (state, m) in enumerate(chunk):
                bay, functional, armed, squad, warning = m
                if squad >= 0:
                    a = next(answers, (None,) * 4)
                    theirs = (a[0], a[1], a[2], a[3])
                    self.check(theirs == (bay, functional, armed, squad),
                               f'{label} {start + k}: original {theirs}, ours {m}')
                if warning >= 0:
                    want_boxes.append(warning)
            self.check(boxes == want_boxes, f'{label}: warning boxes {boxes}, ours {want_boxes}')
        return len(runnable), refused

    def tally(self, m):
        warning = m[4]
        key = {-1: 'launch', 0: 'not_functional', 1: 'unarmed', 2: 'no_herc', 3: 'squad_unarmed'}[warning]
        self.coverage[key] = self.coverage.get(key, 0) + 1

    def corpus(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        rng = self.prep.initial_rng(self.cd, 71)
        career, rng_after, _ = self.backend.new_game_request(self.core, self.cd, 'GER', 'TESTPILOT', 1, 1, 0, rng)
        corpus = {'GAME_R': self.memory(game_r), 'GAME_T': self.memory(game_t), 'new career': career}
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        if (golden / 'results.dat').is_file():
            chain = load(Path(self.backend.__file__).parent / 'es2chain.py', 'launch_chain')
            out = chain.Engine(self.core, self.cd).advance(
                game_r, (golden / 'mission.var').read_bytes(), (golden / 'results.dat').read_bytes(), rng, 'GER', 0, 0)
            corpus['after C1_01'] = out['sav']
        missions = self.prep.campaign(self.core, self.cd)
        chosen = [(career, g, p) for g, p, _ in missions if g >= 1]
        chosen += [(corpus['GAME_R'], g, p) for i, (g, p, _) in enumerate(missions) if g >= 1 and i % 7 == 0]
        for base, g, p in chosen:
            request = self.prep.request(group=g, position=p, mode=1, language='GER', rng=rng_after,
                                        selectors=bytes(60), tables=self.prep.default_tables(self.cd), save=base,
                                        prepared_save=True)
            out, = self.prep.prepare_batch(self.core, self.cd, [request])
            if self.check('error' not in out, f'preparation ({g},{p}): {out.get("error")}'):
                corpus[f'prepared {"career" if base is career else "GAME_R"} ({g},{p})'] = out['prepared_save']
        # Service bay: every slot of one Herc emptied (item 0), per Herc.
        for label in ('new career', 'GAME_R'):
            description = self.lo.describe(self.core, self.cd, corpus[label])
            for record in description['bays']:
                ops = [(self.lo.BAY_MOUNT, record['index'], s, 0, 0) for s in range(record['capacity'])]
                sav, _ = self.lo.service_bay(self.core, self.cd, corpus[label], ops)
                corpus[f'{label} bay {record["index"]} emptied'] = sav
        return corpus

    def variants(self, corpus, count, seed):
        rng = random.Random(seed)
        bases = list(corpus.values())
        out = []
        while len(out) < count:
            sav = rng.choice(bases)
            for _ in range(rng.randint(1, 4)):
                sav = self.edit(rng, sav)
            out.append(sav)
        return out

    def edit(self, rng, sav):
        layout = Layout(sav)
        r = rng.random()
        bays = sorted(layout.records)
        player_bay = layout.word(layout.player)
        if r < 0.12:
            return put(sav, layout.player, rng.choice([-1] + list(range(8))))
        if r < 0.45 and bays:
            k = player_bay if player_bay in layout.records and rng.random() < 0.7 else rng.choice(bays)
            at, items = layout.records[k]
            x = rng.random()
            if x < 0.25:
                return put(sav, at + 2 + 70, rng.choice((0, 50, 99, 100, 101)))
            if x < 0.75:
                word = rng.choice((13, 14, 18, 21, 13, 14, 18, 21, 0, 12, 15, 22, 23))
                return put(sav, at + 2 + 4 + 2 * word, rng.choice((-1, 0, 50, 51, 100)))
            if items:
                value = rng.choice((0, 5, 28, 29, 30, 32))
                # one slot item, or every one of the record (an all-29 Herc
                # is unarmed, an all-28 one armed)
                for j in ([rng.randrange(items)] if rng.random() < 0.5 else range(items)):
                    sav = put(sav, at + 80 + 12 * j + 2, value)
            return sav
        if r < 0.55 and bays:
            k = rng.choice(bays)
            at, items = layout.records[k]
            return put(sav[:at + 80] + sav[at + 80 + 12 * items:], at + 78, 0)
        if r < 0.65:
            return put(sav, layout.team, rng.choice((0, 1, 2, 3, 4)))   # the worker preflight: 0..4
        # a selected pilot of one roster group
        group = rng.randrange(3)
        selected = struct.unpack_from('<H', sav, layout.selection + 2 * group)[0]
        suffix = layout.roster[group * 12 + selected]
        y = rng.random()
        if y < 0.35:
            out = bytearray(sav)
            out[suffix + 2] = rng.choice((0, 1))
            return bytes(out)
        if y < 0.7:
            return put(sav, suffix + 5, rng.choice((-1, 1, 2, 3)))
        return put(sav, suffix, rng.choice([-1] + list(range(8))))

    def launch_checks(self):
        self.coverage, self.outside = {}, 0
        corpus = self.corpus()
        n, refused = self.compare('corpus', list(corpus.values()))
        self.report['corpus'] = dict(states=len(corpus), compared=n, refused=refused, coverage=dict(self.coverage),
                                     prepared=sum(k.startswith('prepared') for k in corpus),
                                     emptied=sum(k.endswith('emptied') for k in corpus))
        self.check(refused == 0, f'corpus: {refused} states refused by ours')
        self.coverage = {}
        states = self.variants(corpus, 600, 1201)
        self.outside = 0
        n, refused = self.compare('sweep', states)
        self.report['sweep'] = dict(states=len(states), compared=n, refused_outside_hangar=refused,
                                    box_only_outside_hangar=self.outside, coverage=dict(self.coverage))
        for key in ('launch', 'not_functional', 'unarmed', 'no_herc', 'squad_unarmed'):
            self.check(self.coverage.get(key, 0) >= 5, f'sweep: only {self.coverage.get(key, 0)} cases of {key}')

    # ---- preferences --------------------------------------------------------------
    CONTROLS = {1: 'music', 2: 'sfx', 3: 'high', 4: 'low', 5: 'window', 6: 'full', 7: 'repair0', 8: 'repair1',
                9: 'repair2', 10: 'build0', 11: 'build1'}

    def prefs_sequence(self, rng, file):
        """One sequence: ours (launch_prefs_tool prefs) and the original's controls,
        both blocks and the file compared at the end."""
        ours = [file.hex() if file is not None else '-']
        theirs = ['prefs-load', 'prefs:show']
        for _ in range(rng.randint(1, 12)):
            r = rng.random()
            if r < 0.6:
                control = rng.choice(list(self.CONTROLS))
                ours.append(f'control {control}')
                # Full Screen shows the Alert box; its ACCEPT stores the byte
                theirs += ['prefs:full', 'prefs:alert'] if control == 6 else ['prefs:' + self.CONTROLS[control]]
            elif r < 0.75:
                ours.append('accept')
                theirs += ['prefs:accept', 'prefs:show']
            elif r < 0.88:
                ours.append('cancel')
                theirs += ['prefs:cancel', 'prefs:show']
            else:
                mode = rng.choice((0, 1))
                ours.append(f'store 42 {mode}')
                theirs.append(f'mode:{mode}')
        self.prefs_coverage.update(o.split()[0] if not o.startswith('control') else o for o in ours[1:])
        if not self.mine():
            return 0
        shard.ran(f'prefs {ours}')
        lines = subprocess.run([str(self.tool), 'prefs'], input=''.join(o + '\n' for o in ours),
                               capture_output=True, text=True, check=True).stdout.splitlines()
        state = [line.split() for line in lines if line.startswith('STATE ')][-1]
        disk = next(line.split()[1] for line in lines if line.startswith('FILE '))
        result = self.worker_run(theirs, disk={'data' + B + 'prefs.cfg': file} if file is not None else None)
        meta = result['metadata']
        written = {k.lower(): v for k, v in result['disk'].items()}.get('data' + B + 'prefs.cfg')
        label = f'prefs {ours}'
        self.check(meta['prefs'] == state[1] and meta['prefs_committed'] == state[2],
                   f'{label}: blocks {meta["prefs"]}/{meta["prefs_committed"]}, ours {state[1]}/{state[2]}')
        self.check((written.hex() if written is not None else '-') == disk,
                   f'{label}: file {None if written is None else written.hex()}, ours {disk}')
        return len(ours) - 1

    def preferences(self):
        rng, steps, sequences = random.Random(1202), 0, 0
        self.prefs_coverage = set()
        files = [None, bytes(range(1, 11)), bytes((5 * i + 1) & 0xff for i in range(54)),
                 bytes((3 * i + 7) & 0xff for i in range(60)), bytes(54)]
        for i in range(40):
            steps += self.prefs_sequence(rng, files[i % len(files)])
            sequences += 1
        self.report['preferences'] = dict(sequences=sequences, operations=steps, kinds=sorted(self.prefs_coverage))
        self.check(len(self.prefs_coverage) == len(self.CONTROLS) + 3, f'preferences: kinds {sorted(self.prefs_coverage)}')

    def main(self):
        self.launch_checks()
        self.preferences()
        self.report['worker_runs'] = self.runs


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:8])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, 'launch-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    shard.finish('launch')
    print(json.dumps(dict(report=test.report, failures=test.failures), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
