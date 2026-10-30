# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Scripted sessions of the own menu es2menu.exe.

es2menu keeps the campaign itself (engine_io.c, es2/shell/engine.h)
and starts no backend; the campaign directory under the menu directory is its
evidence log (menu.ini chain_dir, written by es2menu_backend.py setup).

Part 1 (always): es2menu.exe in a temporary game directory with a stand-in
backend (which it must not start) and synthetic files: stdin scripts for the
refusals, the end of the input, Start mission without a campaign,
Preferences (bytes +0x2c, +0x2d, +6 through formats/cfg, Accept and Cancel)
and the mode byte +0x2a.

Part 2 (needs ES2_DATA_ROOT; read only there): es2launch.exe with
ES2_LAUNCH_SHELL = es2menu and a stand-in dbsim.EXE
(launcher/tests/stand_in_child.c + shell/tests/chain_fake_dbsim.py), each in
its own temporary game directory:

a. Continue (slot 10 = the pinned GAME_R.SAV without its copies, start-up
   draws 71) -> Rock & Roll -> DBSIM copies the recorded c1_01-success output
   -> results and debrief -> Quit. The same steps through es2chain (init
   --save GAME_R.SAV --seed 71, step) must give the same first mission (C1_01
   = pins, RNG, selectors); the -X3 process restarts the RNG (reset + 71, as
   VSHELL), so the results path (summary, SAV, debrief, RNG) and C1_02
   are compared with our results path and preparation from that state
   (es2chain carries the RNG); the menu's results path starts from the SAV
   after the preparation (slot 10 at the exit for DBSIM);
   DATA/MISSION.STR is C1_01's, the debrief's and C1_02's written over each
   other without truncation;
   slot 10 (GAMEFILE.STR, SAV, the three launch-file copies) equals VSHELL's
   own save 0x40e933 of the same state in the mapped original (slot mode of
   the memory worker); prefs.cfg as the save rules say; the debrief text is
   the results path's; the labels are the installation's ESTEXT.BIN.
b. New campaign (name, skill) -> Rock & Roll -> synthetic DBSIM results ->
   Quit: the new-career SAV (final word 2, salvage 100,000 + 1,000 x draw(11)
   after the 58 roster and player draws), then the same comparison with
   es2chain started from that SAV and RNG.
c. Continue of the menu's own slot reuses the campaign and launches with
   0x40f68c's mission.var; a DBSIM end without results (-X1) reads and writes
   nothing (VSHELL's fresh start); the next
   CONTINUE and Rock & Roll hand DBSIM the slot's copies again.
d. Saved games without the launcher: SAVE/RESTORE refused without a
   game; new career, SAVE into slot 3, EXIT, QUIT; RESTORE of slot 3 (reuses
   the campaign), a refused name, SAVE with CANCEL; CONTINUE of a slot 10
   whose final word is 0. Every SAV directory file and the DATA copies equal
   the original save/load of the same operations on the same files.
e. The service bay VSHELL shows after a new career, CONTINUE, RESTORE
   and a continuing results path, and its WEAPONS screen: without the
   launcher a new career, mounts, a disabled row, a guidance mode, WEAPONS
   twice, SAVE (leaving WEAPONS runs 0x412a37), MAIN MENU, QUIT: every SAV
   file equals the original mounts (0x411b10), guidance (0x43e346),
   rebalance (0x412a37) and saves (0x40e933) of the same choices. Through the
   launcher: CONTINUE, a mount, MISSION -> DBSIM: the files DBSIM was handed
   are the prepared script/text/bank with the player.mec of the original
   0x40f68c, the results path starts from the changed SAV.
f. Rock & Roll with the player's Herc emptied: the warning box
   (0x13e/0x13f), no launch, as the original handler 0x445b99 decides for
   the same state. A lost mission (stand-in DBSIM: the player killed, its
   own mission.var): the Replay box; Yes loads slot 10 and answers 2 (SAV
   directory and DATA as the original 0x44d1d4 and the exit save leave
   them, DBSIM's mission.var kept), the replayed mission's results path
   starts from the loaded state; No saves slot 10 as 0x44d24d does, a later
   CONTINUE shows the End of game box (0x12f) as 0x431a73 does and the exit
   save rewrites slot 10 as VSHELL's. CONTINUE on a slot table without the
   slot-10 flag: the End of game box with the final word 0 in memory (0x130),
   nothing loaded, nothing saved, as 0x431a73.
   Part 1 also covers the Preferences: music and sound effects (bytes 0
   and 1 toggled), resolution (byte 4), full screen through the Alert box,
   and Cancel reverting the mode byte a main-menu entry stored.

The slot table at the start is the CD's with the flag of slot 10 set (a
played installation); VSHELL loads slot 10 only when that flag is set.

Arguments: es2menu backend es2chain es2prep_core es2launch stand_in_child
chain_fake_dbsim.py reference_pins.py es2shellprep slots_oracle.py
"""
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

LAUNCH_FILES = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec')
COMPANIONS = (('script.dat', 'script10.dat'), ('MISSION.STR', 'missn10.str'), ('player.mec', 'player10.mec'))
GAME_R_SEED = 71
PREFS = bytes((7 * i + 3) & 0xff for i in range(54))   # own synthetic bytes


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def scratch_base():
    """Outside the execroot (its .git link makes the chain refuse directories)."""
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


def str_groups(data, groups):
    """Packed STR with tag bytes (docs/formats/str.md): [(text, tag)] per group."""
    at, out = 4, []
    for _ in range(groups):
        count, = struct.unpack_from('<h', data, at)
        at += 2
        entries = []
        for _ in range(count):
            length, = struct.unpack_from('<h', data, at)
            entries.append((data[at + 2:at + 1 + length], data[at + 2 + length]))
            at += 3 + length
        out.append(entries)
    return out, at


def played_gamefile(data):
    """The slot table with the flag (tag byte) of group-1 entry 10 set."""
    (groups, _) = str_groups(data, 2)   # texts without their NUL: an entry is 2 + len + 1 + 1 bytes
    at = 4 + 2 + sum(4 + len(text) for text, _ in groups[0]) + 2
    at += sum(4 + len(text) for text, _ in groups[1][:10])
    at += 2 + len(groups[1][10][0]) + 1
    out = bytearray(data)
    out[at] = 1
    return bytes(out)


def estext(vol, language, index):
    """ESTEXT.BIN entry from a VOLN archive (docs/formats/vol.md), own reader."""
    size, = struct.unpack_from('<H', vol, 10)
    directories = vol[12:12 + size].split(b'\0')[:-1]
    count, = struct.unpack_from('<H', vol, 12 + size)
    for i in range(count):
        record = 18 + size + 18 * i
        name = vol[record:record + 13].split(b'\0')[0]
        if name.upper() == b'ESTEXT.BIN' and directories[vol[record + 13]].upper() == language.encode() + b'\\':
            block, = struct.unpack_from('<I', vol, record + 14)
            length, = struct.unpack_from('<I', vol, block + 1)
            payload = vol[block + 9:block + 9 + length]
            n, = struct.unpack_from('<I', payload, 0)
            offset, = struct.unpack_from('<H', payload, 8 + 2 * index)
            text = payload[8 + 2 * n + offset:payload.index(b'\0', 8 + 2 * n + offset)]
            return text.rstrip(b'\r\n')
    raise AssertionError('ESTEXT.BIN not found')


class Test:
    def __init__(self, argv):
        (self.menu_exe, self.backend, self.chain_py, self.core, self.launcher, self.stand_in, self.fake,
         pins_py, self.worker, oracle_py) = [Path(a).resolve() for a in argv]
        self.scratch = tempfile.TemporaryDirectory(prefix='es2menu-', dir=scratch_base())
        self.tmp = Path(self.scratch.name)
        self.failures, self.report = [], {}
        self.pins = load(pins_py, 'menu_pins')
        self.oracle = load(oracle_py, 'menu_slots_oracle')
        self.oracle_runs = 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)

    def run_menu(self, game, menu, state, script, extra=()):
        argv = [str(self.menu_exe), '--menu', str(menu), *extra, 'dummy', '-eggplant', f'-X{state}', '-G']
        run = subprocess.run(argv, cwd=game, input=script.replace('|', '\n').encode('ascii'),
                             capture_output=True, timeout=600)
        return run.returncode, run.stdout

    # ---- part 1: stand-in backend ------------------------------------------------
    def synthetic(self):
        game = self.tmp / 'synthetic' / 'game'
        (game / 'DATA').mkdir(parents=True)
        (game / 'DATA' / 'PREFS.CFG').write_bytes(PREFS)
        menu = self.tmp / 'synthetic' / 'menu'
        menu.mkdir()
        stand_in = self.tmp / 'synthetic' / 'backend.py'
        stand_in.write_text(
            'import sys\n'
            'args = sys.argv[1:]\n'
            'answer = args[args.index("--answer") + 1]\n'
            'open(args[args.index("--menu") + 1] + "/calls.txt", "a").write(" ".join(args[:1]) + "\\n")\n'
            'open(answer, "w").write("OK\\nCAMPAIGN -\\n" if args[0] == "status" else "ERROR stand-in\\n")\n',
            encoding='ascii')
        def ini(game_dir):
            (menu / 'menu.ini').write_text(f'[Menu]\npython = {sys.executable}\nbackend = {stand_in}\n'
                                           f'game_dir = {game_dir}\n', encoding='ascii')
        ini(game)
        code, out = self.run_menu(game, menu, 1, '')
        self.check(code == 0 and b'choice> ' in out, f'end of input: exit {code}')
        # Entry mode 1 plays the intro (German directory), CREDITS its
        # movie; -a, --no-movies and entry mode 6 print none
        self.check(b'[movie avg\\intr_pt1.avi]' in out and b'[movie avg\\intr_pt2.avi]' in out and
                   out.index(b'intr_pt1') < out.index(b'intr_pt2') < out.index(b'choice> '), 'intro movies')
        code, out = self.run_menu(game, menu, 6, '10|5')
        self.check(code == 0 and out.count(b'[movie') == 1 and b'[movie avi\\credits.avi]' in out,
                   'CREDITS: credits.avi only')
        for extra in (('-a',), ('--no-movies',)):
            code, out = self.run_menu(game, menu, 0, '10|5', extra)
            self.check(code == 0 and b'[movie' not in out, f'{extra[0]}: no movie')
        code, out = self.run_menu(game, menu, 1, '3|5')
        self.check(code == 0 and b'no campaign loaded' in out, 'Start mission without a campaign')
        code, out = self.run_menu(game, menu, 1, '19|5')   # 7..9 are practice, instant action, demo
        self.check(code == 0 and b'? 1..6' in out, 'unknown choice')
        # Preferences: repair 3rd value, weapons 2nd, display 2nd, Accept.
        code, out = self.run_menu(game, menu, 1, '4|1|3|2|2|3|2|4|5')
        prefs = (game / 'DATA' / 'PREFS.CFG').read_bytes()
        want = bytearray(PREFS)
        want[0x2c], want[0x2d], want[6] = 2, 1, 1
        self.check(code == 0 and prefs == bytes(want), 'Preferences Accept writes +0x2c, +0x2d, +6')
        code, out = self.run_menu(game, menu, 1, '4|1|1|2|1|5|5')
        self.check(code == 0 and (game / 'DATA' / 'PREFS.CFG').read_bytes() == bytes(want), 'Preferences Cancel writes nothing')
        # A short prefs file: Accept writes the zero-extended 54-byte block.
        (game / 'DATA' / 'PREFS.CFG').write_bytes(PREFS[:10])
        code, out = self.run_menu(game, menu, 1, '4|4|5')
        self.check((game / 'DATA' / 'PREFS.CFG').read_bytes() == PREFS[:10] + bytes(44), 'Accept on a short prefs file')
        # START NEW GAME stores mode 1 at +0x2a only (read-modify-write), even
        # when the registration is not completed (empty name).
        (game / 'DATA' / 'PREFS.CFG').write_bytes(PREFS)
        code, out = self.run_menu(game, menu, 1, '1||5')
        want = bytearray(PREFS)
        want[0x2a] = 1
        self.check(code == 0 and b'printable' in out and (game / 'DATA' / 'PREFS.CFG').read_bytes() == bytes(want),
                   'New campaign: mode byte and name check')
        # Music, sound effects (toggles of bytes 0 and 1), Low Res (byte
        # 4 := 1), full screen through the Alert box, Accept.
        (game / 'DATA' / 'PREFS.CFG').write_bytes(PREFS)
        code, out = self.run_menu(game, menu, 1, '4|6|7|8|2|3|2|4|5')
        want = bytearray(PREFS)
        want[0] ^= 1
        want[1] ^= 1
        want[4], want[6] = 1, 1
        self.check(code == 0 and (game / 'DATA' / 'PREFS.CFG').read_bytes() == bytes(want),
                   'Preferences: music, sound effects, resolution, full screen')
        # Cancel reverts every byte stored after the start-up load, also the
        # mode byte START NEW GAME wrote (0x40ddb6); a later Accept writes the
        # reverted block (0x40dd0a): the file is the start-up one again.
        (game / 'DATA' / 'PREFS.CFG').write_bytes(PREFS)
        code, out = self.run_menu(game, menu, 1, '1||4|6|5|4|4|5')
        self.check(code == 0 and (game / 'DATA' / 'PREFS.CFG').read_bytes() == PREFS,
                   'Preferences: Cancel reverts the stored mode byte')
        code, out = self.run_menu(game, menu, 1, '1|PILOT|7|5')
        self.check(code == 0 and b'no such skill' in out, 'New campaign: skill check')
        # The registration field's set 0x476205 (letters, digits, space)
        code, out = self.run_menu(game, menu, 1, '1|PILOT_1|5')
        self.check(code == 0 and b'name field takes' in out and b'skill> ' not in out,
                   'New campaign: the registration field refuses a character outside its set')
        # The new career runs in es2menu itself; without cd_root in
        # menu.ini the engine's error is shown and nothing is launched.
        code, out = self.run_menu(game, menu, 1, '1|PILOT|1|1|5')
        self.check(code == 0 and b'! menu.ini names no cd_root' in out, 'New campaign: engine error shown, no exit 2')
        # The start-up CD check with a cd_root that has no avi\pt1.avi:
        # error.str message 5, exit 0, no menu; with a synthetic pt1.avi the
        # menu starts; cd_check = skip turns the check off.
        cd = self.tmp / 'synthetic-cd'
        (cd / 'avi').mkdir(parents=True)
        ini(game)
        with open(menu / 'menu.ini', 'a', encoding='ascii') as f:
            f.write(f'cd_root = {cd}\n')
        code, out = self.run_menu(game, menu, 1, '5')
        self.check(code == 0 and b'error.str message 5' in out and b'choice>' not in out, 'CD check: no pt1.avi quits')
        (cd / 'avi' / 'pt1.avi').write_bytes(b'synthetic')
        code, out = self.run_menu(game, menu, 1, '5')
        self.check(code == 0 and b'error.str message' not in out and b'choice>' in out, 'CD check: pt1.avi opens')
        (cd / 'avi' / 'pt1.avi').unlink()
        with open(menu / 'menu.ini', 'a', encoding='ascii') as f:
            f.write('cd_check = skip\n')
        code, out = self.run_menu(game, menu, 1, '5')
        self.check(code == 0 and b'error.str message' not in out and b'choice>' in out, 'CD check: skip')
        ini(game)
        # Refusals answer 0 without a menu.
        code, out = self.run_menu(self.tmp, menu, 1, '5')
        self.check(code == 0 and b'menu.ini names' in out and b'choice>' not in out, 'wrong directory refused')
        native = self.tmp / 'synthetic' / 'native-r111'
        (native / 'DATA').mkdir(parents=True)
        ini(native)
        code, out = self.run_menu(native, menu, 1, '5')
        self.check(code == 0 and b'primary installation' in out and b'choice>' not in out, 'native-r111 refused')
        # menu.ini's primary = allowed (configure-shell.ps1 -AllowPrimary) lifts the refusal; another value is a bad menu.ini.
        with open(menu / 'menu.ini', 'a', encoding='ascii') as f:
            f.write('primary = allowed\n')
        code, out = self.run_menu(native, menu, 1, '5')
        self.check(b'primary installation' not in out and b'menu.ini names' not in out, 'native-r111 allowed')
        ini(native)
        with open(menu / 'menu.ini', 'a', encoding='ascii') as f:
            f.write('primary = yes\n')
        code, out = self.run_menu(native, menu, 1, '5')
        self.check(b'choice>' not in out, 'primary = yes refused')
        ini(game)
        run = subprocess.run([str(self.menu_exe), '--menu', str(menu), '-X'], cwd=game, capture_output=True, timeout=60)
        self.check(run.returncode == 0, 'bad arguments answer 0')
        code, out = self.run_menu(game, menu, 3, '5')
        self.check(code == 0 and b'no mission was handed' in out, '-X3 without a launched mission')
        # es2menu starts no backend (menu.ini still names the stand-in).
        self.check(not (menu / 'calls.txt').exists(), 'S-22: es2menu started the backend')
        self.report['synthetic'] = 'passed' if not self.failures else 'failed'

    # ---- part 2: real backend --------------------------------------------------------
    def game(self, name, delegate):
        game = self.tmp / name / 'game'
        for sub in ('DATA', 'SAV', 'VOL'):
            (game / sub).mkdir(parents=True)
        (game / 'DATA' / 'language.cfg').write_bytes(b'G')
        shutil.copyfile(self.cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
        (game / 'SAV' / 'GAMEFILE.STR').write_bytes(self.gamefile_start)
        shutil.copyfile(self.cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
        (game / 'SAV' / 'GAME_R.SAV').write_bytes(self.game_r)
        body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
        body += struct.pack('<hh', 1, 2) + b'c\0\0'
        (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
        shutil.copyfile(self.stand_in, game / 'dbsim.EXE')
        (game / 'dbsim.delegate').write_text(delegate + '\n', encoding='ascii')
        return game

    def fake_command(self, chain_dir, recorded):
        command = f'"{sys.executable}" -B "{self.fake}" --chain "{chain_dir}"'
        if recorded:
            command += f' --recorded "{self.golden / "results.dat"}" "{self.golden / "mission.var"}"'
        return command

    def launch(self, game, shell):
        log = game / 'launch.log'
        env = dict(os.environ, ES2_LAUNCH_SHELL=shell, ES2_LAUNCH_LOG=str(log), ES2_LAUNCH_NO_UI='1')
        env.pop('ES2_LAUNCH_SHELL_CONSOLE', None)
        run = subprocess.run([str(self.launcher)], cwd=game, env=env, timeout=1500)
        self.check(run.returncode == 0, f'{game}: launcher exit {run.returncode}')
        return [line.split(' ', 1)[1] for line in log.read_text(encoding='ascii').splitlines()][1:]

    def menu_session(self, name, script, recorded):
        menu = self.tmp / name / 'menu'
        game = self.game(name, self.fake_command(menu / 'campaign-000', recorded))
        setup = subprocess.run([sys.executable, '-B', str(self.backend), 'setup', '--menu', str(menu),
                                '--menu-exe', str(self.menu_exe), '--core', str(self.core), '--cd-root', str(self.cd),
                                '--game-dir', str(game), '--seed', str(GAME_R_SEED)],
                               capture_output=True, text=True, check=True)
        shell = json.loads(setup.stdout)['launch_shell']
        (self.tmp / name / 'input.txt').write_text(script.replace('|', '\n') + '\n', encoding='ascii')
        lines = self.launch(game, shell + f' --input "{self.tmp / name / "input.txt"}"')
        want = [f'start shell-replacement {shell} --input "{self.tmp / name / "input.txt"}" dummy -eggplant -X1 -G',
                'exit shell-replacement 2', 'start dbsim.EXE dummy -eggplant -X2 -R0 -G', 'exit dbsim.EXE 3',
                f'start shell-replacement {shell} --input "{self.tmp / name / "input.txt"}" dummy -eggplant -X3 -G',
                'exit shell-replacement 0', 'end 0']
        self.check(lines == want, f'{name}: launcher log {lines}')
        return game, menu, menu / 'campaign-000'

    def chain_session(self, name, save, recorded, rng=None):
        state = self.tmp / name / 'chain'
        game = self.game(name, self.fake_command(state, recorded))
        (self.tmp / name / 'start.sav').write_bytes(save)
        extra = ['--seed', str(GAME_R_SEED)]
        if rng is not None:
            (self.tmp / name / 'start.rng').write_bytes(rng)
            extra = ['--rng-state', str(self.tmp / name / 'start.rng')]
        init = subprocess.run([sys.executable, '-B', str(self.chain_py), 'init', '--core', str(self.core),
                               '--cd-root', str(self.cd), '--state', str(state), '--game-dir', str(game),
                               '--save', str(self.tmp / name / 'start.sav'), '--missions', '1',
                               '--hangar', 'lowest-free', *extra],
                              capture_output=True, text=True, check=True)
        self.launch(game, json.loads(init.stdout)['launch_shell'])
        return state

    def compare(self, label, menu_state, chain_state):
        """Launch files, DBSIM files, SAVs, RNG/selectors and summaries equal."""
        mine = json.loads((menu_state / 'chain.json').read_text(encoding='utf-8'))
        theirs = json.loads((chain_state / 'chain.json').read_text(encoding='utf-8'))
        self.check(len(mine['missions']) == len(theirs['missions']) == 2, f'{label}: mission count')
        # The first mission (prepared in the -X1 process from the start-up
        # RNG) as es2chain prepares it.
        for a, b in zip(mine['missions'][:1], theirs['missions'][:1]):
            for key in ('mission', 'group', 'position', 'files', 'input_rng', 'output_rng',
                        'input_selectors', 'output_selectors', 'hangar'):
                self.check(a[key] == b[key], f'{label} mission {a["index"]}: {key} differs from es2chain')
            for n in LAUNCH_FILES:
                self.check((menu_state / f'mission-{a["index"]:03d}' / n).read_bytes() ==
                           (chain_state / f'mission-{b["index"]:03d}' / n).read_bytes(), f'{label}: {n} differs')
        for key in ('mission', 'group', 'position', 'hangar'):
            self.check(mine['missions'][1][key] == theirs['missions'][1][key],
                       f'{label} mission 1: {key} differs from es2chain')
        # vshell_preparation.md: the -X3 process restarts the RNG (reset + the start-up
        # draws, menu.ini seed 71), as VSHELL does; es2chain carries the state
        # of the first preparation. The results path and the second
        # preparation are therefore compared with our own results path and
        # preparation from the recorded start (chain_real_test replays both
        # through the original with ES2_CHAIN_VERIFY).
        played = mine['missions'][0]
        reset_rng = load(self.chain_py, f'menu_prep_{label}').prep.initial_rng(self.cd, GAME_R_SEED)
        self.check(played.get('results_rng') == reset_rng.hex() and played.get('seed') == GAME_R_SEED,
                   f'{label}: the results path did not start from the reset RNG')
        # A campaign from a slot starts from the loaded state (item lists
        # reversed, stale tail dropped); a new career from its SAV.
        start = (chain_state / 'save-000.sav').read_bytes()
        if mine['menu']['source']['kind'] == 'slot':
            start = self.memory(start)
        self.check((menu_state / 'save-000.sav').read_bytes() == start, f'{label}: save-000.sav differs')
        # The menu's results path starts from the SAV after the
        # preparation (what VSHELL's slot 10 holds when DBSIM starts); es2chain
        # starts from the SAV before it. Same results path, other input.
        chain_module = load(self.chain_py, f'menu_chain_{label}')
        engine = chain_module.Engine(self.core, self.cd)
        prepared = mine['missions'][0].get('prepared_save')
        self.check(prepared == 'save-000-prepared.sav' and played.get('results_save') == prepared,
                   f'{label}: no SAV after the first preparation')
        if prepared:
            out = engine.advance((menu_state / prepared).read_bytes(),
                                 (menu_state / 'mission-000' / 'dbsim' / 'mission.var').read_bytes(),
                                 (menu_state / 'mission-000' / 'dbsim' / 'results.dat').read_bytes(),
                                 reset_rng, 'GER', mine['repair_option'], mine['restock_option'])
            self.check(out.get('sav') == (menu_state / 'save-001.sav').read_bytes(),
                       f'{label}: save-001.sav is not the results path of the prepared SAV')
            want = engine.summary(out, 'GER')
            got = played['advance']
            self.check({k: v for k, v in got.items() if k not in ('save', 'dbsim')} ==
                       {k: v for k, v in want.items() if k != 'save'} and
                       got['save']['sha256'] == want['save']['sha256'],
                       f'{label}: results summary differs from our results path')
            self.check(got['dbsim'] == theirs['missions'][0]['advance']['dbsim'], f'{label}: DBSIM files differ')
            # The second preparation from that path's SAV and RNG.
            second = mine['missions'][1]
            self.check(second['input_rng'] == out['rng'].hex() and second['input_save'] == 'save-001.sav',
                       f'{label} mission 1: not prepared from the results path\'s state')
            request = chain_module.prep.request(group=second['group'], position=second['position'], mode=1,
                                                language='GER', rng=out['rng'],
                                                selectors=bytes.fromhex(played['output_selectors']),
                                                tables=engine.tables(), save=out['sav'], prepared_save=True)
            again, = chain_module.prep.prepare_batch(self.core, self.cd, [request])
            self.check('error' not in again and
                       all((menu_state / 'mission-001' / n).read_bytes() == again[n] for n in LAUNCH_FILES) and
                       second['output_rng'] == again['rng'].hex() and
                       second['output_selectors'] == again['selectors'].hex() and
                       (menu_state / second['prepared_save']).read_bytes() == again['prepared_save'],
                       f'{label} mission 1: preparation differs from ours')
        for n in ('results.dat', 'mission.var'):
            self.check((menu_state / 'mission-000' / 'dbsim' / n).read_bytes() ==
                       (chain_state / 'mission-000' / 'dbsim' / n).read_bytes(), f'{label}: DBSIM {n} differs')
        return mine

    def oracle_slots(self, label, disk, ops, save=None, mode=1, emit=False):
        """VSHELL's own save/load functions on a memory disk (slots_oracle)."""
        self.oracle_runs += 1
        return self.oracle.run(self.worker, self.cd, disk, ops, self.tmp / 'oracle' / f'{label}-{self.oracle_runs}',
                               save=save, language='G', mode=mode, emit=emit)

    def same_disk(self, label, game, result, names):
        disk = {key.lower(): value for key, value in result['disk'].items()}
        for name in names:
            mine = game / 'SAV' / name
            theirs = disk.get('sav\\' + name.lower())
            ours = mine.read_bytes() if mine.is_file() else None
            where = '-' if theirs is None or ours is None else \
                next((i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]), min(len(ours), len(theirs)))
            self.check(theirs is not None and ours == theirs,
                       f'{label}: SAV/{name} differs from VSHELL\'s save (sizes {None if ours is None else len(ours)}/'
                       f'{None if theirs is None else len(theirs)}, first difference at {where})')

    def data_disk(self, game):
        return {'data\\script.dat': (game / 'DATA' / 'script.dat').read_bytes(),
                'data\\mission.str': (game / 'DATA' / 'MISSION.STR').read_bytes(),
                'data\\player.mec': (game / 'DATA' / 'player.mec').read_bytes()}

    def memory(self, sav):
        """The file whose restore by the original readers gives the state
        `sav` describes: the worker can only start from a restored SAV, and
        a restore reverses every item list (es2_shell_loaded_save, an
        involution on writer images, checked against the original in
        slots_real_test)."""
        run = subprocess.run([str(self.core), str(self.cd), '--loaded-save'], input=sav.hex() + '\n',
                             capture_output=True, text=True, check=True).stdout.splitlines()
        return bytes.fromhex(run[1])

    def check_slot(self, label, game, menu_state):
        """Slot 10 after the exit equals VSHELL's save 0x40e933(10, 0) of the
        campaign's current SAV over the same save directory and DATA."""
        record = json.loads((menu_state / 'chain.json').read_text(encoding='utf-8'))
        disk = {'sav\\gamefile.str': self.gamefile_start, 'sav\\GAME_R.SAV': self.game_r, **self.data_disk(game)}
        result = self.oracle_slots(label, disk, ['save:10'],
                                   save=self.memory((menu_state / record['save']).read_bytes()))
        self.same_disk(label, game, result, ('GAMEFILE.STR', 'GAME_R.SAV', 'script10.dat', 'missn10.str', 'player10.mec'))
        prefs = bytearray((self.cd / 'DATA' / 'PREFS.CFG').read_bytes())
        prefs[0x2a] = 1
        self.check((game / 'DATA' / 'PREFS.CFG').read_bytes() == bytes(prefs), f'{label}: prefs.cfg mode byte')

    def continue_recorded(self):
        game, menu, state = self.menu_session('continue', '2|4|1|5', True)
        chain = self.chain_session('continue-chain', self.game_r, True)
        mine = self.compare('continue', state, chain)
        first = {n: (state / 'mission-000' / n).read_bytes() for n in LAUNCH_FILES}
        self.check(all(self.pins.matches('DATA/' + n, d) for n, d in first.items()), 'continue: C1_01 differs from the pins')
        self.check(len(mine['missions']) > 1 and mine['missions'][1]['mission'] == 'C1_02', 'continue: C1_02 expected next')
        self.check_slot('continue', game, state)
        # The debrief text is the results path's; the transcript shows it and
        # the installation's labels.
        engine = load(self.chain_py, 'menu_chain').Engine(self.core, self.cd)
        out = engine.advance(self.game_r, (self.golden / 'mission.var').read_bytes(),
                             (self.golden / 'results.dat').read_bytes(),
                             bytes.fromhex(mine['missions'][0]['results_rng']), 'GER', 0, 0)
        debrief = (state / 'mission-000' / 'debrief.txt').read_bytes()
        transcript = (menu / 'menu.log').read_bytes()
        # DATA/MISSION.STR after the -X3 process: C1_01's, the debrief's
        # (0x418014 in the results path) and C1_02's written over each other
        # without truncation (file_io.md); slot 10 holds its copy.
        def over(old, new):
            return new + old[len(new):]
        played = mine['missions'][0]
        path = engine.advance((state / played['results_save']).read_bytes(),
                              (state / 'mission-000' / 'dbsim' / 'mission.var').read_bytes(),
                              (state / 'mission-000' / 'dbsim' / 'results.dat').read_bytes(),
                              bytes.fromhex(played['results_rng']), 'GER', mine['repair_option'], mine['restock_option'])
        want = (state / 'mission-000' / 'MISSION.STR').read_bytes()
        if path.get('debrief'):
            want = over(want, path['strings'])
        want = over(want, (state / 'mission-001' / 'MISSION.STR').read_bytes())
        self.check(path.get('debrief') and (game / 'DATA' / 'MISSION.STR').read_bytes() == want and
                   (game / 'SAV' / 'missn10.str').read_bytes() == want, 'continue: DATA/MISSION.STR after the results path')
        self.check(debrief == out['text'] and len(debrief) > 0, 'continue: debrief text differs from the results path')
        self.check(debrief in transcript, 'continue: debrief text not shown')
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        for index in (0x04, 0x05, 0x0a, 0x0e, 0xb8, 0xb9):
            self.check(estext(vol, 'GER', index) in transcript, f'continue: label {index:#x} missing')
        self.report['continue'] = dict(missions=[m['mission'] for m in mine['missions']],
                                       advance={k: mine['missions'][0]['advance'][k] for k in ('ending', 'advance', 'next')},
                                       next_files={n: v['sha256'] for n, v in mine['missions'][1]['files'].items()})
        return game, menu, state

    def new_campaign(self):
        game, menu, state = self.menu_session('new', '1|TESTPILOT|2|1|4|1|5', False)
        prep = load(self.chain_py, 'menu_chain_new').prep
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        sav = (state / 'save-000.sav').read_bytes()
        # Independent model of the additive generator: 58 roster/player draws,
        # then the salvage draw.
        start = prep.initial_rng(self.cd, GAME_R_SEED)
        words, a, b = list(struct.unpack('<56H', start[:112])), start[112], start[113]
        def word():
            nonlocal a, b
            words[b] = (words[a] + words[b]) & 0xffff
            value = words[b]
            a, b = (a + 1) % 56, (b + 1) % 56
            return value
        for _ in range(58):
            word()
        salvage = 100000 + 1000 * ((word() & 0x7fff) % 11)
        rng = struct.pack('<56HBB', *words, a, b)
        self.check(record['initial']['rng_sha256'] == sha(rng), 'new: RNG after the new-career draws')
        self.check(record['menu']['source']['salvage'] == salvage, 'new: salvage total')
        describe = subprocess.run([str(self.core), str(self.cd), '--describe-save'], input=sav.hex() + '\n',
                                  capture_output=True, text=True, check=True).stdout.splitlines()
        player = next(line.split() for line in describe if line.startswith('PLAYER '))
        size = next(line.split() for line in describe if line.startswith('SIZE '))
        self.check(bytes.fromhex(player[-1]) == b'TESTPILOT' and player[6] == '1', 'new: player name and rank')
        end = int(size[2])
        self.check(struct.unpack_from('<I', sav, end - 2026)[0] == salvage and
                   struct.unpack_from('<h', sav, end - 22)[0] == 2 and sav[end - 2022:end - 22] == bytes(2000),
                   'new: tally, final word 2, zero bank')
        self.check(record['missions'][0]['mission'] == 'C1_01', 'new: first mission C1_01')
        chain = self.chain_session('new-chain', sav, False, bytes.fromhex(record['missions'][0]['input_rng']))
        mine = self.compare('new', state, chain)
        self.check_slot('new', game, state)
        self.report['new'] = dict(salvage=salvage, missions=[m['mission'] for m in mine['missions']],
                                  advance={k: mine['missions'][0]['advance'][k] for k in ('ending', 'advance', 'next')})

    def reuse_and_abort(self, game, menu, state):
        before = len(list(menu.glob('campaign-*')))
        code, out = self.run_menu(game, menu, 1, '2|4')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        self.check(code == 2 and len(list(menu.glob('campaign-*'))) == before and
                   record['pending']['index'] == 1 and record['pending'].get('launched'),
                   'reuse: Continue of the own slot reuses the campaign and launches C1_02')
        # Rock & Roll wrote 0x40f68c's mission.var of the loaded state: the
        # prepared bank of C1_02.
        prepared = (state / 'mission-001' / 'mission.var').read_bytes()
        self.check((game / 'DATA' / 'mission.var').read_bytes() == prepared, 'reuse: DATA/mission.var of the launch')
        # A start without results (-X1) is VSHELL's fresh start: it
        # reads and writes nothing (VSHELL does not put the prepared files
        # back; the next CONTINUE and Rock & Roll do). The log
        # records the start.
        (game / 'DATA' / 'mission.var').write_bytes(b'written by an interrupted DBSIM')
        saves, data = self.save_dir(game), {n: (game / 'DATA' / n).read_bytes() for n in LAUNCH_FILES}
        code, out = self.run_menu(game, menu, 1, '5')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        self.check(code == 0 and b'without results' in out and not record['pending'].get('launched') and
                   record['starts'][-1]['action'] == 'abort' and self.save_dir(game) == saves and
                   {n: (game / 'DATA' / n).read_bytes() for n in LAUNCH_FILES} == data,
                   'abort: DBSIM end without results reads and writes nothing')
        # CONTINUE and Rock & Roll hand DBSIM the mission again: the slot's
        # copies and 0x40f68c's mission.var and player.mec.
        code, out = self.run_menu(game, menu, 1, '2|4')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        self.check(code == 2 and record['pending']['index'] == 1 and record['pending'].get('launched') and
                   (game / 'DATA' / 'mission.var').read_bytes() == prepared and
                   all((game / 'DATA' / d).read_bytes() == (game / 'SAV' / c).read_bytes() for d, c in COMPANIONS),
                   'abort: CONTINUE and Rock & Roll launch the mission again')

    # ---- The Saved games screen against VSHELL's own save and load ------------------
    def direct_game(self, name):
        """Game and menu directory for menu runs without the launcher."""
        menu = self.tmp / name / 'menu'
        game = self.game(name, 'unused')
        subprocess.run([sys.executable, '-B', str(self.backend), 'setup', '--menu', str(menu),
                        '--menu-exe', str(self.menu_exe), '--core', str(self.core), '--cd-root', str(self.cd),
                        '--game-dir', str(game), '--seed', str(GAME_R_SEED)],
                       capture_output=True, text=True, check=True)
        return game, menu

    def save_dir(self, game):
        return {p.name: p.read_bytes() for p in (game / 'SAV').iterdir()}

    def saved_games(self):
        game, menu = self.direct_game('slots')
        before = self.save_dir(game)
        # Without a loaded game SAVE is refused, RESTORE of an empty slot too;
        # nothing is written (no loaded game, no save on exit).
        code, out = self.run_menu(game, menu, 1, '6|1|2|4|3|5')
        self.check(code == 0 and b'no game loaded' in out and b'slot 4 holds no game' in out and
                   self.save_dir(game) == before, 'slots: SAVE and RESTORE refused without a game')
        # New career, the bay's MAIN MENU (saves slot 10), SAVE into slot 3
        # as "ALPHA 7", EXIT, QUIT: slot 3 and slot 10 as VSHELL's 0x40e933
        # writes them for the campaign's SAV.
        code, out = self.run_menu(game, menu, 1, '1|TESTPILOT|2|1|1|6|1|3|ALPHA 7|1|3|5')
        state = menu / 'campaign-000'
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        self.check(code == 0 and record['save'] == 'save-000-prepared.sav', 'slots: new career prepared')
        disk = {'sav\\gamefile.str': before['GAMEFILE.STR'], 'sav\\GAME_R.SAV': before['GAME_R.SAV'],
                **self.data_disk(game)}
        result = self.oracle_slots('slots-save', disk, ['save:10', 'save:2: 3. ALPHA 7', 'save:10'],
                                   save=self.memory((state / record['save']).read_bytes()))
        self.same_disk('slots: SAVE slot 3 and exit', game, result,
                       ('GAMEFILE.STR', 'GAME_2.SAV', 'script2.dat', 'missn2.str', 'player2.mec', 'GAME_R.SAV',
                        'script10.dat', 'missn10.str', 'player10.mec'))
        written = {k.split('\\', 1)[1].lower() for k in result['disk'] if k.lower().startswith('sav\\')}
        self.check({n.lower() for n in self.save_dir(game)} == written, 'slots: the files in SAV are VSHELL\'s')
        # Restart: RESTORE slot 3 (loads it, saves it as slot 10, reuses the
        # campaign, service bay), the bay's SAVE: a refused name, SAVE with
        # CANCEL, EXIT; the bay's MAIN MENU, QUIT.
        before = self.save_dir(game)
        stale = {'script.dat': b'stale script', 'MISSION.STR': b'stale text', 'player.mec': b'stale mec'}
        for name, data in stale.items():
            (game / 'DATA' / name).write_bytes(data)
        code, transcript = self.run_menu(game, menu, 1, '6|2|3|2|1|5|bad_name|1|5|OTHER|2|3|1|5')
        self.check(code == 0 and b'fit its width' in transcript and b' 3. ALPHA 7' in transcript and
                   not (menu / 'campaign-001').exists(), 'slots: RESTORE reuses the campaign, a bad name is refused')
        # RESTORE saves slot 10 at once (0x438393), the bay's MAIN MENU and
        # the exit once more each.
        self.check(transcript.count(b'[saved slot 10: SAV\\GAME_R.SAV]') == 3,
                   'slots: RESTORE, MAIN MENU and exit save slot 10')
        disk = {'sav\\' + k: v for k, v in before.items()}
        disk.update({'data\\' + k.lower(): v for k, v in stale.items()})
        result = self.oracle_slots('slots-restore', disk, ['load:2:1', 'save:10', 'save:10', 'save:10'])
        self.same_disk('slots: RESTORE and exit', game, result,
                       ('GAMEFILE.STR', 'GAME_2.SAV', 'GAME_R.SAV', 'script10.dat', 'missn10.str', 'player10.mec'))
        theirs = {k.lower(): v for k, v in result['disk'].items()}
        self.check(all((game / 'DATA' / n).read_bytes() == theirs['data\\' + n.lower()] for n in stale),
                   'slots: RESTORE copies the slot files to DATA')
        # CONTINUE of a slot 10 whose final word is not 2: the end text, the
        # game stays loaded and the exit rewrites slot 10 as VSHELL does. The
        # slot's companions are prepared launch files (the load composes the
        # briefing texts from the copied MISSION.STR, 0x4135bb).
        companions = {slot_name: (game / 'DATA' / data_name).read_bytes() for data_name, slot_name in COMPANIONS}
        game, menu = self.direct_game('slots-end')
        describe = subprocess.run([str(self.core), str(self.cd), '--describe-save'], input=self.game_r.hex() + '\n',
                                  capture_output=True, text=True, check=True).stdout.splitlines()
        end = int(next(line.split() for line in describe if line.startswith('SIZE '))[2])
        lost = bytearray(self.game_r)
        struct.pack_into('<h', lost, end - 22, 0)
        (game / 'SAV' / 'GAME_R.SAV').write_bytes(bytes(lost))
        for slot_name, data in companions.items():
            (game / 'SAV' / slot_name).write_bytes(data)
        before = self.save_dir(game)
        code, out = self.run_menu(game, menu, 1, '2|5')
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        self.check(code == 0 and estext(vol, 'GER', 0x130) in out and not (menu / 'campaign-000').exists(),
                   'slots: CONTINUE with final word 0 shows the end text, no campaign')
        result = self.oracle_slots('slots-end', {'sav\\' + k: v for k, v in before.items()}, ['load:10:1', 'save:10'])
        self.same_disk('slots: CONTINUE of a lost campaign and exit', game, result,
                       ('GAMEFILE.STR', 'GAME_R.SAV', 'script10.dat', 'missn10.str', 'player10.mec'))
        for label_id in (0x06, 0x1d, 0x1e, 0x1f, 0x22):
            self.check(estext(vol, 'GER', label_id) in transcript, f'slots: label {label_id:#x} missing')
        self.report['slots'] = dict(oracle_runs=self.oracle_runs)

    # ---- The service bay against VSHELL's own functions ----------------------------
    def bay_ops(self, state):
        """The worker operations for the campaign's recorded service-bay steps
        (applied clicks and mounts as 0x411b10, guidance, rebalance)."""
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        ops = []
        for mission in record['missions']:
            for step in mission.get('bay', []):
                kind, bay, slot, value, _ = step['op']
                if kind in (1, 2) and step['applied']:
                    ops.append(f'mount:{bay}:{slot}:{value}')
                elif kind == 3:
                    ops.append(f'guidance:{bay}:{slot}:{value}')
                elif kind == 4:
                    ops.append(f'rebalance:{value}')
        return record, ops

    def bay_choices(self):
        """Rows and slots for the WEAPONS script, from the CD-default hangar
        and inventory a new career has (es2loadout's catalogue)."""
        lo = load(self.backend.parent / 'es2loadout.py', 'menu_loadout')
        catalog = lo.Catalog(self.core, self.cd)
        d = lo.describe(self.core, self.cd, lo.default_save(self.core, self.cd, catalog, 'GER', 1, 0))
        def usable(bay, slot, item):
            row = next(r for r in lo.weapon_rows(catalog, d, bay, slot) if r['item'] == item)
            return row['enabled'] and (item == 0 or row['count'] > 0)
        player = d['player']['bay']
        cap = lo.bay_vehicle(d, player)[1]
        stocked = next(i for i in catalog.rows if i and usable(player, 0, i))
        disabled = next(i for i in catalog.rows if not usable(player, 1, i))
        rack = next((b['index'], s, i) for b in d['bays'] for s in range(b['capacity']) for i in (13, 14, 15, 16)
                    if usable(b['index'], s, i))
        row = {item: catalog.rows.index(item) + 1 for item in catalog.rows}
        return player, cap, stocked, disabled, rack, row

    def bay_session(self):
        game, menu = self.direct_game('bay')
        before = self.save_dir(game)
        player, cap, stocked, disabled, (rack_bay, rack_slot, rack), row = self.bay_choices()
        # New career -> bay. WEAPONS: the player's Herc slot 1 <- a stocked
        # row, its last slot <- None, slot 2 <- a disabled row, guidance on a
        # slot without a rack (refused); a missile rack where the ARM tables
        # admit one, guidance 3 (ARM); back; WEAPONS again (no leave); back.
        # SAVE (leaves WEAPONS: 0x412a37) slot 4 "BAY", EXIT; MAIN MENU; QUIT.
        script = (f'1|TESTPILOT|2|1|3|2|1|{row[stocked]}|2|{cap}|{row[0]}|2|2|{row[disabled]}|3|1|'
                  f'1|{rack_bay + 1}|2|{rack_slot + 1}|{row[rack]}|3|{rack_slot + 1}|3|4|3|4|'
                  '2|1|4|BAY|1|3|1|5')
        code, out = self.run_menu(game, menu, 1, script)
        state = menu / 'campaign-000'
        record, ops = self.bay_ops(state)
        steps = record['missions'][0].get('bay', [])
        kinds = [(step['op'][0], step['op'][3], step['applied']) for step in steps]
        prefs = (game / 'DATA' / 'PREFS.CFG').read_bytes()
        self.check(code == 0 and kinds == [(2, stocked, 1), (2, 0, 1), (2, rack, 1), (3, 2, 1), (4, prefs[0x2d], 1)],
                   f'bay: recorded steps {kinds}')
        self.check(b'not available' in out and b'no missile rack' in out, 'bay: disabled row and guidance refused')
        marks = re.findall(rb' \*([1-8])  ', out)
        self.check(marks and int(marks[-1]) == rack_bay + 1,
                   f'bay: the second WEAPONS visit keeps bay {rack_bay + 1} (0x482bc9, S-46), shows {marks[-1:]}')
        prepared = (state / record['missions'][0]['prepared_save']).read_bytes()
        disk = {'sav\\gamefile.str': before['GAMEFILE.STR'], 'sav\\GAME_R.SAV': before['GAME_R.SAV'],
                **self.data_disk(game)}
        result = self.oracle_slots('bay', disk, ops + ['save:3: 4. BAY', 'save:10', 'save:10'],
                                   save=self.memory(prepared))
        self.same_disk('bay: WEAPONS, SAVE, MAIN MENU, QUIT', game, result,
                       ('GAMEFILE.STR', 'GAME_3.SAV', 'script3.dat', 'missn3.str', 'player3.mec', 'GAME_R.SAV',
                        'script10.dat', 'missn10.str', 'player10.mec'))
        self.check((state / record['save']).read_bytes() == (game / 'SAV' / 'GAME_3.SAV').read_bytes()[:len(
            (state / record['save']).read_bytes())], 'bay: the slot holds the campaign\'s current SAV')
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        for label_id in (0x0c, 0x13, 0x14, 0x15, 0x1a):
            self.check(estext(vol, 'GER', label_id) in out, f'bay: label {label_id:#x} missing')
        self.report['bay'] = dict(steps=kinds, worker_ops=ops)

    def bay_mission(self):
        """CONTINUE -> bay -> WEAPONS mount -> MISSION through the launcher."""
        game, menu, state = self.menu_session('bay-mission', '2|3|2|1|27|4|4|1|5', False)
        record, ops = self.bay_ops(state)
        mission = record['missions'][0]
        self.check([step['applied'] for step in mission.get('bay', [])] == [1, 1] and len(ops) == 2,
                   f'bay mission: steps {mission.get("bay")}')
        prepared = (state / mission['prepared_save']).read_bytes()
        disk = {'sav\\gamefile.str': self.gamefile_start, 'sav\\GAME_R.SAV': self.game_r}
        result = self.oracle_slots('bay-mission', disk, ops + ['launch'], save=self.memory(prepared), emit=True)
        theirs = {k.lower(): v for k, v in result['disk'].items()}
        seen = state / 'fake-dbsim' / '0'
        for name in ('script.dat', 'MISSION.STR'):
            self.check((seen / name).read_bytes() == (state / 'mission-000' / name).read_bytes(),
                       f'bay mission: DBSIM was not handed the prepared {name}')
        for name in ('mission.var', 'player.mec'):
            self.check((seen / name).read_bytes() == theirs['data\\' + name.lower()],
                       f'bay mission: DBSIM was not handed the {name} of 0x40f68c')
        self.check((seen / 'player.mec').read_bytes() != (state / 'mission-000' / 'player.mec').read_bytes(),
                   'bay mission: the mount did not reach player.mec')
        self.check(mission.get('results_save') == mission.get('launch_save') and
                   (state / mission['results_save']).read_bytes() == result['emitted'],
                   'bay mission: the results path does not start from the original state after the mount')
        engine = load(self.chain_py, 'menu_chain_bay').Engine(self.core, self.cd)
        out = engine.advance(result['emitted'], (state / 'mission-000' / 'dbsim' / 'mission.var').read_bytes(),
                             (state / 'mission-000' / 'dbsim' / 'results.dat').read_bytes(),
                             bytes.fromhex(mission['results_rng']), 'GER', record['repair_option'],
                             record['restock_option'])
        self.check(out.get('sav') == (state / 'save-001.sav').read_bytes(), 'bay mission: results path differs')
        self.check_slot('bay-mission', game, state)
        self.report['bay_mission'] = dict(worker_ops=ops, next=record['missions'][1]['mission'])

    # ---- Launch checks, Replay and End of game boxes ---------------------------
    def launch_refused(self):
        """New career, every slot of the player's Herc emptied in WEAPONS,
        MISSION: the warning box, the bay stays; then MAIN MENU, QUIT."""
        game, menu = self.direct_game('refused')
        player, cap, _, _, _, row = self.bay_choices()
        script = '1|TESTPILOT|2|1|3|' + ''.join(f'2|{s}|{row[0]}|' for s in range(1, cap + 1)) + '4|4|1|5'
        code, out = self.run_menu(game, menu, 1, script)
        state = menu / 'campaign-000'
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        self.check(code == 0 and estext(vol, 'GER', 0x13e) in out and estext(vol, 'GER', 0x13f) in out and
                   estext(vol, 'GER', 0x13b) in out and not record['pending'].get('launched'),
                   'refused: the unarmed warning, no launch')
        result = self.oracle_slots('refused', {'sav\\gamefile.str': self.gamefile_start}, ['rockroll'],
                                   save=self.memory((state / record['save']).read_bytes()))
        boxes = [call[1] for call in result['metadata']['ui_calls'] if call[0] == 0x44d90c]
        self.check(boxes == [1] and result['metadata']['quit'] == 0, f'refused: the original shows {boxes}')
        self.report['refused'] = dict(boxes=boxes)

    def fake_dbsim(self, game, state, kill=''):
        run = subprocess.run([sys.executable, '-B', str(self.fake), '--chain', str(state), '--kill', kill],
                             cwd=game, capture_output=True, text=True, timeout=300)
        self.check(run.returncode == 3, f'stand-in DBSIM exit {run.returncode}: {run.stderr[-500:]}')

    def lost_mission(self, name):
        """CONTINUE (GAME_R) -> bay -> MISSION, then a stand-in DBSIM run in
        which the player is killed. Returns the directories and the SAV
        directory and DATA before the -X3 start."""
        game, menu = self.direct_game(name)
        code, out = self.run_menu(game, menu, 1, '2|4')
        state = menu / 'campaign-000'
        self.check(code == 2, f'{name}: Rock & Roll answered {code}')
        self.fake_dbsim(game, state, kill='0')
        data = {'data\\' + n.lower(): (game / 'DATA' / n).read_bytes() for n in LAUNCH_FILES}
        return game, menu, state, self.save_dir(game), data

    def same_data(self, label, game, result):
        disk = {key.lower(): value for key, value in result['disk'].items()}
        for name in LAUNCH_FILES:
            self.check((game / 'DATA' / name).read_bytes() == disk.get('data\\' + name.lower()),
                       f'{label}: DATA/{name} differs from VSHELL\'s')

    def replay_yes(self):
        game, menu, state, before, data = self.lost_mission('replay-yes')
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        code, out = self.run_menu(game, menu, 3, '1')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        self.check(code == 2 and estext(vol, 'GER', 0x134) in out and estext(vol, 'GER', 0x135) in out,
                   f'replay yes: Replay box and exit 2 (exit {code})')
        self.check(record['missions'][0]['advance']['ending'] == 'player-lost' and record['missions'][1].get('replay_of') == 0 and
                   record['pending'] and record['pending'].get('launched'),
                   f'replay yes: the mission is launched again ({record["missions"][0].get("advance", {}).get("ending")}, '
                   f'{[m.get("replay_of") for m in record["missions"]]}, {record["pending"]}, {record["finished"]})')
        # The original: the results path's state (final word 3), Yes, the exit save.
        disk = {'sav\\' + k: v for k, v in before.items()}
        disk.update(data)
        result = self.oracle_slots('replay-yes', disk, ['final:3', 'replay-yes', 'save:10'],
                                   save=self.memory((state / 'save-001.sav').read_bytes()))
        self.check(result['metadata']['exit_code'] == 2 and result['metadata']['quit'] == 1, 'replay yes: original exit')
        self.same_disk('replay yes', game, result, ('GAMEFILE.STR', 'GAME_R.SAV', 'script10.dat', 'missn10.str',
                                                    'player10.mec'))
        self.same_data('replay yes', game, result)
        self.check((game / 'DATA' / 'mission.var').read_bytes() == data['data\\mission.var'],
                   'replay yes: DBSIM\'s mission.var was rewritten')
        # The replayed mission: DBSIM again (no kill), then -X3: the results
        # path starts from the state Yes loaded (slot 10 at the exit for DBSIM).
        self.fake_dbsim(game, state)
        seen = state / 'fake-dbsim' / '1'
        self.check((seen / 'mission.var').read_bytes() == data['data\\mission.var'] and
                   all((seen / n).read_bytes() == (game / 'SAV' / c).read_bytes() for n, c in COMPANIONS),
                   'replay yes: DBSIM got the slot-10 copies and its own mission.var')
        code, out = self.run_menu(game, menu, 3, '1|5')
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        replayed = record['missions'][1]
        loaded = self.memory(before['GAME_R.SAV'])
        self.check(code == 0 and replayed.get('results_save') == 'save-001-replay.sav' and 'advance' in replayed and
                   (state / 'save-001-replay.sav').read_bytes() == loaded and len(record['missions']) == 3,
                   'replay yes: the replayed mission\'s results path starts from slot 10')
        self.report['replay_yes'] = dict(missions=[m['mission'] for m in record['missions']],
                                         advance=replayed.get('advance', {}).get('ending'))

    def replay_no(self):
        game, menu, state, before, data = self.lost_mission('replay-no')
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        code, out = self.run_menu(game, menu, 3, '2|5')
        self.check(code == 0 and estext(vol, 'GER', 0x135) in out and
                   out.count(b'[saved slot 10: SAV\\GAME_R.SAV]') == 2, 'replay no: the slot-10 save and the exit save')
        disk = {'sav\\' + k: v for k, v in before.items()}
        disk.update(data)
        result = self.oracle_slots('replay-no', disk, ['final:3', 'replay-no', 'save:10'],
                                   save=self.memory((state / 'save-001.sav').read_bytes()))
        self.check([c[0] for c in result['metadata']['ui_calls']] == [0x431718], 'replay no: the original shows the main menu')
        names = ('GAMEFILE.STR', 'GAME_R.SAV', 'script10.dat', 'missn10.str', 'player10.mec')
        self.same_disk('replay no', game, result, names)
        # A later CONTINUE: the End of game box for final word 3, the slot stays
        # loaded and the exit rewrites slot 10.
        before = self.save_dir(game)
        prefs = (game / 'DATA' / 'PREFS.CFG').read_bytes()
        code, out = self.run_menu(game, menu, 1, '2|5')
        self.check(code == 0 and estext(vol, 'GER', 0x12f) in out and estext(vol, 'GER', 0x132) in out,
                   'replay no: CONTINUE shows the End of game box (killed)')
        disk = {'sav\\' + k: v for k, v in before.items()}
        disk['data\\prefs.cfg'] = prefs
        result = self.oracle_slots('replay-no-continue', disk, ['prefs-load', 'continue', 'save:10'])
        # 0x431ac8 pushes ecx after loading only cx: the word is the low half
        self.check([[c[0], c[1] & 0xffff] for c in result['metadata']['ui_calls'] if c[0] == 0x44d55f] == [[0x44d55f, 3]] and
                   result['metadata']['loaded'] == 1,
                   f'replay no: the original CONTINUE {result["metadata"]["ui_calls"]}')
        self.same_disk('replay no: CONTINUE and exit', game, result, names)
        self.check((game / 'DATA' / 'PREFS.CFG').read_bytes() == {k.lower(): v for k, v in result['disk'].items()}['data\\prefs.cfg'],
                   'replay no: prefs.cfg after CONTINUE differs from VSHELL\'s')
        self.report['replay_no'] = 'passed'

    def continue_without_slot(self):
        """The CD's slot table (slot 10 without flag), nothing loaded."""
        game, menu = self.direct_game('no-slot')
        (game / 'SAV' / 'GAMEFILE.STR').write_bytes((self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes())
        before, prefs = self.save_dir(game), (game / 'DATA' / 'PREFS.CFG').read_bytes()
        vol = (self.cd / 'VOL' / 'LANG0.VOL').read_bytes()
        code, out = self.run_menu(game, menu, 1, '2|5')
        self.check(code == 0 and estext(vol, 'GER', 0x130) in out and self.save_dir(game) == before and
                   b'[saved slot' not in out, 'no slot: End of game box (war lost), nothing saved')
        disk = {'sav\\' + k: v for k, v in before.items()}
        disk['data\\prefs.cfg'] = prefs
        result = self.oracle_slots('no-slot', disk, ['prefs-load', 'continue'])
        # 0x431ac8 pushes ecx after loading only cx: the word is the low half
        self.check([[c[0], c[1] & 0xffff] for c in result['metadata']['ui_calls'] if c[0] == 0x44d55f] == [[0x44d55f, 0]] and
                   result['metadata']['loaded'] == 0,
                   f'no slot: the original CONTINUE {result["metadata"]["ui_calls"]}')
        self.same_disk('no slot', game, result, ('GAMEFILE.STR', 'GAME_R.SAV'))
        self.check((game / 'DATA' / 'PREFS.CFG').read_bytes() == {k.lower(): v for k, v in result['disk'].items()}['data\\prefs.cfg'],
                   'no slot: prefs.cfg differs from VSHELL\'s')

    # ---- The ONLINE MANUAL entry --------------------------------------------------
    def online_manual(self):
        """Choice 11 (ONLINE MANUAL 0x431e1b): preference byte 6 := 0 in the file, the
        manual's path by DATA\\LANGUAGE.CFG below menu.ini's cd_root. Every session here
        uses the policy `text` (or `hide`): nothing is ever opened."""
        base = self.tmp / 'manual'
        game, menu, cd = base / 'game', base / 'menu', base / 'cd'
        (game / 'DATA').mkdir(parents=True)
        menu.mkdir()
        for name in ('english', 'german'):
            (cd / name).mkdir(parents=True)
            (cd / name / 'ES2GUIDE.HLP').write_bytes(b'help')

        def ini(policy, root=cd):
            (menu / 'menu.ini').write_text(f'[Menu]\ngame_dir = {game}\ncd_root = {root}\n'
                                           + 'cd_check = skip\n'   # the synthetic CD has no avi\pt1.avi
                                           + (f'online_manual = {policy}\n' if policy else ''), encoding='ascii')
        prefs = bytearray(PREFS)
        prefs[6] = 1
        prefs_file = game / 'DATA' / 'PREFS.CFG'
        language = game / 'DATA' / 'LANGUAGE.CFG'
        want = bytearray(prefs)
        want[6] = 0
        ini('text')
        prefs_file.write_bytes(bytes(prefs))
        language.write_bytes(b'E')
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(code == 0 and b'  11 Online manual' in out, 'ONLINE MANUAL: entry 11 offered')
        self.check(b'Windows help file ' + str(cd).encode() + b'\\english\\es2guide.hlp (not opened' in out,
                   f'ONLINE MANUAL: english path named: {out[-300:]!r}')
        self.check(prefs_file.read_bytes() == bytes(want), 'ONLINE MANUAL: byte 6 := 0 written, the others unchanged')
        # a short prefs file: the zero-extended 54-byte block with byte 6 cleared
        prefs_file.write_bytes(PREFS[:8])
        code, out = self.run_menu(game, menu, 1, '11|5')
        short = bytearray(PREFS[:8] + bytes(46))
        short[6] = 0
        self.check(prefs_file.read_bytes() == bytes(short), 'ONLINE MANUAL: short prefs file written as 54 bytes')
        # the committed block is the current one afterwards (0x40dd6e): music toggle, Cancel
        # (back to the committed block), Accept writes byte 6 = 0 and the others as they were
        prefs_file.write_bytes(bytes(prefs))
        code, out = self.run_menu(game, menu, 1, '11|4|6|5|4|4|5')
        self.check(code == 0 and prefs_file.read_bytes() == bytes(want), 'ONLINE MANUAL: committed block follows the write')
        # the synthetic CD has english and german manuals only
        language.write_bytes(b'G')
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(b'german\\es2guide.hlp (not opened' in out, 'ONLINE MANUAL: german path')
        language.write_bytes(b'F')
        prefs_file.write_bytes(bytes(prefs))
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(b'! The online manual was not found: ' + str(cd).encode() + b'\\french\\es2guide.hlp' in out,
                   'ONLINE MANUAL: missing file named')
        self.check(prefs_file.read_bytes() == bytes(want), 'ONLINE MANUAL: prefs written before the file is looked for')
        # VSHELL's severity-4 assertion (0x44e560): the report line on the console and in
        # assert.log (the 78-dash rule once per process), exit code 1, no loop-end save.
        # The prefs were written before the language is read.
        rule = b'-' * 78 + b'\r\n'
        for letter in (b'e', b'X', b''):
            language.write_bytes(letter)
            prefs_file.write_bytes(bytes(prefs))
            if (game / 'assert.log').exists():
                (game / 'assert.log').unlink()
            code, out = self.run_menu(game, menu, 1, '11|5')
            line = b'Assert::err: Line 260 in "src\\WMAIN.CPP": online help not found.'
            self.check(code == 1 and line in out and b'choice> ' in out and b'[exit 1]' in out,
                       f'ONLINE MANUAL: language byte {letter!r}: exit {code}')
            self.check((game / 'assert.log').read_bytes() == rule + line + b'\r\n',
                       f'ONLINE MANUAL: assert.log for language byte {letter!r}')
            self.check(prefs_file.read_bytes() == bytes(want), f'ONLINE MANUAL: prefs written before the assertion ({letter!r})')
        # a second run appends again, with its own rule (a new process)
        code, out = self.run_menu(game, menu, 1, '11|5')
        line = b'Assert::err: Line 260 in "src\\WMAIN.CPP": online help not found.'
        self.check((game / 'assert.log').read_bytes() == (rule + line + b'\r\n') * 2, 'ONLINE MANUAL: assert.log is appended')
        language.unlink()
        (game / 'assert.log').unlink()
        code, out = self.run_menu(game, menu, 1, '11|5')
        line = b'Assert::err: Line 183 in "src\\utility.cpp": data\\language.cfg'
        self.check(code == 1 and line in out, f'ONLINE MANUAL: no language.cfg: exit {code}')
        self.check((game / 'assert.log').read_bytes() == rule + line + b'\r\n', 'ONLINE MANUAL: assert.log for no language.cfg')
        (game / 'assert.log').unlink()
        # policy hide: not offered, not run, nothing written
        ini('hide')
        language.write_bytes(b'E')
        prefs_file.write_bytes(bytes(prefs))
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(b'  11 ' not in out and b'? 1..6' in out and prefs_file.read_bytes() == bytes(prefs),
                   'ONLINE MANUAL: policy hide')
        # a policy value other than open, text, hide refuses to start (as cd_check does)
        ini('keep')
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(code == 0 and b'choice>' not in out, 'ONLINE MANUAL: policy keep refused')
        # a cd_root with a trailing backslash is joined as it is
        ini('text', str(cd) + '\\')
        code, out = self.run_menu(game, menu, 1, '11|5')
        self.check(b'Windows help file ' + str(cd).encode() + b'\\english\\es2guide.hlp (not' in out,
                   'ONLINE MANUAL: root with a trailing backslash')
        # the real CD, read only: the three manuals exist
        if os.environ.get('ES2_DATA_ROOT'):
            root = Path(os.environ['ES2_DATA_ROOT']).resolve()
            ini('text', root)
            for letter, name in ((b'E', 'ENGLISH'), (b'F', 'FRENCH'), (b'G', 'GERMAN')):
                language.write_bytes(letter)
                code, out = self.run_menu(game, menu, 1, '11|5')
                self.check(b'Windows help file ' + str(root).encode() + b'\\' + name.lower().encode() + b'\\es2guide.hlp (not' in out,
                           f'ONLINE MANUAL: real CD {name}')
        self.report['online_manual'] = 'passed' if not self.failures else 'failed'

    def real(self):
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        self.game_r = self.pins.require(self.cd, 'SAV/GAME_R.SAV')[0]
        # A played installation's slot table: the CD's with the flag of slot
        # 10 set (VSHELL loads slot 10 only when its flag is set, 0x40eae3).
        self.gamefile_start = played_gamefile((self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes())
        game, menu, state = self.continue_recorded()
        self.reuse_and_abort(game, menu, state)
        self.new_campaign()
        self.saved_games()
        self.bay_session()
        self.bay_mission()
        self.launch_refused()
        self.replay_yes()
        self.replay_no()
        self.continue_without_slot()
        self.mission_texts()
        self.mission_movies()

    # ---- The movies the text menu prints in the bay-mission transcript ---------
    def mission_movies(self):
        """CONTINUE (GAME_R, C1_01: group 1, position 0) -> MISSION: page 0's
        c1.avi and alph_th.avi, then page 1's briefing movie; the -X3 process
        prints the debrief movie (the advancement's debrief word, chain.json)
        once. The words themselves are compared with the original in
        //shell:movies_real_test."""
        log = (self.tmp / 'bay-mission' / 'menu' / 'menu.log').read_bytes()
        record = json.loads((self.tmp / 'bay-mission' / 'menu' / 'campaign-000' / 'chain.json')
                            .read_text(encoding='utf-8'))
        lines = [l for l in log.replace(b'\r', b'').split(b'\n') if l.startswith(b'[movie ')]
        self.check(lines[:2] == [b'[movie avg\\intr_pt1.avi]', b'[movie avg\\intr_pt2.avi]'],
                   f'mission movies: the -X1 process starts with the intro ({lines[:2]})')
        lines = lines[2:]
        page0 = lines[:2] == [b'[movie avi\\c1.avi]', b'[movie avi\\alph_th.avi]']
        self.check(page0, f'mission movies: page 0 printed {lines[:2]}')
        debrief = record['missions'][0].get('advance', {}).get('debrief', {}).get('auxiliary')
        # lines: the two page-0 movies, the briefing movie, then the debrief movie
        want = 4 if debrief is not None and debrief != -1 else 3
        self.check(len(lines) == want, f'mission movies: {len(lines)} movie lines, {want} expected ({lines})')
        self.report['mission_movies'] = dict(lines=len(lines), debrief_word=debrief)

    # ---- The mission-screen texts in the bay-mission transcript ----------------
    def mission_texts(self):
        """The bay-mission session's transcript (menu.log, both processes)
        holds the three page-1 texts VSHELL's 0x4135bb composes from the
        prepared state and MISSION.STR, and after the -X3 results path the
        page-4 values of 0x40f904 for that path's words (outcome, tally and
        P from results.dat, lost and added from our results path, the state
        the path wrote). Texts are compared in memory, never printed."""
        log = (self.tmp / 'bay-mission' / 'menu' / 'menu.log').read_bytes()
        state = self.tmp / 'bay-mission' / 'menu' / 'campaign-000'
        record = json.loads((state / 'chain.json').read_text(encoding='utf-8'))
        mission = record['missions'][0]
        prepared = (state / mission['prepared_save']).read_bytes()
        disk = {'sav\\gamefile.str': self.gamefile_start,
                'data\\mission.str': (state / 'mission-000' / 'MISSION.STR').read_bytes()}
        result = self.oracle_slots('texts-page1', disk, ['briefing'], save=self.memory(prepared))
        texts = emitted_texts(result['disk'].get('texts.emit', b''))
        page1 = len(texts) == 3 and all(t is not None and t in log for t in texts)
        self.check(page1, 'mission texts: the page-1 texts of 0x4135bb are not in the transcript')
        page4 = None
        if (state / 'mission-000' / 'debrief.txt').is_file():
            results = (state / 'mission-000' / 'dbsim' / 'results.dat').read_bytes()
            engine = load(self.chain_py, 'menu_chain_texts').Engine(self.core, self.cd)
            out = engine.advance((state / mission['results_save']).read_bytes(),
                                 (state / 'mission-000' / 'dbsim' / 'mission.var').read_bytes(), results,
                                 bytes.fromhex(mission['results_rng']), 'GER', record['repair_option'],
                                 record['restock_option'])
            outcome, tally, pairs = struct.unpack_from('<hIh', results)
            recovered = (pairs + out['added']) & 0xffff
            words = f'{outcome & 0xffff}:{tally}:{out["lost"] & 0xffff}:{recovered}'
            result = self.oracle_slots('texts-page4', {'sav\\gamefile.str': self.gamefile_start},
                                       [f'debrief-stats:{words}'],
                                       save=self.memory((state / 'save-001.sav').read_bytes()))
            v = emitted_texts(result['disk'].get('texts.emit', b''))
            page4 = (len(v) == 10 and v[0] in log and b' ' + v[1] + b'\n' in log and b' ' + v[2] + b'\n' in log and
                     b' | %s | %s | %s\n' % (v[3], v[5], v[7]) in log and
                     b' | %s | %s | %s\n' % (v[4], v[6], v[8]) in log and b' | %s\n' % v[9] in log)
            self.check(page4, 'mission texts: the page-4 values of 0x40f904 are not in the transcript')
        self.report['mission_texts'] = dict(page1=page1, page4=page4)


def emitted_texts(data):
    """The text worker ops' texts.emit: [u32 size][bytes], 0xffffffff = null."""
    out, at = [], 0
    while at + 4 <= len(data):
        n, = struct.unpack_from('<I', data, at)
        at += 4
        if n == 0xffffffff:
            out.append(None)
        else:
            out.append(data[at:at + n])
            at += n
    return out


def main():
    test = Test(sys.argv[1:11])
    try:
        test.synthetic()
        test.online_manual()
        if os.environ.get('ES2_DATA_ROOT'):
            test.real()
        else:
            print('SKIP real part: ES2_DATA_ROOT not set')
    finally:
        test.scratch.cleanup()
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, 'menu-report.json').write_text(json.dumps(test.report, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(report=test.report, failures=test.failures), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
