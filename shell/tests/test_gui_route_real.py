# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2shell.exe is a GUI-subsystem program and opens no window headless)
"""The graphical shell es2shell.exe headless against the text menu es2menu.exe.

Both presenters run the same flow over the same host (shell/src/host.c);
the text menu is proven against VSHELL (//shell:menu_real_test). Here the
graphical shell is driven by a headless script (es2shell --headless --script:
clicks at the centres of the layout table's controls, typed keys through VSHELL's input
path) and must leave the same files as the text menu given the same choices
and the same start-up draw count (menu.ini seed):

Part 1 (always): es2shell without menu.ini or with a menu.ini whose game
directory is another one ends with exit code 0 and touches nothing.

Part 2 (needs ES2_DATA_ROOT): in temporary game directories (the CD's
PREFS.CFG, GAMEFILE.STR and VOL\\LANG0.VOL, own error.str and language.cfg):
a. Route A: new career (TESTPILOT, skill 2) -> service bay -> WEAPONS: slot 1
   emptied, then a stocked weapon mounted -> MISSION -> Rock & Roll: exit
   code 2, DATA's four launch files and every SAV file (slot 10 of the exit
   save with its three companions) byte for byte as es2menu's.
b. Saved games: run 1 new career, the bay's SAVE, slot 3 named, ACCEPT, EXIT,
   the bay's MAIN MENU, QUIT; run 2 SAVE/RESTORE, RESTORE slot 3, MISSION,
   Rock & Roll: after each run DATA's launch files and the SAV directory as
   es2menu's.
c. Every bar screen (REPAIR, BUILD, ARMORY, CREW, WEAPONS) opened and left,
   MAIN MENU, QUIT: the files as es2menu's after WEAPONS, MAIN MENU, QUIT;
   then CONTINUE GAME (enabled by slot 10's flag), MISSION, Rock & Roll.
d. The results path: CONTINUE of the pinned GAME_R.SAV (read from a
   measurement clone, as menu_real_test does; slot 10 without its copies,
   seed 71: C1_01), Rock & Roll; the recorded c1_01-success output of DBSIM
   into DATA; -X3: the bay with the mission screen's page 4, MAIN MENU, QUIT.
   The files after both processes as es2menu's (SKIP without the pin or the
   recording). Then the same output with the player killed: the Replay
   box, No, the main menu's QUIT, the files again as es2menu's.
e. Route A with an evidence log (menu.ini chain_dir): es2shell's
   WEAPONS mounts are the log's `bay` steps with the same SAV after every
   step as es2menu's (their CLICK kind 2 is our MOUNT kind 1).
f. No language letter: prefs byte +0x2b gives GER/FRE/ENG or no
   prefix (shell.log); a letter wins. Prefs byte 0x2f = 1: the main menu's
   notice shows error.str message 6 / caption 2 (empty when the file has no
   message 6) and stores 2.
g. -@: PRACTICE MISSIONS, entry 5, Begin Mission: the DEBUG dialog
   waits (quit 0); Use Default, or ACCEPT of a typed TRAIN8, launch with
   es2menu's files for the same DEBUG answers; without -@ no dialog.
h. -@ on the mode-1 paths: the DEBUG dialog waits at the end of a
   new career (0x41308e) over the main menu; Use Default, ACCEPT of C1_01
   (the default's own name: the same files as Use Default) and of C1_02,
   then the bay, MISSION, Rock & Roll: es2menu's files. The results path's
   advance 2 (0x40f647, after part d's launch): the dialog waits, Use Default
   or ACCEPT C1_02 shows the bay with page 4; es2menu's files.

Arguments: es2shell.exe es2menu.exe reference_pins.py
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

LAUNCH_FILES = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec')
SEED = 71
checks = 0
failures = []


def check(value, label):
    global checks
    checks += 1
    if not value:
        failures.append(label)
        print('FAIL', label, flush=True)


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
    """The slot table with the flag (tag byte) of group-1 entry 10 set (a played installation)."""
    (groups, _) = str_groups(data, 2)
    at = 4 + 2 + sum(4 + len(text) for text, _ in groups[0]) + 2
    at += sum(4 + len(text) for text, _ in groups[1][:10])
    at += 2 + len(groups[1][10][0]) + 1
    out = bytearray(data)
    out[at] = 1
    return bytes(out)


def scratch_base():
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


class Routes:
    def __init__(self, shell, menu, pins=None):
        self.shell = Path(shell).resolve()
        self.menu = Path(menu).resolve()
        self.tmp = Path(tempfile.mkdtemp(prefix='gui-route-', dir=scratch_base()))
        self.cd = None
        self.pins = None
        if pins:
            spec = importlib.util.spec_from_file_location('reference_pins', Path(pins).resolve())
            self.pins = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.pins)

    # ---- directories -------------------------------------------------------------------------
    def game(self, name, chain=False):
        root = self.tmp / name
        game = root / 'game'
        for sub in ('DATA', 'SAV', 'VOL'):
            (game / sub).mkdir(parents=True)
        (game / 'DATA' / 'language.cfg').write_bytes(b'G')
        if self.cd:
            shutil.copyfile(self.cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
            shutil.copyfile(self.cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
            shutil.copyfile(self.cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
        body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
        body += struct.pack('<hh', 1, 2) + b'c\0\0'
        (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
        menu = root / 'menu'
        menu.mkdir()
        lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {self.cd or ""}', 'hangar = lowest-free', f'seed = {SEED}',
                 f'chain_dir = {root / "chain" if chain else ""}', 'online_manual = text']
        (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
        return game, menu

    def files(self, game):
        out = {'DATA\\' + n: (game / 'DATA' / n).read_bytes() for n in LAUNCH_FILES if (game / 'DATA' / n).exists()}
        out.update({'SAV\\' + p.name: p.read_bytes() for p in (game / 'SAV').iterdir()})
        return out

    # ---- the two presenters --------------------------------------------------------------------
    def run_menu(self, game, menu, script, state=1, extra=('-G',)):
        run = subprocess.run([str(self.menu), '--menu', str(menu), 'dummy', '-eggplant', f'-X{state}', *extra], cwd=game,
                             input=script.replace('|', '\n').encode('ascii') + b'\n', capture_output=True, timeout=600)
        return run.returncode, run.stdout

    def run_shell(self, game, menu, script, name, state=1, extra=('-G',)):
        script_path = game.parent / f'{name}.script'
        report = game.parent / f'{name}.report'
        script_path.write_text(script, encoding='ascii')
        run = subprocess.run([str(self.shell), '--menu', str(menu), '--headless', '--script', str(script_path),
                              '--report', str(report), 'dummy', '-eggplant', f'-X{state}', *extra], cwd=game,
                             capture_output=True, timeout=600)
        text = report.read_text(encoding='latin-1') if report.exists() else ''
        return run.returncode, text

    def same(self, label, menu_game, shell_game):
        mine, theirs = self.files(shell_game), self.files(menu_game)
        differ = sorted(k for k in set(mine) | set(theirs) if mine.get(k) != theirs.get(k))
        check(not differ, f'{label}: files differ from es2menu\'s: {differ}')
        return mine

    # ---- part 1 ----------------------------------------------------------------------------------
    def synthetic(self):
        game, menu = self.game('synthetic')
        (menu / 'menu.ini').unlink()
        before = self.files(game)
        code, report = self.run_shell(game, menu, 'step\n', 'none')
        check(code == 0 and self.files(game) == before, 'no menu.ini: exit 0, nothing written')
        other, menu2 = self.game('elsewhere')
        code, report = self.run_shell(game, menu2, 'step\n', 'elsewhere')
        check(code == 0 and self.files(game) == before and 'menu.ini names' in (menu2 / 'shell.log').read_text('latin-1'),
              'another game directory: refused, exit 0, nothing written')

    # ---- part 2 ----------------------------------------------------------------------------------
    START = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
             'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n')

    def route_a(self):
        # The text menu chooses: slot 1 emptied (row 27, item 0), then the first
        # row that mounts into slot 1.
        chosen = None
        for row in range(1, 27):
            game, menu = self.game(f'a-menu-{row}', chain=True)
            script = f'1|TESTPILOT|2|1|3|2|1|27|2|1|{row}|4|4'
            code, out = self.run_menu(game, menu, script)
            if out.count(b'[mounted]') == 2 and code == 2:
                chosen = (row, game, menu)
                break
        check(chosen is not None, 'route A: the text menu mounted a weapon')
        if not chosen:
            return
        row, menu_game, menu_m = chosen
        game, menu = self.game('a-shell', chain=True)
        script = (self.START + 'click service_bay.09\nstep 5\nexpect word=2\nclick weapons.159\nstep 2\n'
                  'click weapons.151\nstep 2\n' + f'click weapons.{21 + 5 * (row - 1)}\nstep 2\n' +
                  'click service_bay.19\nstep 40\nexpect page=0 word=-1\n'
                  # page 0 (first visit): its movies end with the screen word -1 (0x41ef7a); MISSION again: page 1
                  'click service_bay.19\nstep 40\nexpect word=7 page=1\nclick mission.27\nstep 5\n')
        code, report = self.run_shell(game, menu, script, 'route-a')
        check(code == 2 and 'errors=0' in report and ' quit=1 exit=2' in report, f'route A: exit 2 and no script error\n{report}')
        log = (menu / 'shell.log').read_text('latin-1')
        check('mounts 2 ' in log and 'movie avi\\c1.avi' in log and 'map built' in log,
              f'route A: two mounts, the group movie, the map object (logged)\n{log}')
        self.same('route A', menu_game, game)
        # Item d: WEAPONS' mounts are the evidence log's bay steps, as the
        # text menu's (its CLICK kind 2 is our MOUNT kind 1: the verify
        # replays both as 0x411b10), with the same SAV after every step
        theirs, mine = self.chain_steps(menu_m), self.chain_steps(menu)
        norm = lambda steps: [([1 if o[0] == 2 else o[0]] + o[1:4], applied, sav) for o, applied, sav in steps]
        check(len(mine) >= 2 and norm(mine) == norm(theirs),
              f'route A: the bay steps of the evidence log as es2menu\'s\n{[s[:2] for s in mine]}\n{[s[:2] for s in theirs]}')

    def saved_games(self):
        game_m, menu_m = self.game('b-menu')
        code1, out = self.run_menu(game_m, menu_m, '1|TESTPILOT|2|1|2|1|3|ALPHA 7|1|3|1|5')
        check(code1 == 0 and b'[saved slot 2' in out, 'saved games: the text menu saved slot 3')
        game, menu = self.game('b-shell')
        script = (self.START + 'click service_bay.07\nstep 5\nexpect screen=2\nclick saved_games.06\nstep 2\n'
                  'click saved_games.18\nstep 3\ntype ALPHA 7\nclick saved_games.16\nstep 3\nclick saved_games.22\n'
                  'step 5\nexpect screen=5\nclick service_bay.05\nstep 5\nexpect screen=0\nclick main_menu.13\nstep 5\n')
        code, report = self.run_shell(game, menu, script, 'save')
        check(code == 0 and 'errors=0' in report, f'saved games run 1: exit 0, no script error\n{report}')
        self.same('saved games run 1', game_m, game)
        code_m, out = self.run_menu(game_m, menu_m, '6|2|3|4')
        script = ('step 200\nexpect screen=0\nclick main_menu.11\nstep 5\nexpect screen=2\nclick saved_games.06\nstep 2\n'
                  'click saved_games.20\nstep 5\nexpect screen=5 game=1\nclick service_bay.19\nstep 40\n'
                  'click service_bay.19\nstep 40\nexpect word=7 page=1\nclick mission.27\nstep 5\n')
        code, report = self.run_shell(game, menu, script, 'restore')
        check(code_m == 2 and code == 2 and 'errors=0' in report, f'saved games run 2: both exit 2\n{report}')
        self.same('saved games run 2', game_m, game)

    def editors(self):
        """Every bar screen opens and leaves; with no
        operation the state is the text menu's after WEAPONS and MAIN MENU
        (the leaves' rebalance 0x412a37 changes nothing more on a new career)."""
        game_m, menu_m = self.game('c-menu')
        code_m, out = self.run_menu(game_m, menu_m, '1|TESTPILOT|2|1|3|4|1|5')
        game, menu = self.game('c-shell')
        script = self.START
        for control, word in (('11', 3), ('13', 4), ('15', 5), ('17', 6), ('09', 2)):
            script += f'click service_bay.{control}\nstep 5\nexpect word={word}\n'
        script += 'click service_bay.05\nstep 5\nexpect screen=0\nclick main_menu.13\nstep 5\n'
        code, report = self.run_shell(game, menu, script, 'editors')
        check(code_m == 0 and code == 0 and 'errors=0' in report, f'editors: every bar screen, exit 0\n{report}')
        self.same('editors', game_m, game)
        # CONTINUE GAME (enabled by slot 10's flag, which the exit save set), MISSION twice, Rock & Roll
        code_m, out = self.run_menu(game_m, menu_m, '2|4')
        script = ('step 200\nexpect screen=0\nclick main_menu.09\nstep 5\nexpect screen=5 game=1\nclick service_bay.19\n'
                  'step 40\nclick service_bay.19\nstep 40\nexpect word=7 page=1\nclick mission.27\nstep 5\n')
        code, report = self.run_shell(game, menu, script, 'continue')
        check(code_m == 2 and code == 2 and 'errors=0' in report, f'continue: both exit 2\n{report}')
        self.same('continue', game_m, game)

    def results(self):
        """The results path: CONTINUE of the pinned GAME_R.SAV (slot 10 without
        its copies, seed 71: C1_01), Rock & Roll; DBSIM's recorded
        c1_01-success output into DATA; the -X3 start (advance 2: the next
        mission, the bay with the mission screen's page 4 and its statistics),
        MAIN MENU, QUIT. The files after both processes as es2menu's."""
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        game_r = self.pins.fetch(self.cd, 'SAV/GAME_R.SAV')[0] if self.pins else None
        if game_r is None or not (golden / 'results.dat').exists():
            print('SKIP results: no pinned GAME_R.SAV in a measurement clone or no recorded c1_01-success output')
            return
        dirs = []
        for name in ('d-menu', 'd-shell'):
            game, menu = self.game(name)
            (game / 'SAV' / 'GAMEFILE.STR').write_bytes(played_gamefile((self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()))
            (game / 'SAV' / 'GAME_R.SAV').write_bytes(game_r)
            dirs.append((game, menu))
        (game_m, menu_m), (game, menu) = dirs
        code_m, out = self.run_menu(game_m, menu_m, '2|4')
        code, report = self.run_shell(game, menu, 'step 200\nclick main_menu.09\nstep 5\nexpect screen=5 game=1\n'
                                      'click service_bay.19\nstep 40\nclick service_bay.19\nstep 40\nexpect page=1\n'
                                      'click mission.27\nstep 5\n', 'launch')
        check(code_m == 2 and code == 2 and 'errors=0' in report, f'results: CONTINUE and Rock & Roll exit 2\n{report}')
        self.same('results: launch', game_m, game)
        for g in (game_m, game):
            for sub in ('DATA', 'SAV'):
                shutil.copytree(g / sub, g.parent / 'launched' / sub)
            for n in ('results.dat', 'mission.var'):
                shutil.copyfile(golden / n, g / 'DATA' / n)
        code_m, out = self.run_menu(game_m, menu_m, '1|5', state=3)
        code, report = self.run_shell(game, menu, 'step 20\nexpect screen=5 word=7 page=4\nclick service_bay.05\nstep 5\n'
                                      'expect screen=0\nclick main_menu.13\nstep 5\n', 'results', state=3)
        log = (menu / 'shell.log').read_text('latin-1')
        check(code_m == 0 and code == 0 and 'errors=0' in report and 'results C1_01' in log,
              f'results: -X3, the bay with page 4, MAIN MENU, QUIT\n{report}\n{log}')
        self.same('results: -X3', game_m, game)
        # The same results with the player killed (his Herc's condition word
        # 22 := 0, after the 8-byte head and its item pairs): the Replay box over palette 1
        # (0x401d37), No (replay_box.07, 0x44d24d), then the main menu 0x431718
        # takes QUIT (trap: the screen must not stay frozen).
        for g in (game_m, game):
            for sub in ('DATA', 'SAV'):   # back to the state after the launch
                shutil.rmtree(g / sub)
                shutil.copytree(g.parent / 'launched' / sub, g / sub)
            killed = bytearray((golden / 'results.dat').read_bytes())
            at = 8 + 4 * struct.unpack_from('<h', killed, 6)[0] + 2 * 22
            killed[at:at + 2] = b'\0\0'
            (g / 'DATA' / 'results.dat').write_bytes(bytes(killed))
            shutil.copyfile(golden / 'mission.var', g / 'DATA' / 'mission.var')
        code_m, out = self.run_menu(game_m, menu_m, '2|5', state=3)
        code, report = self.run_shell(game, menu, 'step 20\nexpect screen=0 box=3 final=3\n'
                                      'click replay_box.07\nstep 5\nexpect screen=0 box=0 quit=0\n'
                                      'click main_menu.13\nstep 5\nexpect quit=1\n', 'replay-no', state=3)
        check(code_m == 0 and code == 0 and 'errors=0' in report, f'results: killed, Replay No, QUIT\n{report}')
        self.same('results: killed, Replay No', game_m, game)

    # ---- evidence log, language letter, -@ ---------------------------------------------------------
    def prefs(self, game, **bytes_at):
        data = bytearray((game / 'DATA' / 'PREFS.CFG').read_bytes())
        for offset, value in bytes_at.items():
            data[int(offset[1:], 16)] = value
        (game / 'DATA' / 'PREFS.CFG').write_bytes(bytes(data))

    def language(self):
        """(b) An argv without a language letter: VSHELL's language word is
        prefs byte +0x2b (0x4079b7), 0/1/2 the directories eng/fre/ger, any
        other value no prefix (0x408bb5); a letter wins over the byte."""
        for value, extra, want in ((2, (), 'GER'), (1, (), 'FRE'), (0, (), 'ENG'), (7, (), '(none)'), (1, ('-G',), 'GER')):
            game, menu = self.game(f'lang-{value}-{len(extra)}')
            self.prefs(game, x2b=value)
            code, report = self.run_shell(game, menu, 'step 5\n', 'lang', extra=extra)
            log = (menu / 'shell.log').read_text('latin-1')
            check(f'\nlanguage {want}\n' in log, f'language: prefs +0x2b = {value}, argv {extra}: {want}\n{log}')

    def notice(self):
        """(c) Prefs byte 0x2f = 1: the main menu's first show shows error.str
        message 6 with caption 2 (0x401a81(6, 2, 0x30)) and stores 2. The
        English file has no message 6: an empty text."""
        for name, messages in (('notice-7', 7), ('notice-3', 3)):
            game, menu = self.game(name)
            body = struct.pack('<h', messages) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(messages))
            body += struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'c%d\0\0' % i for i in range(3))
            (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
            self.prefs(game, x2f=1)
            code, report = self.run_shell(game, menu, 'step 200\nexpect screen=0\n', name)
            log = (menu / 'shell.log').read_text('latin-1')
            text = 'm6' if messages > 6 else ''
            check(f'notice error.str 6 caption 2: "{text}" / "c2"' in log, f'{name}: the notice\'s text\n{log}')
            check((game / 'DATA' / 'PREFS.CFG').read_bytes()[0x2f] == 2, f'{name}: prefs byte 0x2f := 2')

    def debug_dialog(self):
        """(a) -@: after Begin Mission's new game the DEBUG dialog stays open
        (no posted click); Use Default or ACCEPT with a typed name launch,
        with the files of es2menu's DEBUG answers."""
        start = ('step 200\nexpect screen=0\nclick main_menu.17\nstep 5\nclick practice.56\nstep 2\nclick practice.33\n'
                 'step 5\nexpect quit=0 exit=0 game=2\n')
        for name, menu_script, answer in (
                ('default', '7|5|10|1', 'click debug_dialog.09\nstep 5\n'),
                ('accept', '7|5|10|2|TRAIN8', 'click debug_dialog.06\ntype TRAIN8\nclick debug_dialog.11\nstep 5\n')):
            game_m, menu_m = self.game(f'debug-{name}-menu')
            code_m, out = self.run_menu(game_m, menu_m, menu_script, extra=('-G', '-@'))
            game, menu = self.game(f'debug-{name}-shell')
            code, report = self.run_shell(game, menu, start + answer, f'debug-{name}', extra=('-G', '-@'))
            log = (menu / 'shell.log').read_text('latin-1')
            check(code_m == 2 and code == 2 and 'errors=0' in report and 'debug dialog default ' in log,
                  f'DEBUG {name}: the dialog waited, then exit 2\n{report}\n{log}')
            self.same(f'DEBUG {name}', game_m, game)
        # without -@ the posted Use Default: no dialog wait
        game, menu = self.game('debug-none-shell')
        code, report = self.run_shell(game, menu, start.replace('expect quit=0 exit=0 game=2\n', ''), 'debug-none')
        check(code == 2 and 'debug dialog' not in (menu / 'shell.log').read_text('latin-1'), f'no -@: no dialog, exit 2\n{report}')

    # ---- -@ on the mode-1 paths ----------------------------------------------------------------------
    CAREER = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
              'click registration.10\nclick registration.14\nstep 5\nexpect screen=0 game=1 quit=0\n')
    LAUNCH = ('click service_bay.19\nstep 40\nclick service_bay.19\nstep 40\nexpect word=7 page=1\n'
              'click mission.27\nstep 5\n')

    def debug_career(self):
        """(h) -@ at the end of a mode-1 new career: the dialog waits; the
        answer prepares, then the bay; es2menu's files after Rock & Roll."""
        results = {}
        for name, menu_answer, answer in (
                ('default', '1', 'click debug_dialog.09\n'),
                ('c1_01', '2|C1_01', 'click debug_dialog.06\ntype C1^01\nclick debug_dialog.11\n'),
                ('c1_02', '2|C1_02', 'click debug_dialog.06\ntype C1^02\nclick debug_dialog.11\n')):
            game_m, menu_m = self.game(f'career-{name}-menu')
            code_m, out = self.run_menu(game_m, menu_m, f'1|TESTPILOT|2|1|{menu_answer}|4', extra=('-G', '-@'))
            game, menu = self.game(f'career-{name}-shell')
            code, report = self.run_shell(game, menu, self.CAREER + answer + 'step 5\nexpect screen=5 game=1\n' + self.LAUNCH,
                                          f'career-{name}', extra=('-G', '-@'))
            log = (menu / 'shell.log').read_text('latin-1')
            check(code_m == 2 and code == 2 and 'errors=0' in report and 'debug dialog default C1_01' in log,
                  f'career DEBUG {name}: the dialog waited (default C1_01), then the bay and exit 2\n{report}\n{log}')
            results[name] = self.same(f'career DEBUG {name}', game_m, game)

        def launch_files(files):
            return {k: v for k, v in files.items() if any(k.lower().endswith(n.lower()) and 'data' in k.lower()
                                                          for n in LAUNCH_FILES)}
        check(launch_files(results['default']) and launch_files(results['default']) == launch_files(results['c1_01']),
              "career DEBUG: ACCEPT of the default's name C1_01 gives Use Default's launch files")
        check(launch_files(results['c1_02']) != launch_files(results['default']),
              'career DEBUG: ACCEPT C1_02 prepared another mission')
        # end of input in the text menu: the dialog stays, the process ends
        game_m, menu_m = self.game('career-eof-menu')
        code_m, out = self.run_menu(game_m, menu_m, '1|TESTPILOT|2|1', extra=('-G', '-@'))
        check(code_m == 0 and b'== DEBUG ==' in out, f'career DEBUG: end of input, exit {code_m}')

    def debug_results(self):
        """(h) -@ on the results path's advance 2 (part d's state)."""
        golden = Path(os.environ.get('ES2_DATA_REPO') or self.cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
        game_r = self.pins.fetch(self.cd, 'SAV/GAME_R.SAV')[0] if self.pins else None
        if game_r is None or not (golden / 'results.dat').exists():
            print('SKIP debug results: no pinned GAME_R.SAV or no recorded c1_01-success output')
            return
        for name, menu_answer, answer in (('default', '1', 'click debug_dialog.09\n'),
                                          ('c1_02', '2|C1_02', 'click debug_dialog.06\ntype C1^02\nclick debug_dialog.11\n')):
            dirs = []
            for side in ('menu', 'shell'):
                game, menu = self.game(f'next-{name}-{side}')
                (game / 'SAV' / 'GAMEFILE.STR').write_bytes(played_gamefile((self.cd / 'SAV' / 'GAMEFILE.STR').read_bytes()))
                (game / 'SAV' / 'GAME_R.SAV').write_bytes(game_r)
                dirs.append((game, menu))
            (game_m, menu_m), (game, menu) = dirs
            code_m, out = self.run_menu(game_m, menu_m, '2|4')
            code, report = self.run_shell(game, menu, 'step 200\nclick main_menu.09\nstep 5\nexpect screen=5 game=1\n' +
                                          self.LAUNCH, f'next-{name}-launch')
            check(code_m == 2 and code == 2, f'debug results {name}: CONTINUE and Rock & Roll exit 2\n{report}')
            for g in (game_m, game):
                for n in ('results.dat', 'mission.var'):
                    shutil.copyfile(golden / n, g / 'DATA' / n)
            code_m, out = self.run_menu(game_m, menu_m, f'{menu_answer}|1|5', state=3, extra=('-G', '-@'))
            code, report = self.run_shell(game, menu, 'step 20\nexpect screen=0 quit=0\n' + answer +
                                          'step 5\nexpect screen=5 word=7 page=4\nclick service_bay.05\nstep 5\n'
                                          'expect screen=0\nclick main_menu.13\nstep 5\n', f'next-{name}', state=3,
                                          extra=('-G', '-@'))
            log = (menu / 'shell.log').read_text('latin-1')
            check(code_m == 0 and code == 0 and 'errors=0' in report and 'results C1_01' in log and
                  'debug dialog default C1_02' in log,
                  f'debug results {name}: -X3 -@, the dialog waited (default C1_02), the bay with page 4\n{report}\n{log}')
            self.same(f'debug results {name}', game_m, game)

    def chain_steps(self, menu):
        logs = sorted((menu.parent / 'chain').glob('campaign-*/chain.json'))
        if not logs:
            return []
        record = json.loads(logs[-1].read_text('utf-8'))
        steps = []
        for m in record.get('missions', []):
            for step in m.get('bay', []):
                steps.append((step['op'], step['applied'], (logs[-1].parent / step['output']).read_bytes()))
        return steps

    def main(self):
        try:
            self.synthetic()
            root = os.environ.get('ES2_DATA_ROOT')
            if not root or not (Path(root) / 'VER95' / 'VSHELL.EXE').exists():
                print('SKIP part 2: no ES2_DATA_ROOT')
            else:
                self.cd = Path(root).resolve()
                self.route_a()
                self.saved_games()
                self.editors()
                self.results()
                self.language()
                self.notice()
                self.debug_dialog()
                self.debug_career()
                self.debug_results()
        finally:
            if not failures:
                shutil.rmtree(self.tmp, ignore_errors=True)
        print(f'{checks} checks, {len(failures)} failed')
        return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(Routes(*sys.argv[1:4]).main())
