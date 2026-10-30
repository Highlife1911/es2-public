# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2launch.exe and es2shell.exe are GUI-subsystem programs and open no window headless)
"""The launcher chain launcher -> shell -> sim -> shell, natively and under
Node, in two temporary game directories (the CD's PREFS.CFG, GAMEFILE.STR and
VOL\\LANG0.VOL, own error.str and language.cfg); skipped without
ES2_DATA_ROOT or without the recorded c1_01-success output.

* Native: es2launch.exe with ES2_LAUNCH_SHELL = es2shell.exe headless.
* Web: web/launcher.mjs over launch.wasm (web/node/launcher_host.mjs) with
  the memory values memory_probe.exe reads, ES2_LAUNCH_SHELL = es2shell_wasm
  under Node (web/node/run.mjs).

The sim is the stub chain_stub_sim.py in both chains, not es2sim: it copies
DBSIM's recorded c1_01-success output into DATA, returns 3 and hands the
shell its next headless script. The first shell start makes a new career
and launches its first mission (exit 2); the -X3 start takes the results
path, the bay's MAIN MENU and QUIT (exit 0). Both chains must leave DATA
and SAV byte for byte equal, start the same commands (the prefixes aside)
with the same exit codes, and hand the stub the same DBSIM arguments.

Arguments: es2launch.exe es2shell.exe memory_probe.exe chain_stub_sim.py
launch_web.js launcher_host.mjs es2shell_web.js run.mjs NODE
"""
import os
from pathlib import Path
import re
import shutil
import struct
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


def game(root, cd):
    game = root / 'game'
    for sub in ('DATA', 'SAV', 'VOL'):
        (game / sub).mkdir(parents=True)
    (game / 'DATA' / 'language.cfg').write_bytes(b'G')
    shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
    shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
    shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
    body = struct.pack('<h', 3) + b''.join(struct.pack('<h', 3) + b'm%d\0\0' % i for i in range(3))
    body += struct.pack('<hh', 1, 2) + b'c\0\0'
    (game / 'error.str').write_bytes(struct.pack('<I', len(body)) + body)
    menu = root / 'menu'
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}', 'hangar = lowest-free', f'seed = {SEED}', 'chain_dir = ',
             'online_manual = text']
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    (root / 'launch.script').write_text(LAUNCH, encoding='ascii')
    (root / 'results.script').write_text(RESULTS, encoding='ascii')
    return game, menu


def files(game):
    return {f'{sub}\\{p.name}': p.read_bytes() for sub in ('DATA', 'SAV') for p in sorted((game / sub).iterdir())}


def run_chain(root, cd, shell_prefix, launcher, golden, stub):
    game_dir, menu = game(root, cd)
    script = root / 'shell.script'
    shutil.copyfile(root / 'launch.script', script)
    report = root / 'shell.report'
    env = dict(os.environ)
    env['ES2_LAUNCH_SHELL'] = (f'{shell_prefix} --menu {quote(menu)} --headless --script {quote(script)} '
                               f'--report {quote(report)}')
    env['ES2_LAUNCH_SIM'] = (f'{quote(sys.executable)} -B {quote(stub)} --golden {quote(golden)} --script {quote(script)} '
                             f'--next {quote(root / "results.script")} --report {quote(report)} '
                             f'--log {quote(root / "sim.log")}')
    env['ES2_LAUNCH_LOG'] = str(root / 'launch.log')
    env['ES2_LAUNCH_NO_UI'] = '1'
    for name in ('ES2_LAUNCH_SCRIPT', 'ES2_LAUNCH_SHELL_CONSOLE', 'ES2_LAUNCH_SIM_OUTPUT'):
        env.pop(name, None)
    run = subprocess.run(launcher, cwd=game_dir, env=env, capture_output=True, timeout=900)
    log = (root / 'launch.log').read_text('latin-1') if (root / 'launch.log').exists() else ''
    steps = [re.sub(r'^\d\d:\d\d:\d\d\.\d\d\d ', '', line) for line in log.splitlines()]
    steps = [line.replace(env['ES2_LAUNCH_SHELL'], '<shell>').replace(env['ES2_LAUNCH_SIM'], '<sim>') for line in steps]
    text = lambda p: p.read_text('latin-1') if p.exists() else ''
    return dict(code=run.returncode, steps=steps, files=files(game_dir), sim=text(root / 'sim.log'),
                reports=[text(report.with_name(report.name + '.1')), text(report)],
                shell_log=text(menu / 'shell.log'), stderr=run.stderr.decode('latin-1')[-2000:])


def main(args):
    es2launch, es2shell, probe, stub, launch_web, host, shell_web, run_mjs, node = args[:9]
    root = os.environ.get('ES2_DATA_ROOT')
    if not root or not (Path(root) / 'VER95' / 'VSHELL.EXE').exists():
        print('SKIP: no ES2_DATA_ROOT')
        return 0
    cd = Path(root).resolve()
    golden = Path(os.environ.get('ES2_DATA_REPO') or cd.parents[1]) / 'oracle' / 'golden' / 'c1_01-success' / 'output'
    if not (golden / 'results.dat').exists():
        print('SKIP: no recorded c1_01-success output')
        return 0
    tmp = Path(tempfile.mkdtemp(prefix='launch-chain-', dir=scratch_base()))
    try:
        memory = tmp / 'memory.txt'
        probe_run = subprocess.run([str(Path(probe).resolve()), str(memory)], capture_output=True, timeout=60)
        values = dict(line.split(' ') for line in memory.read_text('ascii').splitlines()) if memory.exists() else {}
        check(probe_run.returncode == 0 and values.get('check') == '-1',
              f'memory_probe: the native check passes ({values})')

        native = run_chain(tmp / 'native', cd, quote(es2shell), [str(Path(es2launch).resolve())], golden, stub)
        web = run_chain(tmp / 'web', cd, f'{quote(node)} {quote(run_mjs)} {quote(shell_web)}',
                        [str(Path(node).resolve()), str(Path(host).resolve()), '--memory', str(memory),
                         str(Path(launch_web).resolve()), ''], golden, stub)
        want = ['launcher ', 'start shell-replacement <shell> dummy -eggplant -X1 -G', 'exit shell-replacement 2',
                'start sim-replacement <sim> --arguments "-eggplant -X2 -R0 -G"', 'exit sim-replacement 3',
                'start shell-replacement <shell> dummy -eggplant -X3 -G', 'exit shell-replacement 0', 'end 0']
        for name, chain in (('native', native), ('web', web)):
            check(chain['code'] == 0 and chain['steps'] == want,
                  f'{name}: shell exit 2, sim exit 3, shell exit 0\n{chain["steps"]}\n{chain["stderr"]}')
            check(all('errors=0' in r for r in chain['reports']) and ' exit=2' in chain['reports'][0],
                  f'{name}: both headless scripts ran without an error\n{chain["reports"]}')
            check('results C1_01' in chain['shell_log'], f'{name}: the -X3 start took the results path\n{chain["shell_log"]}')
        check(native['sim'] == web['sim'] == '-eggplant -X2 -R0 -G\n', f'the sim arguments: {native["sim"]!r} {web["sim"]!r}')
        differ = sorted(k for k in set(native['files']) | set(web['files']) if native['files'].get(k) != web['files'].get(k))
        check(native['files'] and not differ, f'DATA and SAV byte for byte as the native chain\'s (differ: {differ})')
        written = {'DATA\\script.dat', 'DATA\\MISSION.STR', 'DATA\\player.mec', 'DATA\\results.dat', 'DATA\\mission.var'}
        check(written <= set(native['files']) and any(k.startswith('SAV\\') and k != 'SAV\\GAMEFILE.STR' for k in native['files']),
              f'the chain wrote the launch files and a save: {sorted(native["files"])}')
        print(f'{len(native["files"])} files compared: {" ".join(sorted(native["files"]))}')
    finally:
        if not failures:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print('kept', tmp)
    print(f'{checks} checks, {len(failures)} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
