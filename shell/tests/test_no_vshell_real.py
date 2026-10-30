# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2shell.exe is a GUI-subsystem program and opens no window headless)
"""es2shell and es2menu never open VSHELL.EXE.

VSHELL's tables, literals and class icon come from the span store
(shell/data/vshell_store_data.c); menu.ini's cd_root is still read
for the archives, texts and movies. This test runs the shell's real path
twice, once with ES2_DATA_ROOT as cd_root and once with a temporary mirror
of it that has every file but VER95\\VSHELL.EXE (hard links on the same
volume, copies where a link fails; removed afterwards, never committed):

1. es2shell headless: gui_tour_real_test's career (new career, seed 12,
   CREW, BUILD, MISSION, WEAPONS, MISSION, Rock & Roll with -G, exit 2)
   and its tour (CONTINUE GAME, the bay, WEAPONS, MISSION with its traced
   fly-over, OBJECTIVES, INTELLIGENCE, BRIEFING; each snapped);
2. es2menu: PRACTICE MISSIONS (entry 6, Herc Type +1, Ammo back, Begin
   Mission) and INSTANT ACTION, each exit 2 with the new game in slot 11
   (GAME_T.SAV).

Both roots must give the same exit codes, the same reports and console
output (the roots' paths replaced) and the same DATA and SAV files byte for
byte. Skipped without ES2_DATA_ROOT.

Arguments: es2shell.exe es2menu.exe test_gui_tour_real.py
"""
import importlib.util
import os
from pathlib import Path
import shutil
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
        print('FAIL', label.encode('ascii', 'backslashreplace').decode('ascii'), flush=True)


def scratch_base():
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


def mirror(cd, target):
    """Every file of cd below target but VER95\\VSHELL.EXE; (links, copies)."""
    links = copies = 0
    for directory, _, names in os.walk(cd):
        rel = Path(directory).relative_to(cd)
        (target / rel).mkdir(parents=True, exist_ok=True)
        for name in names:
            if str(rel / name).replace('/', '\\').upper() == 'VER95\\VSHELL.EXE':
                continue
            try:
                os.link(Path(directory) / name, target / rel / name)
                links += 1
            except OSError:
                shutil.copyfile(Path(directory) / name, target / rel / name)
                copies += 1
    return links, copies


def files(game):
    out = {}
    for sub in ('DATA', 'SAV'):
        for path in sorted((game / sub).iterdir()):
            out[f'{sub}\\{path.name}'.lower()] = path.read_bytes()
    return out


def shell_runs(shell, tour, cd, base):
    """The career and the tour of gui_tour_real_test against cd_root cd."""
    game, menu = base / 'game', base / 'menu'
    for sub in ('DATA', 'SAV', 'VOL'):
        (game / sub).mkdir(parents=True)
    shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
    shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
    shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}', 'hangar = lowest-free', 'seed = 12', 'chain_dir = ',
             'online_manual = text']
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    out = {}
    for name, script, extra in (('career', tour.CAREER, ('-G',)), ('tour', tour.tour(base), ())):
        (base / f'{name}.script').write_text(script, encoding='ascii')
        report = base / f'{name}.report'
        run = subprocess.run([str(shell), '--menu', str(menu), '--headless', '--script', str(base / f'{name}.script'),
                              '--report', str(report), 'dummy', '-eggplant', '-X1', '-a', *extra],
                             cwd=game, capture_output=True, timeout=600)
        text = report.read_text('latin-1') if report.exists() else ''
        out[name] = (run.returncode, text.replace(str(base), '<base>').replace(str(cd), '<cd>'))
        if name == 'tour':
            out['snaps'] = [line for line in text.splitlines() if line.startswith(('snap ', 'present '))]
    out['files'] = files(game)
    return out


def menu_runs(menu_exe, cd, base):
    """PRACTICE MISSIONS and INSTANT ACTION of es2menu against cd_root cd."""
    out = {}
    for name, script in (('practice', '7|6|14|21|10'), ('instant', '8')):
        game, menu = base / name / 'game', base / name / 'menu'
        for sub in ('DATA', 'SAV', 'VOL'):
            (game / sub).mkdir(parents=True)
        (game / 'DATA' / 'language.cfg').write_bytes(b'G')
        shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
        shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
        shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
        menu.mkdir()
        backend = base / name / 'backend.py'
        backend.write_text('import sys\nargs = sys.argv[1:]\n'
                           'open(args[args.index("--answer") + 1], "w").write("OK\\nCAMPAIGN -\\n")\n', encoding='ascii')
        (menu / 'menu.ini').write_text(f'[Menu]\npython = {sys.executable}\nbackend = {backend}\ngame_dir = {game}\n'
                                       f'cd_root = {cd}\nseed = 5\n', encoding='ascii')
        run = subprocess.run([str(menu_exe), '--menu', str(menu), 'dummy', '-eggplant', '-X1', '-G'], cwd=game,
                             input=script.replace('|', '\n').encode('ascii'), capture_output=True, timeout=300)
        text = run.stdout.decode('latin-1').replace(str(base), '<base>').replace(str(cd), '<cd>')
        out[name] = (run.returncode, text, files(game))
    return out


def main(shell, menu_exe, tour_py, cd):
    spec = importlib.util.spec_from_file_location('gui_tour_real', tour_py)
    tour = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tour)
    tmp = Path(tempfile.mkdtemp(prefix='no-vshell-', dir=scratch_base()))
    try:
        root = tmp / 'cd'
        links, copies = mirror(cd, root)
        check(not (root / 'VER95' / 'VSHELL.EXE').exists() and (root / 'VER95').is_dir(),
              'mirror: VER95 without VSHELL.EXE')
        check(links + copies > 100, f'mirror: {links} links, {copies} copies')
        print(f'mirror: {links} hard links, {copies} copies, VER95\\VSHELL.EXE left out', flush=True)

        full, bare = shell_runs(shell, tour, cd, tmp / 'shell-full'), shell_runs(shell, tour, root, tmp / 'shell-bare')
        for name, code in (('career', 2), ('tour', 0)):
            for label, result in (('full CD', full), ('no VSHELL.EXE', bare)):
                check(result[name][0] == code and 'errors=0' in result[name][1],
                      f'es2shell {name} ({label}): exit {result[name][0]}, want {code}, no script error\n'
                      f'{result[name][1][-600:]}')
            check(full[name][1] == bare[name][1], f'es2shell {name}: the reports are equal')
        check(len(bare['snaps']) > 100 and bare['snaps'] == full['snaps'],
              f'es2shell tour: {len(bare["snaps"])} snaps and presents equal')
        check(set(full['files']) == set(bare['files']) and 'sav\\game_r.sav' in bare['files'],
              f'es2shell: the same files {sorted(bare["files"])}')
        for name in sorted(set(full['files']) & set(bare['files'])):
            check(full['files'][name] == bare['files'][name], f'es2shell: {name} equal')
        print(f'es2shell: career and tour, {len(bare["snaps"])} snaps and presents, '
              f'{len(bare["files"])} files equal', flush=True)

        full, bare = menu_runs(menu_exe, cd, tmp / 'menu-full'), menu_runs(menu_exe, root, tmp / 'menu-bare')
        for name in ('practice', 'instant'):
            for label, result in (('full CD', full), ('no VSHELL.EXE', bare)):
                check(result[name][0] == 2, f'es2menu {name} ({label}): exit {result[name][0]}\n{result[name][1][-600:]}')
            check(full[name][1] == bare[name][1], f'es2menu {name}: the console output is equal')
            check(full[name][2] == bare[name][2] and 'sav\\game_t.sav' in bare[name][2],
                  f'es2menu {name}: the same DATA and SAV files {sorted(bare[name][2])}')
            print(f'es2menu {name}: exit {bare[name][0]}, {len(bare[name][2])} files equal', flush=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    check(not tmp.exists(), 'the temporary CD root is removed')


if __name__ == '__main__':
    data = os.environ.get('ES2_DATA_ROOT')
    if not data or not (Path(data) / 'VER95' / 'VSHELL.EXE').exists():
        print('SKIP: no ES2_DATA_ROOT')
        raise SystemExit(0)
    main(*(Path(a).resolve() for a in sys.argv[1:4]), Path(data).resolve())
    print(f'{checks} checks, {len(failures)} failed')
    raise SystemExit(1 if failures else 0)
