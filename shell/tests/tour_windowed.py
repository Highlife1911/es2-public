# window-lint: allow-file the windowed run is visible on purpose (a person clicks the tour path); the career run is headless (es2shell.exe is a GUI-subsystem program and opens no window headless)
"""es2shell windowed over gui_tour_real_test's tour path, with the wall-time record of its presents.

VSHELL spends the first paint's busy waits of the mission map in wall time (vshell_ui.md section 42.6): on the
tour path (MISSION from WEAPONS) the map-area state `bb8ee118` stays on screen for 0.83-0.85 s, and the first
flight frame follows the map area after 0.97-0.99 s (three -Qpc sessions). This helper lets a person
measure the same two intervals on es2shell:

  run       prepares a temporary game directory with gui_tour_real_test's career (headless, -G, Rock & Roll),
            then starts es2shell windowed (visible) N times with ES2_SHELL_PRESENT_LOG=<out>\\present-<n>.jsonl.
            In each run click: CONTINUE GAME, WEAPONS, MISSION, WEAPONS, MISSION, let the fly-over end, close
            the window. Each record is summarised after its run.
  summary   summarises record files: es2shell's present records or VSHELL's capture.jsonl (record version 2),
            which have the same shape.

    python -B shell/tests/tour_windowed.py run <es2shell.exe> <out-dir> [--runs 3]
    python -B shell/tests/tour_windowed.py summary <file> [<file> ...]

`run` needs ES2_DATA_ROOT (the CD). The summary reads, in the record, the first frame record whose index hash
starts with the map-area hash, the next one (the full frame `832f906f`) and the one after it (the first flight
frame), and prints the hold of the map area (map area to full frame), the hold of the full frame and the
interval from the map area to the first flight frame, in ms, and the host's wait record when there is one.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_gui_tour_real import MAP_AREA, FULL_FRAME, read_records, summarise, describe  # noqa: E402
import test_gui_tour_real as tour  # noqa: E402

VSHELL_HOLD_MS = (831.2, 847.8, 847.3)          # map area to full frame
VSHELL_MAP_TO_FLIGHT_MS = (973.2, 989.0, 989.5)  # map area to the first flight frame


def run(shell, out, runs):
    cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
    out.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix='tour-windowed-', dir=out))
    game, menu = tmp / 'game', tmp / 'menu'
    for sub in ('DATA', 'SAV', 'VOL'):
        (game / sub).mkdir(parents=True)
    shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
    shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
    shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}', 'hangar = lowest-free', 'seed = 12', 'chain_dir = ',
             'online_manual = text']
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    tour.run(shell, game, menu, 'career', tour.CAREER, extra=('-G',), code=2)
    if tour.failures:
        print('the career run failed:', *tour.failures, sep='\n')
        return 1
    backup = tmp / 'after-career'
    shutil.copytree(game, backup)
    for n in range(1, runs + 1):
        if n > 1:   # each run starts from the career's state (the tour run saves slot 10 at its end)
            shutil.rmtree(game)
            shutil.copytree(backup, game)
        record = out / f'present-{n}.jsonl'
        print(f'run {n}/{runs}: click CONTINUE GAME, WEAPONS, MISSION, WEAPONS, MISSION; let the fly-over end; '
              f'close the window', flush=True)
        env = dict(os.environ, ES2_SHELL_PRESENT_LOG=str(record))
        subprocess.run([str(shell), '--menu', str(menu), 'dummy', '-eggplant', '-X1', '-a'], cwd=game, env=env)
        print(describe(record, summarise(read_records(record)) if record.is_file() else None), flush=True)
    print(f'VSHELL (S-55b): map area held {" / ".join(map(str, VSHELL_HOLD_MS))} ms, map area to the first flight '
          f'frame {" / ".join(map(str, VSHELL_MAP_TO_FLIGHT_MS))} ms')
    print(f'game directory kept: {tmp}')
    return 0


def main(argv):
    if len(argv) >= 2 and argv[0] == 'summary':
        for path in argv[1:]:
            print(describe(path, summarise(read_records(path))))
        return 0
    if len(argv) >= 3 and argv[0] == 'run':
        runs = int(argv[argv.index('--runs') + 1]) if '--runs' in argv else 3
        return run(Path(argv[1]).resolve(), Path(argv[2]).resolve(), runs)
    print(__doc__)
    return 2


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
