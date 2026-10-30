# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The squad panel's condition 0x4121f8 against VSHELL's own function
in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2_shell_record_condition through the
test driver condition_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro) with `restore:N` (a --state SAV through
the original readers) and `squad-condition` (per bay the record flag and
0x4121f8(record + 8, record word +0x4c), as the squad panel's 0x43e0d7 calls
it). Per state the eight flags and values must be equal.

Corpus: editors_real_test's (GAME_R, GAME_T, a new career, the SAV after the
C1_01 results path, three prepared states; test_editors_real.py loaded as a
module), then a seeded sweep: condition words from editors_real_test's
values and from the whole signed 16-bit range (sums that wrap), and
capacities raised up to 10 (never below a mounted slot).

Arguments: condition_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2loadout.py test_editors_real.py
"""
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import time


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


E1 = load(sys.argv[8], 'editors1') if len(sys.argv) > 8 else None
SWEEP = 240
PER_RUN = 120


class Test(E1.Test if E1 else object):
    def ours(self, states):
        run = subprocess.run([str(self.tool)], input=''.join(s.hex() + '\n' for s in states), capture_output=True,
                             text=True, check=True)
        out = []
        for line in run.stdout.splitlines():
            parts = line.split()
            if parts[0] == 'REJECT':
                out.append(None)
            else:
                values = list(map(int, parts[1:]))
                out.append((values[:8], values[8:]))
        if len(out) != len(states):
            raise ValueError('condition_tool answered %d of %d states' % (len(out), len(states)))
        return out

    def theirs(self, files):
        ops = []
        for i in range(len(files)):
            ops += [f'restore:{i}', 'squad-condition']
        out = self.worker_run(ops, files)
        data = out['disk'].get('squad.condition', b'')
        if len(data) != 24 * len(files):
            raise ValueError('worker wrote %d bytes for %d states' % (len(data), len(files)))
        result = []
        for i in range(len(files)):
            rec = data[24 * i:24 * i + 24]
            result.append((list(rec[:8]), list(struct.unpack_from('<8h', rec, 8))))
        return result

    def sweep_state(self, rng, state):
        out = bytearray(state)
        layout = E1.Layout(state)
        for bay, fixed in layout.records.items():
            wide = rng.random() < 0.5
            for word in range(33):
                if rng.random() < 0.6:
                    value = rng.randint(-32768, 32767) if wide else rng.choice(E1.VALUES)
                    E1.put16(out, fixed + 4 + 2 * word, value)
            if rng.random() < 0.4:
                count = E1.Layout(state).word(fixed + 76)          # loadout item count
                slots = [E1.Layout(state).word(fixed + 78 + 12 * i) for i in range(count)]
                low = max([s + 1 for s in slots] + [E1.Layout(state).word(fixed + 74)])
                E1.put16(out, fixed + 74, rng.randint(min(low, 10), 10))
        return bytes(out)

    def main(self):
        started = time.time()
        corpus = self.corpus()
        rng = random.Random(4747)
        states = list(corpus.values())
        names = list(corpus)
        for _ in range(SWEEP):
            states.append(self.sweep_state(rng, corpus[rng.choice(names)]))
        files = self.memory(states)
        ours = self.ours(files)
        compared = records = wrapped = 0
        for at in range(0, len(files), PER_RUN):
            chunk = list(range(at, min(at + PER_RUN, len(files))))
            usable = [i for i in chunk if ours[i] is not None]
            self.count('rejected_states', len(chunk) - len(usable))
            theirs = self.theirs([files[i] for i in usable])
            for i, t in zip(usable, theirs):
                label = names[i] if i < len(names) else f'sweep {i - len(names)}'
                self.check(t == ours[i], f'{label}: flags/conditions {t}, ours {ours[i]}')
                compared += 1
                records += sum(ours[i][0])
                layout = E1.Layout(files[i])
                for fixed in layout.records.values():
                    total = sum(layout.word(fixed + 4 + 2 * w) for w in range(22)) + \
                        sum(layout.word(fixed + 50 + 2 * s) for s in range(layout.word(fixed + 74)))
                    if not -32768 <= total <= 32767:
                        wrapped += 1
        self.check(compared > SWEEP // 2, 'too few states compared')
        self.check(wrapped > 0, 'coverage: no record whose sum wraps')
        self.report = dict(states=len(files), compared=compared, records=records, wrapped_sums=wrapped,
                           worker_runs=self.runs, coverage=self.coverage, seconds=round(time.time() - started, 1))


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
        Path(out, 'condition-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
