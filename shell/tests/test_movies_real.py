# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Movie queue and player against VSHELL's own 0x41e8c0 and 0x41e98c
in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/movies.h through the test
driver movies_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro, slots_oracle.py) with the
operations movie-init (static initialiser 0x41f11e), movie-flag,
movie-once, movie-enqueue (0x41e8c0), movie-play (0x41e98c with stand-in
objects; the avivideo device does not open, so nothing is played) and
movie-dump (ring 0x485748, indices, player flags, movie flag, once-flags,
selection words).

- the 86 names of the pointer table 0x470fb4 read from the hash-checked
  VSHELL.EXE equal ours;
- SWEEP seeded runs of enqueues (duplicates, -1, low-word aliases, full-ring
  overwrites, the movie flag) and player runs (bay bar, theatre panel, full
  screen, page, palette argument; intro, credits, dropship and sequence
  records): every dump equal byte for byte, the recorded player calls equal
  in order with their arguments;
- the once-flags after the slot load 0x40eaaa (GAME_R, GAME_T) and
  the briefing movie word 0x48419c after a load of GAME_R and GAME_T equal
  ours (es2_shell_briefing_selection, the word the page-1 rule queues).
Nothing original is stored; results are reported as counts.

Arguments: movies_tool es2shellprep slots_oracle.py reference_pins.py
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import time

SWEEP = 40                  # seeded worker runs
VSHELL_SHA = '1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e'
LOG = 0x46a834              # the worker records the debug log too; not a player action
ARG_WORD = {0x407b62, 0x445343}   # word pushes with an undefined high half


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table(used=()):
    """An own slot table (gamefile.str) naming GAME_0.SAV .. GAME_11.SAV; the
    byte after each text is the slot's used flag."""
    body = struct.pack('<h', 12)
    for k in range(12):
        name = f'GAME_{k}.SAV'.encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\0'
    body += struct.pack('<h', 12)
    for k in range(12):
        text = f'S{k}'.encode() + b'\0'
        body += struct.pack('<h', len(text)) + text + bytes([k in used])
    return struct.pack('<I', len(body)) + body


def exe_names(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != VSHELL_SHA:
        raise AssertionError('VSHELL.EXE is not the reference version')
    pe, = struct.unpack_from('<I', data, 0x3c)
    count, = struct.unpack_from('<H', data, pe + 6)
    optional, = struct.unpack_from('<H', data, pe + 20)
    sections = []
    for i in range(count):
        at = pe + 24 + optional + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, at + 8)
        sections.append((va + 0x400000, max(vsize, rsize), raw, rsize))

    def offset(va):
        for start, size, raw, rsize in sections:
            if start <= va < start + size and va - start < rsize:
                return raw + va - start
        raise AssertionError(f'VA {va:#x} outside the file')
    names = []
    for i in range(86):
        pointer, = struct.unpack_from('<I', data, offset(0x470fb4 + 4 * i))
        at = offset(pointer)
        names.append(data[at:data.index(b'\0', at)].decode('latin-1'))
    return names


def u32(rng, choices):
    return rng.choice(choices) & 0xffffffff


def sweep_ops(rng):
    ops = ['movie-init']
    plays = 0
    indices = [0x44, 0x45, 0x46, 0x4a, 0x4b, 0x4f, 0x53, 0x54, 0x55, 5, 0x30, 0x10005, 0xffff, -1]
    for _ in range(rng.randrange(6, 24)):
        roll = rng.random()
        if roll < 0.62:
            index = u32(rng, indices + [rng.randrange(0, 86)])
            rect = [u32(rng, [0x20, 0x15, 0x122, rng.randrange(-5, 1000)]) for _ in range(4)]
            palette = u32(rng, [-1, 3, 4, 7, 12, 0x10003, rng.randrange(0, 0x20000)])
            sequence = u32(rng, [0, 0, 0, 1, 0x100])
            ops.append('movie-enqueue:' + ':'.join(str(x) for x in [index, *rect, palette, 0, sequence]))
        elif roll < 0.82 and plays < 4:
            plays += 1
            ops.append(f'movie-play:{rng.choice([0, 1])}:{rng.choice([0, 1])}:{rng.choice([0, 1])}:'
                       f'{rng.choice([0, 1])}:{rng.choice([0, 1, 4])}')
        elif roll < 0.86:
            ops.append(f'movie-flag:{rng.choice([0, 1, 1, 2])}')
        elif roll < 0.9:
            ops.append(f'movie-once:{rng.randrange(2)}:{rng.randrange(2)}:{rng.randrange(2)}')
        else:
            ops.append('movie-init')
        ops.append('movie-dump')
    return ops


class Test:
    def __init__(self, argv):
        self.tool, self.worker, oracle_py, pins_py = [Path(a).resolve() for a in argv]
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.oracle = load(oracle_py, 'movies_oracle')
        self.pins = load(pins_py, 'movies_pins')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2movies-')
        self.tmp = Path(self.scratch.name)
        self.failures, self.report, self.runs = [], {}, 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return bool(condition)

    def ours(self, lines):
        run = subprocess.run([str(self.tool)], input=('\n'.join(lines) + '\n').encode('ascii'),
                             capture_output=True, timeout=120)
        if run.returncode:
            raise AssertionError(f'movies_tool exited {run.returncode}: {run.stdout[-500:]!r}')
        return run.stdout.decode('latin-1').splitlines()

    def original(self, ops, disk=None, used=()):
        self.runs += 1
        files = {'sav\\gamefile.str': table(used)}
        files.update(disk or {})
        result = self.oracle.run(self.worker, self.cd, files, ops, self.tmp / f'run-{self.runs}')
        data = result['disk'].get('movies.emit', b'')
        dumps = [data[at:at + 304] for at in range(0, len(data), 304)]
        calls = []
        for address, a, _, _ in result['metadata']['ui_calls']:
            if address == LOG:
                continue
            if address in ARG_WORD:
                a = ((a & 0xffff) ^ 0x8000) - 0x8000
            elif address not in (0x41e51b, 0x43a430, 0x4022c5):
                a = 0
            calls.append(f'{address:x}:{a}')
        return dumps, calls

    def names(self):
        exe = self.cd / 'VER95' / 'VSHELL.EXE'
        theirs = exe_names(exe)
        mine = [line.split(' ', 2)[2] for line in self.ours(['NAMES']) if line.startswith('NAME ')]
        self.check(mine == theirs, 'name table differs from 0x470fb4')
        self.report['names'] = len(theirs)

    def sweep(self):
        rng = random.Random(1515)
        equal_dumps = equal_calls = total_dumps = total_calls = 0
        full, panel, theatre = '32:60:576:360', '21:86:239:179', '290:67:295:226'
        fixed = [
            # the sites' own sequences: intro (0x401955, 0x401983, play 0), the
            # first mission visit in theatre 5 with the dropship the theatre step
            # queues (0x444aa5, 0x444ad5, 0x4447af; trapped here, so queued by
            # hand), victory and credits (0x40f58d, 0x40f5bb, play 1), eleven
            # records (the oldest overwritten)
            ['movie-init', f'movie-enqueue:68:{full}:65535:0:0', f'movie-enqueue:69:{full}:65535:0:0',
             'movie-play:0:0:0:1:0', 'movie-dump'],
            ['movie-init', f'movie-enqueue:74:{panel}:4:0:0', f'movie-enqueue:79:{theatre}:65535:0:1',
             f'movie-enqueue:85:{full}:65535:0:0', 'movie-play:1:1:0:0:0', 'movie-dump'],
            ['movie-init', f'movie-enqueue:83:{full}:65535:0:0', f'movie-enqueue:84:{full}:65535:0:0',
             'movie-play:1:0:0:0:0', 'movie-dump'],
            ['movie-init'] + [f'movie-enqueue:{k}:{full}:{k}:0:0' for k in range(11)] + ['movie-dump',
             'movie-play:1:1:1:0:1', 'movie-dump'],
        ]
        for case in range(len(fixed) + SWEEP):
            ops = fixed[case] if case < len(fixed) else sweep_ops(rng)
            dumps, calls = self.original(ops)
            mine = self.ours(ops)
            my_dumps = [bytes.fromhex(l[5:]) for l in mine if l.startswith('DUMP ')]
            my_calls = next(l for l in mine if l.startswith('CALLS')).split()[1:]
            total_dumps += len(dumps)
            total_calls += len(calls)
            for k, (theirs, ours) in enumerate(zip(dumps, my_dumps)):
                equal_dumps += self.check(theirs[:300] == ours, f'sweep {case} dump {k}: '
                                          f'original {theirs[:300].hex()} ours {ours.hex()} ops {ops}')
            self.check(len(dumps) == len(my_dumps), f'sweep {case}: dump count')
            if self.check(calls == my_calls, f'sweep {case}: calls original {calls} ours {my_calls} ops {ops}'):
                equal_calls += len(calls)
        self.report['sweep'] = dict(runs=len(fixed) + SWEEP, dumps=total_dumps, equal_dumps=equal_dumps,
                                    calls=total_calls, equal_calls=equal_calls)

    def flags_and_words(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        ops = ['movie-init', 'movie-once:1:1:1', 'load:0:0', 'movie-dump',
               'movie-once:1:1:1', 'load:1:0', 'movie-dump']
        # the load copies the slot's companions to DATA (slots_real_test's own bytes)
        disk = {'sav\\GAME_0.SAV': game_r, 'sav\\GAME_1.SAV': game_t,
                'data\\script.dat': b'own script bytes', 'data\\mission.str': b'own text bytes',
                'data\\player.mec': b'own mec bytes'}
        for k in (0, 1):
            disk.update({f'sav\\script{k}.dat': b'script', f'sav\\missn{k}.str': b'text',
                         f'sav\\player{k}.mec': b'mec'})
        dumps, _ = self.original(ops, disk, used=(0, 1))
        # The new game 0x40e8a5 (0x40e8b1, 0x40e8b8) is not run here: every op
        # first passes s17_op, which arms its quiet trap of the ESTEXT lookup
        # 0x4087f0, and the new game then faults in strlen.
        mine = self.ours(['movie-init', 'movie-once:1:1:1', 'load', 'movie-dump', 'movie-once:1:1:1', 'load',
                          'movie-dump', f'WORD {game_r.hex()}', f'WORD {game_t.hex()}'])
        my_dumps = [bytes.fromhex(l[5:]) for l in mine if l.startswith('DUMP ')]
        words = [int(l[5:]) for l in mine if l.startswith('WORD ')]
        equal = sum(self.check(t[296:300] == o[296:300], f'once-flags after op {k}: {t[296:300].hex()} '
                               f'ours {o[296:300].hex()}') for k, (t, o) in enumerate(zip(dumps, my_dumps)))
        theirs = [struct.unpack_from('<h', d, 300)[0] for d in dumps[:2]]
        self.check(theirs == words, f'briefing words after load: original {theirs} ours {words}')
        self.report['once_flags'] = dict(cases=len(dumps), equal=equal, briefing_words=theirs)

    def main(self):
        started = time.time()
        self.names()
        self.sweep()
        self.flags_and_words()
        self.report['worker_runs'] = self.runs
        self.report['seconds'] = round(time.time() - started, 1)


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:])
    try:
        test.main()
    finally:
        print(test.report, flush=True)
        test.scratch.cleanup()
    if test.failures:
        print(f'{len(test.failures)} failures')
        return 1
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
