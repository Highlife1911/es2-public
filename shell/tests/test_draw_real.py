# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (as test_texts_real.py)
"""Images on the frame against VSHELL's own blitter and decompressor
in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/draw.h through the test
driver draw_tool. The original: the memory worker's slot mode
(tools/reference-game/shell-invitro, slots_oracle.py) with the
operations `blit` (0x45a09c on every record of blit.in into a memory frame
of the record's size) and `unpack` (0x453a50 on every record of unpack.in);
the frames and decoded buffers come back in blit.emit / unpack.emit and are
compared byte for byte.

- synthetic: SYNTH_RUNS x SYNTH_PER_RUN seeded blits (frames 8..96 x 8..72,
  bitmaps 1..80 square, every mirror mode, with and without the flag 0x10,
  other flag and mirror bits set at random, no clip or a clip inside the
  frame incl. single rows/columns, positions overlapping every edge);
- CD: CD_RUNS x CD_PER_RUN images of SHELL0.VOL (every member kind 0, as
  //formats:dbm_real_test counts) into 640 x 480 frames;
- kind 1 (RLE): RLE_COUNT seeded streams (zero counts, runs and literals
  cut by U, trailing bytes, U up to 60,000).
Nothing original is stored: CD pixels stay in the test's temporary
directory, the report has counts and SHA-256s of our outputs only.

Arguments: draw_tool es2shellprep slots_oracle.py
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

B = '\\'
SYNTH_RUNS, SYNTH_PER_RUN = 16, 250
CD_RUNS, CD_PER_RUN = 6, 8
RLE_COUNT = 200


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table():
    """An own slot table (gamefile.str), no flagged slot (as test_texts_real.py)."""
    body = struct.pack('<h', 12)
    for k in range(12):
        name = f'GAME_{k}.SAV'.encode() + b'\0'
        body += struct.pack('<h', len(name)) + name + b'\0'
    body += struct.pack('<h', 12)
    for k in range(12):
        text = f'S{k}'.encode() + b'\0'
        body += struct.pack('<h', len(text)) + text + b'\0'
    return struct.pack('<I', len(body)) + body


def blit_records(data):
    """Split blit.in into records (for per-case reporting)."""
    out, at = [], 0
    while at < len(data):
        fw, fh, bw, bh = struct.unpack_from('<4H', data, at)
        size = 36 + fw * fh + bw * bh
        out.append((data[at:at + size], fw * fh))
        at += size
    return out


class Test:
    def __init__(self, argv):
        self.tool, self.worker, oracle_py = [Path(a).resolve() for a in argv]
        self.oracle = load(oracle_py, 'draw_oracle')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2draw-', dir=os.environ.get('TEST_TMPDIR'))
        self.tmp = Path(self.scratch.name)
        self.cd = Path(os.environ['ES2_DATA_ROOT']).resolve()
        self.failures, self.report, self.runs = [], {}, 0

    def check(self, condition, message):
        if not condition:
            self.failures.append(message)
            print('FAIL', message, flush=True)
        return condition

    def tool_run(self, *args):
        run = subprocess.run([str(self.tool), *map(str, args)], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(f'draw_tool {args[0]}: {run.stderr.strip()}')
        return run.stdout.strip()

    def theirs(self, op, name, data):
        self.runs += 1
        disk = {'sav' + B + 'gamefile.str': table(), name + '.in': data}
        got = self.oracle.run(self.worker, self.cd, disk, [op], self.tmp / f'run-{self.runs}')
        return got['disk'].get(name + '.emit', b'')

    def compare_blits(self, label, path):
        data = path.read_bytes()
        out = self.tmp / (path.stem + '.ours')
        counts = self.tool_run('blit', path, out)
        ours, theirs = out.read_bytes(), self.theirs('blit', 'blit', data)
        records = blit_records(data)
        equal, at = 0, 0
        for k, (_, frame_bytes) in enumerate(records):
            same = ours[at:at + frame_bytes] == theirs[at:at + frame_bytes]
            equal += self.check(same, f'{label} {path.stem} case {k}: frame differs')
            at += frame_bytes
        self.check(len(ours) == len(theirs) == at, f'{label} {path.stem}: emit sizes {len(ours)} {len(theirs)} {at}')
        entry = self.report.setdefault(label, dict(cases=0, equal=0, counts=[], sha256=hashlib.sha256()))
        entry['cases'] += len(records)
        entry['equal'] += equal
        entry['counts'].append(counts)
        entry['sha256'].update(ours)

    def scenes(self):
        """The scene resources (palette table, background frames, zone images) exist
        and convert/draw: every palette writes 256 entries, every frame is 640 x 480
        kind 0 and draws."""
        lines = self.tool_run('scenes', self.cd).splitlines()
        self.check(not any(line.startswith('MISSING') for line in lines), f'scenes: {lines}')
        palettes = [line for line in lines if line.startswith('palette ')]
        self.check(len(palettes) == 20 and all(', 256 written' in line for line in palettes), f'palettes: {palettes}')
        frames = [line for line in lines if line.startswith('background ')]
        self.check(len(frames) == 6 and all('640x480 kind 0' in line and line.endswith('draw 0') for line in frames),
                   f'background: {frames}')
        zones = [line for line in lines if line.startswith('zone ')]
        self.check(len(zones) == 4 and all(line.endswith('draw 0') for line in zones), f'zones: {zones}')
        self.report['scenes'] = lines

    def main(self):
        started = time.time()
        self.scenes()
        for run in range(SYNTH_RUNS):
            path = self.tmp / f'synth-{run}.in'
            self.tool_run('cases', 0x2600 + run, SYNTH_PER_RUN, path)
            self.compare_blits('synthetic', path)
        for run in range(CD_RUNS):
            path = self.tmp / f'cd-{run}.in'
            self.report.setdefault('cd picks', []).append(self.tool_run('cd-cases', self.cd, 0x2610 + run, CD_PER_RUN, path))
            self.compare_blits('cd', path)
            path.unlink()
        path = self.tmp / 'rle.in'
        self.tool_run('rle-cases', 0x2620, RLE_COUNT, path)
        out = self.tmp / 'rle.ours'
        self.tool_run('unpack', path, out)
        ours, theirs = out.read_bytes(), self.theirs('unpack', 'unpack', path.read_bytes())
        equal, at, cases = 0, 0, 0
        while at + 4 <= len(ours):
            u, = struct.unpack_from('<I', ours, at)
            equal += self.check(ours[at:at + 4 + u] == theirs[at:at + 4 + u], f'rle case {cases} differs (U {u})')
            at += 4 + u
            cases += 1
        self.check(cases == RLE_COUNT and len(theirs) == len(ours), 'rle: emit sizes differ')
        self.report['rle'] = dict(cases=cases, equal=equal, bytes=len(ours), sha256=hashlib.sha256(ours).hexdigest())
        for key in ('synthetic', 'cd'):
            self.report[key]['sha256'] = self.report[key]['sha256'].hexdigest()
        self.report['worker_runs'] = self.runs
        self.report['seconds'] = round(time.time() - started, 1)


def main():
    if not os.environ.get('ES2_DATA_ROOT'):
        print('SKIP: ES2_DATA_ROOT not set')
        return 0
    test = Test(sys.argv[1:4])
    try:
        test.main()
    finally:
        test.scratch.cleanup()
    print(json.dumps(dict(report=test.report, failures=test.failures[:50]), indent=1))
    return int(bool(test.failures))


if __name__ == '__main__':
    raise SystemExit(main())
