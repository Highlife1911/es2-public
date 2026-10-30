"""The shell's sounds in es2shell: music and effects in the window, not only
the movies' sound.

VSHELL reads hmi\\gm_69.wav, bptlt2.wav, lswitch2.wav and shell1/2.wav
from VOL\\SHLSOUND.VOL (audio.md, "Files and playback format"), not from the
resource VFS (ZONES, SHELL0, SHELL1, LANG0), where none of them is. The
start-up and every play are logged to shell.log.

Needs ES2_DATA_ROOT (the CD as cd_root; SKIP without it). In temporary game
directories (the CD's PREFS.CFG: bytes 0, 1 = 1, byte 5 = 0):
a. headless (the null audio device): the start-up line with all four samples,
   the music (shell2: byte 5 = 0) looping, byte 5 flipped and written;
   lswitch2 on the main menu's first background step; gm_69 on a button
   press; bptlt2 on the bay bar's WEAPONS; no "(not loaded)".
b. headless with -s: no sound object (start-up flag 0), no play, prefs byte 5 kept.
c. the window on SDL's dummy video driver with the disk audio driver (no
   visible window, no audible sound), -X6 -a (fade-in before the main menu,
   no movies) and -X3 -a (fade-in after the results path, here without slot
   10), each ended after a few seconds: the audio file has non-silent
   samples.

Arguments: es2shell.exe
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

checks = 0
failures = []


def check(value, label):
    global checks
    checks += 1
    if not value:
        failures.append(label)
        print('FAIL', label)


def scratch_base():
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


class Sounds:
    def __init__(self, shell, cd):
        self.shell = Path(shell).resolve()
        self.cd = cd
        self.tmp = Path(tempfile.mkdtemp(prefix='gui-sound-', dir=scratch_base()))

    def game(self, name):
        root = self.tmp / name
        game = root / 'game'
        for sub in ('DATA', 'SAV', 'VOL'):
            (game / sub).mkdir(parents=True)
        (game / 'DATA' / 'language.cfg').write_bytes(b'G')
        shutil.copyfile(self.cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
        shutil.copyfile(self.cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
        shutil.copyfile(self.cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
        body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
        body += struct.pack('<hh', 1, 2) + b'c\0\0'
        (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
        menu = root / 'menu'
        menu.mkdir()
        lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {self.cd}', 'hangar = lowest-free', 'seed = 71',
                 'chain_dir = ', 'online_manual = text']
        (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
        return game, menu

    def headless(self, name, script, args):
        game, menu = self.game(name)
        before = (game / 'DATA' / 'PREFS.CFG').read_bytes()
        script_path = game.parent / 'run.script'
        report = game.parent / 'run.report'
        script_path.write_text(script, encoding='ascii')
        run = subprocess.run([str(self.shell), '--menu', str(menu), '--headless', '--script', str(script_path),
                              '--report', str(report), 'dummy', '-eggplant', *args], cwd=game,
                             capture_output=True, timeout=600, creationflags=0x08000000)
        log = (menu / 'shell.log').read_text('latin-1') if (menu / 'shell.log').exists() else ''
        text = report.read_text('latin-1') if report.exists() else ''
        return run.returncode, text, log, before, (game / 'DATA' / 'PREFS.CFG').read_bytes()

    def part_a(self):
        script = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
                  'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n'
                  'click service_bay.09\nstep 5\nexpect word=2\n')
        code, report, log, before, after = self.headless('a', script, ['-X1', '-G'])
        check('errors=0' in report, f'a: the script ran\n{report}')
        music = 'hmi\\shell1.wav' if before[5] else 'hmi\\shell2.wav'
        check(f'sound start-up: flag 1 active 1 music {music} device open samples 4 of 4' in log,
              f'a: start-up with the four samples of SHLSOUND.VOL\n{log}')
        check(f'sound play {music} volume 0 loop\n' in log, f'a: the music loops from the focus gain\n{log}')
        check(after[5] == before[5] ^ 1, f'a: prefs byte 5 flipped and written ({before[5]} -> {after[5]})')
        for sample in ('lswitch2', 'gm_69', 'bptlt2'):
            check(f'sound play hmi\\{sample}.wav volume 100\n' in log, f'a: {sample} played\n{log}')
        check('(not loaded)' not in log, f'a: no play of a missing sample\n{log}')
        # the order: lswitch2 on the main menu, gm_69 at the NEW CAREER press, bptlt2 after WEAPONS
        order = [log.find(f'sound play hmi\\{s}.wav volume 100') for s in ('lswitch2', 'gm_69', 'bptlt2')]
        check(order == sorted(order), f'a: lswitch2, gm_69, bptlt2 in that order {order}')

    def part_b(self):
        code, report, log, before, after = self.headless('b', 'step 200\nexpect screen=0\nclick main_menu.07\nstep 5\n',
                                                        ['-X1', '-G', '-s'])
        check('sound start-up: flag 0 active 0 music - device none samples 0 of 4' in log, f'b: -s, no sound object\n{log}')
        check('sound play' not in log and after[5] == before[5], f'b: -s: no play, prefs byte 5 kept\n{log}')

    def part_c(self, state):
        game, menu = self.game('c' + state)
        out = game.parent / 'audio.raw'
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='disk', SDL_AUDIO_DISK_OUTPUT_FILE=str(out))
        p = subprocess.Popen([str(self.shell), '--menu', str(menu), 'dummy', '-eggplant', state, '-a', '-G'], cwd=game,
                             env=env, creationflags=0x08000000)
        try:
            p.wait(timeout=6)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        data = out.read_bytes() if out.exists() else b''
        loud = sum(1 for b in data if b)
        log = (menu / 'shell.log').read_text('latin-1') if (menu / 'shell.log').exists() else ''
        check('device open samples 4 of 4' in log and 'sound music volume 100' in log,
              f'c {state}: the window opened the SDL device, the fade-in reached 100\n{log}')
        check(len(data) > 0 and loud > len(data) // 4,
              f'c {state}: disk audio not silent ({loud} of {len(data)} bytes nonzero)')


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = os.environ.get('ES2_DATA_ROOT')
    if not root or not (Path(root) / 'VOL' / 'SHLSOUND.VOL').exists():
        print('SKIP: ES2_DATA_ROOT with VOL\\SHLSOUND.VOL not set')
        return 0
    s = Sounds(sys.argv[1], Path(root).resolve())
    try:
        s.part_a()
        s.part_b()
        s.part_c('-X6')
        s.part_c('-X3')
    finally:
        shutil.rmtree(s.tmp, ignore_errors=True)
    print(f'{checks} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
