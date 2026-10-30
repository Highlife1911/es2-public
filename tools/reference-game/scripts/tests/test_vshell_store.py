"""vshell_store.py on an own synthetic PE; the committed store's shape.

The synthetic image has a DATA section with two tables, a pointer table whose
slots point at strings, two literals, and a .rsrc section with a group icon
"ES2ICON" naming one RT_ICON image. The generator's spans and pointer tables
are replaced by synthetic ones; the cases check the merge, the provenance,
the pointer windows, the icon and --check. The committed-file cases need no
data: every measured read of shell/data/vshell_reads.txt lies in a committed
run. The real-data case regenerates the store from the reference VSHELL.EXE
and requires the committed file to be byte-identical (skipped without
ES2_DATA_ROOT).
"""
import json
import os
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parents[2]
sys.path.insert(0, str(SCRIPTS))
import vshell_store as vs  # noqa: E402

STORE = REPO / 'shell' / 'data' / 'vshell_store_data.c'
READS = REPO / 'shell' / 'data' / 'vshell_reads.txt'


def rsrc(base_rva):
    """Own .rsrc: type 14 / name ES2ICON / lang 0 -> group; type 3 / id 7 / lang 0 -> image."""
    image = bytes([40]) + bytes(39) + b'\x11' * 24       # a header-like start and some bits
    group = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHIH', 16, 16, 0, 0, 1, 4, len(image), 7)
    out = bytearray(0x200)

    def directory(at, entries):  # entries: [(name or id, target)]
        named = [e for e in entries if isinstance(e[0], int) and e[0] & 0x80000000]
        struct.pack_into('<IIHHHH', out, at, 0, 0, 0, 0, len(named), len(entries) - len(named))
        for i, (name, target) in enumerate(entries):
            struct.pack_into('<II', out, at + 16 + 8 * i, name, target)

    name_at = 0x180
    struct.pack_into('<H', out, name_at, 7)
    out[name_at + 2:name_at + 16] = 'ES2ICON'.encode('utf-16-le')
    # root: types 3 and 14 (ids sorted)
    directory(0x00, [(3, 0x80000000 | 0x20), (14, 0x80000000 | 0x38)])
    directory(0x20, [(7, 0x80000000 | 0x50)])                     # RT_ICON id 7
    directory(0x38, [(0x80000000 | name_at, 0x80000000 | 0x68)])  # RT_GROUP_ICON "ES2ICON"
    directory(0x50, [(0x409, 0x80)])
    directory(0x68, [(0x409, 0x90)])
    struct.pack_into('<IIII', out, 0x80, base_rva + 0xa0, len(image), 0, 0)
    struct.pack_into('<IIII', out, 0x90, base_rva + 0x100, len(group), 0, 0)
    out[0xa0:0xa0 + len(image)] = image
    out[0x100:0x100 + len(group)] = group
    return bytes(out), image, group


def synthetic_pe(data, rsrc_rva=0x3000):
    """Own minimal PE32: DATA at 0x402000, .rsrc at 0x403000 (image base 0x400000)."""
    resources, image, group = rsrc(rsrc_rva)
    header = bytearray(0x400)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, 0x40)
    header[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x44, 0x14c, 2, 0, 0, 0, 0xe0, 0x102)
    struct.pack_into('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', header, 0x58, 0x10b, 1, 0, 0, 0x200, 0,
                     0x1000, 0x1000, 0x2000, 0x400000, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0, 0x4000, 0x400, 0,
                     2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into('<II', header, 0x58 + 96 + 8 * 2, rsrc_rva, len(resources))
    body, pointer = b'', 0x400
    for i, (name, rva, blob) in enumerate(((b'DATA', 0x2000, data), (b'.rsrc', rsrc_rva, resources))):
        size = (len(blob) + 0x1ff) & ~0x1ff
        struct.pack_into('<8sIIIIIIHHI', header, 0x138 + 40 * i, name, len(blob), rva, size, pointer,
                         0, 0, 0, 0, 0xc0000040)
        body += blob.ljust(size, b'\0')
        pointer += size
    return bytes(header) + body, image, group


def synthetic_data():
    data = bytearray(0x400)
    data[0x10:0x18] = bytes(range(1, 9))                      # table A 0x402010, 8 bytes
    data[0x18:0x1c] = b'\xaa\xbb\xcc\xdd'                     # table B 0x402018, touches A
    data[0x40:0x48] = struct.pack('<II', 0x402100, 0x4023f8)  # pointer table 0x402040
    data[0x100:0x106] = b'alpha\0'
    data[0x3f8:0x3fd] = b'omeg\0'                             # 8 bytes before the section's end
    data[0x200:0x204] = b'abc\0'                              # literal 0x402200
    data[0x300:0x302] = b' \0'                                # literal 0x402300
    return bytes(data)


LAYOUT = {'screens': [{'controls': [{'text': {'exe_string': '0x402200', 'length': 3}},
                                    {'args': [{'exe_string': '0x402300', 'length': 1}]},
                                    {'text': {'exe_string': '0x402200', 'length': 3}}]}]}


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.spans, self.pointers = vs.SPANS, vs.POINTERS
        vs.SPANS = [('t.a', 0x402010, 8), ('t.b', 0x402018, 4)]
        vs.POINTERS = [('t.names', 0x402040, 2, 16)]
        self.pe, self.icon_image, self.icon_group = synthetic_pe(synthetic_data())

    def tearDown(self):
        vs.SPANS, vs.POINTERS = self.spans, self.pointers

    def runs(self, reads=()):
        image = vs.Image(self.pe)
        return image, vs.merge(vs.intervals(image, LAYOUT, list(reads)))

    def test_merge_and_provenance(self):
        _, runs = self.runs([(0x402380, 4, 'file')])
        self.assertEqual(runs, [
            (0x402010, 12, 't.a, t.b'),            # touching spans join
            (0x402040, 8, 't.names.pointers'),
            (0x402100, 16, 't.names'),             # the pointer window
            (0x402200, 4, 'literal'),              # length + NUL, once for two uses
            (0x402300, 2, 'literal'),
            (0x402380, 4, 'measured'),             # a measured read no reader names
            (0x4023f8, 8, 't.names'),              # the window ends at the section's raw end
        ])

    def test_pointer_without_nul_is_refused(self):
        data = bytearray(synthetic_data())
        data[0x100:0x110] = b'x' * 16
        self.pe = synthetic_pe(bytes(data))[0]
        with self.assertRaises(ValueError):
            self.runs()

    def test_span_outside_the_image_is_refused(self):
        vs.SPANS = [('t.far', 0x480000, 4)]
        with self.assertRaises(ValueError):
            self.runs()

    def test_icon(self):
        va, group, images = vs.icon(vs.Image(self.pe))
        self.assertEqual((va, group), (0x403100, self.icon_group))
        self.assertEqual(images, [(7, 0x4030a0, self.icon_image)])

    def test_render_and_check(self):
        text = vs.render(self.pe, LAYOUT, [])
        self.assertIn('const size_t es2_vshell_run_count = 6;', text)
        self.assertIn('{0x402010u, 12u, run_402010, "t.a, t.b"},', text)
        self.assertIn('static const uint8_t run_402010[12] = {\n    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, '
                      '0xaa, 0xbb, 0xcc, 0xdd,\n};', text)
        self.assertIn('const uint8_t es2_vshell_icon_group[20] = {', text)
        self.assertIn('{7, 0x4030a0u, 64u, icon_7},', text)
        with tempfile.TemporaryDirectory() as tmp:
            exe, layout, reads, out = (Path(tmp) / n for n in ('x.exe', 'l.json', 'r.txt', 'o.c'))
            exe.write_bytes(self.pe)
            layout.write_text(json.dumps(LAYOUT), encoding='utf-8')
            reads.write_text('# measured\n0x402380 4 file\n', encoding='utf-8')
            args = ['--exe', str(exe), '--layout', str(layout), '--reads', str(reads), '--out', str(out)]
            self.assertEqual(vs.main(args), 2)                         # not the reference file
            self.assertEqual(vs.main(args + ['--any-hash', '--check']), 1)  # nothing written yet
            self.assertEqual(vs.main(args + ['--any-hash']), 0)
            self.assertIn('{0x402380u, 4u, run_402380, "measured"},', out.read_text(encoding='ascii'))
            self.assertEqual(vs.main(args + ['--any-hash', '--check']), 0)
            out.write_bytes(out.read_bytes().replace(b'0x01, 0x02', b'0x01, 0x03'))
            self.assertEqual(vs.main(args + ['--any-hash', '--check']), 1)

    def test_read_list(self):
        self.assertEqual(vs.read_list('# c\n0x46f72c 16 file  # x\n\n0x479c31 2 literal\n'),
                         [(0x46f72c, 16, 'file'), (0x479c31, 2, 'literal')])


class Committed(unittest.TestCase):
    def runs(self):
        text = STORE.read_text(encoding='ascii')
        return [(int(va, 16), int(size)) for va, size in re.findall(r'\{0x([0-9a-f]+)u, (\d+)u, run_', text)]

    def test_runs_sorted_disjoint(self):
        runs = self.runs()
        self.assertTrue(runs)
        for (a, n), (b, _) in zip(runs, runs[1:]):
            self.assertLess(a + n, b)

    def test_every_measured_read_is_in_a_run(self):
        runs = self.runs()
        reads = vs.read_list(READS.read_text(encoding='utf-8'))
        self.assertTrue(reads)
        for va, size, reader in reads:
            self.assertTrue(any(a <= va and va + size <= a + n for a, n in runs), f'{reader} {va:#x}+{size}')

    def test_static_spans_are_in_a_run(self):
        runs = self.runs()
        for reader, va, size in vs.SPANS:
            self.assertTrue(any(a <= va and va + size <= a + n for a, n in runs), f'{reader} {va:#x}+{size}')


class Real(unittest.TestCase):
    def test_regenerated_store_is_identical(self):
        root = os.environ.get('ES2_DATA_ROOT', '')
        exe = Path(root) / 'VER95' / 'VSHELL.EXE'
        if not root or not exe.exists():
            self.skipTest('ES2_DATA_ROOT not set')
        self.assertEqual(vs.main(['--exe', str(exe), '--check']), 0)


if __name__ == '__main__':
    unittest.main()
