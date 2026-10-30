"""rtl_imports.py on a synthetic PE32 image; no game data required."""
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rtl_imports as ri

BASE = 0x400000
CODE_RVA, DATA_RVA, IDATA_RVA = 0x1000, 0x2000, 0x3000
SECTION = 0x200


def rel32(site, target):
    return struct.pack('<i', target - (site + 5))


class Synthetic:
    """A three-section PE32: CODE with four functions and four thunks, DATA
    with one function pointer, .idata importing three symbols from
    cw3220.DLL and one from KERNEL32.dll."""

    def __init__(self):
        self.idata = bytearray(SECTION)
        names = {}
        cursor = 0x100
        for symbol in ('_memset', '_errno', '_free', 'Sleep'):
            names[symbol] = cursor
            entry = struct.pack('<H', 0) + symbol.encode() + b'\0'
            self.idata[cursor:cursor + len(entry)] = entry
            cursor += (len(entry) + 1) & ~1
        dlls = {}
        for dll in ('cw3220.DLL', 'KERNEL32.dll'):
            dlls[dll] = cursor
            self.idata[cursor:cursor + len(dll) + 1] = dll.encode() + b'\0'
            cursor += len(dll) + 2
        # lookup tables at 0x60/0x80, IATs at 0x40/0x50
        self.slot = {}
        layout = (('cw3220.DLL', ('_memset', '_errno', '_free'), 0x60, 0x40),
                  ('KERNEL32.dll', ('Sleep',), 0x80, 0x54))
        for n, (dll, symbols, lookup, iat) in enumerate(layout):
            for i, symbol in enumerate(symbols):
                struct.pack_into('<I', self.idata, lookup + 4 * i, IDATA_RVA + names[symbol])
                struct.pack_into('<I', self.idata, iat + 4 * i, IDATA_RVA + names[symbol])
                self.slot[symbol] = BASE + IDATA_RVA + iat + 4 * i
            struct.pack_into('<IIIII', self.idata, 20 * n, IDATA_RVA + lookup, 0, 0,
                             IDATA_RVA + dlls[dll], IDATA_RVA + iat)
        va = BASE + CODE_RVA
        self.thunk = {s: va + 0x100 + 6 * i for i, s in enumerate(('_memset', '_errno', '_free', 'Sleep'))}
        code = bytearray(b'\x90' * SECTION)

        def put(at, data):
            code[at:at + len(data)] = data
        # A 0x401000: frame, call _memset, ret
        put(0x00, b'\x55\x8b\xec' + b'\xe8' + rel32(va + 3, self.thunk['_memset']) + b'\x5d\xc3')
        # B 0x401010: frame, cmp dword [errno thunk], 0, jmp _free (tail call)
        put(0x10, b'\x55\x8b\xec' + b'\x83\x3d' + struct.pack('<I', self.thunk['_errno']) + b'\x00'
            + b'\x5d' + b'\xe9' + rel32(va + 0x1b, self.thunk['_free']))
        # C 0x401030: frameless (push ebx), only referenced from DATA; call _memset
        put(0x30, b'\x53' + b'\xe8' + rel32(va + 0x31, self.thunk['_memset']) + b'\x5b\xc3')
        # D 0x401040: frame, call dword [_free slot], call Sleep thunk
        put(0x40, b'\x55\x8b\xec' + b'\xff\x15' + struct.pack('<I', self.slot['_free'])
            + b'\xe8' + rel32(va + 0x49, self.thunk['Sleep']) + b'\x5d\xc3')
        for symbol, thunk in self.thunk.items():
            put(thunk - va, b'\xff\x25' + struct.pack('<I', self.slot[symbol]))
        data = bytearray(SECTION)
        struct.pack_into('<I', data, 8, va + 0x30)
        self.code, self.data = code, data

    def image(self):
        header = bytearray(0x200)
        header[0:2] = b'MZ'
        struct.pack_into('<I', header, 0x3C, 0x40)
        header[0x40:0x44] = b'PE\0\0'
        struct.pack_into('<HHIIIHH', header, 0x44, 0x14C, 3, 0, 0, 0, 0xE0, 0x10F)
        optional = 0x58
        struct.pack_into('<H', header, optional, 0x10B)
        struct.pack_into('<I', header, optional + 16, CODE_RVA)
        struct.pack_into('<I', header, optional + 28, BASE)
        struct.pack_into('<II', header, optional + 104, IDATA_RVA, 0x28)
        sections = (('CODE', CODE_RVA, 0x60000020, self.code), ('DATA', DATA_RVA, 0xC0000040, self.data),
                    ('.idata', IDATA_RVA, 0xC0000040, self.idata))
        body = b''
        for n, (name, rva, flags, blob) in enumerate(sections):
            s = optional + 0xE0 + 40 * n
            header[s:s + 8] = name.encode().ljust(8, b'\0')
            struct.pack_into('<IIII', header, s + 8, SECTION, rva, SECTION, 0x200 + SECTION * n)
            struct.pack_into('<I', header, s + 36, flags)
            body += bytes(blob)
        return bytes(header) + body


def sites(result, symbol):
    return [(s['site'], s['kind'], s['owner']) for s in result['imports'][symbol]['sites']]


class ScanTest(unittest.TestCase):
    def setUp(self):
        self.pe = Synthetic()
        self.result = ri.scan(ri.Image(self.pe.image()), 'CW3220.dll')

    def test_imports_slots_and_thunks(self):
        imports = self.result['imports']
        self.assertEqual(sorted(imports), ['_errno', '_free', '_memset'])
        for symbol in imports:
            self.assertEqual(imports[symbol]['slot'], self.pe.slot[symbol])
            self.assertEqual(imports[symbol]['thunks'], [self.pe.thunk[symbol]])

    def test_sites_and_owners(self):
        va = BASE + CODE_RVA
        self.assertEqual(sites(self.result, '_memset'), [(va + 3, 'call', va), (va + 0x31, 'call', va + 0x30)])
        self.assertEqual(sites(self.result, '_free'), [(va + 0x1b, 'jmp', va + 0x10), (va + 0x43, 'call [IAT]', va + 0x40)])
        self.assertEqual(sites(self.result, '_errno'), [(va + 0x15, 'imm', va + 0x10)])

    def test_thunks_are_not_entries_and_kernel32_is_ignored(self):
        self.assertFalse(set(self.pe.thunk.values()) & set(self.result['entries']))
        self.assertNotIn('Sleep', self.result['imports'])

    def test_extra_entries_split_owners(self):
        va = BASE + CODE_RVA
        result = ri.scan(ri.Image(self.pe.image()), 'cw3220.dll', {va + 2})
        self.assertEqual(sites(result, '_memset')[0], (va + 3, 'call', va + 2))

    def test_counts(self):
        imports = self.result['imports']
        self.assertEqual([ri.call_count(imports[s]) for s in ('_memset', '_free', '_errno')], [2, 2, 0])
        self.assertEqual(ri.imm_count(imports['_errno']), 1)

    def test_bad_images(self):
        with self.assertRaises(ri.PEError):
            ri.Image(b'XX' + bytes(100))
        broken = bytearray(self.pe.image())
        broken[0x40:0x44] = b'NE\0\0'
        with self.assertRaises(ri.PEError):
            ri.Image(bytes(broken))


DOC_OK = """# Synthetic

| Import | Class | Sites | Note |
| --- | --- | ---: | --- |
| `_memset` | pure | 2 | |
| `_free` | state | 2 | |
| `_errno` | state | imm 1 | data import |

| Import | Sites |
| --- | --- |
| `_memset` | 99 |
"""

DOC_BAD = """| Sites | Import |
| --- | --- |
| 3 | `_memset` |
| imm 1 | `_errno` |
| 1 | `_strlen` |
"""


def run(args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        status = ri.main(args)
    return status, out.getvalue()


class CliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.exe = root / 'synthetic.exe'
        self.exe.write_bytes(Synthetic().image())
        self.ok, self.bad = root / 'ok.md', root / 'bad.md'
        self.ok.write_text(DOC_OK, encoding='utf-8')
        self.bad.write_text(DOC_BAD, encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def test_check_passes_on_first_table(self):
        status, out = run([str(self.exe), '--registry', '', '--check', str(self.ok)])
        self.assertEqual(status, 0, out)
        self.assertIn('3 imports checked, 0 problem(s)', out)

    def test_check_reports_differences(self):
        status, out = run([str(self.exe), '--registry', '', '--check', str(self.bad)])
        self.assertEqual(status, 1)
        self.assertIn('_memset: document 3, binary 2', out)
        self.assertIn('missing in', out)
        self.assertIn('_free', out)
        self.assertIn('not imported: _strlen', out)

    def test_table_output(self):
        status, out = run([str(self.exe), '--registry', ''])
        self.assertEqual(status, 0)
        self.assertIn('| `_memset` |', out)
        self.assertIn('imm 1', out)
        self.assertIn('3 imports from cw3220.dll, 4 call sites', out)

    def test_map_counts_and_hooks_in_site_rows(self):
        root = Path(self.tmp.name)
        mapdir = root / 'map'
        mapdir.mkdir()
        va = BASE + CODE_RVA
        counts = {s: 7 for s in ri.SUITES}
        (mapdir / 'map.json').write_text(
            '{"rows": [{"va": "%#x", "counts": %s}]}' % (va + 0x30, str(counts).replace("'", '"')), encoding='utf-8')
        registry = root / 'hook_registry.h'
        registry.write_text('ES2_HOOK(alpha, 0x1000, d[0], NULL, g)\n', encoding='utf-8')
        status, out = run([str(self.exe), '--map', str(mapdir), '--registry', str(registry), '--sites'])
        self.assertEqual(status, 0)
        self.assertIn(f'{va:#x} (alpha)', out)
        self.assertIn(f'{va + 0x30:#x} [7/7/7/7/7/7]', out)


if __name__ == '__main__':
    unittest.main()
