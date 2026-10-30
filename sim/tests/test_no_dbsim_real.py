# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (replay_suite.py starts es2sim with its own no-window flag)
"""es2sim never opens DBSIM.EXE.

The guest image is built from the own tables (sim/data/dbsim_data.c,
dbsim_image_tables.c; own_process.md, "es2sim without DBSIM.EXE"); the CD is still read for the
VOLs, DATA, SAV and TAPES the run copies. This test

1. checks that neither es2sim.exe nor es2sim_window.exe carries the file
   path of the old image loader (the "VER95" directory) or the hash gate's
   SHA-256 of DBSIM.EXE (the path and the gate live only in the testonly
   //sim:image_file);
2. builds a temporary mirror of ES2_DATA_ROOT with every file but
   VER95\\DBSIM.EXE (hard links on the same volume, copies where a link
   fails; removed afterwards, never committed) and replays two short oracle
   suites through replay_suite.py with ES2_DATA_ROOT set to the mirror (the
   working directory's DRIVE.CFG then names the mirror too) and the goldens
   of the real oracle directory: each must be IDENTICAL.

Skipped without ES2_DATA_ROOT or without the goldens.

Arguments: es2sim.exe es2sim_window.exe replay_suite.py oracle-suites.json oracle_regression.py
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SUITES = ("c1_01-quit-dialog", "c4_10-probe")
GATE_SHA256 = b"8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"
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


def find_cd(root):
    for candidate in (root, root / 'extracted' / 'cd'):
        if (candidate / 'VOL').is_dir() and (candidate / 'DATA').is_dir():
            return candidate
    return None


def oracle_dir(root):
    override = os.environ.get('ES2_ORACLE_DIR')
    if override:
        return Path(override)
    repo = os.environ.get('ES2_DATA_REPO')
    if repo:
        return Path(repo) / 'oracle'
    if root.name.lower() == 'cd' and root.parent.name.lower() == 'extracted':
        return root.parent.parent / 'oracle'
    return root / 'oracle'


def mirror(cd, target):
    """Every file of cd below target but VER95\\DBSIM.EXE; (links, copies)."""
    links = copies = 0
    for directory, _, names in os.walk(cd):
        rel = Path(directory).relative_to(cd)
        (target / rel).mkdir(parents=True, exist_ok=True)
        for name in names:
            if str(rel / name).replace('/', '\\').upper() == 'VER95\\DBSIM.EXE':
                continue
            try:
                os.link(Path(directory) / name, target / rel / name)
                links += 1
            except OSError:
                shutil.copyfile(Path(directory) / name, target / rel / name)
                copies += 1
    return links, copies


def main(sim, window, replay, registry, oracle_script, cd, oracle):
    for exe in (sim, window):
        data = exe.read_bytes()
        check(b'VER95' not in data.upper(), f'{exe.name}: no VER95 path (the old image loader is not linked)')
        check(GATE_SHA256 not in data.lower(), f'{exe.name}: no DBSIM.EXE hash gate')
    print(f'binaries: {sim.name} and {window.name} carry neither the VER95 path nor the DBSIM.EXE hash', flush=True)

    tmp = Path(tempfile.mkdtemp(prefix='no-dbsim-', dir=scratch_base()))
    try:
        root = tmp / 'cd'
        links, copies = mirror(cd, root)
        check(not (root / 'VER95' / 'DBSIM.EXE').exists() and (root / 'VER95').is_dir() and (root / 'VOL').is_dir(),
              'mirror: VER95 without DBSIM.EXE')
        check(links + copies > 100, f'mirror: {links} links, {copies} copies')
        print(f'mirror: {links} hard links, {copies} copies, VER95\\DBSIM.EXE left out', flush=True)
        env = dict(os.environ, ES2_DATA_ROOT=str(root), ES2_ORACLE_DIR=str(oracle), TEST_TMPDIR=str(tmp / 'work'))
        env.pop('TEST_UNDECLARED_OUTPUTS_DIR', None)
        (tmp / 'work').mkdir()
        command = [sys.executable, '-B', str(replay), '--sim', str(sim), '--registry', str(registry),
                   '--oracle-script', str(oracle_script), '--output-dir', str(tmp / 'out')]
        for suite in SUITES:
            command += ['--suite', suite]
        run = subprocess.run(command, env=env, capture_output=True, timeout=3300, creationflags=0x08000000)
        text = run.stdout.decode('latin-1')
        print(text, flush=True)
        if run.returncode:
            print(run.stderr.decode('latin-1')[-2000:], flush=True)
        check(run.returncode == 0, f'replay_suite.py exit {run.returncode}')
        for suite in SUITES:
            line = next((l for l in text.splitlines() if l.startswith(f'replay {suite}:')), '')
            check(' IDENTICAL' in line, f'{suite} on the CD without DBSIM.EXE: {line or "no result line"}')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    check(not tmp.exists(), 'the temporary CD root is removed')


if __name__ == '__main__':
    data = os.environ.get('ES2_DATA_ROOT')
    cd = find_cd(Path(data).resolve()) if data else None
    if not cd:
        print('SKIP: no CD below ES2_DATA_ROOT')
        raise SystemExit(0)
    oracle = oracle_dir(cd)
    if not (oracle / 'golden').is_dir():
        print(f'SKIP: no goldens in {oracle}')
        raise SystemExit(0)
    main(*(Path(a).resolve() for a in sys.argv[1:6]), cd, oracle)
    print(f'{checks} checks, {len(failures)} failed')
    raise SystemExit(1 if failures else 0)
