"""Do the shipped HUD string tables fit 0x437598's tables and 0x43a5a0's range text?

0x437598 (sim/src/hud_cockpit.c `hud_strings`) loads stX\\strings0.str
(name 0x49b747, extension 0x49b750, the language letter in the template
0x49f255) into the holder 0x4d1298 and hands the groups in file order to
0x4539a0, one per pointer table. 0x4539a0 stores as many pointers as the
group has strings; it does not read the table size, so a group longer than
its table writes over the next one. The MFD status draw 0x43a5a0 copies the
text [0x4d14f4] (group 20, string 2) into a 0x100-byte frame buffer; the own
code refuses a text of 0x100 bytes or more (hud.md, "Refusals").

The scan checks every STRINGS0.STR on the CD: group counts within the table
sizes, and the length of string 2 of group 20.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

# (table, size) in 0x437598's order.
TABLES = [
    (0x4d12a8, 0x12), (0x4d1338, 3), (0x4d1344, 1), (0x4d1348, 1), (0x4d134c, 4), (0x4d135c, 0xd),
    (0x4d1390, 4), (0x4d13a0, 3), (0x4d13ac, 1), (0x4d13b0, 3), (0x4d13bc, 5), (0x4d13d0, 2),
    (0x4d13d8, 3), (0x4d13e4, 0x13), (0x4d1430, 0x13), (0x4d147c, 0xc), (0x4d14ac, 0xc), (0x4d14dc, 1),
    (0x4d14e0, 1), (0x4d14e4, 2), (0x4d14ec, 3), (0x4d14f8, 1), (0x4d14fc, 0x15), (0x4d1550, 0x28),
    (0x4d15f0, 5), (0x4d1604, 2), (0x4d160c, 1), (0x4d1610, 1), (0x4d1614, 5), (0x4d1628, 2),
    (0x4d1630, 1), (0x4d1634, 1), (0x4d1638, 4), (0x4d1648, 2), (0x4d1660, 1), (0x4d1664, 0xf),
    (0x4d16a0, 4), (0x4d1650, 2), (0x4d1658, 1), (0x4d165c, 1), (0x4d16b0, 8),
]
RANGE_TEXT = 0x4d14f4
RANGE_LIMIT = 0x100


def locate(address):
    for g, (table, size) in enumerate(TABLES):
        if table <= address < table + 4 * size:
            return g, (address - table) // 4
    raise ValueError(f"{address:#x} is in no table")


def scan(root):
    group, index = locate(RANGE_TEXT)
    result = {"files": {}, "violations": [], "range_text": {"group": group, "string": index}}
    for label, data in cd.entries(root, lambda n: n == "STRINGS0.STR"):
        groups = cd.str_groups(data)
        counts = [len(g) for g in groups]
        entry = {"groups": len(groups), "longest_string": max((len(t) for g in groups for t, _ in g), default=0)}
        if len(groups) < len(TABLES):
            result["violations"].append({"where": label, "case": "fewer groups than tables", "groups": len(groups)})
        for g, (table, size) in enumerate(TABLES[:len(groups)]):
            if counts[g] > size:
                result["violations"].append({"where": f"{label} group {g}", "case": "group longer than its table",
                                             "count": counts[g], "table": f"{table:#x}", "size": size})
        if group < len(groups) and index < counts[group]:
            text = groups[group][index][0]
            entry["range_text"] = text.decode("latin-1")
            entry["range_text_length"] = len(text)
            if len(text) >= RANGE_LIMIT:
                result["violations"].append({"where": label, "case": "range text", "length": len(text)})
        else:
            result["violations"].append({"where": label, "case": "no range text"})
        result["files"][label] = entry
    result["holds"] = bool(result["files"]) and not result["violations"]
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
    print(text)
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
