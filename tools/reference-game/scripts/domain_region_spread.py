"""Do the shipped part tables meet region_spread's termination precondition?

region_spread 0x40cf44 (docs/reverse-engineering/hit_tests.md) sums, in a
16-bit word, the weights of a part's links whose capacity word is still
non-negative and draws below that total; it terminates when every weight
is non-negative and the live total stays below 0x8000. The live total is
at most the sum of all of a part's weights, so the scan checks every link
weight >= 0 and each part's weight sum < 0x8000 over the DMG tables and
the WEAPONS.DAT records whose prefix weapon_fit installs as a part.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

TOTAL_LIMIT = 0x8000


def check_parts(where, parts, result):
    """parts: [(part label, [(weight, capacity index)])]."""
    for label, links in parts:
        weights = [w for w, _ in links]
        total = sum(weights)
        result["parts"] += 1
        result["links"] += len(links)
        result["maximum_total"] = max(result["maximum_total"], total)
        result["maximum_weight"] = max([result["maximum_weight"]] + weights)
        result["minimum_weight"] = min([result["minimum_weight"]] + weights)
        if any(w < 0 for w in weights):
            result["violations"].append({"where": f"{where} {label}", "case": "negative weight",
                                         "weights": weights})
        if total >= TOTAL_LIMIT:
            result["violations"].append({"where": f"{where} {label}", "case": "total",
                                         "total": total})


def scan(root):
    result = {"files": 0, "parts": 0, "links": 0, "maximum_total": 0,
              "maximum_weight": 0, "minimum_weight": 0, "violations": []}
    for label, data in cd.entries(root, cd.suffix(".DMG")):
        _, regions = cd.dmg_file(data)
        check_parts(label, [(f"region {i}", r["links"]) for i, r in enumerate(regions)], result)
        result["files"] += 1
    for label, data in cd.entries(root, lambda n: n == "WEAPONS.DAT"):
        try:
            records = cd.sim_weapons(data)
        except cd.FormatError:
            continue  # the shell's GAM\WEAPONS.DAT has another layout
        check_parts(label, [(f"record {i}", r["links"]) for i, r in enumerate(records)], result)
        result["files"] += 1
    result["holds"] = result["files"] > 0 and not result["violations"]
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
