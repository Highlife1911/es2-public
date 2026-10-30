"""Do any CD texts reach the fatal cases of VSHELL's line breaker 0x40d686?

The breaker and its quirks are in docs/reverse-engineering/vshell_text.md
("Text pages") and vshell_ui.md section 16.4. With W = r - l of the panel, the
assertion fires when, after a break at the last break q, the text from
q + 1 up to the current space is not narrower than W. The last break only
moves at a space whose line fits, so that text is one word (fatal when its
width is >= W) or a word, a space and the next word (fatal when wider than
W). Every text is safe when each word is narrower than W and each such pair
is at most W wide; a '\n' ends the pair, other bytes below 0x20 count as a
space glyph but not as a break candidate.

Texts (vshell_ui.md sections 4.8 and 16): the page-0 campaign text (one string of
<lang>\\campaign.str, panel (10, 21)-(613, 168), W = 603) and the briefing,
objectives, intelligence and debrief texts (W = 565), each the
concatenation without separator of the strings referenced by an MSN list-4
or list-5 record (mission.md), looked up in the mission's language member
(.ENG/.FRE/.GER). Which variant of an ID a mission keeps, and whether it
keeps one, depends on the preparation's conditions; the scan takes every
variant and the empty choice at every reference, which covers each
composition the preparation can produce. Glyph widths are SHELL1's
DFN\\FONT2.DFN (VSHELL's VOL order). The longest line (labels assert above
100 bytes) is reported from the compositions with every reference present,
each with its first variant and each further variant of one reference.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cd_corpus as cd

CAMPAIGN_WIDTH = 613 - 10
PANEL_WIDTH = 575 - 10
LANGUAGES = {"ENG": "ENG", "FRE": "FRE", "GER": "GER"}
# MSN list index (file order) -> (name, offset of the references, count).
MSN_ARRAYS = {
    3: (("objectives", 2, 10), ("briefing", 0x16, 30), ("intelligence", 0x52, 30)),
    4: (("debrief", 2, 30),),
}
MSN_STRIDES = (14, 82, 8, 144, 64)
LABEL_LIMIT = 100


def dfn_widths(data):
    """{byte: advance} of a DFN font (docs/formats/dfn.md)."""
    if cd.take(data, 0, 4) != bytes((5, 0, 0x28, 0)):
        raise cd.FormatError("no DFN tag")
    count = cd.i16(data, 8)
    first = cd.i16(data, 0x0c)
    fixed = cd.i16(data, 0x10)
    colours = cd.i16(data, 0x1c)
    pixel_bytes = cd.u32(data, 0x1e)
    offset = 0x22 + 4 * colours + pixel_bytes + 4 * count
    if fixed == -1:
        widths = cd.take(data, offset, count)
    else:
        widths = bytes([fixed & 0xff]) * count
    return {first + i: widths[i] for i in range(count) if 0 <= first + i < 256}


def language_records(data):
    """(id, condition, third word, text) of a mission language member
    (mission.md, "Localized .ENG/.FRE/.GER mission records")."""
    count = cd.i16(data, 0)
    offset = 2
    out = []
    for _ in range(count):
        ident = cd.i16(data, offset)
        condition = cd.i16(data, offset + 2)
        third = cd.i16(data, offset + 4)
        length = cd.i16(data, offset + 6)
        text = cd.take(data, offset + 8, length)
        offset += 8 + length
        out.append((ident, condition, third, text.split(b"\0")[0]))
    if offset != len(data):
        raise cd.FormatError(f"language records end at {offset:#x} of {len(data):#x}")
    return out


def msn_reference_arrays(data):
    """(array name, record index, references) of MSN lists 4 and 5."""
    if cd.i16(data, 0) != 5:
        raise cd.FormatError("MSN version is not 5")
    offset = 2
    out = []
    for index, stride in enumerate(MSN_STRIDES):
        count = cd.i16(data, offset)
        if count < 0:
            raise cd.FormatError(f"list {index} count {count}")
        offset += 2
        for record_index in range(count):
            record = cd.take(data, offset + stride * record_index, stride)
            for name, start, total in MSN_ARRAYS.get(index, ()):
                refs = [cd.i16(record, start + 2 * i) for i in range(total)]
                out.append((name, record_index, refs))
        offset += stride * count
    return out


class Breaker:
    """The safety measures of one width table over byte streams."""

    def __init__(self, widths):
        self.widths = widths
        self.space = widths.get(0x20, 0)
        self.missing = set()

    def advance(self, byte):
        if byte < 0x20:
            byte = 0x20
        if byte not in self.widths:
            self.missing.add(byte)
            return 0
        return self.widths[byte]

    def step(self, states, text, worst):
        """Feed text to every state (previous word + space width or None,
        current word width); worst holds the widest word and pair."""
        for byte in text:
            if byte == 0x0a:
                states = {(None, 0)}
                continue
            if byte == 0x20:
                nxt = set()
                for before, word in states:
                    worst["word"] = max(worst["word"], word)
                    if before is not None:
                        worst["pair"] = max(worst["pair"], before + word)
                    nxt.add((word + self.space, 0))
                states = nxt
                continue
            add = self.advance(byte)
            states = {(before, word + add) for before, word in states}
        return prune(states)

    def finish(self, states, worst):
        """The text end measures the last word (the end case breaks without
        the assertion, so it is reported only as the last word's width)."""
        for before, word in states:
            worst["last_word"] = max(worst["last_word"], word)


def prune(states):
    """Keep the states not dominated in both components (the measures only
    grow with them; None ranks below every width)."""
    ordered = sorted(states, key=lambda s: (-1 if s[0] is None else s[0], s[1]), reverse=True)
    kept = []
    best_word = -1
    for before, word in ordered:
        if word > best_word:
            kept.append((before, word))
            best_word = word
    return set(kept)


def new_worst():
    return {"word": 0, "pair": 0, "last_word": 0}


def worst_over_choices(breaker, choices):
    """The widest word and pair over every composition that takes, at each
    position, one of the byte strings in choices[i] or nothing."""
    worst = new_worst()
    states = {(None, 0)}
    for options in choices:
        nxt = set(states)
        for text in options:
            nxt |= breaker.step(states, text, worst)
        states = prune(nxt)
    breaker.finish(states, worst)
    return worst


def break_lines(widths, text, width):
    """0x40d686 on one text: the line strings and whether the assertion
    fired (vshell_text.md "Line breaking 0x40d686")."""
    buf = bytearray(text) + b"\0"
    adv = lambda b: widths.get(0x20 if b < 0x20 else b, 0)

    def measure(start, end):
        total = 0
        for k in range(start, end):
            if buf[k] == 0:
                break
            total += adv(buf[k])
        return total

    lines = [0]
    last = 0
    start = 0
    fatal = False
    p = 0
    while buf[p] != 0:
        byte = buf[p]
        if byte == 0x0a:
            buf[p] = 0
            last = p
            start = p + 1
            lines.append(start)
        elif byte < 0x20:
            buf[p] = 0x20
        elif byte == 0x20:
            if measure(start, p) <= width:
                last = p
            else:
                if buf[last] == 0x20:
                    buf[last] = 0
                start = last + 1
                lines.append(start)
                if measure(start, p) >= width:
                    fatal = True
        p += 1
    if measure(start, p) > width:
        buf[last] = 0
        start = last + 1
        lines.append(start)

    def line(at):
        end = buf.index(0, at)
        return bytes(buf[at:end])
    return [line(at) for at in lines], fatal


def single_variant_compositions(choices):
    """Every reference present with its first variant, and each further
    variant of one reference in that context."""
    present = [c for c in choices if c]
    yield b"".join(c[0] for c in present)
    for i, options in enumerate(present):
        for variant in options[1:]:
            yield b"".join(variant if k == i else c[0] for k, c in enumerate(present))


def scan(root):
    root = Path(root)
    archives = {label: members for label, _, members in cd.archives(root)}
    if "VOL/SHELL1.VOL" not in archives or "VOL/ZONES.VOL" not in archives:
        raise SystemExit("SHELL1.VOL or ZONES.VOL missing below the root")
    font = next(m.payload() for m in archives["VOL/SHELL1.VOL"]
                if m.path.upper() == "DFN\\FONT2.DFN")
    widths = dfn_widths(font)
    breaker = Breaker(widths)
    result = {"font": "VOL/SHELL1.VOL:DFN\\FONT2.DFN",
              "campaign_width": CAMPAIGN_WIDTH, "panel_width": PANEL_WIDTH,
              "campaign": [], "missions": {}, "violations": []}
    longest = {"campaign": {"bytes": 0, "where": None},
               "missions": {"bytes": 0, "where": None}}
    counts = {"campaign_strings": 0, "mission_texts": 0, "compositions": 0}

    def note_lines(kind, where, text, width):
        lines, fatal = break_lines(widths, text, width)
        if fatal:
            result["violations"].append({"where": where, "case": "assertion"})
        for line in lines:
            if len(line) > LABEL_LIMIT:
                result["violations"].append({"where": where, "case": "label",
                                             "bytes": len(line)})
            if len(line) > longest[kind]["bytes"]:
                longest[kind].update(bytes=len(line), where=where)

    for label, data in cd.entries(root, lambda n: n == "CAMPAIGN.STR"):
        for g, group in enumerate(cd.str_groups(data)):
            for s, (text, _) in enumerate(group):
                worst = worst_over_choices(breaker, [[text]])
                where = f"{label} group {g} string {s}"
                result["campaign"].append(dict(where=where, bytes=len(text), **worst))
                note_lines("campaign", where, text, CAMPAIGN_WIDTH)
                counts["campaign_strings"] += 1
                check(result, where, worst, CAMPAIGN_WIDTH)

    zones = {m.name.upper(): m.payload() for m in archives["VOL/ZONES.VOL"]}
    totals = new_worst()
    for name in sorted(zones):
        stem, _, ext = name.rpartition(".")
        if ext not in LANGUAGES or stem + ".MSN" not in zones:
            continue
        variants = {}
        for ident, _, _, text in language_records(zones[name]):
            variants.setdefault(ident, []).append(text)
        mission = {}
        for array, index, refs in msn_reference_arrays(zones[stem + ".MSN"]):
            choices = [variants.get(r, []) for r in refs if r != -1]
            worst = worst_over_choices(breaker, choices)
            where = f"VOL/ZONES.VOL:MSN\\{name} {array} {index}"
            check(result, where, worst, PANEL_WIDTH)
            counts["mission_texts"] += 1
            for composition in single_variant_compositions(choices):
                note_lines("missions", where, composition, PANEL_WIDTH)
                counts["compositions"] += 1
            entry = mission.setdefault(array, new_worst())
            for key in entry:
                entry[key] = max(entry[key], worst[key])
                totals[key] = max(totals[key], worst[key])
        result["missions"][name] = mission
    result["mission_maximum"] = totals
    result["campaign_maximum"] = {k: max((c[k] for c in result["campaign"]), default=0)
                                  for k in new_worst()}
    result["longest_line"] = longest
    result["counts"] = counts
    result["bytes_without_glyph"] = sorted(breaker.missing)
    result["holds"] = not result["violations"]
    return result


def check(result, where, worst, width):
    if worst["word"] >= width:
        result["violations"].append({"where": where, "case": "word", "width": worst["word"]})
    if worst["pair"] > width:
        result["violations"].append({"where": where, "case": "pair", "width": worst["pair"]})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", help="CD root (default ES2_DATA_ROOT)")
    parser.add_argument("--out", help="write the JSON evidence here")
    args = parser.parse_args(argv)
    result = scan(cd.data_root(args.root))
    text = json.dumps(result, indent=1)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    summary = {k: result[k] for k in ("holds", "counts", "campaign_maximum",
                                      "mission_maximum", "longest_line",
                                      "bytes_without_glyph")}
    print(json.dumps(summary))
    return 0 if result["holds"] else 1


if __name__ == "__main__":
    sys.exit(main())
