#!/usr/bin/env python3
"""A scripted joystick for the input journal (proxy/input_journal.h).

DBSIM's joystick init `0x477674` asks `joyGetDevCapsA` for devices 0 and 1
with a size of 808 bytes (`0x4776ba`) and, for a device that answers 0, polls
it once with `joyGetPosEx` (`0x4776e4`); the per-frame poll `0x477720` (from
the device poll `0x45baa8`) calls `joyGetPosEx` for devices 0 and 1
(`0x477754`). It reads X and Y, Z and R (device 0's when its caps word
`wCaps` has JOYCAPS_HASZ/HASR, else device 1's X/Y), the buttons, and the
point of view when `wCaps` has JOYCAPS_HASPOV (0, 9000, 18000, 27000 map to
up, right, down, left). This tool writes the journal entries of a joystick on
device 0 (device 1 stays absent: its caps answer 165 as on a machine without
devices and its polls fall back, deterministically) from a piecewise program:

  python -B joystick_script.py --program PROGRAM.json --output SCRIPT.jsonl

PROGRAM.json: {"name", "note", "frames": N, "segments": [{"from": f0, "to": f1,
"x"|"y"|"z"|"r": value or [start, end] (linear, 0..65535, centre 32768),
"pov": 0/9000/18000/27000/65535, "buttons": bit mask}, ...], "keys": [script
lines to merge, optional]}. Later segments override earlier ones for the axes
they name. Frame 0 gets two device-0 entries (the init poll during the load and
the first frame's poll); every later frame one. Standard library only; LF.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

CAPS_SIZE = 808          # the size DBSIM passes (0x4776a3 push 0x328)
JOYCAPS_HASZ, JOYCAPS_HASR, JOYCAPS_HASPOV = 0x01, 0x02, 0x10
JOY_FLAGS = 0xccf        # the dwFlags DBSIM sets before each call (0x4776c7)
CENTRE, POV_CENTRED = 32768, 0xffff


def caps_blob(buttons=8):
    """JOYCAPSA (404 bytes) followed by zeros up to the 808 bytes DBSIM asks for."""
    name = b'ES2 scripted joystick'.ljust(32, b'\0')
    caps = struct.pack('<HH32s', 0x7a, 0x1, name)
    caps += struct.pack('<6I', 0, 65535, 0, 65535, 0, 65535)            # X, Y, Z ranges
    caps += struct.pack('<3I', buttons, 10, 1000)                       # buttons, period min/max
    caps += struct.pack('<6I', 0, 65535, 0, 0, 0, 0)                    # R, U, V ranges
    caps += struct.pack('<4I', JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASPOV, 4, 4, buttons)
    caps += b'\0' * 32 + b'\0' * 260                                    # szRegKey, szOEMVxD
    assert len(caps) == 404
    return caps + b'\0' * (CAPS_SIZE - len(caps))


def pos_blob(x, y, z, r, buttons, pov):
    return struct.pack('<13I', 52, JOY_FLAGS, x, y, z, r, 0, 0, buttons, bin(buttons).count('1'), pov, 0, 0)


def state_at(program, f):
    s = dict(x=CENTRE, y=CENTRE, z=CENTRE, r=CENTRE, pov=POV_CENTRED, buttons=0)
    for seg in program['segments']:
        if not seg['from'] <= f < seg['to']:
            continue
        span = max(1, seg['to'] - 1 - seg['from'])
        for axis in ('x', 'y', 'z', 'r'):
            if axis in seg:
                v = seg[axis]
                s[axis] = round(v[0] + (v[1] - v[0]) * (f - seg['from']) / span) if isinstance(v, list) else v
        for key in ('pov', 'buttons'):
            if key in seg:
                s[key] = seg[key]
    for axis in ('x', 'y', 'z', 'r'):
        if not 0 <= s[axis] <= 65535:
            raise ValueError(f'axis {axis} out of range at frame {f}: {s[axis]}')
    return s


def entries(program):
    lines = [dict(f=0, api='joycaps', a=0, b=0, c=CAPS_SIZE, d=0, hex=caps_blob().hex()),
             dict(f=0, api='joycaps', a=1, b=165, c=CAPS_SIZE, d=0)]
    for f in range(program['frames']):
        s = state_at(program, f)
        line = dict(f=f, api='joypos', a=0, b=0, c=0, d=0, hex=pos_blob(s['x'], s['y'], s['z'], s['r'], s['buttons'], s['pov']).hex())
        lines += [line, dict(line)] if f == 0 else [line]
    return lines


def write(program, output):
    head = dict(event='script', name=program['name'], note=program['note'])
    rows = entries(program)
    keys = [json.loads(k) if isinstance(k, str) else k for k in program.get('keys', [])]
    # Stable merge by frame: within a frame the joystick lines (no pump key) come before pump-keyed key lines.
    merged = sorted(rows + keys, key=lambda e: (e['f'], 'p' in e, e.get('p', 0)))
    text = '\n'.join(json.dumps(e, separators=(',', ':')) for e in [head] + merged) + '\n'
    Path(output).write_bytes(text.encode('utf-8'))
    return len(merged)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--program', required=True)
    p.add_argument('--output', required=True)
    a = p.parse_args(argv)
    try:
        program = json.loads(Path(a.program).read_text(encoding='utf-8'))
        n = write(program, a.output)
    except (ValueError, KeyError, OSError) as error:
        print(f'joystick_script: {error}', file=sys.stderr)
        return 2
    print(f'wrote {n} entries to {a.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
