# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2shell.exe is a GUI-subsystem program and opens no window headless)
"""es2shell's BUILD and ARMORY frames with the real German data.

es2shell runs headless (--script with `dump`) in a temporary game directory
(the CD's PREFS.CFG, GAMEFILE.STR and VOL\\LANG0.VOL, as gui_route_real_test)
on a new career (TESTPILOT) in the service bay, German (-G):

a. BUILD, the empty sixth bay, the Tomahawk row, Build: the squad panel's
   sixth row in the frame right after the Build (VSHELL's 0x4475ce runs
   0x43e0d7, 0x43e167, 0x43da0c at once) differs from the row before the
   Build (trap: reading the SAV the Build has just freed keeps the old row
   until the next interaction).
b. ARMORY: the column of the cost word / 1000 (the constructor's
   0x449847..0x449863, from the start-up weapon table) holds text in every
   row with a weapon (trap: written only when the screens are built with a
   game loaded, it stays empty).

Only frame regions are compared and counted; no frame is stored. Skipped
without ES2_DATA_ROOT.

Arguments: es2shell.exe
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

W, H = 640, 480
checks = 0
failures = []


def check(value, label):
    global checks
    checks += 1
    if not value:
        failures.append(label)
        print('FAIL', label, flush=True)


def scratch_base():
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


def frame(path):
    """The palette indices of a `dump` BMP (top-down, 8 bits)."""
    data = path.read_bytes()
    offset, = struct.unpack_from('<I', data, 10)
    return data[offset:offset + W * H]


def region(pixels, left, top, right, bottom):
    return b''.join(pixels[y * W + left:y * W + right] for y in range(top, bottom))


START = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
         'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n')


def main(shell, cd):
    tmp = Path(tempfile.mkdtemp(prefix='gui-bay-', dir=scratch_base()))
    game, menu = tmp / 'game', tmp / 'menu'
    for sub in ('DATA', 'SAV', 'VOL'):
        (game / sub).mkdir(parents=True)
    (game / 'DATA' / 'language.cfg').write_bytes(b'G')
    shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
    shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
    shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}', 'hangar = lowest-free', 'seed = 71', 'chain_dir = ',
             'online_manual = text']
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    dump = {k: tmp / f'{k}.bmp' for k in ('before', 'built', 'armory')}
    script = (START + 'click service_bay.13\nstep 5\nexpect word=4\n'
              # the sixth bay (squad_panel.17 + 5 k, k = 5) is empty on a new career
              'click squad_panel.42\nstep 3\nexpect bay=5\nclick build.22\nstep 3\n'
              f'dump {dump["before"]}\nclick build.78\ndump {dump["built"]}\n'
              f'click service_bay.15\nstep 5\nexpect word=5\ndump {dump["armory"]}\n')
    (tmp / 'bay.script').write_text(script, encoding='ascii')
    run = subprocess.run([str(shell), '--menu', str(menu), '--headless', '--script', str(tmp / 'bay.script'),
                          '--report', str(tmp / 'bay.report'), 'dummy', '-eggplant', '-X1', '-G'], cwd=game,
                         capture_output=True, timeout=600)
    report = (tmp / 'bay.report').read_text('latin-1') if (tmp / 'bay.report').exists() else ''
    check(run.returncode == 0 and 'errors=0' in report, f'es2shell: exit 0, no script error\n{report}')
    log = (menu / 'shell.log').read_text('latin-1') if (menu / 'shell.log').exists() else ''
    check('editor 0x4475ce applied 1' in log, f'the Build was applied (shell.log)\n{log}')
    if not all(p.exists() for p in dump.values()):
        check(False, 'every dump written')
        return tmp
    f = {k: frame(p) for k, p in dump.items()}
    # (a) the squad panel's sixth row: ESQuad (8, 0x3c + 12 k ...) under the
    # panel; the band 15..236 x 432..446 holds its number, name and state
    row = lambda pixels: region(pixels, 15, 432, 236, 446)
    check(row(f['built']) != row(f['before']), 'BUILD: the sixth squad row changes in the frame of the Build')
    # (b) ARMORY: the cost column (x 233..270, rows 0x48 + 12 r under the
    # list at y 71): each weapon row's band has ink besides the background
    blank = 0
    weapon_rows = 0
    for r in range(26):
        top = 71 + 0x48 + 12 * r - 12
        name = region(f['armory'], 70, top, 200, top + 12)
        if len(set(name)) < 2:
            continue   # a separator row without a weapon
        weapon_rows += 1
        if len(set(region(f['armory'], 240, top, 271, top + 12))) < 2:
            blank += 1
    check(weapon_rows >= 16 and blank == 0, f'ARMORY: the cost column filled ({weapon_rows} weapon rows, {blank} blank)')
    return tmp


if __name__ == '__main__':
    root = os.environ.get('ES2_DATA_ROOT')
    if not root or not (Path(root) / 'VER95' / 'VSHELL.EXE').exists():
        print('SKIP: no ES2_DATA_ROOT')
        raise SystemExit(0)
    tmp = main(Path(sys.argv[1]).resolve(), Path(root).resolve())
    if not failures:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'{checks} checks, {len(failures)} failed')
    raise SystemExit(1 if failures else 0)
