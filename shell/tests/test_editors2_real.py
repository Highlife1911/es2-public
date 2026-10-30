# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Service-bay editors CREW, ARMORY and the weapons scrap against
VSHELL's own handlers and functions in the mapped original.

Skipped without ES2_DATA_ROOT. Builds on //shell:editors_real_test:
its corpus, SAV layout walker, worker driver and REPAIR/BUILD checks are
reused (test_editors_real.py, loaded as a module). Ours: es2/shell/editors.h
through editors_tool. The original: the memory worker's slot mode with the
REPAIR/BUILD operations and the CREW/ARMORY ones: `crew-show` (0x442198, 0x442215(0..3, 0),
0x43dcdd per bay), `crew-order:O` (0x442215), `crew-pilot:G` (0x442548),
`crew-bay:K` (0x43dcdd with the selector flag 0x47665a), `crew-clear`
(0x4429e2), `show-select` (the REPAIR/WEAPONS show's 0x43dcdd(0x41124e) in
CREW's case), `armory-show` (0x449b87), `armory-leave` (0x449c46),
`armory-row:R:B` (0x449d2f / 0x44a06e), `armory-clear` (0x44a584),
`armory-scrap` (0x44a508, the warning 0x448227 recorded), `armory-accept:T`
(0x448447), `building:V`, `s17-reset` and `s17-state` (bay selection, CREW
order and pilots, ARMORY row, bank free count, the difficulty word 0x4826ea).

Per sequence: the SAV after it equals the original writers' byte for byte;
after every CREW and ARMORY operation the bay selection, the order, the three
order pilots, the ARMORY row and the bank's free count equal the original's
globals, and the difficulty word is 0; the category each weapons scrap warns
about equals ours; the REPAIR/BUILD checks (answers before REPAIR/BUILD/scrap, REPAIR
selection and snapshot) hold as in editors_real_test.

Arguments: editors_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2loadout.py test_editors_real.py
"""
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import sys
import time


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


E1 = load(sys.argv[8], 'editors1') if len(sys.argv) > 8 else None
OPEN, CLOSE, BAY, PICK, REPAIR, REPAIR_ALL, CANCEL, BUILD_TYPE, BUILD, SCRAP = range(1, 11)
A_ROW, A_CLEAR, A_SCRAP, C_ORDER, C_PILOT, C_CLEAR = range(11, 17)
WARNING = 0x448227          # recorded weapons scrap warning (its category)
SWEEP = 150


def cut(sav, category, n):
    """The SAV with the item list of `category` cut to its first n items
    (inventory framing: flag byte, count word, ten bytes per item)."""
    at = 0
    for i in range(33):
        count, = struct.unpack_from('<h', sav, at + 1)
        if i == category:
            keep = min(n, count)
            return sav[:at + 1] + struct.pack('<h', keep) + sav[at + 3:at + 3 + 10 * keep] + sav[at + 3 + 10 * count:]
        at += 3 + 10 * count
    raise ValueError(category)


class Test(E1.Test if E1 else object):
    # ---- ours ------------------------------------------------------------------
    def ours(self, cases):
        text = self.tables.hex() + '\n' + self.editor_tables.hex() + '\n'
        for case in cases:
            text += f'CASE {case["bay"]} {case["building"]}\nSAVE {case["state"].hex()}\n'
            text += ''.join(f'OP {k} {a} {b}\n' for k, a, b in case['ops']) + 'END\n'
        run = E1.subprocess.run([str(self.tool), str(self.cd)], input=text, capture_output=True, text=True, check=True)
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
                    type=int(p[7]), salvage=int(p[8]), conditions=bytes.fromhex(p[9]), order=int(p[10]),
                    crew=[int(p[11]), int(p[12]), int(p[13])], armory_row=int(p[14]), building=int(p[15]))

    @staticmethod
    def view(line):
        keys = ('salvage', 'bank', 'first', 'one', 'record', 'condition', 'item', 'all', 'value', 'bank9', 'build',
                'repair', 'repair_all', 'scrap', 'build_enabled', 'armory_rows', 'clear', 'category', 'items',
                'in_bank', 'free', 'scrap_value')
        return dict(zip(keys, map(int, line.split()[1:])))

    # ---- the original --------------------------------------------------------------
    def translate(self, case, result):
        building = case['building']
        ops = [f'select:{case["bay"] % 65536}', 'screen:65535', 'herc-type:65535', 'repair-pick:0:0', 's17-reset',
               f'building:{building}']
        expect = dict(answers=[], states=[], selects=[], repair_opened=False, s17=[], warnings=[])
        screen, snapped = -1, False

        def leave():
            if screen in (2, 3, 4):
                ops.extend([f'rebalance:{building}', f'building:{building}'])
            elif screen == 5:
                ops.append('armory-leave')

        def state(step):
            ops.append('s17-state')
            expect['s17'].append((step['session'], step['view']))

        for step in result['steps']:
            (kind, a, b), applied, before, after = step['op'], step['applied'], step['before'], step['session']
            if kind == OPEN and applied:
                leave()
                if a in (2, 3):
                    ops.append('show-select' if screen == 6 else f'select:{after["bay"] % 65536}')
                    if a == 3:
                        ops += ['repair-snapshot', 'repair-pick:0:0']
                        expect['repair_opened'] = True
                        snapped = snapped or after['bay'] != -1
                    ops.append(f'screen:{a}')
                elif a == 4:
                    ops += ['herc-type:0', 'screen:4']
                elif a == 5:
                    ops += ['armory-show', 'screen:5']
                else:
                    ops += ['screen:6', 'crew-show']
                state(step)
            elif kind == CLOSE:
                leave()
                ops.append(f'screen:{a % 65536}')
            elif kind == BAY:
                if screen == 6:
                    ops.append(f'crew-bay:{a}')
                    state(step)
                elif applied:
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
            elif kind == A_ROW:
                if before['armory_rows'] >> a & 1:
                    ops.append(f'armory-row:{a}:{b}')
                state(step)
            elif kind == A_CLEAR:
                if building:
                    ops.append('armory-clear')
                state(step)
            elif kind == A_SCRAP:
                if before['category'] != -1:
                    ops += ['armory-scrap', f'armory-accept:{before["category"]}']
                    expect['warnings'].append(before['category'])
                state(step)
            elif kind in (C_ORDER, C_PILOT, C_CLEAR):
                ops.append({C_ORDER: f'crew-order:{a}', C_PILOT: f'crew-pilot:{a}', C_CLEAR: 'crew-clear'}[kind])
                state(step)
            screen = after['screen']
        ops += ['editor-state', 'emit']
        expect['states'].append((result['steps'][-1]['session'] if result['steps'] else None, snapped))
        return ops, expect

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
        runs, current = [], []
        for item in planned:
            trial = current + [item]
            ops = sum(len(x[3]) + 1 for x in trial)
            answers = sum(len(x[4]['answers']) for x in trial)
            states = sum(len(x[4]['states']) for x in trial)
            s17 = sum(len(x[4]['s17']) for x in trial)
            if current and (len(trial) > E1.CASES_PER_RUN or ops > 500 or answers > 250 or states > 140 or s17 > 250 or
                            answers * 80 + states * 230 + s17 * 60 > 55000):
                runs.append(current)
                trial = [item]
            current = trial
        if current:
            runs.append(current)
        for chunk_items in runs:
            ops = []
            for i, x in enumerate(chunk_items):
                ops += [f'restore:{i}'] + x[3]
            out = self.worker_run(ops, [x[2] for x in chunk_items])
            meta = out['metadata']
            answers, states, s17 = iter(meta['editor_answers']), iter(meta['editor']), iter(meta['s17'])
            selects = [c[1] & 0xffff for c in meta['ui_calls'] if c[0] == E1.SELECT]
            selects = iter([x - 65536 if x >= 32768 else x for x in selects])
            warnings = iter([c[1] & 0xffff for c in meta['ui_calls'] if c[0] == WARNING])
            emitted, at = out['disk'].get('editors.emit', b''), 0
            for case, result, _, _, expect in chunk_items:
                label = case['label']
                size, = struct.unpack_from('<I', emitted, at)
                theirs = emitted[at + 4:at + 4 + size]
                at += 4 + size
                self.check(theirs == result['sav'], f'{label}: SAV differs from the original ({case["ops"]})')
                for kind, applied, view, bay in expect['answers']:
                    self.answer(label, next(answers), kind, applied, view, bay)
                for st, snapped in expect['states']:
                    self.state(label, next(states), st, expect['repair_opened'], snapped)
                for bay in expect['selects']:
                    self.check(next(selects, None) == bay, f'{label}: scrap selected another bay than 0x43dcdd')
                for session, view in expect['s17']:
                    self.s17(label, next(s17), session, view)
                for category in expect['warnings']:
                    self.check(next(warnings, None) == category, f'{label}: the weapons scrap warned about another category')
                self.tally(case, result)
                self.tally2(case, result)
            self.check(next(answers, None) is None and next(states, None) is None and next(selects, None) is None and
                       next(s17, None) is None and next(warnings, None) is None, 'worker records left over')

    def s17(self, label, theirs, session, view):
        bay, order, p1, p2, p3, row, free = theirs[:7]
        mine = (session['bay'], session['order'], *session['crew'], session['armory_row'], view['free'])
        self.check((bay, order, p1, p2, p3, row, free) == mine,
                   f'{label}: CREW/ARMORY globals {(bay, order, p1, p2, p3, row, free)}, ours {mine}')
        self.check(theirs[12] == 0, f'{label}: difficulty word {theirs[12]}')
        self.count('s17_states')

    def tally2(self, case, result):
        for step in result['steps']:
            (kind, a, b), applied, before, after, view = step['op'], step['applied'], step['before'], step['session'], step['view']
            name = {A_ROW: 'armory_row', A_CLEAR: 'armory_clear', A_SCRAP: 'armory_scrap', C_ORDER: 'crew_order',
                    C_PILOT: 'crew_pilot', C_CLEAR: 'crew_clear'}.get(kind)
            if name:
                self.count(f'{name}_{"applied" if applied else "ignored"}')
            if kind == A_ROW and view['free'] < before['free']:
                self.count('armory_add')
            if kind == A_ROW and b != 1 and view['free'] > before['free']:
                self.count('armory_remove')
            if kind == A_SCRAP and applied:
                self.count('weapons_scrap_value' if view['salvage'] != before['salvage'] else 'weapons_scrap_empty')
            if kind == BAY and after['screen'] == 6:
                self.count('crew_bay_applied' if applied else 'crew_bay_ignored')
            if kind == OPEN and applied and a in (2, 3) and step['bay_before'] != after['bay']:
                self.count(f'show_select_{a}')
            if kind == OPEN and applied and a == 6:
                self.count('crew_show')

    # ---- corpus and sequences -----------------------------------------------------------
    def edit2(self, rng, state):
        out = bytearray(self.edit(rng, state))
        layout = E1.Layout(bytes(out))
        records = sorted(layout.records)
        if rng.random() < 0.5:
            for suffix in layout.selected() + [layout.player]:
                if rng.random() < 0.5:
                    E1.put16(out, suffix, rng.choice(records + [-1]))
                    out[suffix + 2] = rng.choice((0, 1))
        if records and rng.random() < 0.3:
            E1.put16(out, layout.records[rng.choice(records)], 8)
        if rng.random() < 0.3:
            E1.put16(out, layout.team, rng.choice((1, 2, 3, 4)))
        if rng.random() < 0.4:
            E1.put16(out, layout.five, rng.choice((0, 1, 2, 3, 4, 5)))
            for i in range(5):
                E1.put16(out, layout.five + 4 + 4 * i, rng.choice((0, 0, 0, 1, 2, 7, 13, 18, 22, 25, 32)))
        if rng.random() < 0.3:
            struct.pack_into('<I', out, layout.tally, rng.choice((0, 1000, 4000, 20000, 60000, 2 ** 32 - 10)))
        return bytes(out)

    def sequence2(self, rng, state):
        layout = E1.Layout(state)
        bays = sorted(layout.records)
        ops, last = [], -1
        screen = rng.choice((5, 6, 6))
        ops.append((OPEN, screen, 0))
        last = 0 if screen == 5 else last

        def jump(choices):
            nonlocal screen, last
            screen = rng.choice(choices)
            ops.append((OPEN, screen, 0))
            if screen == 5:
                last = 0

        for _ in range(rng.randint(3, 14)):
            r = rng.random()
            if screen == 6:
                if r < 0.22:
                    ops.append((C_ORDER, rng.randrange(4), 0))
                elif r < 0.42:
                    ops.append((C_PILOT, rng.randrange(3), 0))
                elif r < 0.64:
                    ops.append((BAY, rng.choice(bays + list(range(8))), 0))
                elif r < 0.72:
                    ops.append((C_CLEAR, 0, 0))
                elif r < 0.9:
                    jump((2, 3, 3, 4, 5))
                else:
                    screen = rng.choice((-1, 0, 7))
                    ops.append((CLOSE, screen, 0))
            elif screen == 5:
                if r < 0.55:
                    row = rng.choice((last, last, last, rng.randrange(26)))
                    ops.append((A_ROW, row, rng.choice((1, 1, 2))))
                    last = row
                elif r < 0.65:
                    ops.append((A_CLEAR, 0, 0))
                elif r < 0.75:
                    ops.append((A_SCRAP, 0, 0))
                elif r < 0.9:
                    jump((2, 3, 4, 6))
                else:
                    screen = rng.choice((-1, 0, 7))
                    ops.append((CLOSE, screen, 0))
            elif screen in (2, 3, 4):
                if r < 0.3:
                    ops.append((BAY, rng.choice(bays + list(range(8))), 0))
                elif r < 0.4 and screen == 3:
                    ops.append((REPAIR_ALL, 0, 0))
                elif r < 0.45 and screen != 2:
                    ops.append((SCRAP, 0, 0))
                else:
                    jump(tuple(x for x in (2, 3, 4, 5, 6) if x != screen))
            else:
                jump((2, 3, 4, 5, 6))
        return ops

    def targeted2(self, corpus):
        cases = []
        state = corpus['GAME_R']
        layout = E1.Layout(state)
        bays = sorted(layout.records)
        crew = [(OPEN, 6, 0)]
        for o in (1, 2, 3):
            crew.append((C_ORDER, o, 0))
            for g in range(3):
                crew += [(C_PILOT, g, 0)] + [(BAY, k, 0) for k in bays[:3]]
            crew.append((C_CLEAR, 0, 0))
        crew += [(C_ORDER, 0, 0)] + [(BAY, k, 0) for k in range(8)] + [(OPEN, 3, 0)]
        cases.append(dict(label='targeted crew', state=state, bay=bays[0] if bays else -1, building=0, ops=crew))
        # the REPAIR and WEAPONS shows after CREW with a vehicle-8 Herc first
        if bays:
            eight = bytearray(state)
            E1.put16(eight, layout.records[bays[0]], 8)
            for target in (2, 3):
                cases.append(dict(label=f'targeted vehicle 8 show {target}', state=bytes(eight), bay=-1, building=0,
                                  ops=[(OPEN, 6, 0), (C_ORDER, 1, 0), (C_CLEAR, 0, 0), (BAY, bays[0], 0), (OPEN, target, 0)]))
        for building in (0, 1):
            ops = [(OPEN, 5, 0)]
            for row in range(26):
                ops += [(A_ROW, row, 1), (A_ROW, row, 1), (A_ROW, row, 1), (A_ROW, row, 2)]
                if row % 5 == 0:
                    ops.append((A_CLEAR, 0, 0))
            ops.append((CLOSE, -1, 0))
            for label in ('GAME_R', 'new career'):
                cases.append(dict(label=f'targeted armory {label} building {building}', state=corpus[label], bay=0,
                                  building=building, ops=ops))
        for label, st in corpus.items():
            ops = [(OPEN, 5, 0)]
            for row in range(26):
                ops += [(A_ROW, row, 1), (A_SCRAP, 0, 0)]
            cases.append(dict(label=f'targeted weapons scrap {label}', state=st, bay=0, building=1, ops=ops[:120]))
        # every category cut down to one item: 0x412c8e's minimum 1
        single = state
        for category in range(33):
            single = cut(single, category, 1)
        ops = [(OPEN, 5, 0)]
        for row in range(26):
            ops += [(A_ROW, row, 1), (A_SCRAP, 0, 0)]
        cases.append(dict(label='targeted weapons scrap single items', state=single, bay=0, building=0, ops=ops))
        # the add boundary: an empty bank and salvage = the row's category
        # cost (no add, unsigned <) or the cost + 1 (add); the costs come
        # from a probe of ours (the bank cost after one add per row)
        def empty_bank(tally):
            st = bytearray(state)
            E1.put16(st, layout.five, 5)
            for i in range(5):
                E1.put16(st, layout.five + 4 + 4 * i, 0)
            struct.pack_into('<I', st, layout.tally, tally)
            return bytes(st)
        probe = [(OPEN, 5, 1)]
        for row in range(26):
            probe += [(A_ROW, row, 1), (A_ROW, row, 1), (A_ROW, row, 2)]
        result, = self.ours([dict(label='probe', state=empty_bank(10 ** 7), bay=0, building=1, ops=probe)])
        costs = {}
        for step in result['steps']:
            (kind, row, b), view = step['op'], step['view']
            if kind == A_ROW and b == 1 and view['free'] < step['before']['free']:
                costs.setdefault(view['bank'] - step['before']['bank'], row)
        self.check(len(costs) > 3, 'probe found too few category costs')
        for cost, row in sorted(costs.items()):
            for tally in (cost, cost + 1):
                cases.append(dict(label=f'targeted armory salvage {tally}', state=empty_bank(tally), bay=0, building=1,
                                  ops=[(OPEN, 5, 0), (A_ROW, row, 1), (A_ROW, row, 1)]))
        # orders at and above the team capacity
        for capacity in (1, 2, 3):
            st = bytearray(state)
            E1.put16(st, layout.team, capacity)
            ops = [(OPEN, 6, 0)]
            for o in (3, 2, 1):
                ops += [(C_ORDER, o, 0), (C_PILOT, o - 1, 0)] + [(BAY, k, 0) for k in bays[:4]]
            cases.append(dict(label=f'targeted crew capacity {capacity}', state=bytes(st), bay=-1, building=0, ops=ops))
        return cases

    def main(self):
        started = time.time()
        corpus = self.corpus()
        rng = random.Random(1717)
        cases = self.targeted2(corpus)
        for label, state in corpus.items():
            for trial in range(4):
                bay = rng.choice(sorted(E1.Layout(state).records) + [rng.randrange(8)])
                cases.append(dict(label=f'{label}/{trial}', state=state, bay=bay, building=rng.choice((0, 1, 1)),
                                  ops=self.sequence2(rng, state)))
        names = list(corpus)
        for trial in range(SWEEP):
            state = self.edit2(rng, corpus[rng.choice(names)])
            bay = rng.choice(sorted(E1.Layout(state).records) + [-1, rng.randrange(8)])
            cases.append(dict(label=f'sweep {trial}', state=state, bay=bay, building=rng.choice((0, 1, 1)),
                              ops=self.sequence2(rng, state)))
        for case in cases:   # OPEN and CLOSE leave with the case's weapons-building byte
            case['ops'] = [(k, a, case['building'] if k in (OPEN, CLOSE) else v) for k, a, v in case['ops']]
        self.compare(cases)
        self.report = dict(cases=len(cases), worker_runs=self.runs, coverage=self.coverage,
                           seconds=round(time.time() - started, 1))
        for key in ('crew_show', 'crew_order_applied', 'crew_pilot_applied', 'crew_pilot_ignored', 'crew_clear_applied',
                    'crew_clear_ignored', 'crew_bay_applied', 'crew_bay_ignored', 'show_select_2', 'show_select_3',
                    'armory_row_applied', 'armory_row_ignored', 'armory_add', 'armory_remove', 'armory_clear_applied',
                    'armory_clear_ignored', 'armory_scrap_applied', 'weapons_scrap_value', 'weapons_scrap_empty',
                    's17_states'):
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
        Path(out, 'editors2-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
