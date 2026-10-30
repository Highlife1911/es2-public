"""Does any shipped VOL member use the decoder kinds 7 or 9?

stream_open 0x4730e8 (docs/reverse-engineering/file_io.md) reads a member's
block kind byte: 2 reads the stored bytes, 7 and 9 attach the decoders
0x47b50c / 0x47b840, other kinds open nothing. The scan reads the kind
byte of every member of every VOLN archive on the CD.
"""

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

DECODER_KINDS = (7, 9)


def scan(root):
    result = {"archives": {}, "decoder_members": []}
    total = Counter()
    for label, _, members in cd.archives(root):
        kinds = Counter(m.kind for m in members)
        total.update(kinds)
        result["archives"][label] = {"members": len(members),
                                     "kinds": {str(k): v for k, v in sorted(kinds.items())}}
        for m in members:
            if m.kind in DECODER_KINDS:
                result["decoder_members"].append({"where": f"{label}:{m.path}", "kind": m.kind})
    result["kinds"] = {str(k): v for k, v in sorted(total.items())}
    result["archive_count"] = len(result["archives"])
    result["holds"] = bool(result["archives"]) and not result["decoder_members"]
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
    print(json.dumps({k: result[k] for k in ("holds", "archive_count", "kinds")}))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
