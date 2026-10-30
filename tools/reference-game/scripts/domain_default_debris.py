"""Is the default debris list count [0x4a9654] above 3 for the shipped data?

0x4089bc (docs/formats/collision.md) picks the default list DEF_DEB
(0x4a9650, count 0x4a9654) for a debris index below its count and the
actor's DAT list [0x4a964c] otherwise; the hit tests and mech_fly ask it
for indices up to 3 (hit_tests.md). The count is the group count G of
dat\\DEF_DEB.DAT (docs/formats/dat.md: i16 G, then groups of W id, i16 K
and K 14-byte children). The scan reads every DEF_DEB.DAT on the CD and
checks G > 3 and that the groups consume the file.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

LOWEST_INDEX_ASKED = 3


def debris_groups(data):
    """The child counts K of a *_DEB.DAT file."""
    count = cd.i16(data, 0)
    if count < 0:
        raise cd.FormatError(f"group count {count}")
    offset = 2
    children = []
    for _ in range(count):
        k = cd.i16(data, offset + 2)
        if k < 0:
            raise cd.FormatError(f"child count {k} at {offset:#x}")
        cd.take(data, offset + 4, 14 * k)
        children.append(k)
        offset += 4 + 14 * k
    if offset != len(data):
        raise cd.FormatError(f"groups end at {offset:#x} of {len(data):#x}")
    return children


def scan(root):
    result = {"files": {}, "violations": []}
    for label, data in cd.entries(root, lambda n: n == "DEF_DEB.DAT"):
        children = debris_groups(data)
        result["files"][label] = {"groups": len(children), "children": children}
        if len(children) <= LOWEST_INDEX_ASKED:
            result["violations"].append({"where": label, "groups": len(children)})
    result["minimum_groups"] = min((f["groups"] for f in result["files"].values()), default=None)
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
    print(json.dumps(result))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
