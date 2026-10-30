# window-lint: allow-file inherits console: developer command line tool (es2menu starts no Python): runs in the console of its caller or in a Bazel test; the children share that console
"""Filesystem front end for the portable C17 preparation library (stdlib only).

All preparation bytes travel in memory. --all reports hashes without exporting
assets. Export requires a new directory outside installations and outside
repositories, unless the repository ignores it (e.g. es2-data/oracle/launch).
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

FILES = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec')
REFERENCE_HASH = '1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def initial_rng(root, seed):
    """Read, hash-check and map the 112-byte table; never store it in source."""
    image = (Path(root) / 'VER95' / 'VSHELL.EXE').read_bytes()
    if sha(image) != REFERENCE_HASH:
        raise ValueError('VSHELL.EXE differs from the documented reference')
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    table = None
    for i in range(count):
        at = pe + 24 + optional + i * 40
        address, size, raw = struct.unpack_from('<III', image, at + 12)
        offset = 0x47f850 - 0x400000 - address
        if 0 <= offset and offset + 112 <= size:
            table = image[raw + offset:raw + offset + 112]
    if table is None or len(table) != 112:
        raise ValueError('initial RNG table is outside mapped file sections')
    words = list(struct.unpack('<56H', table))
    a, b = 24, 55
    for _ in range(seed if seed <= 32768 else 0):
        words[b] = (words[a] + words[b]) & 65535
        a, b = (a + 1) % 56, (b + 1) % 56
    return struct.pack('<56HBB', *words, a, b)


def default_tables(root):
    """Supply checked runtime tables/paths; none are source fixtures."""
    image = (Path(root) / 'VER95' / 'VSHELL.EXE').read_bytes()
    if sha(image) != REFERENCE_HASH:
        raise ValueError('VSHELL.EXE differs from the documented reference')
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    def read(va, n):
        for i in range(count):
            at = pe + 24 + optional + i * 40
            address, size, raw = struct.unpack_from('<III', image, at + 12)
            offset = va - 0x400000 - address
            if 0 <= offset and offset + n <= size:
                return image[raw + offset:raw + offset + n]
        raise ValueError('default table outside file sections')
    result = read(0x47f850, 112) + read(0x46f72c, 16) + read(0x46f868, 36)
    for pointer in struct.unpack('<9I', read(0x47083c, 36)):
        path = read(pointer, 64).split(b'\0', 1)[0]
        if len(path) >= 64:
            raise ValueError('unterminated preset path')
        result += path.ljust(64, b'\0')
    return result


# Results path tables (vshell_preparation.md "Results path"), in the
# order es2_shell_results expects: part groups, repair thresholds and levels,
# unlock values and variables, item IDs of variables 21..49, item index list.
RESULTS_TABLE_SPANS = ((0x46f9e4, 48), (0x46fec4, 12), (0x46feb8, 12), (0x46fb80, 66),
                       (0x46fbc2, 66), (0x46fa20, 58), (0x46f9a8, 60))


def results_tables(root):
    """Read the 322 table bytes from the hash-checked executable at run time."""
    image = (Path(root) / 'VER95' / 'VSHELL.EXE').read_bytes()
    if sha(image) != REFERENCE_HASH:
        raise ValueError('VSHELL.EXE differs from the documented reference')
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    out = b''
    for va, n in RESULTS_TABLE_SPANS:
        for i in range(count):
            address, size, raw = struct.unpack_from('<III', image, pe + 24 + optional + i * 40 + 12)
            offset = va - 0x400000 - address
            if 0 <= offset and offset + n <= size:
                out += image[raw + offset:raw + offset + n]
                break
        else:
            raise ValueError('results table outside file sections')
    return out


def results_request(*, tables, save, variables, results, rng, language='GER', mode=1,
                    repair=0, restock=0, position=None):
    """One private --results request for the portable core."""
    if language not in ('ENG', 'FRE', 'GER') or len(variables) != 2000 or len(rng) != 114:
        raise ValueError('results request needs a language, 2000 variable and 114 RNG bytes')
    if not (0 <= repair <= 255 and 0 <= restock <= 255 and -32768 <= mode <= 32767):
        raise ValueError('options exceed their original field widths')
    group, index = position if position is not None else (0, 0)
    return (f'{language} {mode} {repair} {restock} {int(position is not None)} {group} {index}\n' +
            tables.hex() + '\n' + save.hex() + '\n' + variables.hex() + '\n' + results.hex() + '\n' +
            rng.hex() + '\n')


def results_batch(core, root, requests, overlays=()):
    """overlays: (VFS path, file) pairs of synthetic test files shadowing the CD."""
    extra = [str(x) for pair in overlays for x in pair]
    completed = subprocess.run([str(core), str(root), '--results', *extra], input=''.join(requests),
                               text=True, capture_output=True, check=True)
    lines = iter(completed.stdout.splitlines())
    out = []
    for _ in requests:
        status = next(lines, None)
        if status == 'OK':
            meta = next(lines).split()
            refs = next(lines).split()
            if meta[0] != 'META' or refs[0] != 'REFS' or len(meta) != 13 or len(refs) != 31:
                raise ValueError('invalid results transport')
            keys = ('ending', 'advance', 'group', 'position', 'final_word', 'lost', 'total',
                    'debrief', 'passed', 'debrief_count', 'debrief_aux', 'added')
            row = dict(zip(keys, map(int, meta[1:])))
            row['debrief_refs'] = list(map(int, refs[1:]))
            for name in ('sav', 'strings', 'text', 'rng'):
                row[name] = bytes.fromhex(next(lines))
            out.append(row)
        elif status and status.startswith('ERROR '):
            out.append({'error': status[6:]})
        else:
            raise ValueError('invalid results response')
    if next(lines, None) is not None:
        raise ValueError('extra results response')
    return out


def encode_loadout(loadout):
    actors = loadout['actors']
    if not 1 <= len(actors) <= 32767:
        raise ValueError('loadout requires 1..32767 actors')
    out = bytearray(struct.pack('<HH', 0, len(actors)))
    for actor in actors:
        items = actor.get('items', [])
        secondary = actor.get('secondary', [5] * len(items))
        condition = actor.get('condition', [100] * 33)
        pilot = actor.get('pilot', [0, 0])
        if len(items) > 10 or len(secondary) != len(items) or len(condition) != 33 or len(pilot) != 2:
            raise ValueError('invalid actor array lengths')
        out += struct.pack('<HHhH', *pilot, actor['vehicle'], len(items))
        out += struct.pack('<' + 'h' * len(items), *items)
        out += struct.pack('<' + 'H' * len(items), *secondary)
        out += struct.pack('<34H', 0, *condition)
    flags = loadout.get('flags', [0] * 33)
    if len(flags) != 33:
        raise ValueError('loadout requires 33 item flags')
    return bytes(out + struct.pack('<H', 33) + bytes(flags))


def request(*, mission=None, group=0, position=0, mode=1, difficulty=0,
            player_value=0, mode0_values=(0, 0, 0, 0), language='GER',
            bank=None, rng, selectors=bytes(60), player=b'', build=True,
            tables=b'', save=None, training_vehicle=0, preset_flag=0,
            preset_index=0, retain=False, prepared_save=False):
    bank_override = bank is not None
    bank = bytes(2000) if bank is None else bank
    if mission is not None and not re.fullmatch(r'[A-Za-z0-9_]{1,31}', mission):
        raise ValueError('invalid mission stem')
    if language not in ('ENG', 'FRE', 'GER'):
        raise ValueError('invalid language')
    if len(bank) != 2000 or len(rng) != 114 or len(selectors) != 60 or max(rng[-2:]) >= 56:
        raise ValueError('state requires bank=2000, RNG=114 and selectors=60 bytes; cursors <56')
    values = (group, position, mode, difficulty, player_value)
    if any(not -32768 <= v <= 32767 for v in values) or len(mode0_values) != 4 or any(not 0 <= v <= 255 for v in mode0_values):
        raise ValueError('options exceed their original field widths')
    if not 0 <= training_vehicle <= 8 or not 0 <= preset_flag <= 255 or not -32768 <= preset_index <= 32767:
        raise ValueError('invalid training controls')
    header = ' '.join(map(str, (mission or '-', language, *values, *mode0_values,
        2 if save is not None else int(build), training_vehicle, preset_flag,
        preset_index, int(retain), int(bank_override)) + ((1,) if prepared_save else ())))
    return header + '\n' + (bank + rng + selectors).hex() + '\n' + (save if save is not None else player).hex() + '\n' + tables.hex() + '\n'


def prepare_batch(core, root, requests):
    completed = subprocess.run([str(core), str(root)], input=''.join(requests),
                               text=True, capture_output=True, check=True)
    lines = iter(completed.stdout.splitlines())
    results = []
    for text in requests:
        status = next(lines, None)
        if status == 'OK':
            try:
                buffers = {name: bytes.fromhex(next(lines)) for name in FILES}
                buffers['rng'] = bytes.fromhex(next(lines))
                buffers['selectors'] = bytes.fromhex(next(lines))
                if len(text.split('\n', 1)[0].split()) == 18:   # prepared_save=True
                    line = next(lines)
                    buffers['prepared_save'] = None if line == '-' else bytes.fromhex(line)
            except (StopIteration, ValueError) as error:
                raise ValueError('invalid preparation transport') from error
            results.append(buffers)
        elif status and status.startswith('ERROR '):
            results.append({'error': status[6:]})
        else:
            raise ValueError('invalid preparation response')
    if next(lines, None) is not None:
        raise ValueError('extra preparation response')
    return results


def campaign(core, root):
    result = subprocess.run([str(core), str(root), '--list'], text=True,
                            capture_output=True, check=True)
    return [(int(g), int(p), name) for g, p, name in
            (line.split() for line in result.stdout.splitlines())]


def summarize(buffers):
    if 'error' in buffers:
        return {'success': False, 'error': buffers['error']}
    return {'success': True, 'files': [dict(file=name, size=len(buffers[name]),
             sha256=sha(buffers[name])) for name in FILES],
            'rng_sha256': sha(buffers['rng']), 'selectors_sha256': sha(buffers['selectors'])}


# Bazel tests run with a PATH that has no git.
GIT = shutil.which('git') or 'C:/Program Files/Git/cmd/git.exe'


def git_ignored(repository, path):
    # Launch sets carry prepared CD mission data: inside a
    # repository such as es2-data they are allowed only where git ignores them.
    # Any git failure counts as not ignored.
    try:
        result = subprocess.run([GIT, '-C', str(repository), 'check-ignore', '-q', str(path)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


def export_path(path):
    # Resolve existing parents/junctions before testing ancestry. Refuse any
    # repository, including worktrees (.git is a file), not merely this checkout.
    destination = Path(path).absolute()
    for parent in (destination, *destination.parents):
        if parent.is_symlink() or (hasattr(parent, 'is_junction') and parent.is_junction()):
            raise ValueError('output ancestry contains a link/junction')
    destination = destination.resolve()
    # build/installs of any checkout: build/ is ignored by git, so the repository test below would
    # let the installations pass.
    if any(a.name.lower() == 'installs' and a.parent.name.lower() == 'build' and (a.parent.parent / '.git').exists()
           for a in (destination, *destination.parents)):
        raise ValueError('output under the installations (es2/build/installs) is forbidden')
    for parent in (destination, *destination.parents):
        if (parent / '.git').exists() and not git_ignored(parent, destination):
            raise ValueError('output inside a repository is forbidden unless the repository ignores it')
    if destination.exists():
        raise ValueError('output must be a new immutable launch directory')
    return destination


def export(buffers, path, provenance):
    destination = export_path(path)
    summary = summarize(buffers)
    if not summary['success']:
        raise ValueError(summary['error'])
    # Parent must already exist: avoid creating a chain before validation.
    destination.mkdir()
    manifest = dict(schema=1, name=destination.name,
                    utc=datetime.now(timezone.utc).isoformat(),
                    files=summary['files'], provenance=provenance)
    for name in FILES:
        with (destination / name).open('xb') as stream:
            stream.write(buffers[name])
    with (destination / 'manifest.json').open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)
        stream.write('\n')
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', required=True, type=Path)
    parser.add_argument('--cd-root', required=True, type=Path)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--mission')
    selection.add_argument('--campaign', nargs=2, type=int, metavar=('GROUP', 'POSITION'))
    selection.add_argument('--all', action='store_true')
    parser.add_argument('--group', type=int, default=0, help='group index for --mission')
    parser.add_argument('--position', type=int, default=0)
    parser.add_argument('--mode', type=int, default=None)
    parser.add_argument('--difficulty', type=int, default=0)
    parser.add_argument('--player-value', type=int, default=0)
    parser.add_argument('--mode0-values', nargs=4, type=int, default=(0, 0, 0, 0))
    parser.add_argument('--training-options', nargs=5, type=int, metavar=('C1','C2','C3','C4','C5'))
    parser.add_argument('--preset-flag', type=int, default=0)
    parser.add_argument('--preset-index', type=int, default=0)
    parser.add_argument('--language', choices=('GER', 'ENG', 'FRE'), default='GER')
    parser.add_argument('--variables', type=Path)
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--rng-state', type=Path)
    parser.add_argument('--reset-selectors', type=Path)
    loadout = parser.add_mutually_exclusive_group()
    loadout.add_argument('--player-template', type=Path)
    loadout.add_argument('--loadout', type=Path, help='JSON actors/flags; see README')
    loadout.add_argument('--save', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    if not 0 <= args.seed <= 65535:
        parser.error('--seed must be 0..65535')
    if args.all and args.output:
        parser.error('--all is a hash-only audit; omit --output')
    if args.output:
        export_path(args.output)
    rng = args.rng_state.read_bytes() if args.rng_state else initial_rng(args.cd_root, args.seed)
    bank = args.variables.read_bytes() if args.variables else None
    selectors = args.reset_selectors.read_bytes() if args.reset_selectors else bytes(60)
    if args.player_template:
        player = args.player_template.read_bytes()
    elif args.loadout:
        player = encode_loadout(json.loads(args.loadout.read_text(encoding='utf-8')))
    else:
        player = b''
    save = args.save.read_bytes() if args.save else None
    tables = default_tables(args.cd_root)
    training = args.training_options or (*args.mode0_values[:3], 0, args.mode0_values[3])
    source = 'sav' if save is not None else 'template' if args.player_template else 'loadout' if args.loadout else 'cd-defaults'
    chosen = campaign(args.core, args.cd_root) if args.all else [
        (*(args.campaign or (args.group, args.position)), args.mission)]
    requests = [request(mission=name, group=g, position=p, mode=args.mode if args.mode is not None else int(g != 0),
        difficulty=args.difficulty, player_value=args.player_value,
        mode0_values=(*training[:3],training[4]), language=args.language, bank=bank, rng=rng,
        selectors=selectors, player=player, build=not args.player_template,
        tables=tables, save=save, training_vehicle=training[3],
        preset_flag=args.preset_flag, preset_index=args.preset_index) for g, p, name in chosen]
    results = prepare_batch(args.core, args.cd_root, requests)
    report = [dict(group=g, position=p, mission=name, state_source=source,
                   mode=args.mode if args.mode is not None else int(g != 0), **summarize(result))
              for (g, p, name), result in zip(chosen, results)]
    if args.output and report[0]['success']:
        export(results[0], args.output, dict(kind='es2-shell-preparation', process=None, state_source=source,
            cd_root=str(args.cd_root.resolve()), algorithm_reference_sha256=REFERENCE_HASH,
            default_tables_sha256=sha(tables),
            selection=report[0], seed=args.seed, input_rng_sha256=sha(rng),
            input_bank_sha256=sha(bank or bytes(2000)) if bank is not None or save is None else None,
            input_bank_source='explicit' if bank is not None else 'sav' if save is not None else 'zero',
            input_loadout_sha256=sha(save if save is not None else player),
            input_selectors_sha256=sha(selectors), mode=report[0]['mode'],
            difficulty=args.difficulty, player_value=args.player_value,
            training_options=training,preset_index=args.preset_index,preset_flag=args.preset_flag,
            mode0_values=(*training[:3],training[4]), language=args.language,
            output_rng_state=results[0]['rng'].hex(),
            output_reset_selectors=results[0]['selectors'].hex()))
    print(json.dumps(report if args.all else report[0], indent=2))
    return int(any(not item['success'] for item in report))


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'es2prep: {error}', file=sys.stderr)
        raise SystemExit(2)
