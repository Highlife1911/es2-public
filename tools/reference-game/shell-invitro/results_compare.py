# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Differential sweep of the VSHELL results path and debrief selection.

Standard library only. Every case is a SAV built by our formats:sav writer
(CD defaults plus a loadout plan, then field edits made here), a synthetic
mission.var and results.dat, and an RNG state from the original seed rule.
Both the portable shell library (es2prep_core --results) and the original
VSHELL code (es2shellprep --results-batch, UI entries trapped) process it; the
SAV image, CAREER position, return path, final word, debrief record, rewritten
MISSION.STR, debrief text and RNG state are compared in memory. Reports
contain hashes and field names only. No game process, no installation access.
"""
import argparse
import importlib.util
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EXTREMES = (-32768, -1, 0, 1, 29, 30, 79, 80, 81, 99, 100, 101, 32767)
LANGUAGE_LETTER = dict(ENG='E', FRE='F', GER='G')


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


# ---- SAV model (framing as in docs/formats/mission.md) ----------------------
class Pilot:
    def __init__(self, prefix, name, suffix):
        self.prefix, self.name, self.suffix = prefix, name, bytearray(suffix)

    def get(self, offset):
        if offset == 0x24:
            return self.suffix[2]
        return struct.unpack_from('<h', self.suffix, offset - 0x22)[0]

    def set(self, offset, value):
        if offset == 0x24:
            self.suffix[2] = value & 255
        else:
            struct.pack_into('<H', self.suffix, offset - 0x22, value & 0xffff)

    def bytes(self):
        return self.prefix + struct.pack('<h', len(self.name)) + self.name + bytes(self.suffix)


class Sav:
    """Mutable SAV projection; bays map index -> [fixed(76), {slot: 10 bytes}]."""

    @classmethod
    def parse(cls, data):
        self, at = cls(), 0

        def take(n):
            nonlocal at
            if at + n > len(data):
                raise ValueError('truncated SAV')
            value = data[at:at + n]
            at += n
            return value

        def word():
            return struct.unpack('<h', take(2))[0]

        def pilot():
            prefix = take(4)
            name = take(word())
            return Pilot(prefix, name, take(25))
        self.inventory = []
        for _ in range(33):
            flag = take(1)[0]
            n = word()
            self.inventory.append([flag, [take(10) for _ in range(n)]])
        self.five = bytearray(take(22))
        self.selection = bytearray(take(152))
        self.roster = [pilot() for _ in range(36)]
        self.roster_selection = list(struct.unpack('<6H', take(12)))
        self.team = list(struct.unpack('<2h', take(4)))
        self.player = pilot()
        self.bays = {}
        for _ in range(word()):
            index = word()
            fixed = bytearray(take(76))
            items = {}
            for _ in range(word()):
                slot = word()
                items[slot] = take(10)
            self.bays[index] = [fixed, items]
        self.nine = bytearray(take(18))
        self.total = struct.unpack('<I', take(4))[0]
        self.variables = bytearray(take(2000))
        self.final = bytearray(take(22))
        return self

    def build(self):
        out = bytearray()
        for flag, items in self.inventory:
            out += bytes([flag]) + struct.pack('<h', len(items)) + b''.join(items)
        out += self.five + self.selection + b''.join(p.bytes() for p in self.roster)
        out += struct.pack('<6H', *self.roster_selection) + struct.pack('<2h', *self.team)
        out += self.player.bytes() + struct.pack('<h', len(self.bays))
        for index in sorted(self.bays):
            fixed, items = self.bays[index]
            out += struct.pack('<h', index) + fixed + struct.pack('<h', len(items))
            for slot in sorted(items):
                out += struct.pack('<h', slot) + items[slot]
        out += self.nine + struct.pack('<I', self.total & 0xffffffff) + self.variables + self.final
        return bytes(out)

    def position(self, group, index):
        struct.pack_into('<hh', self.selection, 0, group, index)

    def selected(self, g):
        return self.roster[g * 12 + self.roster_selection[g]]

    def consumers(self):
        """Pilots whose results records VSHELL reads, in reading order."""
        result = [self.player]
        for order in range(1, self.team[1]):
            for g in range(3):
                if self.selected(g).get(0x27) == order:
                    if self.selected(g).get(0x24):
                        result.append(self.selected(g))
                    break
        return result


def bay_fixed(vehicle, position, capacity, conditions=100, progress=100, countdown=0):
    fixed = bytearray(76)
    struct.pack_into('<hh', fixed, 0, vehicle, position)
    struct.pack_into('<33h', fixed, 4, *([conditions] * 33 if isinstance(conditions, int) else conditions))
    struct.pack_into('<hhh', fixed, 70, progress, countdown, capacity)
    return fixed


def item_record(catalog, item, condition=100, secondary=5):
    indices = struct.unpack('<30h', catalog.items)
    index = indices.index(item) if item in indices else -1
    return struct.pack('<5h', item, index, 100, condition, secondary)


# ---- Synthetic inputs -----------------------------------------------------------
def condition_words(rnd, kind):
    if kind == 'healthy':
        return [100] * 33
    if kind == 'lost':
        return [0] * 33
    if kind == 'pilot-lost':
        words = [100] * 33
        words[22] = 0
        return words
    if kind == 'extreme':
        return [rnd.choice(EXTREMES) for _ in range(33)]
    if kind == 'slots':
        words = [rnd.randrange(30, 101) for _ in range(23)] + [rnd.choice((0, 79, 80, 81, 100)) for _ in range(10)]
        return words
    if kind == 'boundary':
        words = [rnd.choice((28, 29, 30, 31)) for _ in range(33)]
        return words
    return [rnd.randrange(-10, 111) for _ in range(33)]


def results_bytes(sav, rnd, outcome, tally, pairs, kinds, words=None):
    out = bytearray(struct.pack('<hih', outcome, tally, len(pairs)))
    for a, b in pairs:
        out += struct.pack('<hh', a, b)
    for i, pilot in enumerate(sav.consumers()):
        if pilot.get(0x22) in sav.bays:
            out += struct.pack('<33h', *condition_words(rnd, kinds[i % len(kinds)]))
        triple = words[i % len(words)] if words else [rnd.randrange(0, 12) for _ in range(3)]
        out += struct.pack('<3h', *triple)
    # Trailing bytes are ignored by both readers; they keep the cursor in range
    # if the original reaches further (duplicate bays), so errors stay faults.
    return bytes(out) + bytes(rnd.randrange(256) for _ in range(160))


def bank_bytes(rnd, profile, extra=None):
    words = [0] * 1000
    if profile == 'items':
        for var in range(21, 43):
            words[var] = rnd.choice((0, 0, 1, 2, 3))
    if profile in ('unlock', 'items'):
        for var in range(50, 64):
            words[var] = rnd.choice((0, 1, 2, 3, -1))
    if profile == 'random':
        for var in rnd.sample(range(10, 1000), 40):
            if 43 <= var <= 49:
                continue
            words[var] = rnd.randrange(-3, 40) if not 21 <= var <= 42 else rnd.randrange(0, 4)
    for var, value in (extra or {}).items():
        words[var] = value
    return struct.pack('<1000h', *words)


# ---- Original worker transport ----------------------------------------------------
def decode(data):
    rows, at = [], 0
    while at < len(data):
        if data[at:at + 4] != b'ES2R':
            raise ValueError('bad results packet magic')
        at += 4
        blocks = []
        for _ in range(4):
            if at + 4 > len(data):
                raise ValueError('truncated results packet')
            size, = struct.unpack_from('<I', data, at)
            at += 4
            if size > 16 * 1024 * 1024 or at + size > len(data):
                raise ValueError('bad results packet extent')
            blocks.append(data[at:at + size])
            at += size
        meta = json.loads(blocks[0])
        calls = meta['ui_calls']
        addresses = [c[0] for c in calls]
        if [0x44d0e7, 3] in [c[:2] for c in calls]:
            ending, advance = 3, -1
        elif 0x40f904 not in addresses:
            ending, advance = 4, -1
        else:
            ending = 5
            advance = (1 if 0x40e933 in addresses else 0 if [0x44d0e7, 0] in [c[:2] for c in calls]
                       else 2 if 0x413305 in addresses else -1)
        rows.append(dict(ending=ending, advance=advance, group=meta['after'][0], position=meta['after'][1],
                         final_word=meta['final_word'], lost=meta['lost'], total=meta['total'],
                         debrief=meta['debrief_ran'], debrief_count=meta['debrief_count'],
                         debrief_refs=meta['debrief_refs'], debrief_aux=meta['debrief_aux'],
                         sav=blocks[1], strings=blocks[2], text=blocks[3],
                         rng=bytes.fromhex(meta['output_rng_state']), ui_calls=calls))
    return rows


def worker_request(case):
    position = case.get('position_override', (-1, -1))
    return (f"{case['mode']} {case['repair']} {case['restock']} {position[0]} {position[1]}\n" +
            case['sav'].hex() + '\n' + case['bank'].hex() + '\n' + case['results'].hex() + '\n' +
            case['rng'].hex() + '\n')


def original_batch(worker, cd, language, cases, overlays=()):
    """A fault or assertion ends one worker; remaining cases run in a new one."""
    out = []
    extra = [x for name, path in overlays for x in ('--overlay', name, str(path))]
    while len(out) < len(cases):
        r = subprocess.run([str(worker), '--cd-root', str(cd), '--language', LANGUAGE_LETTER[language],
                            '--results-batch', *extra], input=''.join(worker_request(c) for c in cases[len(out):]).encode(),
                           capture_output=True, timeout=600)
        out.extend(decode(r.stdout))
        if r.returncode:
            evidence = [line.strip() for line in r.stderr.decode('utf-8', errors='replace').splitlines()
                        if line.startswith(('ASSERT ', 'FAULT ', 'FAIL ', 'TRAP '))]
            if len(out) >= len(cases):
                raise ValueError('worker failed after the last response')
            out.append(dict(error='; '.join(evidence) or f'exit {r.returncode}'))
        elif len(out) != len(cases):
            raise ValueError('worker returned too few packets')
    return out


FIELDS = ('ending', 'advance', 'group', 'position', 'final_word', 'lost', 'total', 'debrief',
          'debrief_count', 'debrief_refs', 'debrief_aux', 'sav', 'strings', 'text', 'rng')


# Safe-API rejections and the original failure each one stands for
# (vshell_preparation.md, "Safe API policy"): empty list 5 asserts in
# 0x41d8e7; a consumed pilot without loadout faults in 0x4122e1 (0x4112a0);
# a zero team count faults in the budget division at 0x40f4eb; a debrief
# reference outside the text table makes 0x4139f6 pass a null string pointer
# to the C string adapters (host address, null read).
KNOWN_REJECTIONS = (('not found 5', 'ASSERT results path caller=0041d925'),
                    ('member not found', 'FAULT results path code=c0000005 EIP=00412314'),
                    ('invalid argument 0', 'FAULT results path code=c0000094 EIP=0040f4eb'),
                    ('offset out of range', 'FAULT results path code=c0000005 EIP='))
NULL_READ = 'address=00000000'


def compare(ours, theirs):
    if 'error' in ours or 'error' in theirs:
        both = 'error' in ours and 'error' in theirs
        known = both and any(a in ours['error'] and theirs['error'].startswith(b) and
                             (a != 'offset out of range' or theirs['error'].endswith(NULL_READ))
                             for a, b in KNOWN_REJECTIONS)
        return dict(equal=False, outside_contract=known,
                    own_error=ours.get('error'), original_error=theirs.get('error'))
    differences = []
    for name in FIELDS:
        a, b = ours[name], theirs[name]
        if name == 'debrief':
            a, b = bool(a), bool(b)
        if name in ('debrief_count', 'debrief_refs', 'debrief_aux', 'strings') and not theirs['debrief']:
            continue
        if a != b:
            entry = dict(field=name)
            if isinstance(a, bytes):
                entry['first_difference'] = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y),
                                                 min(len(a), len(b)))
                entry.update(size=len(a), reference_size=len(b))
            else:
                entry.update(ours=a, original=b)
            differences.append(entry)
    row = dict(equal=not differences, ending=theirs['ending'], advance=theirs['advance'],
               sav_sha256=prep.sha(theirs['sav']), text_size=len(theirs['text']),
               debrief=bool(theirs['debrief']))
    if differences:
        row['differences'] = differences
    return row


def effects(before, after):
    """Which parts of the contract a matching case exercised (coverage only)."""
    words = struct.unpack('<1000h', after.variables)
    count = lambda sav: sum(len(items) for _, items in sav.inventory)
    pilots = lambda sav: [sav.player] + [sav.selected(g) for g in range(3)]
    result = dict(hercs_lost=words[8], pilots_lost=words[9],
                  bays=[len(before.bays), len(after.bays)], items=[count(before), count(after)],
                  player_bay=[before.player.get(0x22), after.player.get(0x22)])
    result['replaced'] = sum(a != b for a, b in zip(before.roster_selection[:3], after.roster_selection[:3]))
    result['rank_up'] = sum(b.get(0x25) > a.get(0x25) for a, b in zip(pilots(before), pilots(after)))
    result['counter_up'] = sum(b.get(0x29) > a.get(0x29) for a, b in zip(pilots(before), pilots(after)))
    result['five_changed'] = before.five != after.five
    result['flags_changed'] = sum(a[0] != b[0] for a, b in zip(before.inventory, after.inventory))
    result['nine_changed'] = before.nine != after.nine
    result['repaired'] = sum(1 for index, (fixed, _) in after.bays.items() if index in before.bays and
                             fixed[4:30] != before.bays[index][0][4:30] and len(set(bytes(fixed[4 + i:6 + i]) for i in range(0, 26, 2))) == 1)
    result['construction'] = sum(1 for index, (fixed, _) in after.bays.items() if index in before.bays and
                                 fixed[70:74] != before.bays[index][0][70:74])
    return result


# ---- Case generation --------------------------------------------------------------
class Builder:
    def __init__(self, core, cd):
        self.core, self.cd = core, cd
        self.catalog = loadout.Catalog(core, cd)
        self.campaign = prep.campaign(core, cd)
        self.counts = {}
        for g, p, name in self.campaign:
            self.counts[g] = max(self.counts.get(g, 0), p + 1)
        self.legal = [p for p in sweep.loadout_plans(self.catalog) if p['ui']]
        self.rngs = {}

    def rng(self, seed):
        if seed not in self.rngs:
            self.rngs[seed] = prep.initial_rng(self.cd, seed)
        return self.rngs[seed]

    def plan(self, rnd, team):
        player = rnd.choice(self.legal)
        groups = rnd.sample(range(3), team)
        members = [(g, rnd.randrange(12), m['herc'], m['mounts'])
                   for g, m in zip(groups, (rnd.choice(self.legal) for _ in range(team)))]
        return dict(player=(player['herc'], player['mounts']), team=members, flags=None, zero_variables=True)

    def saves(self, language, plans):
        requests = [loadout.save_request(self.catalog, language, 1, 0, plan) for plan in plans]
        built = loadout.build_saves(self.core, self.cd, requests)
        bad = [b for b in built if isinstance(b, str)]
        if bad:
            raise ValueError('SAV build failed: ' + bad[0])
        return [Sav.parse(b) for b in built]

    def vehicle(self, vid):
        position, vid, capacity = self.catalog.vehicle(vid)
        return position, capacity


def last_positions(builder):
    return [(g, n - 1) for g, n in sorted(builder.counts.items())]


def finish(case, sav, rnd, outcome=None, tally=None, pairs=None, kinds=('healthy',), words=None,
           bank=None, seed=None):
    case['sav'] = sav.build()
    case['bank'] = bank if bank is not None else bank_bytes(rnd, 'zero')
    outcome = rnd.choice((0, 1)) if outcome is None else outcome
    tally = rnd.choice((0, 1000, 25000, 250000)) if tally is None else tally
    case['results'] = results_bytes(sav, rnd, outcome, tally, pairs or [], kinds, words)
    case['seed'] = rnd.randrange(128) if seed is None else seed
    case.update(outcome=outcome)
    return case


def cases(builder, profile, count, rnd, hooks=36):
    """Yield case dicts (language, mode, options, inputs, labels)."""
    languages = ('GER',) if profile == 'tiny' else ('ENG', 'FRE', 'GER')
    for language in languages:
        templates = builder.saves(language, [builder.plan(rnd, t % 4) for t in range(24)])

        def base(team=None):
            choices = [s for s in templates if team is None or len(s.consumers()) - 1 == team]
            return Sav.parse(rnd.choice(choices or templates).build())
        # Group boundaries, both outcomes, every CAREER position (results path and debrief per mission).
        for g, p, name in builder.campaign:
            if profile == 'tiny' and (g, p) not in ((1, 0), (1, 9)):
                continue
            for outcome in (0, 1):
                sav = base()
                sav.position(g, p)
                yield finish(dict(axis='position', language=language, mode=1, repair=0, restock=0,
                                  mission=name, before=[g, p]), sav, rnd, outcome=outcome)
        if profile == 'tiny':
            return
        # Signed outcome words and tallies at the last position of each group.
        for (g, p), outcome in itertools.product(last_positions(builder), (-32768, -1, 2, 32767)):
            sav = base()
            sav.position(g, p)
            yield finish(dict(axis='outcome-word', language=language, mode=1, repair=0, restock=0,
                              before=[g, p]), sav, rnd, outcome=outcome, tally=rnd.choice((-1, 2**31 - 1, -2**31)))
        # Hooks: (1,2)->(1,3) moves the player to bay 4; (1,5)->(1,6) removes vehicle 8.
        position8, capacity8 = builder.vehicle(8)
        for hook in range(hooks):
            variant = hook % 12
            sav = base()
            sav.position(1, 2 if variant == 0 else 5)
            label = 'bay4' if variant == 0 else None
            if variant:
                free = [b for b in range(8) if b not in sav.bays]
                if variant in (1, 2, 3):      # one vehicle 8, items with conditions 79/80/81
                    bay = free[variant % len(free)]
                    conditions = [100] * 23 + [rnd.choice((0, 79, 80, 81, 100, -32768, 32767)) for _ in range(10)]
                    items = {s: item_record(builder.catalog, rnd.choice((1, 3, 8, 13, 22)), secondary=rnd.choice((1, 5)))
                             for s in range(capacity8) if rnd.random() < 0.7}
                    sav.bays[bay] = [bay_fixed(8, position8, capacity8, conditions), items]
                    label = f'vehicle8-bay{bay}'
                elif variant in (4, 5):       # duplicate vehicle 8: only the first bay is removed
                    for bay in free[:2]:
                        sav.bays[bay] = [bay_fixed(8, position8, capacity8), {0: item_record(builder.catalog, 3)}]
                    label = 'vehicle8-duplicate'
                elif variant in (6, 7):       # no vehicle 8, bay 7 empty
                    for bay, (fixed, items) in list(sav.bays.items()):
                        if struct.unpack_from('<h', fixed)[0] == 8:
                            del sav.bays[bay]
                    sav.bays.pop(7, None)
                    label = 'no-vehicle8-bay7-empty'
                else:                         # no vehicle 8, bay 7 holds vehicle v
                    for bay, (fixed, items) in list(sav.bays.items()):
                        if struct.unpack_from('<h', fixed)[0] == 8:
                            del sav.bays[bay]
                    vid = rnd.choice([v for v, _ in builder.catalog.vehicles if v != 8])
                    position, capacity = builder.vehicle(vid)
                    sav.bays[7] = [bay_fixed(vid, position, capacity), {}]
                    if variant >= 10 and vid in sav.bays and vid != 7:
                        sav.player.set(0x22, vid)   # the bay-7 vehicle word equals the player's bay
                    label = f'no-vehicle8-bay7-vehicle{vid}'
            yield finish(dict(axis='hook', language=language, mode=1, repair=0, restock=0, hook=label,
                              before=[1, 2 if variant == 0 else 5]), sav, rnd, outcome=1)
        # Conditions, losses, repairs, purchases, unlocks, options and modes.
        kinds_all = ('healthy', 'random', 'extreme', 'slots', 'boundary', 'lost', 'pilot-lost')
        for i in range(count):
            sav = base(i % 4)
            g, p, name = rnd.choice([c for c in builder.campaign if c[0] > 0])
            sav.position(g, p)
            for fixed, items in sav.bays.values():
                if rnd.random() < 0.3:
                    struct.pack_into('<33h', fixed, 4, *condition_words(rnd, rnd.choice(('random', 'extreme', 'slots'))))
                if rnd.random() < 0.15:
                    struct.pack_into('<hh', fixed, 70, rnd.choice((0, 33, 50, 99)), rnd.randrange(0, 4))
            for pilot in [sav.player] + [sav.selected(g2) for g2 in range(3)]:
                for offset in (0x25, 0x29):
                    if rnd.random() < 0.3:
                        pilot.set(offset, rnd.choice((0, 1, 2, 3, -1)))
                for offset in (0x33, 0x35, 0x37, 0x39):
                    if rnd.random() < 0.3:
                        pilot.set(offset, rnd.choice((0, 9, 10, 19, 20, 29, 99, -1, 32767)))
            if rnd.random() < 0.5:
                ids = [rnd.choice((0, 0, 1, 5, 13, 16, 22, 25, 32)) for _ in range(5)]
                sav.five[:] = struct.pack('<h' + 'hh' * 5, rnd.choice((0, 2, 5)),
                                          *itertools.chain.from_iterable(enumerate(ids)))
            sav.total = rnd.choice((0, 5000, 60000, 250000, 3000000, 0xfffffff0, 0x80000000))
            if rnd.random() < 0.2:
                for flags in sav.inventory:
                    if rnd.random() < 0.3:
                        flags[0] = 1 - flags[0]
            if rnd.random() < 0.2:
                sav.nine[:] = struct.pack('<9h', *[rnd.choice((0, 0, 1)) for _ in range(9)])
            if rnd.random() < 0.25:
                # Team words and member flags: capacity/count bound different loops.
                sav.team = [rnd.choice((0, 1, 2, 3, 4)), rnd.choice((0, 1, 1, 2, 3, 4))]
                if rnd.random() < 0.5:
                    member = sav.selected(rnd.randrange(3))
                    member.set(0x24, 1 - min(member.get(0x24), 1))
                if rnd.random() < 0.3:
                    member = sav.selected(rnd.randrange(3))
                    if member.get(0x22) in sav.bays or rnd.random() < 0.2:
                        member.set(0x22, rnd.choice(sorted(sav.bays)) if rnd.random() < 0.8 else rnd.randrange(8))
                        member.set(0x27, rnd.randrange(1, 4))
                        member.set(0x24, 1)
            mode = 1 if rnd.random() < 0.9 else 0
            pairs = [(rnd.randrange(33), rnd.choice((0, 50, 100))) for _ in range(rnd.choice((0, 0, 1, 3)))]
            kinds = [rnd.choice(kinds_all) for _ in range(4)]
            words = [[rnd.choice((0, 1, 9, 10, 20, -1, 32767)) for _ in range(3)] for _ in range(4)]
            bank = bank_bytes(rnd, rnd.choice(('zero', 'items', 'unlock', 'random')))
            yield finish(dict(axis='random', language=language, mode=mode, repair=rnd.choice((0, 0, 1, 2, 3)),
                              restock=rnd.choice((0, 0, 1)), before=[g, p], mission=name, kinds=kinds),
                         sav, rnd, pairs=pairs, kinds=kinds, words=words, bank=bank, outcome=rnd.choice((0, 1, 1)))


def debrief_cases(builder, rnd, comparisons):
    """Every authored list-1 comparison at literal-1/literal/literal+1 and
    the signed extremes, for both outcomes; German, seeds varied."""
    template = builder.saves('GER', [builder.plan(rnd, 0)])[0]
    seen = set()
    for (g, p, name), label, bank, evidence in comparisons:
        if (g, p, label) in seen or g == 0:
            continue
        seen.add((g, p, label))
        sav = Sav.parse(template.build())
        sav.position(g, p)
        yield finish(dict(axis='debrief-comparison', language='GER', mode=1, repair=2, restock=1,
                          before=[g, p], mission=name, comparison=label), sav, rnd,
                     outcome=rnd.choice((0, 1)), bank=bank, kinds=('healthy',))


def synthetic_mission(rnd):
    """An invented MSN and German text file for the debrief reader: random
    condition types 0..3 with dependencies, auxiliary records with -1 entries
    and duplicates, debrief records whose conditions may all fail. Text IDs
    0..7 always exist (after one -1 entry), so raw unremapped references of a
    record left by failing conditions stay inside the text table."""
    ids, conditions = list(range(1, 9)), []
    for cid in ids:
        kind = rnd.choice((0, 0, 1, 2, 3, 5))
        dep = rnd.choice([-1, -1] + [c[0] for c in conditions])
        if kind == 0:
            operator = rnd.randrange(281, 287) if rnd.random() < 0.9 else rnd.choice((280, 287, -1))
            record = (cid, dep, 0, rnd.choice((0, 8, 9, 30)), operator, rnd.choice((-1, 0, 1, 2)), 0)
        elif kind == 5:      # unknown type: never retained
            record = (cid, dep, 5, 0, 0, 0, 0)
        elif kind == 1:
            record = (cid, dep, 1, rnd.choice((2, 3, 5)), 0, 0, 0)
        elif kind == 2:
            sources = [c[0] for c in conditions if c[2] in (1, 3)] or [99]
            low = rnd.randrange(0, 3)
            record = (cid, rnd.choice(sources), 2, low, low + rnd.randrange(0, 3), 0, 0)
        else:
            record = (cid, dep, 3, rnd.randrange(5), 0, rnd.choice((2, 3)), 0)
        conditions.append(record)
    auxiliary = [(rnd.choice((-1, 40, 41, 40, 42)), -1, 0, rnd.randrange(-5, 300)) for _ in range(rnd.randrange(1, 5))]
    present = sorted({a[0] for a in auxiliary} - {-1})
    debriefs = []
    for _ in range(rnd.randrange(1, 5)):
        refs = [rnd.randrange(8) if rnd.random() < 0.7 else -1 for _ in range(rnd.randrange(1, 6))]
        debriefs.append((rnd.choice(ids + [99, 99]), refs, rnd.choice([-1] + present)))
    out = bytearray(struct.pack('<h', 5))
    for family in range(1, 18):
        if family == 1:
            out += struct.pack('<h', len(conditions)) + b''.join(struct.pack('<7h', *c) for c in conditions)
        elif family == 3:
            out += struct.pack('<h', len(auxiliary)) + b''.join(struct.pack('<4h', *a) for a in auxiliary)
        elif family == 5:
            out += struct.pack('<h', len(debriefs))
            for condition, refs, aux in debriefs:
                out += struct.pack('<32h', condition, *(refs + [-1] * (30 - len(refs))), aux)
        else:
            out += struct.pack('<h', 0)
    texts = [(-1, -1, b'unused')] + [(i, rnd.choice((-1, -1, rnd.choice(ids))), b'text%d ' % i) for i in range(8)]
    language = bytearray(struct.pack('<h', len(texts)))
    for tid, dep, body in texts:
        language += struct.pack('<hhhh', tid, dep, 0, len(body) + 1) + body + bytes(1)
    return bytes(out), bytes(language)


def synthetic_cases(builder, rnd, missions=40, per=8):
    """Invented MSN/text overlays for the mission at (1,0): both implementations
    read them instead of the CD members. Positions (1,0) and (1,9) give return 2
    (text composed) and return 0/1."""
    g, p, name = next(c for c in builder.campaign if c[:2] == (1, 0))
    template = builder.saves('GER', [builder.plan(rnd, 0)])[0]
    for index in range(missions):
        msn, text = synthetic_mission(rnd)
        overlay = {f'MSN/{name}.MSN': msn, f'MSN/{name}.GER': text}
        for _ in range(per):
            sav = Sav.parse(template.build())
            sav.position(1, rnd.choice((0, 9)))
            extra = {v: rnd.choice((-1, 0, 1, 2)) for v in (8, 9, 30)}
            yield finish(dict(axis='synthetic-debrief', language='GER', mode=1, repair=2, restock=1,
                              overlay=overlay, overlay_id=index, synthetic=index), sav, rnd,
                         bank=bank_bytes(rnd, 'zero', extra), seed=rnd.randrange(32769))


def budget_cases(builder, core, cd, rnd, count=60):
    """Repair budget equal to the cost, one above and one below. The cost is
    measured first with our implementation (repair option 2 against 0); the
    original then decides each of the three cases independently."""
    tables = prep.results_tables(cd)
    templates = builder.saves('GER', [builder.plan(rnd, 0) for _ in range(8)])
    probes = []
    for i in range(count):
        sav = Sav.parse(rnd.choice(templates).build())
        g, p, name = rnd.choice([c for c in builder.campaign if c[0] > 0])
        sav.position(g, p)
        sav.team = [1, 1]
        sav.total = 1000000
        case = finish(dict(axis='repair-budget', language='GER', mode=1, repair=0, restock=1, before=[g, p]),
                      sav, rnd, kinds=(rnd.choice(('random', 'slots', 'extreme')),), outcome=1)
        case['rng'] = builder.rng(case['seed'])
        probes.append((sav, case))
    requests = [prep.results_request(tables=tables, save=case['sav'], variables=case['bank'],
                                     results=case['results'], rng=case['rng'], mode=1, repair=repair, restock=1)
                for sav, case in probes for repair in (2, 0)]
    outs = prep.results_batch(core, cd, requests)
    for (sav, case), plain, repaired in zip(probes, outs[0::2], outs[1::2]):
        if 'error' in plain or 'error' in repaired:
            continue
        cost = (plain['total'] - repaired['total']) & 0xffffffff
        gained = (plain['total'] - 1000000) & 0xffffffff
        if not cost or cost >= 1 << 31:
            continue
        for delta in (-1, 0, 1):
            sav.total = (cost + delta - gained) & 0xffffffff
            yield dict(case, sav=sav.build(), budget_delta=delta, cost=cost)


def run(core, worker, cd, all_cases, batch_size=96, progress=None):
    rows = []
    tables = prep.results_tables(cd)
    key = lambda c: (c['language'], c.get('overlay_id', -1))
    all_cases = sorted(all_cases, key=key)
    scratch = tempfile.TemporaryDirectory(prefix='es2-results-')
    for (language, overlay_id), group in itertools.groupby(all_cases, key=key):
        group = list(group)
        overlays = []
        for name, data in (group[0].get('overlay') or {}).items():
            path = Path(scratch.name) / f'{overlay_id}-{name.replace("/", "_")}'
            path.write_bytes(data)
            overlays.append((name, path))
        for start in range(0, len(group), batch_size):
            chunk = group[start:start + batch_size]
            for c in chunk:
                c['rng'] = prep.initial_rng(cd, c['seed']) if 'rng' not in c else c['rng']
            ours = prep.results_batch(core, cd, [prep.results_request(
                tables=tables, save=c['sav'], variables=c['bank'], results=c['results'], rng=c['rng'],
                language=language, mode=c['mode'], repair=c['repair'], restock=c['restock']) for c in chunk],
                overlays)
            theirs = original_batch(worker, cd, language, chunk, overlays)
            for c, a, b in zip(chunk, ours, theirs):
                label = {k: v for k, v in c.items() if k not in ('sav', 'bank', 'results', 'rng', 'overlay')}
                label.update(compare(a, b))
                if label['equal']:
                    label['effects'] = effects(Sav.parse(c['sav']), Sav.parse(b['sav']))
                rows.append(label)
            if progress:
                progress(rows)
    scratch.cleanup()
    return rows


def summary(rows):
    result = dict(cases=len(rows), equal=sum(r['equal'] for r in rows),
                  outside_contract=sum(r.get('outside_contract', False) for r in rows),
                  mismatches=sum(not r['equal'] and not r.get('outside_contract', False) for r in rows))
    result['axes'] = {axis: dict(cases=len(sub), equal=sum(r['equal'] for r in sub),
                                 outside_contract=sum(r.get('outside_contract', False) for r in sub))
                      for axis in sorted({r['axis'] for r in rows})
                      for sub in [[r for r in rows if r['axis'] == axis]]}
    equal = [r for r in rows if r['equal']]
    result['paths'] = sorted({(r['ending'], r['advance']) for r in equal})
    result['debriefs'] = sum(r['debrief'] for r in equal)
    result['texts'] = sum(r['text_size'] > 0 for r in equal)
    effect = [r['effects'] for r in equal]
    result['exercised'] = dict(
        hercs_lost=sum(e['hercs_lost'] > 0 for e in effect), pilots_lost=sum(e['pilots_lost'] > 0 for e in effect),
        bays_removed=sum(e['bays'][1] < e['bays'][0] for e in effect),
        items_added=sum(e['items'][1] > e['items'][0] for e in effect),
        player_bay_changed=sum(e['player_bay'][0] != e['player_bay'][1] for e in effect),
        pilots_replaced=sum(e['replaced'] > 0 for e in effect), rank_up=sum(e['rank_up'] > 0 for e in effect),
        counter_up=sum(e['counter_up'] > 0 for e in effect), five_bank_changed=sum(e['five_changed'] for e in effect),
        flags_changed=sum(e['flags_changed'] > 0 for e in effect), nine_changed=sum(e['nine_changed'] for e in effect),
        repaired=sum(e['repaired'] > 0 for e in effect), construction=sum(e['construction'] > 0 for e in effect))
    return result


def markdown(report):
    lines = ['# VSHELL results-path sweep', '', json.dumps(report['summary']), '',
             '| Axis | Cases | Equal | Outside contract |', '|---|---:|---:|---:|']
    for axis, s in report['summary']['axes'].items():
        lines.append(f"| {axis} | {s['cases']} | {s['equal']} | {s['outside_contract']} |")
    for r in report['cases']:
        if not r['equal']:
            lines.extend(['', json.dumps(r)])
    return '\n'.join(lines) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', required=True, type=Path)
    p.add_argument('--worker', required=True, type=Path)
    p.add_argument('--cd-root', required=True, type=Path)
    p.add_argument('--profile', choices=('tiny', 'full'), default='full')
    p.add_argument('--random-cases', type=int, default=600, help='random cases per language')
    p.add_argument('--hook-cases', type=int, default=36, help='hook cases per language')
    p.add_argument('--skip-debrief-comparisons', action='store_true')
    p.add_argument('--synthetic-missions', type=int, default=40, help='invented MSNs (8 cases each)')
    p.add_argument('--seed', type=int, default=20261008)
    p.add_argument('--report', required=True, type=Path)
    args = p.parse_args(argv)
    rnd = random.Random(args.seed)
    builder = Builder(args.core, args.cd_root)
    started = last = time.monotonic()

    def progress(rows):
        nonlocal last
        if time.monotonic() - last >= 10:
            print(json.dumps({k: v for k, v in summary(rows).items() if k != 'axes'}), flush=True)
            last = time.monotonic()
    chosen = list(cases(builder, args.profile, args.random_cases, rnd, args.hook_cases))
    if args.profile == 'full':
        chosen += list(synthetic_cases(builder, rnd, args.synthetic_missions))
        chosen += list(budget_cases(builder, args.core, args.cd_root, rnd))
    if args.profile == 'full' and not args.skip_debrief_comparisons:
        chosen += list(debrief_cases(builder, rnd, sweep.comparison_banks(args.core, args.cd_root)))
    rows = run(args.core, args.worker, args.cd_root, chosen, progress=progress)
    report = dict(profile=args.profile, seed=args.seed, elapsed_seconds=time.monotonic() - started,
                  reference_sha256=prep.REFERENCE_HASH, core_sha256=prep.sha(args.core.read_bytes()),
                  worker_sha256=prep.sha(args.worker.read_bytes()), summary=summary(rows), cases=rows)
    args.report.write_bytes((json.dumps(report, indent=1) + '\n').encode())
    args.report.with_suffix('.md').write_bytes(markdown(report).encode())
    print(json.dumps(report['summary']), flush=True)
    return int(report['summary']['mismatches'] != 0)


def configure(cli=ROOT / 'shell/tools/es2prep.py'):
    """Load the portable front end modules (es2prep.py and its sibling)."""
    global prep, loadout, sweep
    cli = Path(cli)
    prep = module(cli, 'results_prep')
    loadout = module(cli.with_name('es2loadout.py'), 'results_loadout')
    sweep = module(HERE / 'sweep_compare.py', 'results_sweep')


configure()

if __name__ == '__main__':
    raise SystemExit(main())
