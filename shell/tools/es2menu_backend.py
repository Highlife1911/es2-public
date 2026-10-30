# window-lint: allow-file inherits console: runs inside a console or a Bazel test; the children share that console
"""Set-up and test front end of the own menu es2menu.exe. Standard library
only.

es2menu.exe keeps the campaign itself (shell/src/engine_io.c over
es2/shell/engine.h, VSHELL's state in SAV\\ and DATA\\) and starts no backend.
What stays here:

    es2menu_backend.py setup --menu NEW_DIR --menu-exe EXE --core CORE --cd-root CD --game-dir GAME ...

writes menu.ini (read by es2menu: game_dir, cd_root, seed, hangar, chain_dir;
core is recorded for the evidence log's tools) and prints the
ES2_LAUNCH_SHELL value; and new_game_request(), the new career through
es2prep_core --new-game, which the real-data tests use as their reference.
The evidence log (chain_dir, default the menu directory; ES2_MENU_CHAIN_DIR
overrides it, empty switches it off) holds campaign-NNN directories in
es2chain's schema 1 and `current`. docs/reverse-engineering/launcher.md,
sections "Own menu" and "Shell state engine".
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ITEM_LIST_SIZE = 60   # last span of es2prep.RESULTS_TABLE_SPANS: the thirty item IDs at 0x46f9a8
SKILLS = 4            # ROOKIE, REGULAR, VETERAN, ELITE (rank word 0..3)


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


chain_tool = _module(HERE / 'es2chain.py', 'es2chain_for_menu')
prep = chain_tool.prep


# ---- new career through es2_shell_new_game (reference for the tests) -------
def new_game_request(core, root, language, name, rank, group, position, rng):
    """SAV, RNG and salvage of the new career (VSHELL 0x40e8a5)."""
    raw = name.encode('ascii')
    if not 1 <= len(raw) <= 29 or any(not 0x20 <= b < 0x7f for b in raw):
        raise ValueError('pilot name must have 1..29 printable ASCII characters')
    if not 0 <= rank < SKILLS or len(rng) != 114:
        raise ValueError('skill must be 0..3 and the RNG state 114 bytes')
    items = prep.results_tables(root)[-ITEM_LIST_SIZE:]
    text = (f'{language} {rank} {group} {position}\n' + prep.default_tables(root)[:164].hex() + '\n' +
            items.hex() + '\n' + raw.hex() + '\n' + rng.hex() + '\n')
    run = subprocess.run([str(core), str(root), '--new-game'], input=text, text=True,
                         capture_output=True, check=True)
    lines = run.stdout.splitlines()
    if lines and lines[0].startswith('ERROR '):
        raise ValueError('new game rejected: ' + lines[0][6:])
    if len(lines) != 4 or lines[0] != 'OK' or not lines[3].startswith('SALVAGE '):
        raise ValueError('invalid new-game response')
    return bytes.fromhex(lines[1]), bytes.fromhex(lines[2]), int(lines[3].split()[1])


def setup(args):
    game = chain_tool.checked_game_dir(args.game_dir)
    directory = prep.export_path(args.menu)
    exe = Path(args.menu_exe).resolve()
    directory.mkdir()
    lines = ['[Menu]', f'core = {Path(args.core).resolve()}', f'cd_root = {Path(args.cd_root).resolve()}',
             f'game_dir = {game}', f'hangar = {args.hangar}',
             f'seed = {"" if args.seed is None else args.seed}',
             f'chain_dir = {"" if args.no_log else directory}']
    (directory / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    return dict(menu=str(directory), launch_shell=f'"{exe}" --menu "{directory}"')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    first = commands.add_parser('setup', help='create a menu directory and print ES2_LAUNCH_SHELL')
    first.add_argument('--menu', required=True, type=Path, help='new menu directory')
    first.add_argument('--menu-exe', required=True, type=Path, help='es2menu.exe')
    first.add_argument('--core', required=True, type=Path, help='es2prep_core.exe (recorded for the log tools)')
    first.add_argument('--cd-root', required=True, type=Path)
    first.add_argument('--game-dir', required=True, type=Path, help='copy of an installation (not native-r111)')
    first.add_argument('--seed', type=int, help='fixed start-up draw count 0..127 (default: GetTickCount() & 127)')
    first.add_argument('--hangar', choices=('stop', 'lowest-free'), default='lowest-free',
                       help='player without a Herc: es2chain policy (default lowest-free)')
    first.add_argument('--no-log', action='store_true', help='no evidence log (chain_dir empty)')
    args = parser.parse_args(argv)
    if args.seed is not None and not 0 <= args.seed <= 127:
        parser.error('--seed must be 0..127')
    print(json.dumps(setup(args), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
