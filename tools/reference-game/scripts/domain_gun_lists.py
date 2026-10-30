"""Do the shipped hardpoint lists keep group_fire inside its ten-word array?

group_fire 4107e4 (docs/reverse-engineering/weapons.md) marks visited
weapons in a ten-word local array, indexed by the group's selected index
I = sx8(byte G+1Dh) and walked over the weapon count sx(word G+8): count
<= 10 and I <= 9 keep it inside. 40fff8 stores the count of the vehicle's
gl\\*.GL list at G+8; I is written only as an index below that count
(weapon_factory's first non-equipment index, the search 0x410708, the
weapon keys 1..0 of hud.md), so a count of at most 10 also bounds I by 9.
The scan reads every GL member and loose file, and checks the record
slot byte +0x17 that weapon_fit uses for the capacity b+12 and the region
b+19 against the 29 regions and 22 capacities of the DMG tables.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

COUNT_LIMIT = 10
INDEX_LIMIT = 9
SLOT_LIMIT = 9


def scan(root):
    result = {"files": {}, "violations": []}
    for label, data in cd.entries(root, cd.suffix(".GL")):
        records = cd.gun_list(data)
        slots = [slot for _, slot in records]
        result["files"][label] = {"count": len(records), "slots": slots,
                                  "categories": [c for c, _ in records]}
        if len(records) > COUNT_LIMIT:
            result["violations"].append({"where": label, "case": "count",
                                         "count": len(records)})
        for j, slot in enumerate(slots):
            if not 0 <= slot <= SLOT_LIMIT:
                result["violations"].append({"where": label, "case": "slot",
                                             "record": j, "slot": slot})
    counts = [f["count"] for f in result["files"].values()]
    result["maximum_count"] = max(counts, default=0)
    result["maximum_index"] = result["maximum_count"] - 1
    result["slot_range"] = [min((min(f["slots"]) for f in result["files"].values()
                                 if f["slots"]), default=None),
                            max((max(f["slots"]) for f in result["files"].values()
                                 if f["slots"]), default=None)]
    result["file_count"] = len(counts)
    result["holds"] = (bool(counts) and not result["violations"]
                       and result["maximum_index"] <= INDEX_LIMIT)
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
    print(json.dumps({k: result[k] for k in ("holds", "file_count", "maximum_count",
                                             "maximum_index", "slot_range")}))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
