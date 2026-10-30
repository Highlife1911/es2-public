"""dbsim_image_tables.py on an own synthetic PE32; the committed table is current (real data)."""
import hashlib
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parents[2]
sys.path.insert(0, str(SCRIPTS))
import dbsim_image_tables as dt  # noqa: E402

TABLE = REPO / 'sim' / 'data' / 'dbsim_image_tables.c'
BASE = 0x10000
# name, rva, virtual size, raw offset, raw size, flags
SECTIONS = [
    ('CODE', 0x1000, 0x1000, 0x400, 0x200, 0x60000020),
    ('DATA', 0x2000, 0x3000, 0x600, 0x200, 0xc0000040),
    ('.idata', 0x5000, 0x1000, 0x800, 0x400, 0xc0000040),
    ('.rsrc', 0x6000, 0x1000, 0xc00, 0x400, 0x40000040),
]
FILE_SIZE = 0x1000


class Builder:
    def __init__(self):
        self.f = bytearray(FILE_SIZE)

    def put(self, at, fmt, *values):
        struct.pack_into(fmt, self.f, at, *values)

    def at_rva(self, rva):
        for _, s_rva, _, raw, size, _ in SECTIONS:
            if s_rva <= rva < s_rva + size:
                return raw + rva - s_rva
        raise ValueError(hex(rva))

    def rput(self, rva, fmt, *values):
        self.put(self.at_rva(rva), fmt, *values)

    def rbytes(self, rva, data):
        at = self.at_rva(rva)
        self.f[at:at + len(data)] = data


def synthetic(icon_language=0):
    b = Builder()
    b.f[0:2] = b'MZ'
    b.put(0x3c, '<I', 0x80)
    b.f[0x80:0x84] = b'PE\0\0'
    b.put(0x84, '<HH', 0x14c, len(SECTIONS))
    b.put(0x94, '<H', 0xe0)
    o = 0x98
    b.put(o, '<H', 0x10b)
    b.put(o + 16, '<I', 0x1010)
    b.put(o + 28, '<I', BASE)
    b.put(o + 32, '<I', 0x1000)
    b.put(o + 56, '<I', 0x7000)
    b.put(o + 60, '<I', 0x400)
    b.put(o + 92, '<I', 16)
    b.put(o + 104, '<II', 0x5000, 0x80)
    b.put(o + 112, '<II', 0x6000, 0x200)
    b.put(o + 136, '<II', 0, 0)
    for i, (name, rva, vsize, raw, size, flags) in enumerate(SECTIONS):
        s = o + 0xe0 + 40 * i
        b.f[s:s + 8] = name.encode().ljust(8, b'\0')
        b.put(s + 8, '<IIII', vsize, rva, size, raw)
        b.put(s + 36, '<I', flags)
    # Imports: KERNEL32.dll GetTickCount (hint 3) and ordinal 5; cw3220.DLL _memset (hint 0).
    b.rput(0x5000, '<IIIII', 0x5100, 0, 0, 0x5200, 0x5140)
    b.rput(0x5014, '<IIIII', 0, 0, 0, 0x5210, 0x5150)
    b.rput(0x5100, '<III', 0x5220, 0x80000005, 0)
    b.rput(0x5140, '<III', 0x5220, 0x80000005, 0)
    b.rput(0x5150, '<II', 0x5230, 0)
    b.rbytes(0x5200, b'KERNEL32.dll\0')
    b.rbytes(0x5210, b'cw3220.DLL\0')
    b.rput(0x5220, '<H', 3)
    b.rbytes(0x5222, b'GetTickCount\0')
    b.rput(0x5230, '<H', 0)
    b.rbytes(0x5232, b'_memset\0')
    # Resources: root (types 3, 14 and 6), ICON #7, GROUP_ICON "TESTICON", a STRING to skip.
    r = 0x6000
    b.rput(r + 0x0e, '<H', 3)
    b.rput(r + 0x10, '<IIIIII', 3, 0x80000030, 6, 0x80000050, 14, 0x80000070)
    b.rput(r + 0x3e, '<H', 1)
    b.rput(r + 0x40, '<II', 7, 0x80000090)
    b.rput(r + 0x5e, '<H', 1)
    b.rput(r + 0x60, '<II', 1, 0x800000b0)
    b.rput(r + 0x7c, '<H', 1)
    b.rput(r + 0x80, '<II', 0x80000150, 0x800000d0)
    b.rput(r + 0x9e, '<H', 1)
    b.rput(r + 0xa0, '<II', icon_language, 0x110)
    b.rput(r + 0xbe, '<H', 1)
    b.rput(r + 0xc0, '<II', 0, 0x120)
    b.rput(r + 0xde, '<H', 1)
    b.rput(r + 0xe0, '<II', 0, 0x130)
    b.rput(r + 0x110, '<II', 0x6180, 18)
    b.rput(r + 0x120, '<II', 0x61a0, 4)
    b.rput(r + 0x130, '<II', 0x61b0, 20)
    b.rput(r + 0x150, '<H', 8)
    b.rbytes(r + 0x152, 'TESTICON'.encode('utf-16-le'))
    b.rbytes(0x6180, bytes(range(0x40, 0x52)))
    b.rbytes(0x61a0, b'\xde\xad\xbe\xef')
    b.rbytes(0x61b0, struct.pack('<HHHBBBBHHIH', 0, 1, 1, 32, 32, 16, 0, 1, 4, 18, 7))
    return bytes(b.f)


class SyntheticTest(unittest.TestCase):
    def setUp(self):
        self.data = synthetic()
        self.text = dt.generate(self.data, source='synthetic')

    def test_header_and_layout(self):
        t = self.text
        self.assertIn('SHA-256 %s.' % hashlib.sha256(self.data).hexdigest(), t)
        self.assertIn('.image_base = 0x00010000u, .image_size = 0x00007000u, .headers_size = 0x00000400u, '
                      '.entry_rva = 0x00001010u,', t)
        self.assertIn('.section_count = 4u,', t)
        self.assertIn('{ "CODE", 0x00001000u, 0x00001000u, 0x00000400u, 0x00000200u, 0x60000020u },  /* VA 0x00011000 */', t)
        self.assertIn('{ ".rsrc", 0x00006000u, 0x00001000u, 0x00000c00u, 0x00000400u, 0x40000040u },', t)
        self.assertIn('.import_rva = 0x00005000u, .import_size = 0x00000080u,', t)
        self.assertIn('.reloc_rva = 0x00000000u, .reloc_size = 0x00000000u,', t)
        self.assertIn('.resource_rva = 0x00006000u, .resource_size = 0x00000200u,', t)
        self.assertIn('.code_lo = 0x00011000u, .code_hi = 0x00012000u,', t)

    def test_imports_in_file_order(self):
        rows = [line.strip() for line in self.text.splitlines() if line.startswith('    { "')]
        self.assertEqual(rows, [
            '{ "KERNEL32.dll", 0x00015200u, "GetTickCount", 0x0003u, 0x00015220u, 0u, 0x00015140u },',
            '{ "KERNEL32.dll", 0x00015200u, NULL, 0x0000u, 0x00000000u, 5u, 0x00015144u },',
            '{ "cw3220.DLL", 0x00015210u, "_memset", 0x0000u, 0x00015230u, 0u, 0x00015150u },',
        ])
        self.assertIn('es2_sim_dbsim_import_count = 3u;', self.text)

    def test_resources_icons_only(self):
        t = self.text
        self.assertIn('/* Resource type 3 #7, language 0: 18 bytes at VA 0x00016180. */', t)
        self.assertIn('static const uint8_t resource_0[18] = {', t)
        self.assertIn('    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, '
                      '0x4f,  /* 0x00016180 */', t)
        self.assertIn('    0x50, 0x51,  /* 0x00016190 */', t)
        self.assertIn('/* Resource type 14 "TESTICON", language 0: 20 bytes at VA 0x000161b0. */', t)
        self.assertIn('    { 3u, NULL, 7u, 0u, 0x00016180u, 18u, resource_0 },', t)
        self.assertIn('    { 14u, "TESTICON", 0u, 0u, 0x000161b0u, 20u, resource_1 },', t)
        self.assertIn('es2_sim_dbsim_resource_count = 2u;', t)
        self.assertNotIn('0xde, 0xad', t)            # the STRING resource is not emitted
        self.assertIn('language 1033', dt.generate(synthetic(icon_language=1033)))

    def test_refusals(self):
        bad = bytearray(self.data)
        bad[0] = ord('X')
        with self.assertRaisesRegex(dt.FormatError, 'no MZ header'):
            dt.generate(bytes(bad))
        bad = bytearray(self.data)
        struct.pack_into('<H', bad, 0x84, 0x8664)
        with self.assertRaisesRegex(dt.FormatError, 'not i386'):
            dt.generate(bytes(bad))
        bad = bytearray(self.data)
        struct.pack_into('<I', bad, 0x800 + 0x150, 0x5ff0)        # a symbol RVA without file bytes
        with self.assertRaisesRegex(dt.FormatError, 'no file bytes'):
            dt.generate(bytes(bad))
        bad = bytearray(self.data)
        struct.pack_into('<I', bad, 0xc00 + 0x14, 0x30)            # type 3's target no longer a directory
        with self.assertRaisesRegex(dt.FormatError, 'no directory'):
            dt.generate(bytes(bad))

    def test_cli_gate_and_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'DBSIM.EXE'
            exe.write_bytes(self.data)
            out = Path(tmp) / 'tables.c'
            self.assertEqual(dt.main(['--exe', str(exe), '--out', str(out)]), 1)   # not the reference
            self.assertFalse(out.exists())


class CommittedTableTest(unittest.TestCase):
    def test_shape(self):
        t = TABLE.read_text(encoding='ascii')
        self.assertIn('SHA-256 %s.' % dt.EXPECTED_SHA256, t)
        self.assertNotIn('\r', t)
        self.assertIn('const es2_sim_image_info es2_sim_dbsim_layout = {', t)
        self.assertIn('{ 14u, "ES2ICON", 0u, 0u,', t)


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_committed_table_is_current(self):
        exe = dt.find_exe()
        if exe is None:
            self.skipTest('no VER95/DBSIM.EXE below ES2_DATA_ROOT')
        self.assertEqual(dt.generate(dt.read_reference(exe)), TABLE.read_text(encoding='ascii'),
                         'regenerate sim/data/dbsim_image_tables.c with scripts/dbsim_image_tables.py')


if __name__ == '__main__':
    unittest.main()
