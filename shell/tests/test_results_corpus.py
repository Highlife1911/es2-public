"""Real-data test of the results.dat corpus; skipped without ES2_DATA_ROOT.

Read only: the extracted CD, the recorded DBSIM outputs under es2-data
(oracle/golden and logs), the sealed launch sets (oracle/launch) and the
pinned GAME_R.SAV. Nothing is written outside the test's outputs directory; no
game process starts. The returned files are game output and stay in es2-data.

1. Corpus: every directory under oracle/golden and logs that holds a
   results.dat and a mission.var written by a suite run (snapshot DATA
   directories of play sessions excluded), de-duplicated by the SHA-256 of
   both files. Each pair is attributed to the launch set its run started
   from: the run's launch record (<stem>-launch.json next to <stem>-output)
   names the prepared script.dat, player.mec and mission.var hashes; golden
   outputs are attributed through the launch set their manifest names (the
   suite directory when it names none). A run that matches
   no launch set, and a tape playback (-p or -D in the launch record's
   arguments: the tape brings its own mission files), is reported and only
   parsed (records implied by the size), never compared. Equal pairs from
   different launch sets (a short run of two loadouts of one mission) are
   checked once per launch set.
2. Static contract (docs/formats/mission.md "Return results.dat",
   mission_runtime.md "results writer 42412c"): the grammar consumes the file
   exactly with N = the launch player.mec record count = the records the
   VSHELL results path reads from the start SAV; the flag is 0/1; the tally
   minus 25,000 times the returned variable word 20 is a value
   multiply_q10(2500, sum) can produce; salvage pairs carry an item ID 0..32
   and a percentage in 50..100; the per-actor words are the documented
   transform (0..100 observed); mission.var has 2,000 bytes.
3. Results path: each pair, from the start SAV and the RNG state our
   preparation of the launch set leaves (the preparation itself must equal
   the launch files), runs through es2campaign.py's results path and through
   the original VSHELL results path in the mapped executable (es2shellprep
   --results-batch, VSHELL 0x40f09f): mode 1 with repair options 0/1/2 and
   restock options 0/1, plus mode 0. Compared: SAV bytes, CAREER position,
   return path and value, final word, total, lost pilots, debrief record,
   MISSION.STR, debrief text, RNG state.

Arguments: es2campaign.py es2prep_core es2shellprep results_compare.py
reference_pins.py
Environment: ES2_RESULTS_CORPUS_REPORT=<file> also writes the report there.
"""
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys

GAME_R = 'SAV/GAME_R.SAV'
GAME_R_SEED = 71   # vshell_preparation.md: GAME_R + 71 draws reproduces the pinned C1_01 files
C1_01_PINS = {'script.dat': 'DATA/script.dat', 'MISSION.STR': 'DATA/MISSION.STR',
              'mission.var': 'DATA/mission.var', 'player.mec': 'DATA/player.mec'}
KEY_FILES = ('script.dat', 'player.mec', 'mission.var')
OPTIONS = [(1, repair, restock) for repair in (0, 1, 2) for restock in (0, 1)] + [(0, 0, 0)]
CLEARED_ON_READ = [10, 20] + list(range(21, 43))   # mission.md "mission.var": words DBSIM zeroes after reading
ENDINGS = {3: 'player-lost', 4: 'other-mode', 5: 'advanced'}


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(data):
    return hashlib.sha256(data).hexdigest()


def q10(a, b):
    """multiply_q10 47e0b0: (int32)((int64)a*b >> 10)."""
    value = (a * b) >> 10
    value &= 0xffffffff
    return value - (1 << 32) if value & 0x80000000 else value


def parse_results(data, records):
    """The writer grammar of 42412c with an implicit record count."""
    if len(data) < 8:
        raise ValueError('shorter than the 8-byte head')
    flag, tally, count = struct.unpack_from('<hIh', data)
    if count < 0:
        raise ValueError('negative pair count')
    at = 8 + 4 * count
    if len(data) < at:
        raise ValueError('truncated pair list')
    pairs = [struct.unpack_from('<hh', data, 8 + 4 * i) for i in range(count)]
    if records is None:
        records = (len(data) - at) // 72
    if len(data) != at + 72 * records:
        raise ValueError(f'{len(data) - at} record bytes for {records} records of 72')
    actors = []
    for k in range(records):
        words = struct.unpack_from('<36h', data, at + 72 * k)
        actors.append(dict(groups=list(words[:13]), parts=list(words[13:23]), combined=list(words[23:33]),
                           tail=list(words[33:])))
    return dict(flag=flag, tally=tally, pairs=[list(p) for p in pairs], actors=actors)


def tally_sum(tally, bank):
    """(sum, exact) with tally = multiply_q10(2500, sum) + 25000 * word 20."""
    rest = tally - 25000 * bank[20]
    rest = rest - (1 << 32) if rest >= 1 << 31 else rest
    s = -((-rest * 1024) // 2500)   # smallest sum whose product reaches rest
    return s, q10(2500, s) == rest


class Corpus:
    def __init__(self, argv):
        (self.campaign_py, self.core, self.worker, compare_py, pins_py) = [Path(a).resolve() for a in argv]
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.data_root = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1])
        self.campaign = load(self.campaign_py, 'corpus_campaign')
        self.prep = self.campaign.prep
        self.rc = load(compare_py, 'corpus_results_compare')
        self.rc.configure(self.campaign_py.with_name('es2prep.py'))
        self.pins = load(pins_py, 'corpus_pins')
        self.tables = self.prep.default_tables(self.cd)
        self.failures = []
        self.report = dict(starts={}, pairs=[], unattributed=[], cases=0, equal=0)

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)

    # ---- launch sets -------------------------------------------------------------
    def starts(self):
        """name -> SAV, seed, language, launch file hashes; keyed by the run's inputs."""
        starts = {}
        sav, _ = self.pins.require(self.cd, GAME_R)
        starts['c1_01-success'] = dict(sav=sav, seed=GAME_R_SEED, language='GER',
                                       files={n: self.pins.PINS[p][1] for n, p in C1_01_PINS.items()})
        for manifest_path in sorted((self.data_root / 'oracle' / 'launch').glob('*/manifest.json')):
            manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            provenance = manifest.get('provenance') or {}
            save = manifest_path.parent / (provenance.get('generated_sav_file') or 'loadout.sav')
            if not save.is_file():
                continue
            sav = save.read_bytes()
            if sha(sav) != provenance.get('generated_sav_sha256'):
                self.check(False, f'{manifest_path.parent.name}: loadout.sav changed')
                continue
            starts[manifest_path.parent.name] = dict(sav=sav, seed=provenance['seed'], language=provenance['language'],
                                                     files={f['file']: f['sha256'] for f in manifest['files']})
        return starts

    def attribute(self, directory, by_inputs):
        """Launch-set name of a returned output directory, or None."""
        name = directory.name
        if name.endswith('-output'):
            launch = directory.parent / (name[:-len('output')] + 'launch.json')
            if launch.is_file():
                record = json.loads(launch.read_text(encoding='utf-8-sig'))
                if any(str(a)[:2] in ('-p', '/p', '-D', '/D') for a in record.get('arguments') or []):
                    return None   # tape playback: the tape's own script.dat, player.mec and mission.var
                inputs = {Path(f['file']).name.lower(): f['sha256'].lower()
                          for f in record.get('prepared_mission_inputs') or []}
                key = tuple(inputs.get(n.lower()) for n in KEY_FILES)
                return by_inputs.get(key)
        parts = directory.relative_to(self.data_root).parts
        if parts[:2] == ('oracle', 'golden'):
            manifest = self.data_root / 'oracle' / 'golden' / parts[2] / 'manifest.json'
            launch_set = json.loads(manifest.read_text(encoding='utf-8-sig')).get('launch_set') if manifest.is_file() else None
            if isinstance(launch_set, dict):
                launch_set = launch_set.get('name')
            return launch_set or parts[2]   # several suites can start from one launch set
        return None

    def corpus(self, starts):
        by_inputs = {tuple(s['files'][n] for n in KEY_FILES): name for name, s in starts.items()}
        directories = sorted({p.parent for base in ('oracle/golden', 'logs')
                              for p in (self.data_root / base).rglob('results.dat')})
        pairs = collections.OrderedDict()
        for directory in directories:
            if directory.name.upper() == 'DATA':      # snapshot of a play session, not a suite run
                continue
            try:
                results, bank = (directory / 'results.dat').read_bytes(), (directory / 'mission.var').read_bytes()
            except OSError:
                continue
            source = directory.relative_to(self.data_root).as_posix()
            start = self.attribute(directory, by_inputs)
            entry = pairs.setdefault((sha(results), sha(bank), start), dict(results=results, bank=bank, sources=[], starts=set()))
            entry['sources'].append(source)
            entry['starts'].add(start)
        return list(pairs.values())

    # ---- checks ------------------------------------------------------------------
    def prepare(self, name, start):
        sav = start['sav']
        parsed = self.rc.Sav.parse(sav)
        group, position = struct.unpack_from('<hh', parsed.selection)
        request = self.prep.request(group=group, position=position, mode=1, language=start['language'],
                                    rng=self.prep.initial_rng(self.cd, start['seed']), selectors=bytes(60),
                                    tables=self.tables, save=sav)
        out, = self.prep.prepare_batch(self.core, self.cd, [request])
        self.check('error' not in out, f'{name}: preparation rejected {out.get("error")}')
        if 'error' in out:
            return None
        self.check({n: sha(out[n]) for n in start['files']} == start['files'],
                   f'{name}: our preparation differs from the launch files DBSIM ran')
        records, = struct.unpack_from('<h', out['player.mec'], 2)
        consumers = len(parsed.consumers())
        self.check(records == consumers, f'{name}: player.mec has {records} records, the results path reads {consumers}')
        start.update(rng=out['rng'], records=records, launch_bank=out['mission.var'], mission=(group, position))
        self.report['starts'][name] = dict(mission=self.prep_name(group, position), group=group, position=position,
                                           language=start['language'], records=records,
                                           sav_sha256=sha(sav), rng_sha256=sha(out['rng']))
        return start

    def prep_name(self, group, position):
        if not hasattr(self, '_campaign'):
            self._campaign = {(g, p): n for g, p, n in self.prep.campaign(self.core, self.cd)}
        return self._campaign.get((group, position))

    def contract(self, label, results, bank, start):
        try:
            parsed = parse_results(results, start['records'] if start else None)
        except ValueError as error:
            self.check(False, f'{label}: results.dat grammar: {error}')
            return None
        self.check(len(bank) == 2000, f'{label}: mission.var has {len(bank)} bytes')
        words = struct.unpack('<1000h', bank)
        self.check(parsed['flag'] in (0, 1), f'{label}: flag {parsed["flag"]}')
        total, exact = tally_sum(parsed['tally'], words)
        self.check(exact, f'{label}: tally {parsed["tally"]} is no multiply_q10(2500, sum) + 25000 * word 20')
        for item, percent in parsed['pairs']:
            self.check(0 <= item <= 32 and 50 <= percent <= 100, f'{label}: salvage pair {(item, percent)}')
        for actor in parsed['actors']:
            values = actor['groups'] + actor['parts'] + actor['combined']
            self.check(all(0 <= v <= 100 for v in values), f'{label}: condition words outside 0..100')
        parsed['score_sum'] = total
        parsed['bank_nonzero'] = {i: w for i, w in enumerate(words) if w}
        if start:
            launch = struct.unpack('<1000h', start['launch_bank'])
            parsed['bank_changed'] = {i: [launch[i], words[i]] for i in range(1000) if launch[i] != words[i]}
            parsed['cleared_on_read_nonzero'] = {i: words[i] for i in CLEARED_ON_READ if words[i]}
        return parsed

    def results_paths(self, label, results, bank, start):
        cases = [(mode, repair, restock) for mode, repair, restock in OPTIONS]
        requests = [dict(mode=m, repair=r, restock=s, sav=start['sav'], bank=bank, results=results, rng=start['rng'])
                    for m, r, s in cases]
        theirs = self.rc.original_batch(self.worker, self.cd, start['language'], requests)
        rows = []
        before = self.rc.Sav.parse(start['sav'])
        for (mode, repair, restock), b in zip(cases, theirs):
            a = self.campaign.advance(self.core, self.cd, start['sav'], bank, results, language=start['language'],
                                      mode=mode, repair=repair, restock=restock, rng=start['rng'])
            row = self.rc.compare(a, b)
            self.report['cases'] += 1
            self.report['equal'] += bool(row['equal'])
            self.check(row['equal'], f'{label} mode {mode} repair {repair} restock {restock}: '
                                     f'{row.get("differences") or row.get("own_error") or row.get("original_error")}')
            entry = dict(mode=mode, repair=repair, restock=restock, equal=row['equal'])
            if 'error' not in a and 'error' not in b:
                entry.update(ending=ENDINGS.get(b['ending'], b['ending']), advance=b['advance'],
                             next=[b['group'], b['position']], final_word=b['final_word'], lost=b['lost'],
                             total=b['total'], debrief=bool(b['debrief']), sav_sha256=sha(b['sav']),
                             rng_sha256=sha(b['rng']), text_size=len(b['text']), text_sha256=sha(b['text']),
                             effects=self.rc.effects(before, self.rc.Sav.parse(b['sav'])))
                if b['debrief']:
                    entry.update(debrief_count=b['debrief_count'], debrief_aux=b['debrief_aux'],
                                 debrief_refs=[r for r in b['debrief_refs'] if r != -1],
                                 strings_sha256=sha(b['strings']))
            rows.append(entry)
        return rows

    def run(self):
        starts = self.starts()
        corpus = self.corpus(starts)
        self.check(bool(corpus), 'no recorded results.dat found')
        prepared = {}
        for entry in corpus:
            names = sorted(n for n in entry['starts'] if n)
            label = f'{entry["sources"][0]} ({sha(entry["results"])[:12]}/{sha(entry["bank"])[:12]})'
            row = dict(results_sha256=sha(entry['results']), mission_var_sha256=sha(entry['bank']),
                       size=len(entry['results']), sources=len(entry['sources']), first_source=entry['sources'][0],
                       golden=[s for s in entry['sources'] if s.startswith('oracle/golden/')], starts=names)
            self.check(len(names) <= 1, f'{label}: attributed to several launch sets {names}')
            if None in entry['starts'] or not names:
                self.report['unattributed'] += [s for s in entry['sources']]
            start = None
            if len(names) == 1:
                name = names[0]
                if name not in prepared:
                    prepared[name] = self.prepare(name, starts[name]) if name in starts else None
                start = prepared[name]
                self.check(start is not None, f'{label}: no usable start {name}')
            parsed = self.contract(label, entry['results'], entry['bank'], start)
            if parsed:
                row['contract'] = {k: v for k, v in parsed.items() if k != 'bank_nonzero'}
                row['bank_nonzero'] = parsed['bank_nonzero']
            if parsed and start:
                row['paths'] = self.results_paths(label, entry['results'], entry['bank'], start)
            self.report['pairs'].append(row)
            print(json.dumps(dict(pair=label, start=names, flag=parsed and parsed['flag'],
                                  equal=[p['equal'] for p in row.get('paths', [])])), flush=True)
        if self.report['unattributed']:   # parsed with the record count the size implies; no results path
            print('NOTE unattributed runs:', self.report['unattributed'], flush=True)
        golden = {s.split('/')[2] for row in self.report['pairs'] for s in row['golden']}
        for suite in ('c1_01-success', 'c3_09-loadout-b', 'c3_10-loadout-hercs', 'c4_08-razor-plasma'):
            self.check(suite in golden, f'{suite}: golden output missing from the corpus')


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    corpus = Corpus(sys.argv[1:6])
    corpus.run()
    text = json.dumps(corpus.report, indent=1, sort_keys=True) + '\n'
    for target in (os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR'), os.environ.get('ES2_RESULTS_CORPUS_REPORT')):
        if target:
            path = Path(target)
            if path.is_dir():
                path = path / 'results-corpus.json'
            path.write_bytes(text.encode('utf-8'))
    print(json.dumps(dict(pairs=len(corpus.report['pairs']), cases=corpus.report['cases'],
                          equal=corpus.report['equal'], failures=corpus.failures)))
    return int(bool(corpus.failures))


if __name__ == '__main__':
    raise SystemExit(main())
