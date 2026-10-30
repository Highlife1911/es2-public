# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Service-bay editors REPAIR, BUILD and the Herc scrap against
VSHELL's own handlers and functions in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/editors.h through the test
driver editors_tool (every operation applied on its own, refusals dropped,
the batch checked against the steps). The original: the memory worker's
slot mode (tools/reference-game/shell-invitro, slots_oracle.py) with the
operations: `restore:N` (a --state SAV through the original readers),
`select:B`, `screen:W`, `herc-type:T`, `repair-pick:T:R` (0x434549),
`repair-snapshot` (0x433f86), `repair-one` (0x4351bd), `repair-all`
(0x4352e9), `repair-cancel` (0x435403), `build:T` (0x4475ce), `scrap`
(0x448326 with the trapped and recorded 0x43dcdd), `rebalance:O` (0x412a37),
`editor-answers` (0x41124e, 0x411101, 0x412baa, 0x41232a, 0x411a78,
0x41401b, 0x414174 and the bank words the enable rules read), `editor-state`
and `emit` (the original SAV writers).

Per sequence: the SAV after it equals the original writers' byte for byte;
before every REPAIR, REPAIR ALL, Scrap and Build the numbers the screen
decides on equal the original functions' answers, and ours applied the
operation exactly when the enable rule on the original's numbers holds;
after every row click and at the end the selection and the snapshot equal
the original's globals; the bay the scrap selects in REPAIR equals the
original's 0x43dcdd argument. Which bay the REPAIR show and the squad panel
keep is UI code (0x43397c, 0x43dcdd write unbuilt widgets) and is checked
through 0x41124e's answer and the unit test.

Corpus: GAME_R, GAME_T, a new career, the SAV after the C1_01 results path,
three prepared states, then seeded edits of them (conditions, build
progress, salvage, the nine-word and five-entry banks, pilot orders) and
seeded operation sequences.

Arguments: editors_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2loadout.py
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
import time

B = '\\'
SELECT = 0x43dcdd           # recorded squad-panel bay selection (scrap in REPAIR)
CASES_PER_RUN = 16
SWEEP = 150                 # seeded edited states
OPEN, CLOSE, BAY, PICK, REPAIR, REPAIR_ALL, CANCEL, BUILD_TYPE, BUILD, SCRAP = range(1, 11)
EDITOR_SPANS = ((0x47665c, 10), (0x46f868, 18), (0x46f87a, 18), (0x476a4c, 52))   # ARMORY rows
VALUES = (0, 1, 2, 29, 30, 31, 45, 50, 59, 60, 61, 79, 80, 81, 85, 89, 90, 91, 95, 99, 100, 100, 100, -5, 120)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def exe_tables(root, spans, reference):
    """Spans of the hash-checked VSHELL.EXE, read at run time."""
    import hashlib
    image = (Path(root) / 'VER95' / 'VSHELL.EXE').read_bytes()
    if hashlib.sha256(image).hexdigest() != reference:
        raise ValueError('VSHELL.EXE differs from the documented reference')
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    out = b''
    for va, n in spans:
        for i in range(count):
            address, size, raw = struct.unpack_from('<III', image, pe + 24 + optional + i * 40 + 12)
            offset = va - 0x400000 - address
            if 0 <= offset and offset + n <= size:
                out += image[raw + offset:raw + offset + n]
                break
        else:
            raise ValueError('table outside file sections')
    return out


class Layout:
    """Offsets in a SAV (framing of docs/formats/mission.md, own walker)."""
    def __init__(self, sav):
        self.sav = sav
        at = 0
        for _ in range(33):
            at += 3 + 10 * self.word(at + 1)
        self.five = at
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
            self.records[self.word(at)] = at + 2      # fixed bytes
            at += 80 + 12 * self.word(at + 78)
        self.nine = at
        self.tally = at + 18

    def word(self, offset):
        return struct.unpack_from('<h', self.sav, offset)[0]

    def pilot(self, at, out):
        suffix = at + 6 + self.word(at + 4)
        out.append(suffix)
        return suffix + 25

    def selected(self):
        """Suffix offsets of the three selected pilots."""
        return [self.roster[12 * g + self.word(self.selection + 2 * g)] for g in range(3)]


def put16(out, offset, value):
    struct.pack_into('<h', out, offset, value)


class Test:
    def __init__(self, argv):
        (self.tool, self.core, self.worker, oracle_py, pins_py, backend_py, loadout_py) = [Path(a).resolve() for a in argv]
        self.oracle, self.pins = load(oracle_py, 'editors_oracle'), load(pins_py, 'editors_pins')
        self.backend, self.lo = load(backend_py, 'editors_backend'), load(loadout_py, 'editors_loadout')
        self.prep = self.lo.prep
        self.scratch = tempfile.TemporaryDirectory(prefix='es2editors-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        self.tables = self.prep.results_tables(self.cd)
        self.editor_tables = exe_tables(self.cd, EDITOR_SPANS, self.prep.REFERENCE_HASH)
        self.failures, self.report, self.runs = [], {}, 0
        self.coverage = {}

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def count(self, key, n=1):
        self.coverage[key] = self.coverage.get(key, 0) + n

    def memory(self, savs):
        """es2_shell_loaded_save of each SAV (the file the original reader
        restores to that memory state)."""
        run = subprocess.run([str(self.core), str(self.cd), '--loaded-save'], input=''.join(s.hex() + '\n' for s in savs),
                             capture_output=True, text=True, check=True)
        lines, out = run.stdout.splitlines(), []
        for i in range(0, len(lines), 2):
            if lines[i] != 'OK':
                raise ValueError(lines[i])
            out.append(bytes.fromhex(lines[i + 1]))
        return out

    # ---- ours ---------------------------------------------------------------------
    def ours(self, cases):
        text = self.tables.hex() + '\n' + self.editor_tables.hex() + '\n'
        for case in cases:
            text += f'CASE {case["bay"]}\nSAVE {case["state"].hex()}\n'
            text += ''.join(f'OP {k} {a} {b}\n' for k, a, b in case['ops']) + 'END\n'
        run = subprocess.run([str(self.tool), str(self.cd)], input=text, capture_output=True, text=True, check=True)
        lines = iter(run.stdout.splitlines())
        out = []
        for case in cases:
            result = dict(steps=[], reject=None)
            line = next(lines)
            if line.startswith('REJECT'):
                result['reject'] = line.split()[1]
                next(lines)
                out.append(result)
                continue
            view = self.view(line)
            for line in lines:
                parts = line.split()
                if parts[0] == 'STEP':
                    session = self.session(next(lines))
                    after = self.view(next(lines))
                    result['steps'].append(dict(op=case['ops'][int(parts[1])], applied=int(parts[2]), before=view,
                                                session=session, view=after))
                    view = after
                elif parts[0] == 'DROP':
                    self.count('dropped')
                elif parts[0] == 'BATCH':
                    self.check(parts[1] == 'same', f'{case["label"]}: batch differs from the steps')
                elif parts[0] == 'SAV':
                    result['sav'] = bytes.fromhex(parts[1])
                elif parts[0] == 'END':
                    break
            out.append(result)
        return out

    @staticmethod
    def session(line):
        p = line.split()
        return dict(screen=int(p[1]), bay=int(p[2]), tab=int(p[3]), row=int(p[4]), family=int(p[5]), index=int(p[6]),
                    type=int(p[7]), salvage=int(p[8]), conditions=bytes.fromhex(p[9]))

    @staticmethod
    def view(line):
        keys = ('salvage', 'bank', 'first', 'one', 'record', 'condition', 'item', 'all', 'value', 'bank9', 'build',
                'repair', 'repair_all', 'scrap', 'build_enabled')
        return dict(zip(keys, map(int, line.split()[1:])))

    # ---- the original -----------------------------------------------------------------
    def translate(self, case, result):
        """Worker operations for one case (after restore:N) and what to expect."""
        ops = [f'select:{case["bay"] % 65536}', 'screen:65535', 'herc-type:65535', 'repair-pick:0:0']
        # The snapshot's condition words are compared once a snapshot with a
        # bay was taken in this case (with bay -1 the original keeps the
        # previous ones: another case's in the same worker run).
        expect = dict(answers=[], states=[], selects=[], repair_opened=False)
        screen, snapped = -1, False
        for step in result['steps']:
            (kind, a, b), applied, before, after = step['op'], step['applied'], step['before'], step['session']
            if kind == OPEN and applied:
                if screen in (2, 3, 4):
                    ops.append(f'rebalance:{b}')
                if a == 3:
                    ops += [f'select:{after["bay"] % 65536}', 'repair-snapshot', 'repair-pick:0:0', 'screen:3']
                    expect['repair_opened'] = True
                    snapped = snapped or after['bay'] != -1
                else:
                    ops += ['herc-type:0', 'screen:4']
            elif kind == CLOSE:
                ops += [f'rebalance:{b}', f'screen:{a % 65536}']
            elif kind == BAY and applied:
                ops.append(f'select:{a}')
                if after['screen'] == 3:
                    ops += ['repair-pick:0:0', 'repair-snapshot']
                    snapped = True
            elif kind == PICK:
                ops += [f'repair-pick:{a}:{b}', 'editor-state']
                expect['states'].append((after, snapped))
            elif kind == BUILD_TYPE and applied:
                ops.append(f'herc-type:{a}')
            elif kind in (REPAIR, REPAIR_ALL, SCRAP, BUILD):
                ops.append('editor-answers')
                expect['answers'].append((kind, applied, before, step['bay_before']))
                if applied:
                    if kind == REPAIR:
                        ops.append('repair-one')
                    elif kind == REPAIR_ALL:
                        ops.append('repair-all')
                    elif kind == BUILD:
                        ops.append(f'build:{after["type"]}')
                    else:
                        ops += [f'screen:{after["screen"]}', 'scrap']
                        if after['screen'] == 3:
                            expect['selects'].append(after['bay'])
                            ops += [f'select:{after["bay"] % 65536}', 'repair-pick:0:0', 'repair-snapshot']
                            snapped = snapped or after['bay'] != -1
            elif kind == CANCEL:
                ops.append('repair-cancel')
            screen = after['screen']
        ops += ['editor-state', 'emit']
        expect['states'].append((result['steps'][-1]['session'] if result['steps'] else None, snapped))
        return ops, expect

    def worker_run(self, ops, states):
        self.runs += 1
        scratch = self.tmp / f'run-{self.runs}'
        scratch.mkdir()
        argv = [str(self.worker), '--cd-root', str(self.cd), '--language', 'G', '--mode', '1', '--pipe']
        path = scratch / 'gamefile.str'
        path.write_bytes(self.gamefile)
        argv += ['--file', 'sav' + B + 'gamefile.str', str(path)]
        for index, state in enumerate(states):
            path = scratch / f'state-{index:03d}.sav'
            path.write_bytes(state)
            argv += ['--state', str(path)]
        for op in ops:
            argv += ['--op', op]
        run = subprocess.run(argv, capture_output=True, timeout=300)
        if run.returncode:
            raise RuntimeError(f'worker exited {run.returncode}: {run.stderr.decode("utf-8", "replace")[-3000:]}')
        return self.oracle.decode(run.stdout)

    def compare(self, cases):
        results = self.ours(cases)
        runnable = [(c, r) for c, r in zip(cases, results) if r['reject'] is None]
        self.count('rejected_states', len(cases) - len(runnable))
        files = self.memory([c['state'] for c, _ in runnable])
        planned = []
        for (case, result), file in zip(runnable, files):
            for k, step in enumerate(result['steps']):
                step['bay_before'] = result['steps'][k - 1]['session']['bay'] if k else case['bay']
            worker_ops, expect = self.translate(case, result)
            planned.append((case, result, file, worker_ops, expect))
        # Worker limits: 512 --op, 256 answer and 256 state records, 65,000
        # metadata characters per run.
        runs, current = [], []
        for item in planned:
            trial = current + [item]
            ops = sum(len(x[3]) + 1 for x in trial)
            answers = sum(len(x[4]['answers']) for x in trial)
            states = sum(len(x[4]['states']) for x in trial)
            if current and (len(trial) > CASES_PER_RUN or ops > 500 or answers > 250 or states > 140 or
                            answers * 80 + states * 230 > 55000):
                runs.append(current)
                trial = [item]
            current = trial
        if current:
            runs.append(current)
        for chunk_items in runs:
            chunk = [(x[0], x[1]) for x in chunk_items]
            expects = [x[4] for x in chunk_items]
            ops = []
            for i, x in enumerate(chunk_items):
                ops += [f'restore:{i}'] + x[3]
            out = self.worker_run(ops, [x[2] for x in chunk_items])
            meta = out['metadata']
            answers, states = iter(meta['editor_answers']), iter(meta['editor'])
            selects = [c[1] & 0xffff for c in meta['ui_calls'] if c[0] == SELECT]
            selects = iter([x - 65536 if x >= 32768 else x for x in selects])
            emitted, at = out['disk'].get('editors.emit', b''), 0
            for (case, result), expect in zip(chunk, expects):
                label = case['label']
                size, = struct.unpack_from('<I', emitted, at)
                theirs = emitted[at + 4:at + 4 + size]
                at += 4 + size
                self.check(theirs == result['sav'], f'{label}: SAV differs from the original ({case["ops"]})')
                for kind, applied, view, bay in expect['answers']:
                    self.answer(label, next(answers), kind, applied, view, bay)
                for state, snapped in expect['states']:
                    self.state(label, next(states), state, expect['repair_opened'], snapped)
                for bay in expect['selects']:
                    self.check(next(selects, None) == bay, f'{label}: scrap selected another bay than 0x43dcdd')
                self.tally(case, result)
            self.check(next(answers, None) is None and next(states, None) is None and next(selects, None) is None,
                       'worker records left over')

    def answer(self, label, a, kind, applied, view, bay):
        bay_, salvage, first, one, bank, record, condition, item, all_, value, bank9, build_v = a
        mask = 0xffffffff
        ours = (bay, view['salvage'], view['first'], view['one'], view['bank'], view['record'], view['condition'],
                view['item'], view['all'], view['value'], view['bank9'], view['build'])
        theirs = (bay_, salvage, first, one, bank, record, condition, item, all_, value, bank9,
                  ((build_v * 1000 + 2 ** 31) & mask) - 2 ** 31)
        self.check(ours == theirs, f'{label}: answers {theirs}, ours {ours}')
        available = (salvage - bank) & mask
        enabled = {REPAIR: available >= item & mask, REPAIR_ALL: available >= all_ & mask,
                   SCRAP: bool(record and not one and bank9), BUILD: bool(not record and available > (build_v * 1000) & mask)}[kind]
        self.check(bool(applied) == enabled, f'{label}: operation {kind} applied {applied}, enable rule {enabled}')
        name = {REPAIR: 'repair', REPAIR_ALL: 'repair_all', SCRAP: 'scrap', BUILD: 'build'}[kind]
        self.count(f'{name}_{"applied" if applied else "disabled"}')

    def state(self, label, theirs, ours, repair_opened, snapped):
        if ours is None:
            return
        bay, tab, row, family, index, _type, salvage, conditions = theirs
        self.check(bay == ours['bay'], f'{label}: bay selection {bay}, ours {ours["bay"]}')
        if repair_opened:
            mine = (ours['tab'], ours['row'], ours['family'], ours['index'], ours['salvage'])
            self.check((tab, row, family, index, salvage) == mine,
                       f'{label}: REPAIR selection/snapshot {(tab, row, family, index, salvage)}, ours {mine}')
        if snapped:
            self.check(conditions == ours['conditions'].hex(), f'{label}: snapshot condition words differ')
            self.count('snapshot_words')

    def tally(self, case, result):
        start, end = Layout(case['state']), Layout(result['sav'])
        if start.word(start.team + 2) != end.word(end.team + 2):
            self.count('scrap_deactivation')           # only 0x41094b changes the team count here
        if start.word(start.player) != -1 and end.word(end.player) == -1:
            self.count('scrap_player_bay')
        for step in result['steps']:
            (kind, a, b), applied = step['op'], step['applied']
            before = step['before']
            if kind == REPAIR and applied:
                self.count(f'repair_family_{step["session"]["family"]}')
                if before['condition'] < 0 or before['condition'] > 100:
                    self.count('repair_outside_0_100')
            if kind == PICK:
                self.count('pick_applied' if applied else 'pick_ignored')
            if kind == CANCEL:
                self.count('cancel')
            if kind == BAY and applied:
                self.count(f'bay_{step["session"]["screen"]}')
            if kind == SCRAP and applied:
                self.count(f'scrap_screen_{step["session"]["screen"]}')
                fixed = start.records.get(step['bay_before'])
                if fixed is not None and start.word(fixed + 70) != 100:
                    self.count('scrap_unfinished')
                if before['value'] and before['salvage'] != step['view']['salvage']:
                    self.count('scrap_value')
            if kind in (OPEN, CLOSE) and applied:
                self.count('leave_or_open')
            if kind == BUILD and applied:
                self.count(f'build_type_{step["session"]["type"]}')

    # ---- corpus and sequences -------------------------------------------------------------
    def prepared(self, base, group, position, rng):
        request = self.prep.request(group=group, position=position, mode=1, language='GER', rng=rng,
                                    selectors=bytes(60), tables=self.prep.default_tables(self.cd), save=base,
                                    prepared_save=True)
        out, = self.prep.prepare_batch(self.core, self.cd, [request])
        if 'error' in out:
            raise ValueError(f'preparation ({group},{position}): {out["error"]}')
        return out['prepared_save']

    def corpus(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        rng = self.prep.initial_rng(self.cd, 71)
        career, rng_after, _ = self.backend.new_game_request(self.core, self.cd, 'GER', 'TESTPILOT', 1, 1, 0, rng)
        loaded = self.memory([game_r, game_t])
        out = {'GAME_R': loaded[0], 'GAME_T': loaded[1], 'new career': career}
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        if (golden / 'results.dat').is_file():
            chain = load(Path(self.backend.__file__).parent / 'es2chain.py', 'editors_chain')
            step = chain.Engine(self.core, self.cd).advance(
                game_r, (golden / 'mission.var').read_bytes(), (golden / 'results.dat').read_bytes(), rng, 'GER', 0, 0)
            out['after C1_01'] = step['sav']
        missions = [(g, p) for g, p, _ in self.prep.campaign(self.core, self.cd) if g >= 1]
        for i, (g, p) in enumerate((missions[0], missions[17], missions[-1])):
            out[f'prepared {g}:{p}'] = self.prepared(career if i != 1 else loaded[0], g, p, rng_after)
        return out

    def edit(self, rng, state):
        out = bytearray(state)
        layout = Layout(state)
        for bay, fixed in layout.records.items():
            if rng.random() < 0.7:
                for word in range(33):
                    if rng.random() < 0.35:
                        put16(out, fixed + 4 + 2 * word, rng.choice(VALUES))
            if rng.random() < 0.15:
                put16(out, fixed + 70, rng.choice((0, 1, 50, 99)))
        if rng.random() < 0.6:
            struct.pack_into('<I', out, layout.tally, rng.choice((0, 300, 1500, 9000, 40000, 99999, 100000, 100001,
                                                                   250000, 900000, 2 ** 31 + 5, 2 ** 32 - 10)))
        if rng.random() < 0.4:
            for i in range(9):
                put16(out, layout.nine + 2 * i, rng.choice((0, 1, 1)))
        if rng.random() < 0.3:
            for i in range(5):
                put16(out, layout.five + 4 + 4 * i, rng.choice((0, 0, 1, 5, 13, 22, 25, 32)))
        if rng.random() < 0.3:
            selected = layout.selected()
            orders = [-1, 0, 1, 2, 3]
            for suffix in selected:
                if rng.random() < 0.5:
                    put16(out, suffix + 5, rng.choice(orders))
                    out[suffix + 2] = rng.choice((0, 1))
            if rng.random() < 0.3:
                put16(out, layout.player + 5, rng.choice(orders))
        return bytes(out)

    def sequence(self, rng, state):
        layout = Layout(state)
        bays = sorted(layout.records)
        empty = [k for k in range(8) if k not in layout.records]
        ops = []
        screen = rng.choice((3, 3, 4))
        ops.append((OPEN, screen, rng.choice((0, 1))))
        for _ in range(rng.randint(2, 12)):
            r = rng.random()
            if screen == 3:
                if r < 0.25:
                    tab = rng.choice((0, 0, 1))
                    ops.append((PICK, tab, rng.randrange(16 if tab == 0 else 9)))
                elif r < 0.45:
                    ops.append((REPAIR, 0, 0))
                elif r < 0.55:
                    ops.append((REPAIR_ALL, 0, 0))
                elif r < 0.63:
                    ops.append((CANCEL, 0, 0))
                elif r < 0.75:
                    ops.append((BAY, rng.choice(bays + list(range(8))), 0))
                elif r < 0.82:
                    ops.append((SCRAP, 0, 0))
                elif r < 0.9:
                    screen = 4
                    ops.append((OPEN, 4, rng.choice((0, 1, 7))))
                else:
                    screen = rng.choice((-1, 0, 2))
                    ops.append((CLOSE, screen, rng.choice((0, 1))))
            elif screen == 4:
                if r < 0.25:
                    ops.append((BUILD_TYPE, rng.randrange(9), 0))
                elif r < 0.45:
                    ops.append((BAY, rng.choice(empty + list(range(8))) if empty else rng.randrange(8), 0))
                elif r < 0.65:
                    ops.append((BUILD, 0, 0))
                elif r < 0.78:
                    ops.append((SCRAP, 0, 0))
                elif r < 0.9:
                    screen = 3
                    ops.append((OPEN, 3, rng.choice((0, 1))))
                else:
                    screen = rng.choice((-1, 0, 2))
                    ops.append((CLOSE, screen, rng.choice((0, 1))))
            else:
                screen = rng.choice((3, 4))
                ops.append((OPEN, screen, rng.choice((0, 1))))
        return ops

    def targeted(self, corpus):
        """Fixed cases: every External/Internal row on a damaged GAME_R Herc,
        REPAIR ALL and CANCEL, Build into each empty bay, scrap of every
        Herc from BUILD and from REPAIR."""
        cases = []
        state = corpus['GAME_R']
        layout = Layout(state)
        damaged = bytearray(state)
        for bay, fixed in layout.records.items():
            for word in range(33):
                put16(damaged, fixed + 4 + 2 * word, VALUES[(word * 7 + bay) % len(VALUES)])
        struct.pack_into('<I', damaged, layout.tally, 2000000)
        damaged = bytes(damaged)
        for bay in layout.records:
            ops = [(OPEN, 3, 0), (BAY, bay, 0)]
            for tab, rows in ((0, 16), (1, 9)):
                for row in range(rows):
                    ops += [(PICK, tab, row), (REPAIR, 0, 0)]
            cases.append(dict(label=f'targeted rows bay {bay}', state=damaged, bay=bay, ops=ops))
            cases.append(dict(label=f'targeted all bay {bay}', state=damaged, bay=bay,
                              ops=[(OPEN, 3, 0), (REPAIR, 0, 0), (REPAIR_ALL, 0, 0), (CANCEL, 0, 0), (REPAIR_ALL, 0, 0),
                                   (CLOSE, -1, 1)]))
            cases.append(dict(label=f'targeted scrap build bay {bay}', state=damaged, bay=bay,
                              ops=[(OPEN, 4, 0), (SCRAP, 0, 0), (BUILD_TYPE, 3, 0), (BUILD, 0, 0), (CLOSE, 0, 0)]))
            cases.append(dict(label=f'targeted scrap repair bay {bay}', state=state, bay=bay,
                              ops=[(OPEN, 3, 0), (SCRAP, 0, 0), (REPAIR_ALL, 0, 0), (CANCEL, 0, 0)]))
        for label, state in corpus.items():
            layout = Layout(state)
            for k in range(8):
                if k not in layout.records:
                    cases.append(dict(label=f'targeted build {label} bay {k}', state=state, bay=k,
                                      ops=[(OPEN, 4, 0), (BUILD_TYPE, k % 9, 0), (BUILD, 0, 0), (OPEN, 3, 1)]))
                    break
        return cases

    def main(self):
        started = time.time()
        corpus = self.corpus()
        rng = random.Random(1414)
        cases = self.targeted(corpus)
        for label, state in corpus.items():
            for trial in range(4):
                bay = rng.choice(sorted(Layout(state).records) + [rng.randrange(8)])
                cases.append(dict(label=f'{label}/{trial}', state=state, bay=bay, ops=self.sequence(rng, state)))
        names = list(corpus)
        for trial in range(SWEEP):
            base = corpus[rng.choice(names)]
            state = self.edit(rng, base)
            bay = rng.choice(sorted(Layout(state).records) + [rng.randrange(8)])
            cases.append(dict(label=f'sweep {trial}', state=state, bay=bay, ops=self.sequence(rng, state)))
        self.compare(cases)
        self.report = dict(cases=len(cases), worker_runs=self.runs, coverage=self.coverage,
                           seconds=round(time.time() - started, 1))
        for key in ('repair_applied', 'repair_disabled', 'repair_all_applied', 'repair_all_disabled', 'scrap_applied',
                    'scrap_disabled', 'build_applied', 'build_disabled', 'cancel', 'pick_applied', 'pick_ignored',
                    'repair_family_0', 'repair_family_1', 'repair_family_2', 'bay_3', 'bay_4', 'scrap_screen_3',
                    'scrap_screen_4', 'repair_outside_0_100', 'scrap_deactivation', 'scrap_player_bay',
                    'scrap_unfinished', 'snapshot_words'):
            self.check(self.coverage.get(key, 0) > 0, f'coverage: no case of {key}')


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
        Path(out, 'editors-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
