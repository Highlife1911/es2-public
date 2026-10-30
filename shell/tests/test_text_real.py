# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Text drawing against VSHELL's own glyph, string, label and edit
draws in the mapped original.

Skipped without ES2_DATA_ROOT. Ours: es2/shell/gui/text.h through the test
driver text_tool. The original: the memory worker's slot-mode operation
`text-draw` (tools/reference-game/shell-invitro/main.c, s27_text_draw):
the string draw 0x401355, the label draw 0x40b9e9, the edit field's setText
0x40c294, insert 0x40c382, backspace 0x40c406 and draw 0x40c6ff with the
fonts VSHELL's loadFont 0x4088da reads (dfn\\font2, black, font, map), each
case into a canvas filled from a seed. Both sides return the SHA-256 of the
canvas per case and, for edit fields, length, width and the text buffer;
they are compared case by case.

- ESTEXT: every string of ENG, FRE and GER ESTEXT.BIN whose bytes all have
  glyphs, in each of the four fonts, as a label (below 100 bytes) or a
  string draw; the counts and one combined hash per language are printed.
- Sweep: SWEEP seeded cases: string draws (alignments 0..3, widths below
  and above the text, pens outside the clip), labels (y = -1 and explicit,
  background on/off, any colour, empty texts, rectangles down to 0 x 0),
  edit draws (caret on/off) and edit typing over the four character sets
  with backspaces, including the Saved games rows (r - l = 325 and 327,
  prefix " N. ") and the registration field (206) typed past their limit.
Nothing original is stored or printed beyond hashes and counts.

Arguments: text_tool es2shellprep slots_oracle.py
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import time

B = '\\'
SWEEP = 900
LANGUAGES = (('eng', 'E'), ('fre', 'F'), ('ger', 'G'))
SETS = (None, b'ABCDEFGHIJKLMNOPQRSTUVWXYZ ',
        b'^0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ',
        b'0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ')
# Codes with a glyph in all four SHELL1 fonts (223 glyphs from 32) except the
# zero-width glyphs of FONT2 and BLACK: VSHELL's blitter crashes on them.
GLYPHS = bytes(c for c in range(32, 255) if c not in (249, 250, 251, 253, 254))


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


def record(kind, font, rect, text, align=0, colour=41, flags=0, background=16, charset=0, prefix=0,
           x=0, y=-1, width=0, seed=0):
    return struct.pack('<8B7iIH', kind, font, align, colour, flags, background, charset, prefix,
                       *rect, x, y, width, seed & 0xffffffff, len(text)) + text


def kinds(data):
    """Kinds of the records of a text.in (to split the emitted stream)."""
    out, at = [], 4
    while at < len(data):
        out.append(data[at])
        n, = struct.unpack_from('<H', data, at + 40)
        at += 42 + n
    return out


def split(emitted, kinds_):
    out, at = [], 0
    for kind in kinds_:
        size = 32 + (98 if kind >= 2 else 0)
        out.append(emitted[at:at + size])
        at += size
    return out if at == len(emitted) else None


class Test:
    def __init__(self, argv):
        self.tool, self.worker, oracle_py = [Path(a).resolve() for a in argv]
        self.oracle = load(oracle_py, 'text_oracle')
        self.scratch = tempfile.TemporaryDirectory(prefix='es2text-', dir=os.environ.get('TEST_TMPDIR'))
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
        self.check(run.returncode == 0, f'text_tool {args[0]} exited {run.returncode}')
        return run.stdout

    def compare(self, label, text_in):
        self.runs += 1
        path = self.tmp / f'{label}.in'
        path.write_bytes(text_in)
        self.tool_run('run', self.cd, path, self.tmp / f'{label}.ours')
        ours = (self.tmp / f'{label}.ours').read_bytes() if (self.tmp / f'{label}.ours').is_file() else b''
        got = self.oracle.run(self.worker, self.cd, {'sav' + B + 'gamefile.str': table(), 'text.in': text_in},
                              ['text-draw'], self.tmp / f'run-{self.runs}', timeout=900)
        theirs = got['disk'].get('text.emit', b'')
        ks = kinds(text_in)
        a, b = split(ours, ks), split(theirs, ks)
        if not self.check(a is not None and b is not None, f'{label}: {len(ours)}/{len(theirs)} emitted bytes'):
            return dict(cases=len(ks), equal=0)
        equal = 0
        for k, (x, y) in enumerate(zip(a, b)):
            same = x == y
            equal += same
            if not same and len(self.failures) < 40:
                self.check(False, f'{label} case {k} kind {ks[k]}: canvas {"equal" if x[:32] == y[:32] else "differs"}'
                                  f'{"" if x[32:] == y[32:] else ", edit state differs"}')
            elif not same:
                self.failures.append(f'{label} case {k}')
        return dict(cases=len(ks), equal=equal, hash=hashlib.sha256(theirs).hexdigest()[:16])

    def estext(self):
        for lang, _ in LANGUAGES:
            path = self.tmp / f'estext-{lang}.in'
            counts = self.tool_run('estext', self.cd, lang, path).strip()
            print(counts, flush=True)
            if not path.is_file():
                continue
            result = self.compare(f'estext-{lang}', path.read_bytes())
            result['counts'] = counts
            self.report[f'ESTEXT {lang}'] = result

    def sweep(self):
        rng = random.Random(2727)
        cw, ch = 360, 40
        recs = []

        def rect(max_w=340, max_h=30):
            w, h = rng.randrange(0, max_w + 1), rng.randrange(0, max_h + 1)
            l, t = rng.randrange(0, cw - w), rng.randrange(0, ch - h)
            return (l, t, l + w, t + h)

        def text(n, pool=GLYPHS):
            return bytes(rng.choice(pool) for _ in range(n))

        for i in range(SWEEP):
            font = rng.randrange(4)
            seed = rng.randrange(2 ** 32)
            kind = rng.choice((0, 1, 1, 2, 3, 3))
            if kind == 0:
                r = rect()
                w = r[2] - r[0]
                recs.append(record(0, font, r, text(rng.randrange(0, 41)), align=rng.randrange(4),
                                   x=rng.randrange(-20, w + 5), y=rng.randrange(-5, r[3] - r[1] + 16),
                                   width=rng.choice((w, w + 1, 0, rng.randrange(-50, 400))), seed=seed))
            elif kind == 1:
                r = rect()
                h = r[3] - r[1]
                y = -1 if rng.random() < 0.5 else rng.randrange(-5, h + 16)
                recs.append(record(1, font, r, text(rng.choice((0, rng.randrange(1, 100), 99))),
                                   align=rng.randrange(3), colour=rng.choice((41, 26, 39, 23, 40, rng.randrange(256))),
                                   flags=rng.randrange(2), background=rng.choice((16, rng.randrange(256))), y=y,
                                   seed=seed))
            elif kind == 2:
                r = rect()
                recs.append(record(2, font, r, text(rng.randrange(0, 90)), colour=rng.choice((41, 39, rng.randrange(256))),
                                   flags=rng.randrange(8) & 6, seed=seed))
            else:
                charset = rng.randrange(4)
                special = rng.random() < 0.35
                if special:   # a Saved games row or the registration field, typed past its limit
                    font = 0
                    which = rng.randrange(3)
                    width = (325, 327, 206)[which]
                    l = rng.randrange(0, cw - width)
                    r = (l, 10, l + width, 22 if which < 2 else 26)
                    charset = 2 if which < 2 else 3
                    prefix = f'{rng.randrange(1, 11):2d}. '.encode() if which < 2 else b''
                else:
                    r = rect()
                    prefix = text(rng.randrange(0, 5))
                pool = SETS[charset] if charset else GLYPHS
                keys = bytearray()
                for _ in range(rng.randrange(0, 120)):
                    p = rng.random()
                    if p < 0.1:
                        keys.append(8)
                    elif p < 0.85 or charset == 0:
                        keys.append(rng.choice(pool))
                    else:
                        keys.append(rng.randrange(1, 256))
                recs.append(record(3, font, r, prefix + bytes(keys), colour=rng.choice((41, 39, rng.randrange(256))),
                                   flags=rng.randrange(8) & 6, charset=charset, prefix=len(prefix), seed=seed))
        self.report['sweep'] = self.compare('sweep', struct.pack('<HH', cw, ch) + b''.join(recs))

    def main(self):
        started = time.time()
        self.estext()
        self.sweep()
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
