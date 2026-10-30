"""Synthetic results transport/model tests plus an opt-in tiny real sweep."""
import json
import os
from pathlib import Path
import random
import struct
import sys
import unittest

import results_compare as results


def pilot(bay, active, order):
    return bytes(4) + struct.pack('<h', 2) + b'P\0' + struct.pack('<hBhhhh', bay, active, 0, order, 0, 100) + bytes(14)


def synthetic_sav(member_bay=1):
    out = bytearray()
    for i in range(33):
        out += bytes([1]) + struct.pack('<h', int(i == 3)) + (struct.pack('<5h', 3, 3, 100, 100, 5) if i == 3 else b'')
    out += struct.pack('<h', 5) + b''.join(struct.pack('<hh', i, 0) for i in range(5))
    out += struct.pack('<hh', 1, 0) + bytes(148)
    out += pilot(member_bay, 1, 1) + b''.join(pilot(-1, 0, -1) for _ in range(35))
    out += struct.pack('<6H', 0, 0, 0, 1, 1, 1) + struct.pack('<2h', 2, 2) + pilot(0, 1, 0)
    fixed = struct.pack('<hh33hhhh', 1, 1, *([100] * 33), 100, 0, 2)
    out += struct.pack('<h', 2) + struct.pack('<h', 0) + fixed + struct.pack('<h', 1) + struct.pack('<h5h', 0, 3, 3, 100, 100, 5)
    out += struct.pack('<h', 1) + fixed + struct.pack('<h', 0)
    out += bytes(18) + struct.pack('<I', 100000) + bytes(2000) + bytes(22)
    return bytes(out)


def packet(meta, sav=b'sav', strings=b'', text=b''):
    blocks = [json.dumps(meta).encode(), sav, strings, text]
    return b'ES2R' + b''.join(struct.pack('<I', len(b)) + b for b in blocks)


META = dict(after=[1, 1], final_word=0, lost=0, total=5, debrief_ran=1, debrief_count=1,
            debrief_refs=[0] + [-1] * 29, debrief_aux=-1, output_rng_state=bytes(114).hex())


class Tests(unittest.TestCase):
    def test_model_round_trip_and_consumers(self):
        data = synthetic_sav()
        sav = results.Sav.parse(data)
        self.assertEqual(sav.build(), data)
        self.assertEqual([p.get(0x22) for p in sav.consumers()], [0, 1])
        sav.selected(0).set(0x24, 0)
        self.assertEqual(len(sav.consumers()), 1)
        body = results.results_bytes(results.Sav.parse(data), random.Random(1), 1, 7, [(5, 9)], ('healthy',))
        self.assertEqual(struct.unpack_from('<hih', body), (1, 7, 1))
        self.assertEqual(len(body), 8 + 4 + 2 * (66 + 6) + 160)
        absent = results.results_bytes(results.Sav.parse(synthetic_sav(member_bay=5)), random.Random(1), 0, 0, [], ('lost',))
        self.assertEqual(len(absent), 8 + 66 + 6 + 6 + 160)
        with self.assertRaises(ValueError):
            results.Sav.parse(data[:100])

    def test_packets_and_paths(self):
        cases = {(0x44d0e7, 3): (3, -1), (0x41f93a, 0): (4, -1)}
        for (address, argument), expected in cases.items():
            row, = results.decode(packet(dict(META, ui_calls=[[address, argument, 0, 0]])))
            self.assertEqual((row['ending'], row['advance']), expected)
        for calls, advance in (([[0x40e933, 10, 0, 0]], 1), ([[0x44d0e7, 0, 0, 0]], 0), ([[0x413305, 0, 0, 0]], 2)):
            row, = results.decode(packet(dict(META, ui_calls=[[0x40f904, 0, 0, 0]] + calls)))
            self.assertEqual((row['ending'], row['advance']), (5, advance))
        two = packet(dict(META, ui_calls=[]))
        self.assertEqual(len(results.decode(two * 2)), 2)
        for at in (1, 5, 9, len(two) - 1):
            with self.assertRaises(ValueError):
                results.decode(two[:at])

    def test_compare_detects_every_field(self):
        row, = results.decode(packet(dict(META, ui_calls=[[0x40f904, 0, 0, 0], [0x413305, 0, 0, 0]]), b'S', b'T', b'X'))
        ours = {k: row[k] for k in results.FIELDS}
        self.assertTrue(results.compare(ours, row)['equal'])
        for name in results.FIELDS:
            changed = dict(ours)
            value = changed[name]
            changed[name] = (value + b'!' if isinstance(value, bytes) else [*value[:-1], 7] if isinstance(value, list)
                             else (not value) if name == 'debrief' else value + 1)
            self.assertFalse(results.compare(changed, row)['equal'], name)
        known = results.compare(dict(error='member not found 2'),
                                dict(error='FAULT results path code=c0000005 EIP=00412314 EAX=8'))
        self.assertTrue(known['outside_contract'])
        unknown = results.compare(dict(error='invalid count 0'), dict(error='FAULT results path code=c0000005 EIP=0'))
        self.assertFalse(unknown['outside_contract'])
        self.assertFalse(results.compare(dict(error='member not found 2'), row)['outside_contract'])

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT not set')
    def test_tiny_sweep(self):
        cd = Path(os.environ['ES2_DATA_ROOT'])
        builder = results.Builder(CORE, cd)
        rows = results.run(CORE, WORKER, cd, list(results.cases(builder, 'tiny', 0, random.Random(5))))
        summary = results.summary(rows)
        self.assertEqual((summary['cases'], summary['equal'], summary['mismatches']), (4, 4, 0), rows)
        self.assertIn((5, 2), [tuple(p) for p in summary['paths']])


if __name__ == '__main__':
    CORE, WORKER = Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    results.configure(Path(sys.argv[1]).resolve())
    unittest.main(argv=[sys.argv[0]])
