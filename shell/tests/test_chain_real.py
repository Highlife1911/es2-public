# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Real-data test of the campaign chain; skipped without ES2_DATA_ROOT.

Read only: the extracted CD, the recorded oracle outputs (golden suites and
regression runs), the sealed launch sets and the pinned GAME_R.SAV. Nothing
is written outside the test's temporary directory; no game process starts.

1. Recorded suites: for every distinct results.dat/mission.var pair a suite
   produced (oracle/golden/<suite>/output and logs/oracle-parallel-*/<suite>/
   run/<suite>-output), starting from the SAV and RNG the suite's launch files
   were prepared from: our preparation equals those launch files; our results
   path and the original one (VSHELL 0x40f09f in the mapped executable,
   es2shellprep --results-batch) agree on the SAV, position, path, final word,
   debrief and RNG; where the chain continues, our preparation of the next
   mission and the original one (es2shellprep --batch, from the original's SAV
   and RNG) agree on all four files, RNG and reset selectors.
2. Launcher end to end: es2launch.exe with ES2_LAUNCH_SHELL = es2chain step in
   a temporary game directory whose dbsim.EXE is a stand-in that copies the
   recorded c1_01-success output: C1_01 (pinned files) -> DBSIM -> results ->
   C1_02, mission limit 1, DATA restored.
3. Whole campaign: the same chain with synthetic DBSIM results (outcome 1)
   from C1_01 to the end of group 5; afterwards every step is replayed
   through the original results path and preparation and compared.
4. A from_slot campaign as the state engine logs it when slot 10 has
   its copies (the timing clones): built with our library from GAME_R.SAV
   and the recorded c1_01-success output, replayed like a live campaign
   (results step from results_rng, C1_02's preparation, the launched
   mission.var/player.mec against the original 0x40f68c); a record without
   results_rng and a wrong launched player.mec are reported, not raised.

Arguments: es2chain.py es2prep_core es2shellprep es2launch stand_in_child
chain_fake_dbsim.py sweep_compare.py reference_pins.py slots_oracle.py
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

SUITES = ('c1_01-success', 'c3_09-loadout-b', 'c3_10-loadout-hercs', 'c4_08-razor-plasma')
PINNED_C1_01 = {name: 'DATA/' + name for name in ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec')}
GAME_R = 'SAV/GAME_R.SAV'
GAME_R_SEED = 71   # vshell_preparation.md: GAME_R + 71 draws reproduces the pinned C1_01 files
LETTER = dict(ENG='E', FRE='F', GER='G')


def scratch_base():
    """TEST_TMPDIR, or under Bazel the output base: the execroot carries a
    .git link, and the chain refuses directories inside repositories."""
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---- The original results path (es2shellprep --results-batch, ES2R packets) ----
def original_results(worker, cd, language, requests):
    """requests: (sav, bank, results, rng) tuples, mode 1, options 0/0."""
    text = ''.join(f'1 0 0 -1 -1\n{s.hex()}\n{b.hex()}\n{r.hex()}\n{g.hex()}\n' for s, b, r, g in requests)
    run = subprocess.run([str(worker), '--cd-root', str(cd), '--language', LETTER[language], '--results-batch'],
                         input=text.encode('ascii'), capture_output=True, timeout=600)
    if run.returncode:
        raise AssertionError('original results path failed: ' + run.stderr.decode(errors='replace')[-400:])
    data, at, rows = run.stdout, 0, []
    while at < len(data):
        assert data[at:at + 4] == b'ES2R', 'bad ES2R packet'
        at += 4
        blocks = []
        for _ in range(4):
            size, = struct.unpack_from('<I', data, at)
            blocks.append(data[at + 4:at + 4 + size])
            at += 4 + size
        meta = json.loads(blocks[0])
        calls = [c[:2] for c in meta['ui_calls']]
        addresses = [c[0] for c in calls]
        # Return path from the trapped UI entries (shell_invitro.md, Results path).
        if [0x44d0e7, 3] in calls:
            ending, advance = 3, -1
        elif 0x40f904 not in addresses:
            ending, advance = 4, -1
        else:
            ending = 5
            advance = (1 if 0x40e933 in addresses else 0 if [0x44d0e7, 0] in calls
                       else 2 if 0x413305 in addresses else -1)
        rows.append(dict(ending=ending, advance=advance, group=meta['after'][0], position=meta['after'][1],
                         final_word=meta['final_word'], sav=blocks[1], strings=blocks[2], text=blocks[3],
                         debrief=meta['debrief_ran'], rng=bytes.fromhex(meta['output_rng_state'])))
    assert len(rows) == len(requests), 'original results: packet count'
    return rows


def describe_bays(core, cd, sav):
    """(player bay, 36 roster bays, hangar bay indices) from es2prep_core --describe-save."""
    run = subprocess.run([str(core), str(cd), '--describe-save'], input=sav.hex() + '\n',
                         capture_output=True, text=True, check=True)
    roster, player, bays = [], None, []
    for line in run.stdout.splitlines()[1:]:
        kind, *rest = line.split()
        if kind == 'PILOT':
            roster.append(int(rest[3]))
        elif kind == 'PLAYER':
            player = int(rest[3])
        elif kind == 'BAY':
            bays.append(int(rest[0]))
    return player, roster, bays


def compare_results(ours, theirs):
    names = ['ending', 'advance', 'group', 'position', 'final_word', 'sav', 'rng', 'text']
    if theirs['debrief']:
        names.append('strings')
    return [n for n in names if ours[n] != theirs[n]]


class Real:
    def __init__(self, argv):
        (self.chain_py, self.core, self.worker, self.launcher, self.stand_in, self.fake,
         sweep_py, pins_py, oracle_py) = [Path(a).resolve() for a in argv]
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.data_root = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1])
        self.chain = load(self.chain_py, 'chain_real')
        self.prep = self.chain.prep
        self.sweep = load(sweep_py, 'chain_sweep')
        self.pins = load(pins_py, 'chain_pins')
        self.oracle = load(oracle_py, 'chain_slots_oracle')
        self.engine = self.chain.Engine(self.core, self.cd)
        self.scratch = tempfile.TemporaryDirectory(prefix='es2chain-real-', dir=scratch_base())
        self.tmp = Path(self.scratch.name)
        self.report = dict(suites=[], launcher=None, campaign=None)
        self.failures = []

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            if not getattr(self, 'expecting_failures', False):
                print('FAIL', message, flush=True)

    # ---- inputs ----------------------------------------------------------------
    def start(self, suite):
        """(SAV, seed, language, expected first launch files {name: sha256})."""
        if suite == 'c1_01-success':
            sav, _ = self.pins.require(self.cd, GAME_R)
            return sav, GAME_R_SEED, 'GER', {n: self.pins.PINS[p][1] for n, p in PINNED_C1_01.items()}
        directory = self.data_root / 'oracle' / 'launch' / suite
        manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
        provenance = manifest['provenance']
        sav = (directory / 'loadout.sav').read_bytes()
        assert self.prep.sha(sav) == provenance['generated_sav_sha256'], f'{suite}: loadout.sav changed'
        return sav, provenance['seed'], provenance['language'], {f['file']: f['sha256'] for f in manifest['files']}

    def recorded(self, suite):
        """Distinct (results, mission.var, sources) the suite's DBSIM runs returned."""
        directories = [self.data_root / 'oracle' / 'golden' / suite / 'output']
        directories += sorted(self.data_root.glob(f'logs/oracle-parallel-*/{suite}/run/{suite}-output'))
        pairs = {}
        for directory in directories:
            try:
                results, bank = (directory / 'results.dat').read_bytes(), (directory / 'mission.var').read_bytes()
            except OSError:
                continue
            key = (self.prep.sha(results), self.prep.sha(bank))
            pairs.setdefault(key, [results, bank, []])[2].append(str(directory.relative_to(self.data_root)))
        return list(pairs.values())

    # ---- 1: recorded suites ----------------------------------------------------
    def suites(self):
        expected_next = None
        for suite in SUITES:
            sav, seed, language, first_files = self.start(suite)
            group, position = self.engine.position(sav)
            first = self.engine.prepare(sav, group, position, self.prep.initial_rng(self.cd, seed), bytes(60), language)
            assert 'error' not in first, f'{suite}: {first.get("error")}'
            self.check({n: self.prep.sha(first[n]) for n in first_files} == first_files,
                       f'{suite}: our preparation differs from the launch files DBSIM ran')
            pairs = self.recorded(suite)
            self.check(bool(pairs), f'{suite}: no recorded DBSIM output found')
            ours = [self.engine.advance(sav, bank, results, first['rng'], language, 0, 0) for results, bank, _ in pairs]
            theirs = original_results(self.worker, self.cd, language,
                                      [(sav, bank, results, first['rng']) for results, bank, _ in pairs])
            for (results, bank, sources), a, b in zip(pairs, ours, theirs):
                row = dict(suite=suite, sources=sources, start=[group, position],
                           results_sha256=self.prep.sha(results), mission_var_sha256=self.prep.sha(bank))
                self.check('error' not in a, f'{suite}: our results path rejected {a.get("error")}')
                if 'error' in a:
                    continue
                differences = compare_results(a, b)
                self.check(not differences, f'{suite} {sources[0]}: results path differs in {differences}')
                reason = self.chain.continuation(a, 1, 1)
                row.update(ending=a['ending'], advance=a['advance'], final_word=a['final_word'],
                           chain=reason, next=[a['group'], a['position']], results_equal=not differences,
                           save_sha256=self.prep.sha(a['sav']))
                if reason == 'stop-mission-limit':   # the chain prepares the next mission
                    mine = self.engine.prepare(a['sav'], a['group'], a['position'], a['rng'], first['selectors'], language)
                    request = self.prep.request(group=b['group'], position=b['position'], mode=1, language=language,
                                                rng=b['rng'], selectors=first['selectors'],
                                                tables=self.engine.tables(), save=b['sav'])
                    original, = self.sweep.original_batch(self.worker, self.cd, language, [request])
                    names = (*self.prep.FILES, 'rng', 'selectors')
                    differing = [n for n in names if 'error' in mine or 'error' in original or mine[n] != original[n]]
                    self.check(not differing, f'{suite} {sources[0]}: next mission differs in {differing}')
                    row['next_mission'] = self.engine.mission(a['group'], a['position'])
                    row['next_files'] = {n: dict(size=len(mine[n]), sha256=self.prep.sha(mine[n])) for n in self.prep.FILES}
                    row['next_equal'] = not differing
                    if suite == 'c1_01-success' and sources[0].startswith('oracle'):
                        expected_next = row
                self.report['suites'].append(row)
                print(json.dumps({k: row[k] for k in ('suite', 'chain', 'next') if k in row}), flush=True)
        # Facts of the current recordings (a re-recorded golden changes them).
        golden = {r['suite']: r for r in self.report['suites'] if r['sources'][0].startswith('oracle')}
        self.check(golden['c1_01-success']['chain'] == 'stop-mission-limit' and
                   golden['c1_01-success'].get('next_mission') == 'C1_02', 'c1_01-success golden: expected C1_02 next')
        for suite in SUITES[1:]:
            self.check(golden[suite]['chain'] == 'stop-player-lost', f'{suite} golden: expected player lost')
        return expected_next

    # ---- 2/3: through the launcher ---------------------------------------------
    def game(self, name, delegate):
        game = self.tmp / name
        data = game / 'DATA'
        data.mkdir(parents=True)
        (data / 'language.cfg').write_bytes(b'G')
        for file in ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec', 'results.dat'):
            (data / file).write_bytes(b'placeholder ' + file.encode())
        # Own synthetic error.str: group 0 with three strings, group 1 with one.
        body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
        body += struct.pack('<hh', 1, 2) + b'c\0\0'
        (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
        shutil.copyfile(self.stand_in, game / 'dbsim.EXE')
        (game / 'dbsim.delegate').write_text(delegate + '\n', encoding='ascii')
        return game

    def run_chain(self, name, missions, recorded=None, extra=()):
        state = self.tmp / f'{name}-chain'
        fake = f'"{sys.executable}" -B "{self.fake}" --chain "{state}"'
        if recorded:
            fake += f' --recorded "{recorded[0]}" "{recorded[1]}"'
        game = self.game(name, fake)
        save = self.tmp / f'{name}-GAME_R.SAV'
        save.write_bytes(self.pins.require(self.cd, GAME_R)[0])
        init = subprocess.run([sys.executable, '-B', str(self.chain_py), 'init', '--core', str(self.core),
                               '--cd-root', str(self.cd), '--state', str(state), '--game-dir', str(game),
                               '--save', str(save), '--seed', str(GAME_R_SEED), '--missions', str(missions), *extra],
                              capture_output=True, text=True, check=True)
        shell = json.loads(init.stdout)['launch_shell']
        log = game / 'launch.log'
        env = dict(os.environ, ES2_LAUNCH_SHELL=shell, ES2_LAUNCH_LOG=str(log), ES2_LAUNCH_NO_UI='1')
        run = subprocess.run([str(self.launcher)], cwd=game, env=env, timeout=1500)
        self.check(run.returncode == 0, f'{name}: launcher exit {run.returncode}')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        record['finished'] = record['finished'] or dict(reason=None)
        lines =[line.split(' ', 1)[1] for line in log.read_text(encoding='ascii').splitlines()]
        for file in ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec', 'results.dat'):
            self.check((game / 'DATA' / file).read_bytes() == b'placeholder ' + file.encode(), f'{name}: DATA/{file} not restored')
        self.check(not (game / 'DATA' / 'EXIT.CFG').exists(), f'{name}: unexpected EXIT.CFG')
        return state, record, lines, shell

    def mission_files(self, state, index):
        return {n: (state / f'mission-{index:03d}' / n).read_bytes() for n in self.prep.FILES}

    def launcher_golden(self, expected_next):
        golden = self.data_root / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        state, record, lines, shell = self.run_chain('golden', 1, (golden / 'results.dat', golden / 'mission.var'))
        want = [f'start shell-replacement {shell} dummy -eggplant -X1 -G', 'exit shell-replacement 2',
                'start dbsim.EXE dummy -eggplant -X2 -R0 -G', 'exit dbsim.EXE 3',
                f'start shell-replacement {shell} dummy -eggplant -X3 -G', 'exit shell-replacement 0', 'end 0']
        self.check(lines[1:] == want, f'launcher log {lines}')
        self.check(record['finished']['reason'] == 'stop-mission-limit', f'golden chain: {record["finished"]}')
        first = self.mission_files(state, 0)
        self.check(all(self.pins.matches(PINNED_C1_01[n], first[n]) for n in first), 'golden chain: C1_01 differs from the pins')
        nxt = self.mission_files(state, 1)
        self.check(expected_next is not None and
                   {n: self.prep.sha(d) for n, d in nxt.items()} == {n: v['sha256'] for n, v in expected_next['next_files'].items()},
                   'golden chain: C1_02 differs from the in-vitro chain')
        replayed = self.replay(state, 'golden')
        self.check(replayed == (1, 2, 2), f'golden replay {replayed}')
        self.report['launcher'] = dict(log=lines[1:], finished=record['finished']['reason'], replay=replayed,
                                       missions=[m['mission'] for m in record['missions']],
                                       next_files={n: self.prep.sha(d) for n, d in nxt.items()})

    def replay(self, state, label):
        """Every step of a chain directory through the original: each results
        step from the recorded SAV, DBSIM files and RNG; each preparation
        (the first included) from the SAV and RNG the original hands over (or
        the chain's recorded start). Read only. Returns (results steps,
        preparations, equal preparations)."""
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        missions, language = record['missions'], record['language']
        played = []
        for m in missions:
            if not (m.get('advance') or {}).get('success'):
                continue
            # A menu campaign's results path starts from the RNG of its
            # -X3 process (reset + the start-up draws, results_rng); es2chain's
            # from the state its last preparation left (output_rng). A slot's
            # own copies (from_slot) were not prepared and have no output_rng:
            # their step can only be replayed from results_rng.
            if 'results_rng' in m:
                rng = m['results_rng']
            elif not m.get('from_slot') and 'output_rng' in m:
                rng = m['output_rng']
            else:
                self.check(False, f'{label} step {m["index"]}: no results_rng (the RNG its results path started from)')
                continue
            played.append((m, bytes.fromhex(rng)))
        # A menu campaign records the SAV its results path started from.
        requests = [((state / m.get('results_save', m['input_save'])).read_bytes(),
                     (state / f'mission-{m["index"]:03d}' / 'dbsim' / 'mission.var').read_bytes(),
                     (state / f'mission-{m["index"]:03d}' / 'dbsim' / 'results.dat').read_bytes(), rng)
                    for m, rng in played]
        played = [m for m, _ in played]
        theirs = original_results(self.worker, self.cd, language, requests) if requests else []
        handed = {}
        if 'input_rng' in missions[0]:
            handed[0] = ((state / missions[0]['input_save']).read_bytes(), bytes.fromhex(missions[0]['input_rng']))
        endings = {'player-lost': 3, 'other-mode': 4, 'advanced': 5}
        for m, b in zip(played, theirs):
            i = m['index']
            summary = m['advance']
            self.check(b['sav'] == (state / summary['save']['file']).read_bytes(),
                       f'{label} step {i}: SAV differs from the original')
            self.check((b['ending'], b['advance'], b['final_word'], [b['group'], b['position']]) ==
                       (endings[summary['ending']], summary['advance'], summary['final_word'],
                        [summary['next']['group'], summary['next']['position']]),
                       f'{label} step {i}: path/position differ from the original')
            if i + 1 < len(missions):
                nxt = missions[i + 1]
                self.check(b['rng'].hex() == nxt['input_rng'], f'{label} step {i}: RNG differs from the original')
                sav = b['sav'] if not nxt['hangar'] else self.chain.with_player_bay(b['sav'], nxt['hangar']['chosen'])
                handed[i + 1] = (sav, b['rng'])
        # Replayed missions and a slot's own copies were not prepared.
        prepared = [m for m in missions if 'replay_of' not in m and not m.get('from_slot')]
        preparation = [self.prep.request(group=m['group'], position=m['position'], mode=1, language=language,
                                         rng=handed[m['index']][1], selectors=bytes.fromhex(m['input_selectors']),
                                         tables=self.engine.tables(), save=handed[m['index']][0])
                       for m in prepared if m['index'] in handed]
        self.check(len(preparation) == len(prepared), f'{label}: a prepared mission has no original input')
        originals = self.sweep.original_batch(self.worker, self.cd, language, preparation) if preparation else []
        equal = 0
        for m, original in zip(prepared, originals):
            mine = self.mission_files(state, m['index'])
            recorded = {n: v['sha256'] for n, v in m['files'].items()}
            same = ('error' not in original and all(mine[n] == original[n] for n in self.prep.FILES) and
                    {n: self.prep.sha(mine[n]) for n in self.prep.FILES} == recorded and
                    original['rng'].hex() == m['output_rng'] and original['selectors'].hex() == m['output_selectors'])
            self.check(same, f'{label} mission {m["index"]} {m["mission"]}: preparation differs from the original')
            equal += same
        self.report.setdefault('bay', {})[label] = self.bay_steps(state, record, label)
        self.report.setdefault('slot_launch', {})[label] = self.slot_launches(state, record, label)
        # The hangar policy's SAV walker agrees with the formats:sav reader on every SAV.
        for path in sorted(state.glob('save-*.sav')):
            sav = path.read_bytes()
            _, player_bay, roster, bays = self.chain.sav_layout(sav)
            self.check((player_bay, roster, bays) == describe_bays(self.core, self.cd, sav) and
                       self.chain.with_player_bay(sav, player_bay) == sav,
                       f'{path.name}: SAV walker differs from formats:sav')
        return len(theirs), len(originals), equal

    def memory(self, sav):
        """The file whose restore by the original readers gives the state
        `sav` describes (es2_shell_loaded_save, an involution)."""
        lines = subprocess.run([str(self.core), str(self.cd), '--loaded-save'], input=sav.hex() + '\n',
                               capture_output=True, text=True, check=True).stdout.splitlines()
        return bytes.fromhex(lines[1])

    def bay_steps(self, state, record, label):
        """The service-bay steps of a menu campaign (mission `bay`
        records) through the original: each run of consecutive steps from its
        first input SAV (applied clicks and mounts as 0x411b10, guidance
        0x43e346, rebalance 0x412a37) must give the last output SAV, and the
        launch after them the launch files of 0x40f68c. Returns the counts."""
        gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        runs = steps = 0
        for m in record['missions']:
            groups = []
            for step in m.get('bay', []):
                steps += 1
                if groups and groups[-1][-1]['output'] == step['input']:
                    groups[-1].append(step)
                else:
                    groups.append([step])
            for group in groups:
                ops = []
                for step in group:
                    kind, bay, slot, value, _ = step['op']
                    if kind in (1, 2) and step['applied']:
                        ops.append(f'mount:{bay}:{slot}:{value}')
                    elif kind == 3:
                        ops.append(f'guidance:{bay}:{slot}:{value}')
                    elif kind == 4:
                        ops.append(f'rebalance:{value}')
                launch = 'launch_files' in m and m.get('launch_save') == group[-1]['output']
                start, end = (state / group[0]['input']).read_bytes(), (state / group[-1]['output']).read_bytes()
                if not ops:
                    self.check(start == end, f'{label} mission {m["index"]}: a service-bay step without effect changed the SAV')
                    continue
                runs += 1
                result = self.oracle.run(self.worker, self.cd, {'sav\\gamefile.str': gamefile},
                                         ops + (['launch'] if launch else []), self.tmp / f'bay-{label}-{runs}',
                                         save=self.memory(start), emit=True)
                self.check(result['emitted'] == end,
                           f'{label} mission {m["index"]}: service-bay SAV differs from the original ({ops})')
                if launch:
                    theirs = {k.lower(): v for k, v in result['disk'].items()}
                    directory = state / f'mission-{m["index"]:03d}' / 'launch'
                    self.check(all((directory / n).read_bytes() == theirs['data\\' + n.lower()]
                                   for n in ('player.mec', 'mission.var')),
                               f'{label} mission {m["index"]}: launch files differ from the original 0x40f68c')
        return dict(steps=steps, original_runs=runs)

    def slot_launches(self, state, record, label):
        """A slot's own copies (from_slot) were not prepared;
        what Rock & Roll handed DBSIM is the copies plus the mission.var and
        player.mec of 0x40f68c for the loaded state. Without service-bay
        steps (bay_steps covers those) the launch of the original on the
        loaded state must give the recorded launch_files (older logs: the
        record's files). Returns the number of launches compared."""
        compared = 0
        for m in record['missions']:
            if not m.get('from_slot') or m.get('bay') or not m.get('launched'):
                continue
            group = 'launch_files' if 'launch_files' in m else 'files'
            loaded = (state / m['input_save']).read_bytes()
            result = self.oracle.run(self.worker, self.cd, {'sav\\gamefile.str': (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()},
                                     ['launch'], self.tmp / f'slot-launch-{label}-{m["index"]}', save=self.memory(loaded))
            theirs = {k.lower(): v for k, v in result['disk'].items()}
            for n in ('mission.var', 'player.mec'):
                data = theirs.get('data\\' + n.lower())
                self.check(data is not None and self.prep.sha(data) == m[group][n]['sha256'],
                           f'{label} mission {m["index"]}: {group}.{n} of the slot\'s copies differs from the original 0x40f68c')
            if group == 'launch_files':
                directory = state / f'mission-{m["index"]:03d}'
                self.check(all((directory / 'launch' / n).read_bytes() == (directory / n).read_bytes()
                               for n in ('script.dat', 'MISSION.STR')),
                           f'{label} mission {m["index"]}: the launched script/text are not the slot\'s copies')
            compared += 1
        return compared

    def from_slot_campaign(self, expected_next):
        """The campaign the state engine logs when slot 10 has its copies
        (a played installation such as the timing clones: GAME_R.SAV with the
        C1_01 files): missions[0] is a from_slot record (no preparation, no
        output_rng, results_rng = reset + 71 draws), DBSIM returns the
        c1_01-success output, C1_02 is prepared. Built here with our own
        library (memory image, 0x40f68c launch files, results path,
        preparation) in the layout engine_io.c writes, then replayed through
        the original like a live campaign; a record without results_rng and a
        wrong launched player.mec must be reported, not crash the verify."""
        golden = self.data_root / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        state = self.tmp / 'from-slot-chain'
        (state / 'mission-000' / 'dbsim').mkdir(parents=True)
        (state / 'mission-000' / 'launch').mkdir()
        (state / 'mission-001').mkdir()
        game_r = self.pins.require(self.cd, GAME_R)[0]
        loaded = self.memory(game_r)
        (state / 'save-000.sav').write_bytes(loaded)
        rng = self.prep.initial_rng(self.cd, GAME_R_SEED)
        copies = self.engine.prepare(game_r, 1, 0, rng, bytes(60), 'GER')   # the C1_01 files the slot holds
        lines = subprocess.run([str(self.core), str(self.cd), '--launch-files'], input=loaded.hex() + '\n',
                               capture_output=True, text=True, check=True).stdout.splitlines()
        self.check(lines[:1] == ['OK'], f'from_slot: launch files {lines[:1]}')
        launched = dict(copies, **{'player.mec': bytes.fromhex(lines[1]), 'mission.var': bytes.fromhex(lines[2])})
        files = lambda d: {n: dict(size=len(d[n]), sha256=self.prep.sha(d[n])) for n in self.prep.FILES}
        for n in self.prep.FILES:
            (state / 'mission-000' / n).write_bytes(copies[n])
            (state / 'mission-000' / 'launch' / n).write_bytes(launched[n])
        self.check(all(self.pins.matches(PINNED_C1_01[n], launched[n]) for n in self.prep.FILES),
                   'from_slot: the launched C1_01 files differ from the pins')
        bank, results = (golden / 'mission.var').read_bytes(), (golden / 'results.dat').read_bytes()
        (state / 'mission-000' / 'dbsim' / 'mission.var').write_bytes(bank)
        (state / 'mission-000' / 'dbsim' / 'results.dat').write_bytes(results)
        out = self.engine.advance(loaded, bank, results, rng, 'GER', 0, 0)
        summary = self.engine.summary(out, 'GER')
        summary['save']['file'] = 'save-001.sav'
        (state / 'save-001.sav').write_bytes(out['sav'])
        nxt = self.engine.prepare(out['sav'], out['group'], out['position'], out['rng'], bytes(60), 'GER')
        for n in self.prep.FILES:
            (state / 'mission-001' / n).write_bytes(nxt[n])
        # The -X3 process starts from reset + 71 draws, not from the RNG
        # es2chain carries: script.dat and mission.var are the reset ones
        # (launcher.md), the text and the MEC those of the in-vitro chain.
        want = expected_next['next_files'] if expected_next else {}
        self.check(all(files(nxt)[n] == want.get(n) for n in ('MISSION.STR', 'player.mec')) and
                   files(nxt)['script.dat']['sha256'].startswith('7d5722af') and
                   files(nxt)['mission.var']['sha256'].startswith('ad22e2ba'),
                   f'from_slot: C1_02 differs from the reset-RNG preparation ({files(nxt)})')
        first = dict(index=0, group=1, position=0, mission='C1_01', from_slot=True, input_save='save-000.sav', hangar=None,
                     files=files(copies), launch_files=files(launched), launch_save='save-000.sav', launched='synthetic',
                     results_save='save-000.sav', results_rng=rng.hex(), seed=GAME_R_SEED, advance=summary)
        second = dict(index=1, group=out['group'], position=out['position'], mission=self.engine.mission(out['group'], out['position']),
                      hangar=None, input_save='save-001.sav', input_rng=out['rng'].hex(), input_selectors=bytes(60).hex(),
                      files=files(nxt), output_rng=nxt['rng'].hex(), output_selectors=nxt['selectors'].hex(), launched=None)
        record = dict(schema=1, language='GER', missions=[first, second], save='save-001.sav',
                      menu=dict(source=dict(kind='slot', file='SAV\\GAME_R.SAV', companions=True)))
        write = lambda r: (state / 'chain.json').write_text(json.dumps(r, indent=1), encoding='utf-8')
        write(record)
        replayed = self.replay(state, 'from-slot')
        launches = self.report['slot_launch']['from-slot']
        self.check(replayed == (1, 1, 1) and launches == 1, f'from_slot replay {replayed}, launches {launches}')
        # The verify must report, not raise, on a played from_slot mission
        # without results_rng and on launched files the original would not write.
        negatives = []
        for name, change in (('no results_rng', lambda r: r['missions'][0].pop('results_rng')),
                             ('wrong launched player.mec',
                              lambda r: r['missions'][0]['launch_files']['player.mec'].update(sha256='00' * 32))):
            broken = json.loads(json.dumps(record))
            change(broken)
            write(broken)
            before = len(self.failures)
            self.expecting_failures = True
            try:
                self.replay(state, f'from-slot-{len(negatives)}')
            except Exception as error:   # the KeyError of the first live run
                self.failures.append(f'from_slot {name}: the verify raised {error!r}')
            finally:
                self.expecting_failures = False
            caught = self.failures[before:]
            del self.failures[before:]
            negatives.append(dict(case=name, reported=caught))
            self.check(bool(caught) and not any('raised' in c for c in caught), f'from_slot {name}: not reported ({caught})')
        write(record)
        self.report['from_slot'] = dict(replay=replayed, slot_launches=launches, next=second['mission'],
                                        save_001_sha256=self.prep.sha(out['sav']), negatives=negatives)
        print(json.dumps(dict(from_slot=self.report['from_slot']['replay'], negatives=[n['case'] for n in negatives
                                                                                       if n['reported']])), flush=True)

    def launcher_campaign(self):
        # C1_04..C1_06 fly the vehicle-8 Herc the (1,3) hook assigns; the (1,6)
        # hook removes it and leaves the player without a bay, where the VSHELL
        # hangar needs a choice: the chain's lowest-free policy decides.
        state, record, lines, _ = self.run_chain('campaign', 0, extra=('--hangar', 'lowest-free'))
        missions = record['missions']
        self.check(record['finished']['reason'] == 'stop-return-1', f'campaign chain: {record["finished"]}')
        self.check(record['completed'] == 50 and len(missions) == 50, f'campaign chain: {record["completed"]} missions')
        self.check(sum(line.startswith('start dbsim.EXE') for line in lines) == record['completed'], 'campaign: DBSIM starts')
        self.check(f'start dbsim.EXE dummy -eggplant -X2 -R{record["completed"] - 1} -G' in lines, 'campaign: -R counter')
        first = self.mission_files(state, 0)
        self.check(all(self.pins.matches(PINNED_C1_01[n], first[n]) for n in first), 'campaign: C1_01 differs from the pins')
        steps, preparations, equal = self.replay(state, 'campaign')
        self.check((steps, preparations, equal) == (50, 50, 50), f'campaign replay {steps}/{preparations}/{equal}')
        self.report['campaign'] = dict(finished=record['finished']['reason'], completed=record['completed'],
                                       hangar=[dict(mission=m['mission'], **m['hangar']) for m in missions if m['hangar']],
                                       missions=[m['mission'] for m in missions], results_steps=steps,
                                       preparations_equal=equal, preparations=preparations)
        print(json.dumps(self.report['campaign']), flush=True)

    def verify(self, directory):
        """ES2_CHAIN_VERIFY: replay an existing chain directory (e.g. the live run)."""
        steps, preparations, equal = self.replay(Path(directory), 'verify')
        self.report['verify'] = dict(directory=str(directory), results_steps=steps, preparations=preparations,
                                     preparations_equal=equal, bay=self.report['bay'].get('verify'),
                                     slot_launch=self.report['slot_launch'].get('verify'))
        print(json.dumps(self.report['verify']), flush=True)


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    real = Real(sys.argv[1:10])
    try:
        expected_next = real.suites()
        real.launcher_golden(expected_next)
        real.from_slot_campaign(expected_next)
        real.launcher_campaign()
        if os.environ.get('ES2_CHAIN_VERIFY'):
            real.verify(os.environ['ES2_CHAIN_VERIFY'])
    finally:
        real.scratch.cleanup()
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, 'chain-report.json').write_text(json.dumps(real.report, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(suites=len(real.report['suites']), failures=real.failures)))
    return int(bool(real.failures))


if __name__ == '__main__':
    raise SystemExit(main())
