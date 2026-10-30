# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Slot statistics against VSHELL's own start-up read 0x4309f0 and
refresh 0x430ec4 in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/slots.h through the test
driver stats_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro, slots_oracle.py) with the
operations `stats` (0x4309f0), `stats-refresh:J` (0x430ec4) and
`stats-dump` (the eleven records at 0x48cebc + 0x43*slot), with the game
of slot K's SAV restored through the original readers (--save; the memory
state a load 0x40eaaa leaves, whose mode-1 branch calls 0x430ec4 itself).

Per case: an own slot table with a random set of flagged slots 0..10, each
flagged slot a SAV of the corpus; then stats, dump, refresh:K, refresh:J,
dump. Both dumps must equal ours byte for byte (737 bytes each).

Corpus: GAME_R, GAME_T (pinned clone files), a new career, then seeded
edits of them (pilot name length 0..30 and bytes, skill, rank, the eleven
pilot words, salvage, the CAREER words). Nothing original is stored.

Arguments: stats_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2prep.py

Sharding (shard.py): case n runs in Bazel shard n % shards; every shard draws all cases from
the seeded stream, so the cases are those of an unsharded run (which runs every case).
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

import shard

B = '\\'
SIZE = 11 * 0x43
SWEEP = 60          # seeded cases (one worker run each)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table(flags):
    """An own slot table (gamefile.str): GAME_k.SAV, texts "Sk", the flags."""
    body = struct.pack('<h', 12)
    for k in range(12):
        name = f'GAME_{k}.SAV'.encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\0'
    body += struct.pack('<h', 12)
    for k in range(12):
        text = f'S{k} own'.encode() + b'\0'
        body += struct.pack('<h', len(text)) + text + bytes([1 if k in flags else 0])
    return struct.pack('<I', len(body)) + body


class Walk:
    """Offsets in a SAV as the reader 0x4309f0 seeks (own walker)."""
    def __init__(self, sav):
        word = lambda at: struct.unpack_from('<h', sav, at)[0]
        at = 0
        for _ in range(33):
            at += 3 + 10 * word(at + 1)
        self.career = at + 22
        at += 22 + 4 + 148
        for _ in range(36):
            at += 31 + word(at + 4)
        self.pilot = at + 16
        at = self.pilot + 4
        self.name = at
        at += 2 + word(at)
        self.suffix = at
        at += 25
        count = word(at)
        at += 2
        for _ in range(max(count, 0)):
            at += 80 + 12 * word(at + 78)
        self.salvage = at + 18


def edit(rng, sav, restorable=False):
    """restorable: a name the worker's --save check accepts (1..30 bytes,
    the last one NUL), for the SAV whose game is restored into memory."""
    w = Walk(sav)
    out = bytearray(sav)
    if rng.random() < 0.5:
        n = rng.randrange(1, 31) if restorable else rng.randrange(0, 31)
        name = bytes(rng.choice(b'ABCZ^ 0123xyz') for _ in range(n))
        if restorable or (rng.random() < 0.5 and n):
            name = name[:-1] + b'\0'
        old = struct.unpack_from('<h', out, w.name)[0]
        out[w.name:w.name + 2 + old] = struct.pack('<h', n) + name
        w = Walk(bytes(out))
    for k in range(11):
        if rng.random() < 0.4 and (not restorable or k in (4, 5, 6, 7, 8, 9)):
            struct.pack_into('<h', out, w.suffix + 3 + 2 * k, rng.choice((0, 1, 2, 3, 7, 99, 1000, -1, 32767)))
    if not restorable and rng.random() < 0.5:
        struct.pack_into('<h', out, w.suffix, rng.randrange(-2, 9))
        out[w.suffix + 2] = rng.randrange(256)
    if rng.random() < 0.5:
        struct.pack_into('<i', out, w.salvage, rng.choice((0, 999, 1000, 1001, 123456789, -1, -1001, 2 ** 31 - 1)))
    if not restorable and rng.random() < 0.5:
        struct.pack_into('<hh', out, w.career, rng.randrange(0, 6), rng.randrange(0, 20))
    if not restorable:
        struct.pack_into('<hh', out, w.pilot, rng.randrange(-5, 50), rng.randrange(-5, 50))
    return bytes(out)


class Test:
    def __init__(self, argv):
        (self.tool, self.core, self.worker, oracle_py, pins_py, backend_py, prep_py) = [Path(a).resolve() for a in argv]
        self.oracle, self.pins = load(oracle_py, 'stats_oracle'), load(pins_py, 'stats_pins')
        self.backend, self.prep = load(backend_py, 'stats_backend'), load(prep_py, 'stats_prep')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2stats-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.failures, self.report, self.runs = [], {}, 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def corpus(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        rng = self.prep.initial_rng(self.cd, 71)
        career = self.backend.new_game_request(self.core, self.cd, 'GER', 'TESTPILOT', 2, 1, 0, rng)[0]
        return [game_r, game_t, career]

    def ours(self, cases):
        text = ''
        for case in cases:
            text += 'CASE\n' + ''.join((case['savs'][k].hex() if k in case['savs'] else '-') + '\n' for k in range(11))
            text += f'OPS stats dump memory:{case["load"]} refresh:{case["load"]} refresh:{case["refresh"]} dump\n'
        run = subprocess.run([str(self.tool)], input=text, capture_output=True, text=True, check=True)
        out, current = [], []
        for line in run.stdout.splitlines():
            if line == 'END':
                out.append(current)
                current = []
            else:
                current.append(line)
        return out

    def theirs(self, case):
        self.runs += 1
        disk = {'sav' + B + 'gamefile.str': table(set(case['savs']))}
        for k, sav in case['savs'].items():
            disk['sav' + B + f'GAME_{k}.SAV'] = sav
        ops = ['stats', 'stats-dump', f'stats-refresh:{case["load"]}', f'stats-refresh:{case["refresh"]}', 'stats-dump']
        got = self.oracle.run(self.worker, self.cd, disk, ops, self.tmp / f'run-{self.runs}',
                              save=case['savs'][case['load']])
        dump = got['disk'].get('stats.emit', b'')
        return [dump[:SIZE], dump[SIZE:]]

    def main(self):
        started = time.time()
        corpus = self.corpus()
        rng = random.Random(2020)
        cases = []
        # every corpus SAV alone in every refreshed slot position class, then mixed
        for i, sav in enumerate(corpus):
            cases.append(dict(label=f'corpus {i}', savs={0: sav, 10: sav}, load=0, refresh=10))
        for trial in range(SWEEP):
            slots = rng.sample(range(11), rng.randrange(1, 12))
            savs = {k: edit(rng, rng.choice(corpus)) if rng.random() < 0.8 else rng.choice(corpus) for k in slots}
            k = rng.choice(slots)
            if savs[k] not in corpus:
                savs[k] = edit(rng, rng.choice(corpus), restorable=True)
            cases.append(dict(label=f'sweep {trial}', savs=savs, load=k, refresh=rng.randrange(11)))
        # Sharding (shard.py): case n runs in Bazel shard n % shards; every shard draws all cases.
        total = len(cases)
        cases = [case for n, case in enumerate(cases) if shard.owns(n)]
        for case in cases:
            shard.ran(case['label'])
        ours = self.ours(cases)
        for case, mine in zip(cases, ours):
            theirs = self.theirs(case)
            errors = [line for line in mine if line.startswith('ERR')]
            if not self.check(not errors, f'{case["label"]}: ours refused {errors}'):
                continue
            dumps = [bytes.fromhex(line[2:]) for line in mine if line.startswith('D ')]
            for stage, (a, b) in enumerate(zip(dumps, theirs)):
                if a != b:
                    diff = [i for i in range(SIZE) if a[i] != b[i]][:8]
                    self.check(False, f'{case["label"]} stage {stage}: records differ at {diff} '
                                      f'(slot/offset {[divmod(i, 0x43) for i in diff]})')
            self.check(len(dumps) == 2 and all(len(t) == SIZE for t in theirs), f'{case["label"]}: dump sizes')
        self.report = dict(cases=len(cases), of=total, worker_runs=self.runs, seconds=round(time.time() - started, 1))


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:8])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    shard.finish('stats')
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
