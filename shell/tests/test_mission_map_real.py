# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""The mission map against VSHELL's own code in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/gui/mission_map.h through the
test driver mission_map_tool. The original: the memory worker's slot-mode
operations (tools/reference-game/shell-invitro/main.c):

  map-raster  the primitives the map draws with: the colour-banded triangle
              0x457de8, the polygon 0x455ad8 (clipper 0x455f58, span builder
              0x456340), the textured quad 0x45399c, the line 0x455624, the
              ellipse 0x45886c, the pixel 0x459cdc, the rectangle 0x4576a4
              (outline and fill), the bitmap 0x4537e8 and the scaled bitmap
              0x453908, each into a canvas filled from a seed; both sides
              return the canvas's SHA-256 per case.

  map-build:ID, map-step:T, map-arrow:K, map-skip, map-paint
              VSHELL's own map object: the build 0x40e75f (script reader,
              squad, views, the terrain image 0x427634, the material draws on
              0x48110a), the fly-over 0x4262cf under a patched tick, the
              arrow slots and the paint 0x425a5e on the frame; per operation
              the frame's SHA-256, the state, the step result, the done flag,
              the tick, the view, the pan and the pan step (and per build the
              terrain image's SHA-256, its size and the RNG state 0x48110a).
              The missions are prepared by es2prep (byte-equal with VSHELL's
              preparation); MISSIONS lists them.

Cases: fixed edge cases plus a seeded sweep (SWEEP cases, seed 4040); for the
map a fixed scenario per mission (scenario()). Nothing original is stored or
printed beyond hashes and counts.

Arguments: mission_map_tool es2shellprep slots_oracle.py es2prep.py es2prep_core
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

SWEEP = 3000
CW, CH = 240, 180
KINDS = ('triangle', 'polygon', 'quad', 'line', 'ellipse', 'pixel', 'outline', 'fill', 'bitmap', 'scaled')


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


def record(kind, args, clip=1, offset=0, rect=(0, 0, CW - 1, CH - 1), origin=(0, 0), seed=0, colour=33,
           bitmap=None, bflags=0, flags=0):
    args = list(args) + [0] * (20 - len(args))
    bw, bh, pix = (0, 0, b'') if bitmap is None else bitmap
    return (struct.pack('<4B6iII20iHHB3x', kind, clip, offset, flags, *rect, *origin, seed & 0xffffffff,
                        colour & 0xffffffff, *args, bw, bh, bflags) + pix)


def bitmap(rng, w, h, zero=0.2):
    return (w, h, bytes(0 if rng.random() < zero else rng.randrange(1, 256) for _ in range(w * h)))


def fixed():
    rng = random.Random(40)
    out = []
    # terrain-like triangles: cells of k pixels, colour words 0xd1..0xea and 0
    for k in (1, 2, 3, 5, 8, 13):
        for c in ((0xd1, 0xd1, 0xd1), (0xd1, 0xea, 0xd5), (0xea, 0xd1, 0xd1), (0xd5, 0xd5, 0xe0), (0, 0, 0),
                  (0xe0, 0xd2, 0xe0), (0xd2, 0xe0, 0xe9)):
            x, y = 20, 30
            out.append(record(0, [x, y + k, x, y, x + k, y, *c], rect=(0, 0, CW - 1, CH - 1), seed=k))
            out.append(record(0, [x, y + k, x + k, y, x + k, y + k, *c], seed=k + 1))
    # degenerate triangles and polygons
    out.append(record(0, [5, 5, 5, 5, 9, 9, 1, 2, 3]))
    out.append(record(1, [3, 10, 10, 10, 10, 10, 10]))
    out.append(record(1, [3, 10, 10, 50, 10, 30, 10]))
    out.append(record(1, [4, 10, 10, 50, 10, 50, 40, 10, 40]))
    out.append(record(1, [4, 10, 10, 10, 40, 50, 40, 50, 10]))
    # the map's 7 x 7 ellipse inside, on and across the rectangle edges
    for cx, cy in ((100, 90), (40, 40), (41, 38), (37, 41), (200, 150), (203, 152), (0, 0), (3, 3)):
        out.append(record(4, [cx - 3, cy - 3, cx + 3, cy + 3], rect=(40, 40, 200, 150), colour=14))
    # the map's outline and background fill with the offset (centre 438-291, 180-67)
    out.append(record(6, [-60, -50, 60, 49], offset=1, rect=(30, 20, 210, 160), origin=(-90, -70), colour=10))
    out.append(record(7, [-90, -70, 90, 69], offset=1, rect=(30, 20, 210, 160), origin=(-90, -70), colour=16))
    # a terrain-sized quad: image of ncols * k by nrows * k stretched over the map
    img = bitmap(rng, 96, 72, zero=0.05)
    for x, y, sx, sy in ((10, 8, 200, 150), (-40, -30, 320, 240), (60, 50, 30, 20), (100, 100, 1, 1),
                         (5, 5, 96, 72), (0, 0, 0, 10), (12, 12, 7, 0)):
        out.append(record(2, [x, y, sx, sy], rect=(20, 15, 220, 165), bitmap=img))
    return out


def sweep(rng):
    out = []
    for _ in range(SWEEP):
        kind = rng.randrange(10)
        clip = rng.randrange(2)
        l, t = rng.randrange(0, CW // 2), rng.randrange(0, CH // 2)
        rect = (l, t, rng.randrange(l, CW), rng.randrange(t, CH))
        seed, colour = rng.randrange(2 ** 32), rng.randrange(256)
        # in clip mode 0 VSHELL does not clip: keep everything on the canvas
        lo_x, hi_x, lo_y, hi_y = (-60, CW + 60, -60, CH + 60) if clip else (0, CW - 1, 0, CH - 1)

        def pt():
            return rng.randrange(lo_x, hi_x), rng.randrange(lo_y, hi_y)

        if kind == 0:
            k = rng.choice((1, 2, 3, 4, 6, 9, 15, 40))
            x, y = rng.randrange(0, CW - k), rng.randrange(0, CH - k)
            base = rng.randrange(0xd1, 0xeb)
            cols = [rng.choice((base, rng.randrange(0xd1, 0xeb), 0)) for _ in range(3)]
            if rng.random() < 0.5:
                pts = [x, y + k, x, y, x + k, y] if rng.random() < 0.5 else [x, y + k, x + k, y, x + k, y + k]
            else:
                pts = [v for _ in range(3) for v in (rng.randrange(0, CW), rng.randrange(0, CH))]
            out.append(record(0, pts + cols, clip=1, rect=(0, 0, CW - 1, CH - 1), seed=seed))
        elif kind == 1:
            n = rng.randrange(3, 9)
            if rng.random() < 0.6:  # convex: points on a circle
                import math
                cx, cy, r = rng.randrange(0, CW), rng.randrange(0, CH), rng.randrange(1, 90)
                angles = sorted(rng.random() * 2 * math.pi for _ in range(n))
                if rng.random() < 0.5:
                    angles.reverse()
                pts = [(int(cx + r * math.cos(a)), int(cy + r * math.sin(a))) for a in angles]
                if not clip:
                    pts = [(min(max(x, 0), CW - 1), min(max(y, 0), CH - 1)) for x, y in pts]
            else:
                pts = [pt() for _ in range(n)]
            out.append(record(1, [n] + [v for p in pts for v in p], clip=clip, rect=rect, seed=seed, colour=colour))
        elif kind == 2:
            w, h = rng.choice(((1, 1), (2, 3), (16, 9), (40, 30), (96, 72), (rng.randrange(1, 64), rng.randrange(1, 64))))
            x, y = rect[0] + rng.randrange(-60, 40), rect[1] + rng.randrange(-60, 40)
            sx, sy = rng.choice((rng.randrange(0, 4), rng.randrange(0, 260))), rng.randrange(0, 200)
            out.append(record(2, [x, y, sx, sy], clip=clip, rect=rect, seed=seed, bitmap=bitmap(rng, w, h)))
        elif kind == 3:
            (x0, y0), (x1, y1) = pt(), pt()
            out.append(record(3, [x0, y0, x1, y1], clip=clip, rect=rect, seed=seed, colour=colour))
        elif kind == 4:
            r = rng.choice((3, 3, 3, rng.randrange(1, 12), rng.randrange(1, 60)))
            ry = r if rng.random() < 0.6 else rng.randrange(1, 40)
            cx, cy = rng.randrange(r + 1, CW - r - 1), rng.randrange(ry + 1, CH - ry - 1)
            out.append(record(4, [cx - r, cy - ry, cx + r, cy + ry], clip=clip, rect=rect, seed=seed, colour=colour))
        elif kind == 5:
            x, y = pt()
            out.append(record(5, [x, y], clip=clip, rect=rect, seed=seed, colour=colour))
        elif kind in (6, 7):
            x0, y0 = pt()
            x1, y1 = rng.randrange(x0, max(x0 + 1, hi_x)), rng.randrange(y0, max(y0 + 1, hi_y))
            if kind == 7 and not clip:
                x1, y1 = min(x1, CW - 1), min(y1, CH - 1)
            out.append(record(kind, [x0, y0, x1, y1], clip=clip, rect=rect, seed=seed, colour=colour))
        elif kind == 8:
            w, h = rng.randrange(1, 40), rng.randrange(1, 40)
            x, y = rng.randrange(-w, CW), rng.randrange(-h, CH)
            out.append(record(8, [x, y], clip=clip, rect=rect, seed=seed, bitmap=bitmap(rng, w, h),
                              bflags=rng.choice((0, 0x10))))
        else:
            w, h = rng.choice(((13, 13), (16, 16), (rng.randrange(1, 40), rng.randrange(1, 40))))
            s = rng.randrange(1, 60)
            sx, sy = (s, s) if rng.random() < 0.7 else (rng.randrange(1, 60), rng.randrange(1, 60))
            x, y = rng.randrange(-sx, CW), rng.randrange(-sy, CH)
            out.append(record(9, [x, y, sx, sy], clip=clip, rect=rect, seed=seed, bitmap=bitmap(rng, w, h),
                              bflags=rng.choice((0, 0x10))))
    return out


def raster(tool, worker, oracle, cd, tmp):
    fixed_cases = fixed()
    recs = fixed_cases + sweep(random.Random(4040))
    data = struct.pack('<HH', CW, CH) + b''.join(recs)
    (tmp / 'map.in').write_bytes(data)
    run = subprocess.run([str(tool), 'raster', str(tmp / 'map.in'), str(tmp / 'map.ours')], capture_output=True)
    if run.returncode:
        print('FAIL mission_map_tool exited', run.returncode, run.stderr.decode(errors='replace')[-500:])
        return 1
    ours = (tmp / 'map.ours').read_bytes()
    subprocess.run([str(tool), 'blank', str(tmp / 'map.in'), str(tmp / 'map.blank')], check=True)
    blank = (tmp / 'map.blank').read_bytes()
    got = oracle.run(worker, cd, {'sav\\gamefile.str': table(), 'map.in': data}, ['map-raster'], tmp / 'run',
                     timeout=900)
    theirs = got['disk'].get('map.emit', b'')
    n = len(recs)
    if len(ours) != 32 * n or len(theirs) != 32 * n:
        print(f'FAIL emitted {len(ours)}/{len(theirs)} bytes for {n} cases')
        return 1
    per_kind = {k: [0, 0, 0] for k in KINDS}
    failures = 0
    for i, rec in enumerate(recs):
        kind = KINDS[rec[0]]
        same = ours[32 * i:32 * i + 32] == theirs[32 * i:32 * i + 32]
        per_kind[kind][0] += 1
        per_kind[kind][1] += same
        per_kind[kind][2] += theirs[32 * i:32 * i + 32] != blank[32 * i:32 * i + 32]
        if not same:
            failures += 1
            if failures <= 20:
                f = struct.unpack_from('<4B6iII20i', rec)
                print(f'FAIL case {i} {kind}: clip {f[1]} offset {f[2]} rect {f[4:8]} args {f[12:24]}')
    for kind, (cases, equal, drawn) in per_kind.items():
        print(f'{kind:8s} {equal}/{cases} equal, {drawn} of them changed the canvas')
        if cases and drawn * 3 < cases:
            print(f'FAIL {kind}: too few cases draw anything')
            failures += 1
    print(f'map-raster: {n - failures}/{n} cases equal ({len(fixed_cases)} fixed, {SWEEP} seeded), '
          f'original stream {hashlib.sha256(theirs).hexdigest()[:16]}')
    return 1 if failures else 0


# ---- the map object: VSHELL's build, fly-over, arrows and skip ---------------

# (group, position) of es2prep's campaign list: training, demo and every
# theatre; C2_08 and C3_09 reach the height table's last row (documented
# refusal: VSHELL reads the heap bytes after its table there).
MISSIONS = ((0, 1), (0, 8), (1, 0), (1, 6), (2, 5), (2, 7), (3, 2), (3, 8), (4, 4), (5, 6), (5, 9))
HEAD = 36 + 114
PROBE = 64


def scenario(seed):
    """Build, the fly-over at random 1..5-tick steps until it is long done,
    the six arrows at several zooms, a paint, a rebuild that is skipped
    early (Esc/Space/click) and runs to the end."""
    r = random.Random(seed)
    ops, t = ['map-build:1'], 1000
    for _ in range(420):
        ops.append(f'map-step:{t}')
        t += r.randrange(1, 6)
    ops += ['map-arrow:0', 'map-arrow:0', 'map-arrow:3', 'map-arrow:4', 'map-arrow:4', 'map-arrow:5',
            'map-arrow:5', 'map-arrow:5', 'map-arrow:2', 'map-arrow:1', 'map-arrow:1', 'map-arrow:5', 'map-arrow:4',
            'map-step:9000', 'map-paint', 'map-build:1', 'map-step:20000', 'map-step:20003', 'map-step:20070',
            'map-skip']
    t = 20071
    for _ in range(40):
        ops.append(f'map-step:{t}')
        t += 2
    return ops


def records(stream, ops):
    out, at = [], 0
    for op in ops:
        if op == 'map-frame':
            continue
        n = HEAD + PROBE if op.startswith('map-build') else PROBE
        out.append((op, stream[at:at + n]))
        at += n
    return out if at == len(stream) else None


def maps(tool, worker, oracle, prep, core, cd, tmp):
    data_files = {f'data\\{n.lower()}': (cd / 'DATA' / n).read_bytes() for n in ('MFORMS.DAT', 'MAT0.DAT', 'MAPLABEL.STR')}
    rng, tables = prep.initial_rng(cd, 0), prep.default_tables(cd)
    status, equal, total, done_seen = 0, 0, 0, 0
    for group, position in MISSIONS:
        prepared, = prep.prepare_batch(core, cd, [prep.request(group=group, position=position, mode=1, rng=rng,
                                                               tables=tables)])
        if 'error' in prepared:
            print(f'FAIL preparation ({group},{position}): {prepared["error"]}')
            status = 1
            continue
        rec = struct.pack('<I', 1)
        for name in ('script.dat', 'player.mec', 'MISSION.STR'):
            rec += struct.pack('<I', len(prepared[name])) + prepared[name]
        (tmp / 'maps.in').write_bytes(rec)
        ops = scenario(group * 100 + position)
        run = subprocess.run([str(tool), 'map', str(cd), str(tmp / 'maps.in'), str(tmp / 'ours.emit'),
                              str(tmp / 'ours.frames')] + ops, capture_output=True)
        if run.returncode:
            print(f'FAIL mission_map_tool map ({group},{position}) exited {run.returncode}')
            status = 1
            continue
        refused = b'refused 1' in run.stderr
        got = oracle.run(worker, cd, dict({'maps.in': rec, 'sav\\gamefile.str': table()}, **data_files), ops,
                         tmp / f'run-{group}-{position}', timeout=900)
        ours, theirs = records((tmp / 'ours.emit').read_bytes(), ops), records(got['disk'].get('map.emit', b''), ops)
        if ours is None or theirs is None:
            print(f'FAIL ({group},{position}): emitted streams do not frame')
            status = 1
            continue
        same = 0
        for (op, a), (_, b) in zip(ours, theirs):
            # the state, tick, view, pan and step always; the image and the
            # frame unless the build refused the height table's last row
            tail = a[-PROBE + 32:] == b[-PROBE + 32:] and a[32:-PROBE] == b[32:-PROBE]  # + image size, RNG
            if tail and (refused or a == b):
                same += 1
            elif status == 0 or same < 3:
                print(f'FAIL ({group},{position}) {op}: {"frame or image" if tail else "state"} differs')
            done_seen += b[-PROBE + 35] == 1 and b[-PROBE + 32] == 0x13
        equal += same
        total += len(ours)
        if same != len(ours):
            status = 1
        print(f'({group},{position}): {same}/{len(ours)} probes equal{" (refused rows: frames not compared)" if refused else ""}')
    if not done_seen:
        print('FAIL no scenario reached the end of the fly-over')
        status = 1
    print(f'map: {equal}/{total} probes equal over {len(MISSIONS)} missions, {done_seen} after the fly-over ended')
    return status


def main(argv):
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    tool, worker, oracle_py, prep_py, core = [Path(a).resolve() for a in argv[:5]]
    oracle = load(oracle_py, 'map_oracle')
    prep = load(prep_py, 'map_prep')
    cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
    if not (cd / 'VER95' / 'VSHELL.EXE').is_file():
        print('SKIP: no VER95/VSHELL.EXE under ES2_DATA_ROOT')
        return 0
    status = 0
    with tempfile.TemporaryDirectory(prefix='es2map-', dir=os.environ.get('TEST_TMPDIR')) as tmp:
        tmp = Path(tmp)
        status |= raster(tool, worker, oracle, cd, tmp)
        status |= maps(tool, worker, oracle, prep, core, cd, tmp)
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
