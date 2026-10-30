# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Widget draws against VSHELL's own draw code in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/gui/widgets.h through the
test driver widgets_tool. The original: the memory worker's slot-mode
operation `widget-draw` (tools/reference-game/shell-invitro/main.c,
s28_widget_draw): the hidden-WinButton erase 0x409f61, the ESRect frame
0x40acd6 with and without fill, ESButtonFont 0x40a129 (frame and bevel),
ESRegionFill 0x40af56, ESTitle 0x40b1d7 (bar, stripes, gap, body dither),
ESArm 0x40cac3, ESGrid 0x40bf2c (grid lines), ESQuad 0x40abb0 and ESPal
0x40d0f0, each into a canvas filled from a seed; both sides return the
canvas's SHA-256 per case, compared case by case.

Cases: fixed edge cases (empty and one-pixel rectangles, bars of height 0
to 2, splits outside the rectangle, frames off) and SWEEP seeded cases over
every kind with random rectangles, colours and fields. Nothing original is
stored or printed beyond hashes and counts.

Arguments: widgets_tool es2shellprep slots_oracle.py
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

SWEEP = 1800
CW, CH = 200, 120
KINDS = ('erase', 'frame', 'button', 'region', 'title', 'arm', 'grid', 'quad', 'pal')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table():
    """An own slot table (gamefile.str), no flagged slot."""
    body = struct.pack('<h', 12)
    for k in range(12):
        name = f'GAME_{k}.SAV'.encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\0'
    body += struct.pack('<h', 12)
    for k in range(12):
        text = f'S{k}'.encode() + b'\0'
        body += struct.pack('<h', len(text)) + text + b'\0'
    return struct.pack('<I', len(body)) + body


def record(kind, flags, rect, colour=33, f55=0, f59=0, f5d=0, f61=0, f6d=0, f71=0, grid=0x22, seed=0):
    return struct.pack('<BBH12iI', kind, flags, 0, *rect, colour, f55, f59, f5d, f61, f6d, f71, grid,
                       seed & 0xffffffff)


def fixed():
    out = []
    for kind in range(9):
        for rect in ((10, 10, 10, 10), (10, 10, 11, 11), (10, 10, 12, 30), (5, 5, 60, 7), (0, 0, CW - 1, CH - 1),
                     (20, 20, 120, 80)):
            for flags in (0x00, 0x1f, 0x31, 0x7f, 0x4a):
                out.append(record(kind, flags, rect, colour=40 + kind, f55=3, f59=7, f5d=9, f61=2, f6d=5,
                                  f71=30, seed=kind * 1000 + flags))
    for bar in (-1, 0, 1, 2, 3, 19, 70):   # ESTitle bars, the stripes are 45-degree lines
        out.append(record(4, 0x20, (10, 10, 150, 90), f55=37, f5d=16, f61=bar, f6d=-3, f71=200, seed=bar))
    for split in (-5, 0, 1, 39, 40, 41, 60):   # ESArm split rows inside and outside
        out.append(record(5, 0x50, (10, 10, 100, 50), f55=split, f59=44, seed=split))
    return out


def sweep(rng):
    out = []
    for _ in range(SWEEP):
        kind = rng.randrange(9)
        w, h = rng.choice((rng.randrange(0, 6), rng.randrange(0, 160))), rng.choice((rng.randrange(0, 6),
                                                                                       rng.randrange(0, 100)))
        w, h = min(w, CW - 1), min(h, CH - 1)
        l, t = rng.randrange(0, CW - w), rng.randrange(0, CH - h)
        flags = rng.randrange(128)
        # colours are dwords: a high byte other than 0 (here negative values) draws no pen pixel
        colour = rng.randrange(256) if rng.random() < 0.9 else rng.randrange(-300, 0)
        out.append(record(kind, flags, (l, t, l + w, t + h), colour=colour,
                          f55=rng.choice((rng.randrange(256), rng.randrange(-5, h + 6))),
                          f59=rng.randrange(256), f5d=rng.randrange(256),
                          f61=rng.choice((rng.randrange(0, h + 4), rng.randrange(-3, 4), 19, 20)),
                          f6d=rng.randrange(-5, w + 6), f71=rng.randrange(-5, w + 6),
                          grid=rng.randrange(256), seed=rng.randrange(2 ** 32)))
    return out


def main(argv):
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    tool, worker, oracle_py = [Path(a).resolve() for a in argv]
    oracle = load(oracle_py, 'widget_oracle')
    cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
    if not (cd / 'VER95' / 'VSHELL.EXE').is_file():
        print('SKIP: no VER95/VSHELL.EXE under ES2_DATA_ROOT')
        return 0
    recs = fixed() + sweep(random.Random(2828))
    data = struct.pack('<HH', CW, CH) + b''.join(recs)
    failures = []
    with tempfile.TemporaryDirectory(prefix='es2widget-', dir=os.environ.get('TEST_TMPDIR')) as tmp:
        tmp = Path(tmp)
        (tmp / 'widget.in').write_bytes(data)
        run = subprocess.run([str(tool), str(tmp / 'widget.in'), str(tmp / 'widget.ours')], capture_output=True)
        if run.returncode:
            print('FAIL widgets_tool exited', run.returncode, run.stderr.decode(errors='replace')[-500:])
            return 1
        ours = (tmp / 'widget.ours').read_bytes()
        got = oracle.run(worker, cd, {'sav\\gamefile.str': table(), 'widget.in': data}, ['widget-draw'],
                         tmp / 'run', timeout=900)
        theirs = got['disk'].get('widget.emit', b'')
    n = len(recs)
    if len(ours) != 32 * n or len(theirs) != 32 * n:
        print(f'FAIL emitted {len(ours)}/{len(theirs)} bytes for {n} cases')
        return 1
    per_kind = {k: [0, 0] for k in KINDS}
    for i, rec in enumerate(recs):
        kind = KINDS[rec[0]]
        same = ours[32 * i:32 * i + 32] == theirs[32 * i:32 * i + 32]
        per_kind[kind][0] += 1
        per_kind[kind][1] += same
        if not same:
            failures.append(i)
            if len(failures) <= 20:
                fields = struct.unpack_from('<BBH12iI', rec)
                print(f'FAIL case {i} {kind}: flags 0x{fields[1]:02x} rect {fields[3:7]} fields {fields[7:15]}')
    for kind, (cases, equal) in per_kind.items():
        print(f'{kind:7s} {equal}/{cases} equal')
    print(f'widget-draw: {n - len(failures)}/{n} cases equal ({len(fixed())} fixed, {SWEEP} seeded), '
          f'original stream {hashlib.sha256(theirs).hexdigest()[:16]}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
