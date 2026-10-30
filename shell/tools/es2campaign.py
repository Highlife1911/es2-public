# window-lint: allow-file inherits console: developer command line tool (es2menu starts no Python): runs in the console of its caller or in a Bazel test; the children share that console
"""Campaign advancement after a mission with the portable shell library.

Standard library only. Reads a SAV (as VSHELL restores it), the returned
DATA/mission.var and DATA/results.dat, runs es2_shell_results through the
private es2prep_core and prints the next CAREER position, the return path and
the debrief selection as JSON. --output-save writes the resulting SAV to a new
file outside installations and repositories (unless the repository ignores it).
Contract: docs/reverse-engineering/vshell_preparation.md, "B2 results
advancement" and "B3 debrief selection". Tables are read from the hash-checked
VSHELL.EXE at run time; the RNG state follows the original seed rule unless a
114-byte state is supplied. The original UI after the results path is not run.

Shell options ("shell options" in the same document): the mode, the
repair option and the weapons-building option live in DATA/PREFS.CFG (bytes
+0x2a, +0x2c, +0x2d), which a restarted VSHELL reads before the results path.
--prefs takes them from such a file; --menu-entry, --repair and
--weapons-building set them as the main menu and the Preferences screen do;
the raw --mode/--repair-option/--restock-option skip the UI value check.
Defaults (no option): mode 1 (a campaign entry: new game, continue game,
save/restore), repair 0 and weapons building 0: the bytes of the CD's
DATA/PREFS.CFG after a campaign entry, and the mode every recorded suite was
prepared with. --output-prefs writes the 54-byte file VSHELL would hold after
these choices.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ENDINGS = {3: 'player-lost', 4: 'other-mode', 5: 'advanced'}
# Main menu entries and the mode word they store (callers of 0x40ec56), and
# the Preferences radio buttons (0x437413/0x437474/0x4374d5, 0x437536/0x437696).
MENU_ENTRIES = {'instant-action': 0, 'new-game': 1, 'continue-game': 1, 'save-restore': 1,
                'practice-missions': 0}
REPAIR_OPTIONS = {'auto-all': 0, 'manual-mine': 1, 'manual-all': 2}
BUILD_OPTIONS = {'auto': 0, 'manual': 1}
PREFS_SIZE = 54


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prep = _module(HERE / 'es2prep.py', 'es2prep_for_campaign')


def advance(core, root, save, variables, results, *, language='GER', mode=1, repair=0, restock=0,
            seed=0, rng=None, position=None):
    if rng is None:
        rng = prep.initial_rng(root, seed)
    request = prep.results_request(tables=prep.results_tables(root), save=save, variables=variables,
                                   results=results, rng=rng, language=language, mode=mode,
                                   repair=repair, restock=restock, position=position)
    out, = prep.results_batch(core, root, [request])
    return out


def prefs_request(core, prefs, *, mode=-1, repair=-1, build=-1):
    """Shell options from a prefs.cfg image (None: the file does not open) after
    the given UI choices (-1 keeps a byte), through the private core transport.
    Returns the options the results path receives and the 54-byte block."""
    if prefs is not None and not isinstance(prefs, (bytes, bytearray)):
        raise ValueError('prefs must be bytes or None')
    request = f'{int(prefs is not None)} {int(mode)} {int(repair)} {int(build)}\n{bytes(prefs or b"").hex()}\n'
    completed = subprocess.run([str(core), '-', '--prefs'], input=request, text=True,
                               capture_output=True, check=True)
    lines = completed.stdout.splitlines()
    if len(lines) == 1 and lines[0].startswith('ERROR '):
        raise ValueError(f'shell option rejected: {lines[0][6:]}')
    if len(lines) != 2 or not lines[0].startswith('OK ') or len(lines[0].split()) != 4:
        raise ValueError('invalid prefs response')
    values = list(map(int, lines[0].split()[1:]))
    block = bytes.fromhex(lines[1])
    if len(block) != PREFS_SIZE:
        raise ValueError('invalid prefs response')
    return dict(mode=values[0], repair=values[1], restock=values[2], prefs=block)


def resolve_options(core, root, *, prefs=None, menu_entry=None, repair=None, build=None, raw_mode=None,
                    raw_repair=None, raw_restock=None, need_block=False):
    """Mode, repair and restock options for the results path, plus the prefs
    block when one was read or requested. Without a prefs file and without UI
    choices the defaults hold. A prefs file is read as VSHELL's start-up reads
    it; UI choices are stored as the main menu and the Preferences screen store
    them (base without --prefs: the CD DATA/PREFS.CFG after a campaign entry);
    raw values replace the result without a UI check."""
    ui = menu_entry is not None or repair is not None or build is not None
    options = dict(mode=1, repair=0, restock=0, source='defaults')
    block = None
    if prefs is not None or ui or need_block:
        base = prefs if prefs is not None else (Path(root) / 'DATA' / 'PREFS.CFG').read_bytes()
        if menu_entry is not None:
            mode = MENU_ENTRIES[menu_entry]
        else:
            mode = -1 if prefs is not None else 1
        out = prefs_request(core, base, mode=mode,
                            repair=REPAIR_OPTIONS[repair] if repair is not None else -1,
                            build=BUILD_OPTIONS[build] if build is not None else -1)
        options = dict(mode=out['mode'], repair=out['repair'], restock=out['restock'],
                       source='prefs' if prefs is not None else 'cd-prefs')
        block = out['prefs']
    if raw_mode is not None or raw_repair is not None or raw_restock is not None:
        if need_block:
            raise ValueError('raw options cannot be written to a prefs file; use the UI options')
        for key, value in (('mode', raw_mode), ('repair', raw_repair), ('restock', raw_restock)):
            if value is not None:
                options[key] = value
        options['source'] += '+raw'
    return options, block


def summary(out, core, root, language):
    if 'error' in out:
        return dict(success=False, error=out['error'])
    result = dict(success=True, ending=ENDINGS.get(out['ending'], out['ending']), advance=out['advance'],
                  final_word=out['final_word'], total=out['total'], lost_pilots=out['lost'],
                  next=dict(group=out['group'], position=out['position']),
                  save=dict(size=len(out['sav']), sha256=prep.sha(out['sav'])),
                  rng_state=out['rng'].hex())
    names = {(g, p): name for g, p, name in prep.campaign(core, root)}
    result['next']['mission'] = names.get((out['group'], out['position']))
    if out['debrief']:
        result['debrief'] = dict(references=out['debrief_refs'], count=out['debrief_count'],
                                 auxiliary=out['debrief_aux'], passed_records=out['passed'],
                                 mission_str=dict(size=len(out['strings']), sha256=prep.sha(out['strings'])),
                                 text=dict(size=len(out['text']), sha256=prep.sha(out['text'])))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', required=True, type=Path)
    parser.add_argument('--cd-root', required=True, type=Path)
    parser.add_argument('--save', required=True, type=Path, help='SAV as restored (e.g. save index 10)')
    parser.add_argument('--mission-var', required=True, type=Path, help='returned DATA/mission.var')
    parser.add_argument('--results', required=True, type=Path, help='returned DATA/results.dat')
    parser.add_argument('--language', choices=('GER', 'ENG', 'FRE'), default='GER')
    parser.add_argument('--prefs', type=Path,
                        help='data\\prefs.cfg read as by the restarted VSHELL (mode +0x2a, options +0x2c/+0x2d)')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--menu-entry', choices=tuple(MENU_ENTRIES),
                      help='main menu entry that set the mode (default without --prefs: a campaign entry, mode 1)')
    mode.add_argument('--mode', type=int, help='raw shell mode word 0x4826f0 (no UI check)')
    repair = parser.add_mutually_exclusive_group()
    repair.add_argument('--repair', choices=tuple(REPAIR_OPTIONS),
                        help='Preferences "Repair Options", option byte 0x4825c8 (default auto-all)')
    repair.add_argument('--repair-option', type=int, help='raw option byte 0x4825c8 (no UI check)')
    build = parser.add_mutually_exclusive_group()
    build.add_argument('--weapons-building', choices=tuple(BUILD_OPTIONS),
                       help='Preferences "Weapons Building", option byte 0x4825c9 (default auto)')
    build.add_argument('--restock-option', type=int, help='raw option byte 0x4825c9 (no UI check)')
    parser.add_argument('--output-prefs', type=Path, help='new file for the 54-byte prefs.cfg after the choices '
                        '(base: --prefs, else the CD DATA/PREFS.CFG)')
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--rng-state', type=Path, help='114-byte RNG state instead of --seed')
    parser.add_argument('--output-save', type=Path, help='new file for the resulting SAV')
    args = parser.parse_args(argv)
    if not 0 <= args.seed <= 65535:
        parser.error('--seed must be 0..65535')
    if args.output_save:
        prep.export_path(args.output_save)
    if args.output_prefs:
        prep.export_path(args.output_prefs)
    options, block = resolve_options(
        args.core, args.cd_root, prefs=args.prefs.read_bytes() if args.prefs else None,
        menu_entry=args.menu_entry, repair=args.repair, build=args.weapons_building, raw_mode=args.mode,
        raw_repair=args.repair_option, raw_restock=args.restock_option, need_block=bool(args.output_prefs))
    rng = args.rng_state.read_bytes() if args.rng_state else None
    out = advance(args.core, args.cd_root, args.save.read_bytes(), args.mission_var.read_bytes(),
                  args.results.read_bytes(), language=args.language, mode=options['mode'],
                  repair=options['repair'], restock=options['restock'], seed=args.seed, rng=rng)
    result = summary(out, args.core, args.cd_root, args.language)
    result['options'] = dict(mode=options['mode'], repair_option=options['repair'],
                             restock_option=options['restock'], source=options['source'])
    if args.output_save and result['success']:
        destination = prep.export_path(args.output_save)
        with destination.open('xb') as stream:
            stream.write(out['sav'])
        result['save']['path'] = str(destination)
    if args.output_prefs and result['success']:
        destination = prep.export_path(args.output_prefs)
        with destination.open('xb') as stream:
            stream.write(block)
        result['options']['prefs'] = dict(path=str(destination), sha256=prep.sha(block))
    print(json.dumps(result, indent=2))
    return int(not result['success'])


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'es2campaign: {error}', file=sys.stderr)
        raise SystemExit(2)
