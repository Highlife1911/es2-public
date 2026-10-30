# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2launch.exe, es2shell.exe and es2sim_window.exe open no window headless or with SDL's dummy driver)
"""The game chain launcher -> shell -> sim -> shell with the real simulation, natively and as
the page runs it; skipped without ES2_DATA_ROOT.

* Web: web/node/game_host.mjs over a site that tools/web/build_site.py builds from the CD into
  TEST_TMPDIR (or the site in ES2_GAME_SITE when that directory holds a manifest.json): the
  launcher, es2shell_wasm headless and es2sim_window's wasm build in one worker over the site's
  Vfs (web/game_worker.mjs), the requests on the main thread (web/game.mjs), the overlay in a
  store with one transaction per persist message.
* Native: es2launch.exe with ES2_LAUNCH_SHELL = es2shell.exe headless and ES2_LAUNCH_SIM =
  chain_window_sim.py around es2sim_window.exe, in a game directory with the site's C:\\ES2
  (the CD's DATA, SAV, TAPES and VOL and manifest.json's language G step) and the CD as cd_root.

The first shell start makes a new career and launches Rock & Roll (exit 2); the sim replays a
journal that quits the mission at tick 201 (exit 3); the -X3 start takes the results path and
QUIT (exit 0). Both sims get the same INI (virtual time, the journal, the tick log) and the
environment block the web worker writes (C:\\ES2HOST\\ENVIRON.BIN), which must be
replay_suite.frame_environment over an empty environment.

Checks: the same steps and exit codes; both headless scripts without an error; the two tick logs
equal but for the host fields (sim/tests/tick_log_raw_diff.py) and the exit record's call counts
(the windowed message pump's follow wall time); DATA and SAV byte for byte as the native chain's
(DRIVE.CFG aside: it names the two installations' roots), the web side read as a reload reads
them (a fresh Vfs over the stored overlay); the processes' part logs (docs/web_build.md section
8.4) build a second site whose prefetch/shell.json is the shell logs' parts; then a second
session on each side (the web one over that site and the stored overlay with --prefetch: the
shell's main menu and QUIT) fetches no shell part beyond the list and leaves equal files again.
The part logs go to TEST_UNDECLARED_OUTPUTS_DIR/part-logs, the site build's --part-log input.
The second site is a local build with --selftest: the page files, the self-test page, audio.js
and the five modules under their site names (docs/web_build.md section 17), so that its build
report lists only media and CD music as absent. Last, the self-test's frame-time run
(web/selftest_run.mjs through game_host.mjs --frame-time, Q at tick 120 on paced virtual time)
over that site's own es2sim.js, es2shell.js, launch.js and vfs.js on a fresh installation: the
same processes and exit codes, the recorder's file collected from C:\\ES2HOST with a frame line
per paced frame; its busy times are printed, not judged (they are this machine's, under Node).

Arguments: es2launch.exe es2shell.exe es2sim_window.exe chain_window_sim.py build_site.py
zstd_pack.exe game_host.mjs vfs.js launch_web.js es2shell_web.js es2sim_window_web.js
replay_suite.py tick_log_raw_diff.py NODE tools_web.js audio_worklet.mjs PAGE_FILE...
"""
import codecs
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

SEED = 71
START = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
         'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n')
# MISSION twice (page 0's movies, then page 1), Rock & Roll
LAUNCH = START + ('click service_bay.19\nstep 40\nclick service_bay.19\nstep 40\nexpect page=1\n'
                  'click mission.27\nstep 5\n')
RESULTS = 'step 20\nexpect screen=5\nclick service_bay.05\nstep 5\nexpect screen=0\nclick main_menu.13\nstep 5\n'
QUIT = 'step 200\nexpect screen=0\nclick main_menu.13\nstep 5\n'
# Q at tick 201 opens the quit box, Tab and Enter take its second button: the mission ends (exit 3).
JOURNAL = [
    {"event": "script", "name": "chain-end", "note": "Q at 201, Tab, Enter: the second button"},
    {"f": 201, "api": "keydown", "vk": 81},
    {"f": 201, "p": 300, "api": "keydown", "vk": 9},
    {"f": 201, "p": 310, "api": "keyup", "vk": 9},
    {"f": 201, "p": 320, "api": "keydown", "vk": 13},
    {"f": 201, "p": 330, "api": "keyup", "vk": 13},
    {"f": 203, "api": "keyup", "vk": 81},
]
MENU_LINES = [f'seed = {SEED}', 'hangar = lowest-free', 'online_manual = text']
INSTALL = 'C:\\ES2'
CD = 'D:'
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


def quote(path):
    return f'"{Path(path).resolve()}"'


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def config(root):
    text = ('[time]\r\nvirtual_time_step_ms=47\r\nvirtual_time_start_ms=16777216\r\nvirtual_time_pace=0\r\n'
            'virtual_time_intra_frame=1\r\n[input]\r\nmode=replay\r\n'
            f'journal={root / "journal.jsonl"}\r\n[arming]\r\nearly=1\r\n')
    (root / 'journal.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in JOURNAL), encoding='ascii')
    (root / 'cfg.ini').write_bytes(codecs.BOM_UTF16_LE + text.encode('utf-16-le'))
    for name, script in (('launch.script', LAUNCH), ('results.script', RESULTS), ('quit.script', QUIT)):
        (root / name).write_text(script, encoding='ascii')


def build_site(build_site_py, zstd_pack, cd, out, part_logs=(), files=(), selftest=False, report=None):
    given = os.environ.get('ES2_GAME_SITE')
    if not part_logs and not files and given and (Path(given) / 'manifest.json').is_file():
        return Path(given).resolve()
    command = [sys.executable, '-B', build_site_py, '--cd', str(cd), '--out', str(out), '--zstd-pack', zstd_pack]
    for log in part_logs:
        command += ['--part-log', str(log)]
    for f in files:
        command += ['--file', f]
    if selftest:
        command.append('--selftest')
    if report:
        command += ['--report', str(report)]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if run.returncode != 0:
        sys.stdout.write(run.stdout.decode('utf-8', 'replace'))
        raise SystemExit('the site build failed')
    return out


def native_path(path, game, cd):
    head, _, rest = path.partition('\\')
    if path.upper().startswith(INSTALL + '\\'):
        return game / path[len(INSTALL) + 1:]
    if head.upper() == CD:
        return cd / rest
    raise ValueError(path)


def native_game(root, site, cd):
    """The site's C:\\ES2 after the language step G, as a directory."""
    manifest = json.loads((site / 'manifest.json').read_text('utf-8'))
    game = root / 'game'
    for d in manifest['directories']:
        if d.upper().startswith(INSTALL + '\\') or d.upper() == INSTALL:
            native_path(d + '\\x', game, cd).parent.mkdir(parents=True, exist_ok=True)
    for e in manifest['files']:
        if e['path'].upper().startswith(INSTALL + '\\'):
            shutil.copyfile(cd / e['path'][len(INSTALL) + 1:], native_path(e['path'], game, cd))
    lang = manifest['languages']['G']
    for d in lang['directories']:
        native_path(d, game, cd).mkdir(parents=True, exist_ok=True)
    for to, frm in lang['copies']:
        shutil.copyfile(native_path(frm, game, cd), native_path(to, game, cd))
    for to, text in lang['writes'].items():
        text = text.replace(INSTALL, str(game)).replace(CD + '\\', str(cd) + '\\')
        native_path(to, game, cd).write_bytes(text.encode('latin-1'))
    menu = root / 'menu'
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}\\'] + MENU_LINES
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    return game, menu


def files(top):
    found = {}
    for sub in ('DATA', 'SAV'):
        for p in sorted((top / sub).rglob('*')):
            if p.is_file():
                found[str(p.relative_to(top)).replace('/', '\\').upper()] = p.read_bytes()
    found.pop('DATA\\DRIVE.CFG', None)
    return found


def run_native(root, game, menu, programs, host_env, first, after=None):
    """One session of es2launch.exe in `game`: the shell's first headless script `first`, after a sim `after`."""
    es2launch, es2shell, es2sim, wrapper = programs
    root.mkdir(parents=True, exist_ok=True)
    script, report = root / 'shell.script', root / 'shell.report'
    shutil.copyfile(first, script)
    env = dict(os.environ)
    env['ES2_LAUNCH_SHELL'] = (f'{quote(es2shell)} --menu {quote(menu)} --headless --script {quote(script)} '
                               f'--report {quote(report)}')
    env['ES2_LAUNCH_SIM'] = (f'{quote(sys.executable)} -B {quote(wrapper)} --exe {quote(es2sim)} '
                             f'--config {quote(root.parents[1] / "cfg.ini")} --tick-log {quote(root / "tick.jsonl")} '
                             f'--environment {quote(host_env)} --script {quote(script)} '
                             f'--next {quote(after or first)} --report {quote(report)}')
    env['ES2_LAUNCH_LOG'] = str(root / 'launch.log')
    env['ES2_LAUNCH_NO_UI'] = '1'
    for name in ('ES2_LAUNCH_SCRIPT', 'ES2_LAUNCH_SHELL_CONSOLE', 'ES2_LAUNCH_SIM_OUTPUT'):
        env.pop(name, None)
    run = subprocess.run([str(Path(es2launch).resolve())], cwd=game, env=env, capture_output=True, timeout=1200)
    log = (root / 'launch.log').read_text('latin-1') if (root / 'launch.log').exists() else ''
    steps = []
    for line in log.splitlines():
        line = re.sub(r'^\d\d:\d\d:\d\d\.\d\d\d ', '', line)
        line = line.replace(env['ES2_LAUNCH_SHELL'], '<shell>').replace(env['ES2_LAUNCH_SIM'], '<sim>')
        steps.append(line)
    text = lambda p: p.read_text('latin-1') if p.exists() else ''
    return dict(code=run.returncode, steps=steps, files=files(game), reports=[text(report.with_name(report.name + '.1')),
                text(report)], stderr=run.stderr.decode('latin-1')[-2000:])


def tick_differences(rd, path_a, path_b):
    """The differences of two tick logs outside the host fields and the exit record's "calls": the windowed
    message pump's call counts follow wall time (message_trace.py's bounded part)."""
    read = lambda p: [json.loads(line) for line in Path(p).read_text('utf-8').splitlines() if line.strip()]
    a, b = read(path_a), read(path_b)
    found = [] if len(a) == len(b) else [f'{len(a)} lines | {len(b)} lines']
    for n, (x, y) in enumerate(zip(a, b), 1):
        for record in (x, y):
            for field in rd.HOST_FIELDS:
                record.pop(field, None)
            if record.get('event') == 'exit':
                record.pop('calls', None)
        if x != y:
            found.append(f'line {n} ({x.get("event")}): ' + ', '.join(k for k in set(x) | set(y) if x.get(k) != y.get(k)))
    return found


def run_web(root, site, node, programs, shell_args, sim_args, overlay=None, prefetch=False):
    game_host, codec, launch, shell, sim = programs
    root.mkdir(parents=True)
    command = [str(Path(node).resolve()), str(Path(game_host).resolve()), '--site', str(site), '--codec', codec,
               '--launch', launch, '--shell', shell, '--sim', sim, '--language', 'G',
               '--shell-args', json.dumps(shell_args), '--sim-args', json.dumps(sim_args),
               '--host-dir', str(root.parent), '--export', str(root / 'export'), '--export-host', str(root / 'host'),
               '--save-overlay', str(root / 'overlay.json'), '--record', str(root / 'record.json'),
               '--part-logs', str(root)]
    for line in MENU_LINES:
        command += ['--menu', line]
    if overlay:
        command += ['--overlay', str(overlay)]
    if prefetch:
        command.append('--prefetch')
    run = subprocess.run(command, capture_output=True, timeout=1200)
    record = json.loads((root / 'record.json').read_text('utf-8')) if (root / 'record.json').exists() else {}
    log = (run.stdout + run.stderr).decode('latin-1').splitlines()
    steps = [s for s in log if re.match(r'(launcher |start |exit |end )', s)]
    return dict(code=run.returncode, record=record, steps=steps, files=files(root / 'export'),
                stderr=run.stderr.decode('latin-1')[-2000:])


def run_frame_time(root, site, node, programs, ticks):
    game_host, codec, launch, shell, sim = programs
    root.mkdir(parents=True)
    command = [str(Path(node).resolve()), str(Path(game_host).resolve()), '--site', str(site), '--codec', codec,
               '--launch', launch, '--shell', shell, '--sim', sim, '--frame-time', str(ticks),
               '--export-host', str(root / 'host'), '--record', str(root / 'record.json')]
    run = subprocess.run(command, capture_output=True, timeout=1200)
    record = json.loads((root / 'record.json').read_text('utf-8')) if (root / 'record.json').exists() else {}
    return dict(code=run.returncode, record=record, stderr=run.stderr.decode('latin-1')[-2000:])


def main(args):
    (es2launch, es2shell, es2sim, wrapper, build_site_py, zstd_pack, game_host, codec, launch_web, shell_web, sim_web,
     replay_suite, raw_diff, node, tools_web, worklet) = args[:16]
    page_files = args[16:]
    modules = {'es2sim': sim_web, 'es2shell': shell_web, 'launch': launch_web, 'vfs': codec, 'tools': tools_web}
    site_files = list(page_files) + [f'audio.js={worklet}'] + [
        f'{name}{ext}={str(Path(path).resolve())[:-3]}{ext}' for name, path in modules.items() for ext in ('.js', '.wasm')]
    data_root = os.environ.get('ES2_DATA_ROOT')
    if not data_root or not (Path(data_root) / 'VER95' / 'VSHELL.EXE').exists():
        print('SKIP: no ES2_DATA_ROOT')
        return 0
    cd = Path(data_root).resolve()
    tmp = Path(tempfile.mkdtemp(prefix='game-chain-', dir=scratch_base()))
    try:
        site = build_site(build_site_py, zstd_pack, cd, tmp / 'site')
        config(tmp)
        web_programs = (game_host, codec, launch_web, shell_web, sim_web)
        shell_args = [['--headless', '--script', str(tmp / 'launch.script'), '--report', str(tmp / 'web' / 'r0.txt')],
                      ['--headless', '--script', str(tmp / 'results.script'), '--report', str(tmp / 'web' / 'r1.txt')]]
        sim_args = [['--config', str(tmp / 'cfg.ini'), '--tick-log', str(tmp / 'web' / 'tick.jsonl')]]
        web = run_web(tmp / 'web', site, node, web_programs, shell_args, sim_args)
        environ = tmp / 'web' / 'host' / 'ENVIRON.BIN'
        rs = load(replay_suite, 'replay_suite_for_game_chain')
        strings, size = rs.load_frame_environment()
        reference = rs.environment_block_bytes(rs.frame_environment({}, strings, size))
        check(environ.exists() and environ.read_bytes() == reference,
              'the worker\'s environment block is frame_environment over an empty environment')
        game, menu = native_game(tmp / 'native', site, cd)
        native_programs = (es2launch, es2shell, es2sim, wrapper)
        native = run_native(tmp / 'native' / 'run', game, menu, native_programs, environ, tmp / 'launch.script',
                            tmp / 'results.script')

        check(web['code'] == 0 and web['record'].get('state') == 0 and web['record'].get('error') is None,
              f'web: the game ended with 0\n{web["record"].get("error")}\n{web["stderr"]}')
        check(web['record'].get('steps') == ['start shell', 'exit shell 2', 'start sim', 'exit sim 3', 'start shell',
                                             'exit shell 0'], f'web: the processes {web["record"].get("steps")}')
        want_web = ['launcher ', 'start shell-replacement es2shell --menu C:\\ES2HOST\\MENU dummy -eggplant -X1 -G',
                    'exit shell-replacement 2',
                    'start sim-replacement es2sim --environment C:\\ES2HOST\\ENVIRON.BIN --arguments "-eggplant -X2 -R0 -G"',
                    'exit sim-replacement 3',
                    'start shell-replacement es2shell --menu C:\\ES2HOST\\MENU dummy -eggplant -X3 -G',
                    'exit shell-replacement 0', 'end 0']
        check(web['steps'] == want_web, f'web: the launcher log {web["steps"]}')
        want = ['launcher ', 'start shell-replacement <shell> dummy -eggplant -X1 -G', 'exit shell-replacement 2',
                'start sim-replacement <sim> --arguments "-eggplant -X2 -R0 -G"', 'exit sim-replacement 3',
                'start shell-replacement <shell> dummy -eggplant -X3 -G', 'exit shell-replacement 0', 'end 0']
        check(native['code'] == 0 and native['steps'] == want, f'native: the launcher log {native["steps"]}\n{native["stderr"]}')
        reports = [(tmp / 'web' / n).read_text('latin-1') if (tmp / 'web' / n).exists() else '' for n in ('r0.txt', 'r1.txt')]
        for name, rep in (('native', native['reports']), ('web', reports)):
            check(all('errors=0' in r for r in rep) and ' exit=2' in rep[0] and ' exit=0' in rep[1],
                  f'{name}: both headless scripts ran without an error\n{rep}')
        check(reports == native['reports'], f'the shell reports equal\n{reports}\n{native["reports"]}')

        diff = tick_differences(load(raw_diff, 'tick_log_raw_diff_for_game_chain'), tmp / 'native' / 'run' / 'tick.jsonl',
                                tmp / 'web' / 'tick.jsonl')
        check(not diff, f'the tick logs equal but for the host fields and the pump counts: {diff[:10]}')
        exits = [json.loads(line) for line in (tmp / 'web' / 'tick.jsonl').read_text('utf-8').splitlines()
                 if '"event":"exit"' in line]
        check(len(exits) == 1 and exits[0].get('exit_code') == 3 and exits[0].get('refusals') == 0,
              f'the sim quit the mission through the journal: {exits}')

        differ = sorted(k for k in set(native['files']) | set(web['files']) if native['files'].get(k) != web['files'].get(k))
        check(native['files'] and not differ, f'DATA and SAV after a reload byte for byte as the native chain\'s (differ: {differ})')
        written = {'DATA\\SCRIPT.DAT', 'DATA\\MISSION.STR', 'DATA\\PLAYER.MEC', 'DATA\\RESULTS.DAT', 'DATA\\MISSION.VAR'}
        check(written <= set(native['files']) and any(k.startswith('SAV\\') and k != 'SAV\\GAMEFILE.STR'
                                                      for k in native['files']),
              f'the chain wrote the launch files, the results and a save: {sorted(native["files"])}')
        transactions = web['record'].get('transactions', [])
        check(transactions and not any(k.upper().startswith('C:\\ES2HOST') for t in transactions for k, _ in t),
              'the store got transactions and none of the worker\'s own files')
        print(f'{len(native["files"])} files compared, {len(transactions)} transactions: '
              f'{" ".join(sorted(native["files"]))}')

        # The part logs of section 8.4: the shell's under "shell", the sim's under its mission key; a site built
        # with them has prefetch/shell.json, and the second session over it finds all its shell parts there.
        logs = sorted((tmp / 'web').glob('[0-9][0-9]-*.json'))
        missions = [json.loads(p.read_text('utf-8'))['mission'] for p in logs]
        check(missions[0::2] == ['shell', 'shell'] and re.fullmatch(r'world-?\d+-zone-?\d+', missions[1] or ''),
              f'the part logs of the three processes: {missions}')
        outputs = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
        if outputs:
            (Path(outputs) / 'part-logs').mkdir(parents=True, exist_ok=True)
            for p in logs:
                shutil.copyfile(p, Path(outputs) / 'part-logs' / p.name)
        prefetch_site = build_site(build_site_py, zstd_pack, cd, tmp / 'site-prefetch', logs, site_files, selftest=True,
                                   report=tmp / 'site-prefetch.json')
        built = json.loads((tmp / 'site-prefetch.json').read_text('utf-8'))
        check(built.get('selftest') and all(r.startswith(('media', 'CD music')) for r in built.get('absent', []))
              and (prefetch_site / 'selftest_run.mjs').is_file() and (prefetch_site / 'audio.js').read_bytes() == Path(worklet).read_bytes(),
              f'the --selftest site: every module and page file, absent only {built.get("absent")}')
        shell_list = json.loads((prefetch_site / 'prefetch' / 'shell.json').read_text('utf-8'))['parts']
        first_seen = list(dict.fromkeys(sha for p, m in zip(logs, missions) if m == 'shell'
                                        for sha in json.loads(p.read_text('utf-8'))['parts']))
        check(shell_list and shell_list == first_seen, 'prefetch/shell.json: the parts of the shell logs in first-fetch order')

        again = run_web(tmp / 'again', prefetch_site, node, web_programs,
                        [['--headless', '--script', str(tmp / 'quit.script'), '--report', str(tmp / 'again' / 'r.txt')]],
                        sim_args, overlay=tmp / 'web' / 'overlay.json', prefetch=True)
        again_path = tmp / 'again' / '00-shell.json'
        again_log = json.loads(again_path.read_text('utf-8')) if again_path.exists() else {}
        check(again_log.get('prefetched') == len(shell_list) and again_log.get('parts') == [],
              f'reload: every shell part of the second session came from prefetch/shell.json '
              f'({again_log.get("prefetched")} prefetched, {len(again_log.get("parts", []))} fetched later)')
        check(again['code'] == 0 and again['record'].get('steps') == ['start shell', 'exit shell 0'],
              f'reload: a second session over the stored overlay quits from the main menu {again["record"].get("steps")}\n'
              f'{again["stderr"]}')
        native_again = run_native(tmp / 'native' / 'again', game, menu, native_programs, environ, tmp / 'quit.script')
        check(native_again['code'] == 0 and native_again['steps'] == want[:2] + ['exit shell-replacement 0', 'end 0'],
              f'native: the second session {native_again["steps"]}')
        later = sorted(k for k in set(native_again['files']) | set(again['files'])
                       if native_again['files'].get(k) != again['files'].get(k))
        check(native_again['files'] and not later,
              f'reload: after the second session DATA and SAV as the native chain\'s (differ: {later})')

        site_programs = (game_host, *(str(prefetch_site / f'{n}.js') for n in ('vfs', 'launch', 'es2shell', 'es2sim')))
        timed = run_frame_time(tmp / 'frame-time', prefetch_site, node, site_programs, 120)
        summary = timed['record'].get('frame_time') or {}
        print('frame-time run under Node:', json.dumps(summary))
        check(timed['code'] == 0 and timed['record'].get('steps') == ['start shell', 'exit shell 2', 'start sim', 'exit sim 3',
                                                                    'start shell', 'exit shell 0'],
              f'frame time: the self-test\'s run {timed["record"].get("steps")}\n{timed["stderr"]}')
        check(summary.get('mode') == 'window' and summary.get('end') is not None and 110 <= summary.get('frames', 0) <= 200
              and (tmp / 'frame-time' / 'host' / 'perf.ndjson').is_file(),
              f'frame time: the recorder file collected with a line per paced frame {summary}')
        transactions = timed['record'].get('transactions', [])
        check(not any(k.upper().startswith('C:\\ES2HOST') for t in transactions for k, _ in t),
              'frame time: the run\'s host files never reach the store')
    finally:
        if not failures:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print('kept', tmp)
    print(f'{checks} checks, {len(failures)} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
