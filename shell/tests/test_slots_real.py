# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Save slots, new career and the SAV after a load or a preparation, byte for
byte against VSHELL's own functions in the mapped original.

Skipped without ES2_DATA_ROOT. The original runs through the memory worker's
slot mode (tools/reference-game/shell-invitro/slots_oracle.py: 0x40e933 save,
0x40eaaa load, 0x40e8a5 new game, start-up reader 0x40e380) and its
preparation with --emit-save-after; ours through es2prep_core (--slot-save,
--loaded-save, --new-game, the preparation batch with prepared_save). Every
slot table, name and file in this test is synthetic or a CD/clone file read
at run time; nothing original is stored.

Arguments: es2prep.py es2menu_backend.py es2prep_core es2shellprep
slots_oracle.py es2shellprep.py reference_pins.py

Sharding (shard.py): the cases of the four parts are numbered in one fixed order and case
n runs in Bazel shard n % shards; every shard draws the same cases (the career of the last
new-career case, which the later parts start from, is computed everywhere). Without sharding
every case runs.
"""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import shard

LANGUAGES = {'G': 'GER', 'E': 'ENG', 'F': 'FRE'}
B = '\\'
DATA = {'data' + B + 'script.dat': b'own script bytes', 'data' + B + 'mission.str': b'own text bytes',
        'data' + B + 'player.mec': b'own mec bytes'}
COMPANIONS = (('script%d.dat', 'script.dat'), ('missn%d.str', 'mission.str'), ('player%d.mec', 'player.mec'))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table(texts, files=None, flags=(), counts=(12, 12), tail=b''):
    """An own slot table: file names GAME_k.SAV (or `files`), the given texts."""
    files = files or [f'GAME_{k}.SAV' for k in range(12)]
    body = struct.pack('<h', counts[0])
    for k in range(counts[0]):
        name = files[k].encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\x07'
    body += struct.pack('<h', counts[1])
    for k in range(counts[1]):
        text = texts[k].encode('latin-1') + b'\0'
        body += struct.pack('<h', len(text)) + text + bytes([1 if k in flags else 0])
    return struct.pack('<I', len(body) + len(tail)) + body + tail


class Test:
    def __init__(self, argv):
        (prep_py, backend_py, self.core, self.worker, oracle_py, shellprep_py, pins_py) = [Path(a).resolve() for a in argv]
        self.prep, self.backend = load(prep_py, 'slots_prep'), load(backend_py, 'slots_backend')
        self.oracle, self.shellprep = load(oracle_py, 'slots_oracle_mod'), load(shellprep_py, 'slots_shellprep')
        self.pins = load(pins_py, 'slots_pins')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2slots-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.failures, self.report, self.runs = [], {}, 0
        self.numbered = 0

    def mine(self, what):
        """Numbers the next case; True when this shard runs it."""
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

    def run(self, disk, ops, **options):
        self.runs += 1
        return self.oracle.run(self.worker, self.cd, disk, ops, self.tmp / f'run-{self.runs}', **options)

    def core_lines(self, request, text):
        run = subprocess.run([str(self.core), str(self.cd), request], input=text, capture_output=True, text=True,
                             check=True)
        return run.stdout.splitlines()

    def empty(self, language):
        """ESTEXT.BIN entry 0x21 up to its NUL, as the original lookup 0x4087f0
        returns it; own VOLN reader (docs/formats/vol.md) on the CD's LANG0.VOL."""
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        size, = struct.unpack_from('<H', vol, 10)
        directories = vol[12:12 + size].split(b'\0')[:-1]
        count, = struct.unpack_from('<H', vol, 12 + size)
        for i in range(count):
            record = 18 + size + 18 * i
            name = vol[record:record + 13].split(b'\0')[0]
            if name.upper() == b'ESTEXT.BIN' and directories[vol[record + 13]].upper().startswith(LANGUAGES[language].encode()):
                block, = struct.unpack_from('<I', vol, record + 14)
                length, = struct.unpack_from('<I', vol, block + 1)
                payload = vol[block + 9:block + 9 + length]
                n, = struct.unpack_from('<I', payload, 0)
                offset, = struct.unpack_from('<H', payload, 8 + 2 * 0x21)
                return payload[8 + 2 * n + offset:payload.index(b'\0', 8 + 2 * n + offset)]
        raise AssertionError(f'ESTEXT.BIN of {language} not found')

    def ours_save(self, gamefile, language, slot, text, mode, image, saves):
        request = (f'{slot} {mode} {int(text is not None)} {len(saves)}\n{gamefile.hex()}\n{self.empty(language).hex()}\n'
                   f'{(text or "").encode("latin-1").hex()}\n{image.hex()}\n')
        for name, data in saves.items():
            request += name.encode().hex() + '\n' + data.hex() + '\n'
        lines = self.core_lines('--slot-save', request)
        if not lines[0].startswith('OK '):
            return None
        return int(lines[0].split()[1]), bytes.fromhex(lines[1]).decode(), bytes.fromhex(lines[2]), bytes.fromhex(lines[3])

    def loaded(self, sav):
        lines = self.core_lines('--loaded-save', sav.hex() + '\n')
        return bytes.fromhex(lines[1])

    # ---- 1. the save 0x40e933 ------------------------------------------------------
    def saves(self, game_r):
        cd_table = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        named = table([f'{k + 1:2d}. PILOT{k}' if k < 10 else 'RESUME' for k in range(12)], flags=(1, 10))
        short = table(['', 'ab', 'abcd', 'abcde', ' 5. ', '10. ^Z9 x'] + [f'T{k}' for k in range(6, 12)],
                      tail=b'\xaa' * 40)
        partial = table([f'{k + 1:2d}. ' for k in range(10)], counts=(12, 10))
        cases = []
        for language in 'GEF':
            cases.append(('cd', cd_table, language, 10, None, 1, None))
        cases += [('cd', cd_table, 'G', 10, None, 0, b'x' * 9000), ('cd', cd_table, 'G', 11, None, 1, b'y' * 10),
                  ('cd', cd_table, 'G', 0, ' 1. Z1', 1, None), ('cd', cd_table, 'G', 9, '10. ', 1, b'z' * 5000),
                  ('named', named, 'G', 4, ' 5. ALPHA 7', 1, b'q' * 100), ('named', named, 'E', 10, None, 1, None),
                  ('short', short, 'F', 2, ' 3. ^', 1, None), ('short', short, 'G', 10, None, 0, None),
                  ('partial', partial, 'E', 3, ' 4. B', 1, None)]
        equal = 0
        for label, gamefile, language, slot, text, mode, old in cases:
            if not self.mine(f'save {label} {language} {slot} {text!r} {mode} {None if old is None else len(old)}'):
                continue
            disk = {'sav' + B + 'gamefile.str': gamefile, **DATA}
            olds = {} if old is None else {f'GAME_{k}.SAV': old for k in range(12)}
            for name, data in olds.items():
                disk['sav' + B + name] = data
            op = f'save:{slot}' + ('' if text is None else ':' + text)
            theirs = self.run(disk, [op], save=self.loaded(game_r), language=language, mode=mode)
            mine = self.ours_save(gamefile, language, slot, text, mode, game_r, olds)
            what = f'save {label} {language} {op} mode {mode} old {None if old is None else len(old)}'
            if not self.check(mine is not None, f'{what}: refused by ours'):
                continue
            effective, file_name, gamefile_out, slot_out = mine
            got = {k.lower(): v for k, v in theirs['disk'].items()}
            ok = (got['sav' + B + 'gamefile.str'] == gamefile_out and got.get('sav' + B + file_name.lower()) == slot_out
                  and all(got.get('sav' + B + (c % effective)) == DATA['data' + B + d] for c, d in COMPANIONS))
            equal += self.check(ok, f'{what}: files differ from the original save')
        # Not loaded: the original writes nothing; strcpy(NULL) for 0..9 faults.
        if self.mine('save without a loaded game'):
            theirs = self.run({'sav' + B + 'gamefile.str': cd_table, **DATA}, ['save:10'], save=game_r, not_loaded=True)
            self.check(theirs['disk'].keys() == {'sav' + B + 'gamefile.str', *DATA} and
                       theirs['disk']['sav' + B + 'gamefile.str'] == cd_table, 'save without a loaded game writes nothing')
        if self.mine('save of slot 3 without a text'):
            try:
                self.run({'sav' + B + 'gamefile.str': cd_table, **DATA}, ['save:3'], save=game_r)
                self.check(False, 'save of slot 3 without a text did not fault')
            except RuntimeError as error:
                self.check('FAULT' in str(error), 'save of slot 3 without a text: no fault reported')
            self.check(self.ours_save(cd_table, 'G', 3, None, 1, game_r, {}) is None, 'ours refuses slot 3 without a text')
        self.report['saves'] = dict(cases=len(cases), equal=equal)

    # ---- 2. the load 0x40eaaa ------------------------------------------------------
    def loads(self, corpus):
        cd_table = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        flagged = table([f'{k + 1:2d}. X' for k in range(12)], flags=(3,))
        equal = 0
        for label, sav in corpus.items():
            if not self.mine(f'load {label}'):
                continue
            disk = {'sav' + B + 'gamefile.str': flagged, 'sav' + B + 'GAME_3.SAV': sav, **DATA}
            disk.update({'sav' + B + (c % 3): b'slot ' + d.encode() for c, d in COMPANIONS})
            theirs = self.run(disk, ['load:3:0'], emit=True)
            ok = (theirs['metadata']['loaded'] == 1 and theirs['emitted'] == self.loaded(sav) and
                  all(theirs['disk']['data' + B + d] == b'slot ' + d.encode() for _, d in COMPANIONS))
            equal += self.check(ok, f'load {label}: SAV after the load or DATA copies differ')
            self.check(self.loaded(self.loaded(sav)) == sav[:len(self.loaded(sav))], f'load {label}: not an involution')
        # A slot without its flag loads nothing (CONTINUE on a fresh table).
        if self.mine('load of slot 10 without its flag'):
            theirs = self.run({'sav' + B + 'gamefile.str': cd_table, 'sav' + B + 'GAME_R.SAV': corpus['GAME_R'], **DATA},
                              ['load:10:1'])
            self.check(theirs['metadata']['loaded'] == 0 and
                       theirs['disk']['data' + B + 'script.dat'] == DATA['data' + B + 'script.dat'],
                       'load of slot 10 without its flag loaded something')
        self.report['loads'] = dict(cases=len(corpus), equal=equal)

    # ---- 3. the new career 0x40e8a5 -------------------------------------------------
    def new_careers(self):
        cases = [(71, 'TESTPILOT', 2, 'G'), (0, 'A', 0, 'E'), (127, 'x' * 29, 3, 'F'), (5, 'Hello World', 1, 'G'),
                 (64, 'Z1', 3, 'E'), (33, '^ 9', 0, 'G')]
        equal, cd_table = 0, (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        for n, (seed, name, rank, language) in enumerate(cases):
            owned = self.mine(f'new career {seed} {name!r} {rank} {language}')
            if not owned and n != len(cases) - 1:    # the last career is the corpus' own: ours runs everywhere
                continue
            rng = self.prep.initial_rng(self.cd, seed)
            sav, rng_after, salvage = self.backend.new_game_request(self.core, self.cd, LANGUAGES[language], name, rank,
                                                                    1, 0, rng)
            if not owned:
                continue
            theirs = self.run({'sav' + B + 'gamefile.str': cd_table}, [f'new:{rank}:{name}'], rng=rng, emit=True,
                              language=language)
            meta = theirs['metadata']
            ok = (theirs['emitted'] == sav and bytes.fromhex(meta['output_rng_state']) == rng_after and
                  meta['total'] == salvage and meta['final_word'] == 2 and meta['position'] == [1, 0] and
                  meta['loaded'] == 1 and [c[0] for c in meta['ui_calls']] == [0x413305])
            equal += self.check(ok, f'new career seed {seed} {name!r} rank {rank} {language}: differs from 0x40e8a5')
        self.report['new_careers'] = dict(cases=len(cases), equal=equal)
        return sav

    # ---- 4. the SAV after a preparation ------------------------------------------------
    def preparations(self, corpus):
        missions = [(g, p) for g, p, _ in self.prep.campaign(self.core, self.cd) if g >= 1]
        tables = self.prep.default_tables(self.cd)
        cases, equal = [], 0
        for label, sav in corpus.items():
            chosen = missions if label == 'new career' else missions[::7]
            cases += [(label, sav, g, p, seed) for g, p in chosen for seed in ((71,) if label != 'GAME_R' else (0, 71))]
        for label, sav, group, position, seed in cases:
            if not self.mine(f'prepared SAV {label} {group}:{position} seed {seed}'):
                continue
            rng = self.prep.initial_rng(self.cd, seed)
            request = self.prep.request(group=group, position=position, mode=1, language='GER', rng=rng,
                                        tables=tables, save=sav, prepared_save=True)
            mine, = self.prep.prepare_batch(self.core, self.cd, [request])
            # The worker starts from a restored file: give it the file whose
            # restore is the state `sav` describes.
            state = self.tmp / f'state-{len(cases)}.sav'
            state.write_bytes(self.loaded(sav))
            theirs, _ = self.shellprep.generate(self.worker, ['--cd-root', self.cd, '--save', state,
                                                              '--campaign', f'{group}:{position}', '--mode', 1,
                                                              '--seed', seed, '--language', 'G', '--emit-save-after'])
            ok = ('error' not in mine and mine.get('prepared_save') == theirs['output.sav'] and
                  all(mine[n] == theirs[n] for n in self.shellprep.NAMES))
            equal += self.check(ok, f'prepared SAV {label} {group}:{position} seed {seed}: differs from the original')
        self.report['preparations'] = dict(cases=len(cases), equal=equal)

    def main(self):
        game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        game_t = self.pins.require(self.cd, 'SAV/GAME_T.SAV')[0]
        self.saves(game_r)
        career = self.new_careers()
        # A SAV with distinct records inside one item list (the results path
        # adds damaged items): the results path of C1_01 from GAME_R with
        # the recorded c1_01-success output, when that output is present.
        corpus = {'GAME_R': game_r, 'GAME_T': game_t, 'new career': career}
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        if (golden / 'results.dat').is_file():
            chain = load(Path(self.backend.__file__).parent / 'es2chain.py', 'slots_chain')
            engine = chain.Engine(self.core, self.cd)
            out = engine.advance(game_r, (golden / 'mission.var').read_bytes(), (golden / 'results.dat').read_bytes(),
                                 self.prep.initial_rng(self.cd, 71), 'GER', 0, 0)
            corpus['after C1_01'] = out['sav']
            self.check(self.loaded(out['sav']) != out['sav'], 'the results SAV should have ordered item lists')
        self.loads(corpus)
        self.preparations(corpus)
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
        Path(out, 'slots-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    shard.finish('slots')
    print(json.dumps(dict(report=test.report, failures=test.failures), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
