"""dbsim_data.py on an own synthetic PE32 and listing; the committed runs are current (real data)."""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parents[2]
sys.path.insert(0, str(SCRIPTS))
import dbsim_data as dd  # noqa: E402

OUT = REPO / 'sim' / 'data' / 'dbsim_data.c'
MAP = REPO / 'sim' / 'tests' / 'image_data_map.h'
BASE = 0x10000
# name, rva, virtual size, raw offset, raw size, flags
SECTIONS = [
    ('CODE', 0x1000, 0x1000, 0x400, 0x400, 0x60000020),
    ('DATA', 0x2000, 0x1000, 0x800, 0x200, 0xc0000040),
    ('.reloc', 0x3000, 0x1000, 0xa00, 0x200, 0x42000040),
]
FILE_SIZE = 0xc00
TABLE = BASE + 0x1040          # 4 index bytes, jump table at +4
DWORD = BASE + 0x1100          # a CODE dword named by a relocated operand at 0x11012
CLASS = BASE + 0x1200          # a class descriptor "Foo", count 5 at +0x20
PTRTYPE = BASE + 0x1280        # a pointer-type descriptor "int *"
FUNC, CALLED, LABEL, ODD = BASE + 0x1300, BASE + 0x1320, BASE + 0x1340, BASE + 0x1361
D = BASE + 0x2000              # the initialised DATA
LISTING = '\n'.join([
    '   11010: 83 3d 00 11 01 00 00 \tcmp\tdword ptr [0x11100], 0x0',
    '   11050: e8 cb 02 00 00        \tcall\t0x11320 <x>',
    '   11300: 55                    \tpush\tebp',
    '   11320: 55                    \tpush\tebp',
    '   11340: c3                    \tret',
    '   11360: 00 00                 \tadd\tbyte ptr [eax], al',
]) + '\n'


class Builder:
    def __init__(self):
        self.f = bytearray(FILE_SIZE)

    def put(self, at, fmt, *values):
        struct.pack_into(fmt, self.f, at, *values)

    def at_va(self, va):
        rva = va - BASE
        for _, s_rva, _, raw, size, _ in SECTIONS:
            if s_rva <= rva < s_rva + size:
                return raw + rva - s_rva
        raise ValueError(hex(va))

    def vput(self, va, fmt, *values):
        self.put(self.at_va(va), fmt, *values)

    def vbytes(self, va, data):
        at = self.at_va(va)
        self.f[at:at + len(data)] = data


def synthetic(extra_reloc=(), table_mov=0x8a):
    b = Builder()
    b.f[0:2] = b'MZ'
    b.put(0x3c, '<I', 0x80)
    b.f[0x80:0x84] = b'PE\0\0'
    b.put(0x84, '<HH', 0x14c, len(SECTIONS))
    b.put(0x94, '<H', 0xe0)
    o = 0x98
    b.put(o, '<H', 0x10b)
    b.put(o + 16, '<I', 0x1000)
    b.put(o + 28, '<I', BASE)
    b.put(o + 56, '<I', 0x4000)
    b.put(o + 60, '<I', 0x400)
    b.put(o + 92, '<I', 16)
    b.put(o + 136, '<II', 0x3000, 0x200)
    for i, (name, rva, vsize, raw, size, flags) in enumerate(SECTIONS):
        s = o + 0xe0 + 40 * i
        b.f[s:s + 8] = name.encode().ljust(8, b'\0')
        b.put(s + 8, '<IIII', vsize, rva, size, raw)
        b.put(s + 36, '<I', flags)
    # CODE: the operand naming DWORD, the table with its cmp/ja/mov/jmp, the descriptors, the targets.
    b.vbytes(BASE + 0x1010, b'\x83\x3d' + struct.pack('<I', DWORD) + b'\x00')
    b.vbytes(TABLE - 22, b'\x83\xf8\x03\x0f\x87\x00\x00\x00\x00' + bytes([table_mov, 0x80])
             + struct.pack('<I', TABLE) + b'\xff\x24\x85' + struct.pack('<I', TABLE + 4))
    b.vbytes(TABLE, b'\x00\x01\x02\x01')
    b.vput(TABLE + 4, '<IIII', FUNC, FUNC, FUNC, FUNC)
    b.vput(DWORD, '<I', 0x12345678)
    b.vput(CLASS, '<IHHII', 0x10, 3, 0x30, 0, 0x77)
    b.vput(CLASS + 0x20, '<II', 5, 5)
    b.vbytes(CLASS + 0x30, b'Foo\0')
    b.vput(PTRTYPE, '<IHH', 4, 0x10, 0xc)
    b.vbytes(PTRTYPE + 0xc, b'int *\0')
    b.vbytes(FUNC, b'\x55\x8b\xec\xc3')
    b.vbytes(CALLED, b'\x53\xc3')
    b.vbytes(LABEL, b'\xc3')
    b.vbytes(ODD - 1, b'\x00\x83\x44\x24\x04\xe0')
    # DATA: a vtable (descriptor, 0, 0, two methods), slots of every kind, strings, bytes.
    b.vput(D, '<IIIII', CLASS, 0, 0, FUNC, CALLED)
    b.vput(D + 0x20, '<IIIII', D + 0x100, D + 0x800, PTRTYPE, LABEL, ODD)
    b.vbytes(D + 0x40, b'Hi "x\\y?\n\0ab\0')
    b.vbytes(D + 0x50, b'\x01\x02\x03' + bytes(20) + b'\x09\x00\x00\x07')
    b.vbytes(D + 0x80, b'\xfe\xff\x00\x01')
    b.vbytes(D + 0x1fc, b'\x01\x02\x03\x04')
    # .reloc: one block for CODE, one for DATA (with a padding entry).
    code_entries = [0x3000 | 0x012]
    data_entries = [0x3000 | x for x in (0x00, 0x0c, 0x10, 0x20, 0x24, 0x28, 0x2c, 0x30)] + list(extra_reloc)
    if len(data_entries) % 2:
        data_entries.append(0)
    at = 0x3000
    for page, entries in ((0x1000, code_entries + [0]), (0x2000, data_entries)):
        b.vput(BASE + at, '<II', page, 8 + 2 * len(entries))
        for k, e in enumerate(entries):
            b.vput(BASE + at + 8 + 2 * k, '<H', e)
        at += 8 + 2 * len(entries)
    return bytes(b.f)


def gen(data=None, **kw):
    return dd.generate(data or synthetic(), LISTING, source='synthetic', code_tables=(TABLE,), code_dwords=(DWORD,),
                       **kw)


class SyntheticTest(unittest.TestCase):
    def setUp(self):
        with mock.patch.dict(dd.DOC_TABLES, {D + 0x52: 'own_process'}, clear=True), \
                mock.patch.dict(dd.NAMES, {}, clear=True):
            self.text, self.report = gen()

    def test_pointer_slots_and_kinds(self):
        t = self.text
        self.assertIn('static const uint32_t p_012000[1] = {', t)
        self.assertIn('    0x00011200u,  /* 0x00012000 code rtti */', t)
        self.assertIn('static const uint32_t p_01200c[2] = {', t)
        self.assertIn('    0x00011300u,  /* 0x0001200c code function */', t)
        self.assertIn('    0x00011320u,  /* 0x00012010 code function */', t)
        self.assertIn('    0x00012100u,  /* 0x00012020 data */', t)
        self.assertIn('    0x00012800u,  /* 0x00012024 bss */', t)
        self.assertIn('    0x00011280u,  /* 0x00012028 code rtti */', t)
        self.assertIn('    0x00011340u,  /* 0x0001202c code label */', t)
        self.assertIn('static const uint32_t d_012030[1] = {', t)
        self.assertIn('0x00011361u,  /* 0x00012030 unexplained (code after a padding byte', t)
        c = self.report['counts']
        self.assertEqual((c['slots'], c['code'], c['data'], c['bss'], c['unexplained']), (8, 5, 1, 1, 1))
        self.assertEqual(self.report['unexplained'], ['0x00012030 -> 0x00011361'])

    def test_strings_bytes_and_zeros(self):
        t = self.text
        self.assertIn('static const char s_012040[10] = "Hi \\042x\\134y\\077\\012";', t)
        self.assertNotIn('s_01204a', t)                       # "ab" is too short: bytes
        # "ab", a short zero gap kept inside, then 1 2; split at the named VA 0x12052.
        self.assertIn('    0x61, 0x62, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,  /* 0x0001204a */', t)
        self.assertIn('static const uint8_t own_process_b_012052[1] = {', t)
        self.assertIn('/* 0x00012052: 1 byte (own_process.md). */', t)
        self.assertIn('static const uint8_t b_012067[4] = {', t)  # after a gap of 20 zeros, inner zeros kept
        self.assertIn('    0x09, 0x00, 0x00, 0x07,  /* 0x00012067 */', t)
        self.assertIn('    0xfe, 0xff, 0x00, 0x01,  /* 0x00012080 */', t)
        self.assertIn('static const uint8_t b_0121fc[4] = {', t)
        c = self.report['counts']
        self.assertEqual(c['init_bytes'], 0x200)
        emitted = 4 * c['slots'] + c['string_bytes'] + c['byte_bytes']
        self.assertEqual(c['zero_skipped'], 0x200 - emitted)

    def test_code_runs_and_table(self):
        t = self.text
        self.assertIn('/* 0x00011040: CODE, 4 bytes, index-byte table. */', t)
        self.assertIn('    0x00, 0x01, 0x02, 0x01,  /* 0x00011040 */', t)
        self.assertIn('/* 0x00011100: CODE, 4 bytes, dword read as data. */', t)
        self.assertIn('static const uint32_t c_011220[1] = { 0x00000005u };', t)
        self.assertIn('type descriptor 0x00011200 "Foo"', t)
        self.assertEqual(self.report['descriptors'], ['0x00011200 Foo'])
        rows = [line for line in t.splitlines() if line.startswith('    { 0x')]
        runs = [r for r in rows if 'ES2_SIM_DATA' in r]
        vas = [int(r.split()[1].rstrip('u,'), 16) for r in runs]
        self.assertEqual(vas, sorted(vas))
        self.assertIn('    { 0x00011040u, 4u, ES2_SIM_DATA_CODE_TABLE, c_011040 },', t)
        self.assertIn('    { 0x00011220u, 4u, ES2_SIM_DATA_RTTI_COUNT, c_011220 },', t)
        self.assertIn('    { 0x00012030u, 4u, ES2_SIM_DATA_DWORDS, d_012030 },', t)
        self.assertIn('    { 0x00012040u, 10u, ES2_SIM_DATA_STRING, s_012040 },', t)
        self.assertIn('const es2_sim_data_span es2_sim_dbsim_data_spans[] = {\n    { 0x00012000u, 512u },', t)
        self.assertIn('es2_sim_dbsim_data_span_count = 4u;', t)

    def test_refusals(self):
        with self.assertRaisesRegex(dd.FormatError, 'no mov'):
            gen(synthetic(table_mov=0x8b))
        with self.assertRaisesRegex(dd.FormatError, 'overlap'):
            gen(synthetic(extra_reloc=[0x3000 | 0x02]))
        with self.assertRaisesRegex(dd.FormatError, 'relocation type 1'):
            gen(synthetic(extra_reloc=[0x1000 | 0x40]))
        with self.assertRaisesRegex(dd.FormatError, 'no relocated operand'):
            dd.generate(synthetic(), LISTING, code_tables=(TABLE,), code_dwords=(BASE + 0x1104,))

    def test_c_literal(self):
        self.assertEqual(dd.c_literal(b''), ['""'])
        self.assertEqual(dd.c_literal(b'a\tb\x7f\x80'), ['"a\\011b\\177\\200"'])
        self.assertEqual(dd.c_literal(b'x' * 80), ['"%s"' % ('x' * 72), '"%s"' % ('x' * 8)])

    def test_map_header(self):
        summary = {'format': 'f/1', 'totals': {'suites': 2},
                   'pages': [{'area': 'CODE', 'read': [['0x00410100', '0x00410104']], 'written': []},
                             {'area': 'DATA', 'read': [['0x004a7bf0', '0x004a7c10']],
                              'written': [['0x004a7c00', '0x004a8000']]},
                             {'area': 'headers', 'read': [['0x00400000', '0x00400004']], 'written': []}],
                   'code_reads': [{'va': '0x00427000'}, {'va': '0x00408310'}]}
        h = dd.map_header(summary)
        self.assertIn('    { 0x00410100u, 0x00410104u },  /* CODE */', h)
        self.assertIn('    { 0x004a7bf0u, 0x004a7c00u },  /* DATA */', h)   # clipped to the file part
        self.assertNotIn('0x00400000u', h)
        self.assertNotIn('0x004a8000u', h)
        self.assertIn('    0x00408310u,\n    0x00427000u,', h)

    def test_cli_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'DBSIM.EXE'
            exe.write_bytes(synthetic())
            listing = Path(tmp) / 'l.txt'
            listing.write_text(LISTING)
            out = Path(tmp) / 'data.c'
            self.assertEqual(dd.main(['--exe', str(exe), '--listing', str(listing), '--out', str(out)]), 1)
            self.assertFalse(out.exists())


class CommittedTest(unittest.TestCase):
    def test_shape(self):
        t = OUT.read_text(encoding='ascii')
        self.assertIn('SHA-256 %s.' % dd.dt.EXPECTED_SHA256, t)
        self.assertNotIn('\r', t)
        self.assertIn('const es2_sim_data_run es2_sim_dbsim_data_runs[] = {', t)
        self.assertIn('    { 0x00497000u, 68608u },', t)
        self.assertLess(len(t), 4 * 1024 * 1024)
        m = MAP.read_text(encoding='ascii')
        self.assertIn('static const uint32_t image_data_map[][2] = {', m)
        self.assertNotIn('\r', m)


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_committed_runs_are_current(self):
        exe = dd.dt.find_exe()
        if exe is None:
            self.skipTest('no VER95/DBSIM.EXE below ES2_DATA_ROOT')
        if not dd.DEFAULT_OBJDUMP.exists():
            self.skipTest('no pinned llvm-objdump')
        text, report = dd.generate(dd.dt.read_reference(exe), dd.run_objdump(dd.DEFAULT_OBJDUMP, exe))
        self.assertEqual(text, OUT.read_text(encoding='ascii'),
                         'regenerate sim/data/dbsim_data.c with scripts/dbsim_data.py')
        self.assertEqual(report['counts']['descriptors'], 196)
        json.dumps(report)


if __name__ == '__main__':
    unittest.main()
