# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Stand-in for DBSIM in the campaign chain test. Started by the
synthetic dbsim.EXE (launcher/tests/stand_in_child.c, dbsim.delegate) in a
temporary game directory; never a game process.

It writes DATA/results.dat and returns 3, as DBSIM does after a mission:

* --recorded RESULTS VAR: for the first mission, copy a recorded DBSIM output
  (an oracle suite's results.dat and mission.var, read only);
* otherwise synthetic results for the chain's current SAV: outcome, tally, no
  item pairs, then for every pilot the results path reads (the player, then
  per team order the first selected roster pilot with that order, if active;
  vshell_preparation.md, "Sequence of 0x40f09f", steps 3 to 5) 33 condition
  words if its bay holds a Herc and three counter words. mission.var stays as
  the preparation wrote it.

--exit-codes lists the exit code per mission (default 3 for all). The four
launch files it was handed are copied to <chain>/fake-dbsim/<index>/.
--kill lists missions on which the player is killed (his Herc's
condition word 22 is 0, so the results path ends with final word 3) and
DBSIM writes its own mission.var (the handed one with word 999 = 0x5a5a).
Standard library only.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def describe(core, cd_root, sav):
    run = subprocess.run([str(core), str(cd_root), '--describe-save'], input=sav.hex() + '\n',
                         capture_output=True, text=True, check=True)
    lines = run.stdout.splitlines()
    if not lines or lines[0] != 'OK':
        raise ValueError('SAV rejected')
    pilots, player, bays, selection, team = {}, None, set(), None, None
    for line in lines[1:]:
        kind, *rest = line.split()
        if kind in ('PILOT', 'PLAYER'):
            index, _, _, bay, active, _, order = map(int, rest[:7])
            entry = dict(bay=bay, active=active, order=order)
            if kind == 'PLAYER':
                player = entry
            else:
                pilots[index] = entry
        elif kind == 'BAY':
            bays.add(int(rest[0]))
        elif kind == 'SELECTION':
            selection = list(map(int, rest))
        elif kind == 'TEAM':
            team = list(map(int, rest))
    return player, pilots, bays, selection, team


def consumers(player, pilots, selection, team):
    result = [player]
    for order in range(1, team[1]):
        for group in range(3):
            pilot = pilots[group * 12 + selection[group]]
            if pilot['order'] == order:
                if pilot['active']:
                    result.append(pilot)
                break
    return result


def synthetic_results(core, cd_root, sav, outcome, tally, index, kill=False):
    player, pilots, bays, selection, team = describe(core, cd_root, sav)
    out = bytearray(struct.pack('<hih', outcome, tally, 0))
    for number, pilot in enumerate(consumers(player, pilots, selection, team)):
        if pilot['bay'] in bays:
            # Healthy Herc with one slot below the return threshold 80 on odd missions.
            words = [100] * 33
            if index % 2:
                words[23] = 79
            if kill and number == 0:
                words[22] = 0
            out += struct.pack('<33h', *words)
        out += struct.pack('<3h', 1 + number, index % 3, 1)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chain', required=True, type=Path)
    parser.add_argument('--recorded', nargs=2, type=Path, metavar=('RESULTS', 'VAR'))
    parser.add_argument('--outcome', type=int, default=1)
    parser.add_argument('--tally', type=int, default=25000)
    parser.add_argument('--exit-codes', default='')
    parser.add_argument('--kill', default='', help='missions (indices) on which the player is killed')
    args = parser.parse_args()
    chain = json.loads((args.chain / 'chain.json').read_text(encoding='utf-8'))
    pending = chain['pending']
    index = pending['index'] if pending else -1
    data = Path.cwd() / 'DATA'
    with (args.chain / 'fake-dbsim.log').open('a', encoding='utf-8') as log:
        log.write(f'{" ".join(sys.argv[1:])} index={index}\n')
    if index >= 0:
        # What this DBSIM was handed (the four launch files in DATA).
        seen = args.chain / 'fake-dbsim' / str(index)
        seen.mkdir(parents=True, exist_ok=True)
        for name in ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec'):
            shutil.copyfile(data / name, seen / name)
    if index == 0 and args.recorded:
        shutil.copyfile(args.recorded[0], data / 'results.dat')
        shutil.copyfile(args.recorded[1], data / 'mission.var')
    elif index >= 0:
        sav = (args.chain / chain['save']).read_bytes()
        kill = index in [int(x) for x in args.kill.split(',') if x]
        (data / 'results.dat').write_bytes(
            synthetic_results(chain['core'], chain['cd_root'], sav, args.outcome, args.tally, index, kill))
        if kill:
            bank = bytearray((data / 'mission.var').read_bytes())
            struct.pack_into('<H', bank, 2 * 999, 0x5a5a)
            (data / 'mission.var').write_bytes(bytes(bank))
    codes = [int(x) for x in args.exit_codes.split(',') if x]
    return codes[index] if 0 <= index < len(codes) else 3


if __name__ == '__main__':
    raise SystemExit(main())
