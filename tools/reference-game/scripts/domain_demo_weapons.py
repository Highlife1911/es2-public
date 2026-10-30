"""The weapon lists of the demo tapes' vehicles.

hud.md infers an ENERGY item in DEMO2 from a call of slot +0x64 of
0x498a84. A tape's second record is the player.mec it was recorded with
(docs/reverse-engineering/file_io.md, "Tape header"; layout
vshell_preparation.md 0x40f68c): per record the identity and rank words,
the vehicle, the capacity K and K item words (0 is empty). The scan lists
every tape's records with the item names of the shell's GAM\\WEAPONS.DAT
and the shell's vehicle names, and checks that DEMO2's player
record carries ENRG (32).
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd
from domain_vehicle_part import shell_weapons

ENERGY = 32
# The shell's vehicle IDs (dts.md's vehicle table; each INI_* record's vehicle word).
VEHICLES = {0: "OUTLAW", 1: "RAPTOR2", 2: "TOMAHAWK", 3: "SAMSON", 4: "COLOSSUS",
            5: "APOCA", 6: "OGRE", 7: "MAVERICK", 8: "RAZOR"}


def scan(root):
    shell = cd.members_by_path(root, "VOL/SHELL0.VOL")
    names = {ident: name for ident, name, _ in shell_weapons(shell["GAM\\WEAPONS.DAT"])[0]}
    result = {"tapes": {}}
    for label, data in cd.entries(root, cd.suffix(".TAP")):
        records, _ = cd.mec_records(cd.tap_records(data)[1])
        result["tapes"][label] = [
            {"identity": identity, "rank": rank, "vehicle": vehicle,
             "vehicle_name": VEHICLES.get(vehicle),
             "items": items, "item_names": [names.get(i, str(i)) for i in items if i]}
            for identity, rank, vehicle, items in records]
    demo2 = [v for k, v in result["tapes"].items() if k.upper().endswith("DEMO2.TAP")]
    result["demo2_player_has_energy"] = bool(demo2 and demo2[0] and ENERGY in demo2[0][0]["items"])
    result["holds"] = result["demo2_player_has_energy"]
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", help="CD root (default ES2_DATA_ROOT)")
    parser.add_argument("--out", help="write the JSON evidence here")
    args = parser.parse_args(argv)
    result = scan(cd.data_root(args.root))
    text = json.dumps(result, indent=1)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
