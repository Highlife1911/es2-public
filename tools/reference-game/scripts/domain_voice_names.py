"""Do the shipped radio messages keep 0x44aff0's voice names within 11 characters?

radio_voice 0x44aff0 (sim/src/hud_radio.c, hud.md) builds in 12-byte
locals C = "_" + the message word [M] (at least two digits) + the message
byte M+0x16 (at least three digits), A = "P" + itoa(x/4 + 1) + C and
B = "P" + chr(x + 0x41) + C, with x the word S+0x135 of the wingman's
record. Its literals at 0x49d588..0x49d58c are "P", "P" and "_".

The word and the byte are the first two aux bytes of the radio strings:
the voice members SIMVOIC?\\P<n>_<ww><bbb>.WAV match them (P1_02001 is
PILOT1.STR's string with aux 02 01). The scan takes the aux bytes of every
COMMAND*, PILOT* and SYSTEM* string table on the CD and computes the
lengths with a one-digit callsign (x below 36); B never exceeds
2 + 1 + 5 + 3 = 11 for any 16-bit word. It also lists the longest voice
member stem of the archives.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

NAME_LIMIT = 11


def name_lengths(word, byte, sign=1):
    c = 1 + max(2, len(str(word))) + max(3, len(str(byte)))
    return 1 + len(str(sign)) + c, 2 + c, c


def scan(root):
    result = {"tables": 0, "strings": 0, "maximum_word": 0, "maximum_byte": 0,
              "longest": {"A": 0, "B": 0, "C": 0}, "violations": [],
              "voice_member_stem_maximum": 0}
    radio = lambda n: n.endswith(".STR") and n.startswith(("COMMAND", "PILOT", "SYSTEM")) \
        and n != "PILOTS.STR"
    for label, data in cd.entries(root, radio):
        result["tables"] += 1
        for g, group in enumerate(cd.str_groups(data)):
            for s, (_, aux) in enumerate(group):
                if len(aux) < 2:
                    continue
                result["strings"] += 1
                word, byte = aux[0], aux[1]
                result["maximum_word"] = max(result["maximum_word"], word)
                result["maximum_byte"] = max(result["maximum_byte"], byte)
                for key, length in zip("ABC", name_lengths(word, byte)):
                    result["longest"][key] = max(result["longest"][key], length)
                    if length > NAME_LIMIT:
                        result["violations"].append({"where": f"{label} group {g} string {s}",
                                                     "name": key, "length": length})
    for label, _, members in cd.archives(root):
        for m in members:
            if m.directory.upper().startswith("SIMVOIC") and m.name.upper().endswith(".WAV"):
                stem = len(m.name.rsplit(".", 1)[0])
                result["voice_member_stem_maximum"] = max(result["voice_member_stem_maximum"], stem)
    result["holds"] = result["strings"] > 0 and not result["violations"]
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
