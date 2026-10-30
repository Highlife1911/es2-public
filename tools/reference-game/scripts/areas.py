"""Module map of the reference DBSIM.EXE: which address range belongs to which area.

The Borland linker places one source module after the other, so the code
segment falls into contiguous ranges that each belong to one subsystem. The
ranges and their basis are the table of docs/reference-game/dbsim_gap.md
section 1 ("Areas (estimate)"); this module is the same table as
data, used by `function_index.py` for the area column of docs/functions/ and
checked against the document by `--check-document`:

    python areas.py --check-document docs/reference-game/dbsim_gap.md

The map is an estimate, not a measurement: it names the module a range comes
from, not the role of every function in it. Addresses refer to the reference
binary at image base 0x00400000 (docs/reference_version.md). Ranges are
half-open, ascending and disjoint; addresses that no range covers (the gaps
between modules that the document does not assign) have no area. The
document additionally moves single functions by verdict or by the majority of
their callers; the column here is the pure address map.

Standard library only.
"""
import argparse
import re
import sys
from pathlib import Path

# Area -> short description (the "Area" column of dbsim_gap.md section 3).
AREAS = {
    'SIM': 'simulation leaves (actor, weapon, effect, AI and mission modules)',
    'R3D': '3D renderer, camera, view',
    'HUD': 'cockpit, HUD, MFD/HDD, gadgets, ports, widgets, display modes',
    'DLG': 'modal dialogs and alerts',
    'PRES': 'presentation (palettes, present, frame clear, DirectDraw)',
    'SHELL': 'process shell (window, pump, WinMain, MainWndProc, clock, fatal error)',
    'AUD': 'audio (sound API, SFX manager, CD music, SOS glue)',
    'INP': 'input (devices, prefs, keyboard, joystick)',
    'FILE': 'files, streams, assets, configuration',
    'RTL': 'runtime, allocator, import thunks',
    'LOAD': 'mission-load module',
    'MATH': 'fixed-point, matrix and distance helpers',
}

# (first VA, end VA exclusive, area), ascending. Transcribed from the table of
# dbsim_gap.md section 1; the unit test and --check-document compare the two.
MODULE_MAP = (
    (0x401000, 0x401078, 'RTL'),
    (0x401078, 0x401d64, 'R3D'),
    (0x401dc0, 0x402000, 'R3D'),
    (0x402000, 0x428000, 'SIM'),
    (0x428000, 0x429660, 'R3D'),
    (0x429660, 0x42e700, 'HUD'),
    (0x42e700, 0x430390, 'R3D'),
    (0x430390, 0x4311e0, 'PRES'),
    (0x4311e0, 0x453824, 'HUD'),
    (0x453824, 0x453878, 'DLG'),
    (0x453878, 0x453a80, 'FILE'),
    (0x453a80, 0x459000, 'DLG'),
    (0x459000, 0x45c8bc, 'INP'),
    (0x45c8bc, 0x461608, 'SHELL'),
    (0x461608, 0x461ff0, 'LOAD'),
    (0x462398, 0x4649d8, 'AUD'),
    (0x4649d8, 0x464c68, 'PRES'),
    (0x464c68, 0x46548c, 'SHELL'),
    (0x46548c, 0x466010, 'PRES'),
    (0x466010, 0x46658c, 'SHELL'),
    (0x46658c, 0x466a86, 'PRES'),
    (0x467828, 0x467b00, 'SHELL'),
    (0x4680e0, 0x46a000, 'R3D'),
    (0x46ac14, 0x46ae00, 'R3D'),
    (0x46be9c, 0x46f000, 'R3D'),
    (0x470a1c, 0x470d94, 'R3D'),
    (0x471e10, 0x472e6c, 'R3D'),
    (0x472e6c, 0x473708, 'FILE'),
    (0x473708, 0x474b30, 'AUD'),
    (0x474b30, 0x476d00, 'R3D'),
    (0x476d00, 0x4775c0, 'RTL'),
    (0x4775c0, 0x477d48, 'INP'),
    (0x477d48, 0x478600, 'R3D'),
    (0x479c00, 0x47a0b8, 'SHELL'),
    (0x47a0b8, 0x47d32c, 'FILE'),
    (0x47d32c, 0x480738, 'MATH'),
    (0x480738, 0x4928d0, 'R3D'),
    (0x4928d0, 0x492b68, 'MATH'),
    (0x492b68, 0x492c80, 'FILE'),
    (0x4930a0, 0x495132, 'R3D'),
    (0x495132, 0x4952fc, 'MATH'),
    (0x4953c0, 0x4958fd, 'RTL'),
    (0x4958fd, 0x49630c, 'AUD'),
    (0x49630c, 0x496a60, 'RTL'),
)

_STARTS = [lo for lo, _, _ in MODULE_MAP]


def area_of(va):
    """Area name of an address, or '' where no range of the map covers it."""
    from bisect import bisect_right
    i = bisect_right(_STARTS, va) - 1
    if i >= 0 and va < MODULE_MAP[i][1]:
        return MODULE_MAP[i][2]
    return ''


def gaps(map_=MODULE_MAP):
    """[(first VA, end VA)] between consecutive ranges that no range covers."""
    return [(a[1], b[0]) for a, b in zip(map_, map_[1:]) if a[1] < b[0]]


def problems_in_map(map_=MODULE_MAP):
    """Structural problems of a range list: bad order, overlap, unknown area."""
    out = []
    for lo, hi, area in map_:
        if not lo < hi:
            out.append(f'range {lo:#x}..{hi:#x} is empty or reversed')
        if area not in AREAS:
            out.append(f'range {lo:#x}..{hi:#x}: unknown area {area!r}')
    for a, b in zip(map_, map_[1:]):
        if b[0] < a[1]:
            out.append(f'ranges {a[0]:#x}..{a[1]:#x} and {b[0]:#x}..{b[1]:#x} overlap or are unsorted')
    return out


RANGE = re.compile(r'`0x([0-9a-fA-F]+)\.\.0x([0-9a-fA-F]+)`')


def read_document_map(path):
    """[(first, end, area)] sorted, from the table of the document's section 1 whose
    header starts with "| Range | Area": every `0x..0x..` range of the first cell."""
    out, inside = [], False
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.startswith('| Range | Area'):
            inside = True
            continue
        if not inside:
            continue
        if not line.startswith('|'):
            break
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if len(cells) < 2 or set(cells[0]) <= set('-: '):
            continue
        for lo, hi in RANGE.findall(cells[0]):
            out.append((int(lo, 16), int(hi, 16), cells[1]))
    if not out:
        raise ValueError(f'{path}: no "| Range | Area" table with ranges')
    return sorted(out)


def check_document(path):
    """Differences between MODULE_MAP and the document's table (empty = they agree)."""
    doc = read_document_map(path)
    ours = set(MODULE_MAP)
    theirs = set(doc)
    out = [f'only in the document: {lo:#x}..{hi:#x} {area}' for lo, hi, area in sorted(theirs - ours)]
    out += [f'only in areas.py: {lo:#x}..{hi:#x} {area}' for lo, hi, area in sorted(ours - theirs)]
    return out + problems_in_map(doc)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check-document', metavar='dbsim_gap.md',
                    help='compare the module map with the table of the document; exit 1 on a difference')
    ap.add_argument('--list', action='store_true', help='print the ranges and the gaps')
    args = ap.parse_args(argv)
    if args.list:
        for lo, hi, area in MODULE_MAP:
            print(f'{lo:06x}..{hi:06x} {area}')
        for lo, hi in gaps():
            print(f'{lo:06x}..{hi:06x} (no area)')
    if args.check_document:
        found = problems_in_map() + check_document(args.check_document)
        for p in found:
            print('FAIL:', p)
        if found:
            return 1
        print(f'PASS: areas.py agrees with {args.check_document} ({len(MODULE_MAP)} ranges)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
