# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The ONLINE MANUAL entry against VSHELL's help routine 0x431e79 in
the mapped original.

Skipped without ES2_DATA_ROOT. Ours: the flow's ONLINE_MANUAL event and the
manual's path through the test driver help_tool (nothing is opened). The
original: the memory worker's slot mode (tools/reference-game/shell-invitro,
slots_oracle.py) with the operation `help[:ROOT]`, which calls 0x431e79
itself: leave full screen 0x4077df, preference byte 6 := 0 (0x40ddfe), commit
0x40dd6e, the writer 0x40dd0a, sound focus 0x42f8ff (trapped), the language
reader 0x408d69 over a memory-disk data\\language.cfg, the path join 0x40d9e1
and the WinHelpA wrapper 0x407953 (trapped: its path argument is recorded).

Seeded sequences of Preferences controls, Accept, Cancel, the display state
and one or two help calls on prefs files that are missing, short, 54 bytes
or longer, for each language byte E, F, G and several CD roots. Compared per
sequence: both prefs blocks, the prefs.cfg the original wrote and the paths
handed to WinHelpA. Language bytes other than E, F, G end VSHELL in a
severity-4 assertion (0x431efc): not run in the original, only the listing
(sub al, 0x45; dec; dec; else assertion) says so, ours is unit-tested.

Arguments: help_tool es2shellprep slots_oracle.py
Sharding (shard.py): the sequences are numbered; sequence n runs in the
shard n % shards; ours is computed in every shard.
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

B = '\\'
CONTROLS = {1: 'music', 2: 'sfx', 3: 'high', 4: 'low', 5: 'window', 6: 'full', 7: 'repair0', 8: 'repair1',
            9: 'repair2', 10: 'build0', 11: 'build1'}
ROOTS = ['D:\\', 'C:\\cd\\', 'X:\\games\\es2\\cd\\', '\\\\server\\share\\es2\\', 'E:\\']
RELATIVE = {'E': 'english', 'F': 'french', 'G': 'german'}


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Test:
    def __init__(self, argv):
        self.tool, self.worker, oracle_py = [Path(a).resolve() for a in argv]
        self.oracle = load(oracle_py, 'help_oracle')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2help-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        self.failures, self.report, self.runs = [], {}, 0
        self.numbered = 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def theirs(self, ops, file, letter):
        self.runs += 1
        disk = {'sav' + B + 'gamefile.str': self.gamefile, 'data' + B + 'language.cfg': letter.encode()}
        if file is not None:
            disk['data' + B + 'prefs.cfg'] = file
        return self.oracle.run(self.worker, self.cd, disk, ops, self.tmp / f'run-{self.runs}', timeout=300)

    def ours(self, lines):
        run = subprocess.run([str(self.tool)], input=''.join(l + '\n' for l in lines),
                             capture_output=True, text=True, check=True)
        out = run.stdout.splitlines()
        if any(l.startswith('ERROR') for l in out):
            raise RuntimeError(run.stdout)
        return out

    def sequence(self, rng, letter, file):
        root = rng.choice(ROOTS)
        ours = [file.hex() if file is not None else '-']
        theirs = ['prefs-load', 'prefs:show']
        helps = 0
        for _ in range(rng.randint(0, 8)):
            r = rng.random()
            if r < 0.5:
                control = rng.choice(list(CONTROLS))
                ours.append(f'control {control}')
                theirs += ['prefs:full', 'prefs:alert'] if control == 6 else ['prefs:' + CONTROLS[control]]
            elif r < 0.65:
                ours.append('accept')
                theirs.append('prefs:accept')
            elif r < 0.8:
                ours.append('cancel')
                theirs.append('prefs:cancel')
            else:
                theirs.append(f'display:{rng.choice((0, 1))}')
        for _ in range(rng.randint(1, 2)):
            for _ in range(rng.randint(0, 2)):
                control = rng.choice((1, 2, 5, 6, 7, 11))
                ours.append(f'control {control}')
                theirs += ['prefs:full', 'prefs:alert'] if control == 6 else ['prefs:' + CONTROLS[control]]
            ours.append(f'manual {letter} {root} bay' if rng.random() < 0.3 else f'manual {letter} {root}')
            theirs.append(f'help:{root}')
            helps += 1
        number = self.numbered
        self.numbered += 1
        mine = self.ours(ours)
        state = [l.split() for l in mine if l.startswith('STATE ')][-1]
        paths = [l.split(' ', 3) for l in mine if l.startswith('MANUAL ')]
        disk = next(l.split()[1] for l in mine if l.startswith('FILE '))
        expected = f'{root}{RELATIVE[letter]}{B}es2guide.hlp'
        label = f'{letter} {root} {ours}'
        self.check(len(paths) == helps and all(p[1] == '0' and p[2] == '1' and p[3] == expected for p in paths),
                   f'{label}: our manual answers {paths}, expected {expected}')
        if not shard.owns(number):
            return helps
        shard.ran(f'help {label}')
        result = self.theirs(theirs, file, letter)
        meta = result['metadata']
        written = {k.lower(): v for k, v in result['disk'].items()}
        self.check(meta['prefs'] == state[1] and meta['prefs_committed'] == state[2],
                   f'{label}: blocks {meta["prefs"]}/{meta["prefs_committed"]}, ours {state[1]}/{state[2]}')
        prefs = written.get('data' + B + 'prefs.cfg')
        self.check((prefs.hex() if prefs is not None else '-') == disk,
                   f'{label}: file {None if prefs is None else prefs.hex()}, ours {disk}')
        original = written.get('help.path', b'').split(b'\0')[:-1]
        self.check([p.decode('latin-1') for p in original] == [expected] * helps,
                   f'{label}: WinHelp paths {original}, expected {helps} x {expected}')
        calls = [c for c in meta['ui_calls'] if c[0] == 0x407953]
        self.check(len(calls) == helps, f'{label}: {len(calls)} WinHelp wrapper calls, expected {helps}')
        return helps

    def main(self):
        rng = random.Random(2102)
        files = [None, bytes(range(1, 11)), bytes((5 * i + 1) & 0xff for i in range(54)),
                 bytes((3 * i + 7) & 0xff for i in range(60)), bytes(54), bytes([1] * 54)]
        sequences = helps = 0
        for i in range(36):
            helps += self.sequence(rng, 'EFG'[i % 3], files[(i // 3) % len(files)])
            sequences += 1
        self.report['sequences'] = sequences
        self.report['help_calls'] = helps
        self.report['worker_runs'] = self.runs
        self.report['letters'] = 'EFG'
        self.check(helps >= 36, f'only {helps} help calls')


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:4])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, 'help-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    shard.finish('help')
    print(json.dumps(dict(report=test.report, failures=test.failures), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
