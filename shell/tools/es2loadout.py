# window-lint: allow-file inherits console: developer command line tool (es2menu starts no Python): runs in the console of its caller or in a Bazel test; the children share that console
"""Loadout JSON -> SAV -> launch set, proved against the original preparation.

Standard library only. The SAV is built by our formats:sav writer (through the
private es2prep_core), prepared by our shell library, then prepared again by
the mapped original VSHELL code (es2shellprep worker) from the same SAV. A
launch set is sealed only if all four files, the returned RNG/reset state and
the original reader/writer round trip of the SAV are byte-identical.
CD names and tables are read at run time; none are stored in this file.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prep = _module(HERE / 'es2prep.py', 'es2prep_for_loadout')
SHELLPREP = HERE.parents[1] / 'tools/reference-game/shell-invitro/es2shellprep.py'
LANGUAGES = ('ENG', 'FRE', 'GER')
# Executable data read at run time from the hash-checked VSHELL image.
ITEM_LIST_VA = 0x46f9a8      # thirty item IDs; item word +2 stores a position
ARMORY_LIST_VA = 0x476a4c    # 27 item IDs offered by the hangar weapon list
ARM_NAMES_VA = 0x47006c      # nine ARM_<vehicle> file name pointers
HERC_NAME_BASE = 110         # 0x411480: name = ESTEXT[vehicle position + 0x6e]
LOCUST = 26                  # live probe: DBSIM stops before tick 0


def sha(data):
    return hashlib.sha256(data).hexdigest()


def exe_read(root, va, size):
    image = (Path(root) / 'VER95' / 'VSHELL.EXE').read_bytes()
    if sha(image) != prep.REFERENCE_HASH:
        raise ValueError('VSHELL.EXE differs from the documented reference')
    pe, = struct.unpack_from('<I', image, 60)
    count, = struct.unpack_from('<H', image, pe + 6)
    optional, = struct.unpack_from('<H', image, pe + 20)
    for i in range(count):
        at = pe + 24 + optional + i * 40
        address, length, raw = struct.unpack_from('<III', image, at + 12)
        offset = va - 0x400000 - address
        if 0 <= offset and offset + size <= length:
            return image[raw + offset:raw + offset + size]
    raise ValueError(f'{va:#x} is outside the mapped file sections')


def fold(text):
    return re.sub(r'\s+', ' ', text.casefold().replace('.', ' ')).strip()


class Catalog:
    """CD catalogue as needed for validation, from the core and the EXE."""

    def __init__(self, core, root):
        self.root = Path(root)
        self.defaults = prep.default_tables(root)
        self.items = exe_read(root, ITEM_LIST_VA, 60)
        self.rows = struct.unpack('<27h', exe_read(root, ARMORY_LIST_VA, 54))   # weapon-list rows in order
        self.armory = set(self.rows) - {0}
        arm_names = []
        for pointer in struct.unpack('<9I', exe_read(root, ARM_NAMES_VA, 36)):
            name = exe_read(root, pointer, 32).split(b'\0', 1)[0].decode('ascii')
            arm_names.append(name)
        run = subprocess.run([str(core), str(root), '--catalog'], input=' '.join(arm_names) + '\n',
                             capture_output=True, text=True, check=True)
        self.vehicles = []  # (id, capacity) in executable table order
        for i in range(9):
            vid, = struct.unpack_from('<h', self.defaults, 128 + 2 * i)
            capacity, = struct.unpack_from('<H', self.defaults, 146 + 2 * i)
            self.vehicles.append((vid, capacity))
        self.weapons, self.inventory, self.hercs_dat, self.herc_info = {}, [], [], []
        self.hardpoints = {i: {} for i in range(9)}
        self.names = {}
        for line in run.stdout.splitlines():
            kind, *rest = line.split()
            if kind == 'WEAPON':
                wid, cost, flag, final, raw = rest
                self.weapons[int(wid)] = dict(cost=int(cost), flag=int(flag), final=int(final),
                                              internal=bytes.fromhex(raw).decode('latin-1'))
            elif kind == 'ITEM':
                self.inventory.append(tuple(map(int, rest)))
            elif kind == 'HERCS':
                self.hercs_dat.append(rest)
            elif kind == 'HERC_INF':
                self.herc_info.append(tuple(map(int, rest)))
            elif kind == 'ARM':
                position, item, *slots = map(int, rest)
                self.hardpoints[position][item] = sorted(s - 2 for s in slots)
            elif kind == 'NAME':
                language, table, index, *raw = rest
                text = bytes.fromhex(raw[0]).decode('cp1252') if raw else ''
                self.names[(language, table, int(index))] = text

    def vehicle(self, value):
        for position, (vid, capacity) in enumerate(self.vehicles):
            if value == vid:
                return position, vid, capacity
        raise ValueError(f'unknown Herc ID {value}')

    def herc_name(self, position, language='ENG'):
        return self.names.get((language, 'ESTEXT', HERC_NAME_BASE + position), '?')

    def weapon_name(self, wid, language='ENG'):
        return self.names.get((language, 'WEAPONS', wid), '?')

    def resolve_herc(self, value):
        if isinstance(value, bool):
            raise ValueError('Herc must be an ID or a name')
        if isinstance(value, int):
            return self.vehicle(value)
        wanted = fold(str(value))
        for position, (vid, capacity) in enumerate(self.vehicles):
            if any(fold(self.herc_name(position, lang)) == wanted for lang in LANGUAGES):
                return position, vid, capacity
        raise ValueError(f'unknown Herc name {value!r}')

    def resolve_weapon(self, value):
        if isinstance(value, bool):
            raise ValueError('weapon must be an ID or a name')
        if isinstance(value, int):
            if value not in self.weapons:
                raise ValueError(f'unknown weapon ID {value}')
            return value
        wanted = fold(str(value))
        for wid, record in self.weapons.items():
            names = [record['internal']] + [self.weapon_name(wid, lang) for lang in LANGUAGES]
            if any(fold(name) == wanted for name in names):
                return wid
        raise ValueError(f'unknown weapon name {value!r}')

    def mount_secondary(self, item):
        # 0x411bb6..0x411bde: hangar mounts store 1 for IDs 13..15, else 5.
        return 1 if 13 <= item <= 15 else 5


def describe(core, root, sav):
    run = subprocess.run([str(core), str(root), '--describe-save'], input=sav.hex() + '\n',
                         capture_output=True, text=True, check=True)
    lines = run.stdout.splitlines()
    if not lines or lines[0] != 'OK':
        raise ValueError('SAV rejected: ' + (lines[0] if lines else run.stderr.strip()))
    result = dict(pilots=[], bays=[])
    for line in lines[1:]:
        kind, *rest = line.split()
        if kind in ('PILOT', 'PLAYER'):
            index, identity, variant, bay, active, rank, order, companion = map(int, rest[:8])
            name = bytes.fromhex(rest[8]).decode('cp1252') if len(rest) > 8 else ''
            entry = dict(index=index, identity=identity, variant=variant, bay=bay, active=active,
                         rank=rank, order=order, companion=companion, name=name)
            if kind == 'PLAYER':
                result['player'] = entry
            else:
                result['pilots'].append(entry)
        elif kind == 'BAY':
            index, vehicle, position, capacity, progress, countdown, count = map(int, rest[:7])
            slots = [dict(zip(('slot', 'item', 'list_position', 'word4', 'condition', 'secondary'),
                              map(int, x.split(':')))) for x in rest[7:]]
            result['bays'].append(dict(index=index, vehicle=vehicle, position=position, capacity=capacity,
                                       progress=progress, countdown=countdown, slots=slots))
        elif kind == 'POSITION':
            result['position'] = list(map(int, rest))
        elif kind == 'SELECTION':
            result['selection'] = list(map(int, rest))
        elif kind == 'TEAM':
            result['team'] = list(map(int, rest))
        elif kind == 'FLAGS':
            result['flags'] = list(bytes.fromhex(rest[0]))
        elif kind == 'NINE':
            result['herc_availability'] = list(map(int, rest))
        elif kind == 'COUNTS':
            result['counts'] = list(map(int, rest))
        elif kind == 'TALLY':
            result['tally'] = int(rest[0])
        elif kind == 'BANK5':
            result['bank5'] = [int(rest[0])] + [list(map(int, x.split(':'))) for x in rest[1:]]
        elif kind == 'SIZE':
            result['size'], result['reader_end'], result['trailing'] = map(int, rest)
    return result


def _weapon_entries(spec, capacity):
    """Weapons as a slot-indexed list or a {slot: weapon} object."""
    if isinstance(spec, list):
        if len(spec) > capacity:
            raise ValueError(f'{len(spec)} weapons for a Herc with {capacity} slots')
        return [(slot, value) for slot, value in enumerate(spec) if value not in (None, 0)]
    if isinstance(spec, dict):
        result = []
        for key, value in spec.items():
            slot = int(key)
            if not 0 <= slot < capacity:
                raise ValueError(f'slot {slot} outside 0..{capacity - 1}')
            if value not in (None, 0):
                result.append((slot, value))
        return sorted(result)
    raise ValueError('weapons must be a list or an object')


def resolve(catalog, loadout, base_description):
    """Validate a loadout object and return the plan, a resolved summary and
    the UI findings. Raises ValueError for structural errors."""
    if not isinstance(loadout, dict) or loadout.get('schema', 1) != 1:
        raise ValueError('loadout must be an object with schema 1')
    known = {'schema', 'mission', 'campaign', 'language', 'seed', 'difficulty', 'player', 'team',
             'availability', 'variables', 'player_name', 'player_rank', 'ui_rules', 'comment'}
    unknown = set(loadout) - known
    if unknown:
        raise ValueError('unknown loadout keys: ' + ', '.join(sorted(unknown)))
    findings, summary = [], {}

    def herc(spec, who):
        if not isinstance(spec, dict) or 'herc' not in spec:
            raise ValueError(f'{who}: herc is required')
        position, vid, capacity = catalog.resolve_herc(spec['herc'])
        mounts = []
        for slot, value in _weapon_entries(spec.get('weapons', []), capacity):
            secondary = None
            if isinstance(value, dict):
                item = catalog.resolve_weapon(value.get('item'))
                if 'secondary' in value:
                    secondary = int(value['secondary'])
                    if not 0 <= secondary <= 65535:
                        raise ValueError(f'{who}: secondary outside 0..65535')
            else:
                item = catalog.resolve_weapon(value)
            if item == 0:
                continue
            if not 1 <= item <= 32:
                raise ValueError(f'{who}: item {item} outside 1..32')
            if item not in catalog.armory:
                findings.append(dict(rule='armory-list', who=who, slot=slot, item=item,
                    detail='not offered by the hangar weapon list (VA 0x476a4c)'))
            elif slot not in catalog.hardpoints[position].get(item, []):
                findings.append(dict(rule='hardpoint', who=who, slot=slot, item=item,
                    detail=f'ARM table allows slots {catalog.hardpoints[position].get(item, [])}'))
            if item == LOCUST:
                findings.append(dict(rule='known-dbsim-stop', who=who, slot=slot, item=item,
                    detail='a MEC record carrying item 26 stopped DBSIM before tick 0 (owner probe 2026-10-07)'))
            mounts.append((slot, item, secondary))
        summary[who] = dict(herc=vid, herc_name=catalog.herc_name(position), capacity=capacity,
            weapons=[dict(slot=s, item=i, name=catalog.weapon_name(i),
                          secondary=catalog.mount_secondary(i) if v is None else v) for s, i, v in mounts])
        return vid, mounts

    player = herc(loadout.get('player'), 'player')
    team = loadout.get('team', [])
    if not isinstance(team, list) or len(team) > 3:
        raise ValueError('team must be a list of at most three members')
    members, groups = [], set()
    for k, member in enumerate(team, 1):
        if not isinstance(member, dict):
            raise ValueError(f'team member {k} must be an object')
        pilot = member.get('pilot', {'group': k - 1})
        if isinstance(pilot, str):
            wanted = fold(pilot)
            matches = [p for p in base_description['pilots'] if fold(p['name']) == wanted]
            if len(matches) != 1:
                raise ValueError(f'team member {k}: pilot name {pilot!r} matches {len(matches)} roster entries')
            group, index = divmod(matches[0]['index'], 12)
        elif isinstance(pilot, dict):
            group = pilot.get('group')
            index = pilot.get('index', None)
            if not isinstance(group, int) or not 0 <= group <= 2:
                raise ValueError(f'team member {k}: roster group must be 0..2')
            if index is None:
                index = base_description['selection'][group]
            if not isinstance(index, int) or not 0 <= index <= 11:
                raise ValueError(f'team member {k}: pilot index must be 0..11')
        else:
            raise ValueError(f'team member {k}: pilot must be a name or {{"group","index"}}')
        if group in groups:
            raise ValueError(f'team member {k}: roster group {group} already supplies a member')
        groups.add(group)
        record = base_description['pilots'][group * 12 + index]
        vid, mounts = herc(member, f'team{k}')
        summary[f'team{k}'].update(pilot=dict(group=group, index=index, name=record['name'],
                                              variant=record['variant'], rank=record['rank']))
        if index != base_description['selection'][group]:
            findings.append(dict(rule='roster-selection', who=f'team{k}', detail=
                f'selected pilot of group {group} changes from {base_description["selection"][group]} to {index}; '
                'the next-replacement index is kept'))
        members.append((group, index, vid, mounts))
    flags = loadout.get('availability', 'mounted')
    if flags == 'mounted':
        flags = None
    elif isinstance(flags, list) and len(flags) == 33 and all(isinstance(x, int) and 0 <= x <= 255 for x in flags):
        used = {i for _, mounts in [player] + [(m[2], m[3]) for m in members] for _, i, _ in mounts}
        for item in sorted(used):
            if not flags[item]:
                findings.append(dict(rule='availability', who='all', item=item,
                    detail='mounted item has availability flag 0 (hangar list disables it)'))
    else:
        raise ValueError('availability must be "mounted" or 33 byte values')
    variables = loadout.get('variables', 'base')
    if variables not in ('base', 'zero'):
        raise ValueError('variables must be "base" or "zero"')
    return dict(player=player, team=members, flags=flags, zero_variables=variables == 'zero'), summary, findings


def plan_blob(plan):
    out = bytearray()
    for group, index, (vehicle, mounts) in [(0, 0, plan['player'])] + [(g, i, (v, m)) for g, i, v, m in plan['team']]:
        out += struct.pack('<BBhB', group, index, vehicle, len(mounts))
        for slot, item, secondary in mounts:
            out += struct.pack('<hhBH', slot, item, secondary is not None, secondary or 0)
    return bytes(out)


def save_request(catalog, language, group, position, plan=None, base=None,
                 player_name='Commander', player_rank=0):
    """One private --save-build request. plan=None returns the base (or the
    CD-default SAV when base is None) unchanged."""
    name = player_name.encode('cp1252')
    if not 1 <= len(name) <= 29:
        raise ValueError('player_name must have 1..29 bytes')
    if not 0 <= player_rank <= 65535:
        raise ValueError('player_rank must be 0..65535')
    flags = plan['flags'] if plan else None
    team = plan['team'] if plan else []
    return (f'{language} {int(plan is not None)} {group} {position} '
            f'{int(bool(plan and plan["zero_variables"]))} {player_rank} {len(team)} {int(flags is not None)}\n' +
            catalog.defaults[:164].hex() + '\n' + catalog.items.hex() + '\n' + (base or b'').hex() + '\n' +
            name.hex() + '\n' + (bytes(flags).hex() if flags is not None else '') + '\n' +
            (plan_blob(plan).hex() if plan else '') + '\n')


def build_saves(core, root, requests):
    """Batch of SAV builds through formats:sav; errors are returned as text."""
    run = subprocess.run([str(core), str(root), '--save-build'], input=''.join(requests),
                         capture_output=True, text=True, check=True)
    lines = iter(run.stdout.splitlines())
    results = []
    for _ in requests:
        status = next(lines, None)
        if status == 'OK':
            results.append(bytes.fromhex(next(lines)))
        elif status and status.startswith('ERROR '):
            results.append(status[6:])
        else:
            raise ValueError('invalid SAV build response')
    if next(lines, None) is not None:
        raise ValueError('extra SAV build response')
    return results


def build_sav(core, root, catalog, language, group, position, plan, base=None,
              player_name='Commander', player_rank=0):
    """Build the SAV with our formats:sav writer. base=None: CD defaults."""
    result, = build_saves(core, root, [save_request(catalog, language, group, position, plan, base,
                                                    player_name, player_rank)])
    if isinstance(result, str):
        raise ValueError('SAV build failed: ' + result)
    return result


def default_save(core, root, catalog, language, group, position, player_name='Commander', player_rank=0):
    return build_sav(core, root, catalog, language, group, position, None, None, player_name, player_rank)


def mec_records(mec):
    count, = struct.unpack_from('<H', mec, 2)
    at, records = 4, []
    for _ in range(count):
        variant, rank, vehicle, capacity = struct.unpack_from('<HHhH', mec, at)
        items = list(struct.unpack_from(f'<{capacity}h', mec, at + 8))
        secondary = list(struct.unpack_from(f'<{capacity}H', mec, at + 8 + 2 * capacity))
        records.append(dict(pilot=[variant, rank], vehicle=vehicle, capacity=capacity,
                            items=items, secondary=secondary))
        at += 76 + 4 * capacity
    return records


def prove(core, worker, root, sav_path, sav, language, group, position, seed, difficulty):
    """Our preparation and the original one from the same SAV file."""
    tables = prep.default_tables(root)
    ours = prep.prepare_batch(core, root, [prep.request(group=group, position=position, mode=1,
        difficulty=difficulty, language=language, rng=prep.initial_rng(root, seed), tables=tables,
        save=sav)])[0]
    if 'error' in ours:
        raise ValueError('our preparation failed: ' + ours['error'])
    shellprep = _module(SHELLPREP, 'es2shellprep_for_loadout')
    outputs, provenance = shellprep.generate(worker, ['--cd-root', root, '--save', sav_path,
        '--campaign', f'{group}:{position}', '--mode', '1', '--seed', seed,
        '--language', language[0], '--difficulty', difficulty & 0xffff, '--emit-save'])
    state = provenance['state']
    checks = {name: ours[name] == outputs[name] for name in prep.FILES}
    checks['rng'] = ours['rng'].hex() == state['output_rng_state']
    checks['selectors'] = ours['selectors'].hex() == state['output_reset_selectors']
    checks['sav_roundtrip'] = outputs.get(shellprep.SAVE_OUTPUT) == sav
    differences = {}
    for name in prep.FILES:
        if not checks[name]:
            a, b = ours[name], outputs[name]
            differences[name] = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    return ours, outputs, provenance, checks, differences


# ---- Service bay, WEAPONS screen ------------------------------------
# es2/shell/hangar.h through es2prep_core --service-bay; the ARM answer of a
# click comes from the CD's ARM tables (Catalog.hardpoints, the facts
# 0x41501f reads), the rest of the click rule is the library's.
BAY_MOUNT, BAY_CLICK, BAY_GUIDANCE, BAY_REBALANCE = 1, 2, 3, 4
GUIDANCE_ITEMS = (13, 14, 15, 16)      # rows with the guidance panel (0x440169..0x44018d)
GUIDANCE_MODES = (0, 1, 2, 3)          # SARH, ARH, ARM, EO (wpn_desc.bin 0x63 + 3 * mode)


def arm_admits(catalog, vehicle, item, slot):
    """0x4142f0 on the ARM tables of `vehicle` (0x41501f indexes them by the
    hangar record's vehicle word, 0x411164; the ARM files are loaded in the
    order of the pointer table 0x47006c, whose index equals the vehicle ID on
    the CD): the group of `item` holds a record for `slot` (group 0 lists
    every slot)."""
    return 0 <= vehicle < len(catalog.hardpoints) and slot in catalog.hardpoints[vehicle].get(item, [])


def bay_vehicle(description, bay):
    """(vehicle word, capacity, {slot: (item, secondary)}) of a bay, or None."""
    for record in description['bays']:
        if record['index'] == bay:
            return (record['vehicle'], record['capacity'],
                    {x['slot']: (x['item'], x['secondary']) for x in record['slots']})
    return None


def weapon_rows(catalog, description, bay, slot):
    """The 27 rows of the weapon list (0x476a4c) for a selected bay and slot:
    count (list length), availability byte, ARM answer and whether the row
    is enabled (0x440256: availability and 0x41501f)."""
    vehicle = bay_vehicle(description, bay)
    rows = []
    for row, item in enumerate(catalog.rows):
        admitted = vehicle is not None and arm_admits(catalog, vehicle[0], item, slot)
        available = description['flags'][item]
        rows.append(dict(row=row, item=item, count=description['counts'][item], available=available,
                         admitted=admitted, enabled=bool(available and admitted)))
    return rows


def click_op(catalog, description, bay, slot, item):
    """A CLICK operation with its ARM answer."""
    vehicle = bay_vehicle(description, bay)
    admitted = vehicle is not None and arm_admits(catalog, vehicle[0], item, slot)
    return (BAY_CLICK, bay, slot, item, int(admitted))


def service_bay(core, root, sav, ops, tables=b''):
    """Apply operations (kind, bay, slot, value, admitted) to a memory-state
    SAV; returns (SAV, [applied])."""
    text = f'{len(ops)}\n{tables.hex()}\n{sav.hex()}\n' + ''.join(' '.join(map(str, op)) + '\n' for op in ops)
    run = subprocess.run([str(core), str(root), '--service-bay'], input=text, capture_output=True, text=True,
                         check=True)
    lines = run.stdout.splitlines()
    if lines and lines[0].startswith('ERROR '):
        raise ValueError('service-bay operation rejected: ' + lines[0][6:])
    if len(lines) != 3 or lines[0] != 'OK' or not lines[1].startswith('APPLIED'):
        raise ValueError('invalid service-bay response')
    return bytes.fromhex(lines[2]), [int(x) for x in lines[1].split()[1:]]


def launch_files(core, root, sav):
    """player.mec and mission.var as 0x40f68c writes them for the state."""
    run = subprocess.run([str(core), str(root), '--launch-files'], input=sav.hex() + '\n', capture_output=True,
                         text=True, check=True)
    lines = run.stdout.splitlines()
    if lines and lines[0].startswith('ERROR '):
        raise ValueError('launch files rejected: ' + lines[0][6:])
    if len(lines) != 3 or lines[0] != 'OK':
        raise ValueError('invalid launch-files response')
    return bytes.fromhex(lines[1]), bytes.fromhex(lines[2])


def checked_output(path):
    destination = prep.export_path(path)
    if not destination.parent.is_dir():
        raise ValueError('output parent must already exist')
    if not re.fullmatch(r'[a-z0-9][a-z0-9_-]*', destination.name):
        raise ValueError('launch name must contain lowercase letters, digits, _ or -')
    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', required=True, type=Path)
    parser.add_argument('--worker', type=Path, help='default: es2shellprep.exe beside the core build')
    parser.add_argument('--cd-root', required=True, type=Path)
    parser.add_argument('--loadout', required=True, type=Path)
    parser.add_argument('--base-sav', type=Path, help='read-only base save; default: CD defaults')
    parser.add_argument('--output', type=Path, help='new launch directory; omit for a proof-only report')
    parser.add_argument('--report', type=Path, help='write the JSON report here as well')
    args = parser.parse_args(argv)
    core, root = args.core.resolve(), args.cd_root.resolve()
    worker = (args.worker or core.parents[1] / 'tools/reference-game/shell-invitro/es2shellprep.exe').resolve()
    destination = checked_output(args.output) if args.output else None
    loadout_bytes = args.loadout.read_bytes()
    loadout = json.loads(loadout_bytes.decode('utf-8'))
    catalog = Catalog(core, root)
    language = loadout.get('language', 'GER')
    if language not in LANGUAGES:
        raise ValueError('language must be ENG, FRE or GER')
    missions = prep.campaign(core, root)
    if 'campaign' in loadout:
        group, position = loadout['campaign']
        matches = [m for m in missions if m[:2] == (group, position)]
    else:
        matches = [m for m in missions if m[2].upper() == str(loadout.get('mission', '')).upper()]
    if len(matches) != 1:
        raise ValueError('mission/campaign must name exactly one CAREER entry')
    group, position, mission = matches[0]
    if group == 0:
        raise ValueError('CAREER group 0 is prepared in mode 0 from mission-authored equipment; '
                         'a SAV loadout applies to groups 1..5 (mode 1)')
    seed, difficulty = int(loadout.get('seed', 0)), int(loadout.get('difficulty', 0))
    if not 0 <= seed <= 65535 or not -32768 <= difficulty <= 32767:
        raise ValueError('seed 0..65535 and difficulty -32768..32767 required')
    base = args.base_sav.read_bytes() if args.base_sav else None
    if base is not None and ('player_name' in loadout or 'player_rank' in loadout):
        raise ValueError('player_name/player_rank apply to CD defaults only')
    player_name = loadout.get('player_name', 'Commander')
    player_rank = int(loadout.get('player_rank', 0))
    if not 0 <= player_rank <= 65535:
        raise ValueError('player_rank must be 0..65535')
    start = base if base is not None else default_save(core, root, catalog, language, group, position,
                                                       player_name, player_rank)
    plan, summary, findings = resolve(catalog, loadout, describe(core, root, start))
    ui_rules = loadout.get('ui_rules', 'enforce')
    if ui_rules not in ('enforce', 'report'):
        raise ValueError('ui_rules must be "enforce" or "report"')
    blocking = [f for f in findings if f['rule'] in ('armory-list', 'hardpoint', 'availability')]
    if ui_rules == 'enforce' and blocking:
        raise ValueError('hangar rules violated (set "ui_rules": "report" to override): ' + json.dumps(blocking))
    sav = build_sav(core, root, catalog, language, group, position, plan, base, player_name, player_rank)
    description = describe(core, root, sav)
    staging = None
    try:
        if destination:
            staging = Path(tempfile.mkdtemp(prefix='.loadout-', dir=destination.parent))
            sav_path = staging / 'loadout.sav'
        else:
            temporary = tempfile.TemporaryDirectory(prefix='es2loadout-')
            sav_path = Path(temporary.name) / 'loadout.sav'
        with sav_path.open('xb') as stream:
            stream.write(sav)
        ours, theirs, provenance, checks, differences = prove(core, worker, root, sav_path, sav, language,
                                                              group, position, seed, difficulty)
        records = mec_records(ours['player.mec'])
        effective = len(records) - 1
        if effective < len(plan['team']):
            findings.append(dict(rule='team-capacity', who='team', detail=
                f'{mission} admits {effective} of {len(plan["team"])} team members; the preparation '
                'deactivates orders at or above the authored first-group size'))
        script = ours['script.dat']
        world = 2 * struct.unpack_from('<h', script, 0)[0] + struct.unpack_from('<h', script, 18)[0]
        report = dict(mission=mission, group=group, position=position, language=language, seed=seed,
                      difficulty=difficulty, world=world, loadout=summary, findings=findings,
                      ui_rules=ui_rules, checks=checks, differences=differences,
                      equal=all(checks.values()),
                      files=[dict(file=n, size=len(ours[n]), sha256=sha(ours[n])) for n in prep.FILES],
                      mec=records)
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        if not report['equal']:
            print(json.dumps(report, indent=2))
            raise ValueError('original and portable preparation differ; launch set not sealed')
        if destination:
            unchecked = [
                'Inventory, purchases and money are not modeled: mounted items are new objects and the base inventory is kept.',
                'Herc availability (SAV nine-word bank) and Herc construction/purchase are not changed or checked.',
                'A byte-identical preparation does not prove that DBSIM runs this loadout; a live smoke test is required.',
                'Hangar rules checked: armory list, ARM hardpoint table, availability flags; other UI paths are not modeled.']
            manifest = dict(schema=1, name=destination.name, utc=datetime.now(timezone.utc).isoformat(),
                            files=report['files'], provenance=dict(
                    kind='es2-shell-loadout', process=None, state_source='generated-sav',
                    cd_root=str(root), algorithm_reference_sha256=prep.REFERENCE_HASH,
                    default_tables_sha256=sha(prep.default_tables(root)),
                    core_sha256=sha(core.read_bytes()),
                    loadout_json_sha256=sha(loadout_bytes), loadout_file='loadout.json',
                    base_sav=dict(path=str(args.base_sav.resolve()), sha256=sha(base)) if base is not None
                    else dict(source='cd-defaults', player_name=player_name, player_rank=player_rank,
                              sha256=sha(start)),
                    generated_sav_sha256=sha(sav), generated_sav_file='loadout.sav',
                    selection=dict(mission=mission, group=group, position=position, mode=1, world=world),
                    language=language, seed=seed, difficulty=difficulty,
                    input_rng_sha256=sha(prep.initial_rng(root, seed)),
                    output_rng_state=ours['rng'].hex(), output_reset_selectors=ours['selectors'].hex(),
                    resolved=summary, sav=description, mec=records, findings=findings,
                    ui_rules=ui_rules, unchecked=unchecked,
                    original_proof=dict(checks=checks, worker_sha256=provenance['worker_sha256'],
                        vshell_sha256=provenance['vshell_sha256'], process=provenance['process'],
                        arguments=provenance['arguments'], resources=provenance['resources'],
                        files=[dict(file=n, sha256=sha(theirs[n])) for n in prep.FILES],
                        emitted_sav_sha256=sha(theirs.get('output.sav', b'')))))
            for name in prep.FILES:
                with (staging / name).open('xb') as stream:
                    stream.write(ours[name])
            with (staging / 'loadout.json').open('xb') as stream:
                stream.write(loadout_bytes)
            with (staging / 'manifest.json').open('x', encoding='utf-8') as stream:
                json.dump(manifest, stream, indent=2)
                stream.write('\n')
            if checked_output(destination) != destination:
                raise ValueError('output changed during sealing')
            staging.rename(destination)
            staging = None
            report['directory'] = str(destination)
        print(json.dumps({k: report[k] for k in ('mission', 'group', 'position', 'world', 'equal', 'checks',
                                                 'findings', 'files')} | {'directory': report.get('directory')},
                         indent=2))
        return 0
    finally:
        if staging is not None and staging.exists():
            shutil.rmtree(staging)
        if not destination:
            temporary.cleanup()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f'es2loadout: {error}', file=sys.stderr)
        raise SystemExit(2)
