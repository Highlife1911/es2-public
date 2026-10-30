# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Practice missions, INSTANT ACTION, VIEW DEMO and the DEBUG dialog
against VSHELL's own handlers in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/practice.h through the test
driver practice_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro, slots_oracle.py) with the
operations: practice-show (PRACTICE MISSIONS 0x431f3a), practice-entry:I
(0x44caa3 ... -> 0x44c40c), practice-control:K:B (the five parameter
handlers with the button byte of the release that runs them),
practice-state, practice-menu (0x44c96a), practice-begin (Begin Mission
0x44ca26), instant-action (0x431935), use-default (the posted click on Use
Default, 0x44dbea), debug-accept:TEXT (ACCEPT 0x44dc4d), view-demo
(0x431bfe), practice-reset (a new process: 0x479c54 := -1, 0x473784 := 0),
practice-emit, and the preferences' prefs-load, prefs:accept, prefs:cancel and
restore:N.

Per run the records of practice-emit (DATA\\script.dat, MISSION.STR,
mission.var, player.mec, prefs.cfg as the original leaves them on its
memory disk, the SAV of the state from the original writers, and the
selection, preset flag, exit code, quit flag, mode word, CAREER position and
loaded flag) equal ours byte for byte, the practice-state answers equal and
ours refuses nothing.

Corpus:
a. every entry with every combination of its controls (Ammo, Damage,
   Mission Difficulty, Time of Day, and Herc Type for entries 4..7: 1,280
   launches), reached by left and right clicks from the previous launch's
   prefs.cfg;
b. INSTANT ACTION: +0x2e 0, 1, 2 and its rotation over consecutive starts,
   after the practice screen (uncommitted controls committed), without the
   posted click (-@: the new game's files only), VIEW DEMO;
c. seeded sequences: repeated entries, disabled Herc Type presses, Main
   Menu with Preferences ACCEPT or CANCEL, ACCEPT of typed mission files,
   three languages, prefs.cfg missing, short, 54 and 60 bytes;
d. a launch after a game the process held (restore:N of SAVs with a changed
   briefing selection, nine-word bank and the 20 bytes at 0x4833ac);
e. the own menu es2menu.exe (main-menu entries 7..9) in temporary game
   directories with a stand-in backend: PRACTICE MISSIONS with two controls,
   INSTANT ACTION, -@ with a typed mission file and with INSTANT ACTION: the
   DATA and SAV directories (prefs.cfg, the four launch files with VSHELL's
   non-truncating rewrites, GAMEFILE.STR, slot 11 and its three companions
   from the exit save) equal the worker's disk after the same operations and
   0x40e933(10); VIEW DEMO through es2launch.exe: exit 5, DBSIM -X5 -D (a
   stand-in returning 6), the menu again with -X6.

Arguments: practice_tool es2prep.py es2shellprep slots_oracle.py es2menu
es2launch stand_in_child
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time

LANGUAGES = {'G': 'GER', 'E': 'ENG', 'F': 'FRE'}
MODULI = (2, 2, 4, 9, 2)          # Ammo, Damage, Difficulty, Herc Type, Time of Day (+0x25..+0x29)
MAX_OPS = 500                     # the worker takes 512 --op
MENU_SEED = 5                     # menu.ini seed of the menu sessions
# DATA files from an earlier mission (own bytes): VSHELL rewrites script.dat and
# MISSION.STR without truncation, and its save asserts on a missing one.
OLD_SCRIPT = bytes((11 * i + 5) & 0xff for i in range(20000))
OLD_STRINGS = bytes((13 * i + 1) & 0xff for i in range(3000))
STEMS = ('TRAIN1', 'TRAIN8', 'DEMO', 'demo^01', 'demo^02', 'C1_01')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def exe_span(image, va, n):
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    for i in range(count):
        address, size, raw = struct.unpack_from('<III', image, pe + 24 + optional + i * 40 + 12)
        offset = va - 0x400000 - address
        if 0 <= offset and offset + n <= size:
            return image[raw + offset:raw + offset + n]
    raise ValueError('span outside file sections')


def records(blob):
    out, at = [], 0
    while at < len(blob):
        rec = {}
        for name in ('script', 'strings', 'variables', 'player', 'prefs'):
            n, = struct.unpack_from('<I', blob, at)
            at += 4
            if n == 0xffffffff:
                rec[name] = None
            else:
                rec[name] = blob[at:at + n]
                at += n
        n, = struct.unpack_from('<I', blob, at)
        rec['sav'] = blob[at + 4:at + 4 + n]
        at += 4 + n
        rec['words'] = struct.unpack_from('<8i', blob, at)
        at += 32
        out.append(rec)
    return out


class Model:
    """The prefs bytes the screen holds, to plan clicks (the verdict never depends on it)."""
    def __init__(self, table, file):
        self.table, self.file = table, file
        self.current = (file or b'')[:54].ljust(54, b'\0')
        self.current = bytearray(self.current)
        self.index = -1

    def load(self):
        self.current = bytearray((self.file or b'')[:54].ljust(54, b'\0'))

    def entry(self, e):
        if self.index != e:
            self.current[0x28] = self.table[e]
            self.index = e

    def show(self):
        self.entry(0)
        image = bytearray(self.current)
        if self.file is not None:
            image[:min(54, len(self.file))] = self.file[:54]
        image[0x2a] = self.current[0x2a] = 0
        self.file = bytes(image)

    def click(self, k, button):
        v, n = self.current[0x25 + k], MODULI[k]
        if k == 3 and not 4 <= self.index <= 7:
            return
        if button == 1:
            v = (v + 1) & 0xff
            v = 0 if v == n else v
        else:
            v = (v - 1) & 0xff
            v = n - 1 if v == 0xff else v
        self.current[0x25 + k] = v

    def begin(self):
        self.file = bytes(self.current)

    def reach(self, k, target):
        """Clicks from the current value to target, the shorter way round."""
        v, n = self.current[0x25 + k], MODULI[k]
        forward, back = (target - v) % n, (v - target) % n
        ops = [f'practice-control:{k}:1'] * forward if forward <= back else [f'practice-control:{k}:3'] * back
        for op in ops:
            self.click(k, int(op[-1]))
        return ops


class Test:
    def __init__(self, argv):
        (self.tool, prep_py, self.worker, oracle_py, self.menu_exe, self.launcher,
         self.stand_in) = [Path(a).resolve() for a in argv]
        self.prep, self.oracle = load(prep_py, 'practice_prep'), load(oracle_py, 'practice_oracle')
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        image = (self.cd / 'VER95' / 'VSHELL.EXE').read_bytes()
        if hashlib.sha256(image).hexdigest() != self.prep.REFERENCE_HASH:
            raise ValueError('VSHELL.EXE differs from the documented reference')
        defaults = self.prep.default_tables(self.cd)
        self.header = [(defaults[:164] + self.prep.results_tables(self.cd)[-60:]).hex(), defaults[164:].hex(),
                       (exe_span(image, 0x479c56, 24) + exe_span(image, 0x479f70, 8) +
                        exe_span(image, 0x47396b, 8)).hex()]
        self.table = exe_span(image, 0x479c56, 24)[::2]
        self.gamefile = (self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()
        self.scratch = tempfile.TemporaryDirectory(prefix='es2practice-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.failures, self.counts = [], {}
        self.runs = 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def count(self, key, n=1):
        self.counts[key] = self.counts.get(key, 0) + n

    # ---- one batch: the same operations on both sides ------------------------
    def run(self, label, ops, language='G', prefs=None, states=(), seed=0):
        work = self.tmp / f'run-{label}'
        work.mkdir()
        rng = self.prep.initial_rng(self.cd, seed)
        argv = [str(self.worker), '--cd-root', str(self.cd), '--language', language, '--mode', '1', '--pipe']
        files = {'sav\\gamefile.str': self.gamefile}
        if prefs is not None:
            files['data\\prefs.cfg'] = prefs
        for i, (name, data) in enumerate(files.items()):
            (work / f'disk-{i}.bin').write_bytes(data)
            argv += ['--file', name, str(work / f'disk-{i}.bin')]
        (work / 'state.rng').write_bytes(rng)
        argv += ['--rng-state', str(work / 'state.rng')]
        for i, state in enumerate(states):
            (work / f'state-{i}.sav').write_bytes(state)
            argv += ['--state', str(work / f'state-{i}.sav')]
        for op in ops:
            argv += ['--op', op]
        original = subprocess.run(argv, capture_output=True, timeout=300)
        ours_ops = [f'restore-hex:{states[int(op[8:])].hex()}' if op.startswith('restore:') else op for op in ops]
        tool_input = '\n'.join(self.header + [rng.hex(), prefs.hex() if prefs is not None else '-'] + ours_ops) + '\n'
        ours = subprocess.run([str(self.tool), str(self.cd), LANGUAGES[language]], input=tool_input.encode('ascii'),
                              capture_output=True, timeout=300)
        return label, ops, original, ours

    def compare(self, result):
        label, ops, original, ours = result
        self.runs += 1
        if not self.check(original.returncode == 0, f'{label}: worker exit {original.returncode}: '
                          f'{original.stderr.decode("utf-8", "replace")[-1500:]}'):
            return []
        if not self.check(ours.returncode == 0, f'{label}: practice_tool exit {ours.returncode}'):
            return []
        packet = self.oracle.decode(original.stdout)
        lines = ours.stdout.decode('ascii').splitlines()
        mine = b''.join(bytes.fromhex(line[5:]) for line in lines if line.startswith('EMIT '))
        states = [list(map(int, line.split()[1:])) for line in lines if line.startswith('STATE ')]
        refused = [line for line in lines if line.startswith(('REFUSED', 'NOTE'))]
        self.check(not refused, f'{label}: ours refused {refused[:3]}')
        self.check(states == packet['metadata'].get('practice', []), f'{label}: practice-state {states[:4]} '
                   f'{packet["metadata"].get("practice", [])[:4]}')
        a, b = records(packet['disk'].get('practice.emit', b'')), records(mine)
        self.check(len(a) == len(b) == ops.count('practice-emit'), f'{label}: {len(a)}/{len(b)} records')
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                keys = [k for k in x if x[k] != y[k]]
                self.check(False, f'{label}: record {i} differs in {keys}')
        self.count('records', len(a))
        self.count('compared bytes', sum(len(v) for r in a for k, v in r.items() if k != 'words' and v))
        return a

    def batches(self, plans, workers=4):
        with ThreadPoolExecutor(max_workers=workers) as pool:
            return [self.compare(r) for r in pool.map(lambda p: self.run(*p[:2], **p[2]), plans)]

    # ---- a. every entry and control combination --------------------------------
    def combinations(self):
        start = bytes((5 * i + 1) & 0xff for i in range(54))
        start = bytearray(start)
        start[0x25:0x2a] = bytes(5)
        start[0x2e] = 0
        model = Model(self.table, bytes(start))
        plans, ops, batch_prefs, n = [], [], model.file, 0
        targets = []
        for e in range(8):
            herc = range(9) if e >= 4 else [None]
            for c4 in herc:
                for c3 in range(4):
                    for c1 in range(2):
                        for c2 in range(2):
                            for c5 in range(2):
                                targets.append((e, (c1, c2, c3, c4, c5)))
        for e, target in targets:
            before = model.file
            model.load()
            model.index = -1
            launch = ['prefs-load', 'practice-reset', 'practice-show']
            model.show()
            launch.append(f'practice-entry:{e}')
            model.entry(e)
            for k in range(5):
                if target[k] is not None:
                    launch += model.reach(k, target[k])
            launch += ['practice-begin', 'use-default', 'practice-emit']
            model.begin()
            if len(ops) + len(launch) > MAX_OPS:
                plans.append((f'a{len(plans)}', ops, dict(prefs=batch_prefs)))
                ops, batch_prefs = [], before
            ops += launch
            n += 1
        plans.append((f'a{len(plans)}', ops, dict(prefs=batch_prefs)))
        return plans, n

    # ---- b. INSTANT ACTION and VIEW DEMO -------------------------------------
    def instant(self):
        plans = []
        rnd = random.Random(29)
        for k in range(3):
            prefs = bytearray(rnd.randrange(256) for _ in range(54))
            prefs[0x25:0x2a] = bytes(rnd.randrange(4) for _ in range(5))
            prefs[0x2e] = k
            prefs = bytes(prefs)
            ops = []
            for _ in range(3):                                       # the rotation over three starts
                ops += ['prefs-load', 'practice-reset', 'instant-action', 'use-default', 'practice-emit']
            for entry in (2, 6):                                     # after the practice screen
                ops += ['prefs-load', 'practice-reset', 'practice-show', f'practice-entry:{entry}',
                        'practice-control:0:1', 'practice-control:2:3', 'practice-state', 'practice-menu',
                        'instant-action', 'practice-state', 'use-default', 'practice-emit']
            ops += ['prefs-load', 'practice-reset', 'instant-action', 'practice-emit']   # -@: no posted click
            ops += ['prefs-load', 'practice-reset', 'view-demo', 'practice-emit']
            plans.append((f'b{k}', ops, dict(prefs=prefs, language='GEF'[k])))
        plans.append(('b-missing', ['prefs-load', 'instant-action', 'use-default', 'practice-emit',
                                    'prefs-load', 'practice-reset', 'instant-action', 'use-default', 'practice-emit'],
                      dict(prefs=None)))
        return plans

    # ---- c. seeded sequences -----------------------------------------------------
    def seeded(self, count=12):
        plans = []
        rnd = random.Random(13)
        for k in range(count):
            kind = k % 4
            prefs = None if kind == 0 else bytes(rnd.randrange(256) for _ in range((10, 54, 60)[kind - 1]))
            if prefs is not None and len(prefs) >= 0x2a:   # value labels that exist (else 0x4087f0 asserts)
                prefs = prefs[:0x25] + bytes(rnd.randrange(10) for _ in range(5)) + prefs[0x2a:]
            if prefs is not None and len(prefs) > 0x2e:
                prefs = bytearray(prefs)
                prefs[0x2e] = rnd.randrange(3)
                prefs = bytes(prefs)
            ops = []
            while len(ops) < 420:
                ops += ['prefs-load', 'practice-reset', 'practice-show']
                for _ in range(rnd.randrange(1, 12)):
                    roll = rnd.random()
                    if roll < 0.3:
                        ops.append(f'practice-entry:{rnd.randrange(8)}')
                    elif roll < 0.85:
                        ops.append(f'practice-control:{rnd.randrange(5)}:{rnd.choice((1, 3))}')
                    elif roll < 0.92:
                        ops.append('practice-state')
                    else:
                        ops += ['practice-menu', rnd.choice(('prefs:accept', 'prefs:cancel')), 'practice-show']
                end = rnd.random()
                if end < 0.15:
                    ops += ['practice-begin', f'debug-accept:{rnd.choice(STEMS)}', 'practice-emit']
                elif end < 0.25:
                    ops += ['practice-menu', 'instant-action', 'use-default', 'practice-emit']
                else:
                    ops += ['practice-begin', 'use-default', 'practice-emit']
            plans.append((f'c{k}', ops, dict(prefs=prefs, language='GEF'[k % 3], seed=rnd.randrange(128))))
        return plans

    # ---- d. after a game the process held --------------------------------------
    def carried(self, sav):
        states = []
        for k in range(2):
            out = bytearray(sav)
            at = 0
            for _ in range(33):
                at += 3 + 10 * struct.unpack_from('<h', out, at + 1)[0]
            selection = at + 22
            for i in range(4, 152):
                out[selection + i] = (i * (7 + k)) & 0xff
            at = selection + 152
            for _ in range(36):
                at += 6 + struct.unpack_from('<h', out, at + 4)[0] + 25
            at += 16
            at += 6 + struct.unpack_from('<h', out, at + 4)[0] + 25
            count, = struct.unpack_from('<h', out, at)
            at += 2
            for _ in range(count):
                at += 80 + 12 * struct.unpack_from('<h', out, at + 78)[0]
            out[at:at + 18] = struct.pack('<9h', *[(3 + k) * (i + 1) for i in range(9)])
            final = at + 18 + 4 + 2000
            out[final + 2:final + 22] = bytes(range(40 + k, 60 + k))
            states.append(bytes(out))
        ops = ['restore:0', 'prefs-load', 'practice-show', 'practice-entry:5', 'practice-begin', 'use-default',
               'practice-emit', 'prefs-load', 'practice-reset', 'instant-action', 'use-default', 'practice-emit',
               'restore:1', 'prefs-load', 'practice-reset', 'instant-action', 'use-default', 'practice-emit']
        return [('d', ops, dict(states=states))]

    # ---- e. the own menu (es2menu.exe, entries 7..9) -------------------
    def menu_game(self, name, prefs):
        game = self.tmp / name / 'game'
        for sub in ('DATA', 'SAV', 'VOL'):
            (game / sub).mkdir(parents=True)
        (game / 'DATA' / 'language.cfg').write_bytes(b'G')
        (game / 'DATA' / 'PREFS.CFG').write_bytes(prefs)
        (game / 'DATA' / 'script.dat').write_bytes(OLD_SCRIPT)
        (game / 'DATA' / 'MISSION.STR').write_bytes(OLD_STRINGS)
        (game / 'SAV' / 'GAMEFILE.STR').write_bytes(self.gamefile)
        (game / 'VOL' / 'LANG0.VOL').write_bytes((self.cd / 'VOL' / 'LANG0.VOL').read_bytes())
        menu = self.tmp / name / 'menu'
        menu.mkdir()
        backend = self.tmp / name / 'backend.py'
        backend.write_text('import sys\nargs = sys.argv[1:]\n'
                           'open(args[args.index("--answer") + 1], "w").write("OK\\nCAMPAIGN -\\n")\n', encoding='ascii')
        (menu / 'menu.ini').write_text(f'[Menu]\npython = {sys.executable}\nbackend = {backend}\ngame_dir = {game}\n'
                                       f'cd_root = {self.cd}\nseed = {MENU_SEED}\n', encoding='ascii')
        return game, menu

    def menu_run(self, game, menu, script, extra=()):
        argv = [str(self.menu_exe), '--menu', str(menu), 'dummy', '-eggplant', '-X1', *extra, '-G']
        return subprocess.run(argv, cwd=game, input=script.replace('|', '\n').encode('ascii'), capture_output=True,
                              timeout=300)

    def menu_compare(self, label, game, ops, prefs):
        """The game directory after the menu against the worker's memory disk after ops."""
        rng = self.prep.initial_rng(self.cd, MENU_SEED)
        result = self.oracle.run(self.worker, self.cd, {'sav\\gamefile.str': self.gamefile, 'data\\prefs.cfg': prefs,
                                                        'data\\script.dat': OLD_SCRIPT, 'data\\MISSION.STR': OLD_STRINGS},
                                 ops, self.tmp / label / 'oracle', rng=rng)
        disk = {name.lower(): data for name, data in result['disk'].items()}
        mine = {}
        for sub in ('DATA', 'SAV'):
            for path in (game / sub).iterdir():
                mine[f'{sub}\\{path.name}'.lower()] = path.read_bytes()
        mine.pop('data\\language.cfg')
        self.check(set(disk) == set(mine), f'{label}: files {sorted(disk)} / {sorted(mine)}')
        for name in sorted(set(disk) & set(mine)):
            self.check(disk[name] == mine[name], f'{label}: {name} differs')
        self.count('menu files compared', len(set(disk) & set(mine)))
        return result

    def menu_sessions(self):
        prefs = bytearray((3 * i + 2) & 0xff for i in range(54))
        prefs[0x25:0x2a] = bytes((1, 0, 2, 0, 1))
        prefs[0x2e] = 2
        prefs = bytes(prefs)
        # PRACTICE MISSIONS: entry 6 (Herc Type +1, Ammo back), Begin Mission; the exit saves slot 11.
        game, menu = self.menu_game('menu-practice', prefs)
        run = self.menu_run(game, menu, '7|6|14|21|10')
        self.check(run.returncode == 2, f'menu practice: exit {run.returncode} {run.stdout[-400:]}')
        self.menu_compare('menu-practice', game, ['prefs-load', 'practice-show', 'practice-entry:5', 'practice-control:3:1',
                                                  'practice-control:0:3', 'practice-begin', 'use-default', 'save:10'], prefs)
        # INSTANT ACTION (+0x2e = 2: DEMO_02, rotation to 0).
        game, menu = self.menu_game('menu-instant', prefs)
        run = self.menu_run(game, menu, '8')
        self.check(run.returncode == 2, f'menu instant action: exit {run.returncode}')
        self.menu_compare('menu-instant', game, ['prefs-load', 'instant-action', 'use-default', 'save:10'], prefs)
        # -@: the DEBUG dialog waits; ACCEPT of a typed name (the first '^' becomes '_').
        game, menu = self.menu_game('menu-debug', prefs)
        run = self.menu_run(game, menu, '7|2|10|2|demo^01', extra=('-@',))
        self.check(run.returncode == 2, f'menu DEBUG ACCEPT: exit {run.returncode}')
        self.menu_compare('menu-debug', game, ['prefs-load', 'practice-show', 'practice-entry:1', 'practice-begin',
                                               'debug-accept:demo^01', 'save:10'], prefs)
        # -@ and INSTANT ACTION: no posted click, the new game's files only.
        game, menu = self.menu_game('menu-debug-instant', prefs)
        run = self.menu_run(game, menu, '8', extra=('-@',))
        self.check(run.returncode == 2, f'menu -@ instant action: exit {run.returncode}')
        self.menu_compare('menu-debug-instant', game, ['prefs-load', 'instant-action', 'save:10'], prefs)
        # VIEW DEMO through the launcher: 5 starts DBSIM -D, its 6 the menu with -X6.
        game, menu = self.menu_game('menu-demo', prefs)
        shutil.copyfile(self.stand_in, game / 'dbsim.EXE')
        (game / 'codes.txt').write_text('6\n', encoding='ascii')
        body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
        body += struct.pack('<hh', 1, 2) + b'c\0\0'
        (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
        (self.tmp / 'menu-demo' / 'input.txt').write_text('9\n5\n', encoding='ascii')
        shell = f'"{self.menu_exe}" --menu "{menu}" --input "{self.tmp / "menu-demo" / "input.txt"}"'
        log = game / 'launch.log'
        env = dict(os.environ, ES2_LAUNCH_SHELL=shell, ES2_LAUNCH_LOG=str(log), ES2_LAUNCH_NO_UI='1')
        env.pop('ES2_LAUNCH_SHELL_CONSOLE', None)
        run = subprocess.run([str(self.launcher)], cwd=game, env=env, timeout=300)
        lines = [line.split(' ', 1)[1] for line in log.read_text(encoding='ascii').splitlines()][1:]
        want = [f'start shell-replacement {shell} dummy -eggplant -X1 -G', 'exit shell-replacement 5',
                'start dbsim.EXE dummy -eggplant -X5 -R0 -D -G', 'exit dbsim.EXE 6',
                f'start shell-replacement {shell} dummy -eggplant -X6 -G', 'exit shell-replacement 0', 'end 0']
        self.check(run.returncode == 0 and lines == want, f'menu VIEW DEMO: launcher log {lines}')
        self.check((game / 'DATA' / 'PREFS.CFG').read_bytes() == prefs and not (game / 'SAV' / 'GAME_T.SAV').exists(),
                   'menu VIEW DEMO: nothing written')
        self.count('menu sessions', 5)

    def main(self):
        started = time.time()
        plans, launches = self.combinations()
        results = self.batches(plans + self.instant() + self.seeded())
        self.count('combination launches', launches)
        covered = set()
        for result in results[:len(plans)]:
            for rec in result:
                covered.add((rec['words'][0],) + tuple(rec['prefs'][0x25:0x2a]))
        self.check(len(covered) == launches, f'{len(covered)} distinct entry/control combinations of {launches}')
        self.count('combinations covered', len(covered))
        first = next((r for r in results if r), None)
        if self.check(first is not None, 'no record for the carry case'):
            self.batches(self.carried(first[0]['sav']), workers=1)
        self.menu_sessions()
        self.count('runs', self.runs)
        print('counts', self.counts, f'{time.time() - started:.1f} s', flush=True)
        if self.failures:
            print(f'{len(self.failures)} failures')
            return 1
        print('practice_real_test passed')
        return 0


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT is not set')
        return 0
    return Test(sys.argv[1:]).main()


if __name__ == '__main__':
    sys.exit(main())
