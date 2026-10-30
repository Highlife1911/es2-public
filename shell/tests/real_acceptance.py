"""Opt-in, read-only SAV reconstruction and four-file acceptance. No asset export.

This is a bounded evidence reader, not the backlog's general SAV reader/writer.
Unknown spans remain bytes. Every decoded offset follows mission.md's framing.
"""
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys


def load_cli(path):
    spec = importlib.util.spec_from_file_location('es2prep', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, n):
        if n < 0 or n > len(self.data) - self.at:
            raise ValueError(f'truncated SAV at {self.at}')
        data = self.data[self.at:self.at + n]
        self.at += n
        return data

    def count(self):
        n, = struct.unpack('<h', self.take(2))
        if n < 0:
            raise ValueError(f'negative SAV count at {self.at - 2}')
        return n

    def pilot(self):
        prefix = self.take(4)
        name = self.take(self.count())
        if not name or name[-1] != 0:
            raise ValueError('unterminated pilot name')
        suffix = self.take(25)
        return dict(identity=struct.unpack_from('<H', prefix, 2)[0],
                    loadout=struct.unpack_from('<h', suffix)[0], active=suffix[2],
                    value=struct.unpack_from('<H', suffix, 3)[0],
                    order=struct.unpack_from('<h', suffix, 5)[0])


def save_inputs(data):
    r = Reader(data)
    flags = []
    for _ in range(33):
        flags += r.take(1)
        r.take(10 * r.count())
    r.take(22)
    selection = r.take(152)
    group, position = struct.unpack_from('<2h', selection)
    pilots = [r.pilot() for _ in range(36)]
    selected = struct.unpack('<3h', r.take(6))
    r.take(6)
    team_capacity, team_count = struct.unpack('<2h', r.take(4))
    player = r.pilot()
    loadouts = {}
    for _ in range(r.count()):
        slot = r.count()
        fixed = r.take(78)
        vehicle, = struct.unpack_from('<h', fixed)
        capacity, occupied = struct.unpack_from('<2h', fixed, 74)
        if slot >= 8 or slot in loadouts or not 0 <= occupied <= capacity <= 10:
            raise ValueError('invalid saved loadout')
        items, secondary = [0] * capacity, [5] * capacity
        seen = set()
        for _ in range(occupied):
            entry = r.take(12)
            index, item = struct.unpack_from('<2h', entry)
            if not 0 <= index < capacity or index in seen:
                raise ValueError('invalid saved equipment slot')
            seen.add(index)
            items[index] = item
            secondary[index], = struct.unpack_from('<H', entry, 10)
        loadouts[slot] = dict(vehicle=vehicle, items=items, secondary=secondary,
                             condition=list(struct.unpack_from('<33H', fixed, 4)))
    r.take(22)
    bank_offset = r.at
    bank = r.take(2000)
    r.take(22)
    if not 1 <= team_count <= team_capacity <= 4:
        raise ValueError('invalid saved team counts')
    if any(not 0 <= i < 12 for i in selected):
        raise ValueError('invalid saved roster selection')
    chosen = [pilots[g * 12 + i] for g, i in enumerate(selected)]
    team = [player]
    # Writer scans the three selected pilots by order, then tests active byte.
    for order in range(1, team_capacity):
        matches = [pilot for pilot in chosen if pilot['order'] == order]
        if matches and matches[0]['active']:
            team.append(matches[0])
    if len(team) != team_count:
        raise ValueError('saved count does not match selected active pilots')
    actors = [dict(loadouts[pilot['loadout']], pilot=[pilot['identity'], pilot['value']])
              for pilot in team]
    return dict(group=group, position=position, bank=bank, bank_offset=bank_offset,
                reader_end=r.at, trailing=len(data)-r.at,
                loadout=dict(actors=actors, flags=flags), player_value=player['value'])


def verify(cli, core, cd, pins):
    cases = [('C1_01', 'GAME_R.SAV', 1, 71,
              ('DATA/script.dat', 'DATA/MISSION.STR', 'DATA/mission.var', 'DATA/player.mec')),
             ('DEMO', 'GAME_T.SAV', 0, 103,
              ('SAV/script11.dat', 'SAV/missn11.str', None, 'SAV/player11.mec'))]
    requests, inputs = [], []
    for name, save, mode, seed, paths in cases:
        raw_save, _ = pins.require(cd, 'SAV/' + save)
        state = save_inputs(raw_save)
        inputs.append((state, raw_save))
        requests.append(cli.request(mission=name, group=state['group'], position=state['position'],
            mode=mode, player_value=state['player_value'], mode0_values=(1, 1, 0, 0),
            bank=state['bank'], rng=cli.initial_rng(cd, seed),
            save=raw_save, tables=cli.default_tables(cd)))
    outputs = cli.prepare_batch(core, cd, requests)
    # Keep both pre-existing frozen-input paths under the same real acceptance.
    frozen_requests=[]
    for case,(state,raw_save) in zip(cases,inputs):
        name,save,mode,seed,paths=case
        for build in (False,True):
            frozen_requests.append(cli.request(mission=name,group=state['group'],position=state['position'],
                mode=mode,player_value=state['player_value'],mode0_values=(1,1,0,0),
                bank=state['bank'],rng=cli.initial_rng(cd,seed),
                player=cli.encode_loadout(state['loadout']),build=build))
    frozen=cli.prepare_batch(core,cd,frozen_requests)
    for i,result in enumerate(frozen):
        if 'error' in result or any(result[name]!=outputs[i//2][name] for name in (*cli.FILES,'rng','selectors')):
            raise AssertionError('real frozen template/loadout regression')
    report = []
    for case, (state, raw_save), output in zip(cases, inputs, outputs):
        name, save, mode, seed, paths = case
        if 'error' in output:
            raise AssertionError(f'{name}: {output["error"]}')
        comparisons = []
        for filename, relative in zip(cli.FILES, paths):
            if relative:
                # The verdict is decided against the pinned hash of the original
                # file; a clone copy, when one holds it right now, adds the
                # byte-for-byte comparison. The clone's DATA is rewritten by
                # oracle regressions, so it is never the only reference.
                reference, clone = pins.fetch(cd, relative)
                equal = pins.matches(relative, output[filename]) and (
                    reference is None or output[filename] == reference)
                reference_sha256 = pins.PINS[relative][1]
                source = clone.name if clone else 'pinned-sha256'
            else:
                reference, source = state['bank'], 'save-bank'
                equal, reference_sha256 = output[filename] == reference, cli.sha(reference)
            comparisons.append(dict(file=filename, equal=equal, size=len(output[filename]),
                                    sha256=cli.sha(output[filename]), reference_sha256=reference_sha256,
                                    reference_source=source))
        report.append(dict(mission=name, save=save, save_sha256=cli.sha(raw_save),
            variable_offset=state['bank_offset'], save_reader_end=state['reader_end'],
            opaque_trailing_bytes=state['trailing'], seed_equivalent_draws=seed,
            input_rng_sha256=cli.sha(cli.initial_rng(cd, seed)),
            output_rng_sha256=cli.sha(output['rng']), files=comparisons))
    print(json.dumps(report, indent=2))
    if not all(entry['equal'] for case in report for entry in case['files']):
        raise AssertionError('four-file acceptance mismatch')
    return report


def main():
    root = os.environ.get('ES2_DATA_ROOT')
    if not root:
        print('SKIP shell real acceptance: set ES2_DATA_ROOT')
        return
    cli = load_cli(Path(sys.argv[1]).resolve())
    cd = Path(root)
    # reference_pins.py sits beside the sweep driver in the same runfiles tree.
    pins = load_cli(Path(sys.argv[4]).resolve().with_name('reference_pins.py'))
    verify(cli, Path(sys.argv[2]).resolve(), cd, pins)
    if len(sys.argv)>4:
        sweep=load_cli(Path(sys.argv[4]).resolve())
        cases=[dict(group=g,position=p,mission=n,mode=int(g!=0),language='GER',
                    source='cd-defaults',axis='baseline')
               for g,p,n in cli.campaign(Path(sys.argv[2]).resolve(),cd)]
        rows=sweep.run(cli,Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve(),cd,cases)
        print(json.dumps(dict(cd_default_parity=sweep.summary(rows),missions=rows),indent=2))
        if len(rows)!=61 or not all(r['equal'] for r in rows):
            raise AssertionError('61-mission CD-default original parity')


if __name__ == '__main__':
    main()
