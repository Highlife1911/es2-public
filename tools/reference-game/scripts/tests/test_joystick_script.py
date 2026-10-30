"""joystick_script.py on a small synthetic program."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import joystick_script as js


class Generator(unittest.TestCase):
    def test_entries(self):
        program = dict(name='t', note='n', frames=4, segments=[
            dict({'from': 1, 'to': 3, 'x': [0, 65535], 'pov': 9000, 'buttons': 5}),
            dict({'from': 2, 'to': 3, 'y': 100})],
            keys=['{"f":2,"p":300,"api":"keydown","vk":13}'])
        out = Path(tempfile.mkdtemp()) / 's.jsonl'
        self.assertEqual(js.write(program, out), 2 + 5 + 1)
        lines = [json.loads(l) for l in out.read_bytes().decode('utf-8').splitlines()]
        self.assertEqual(lines[0], dict(event='script', name='t', note='n'))
        caps = bytes.fromhex(lines[1]['hex'])
        self.assertEqual((lines[1]['api'], lines[1]['a'], lines[1]['b'], lines[1]['c'], len(caps)), ('joycaps', 0, 0, 808, 808))
        self.assertEqual(struct.unpack_from('<I', caps, 60)[0], 8)                 # wNumButtons
        self.assertEqual(struct.unpack_from('<I', caps, 96)[0], 0x13)              # wCaps: Z, R, POV
        self.assertEqual((lines[2]['a'], lines[2]['b'], 'hex' in lines[2]), (1, 165, False))
        pos = [l for l in lines if l.get('api') == 'joypos']
        self.assertEqual([p['f'] for p in pos], [0, 0, 1, 2, 3])                   # frame 0 twice: init and poll
        def fields(p):
            return struct.unpack('<13I', bytes.fromhex(p['hex']))
        self.assertEqual(fields(pos[0])[:4], (52, 0xccf, 32768, 32768))
        self.assertEqual(fields(pos[0])[10], 0xffff)                              # POV centred
        self.assertEqual(fields(pos[2])[2], 0)                                    # x ramp start
        self.assertEqual(fields(pos[3])[2:4], (65535, 100))                       # ramp end, override
        self.assertEqual(fields(pos[3])[8:11], (5, 2, 9000))                      # buttons, count, POV
        self.assertEqual(lines[-1], dict(f=3, api='joypos', a=0, b=0, c=0, d=0, hex=pos[4]['hex']))
        self.assertEqual(lines[-2]['api'], 'keydown')                             # after frame 2's joystick line
        self.assertNotIn(b'\r', out.read_bytes())

    def test_range(self):
        program = dict(name='t', note='n', frames=2, segments=[dict({'from': 0, 'to': 2, 'x': 70000})])
        with self.assertRaises(ValueError):
            js.entries(program)


if __name__ == '__main__':
    unittest.main()
