# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Mission-screen texts against VSHELL's own composition in the
mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/texts.h through the test
driver texts_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro, slots_oracle.py) with the
operations `briefing:N` (0x4135bb after record N of texts.in became
data\\mission.str and the selection 0x4840cc), `campaign:N` (0x40fd2d) and
`debrief-stats:O:T:L:S` (0x40f904 on the restored state); their texts come
back in the memory-disk file texts.emit and are compared byte for byte.

Per language (ENG, FRE, GER; one worker run each for the texts):
- page 1: every campaign mission of CAREER (groups >= 1) prepared by our
  preparation (equal to the original's, //shell:prepare_real_test) from a
  new career; its MISSION.STR and the selection of the prepared SAV; then
  SWEEP seeded selections (references -1 or inside the group, the first
  one often -1) over those MISSION.STRs;
- page 0: campaign.str strings 0..5 (5 is past the end: null in both) and
  65535 (group 0: null);
- page 4: STATS_PER_SAV seeded word sets (outcome -1..3, tallies up to
  2^32 - 1, signed lost/recovered words) on GAME_R, GAME_T, the new career
  and seeded edits of them (pilot kill words, active bytes, order words,
  team word 0) and the SAVs our results path writes for three oracle
  goldens, one worker run per SAV;
- the page-4 words derived from results.dat and the written SAV
  (es2_shell_debrief_words_after) equal the results path's own lost count
  and P + added, for every golden whose results.dat advances from GAME_R.
Nothing original is stored; texts are compared in memory and reported as
counts.

Arguments: texts_tool es2prep_core es2shellprep slots_oracle.py
reference_pins.py es2menu_backend.py es2prep.py

Sharding (shard.py): the units that run the original are numbered in one fixed order (per
language: pages 0 and 1, then each SAV of page 4) and unit n runs in Bazel shard n % shards. Every
shard draws the same SAVs and word sets from the seeded streams. Without sharding every unit runs.
"""
import hashlib
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
SWEEP = 40           # synthetic selections per language
STATS_PER_SAV = 24   # word sets per SAV and language
EDITED = 3           # edited SAVs per corpus SAV
LANGUAGES = (('ENG', 'E', 'eng'), ('FRE', 'F', 'fre'), ('GER', 'G', 'ger'))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table():
    """An own slot table (gamefile.str), no flagged slot."""
    body = struct.pack('<h', 12)
    for k in range(12):
        name = f'GAME_{k}.SAV'.encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\0'
    body += struct.pack('<h', 12)
    for k in range(12):
        text = f'S{k}'.encode() + b'\0'
        body += struct.pack('<h', len(text)) + text + b'\0'
    return struct.pack('<I', len(body)) + body


def emitted(data):
    """texts.emit: [u32 size][bytes]; size 0xffffffff is a null pointer (None)."""
    out, at = [], 0
    while at < len(data):
        n, = struct.unpack_from('<I', data, at)
        at += 4
        if n == 0xffffffff:
            out.append(None)
        else:
            out.append(data[at:at + n])
            at += n
    return out


def digest(texts):
    """Lengths and hash prefixes only: game text never goes into a log."""
    if texts is None:
        return 'null'
    if isinstance(texts, bytes):
        texts = [texts]
    return [(len(t), hashlib.sha256(t).hexdigest()[:12]) if t is not None else None for t in texts]


def str_count(data):
    return struct.unpack_from('<h', data, 4)[0]


class Pilots:
    """Offsets of the pilot suffixes and team word 0 in a SAV (own walker)."""
    def __init__(self, sav):
        word = lambda at: struct.unpack_from('<h', sav, at)[0]
        at = 0
        for _ in range(33):
            at += 3 + 10 * word(at + 1)
        at += 22 + 152
        self.roster = []
        for _ in range(36):
            n = word(at + 4)
            self.roster.append(at + 6 + n)
            at += 31 + n
        self.team = at + 12
        at += 16
        self.player = at + 6 + word(at + 4)


def edit(rng, sav):
    w = Pilots(sav)
    out = bytearray(sav)
    for suffix in [w.player] + w.roster:
        for off in (0x2d, 0x2f, 0x31):
            if rng.random() < 0.6:
                struct.pack_into('<h', out, suffix + off - 0x22, rng.choice((0, 1, 2, 7, 99, 1000, -1, 32767, -32768)))
    for suffix in w.roster:
        if rng.random() < 0.3:
            out[suffix + 2] = rng.choice((0, 1, 2, 255))
        if rng.random() < 0.3:
            struct.pack_into('<h', out, suffix + 5, rng.randrange(-1, 4))   # the worker's SAV check: -1..3
    if rng.random() < 0.7:
        struct.pack_into('<h', out, w.team, rng.randrange(0, 5))   # 0..4
    return bytes(out)


class Test:
    def __init__(self, argv):
        (self.tool, self.core, self.worker, oracle_py, pins_py, backend_py, prep_py) = [Path(a).resolve() for a in argv]
        self.oracle, self.pins = load(oracle_py, 'texts_oracle'), load(pins_py, 'texts_pins')
        self.backend, self.prep = load(backend_py, 'texts_backend'), load(prep_py, 'texts_prep')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2texts-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.failures, self.report, self.runs = [], {}, 0
        self.numbered = 0

    def mine(self, what):
        """Numbers the next unit that runs the original; True when this shard runs it."""
        number = self.numbered
        self.numbered += 1
        if shard.owns(number):
            shard.ran(f'{number}:{what}')
            return True
        return False

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def ours(self, commands):
        """Answers of texts_tool, one list of lines per command."""
        text = f'ROOT {self.cd}\n' + ''.join(c + '\n' for c in commands)
        run = subprocess.run([str(self.tool)], input=text, capture_output=True, text=True, check=True)
        answers, current = [], []
        for line in run.stdout.splitlines():
            if line == 'END':
                answers.append(current)
                current = []
            else:
                current.append(line)
        if not self.check(answers and answers[0] == [], 'texts_tool could not mount the CD'):
            return [[] for _ in commands]
        return answers[1:]

    @staticmethod
    def texts(answer):
        if any(line.startswith('ERR') for line in answer):
            return None
        return [bytes.fromhex(line[2:]) for line in answer if line.startswith('T ')]

    def theirs(self, ops, letter, disk, save=None):
        self.runs += 1
        disk = dict(disk)
        disk['sav' + B + 'gamefile.str'] = table()
        got = self.oracle.run(self.worker, self.cd, disk, ops, self.tmp / f'run-{self.runs}', save=save, language=letter)
        return emitted(got['disk'].get('texts.emit', b''))

    # ---- pages 0 and 1 --------------------------------------------------------
    def texts_for(self, lang, letter, career):
        if not self.mine(f'{lang} pages 0 and 1'):
            return
        missions = [(g, p) for g, p, _ in self.prep.campaign(self.core, self.cd) if g >= 1]
        tables = self.prep.default_tables(self.cd)
        rng = self.prep.initial_rng(self.cd, 71)
        requests = [self.prep.request(group=g, position=p, mode=1, language=lang, rng=rng, tables=tables,
                                      save=career, prepared_save=True) for g, p in missions]
        prepared = self.prep.prepare_batch(self.core, self.cd, requests)
        records = []
        for (g, p), out in zip(missions, prepared):
            if not self.check('error' not in out and out.get('prepared_save'), f'{lang} {g}:{p}: our preparation failed'):
                continue
            records.append((f'{lang} {g}:{p}', out['MISSION.STR'], out['prepared_save']))
        selections = self.ours([f'SELECT {sav.hex()}' for _, _, sav in records])
        cases = []
        for (label, strings, _), answer in zip(records, selections):
            if self.check(len(answer) == 1 and answer[0].startswith('S '), f'{label}: no selection'):
                cases.append((label, strings, bytes.fromhex(answer[0][2:])))
        sweep = random.Random(1100 + len(lang) + ord(letter))
        for trial in range(SWEEP):
            label, strings, _ = sweep.choice(cases)
            count = str_count(strings)
            refs = []
            for k in range(70):
                refs.append(-1 if count == 0 or sweep.random() < 0.4 or (k in (0, 10, 40) and sweep.random() < 0.5)
                            else sweep.randrange(count))
            words = [sweep.randrange(-3, 40)] + refs[:10] + [sweep.randrange(-3, 40)] + refs[10:40] + \
                    [sweep.randrange(-3, 40)] + refs[40:] + [sweep.randrange(0, 200)]
            cases.append((f'{label} sweep {trial}', strings, struct.pack('<74h', *words)))
        commands = [f'BRIEF {s.hex()} {sel.hex()}' for _, s, sel in cases]
        commands += [f'CAMPAIGN {lang} {g}' for g in range(0, 7)]
        commands += [f'TITLE {lang} {g}' for g in range(1, 6)]
        mine = self.ours(commands)
        texts_in = b''.join(struct.pack('<I', len(s)) + s + sel for _, s, sel in cases)
        ops = [f'briefing:{k}' for k in range(len(cases))] + [f'campaign:{(g - 1) & 0xffff}' for g in range(0, 7)]
        theirs = self.theirs(ops, letter, {'texts.in': texts_in})
        if not self.check(len(theirs) == 3 * len(cases) + 7, f'{lang}: {len(theirs)} texts from the worker'):
            return
        equal = 0
        for k, (label, _, _) in enumerate(cases):
            got = self.texts(mine[k])
            equal += self.check(got == theirs[3 * k:3 * k + 3], f'{label}: page-1 texts differ')
        page0 = 0
        for j, g in enumerate(range(0, 7)):
            got = self.texts(mine[len(cases) + j])
            want = theirs[3 * len(cases) + j]
            page0 += self.check((got is None and want is None) or (got == [want]),
                                f'{lang} campaign group {g}: ours {digest(got)} theirs {digest(want)}')
        titles = [self.texts(a) for a in mine[len(cases) + 7:]]
        self.check(all(t and len(t) == 1 and t[0] for t in titles), f'{lang}: titles refused')
        self.report[f'{lang} page 1'] = dict(cases=len(cases), missions=len(records), equal=equal)
        self.report[f'{lang} page 0'] = dict(cases=7, equal=page0, titles=len(titles))

    # ---- page 4 --------------------------------------------------------------
    def stats_for(self, lang, letter, saves):
        rng = random.Random(4000 + ord(letter))
        total = equal = 0
        for label, sav in saves:
            words = []
            for _ in range(STATS_PER_SAV):
                words.append((rng.randrange(-1, 4) & 0xffff,
                              rng.choice((0, 999, 1000, 1001, 100000, 123456789, 2 ** 32 - 1, rng.randrange(2 ** 32))),
                              rng.choice((0, 1, 2, 3, 65535, 32768, rng.randrange(65536))),
                              rng.choice((0, 1, 5, 40, 65535, 32767, rng.randrange(65536)))))
            if not self.mine(f'{lang} page 4 {label}'):
                continue
            mine = self.ours([f'STATS {lang} {sav.hex()} {o} {t} {l} {s}' for o, t, l, s in words])
            theirs = self.theirs([f'debrief-stats:{o}:{t}:{l}:{s}' for o, t, l, s in words], letter, {}, save=sav)
            for k, ws in enumerate(words):
                total += 1
                equal += self.check(self.texts(mine[k]) == theirs[10 * k:10 * k + 10],
                                    f'{lang} {label} {ws}: page-4 texts differ')
        self.report[f'{lang} page 4'] = dict(cases=total, equal=equal)

    # ---- page-4 words after a results path ------------------------------------
    def results_saves(self, game_r):
        """Our results path (equal to the original's, results_compare) on the
        recorded results.dat/mission.var of the oracle goldens from GAME_R;
        the words es2_shell_debrief_words_after derives from results.dat and
        the written SAV must equal the path's own lost and P + added."""
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden'
        inputs = []
        for case in sorted(golden.glob('*/output')) if golden.is_dir() else []:
            if (case / 'results.dat').is_file() and (case / 'mission.var').is_file():
                inputs.append((case.parent.name, (case / 'results.dat').read_bytes(), (case / 'mission.var').read_bytes()))
        if not inputs:
            print('note: no oracle goldens with results.dat; results words not checked')
            return []
        tables = self.prep.results_tables(self.cd)
        rng = self.prep.initial_rng(self.cd, 71)
        requests = [self.prep.results_request(tables=tables, save=game_r, variables=var, results=res, rng=rng)
                    for _, res, var in inputs]
        outs = self.prep.results_batch(self.core, self.cd, requests)
        cases = [(name, res, out) for (name, res, _), out in zip(inputs, outs)
                 if 'error' not in out and out['ending'] == 5]
        mine = self.ours([f'AFTER {res.hex()} {out["sav"].hex()}' for _, res, out in cases])
        equal = 0
        for (name, res, out), answer in zip(cases, mine):
            pairs, = struct.unpack_from('<h', res, 6)
            want = [struct.unpack_from('<h', res)[0], struct.unpack_from('<I', res, 2)[0],
                    ((pairs + out['added'] + 0x8000) & 0xffff) - 0x8000, out['lost']]
            got = [int(x) for x in answer[0].split()[1:5]] if answer and answer[0].startswith('W ') else None
            equal += self.check(got == want, f'results words {name}: ours {got} path {want}')
        self.report['results words'] = dict(goldens=len(inputs), advanced=len(cases), equal=equal)
        return [(f'after {name}', out['sav']) for name, _, out in cases[:3]]

    def main(self):
        started = time.time()
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        rng = random.Random(1111)
        after = self.results_saves(game_r)
        for lang, letter, _ in LANGUAGES:
            career = self.backend.new_game_request(self.core, self.cd, lang, 'TESTPILOT', 2, 1, 0,
                                                   self.prep.initial_rng(self.cd, 71))[0]
            self.texts_for(lang, letter, career)
            saves = [('GAME_R', game_r), ('GAME_T', game_t), ('new career', career)]
            saves += [(f'{label} edit {k}', edit(rng, sav)) for label, sav in list(saves) for k in range(EDITED)]
            saves += after
            self.stats_for(lang, letter, saves)
        self.report['worker_runs'] = self.runs
        self.report['seconds'] = round(time.time() - started, 1)


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:8])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    shard.finish('texts')
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
