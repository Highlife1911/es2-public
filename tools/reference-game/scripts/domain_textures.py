"""Is any shipped bitmap at least 0x8000 wide?

The textured polygon of driver slot 33 (docs/reverse-engineering/
rasterizer.md, "Cursor point and textured slot 33") tests the sign of its
row step with a word compare, so a texture at least 0x8000 wide would step
the wrong way. Textures are DBM records (docs/formats/dbm.md: tag
0e 00 28 00, body size S at +4, height +8, width +0x0a, pixel bytes P at
+0x0f, colour count C at +0x13), standalone or inside DBA/HBA arrays. The
scan finds every record by its tag in every loose file and VOL member and
keeps those whose S equals 13 + P + 4C and fits the data, so it does not
depend on the array framing (it also finds the records in the one DBA
trailer that dbm.md describes).
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

TAG = bytes((0x0e, 0, 0x28, 0))
WIDTH_LIMIT = 0x8000


def bitmaps(data):
    """(offset, height, width) of every consistent DBM record in data."""
    at = data.find(TAG)
    while at != -1:
        if at + 0x15 <= len(data):
            body = cd.u32(data, at + 4)
            height = cd.u16(data, at + 8)
            width = cd.u16(data, at + 0x0a)
            pixels = cd.u32(data, at + 0x0f)
            colours = cd.u16(data, at + 0x13)
            if body == 13 + pixels + 4 * colours and at + 8 + body <= len(data):
                yield at, height, width
        at = data.find(TAG, at + 1)


def scan(root):
    result = {"files": 0, "bitmaps": 0, "maximum_width": 0, "maximum_height": 0,
              "widest": None, "violations": []}
    for label, data in cd.entries(root, lambda n: True):
        found = list(bitmaps(data))
        if not found:
            continue
        result["files"] += 1
        result["bitmaps"] += len(found)
        for offset, height, width in found:
            if width > result["maximum_width"]:
                result["maximum_width"] = width
                result["widest"] = f"{label} +{offset:#x}"
            result["maximum_height"] = max(result["maximum_height"], height)
            if width >= WIDTH_LIMIT:
                result["violations"].append({"where": f"{label} +{offset:#x}", "width": width})
    result["holds"] = result["bitmaps"] > 0 and not result["violations"]
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
