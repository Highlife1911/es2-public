# window-lint: allow-file inherits console: developer command line tool (es2menu starts no Python): runs in the console of its caller or in a Bazel test; the children share that console
"""Campaign chain without VSHELL menus: es2prep, DBSIM, es2campaign.

Standard library only. Our launcher (es2launch.exe) starts this program
instead of VSHELL when ES2_LAUNCH_SHELL names it; the launcher appends the
VSHELL arguments (`dummy -eggplant -X<state> [-s] [-a] [-@] [-<language>]`)
and keeps its own loop: exit code 2 starts DBSIM, 0 ends the launcher.

    es2chain.py init --core CORE --cd-root CD --state NEW_DIR --game-dir CLONE --save SAV ...
    es2chain.py step --state DIR <VSHELL arguments>      (run by the launcher)
    es2chain.py status --state DIR
    es2chain.py restore --state DIR

Per launcher state (docs/reverse-engineering/launcher.md, "campaign chain
through the own launcher"):

* -X1 with no mission pending: prepare the mission at the SAV's CAREER
  position (es2prep library, our SAV restore), write the four launch files into
  the game's DATA directory and return 2.
* -X3/-X4 (DBSIM ended a mission): read the DATA/results.dat and mission.var
  DBSIM wrote, run the VSHELL results path (es2campaign library), keep the new
  SAV, prepare the next CAREER mission and return 2; or stop (return 0) for the
  results path's other endings, the mission limit and every error.
* -X1 with a mission pending (DBSIM failed or was ended), -X6 (demo) and any
  other state: stop and return 0. Measurement runs may pass
  --results-on-exit-1 to read a results.dat DBSIM wrote before exiting with 1.

The RNG state and the thirty reset selectors are carried from step to step,
like VSHELL's memory. Where VSHELL would wait for a hangar decision (the
player has no Herc, e.g. after the (1,6) hook removed vehicle 8) the chain
stops, or with --hangar lowest-free gives the player the lowest free bay; that
is a chain policy, recorded per mission, not VSHELL behaviour. All state lives
in the chain directory (chain.json, the SAV after every mission, each
mission's launch files and DBSIM's two result files). The game's SAV directory
is never written. DATA files the chain replaces are backed up on the first
write and restored when the chain stops (unless --keep-data). The primary
installation native-r111 is refused.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback

HERE = Path(__file__).resolve().parent
SCHEMA = 1
LAUNCH_FILES = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec')
# The files a mission run changes in DATA (same set the oracle runner restores).
TRANSACTION_FILES = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec', 'results.dat', 'EXIT.CFG')
# ES.EXE passes '-' plus the language.cfg byte; 'E' (English) passes nothing.
LANGUAGE_LETTERS = {'G': 'GER', 'F': 'FRE'}
PRIMARY_INSTALLATION = 'native-r111'
CONTINUE = 'continue'


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prep = _module(HERE / 'es2prep.py', 'es2prep_for_chain')
campaign = _module(HERE / 'es2campaign.py', 'es2campaign_for_chain')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def now():
    return datetime.now(timezone.utc).isoformat()


# ---- Decision logic (pure; synthetic unit tests in shell/tests/test_chain.py) ----
def parse_shell_arguments(argv):
    """The launcher's VSHELL argv -> (state, language, flags). Raises ValueError."""
    state, language, flags = None, 'ENG', []
    for token in argv:
        if token in ('dummy', '-eggplant'):
            continue
        if token.startswith('-X'):
            if state is not None:
                raise ValueError('more than one -X argument')
            state = int(token[2:])
        elif token in ('-s', '-a', '-@'):
            flags.append(token)
        elif len(token) == 2 and token[0] == '-' and token[1] in LANGUAGE_LETTERS:
            language = LANGUAGE_LETTERS[token[1]]
        else:
            raise ValueError(f'unexpected shell argument {token!r}')
    if state is None:
        raise ValueError('no -X argument')
    return state, language, flags


def decide(chain, state):
    """What VSHELL would do on this start, restricted to the chain."""
    if chain.get('finished'):
        return 'stop-finished'
    pending = chain.get('pending') is not None
    if state == 1:
        # DBSIM's 1 (fatal error, or ended from outside) makes VSHELL start
        # fresh without reading results. --results-on-exit-1 (measurement
        # runs only) reads a results.dat DBSIM wrote before such an exit.
        if not pending:
            return 'prepare'
        return 'advance-after-exit-1' if chain.get('results_on_exit_1') else 'stop-dbsim-without-results'
    if state in (3, 4):          # VSHELL treats both alike (0x46c274) and reads the results
        return 'advance' if pending else 'stop-nothing-pending'
    if state == 6:               # demo end: VSHELL skips the result readback
        return 'stop-demo'
    return 'stop-unexpected-state'


def continuation(result, completed, limit):
    """After the results path: CONTINUE or the stop reason. completed counts
    the missions finished including this one; limit 0 means no limit."""
    if 'error' in result:
        return 'stop-results-rejected'
    if result['ending'] == 3:
        return 'stop-player-lost'    # final word 3 (0x40f475)
    if result['ending'] == 4:
        return 'stop-other-mode'     # mode != 1 ends after the losses
    if result['advance'] != 2:
        return f'stop-return-{result["advance"]}'   # 1: after the last group, 0: outcome 0 at a group end
    if limit and completed >= limit:
        return 'stop-mission-limit'
    return CONTINUE


def file_state(path):
    try:
        data = path.read_bytes()
        stat = path.stat()
    except FileNotFoundError:
        return None
    return dict(size=len(data), sha256=sha(data), mtime_ns=stat.st_mtime_ns)


def fresh(before, after):
    """DBSIM wrote results.dat during the run iff it exists now and differs
    from the snapshot taken when the mission was handed to DBSIM."""
    return after is not None and after != before


def sav_layout(sav):
    """Offset of the player's bay word, the player's bay, the roster pilots'
    bays and the hangar bay indices, by walking the SAV framing of
    docs/formats/mission.md (inventory, five-entry bank, selection, 36 roster
    pilots, roster selection, team words, player, hangar)."""
    at = 0

    def word(offset):
        if offset + 2 > len(sav):
            raise ValueError('truncated SAV')
        return int.from_bytes(sav[offset:offset + 2], 'little', signed=True)

    def pilot():
        nonlocal at
        length = word(at + 4)
        if length < 0:
            raise ValueError('invalid SAV name length')
        suffix = at + 6 + length
        at = suffix + 25
        return suffix
    for _ in range(33):
        count = word(at + 1)
        if count < 0:
            raise ValueError('invalid SAV item count')
        at += 3 + 10 * count
    at += 22 + 152
    roster = [word(pilot()) for _ in range(36)]
    at += 12 + 4
    player = pilot()
    bays, count = [], word(at)
    at += 2
    for _ in range(count):
        bays.append(word(at))
        items = word(at + 78)
        if items < 0:
            raise ValueError('invalid SAV loadout count')
        at += 80 + 12 * items
    if at > len(sav):
        raise ValueError('truncated SAV')
    return player, word(player), roster, bays


def hangar_choice(player_bay, roster_bays, bays):
    """Chain policy, not VSHELL behaviour: the player without a Herc (bay -1
    or an empty bay, e.g. after the (1,6) hook removes vehicle 8) takes the
    lowest hangar bay no roster pilot is assigned to. None: nothing to do;
    -1: no free bay."""
    if player_bay in bays:
        return None
    free = sorted(set(bays) - set(roster_bays))
    return free[0] if free else -1


def with_player_bay(sav, bay):
    offset, _, _, _ = sav_layout(sav)
    return sav[:offset] + int(bay).to_bytes(2, 'little', signed=True) + sav[offset + 2:]


def checked_game_dir(path):
    game = Path(path).resolve()
    if any(part.lower() == PRIMARY_INSTALLATION for part in game.parts):
        raise ValueError('native-r111 is the primary installation; use a measurement clone')
    if not (game / 'DATA').is_dir() or not (game / 'DBSIM.EXE').is_file():
        raise ValueError(f'{game} is not a game directory (DATA/ and DBSIM.EXE required)')
    return game


# ---- Engine: the portable shell library through es2prep_core ---------------------
class Engine:
    def __init__(self, core, cd_root):
        self.core, self.root = Path(core), Path(cd_root)
        self._tables = None

    def tables(self):
        if self._tables is None:
            self._tables = prep.default_tables(self.root)
        return self._tables

    def position(self, sav):
        run = subprocess.run([str(self.core), str(self.root), '--describe-save'], input=sav.hex() + '\n',
                             capture_output=True, text=True, check=True)
        lines = run.stdout.splitlines()
        if not lines or lines[0] != 'OK':
            raise ValueError('SAV rejected: ' + (lines[0] if lines else run.stderr.strip()))
        for line in lines[1:]:
            kind, *rest = line.split()
            if kind == 'POSITION':
                return tuple(map(int, rest[:2]))
        raise ValueError('SAV description without position')

    @staticmethod
    def hangar(sav):
        _, player_bay, roster, bays = sav_layout(sav)
        return player_bay, roster, bays

    @staticmethod
    def with_player_bay(sav, bay):
        return with_player_bay(sav, bay)

    def mission(self, group, position):
        return next((name for g, p, name in prep.campaign(self.core, self.root) if (g, p) == (group, position)), None)

    def prepare(self, sav, group, position, rng, selectors, language):
        request = prep.request(group=group, position=position, mode=1, language=language, rng=rng,
                               selectors=selectors, tables=self.tables(), save=sav)
        out, = prep.prepare_batch(self.core, self.root, [request])
        return out

    def advance(self, sav, variables, results, rng, language, repair, restock):
        return campaign.advance(self.core, self.root, sav, variables, results, language=language,
                                mode=1, repair=repair, restock=restock, rng=rng)

    def summary(self, out, language):
        return campaign.summary(out, self.core, self.root, language)


# ---- Chain directory ------------------------------------------------------------
class Chain:
    def __init__(self, directory):
        self.dir = Path(directory).resolve()
        self.path = self.dir / 'chain.json'
        self.data = json.loads(self.path.read_text(encoding='utf-8'))
        if self.data.get('schema') != SCHEMA:
            raise ValueError('unknown chain schema')

    def save(self):
        temporary = self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(self.data, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary, self.path)

    def log(self, text):
        with (self.dir / 'chain.log').open('a', encoding='utf-8') as stream:
            stream.write(f'{now()} {text}\n')

    @property
    def game(self):
        return Path(self.data['game_dir'])

    def backup_data(self):
        """Once, before the first DATA write: copy the transaction files."""
        if self.data.get('data_backup') is not None:
            return
        backup = self.dir / 'data-backup'
        backup.mkdir()
        present = []
        for name in TRANSACTION_FILES:
            source = self.game / 'DATA' / name
            if source.is_file():
                shutil.copy2(source, backup / name)
                present.append(name)
        self.data['data_backup'] = present

    def restore_data(self):
        """Put DATA back as it was before the chain; files that did not exist
        then are removed. Returns the names touched."""
        present = self.data.get('data_backup')
        if present is None or self.data.get('data_restored'):
            return []
        touched = []
        for name in TRANSACTION_FILES:
            target = self.game / 'DATA' / name
            if name in present:
                shutil.copy2(self.dir / 'data-backup' / name, target)
                touched.append(name)
            elif target.exists():
                target.unlink()
                touched.append(name)
        self.data['data_restored'] = now()
        return touched


def mission_dir(chain, index):
    return chain.dir / f'mission-{index:03d}'


def prepare_mission(chain, engine, sav, index, write_data):
    """Prepare the CAREER mission of sav; record it as mission <index>."""
    group, position = engine.position(sav)
    if not 1 <= group <= 5:
        raise ValueError(f'CAREER group {group} is not a campaign group (mode 1)')
    player_bay, roster, bays = engine.hangar(sav)
    choice, hangar = hangar_choice(player_bay, roster, bays), None
    if choice is not None:
        if chain.data.get('hangar', 'stop') != 'lowest-free' or choice < 0:
            return dict(error=f'the player has no Herc (bay {player_bay}); the VSHELL hangar needs a choice',
                        stop='stop-player-without-herc', group=group, position=position)
        sav = engine.with_player_bay(sav, choice)
        hangar = dict(player_bay=player_bay, chosen=choice, save=f'save-{index:03d}-hangar.sav')
        (chain.dir / hangar['save']).write_bytes(sav)
        chain.data['save'] = hangar['save']
        chain.log(f'hangar policy: player bay {player_bay} -> {choice}')
    language = chain.data['language']
    out = engine.prepare(sav, group, position, bytes.fromhex(chain.data['rng']),
                         bytes.fromhex(chain.data['selectors']), language)
    if 'error' in out:
        return dict(error=out['error'], group=group, position=position)
    directory = mission_dir(chain, index)
    directory.mkdir()
    for name in LAUNCH_FILES:
        (directory / name).write_bytes(out[name])
    record = dict(index=index, group=group, position=position, mission=engine.mission(group, position),
                  input_save=chain.data['save'],
                  prepared_utc=now(), input_rng=chain.data['rng'], input_selectors=chain.data['selectors'],
                  files={name: dict(size=len(out[name]), sha256=sha(out[name])) for name in LAUNCH_FILES},
                  output_rng=out['rng'].hex(), output_selectors=out['selectors'].hex(), hangar=hangar)
    chain.data['rng'], chain.data['selectors'] = out['rng'].hex(), out['selectors'].hex()
    if write_data:
        chain.backup_data()
        for name in LAUNCH_FILES:
            (chain.game / 'DATA' / name).write_bytes(out[name])
        record['results_before'] = file_state(chain.game / 'DATA' / 'results.dat')
        chain.data['pending'] = record
    chain.data['missions'].append(record)
    chain.log(f'prepared mission {index} {record["mission"]} ({group},{position}) data={int(write_data)}')
    return record


def advance_mission(chain, engine):
    """DBSIM ended the pending mission: results path, new SAV. Returns the
    es2campaign summary and the raw output."""
    pending = chain.data['pending']
    data = chain.game / 'DATA'
    after = file_state(data / 'results.dat')
    if not fresh(pending.get('results_before'), after):
        return None, dict(error='DATA/results.dat was not written by this DBSIM run')
    results, variables = (data / 'results.dat').read_bytes(), (data / 'mission.var').read_bytes()
    evidence = mission_dir(chain, pending['index']) / 'dbsim'
    evidence.mkdir()
    (evidence / 'results.dat').write_bytes(results)
    (evidence / 'mission.var').write_bytes(variables)
    chain.data['missions'][pending['index']]['dbsim_state'] = chain.data['starts'][-1]['state']
    sav = (chain.dir / chain.data['save']).read_bytes()
    out = engine.advance(sav, variables, results, bytes.fromhex(chain.data['rng']), chain.data['language'],
                         chain.data['repair_option'], chain.data['restock_option'])
    summary = engine.summary(out, chain.data['language'])
    summary['dbsim'] = dict(results=dict(size=len(results), sha256=sha(results)),
                            mission_var=dict(size=len(variables), sha256=sha(variables)))
    if 'error' not in out:
        name = f'save-{pending["index"] + 1:03d}.sav'
        (chain.dir / name).write_bytes(out['sav'])
        chain.data['save'] = name
        chain.data['rng'] = out['rng'].hex()
        summary['save']['file'] = name
    # pending is a copy of the mission's record once chain.json was reloaded.
    chain.data['missions'][pending['index']]['advance'] = summary
    chain.data['pending'] = None
    chain.data['completed'] += 1
    return summary, out


def finish(chain, reason, detail=None):
    chain.data['finished'] = dict(reason=reason, utc=now(), detail=detail)
    chain.data['pending'] = None
    restored = [] if chain.data['keep_data'] else chain.restore_data()
    chain.data['finished']['restored'] = restored
    chain.log(f'finished {reason}' + (f': {detail}' if detail else '') + f' restored={",".join(restored)}')
    return 0


def step(directory, argv, engine=None):
    """One shell start. Returns the exit code for the launcher (2 or 0)."""
    chain = Chain(directory)
    engine = engine or Engine(chain.data['core'], chain.data['cd_root'])
    try:
        chain.log('start ' + ' '.join(argv))
        try:
            state, language, flags = parse_shell_arguments(argv)
        except ValueError as error:
            return finish(chain, 'stop-bad-arguments', str(error))
        if language != chain.data['language']:
            return finish(chain, 'stop-language-mismatch', f'launcher {language}, chain {chain.data["language"]}')
        if Path.cwd().resolve() != chain.game.resolve():
            return finish(chain, 'stop-wrong-directory', str(Path.cwd()))
        checked_game_dir(chain.game)
        action = decide(chain.data, state)
        chain.data['starts'].append(dict(utc=now(), state=state, flags=flags, action=action))
        if action == 'stop-finished':
            chain.log('already finished')
            return 0
        if action.startswith('stop-'):
            return finish(chain, action, f'shell state {state}')
        if action == 'prepare':
            sav = (chain.dir / chain.data['save']).read_bytes()
            record = prepare_mission(chain, engine, sav, len(chain.data['missions']), True)
            if 'error' in record:
                return finish(chain, record.get('stop', 'stop-preparation-rejected'), record['error'])
            return 2
        summary, out = advance_mission(chain, engine)
        reason = continuation(out, chain.data['completed'], chain.data['mission_limit'])
        if summary is None and action == 'advance-after-exit-1':
            return finish(chain, 'stop-dbsim-without-results', out['error'])
        if summary is None or 'error' in out:
            return finish(chain, 'stop-results-rejected', out['error'])
        chain.log(f'advanced: {json.dumps({k: summary[k] for k in ("ending", "advance", "next")})}')
        if reason not in (CONTINUE, 'stop-mission-limit'):
            return finish(chain, reason)
        record = prepare_mission(chain, engine, out['sav'], len(chain.data['missions']), reason == CONTINUE)
        if 'error' in record:
            return finish(chain, record.get('stop', 'stop-preparation-rejected'), record['error'])
        if reason != CONTINUE:
            return finish(chain, reason)
        return 2
    except Exception as error:   # the launcher must always get a clean answer
        chain.log(traceback.format_exc().rstrip())
        return finish(chain, 'stop-exception', f'{type(error).__name__}: {error}')
    finally:
        chain.save()


def launch_shell_command(directory):
    """ES2_LAUNCH_SHELL value for this chain (the launcher appends the argv)."""
    return f'"{sys.executable}" -B "{Path(__file__).resolve()}" step --state "{Path(directory).resolve()}"'


def init(args, engine=None):
    game = checked_game_dir(args.game_dir)
    sav = args.save.read_bytes()
    engine = engine or Engine(args.core, args.cd_root)
    group, position = engine.position(sav)
    if not 1 <= group <= 5:
        raise ValueError(f'the SAV selects CAREER group {group}; the chain runs campaign groups 1..5 (mode 1)')
    rng = args.rng_state.read_bytes() if args.rng_state else prep.initial_rng(args.cd_root, args.seed)
    selectors = args.reset_selectors.read_bytes() if args.reset_selectors else bytes(60)
    if len(rng) != 114 or len(selectors) != 60:
        raise ValueError('RNG state must have 114 bytes, reset selectors 60')
    directory = prep.export_path(args.state)
    directory.mkdir()
    (directory / 'save-000.sav').write_bytes(sav)
    data = dict(schema=SCHEMA, created_utc=now(), core=str(Path(args.core).resolve()),
                cd_root=str(Path(args.cd_root).resolve()), game_dir=str(game), language=args.language,
                mission_limit=args.missions, repair_option=args.repair_option,
                restock_option=args.restock_option, keep_data=args.keep_data, hangar=args.hangar,
                results_on_exit_1=args.results_on_exit_1,
                initial=dict(save=dict(file=str(args.save.resolve()), size=len(sav), sha256=sha(sav)),
                             position=[group, position], seed=None if args.rng_state else args.seed,
                             rng_sha256=sha(rng)),
                save='save-000.sav', rng=rng.hex(), selectors=selectors.hex(), completed=0,
                pending=None, finished=None, data_backup=None, data_restored=None, missions=[], starts=[])
    (directory / 'chain.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    return dict(state=str(directory), position=[group, position], launch_shell=launch_shell_command(directory))


def status(directory):
    data = Chain(directory).data
    return dict(finished=data['finished'], completed=data['completed'], save=data['save'],
                pending=None if data['pending'] is None else data['pending']['index'],
                missions=[dict(index=m['index'], mission=m['mission'], position=[m['group'], m['position']],
                               files={k: v['sha256'] for k, v in m['files'].items()},
                               advance=None if 'advance' not in m else
                               {k: m['advance'].get(k) for k in ('ending', 'advance', 'next', 'dbsim', 'error')})
                          for m in data['missions']])


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ['step']:
        # Exit code 2 would start DBSIM: every failure before the chain can
        # record it ends the launcher instead (0).
        if len(argv) < 3 or argv[1] != '--state':
            print('usage: es2chain.py step --state DIR <VSHELL arguments>', file=sys.stderr)
            return 0
        try:
            return step(argv[2], argv[3:])
        except Exception as error:
            print(f'es2chain step: {error}', file=sys.stderr)
            return 0
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    first = commands.add_parser('init', help='create a new chain directory')
    first.add_argument('--core', required=True, type=Path)
    first.add_argument('--cd-root', required=True, type=Path)
    first.add_argument('--state', required=True, type=Path, help='new chain directory')
    first.add_argument('--game-dir', required=True, type=Path, help='measurement clone the launcher runs in')
    first.add_argument('--save', required=True, type=Path, help='SAV of the campaign state (read only)')
    first.add_argument('--language', choices=('GER', 'ENG', 'FRE'), default='GER')
    first.add_argument('--seed', type=int, default=0, help='initial RNG seed (raw original word)')
    first.add_argument('--rng-state', type=Path, help='114-byte RNG state instead of --seed')
    first.add_argument('--reset-selectors', type=Path, help='60 bytes; default zero')
    first.add_argument('--missions', type=int, default=0, help='stop after this many missions (0: no limit)')
    first.add_argument('--repair-option', type=int, default=0, help='option byte 0x4825c8')
    first.add_argument('--restock-option', type=int, default=0, help='option byte 0x4825c9')
    first.add_argument('--keep-data', action='store_true', help='do not restore DATA when the chain stops')
    first.add_argument('--results-on-exit-1', action='store_true',
                       help='measurement runs: read a results.dat written before DBSIM exit code 1 '
                            '(VSHELL would start fresh instead)')
    first.add_argument('--hangar', choices=('stop', 'lowest-free'), default='stop',
                       help='player without a Herc: stop (default) or take the lowest free bay (chain policy)')
    for name in ('status', 'restore'):
        sub = commands.add_parser(name)
        sub.add_argument('--state', required=True, type=Path)
    args = parser.parse_args(argv)
    if args.command == 'init':
        if not 0 <= args.seed <= 65535 or args.missions < 0:
            parser.error('--seed must be 0..65535 and --missions >= 0')
        result = init(args)
    elif args.command == 'status':
        result = status(args.state)
    else:
        chain = Chain(args.state)
        result = dict(restored=chain.restore_data())
        chain.save()
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    if sys.argv[1:2] == ['step']:
        raise SystemExit(main())
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'es2chain: {error}', file=sys.stderr)
        raise SystemExit(2)
