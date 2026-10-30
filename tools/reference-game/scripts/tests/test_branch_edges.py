"""branch_edges.py on an own synthetic PE; the real DBSIM only to check the committed table."""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import branch_edges as edges  # noqa: E402

# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'


def synthetic_pe(code, data=b'', relocations=()):
    """Own minimal PE32: CODE at 0x401000 (entry), DATA at 0x402000, .reloc at 0x403000."""
    base, pages, reloc = 0x400000, {}, b''
    for va in sorted(relocations):
        pages.setdefault((va - base) & ~0xfff, []).append(0x3000 | (va - base) & 0xfff)
    for page, items in sorted(pages.items()):
        items += [0] * (len(items) % 2)
        reloc += struct.pack(f'<II{len(items)}H', page, 8 + 2 * len(items), *items)
    reloc = reloc or struct.pack('<II', 0x1000, 8)  # an empty block: the table must exist
    header = bytearray(0x400)
    header[:2] = b'MZ'; struct.pack_into('<I', header, 0x3c, 0x40); header[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x44, 0x14c, 3, 0, 0, 0, 0xe0, 0x102)
    struct.pack_into('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', header, 0x58, 0x10b, 1, 0, 0x200, 0x400, 0,
                     0x1000, 0x1000, 0x2000, base, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0, 0x4000, 0x400, 0,
                     2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into('<II', header, 0x58 + 96 + 8 * 5, 0x3000, len(reloc))
    body, pointer = b'', 0x400
    for i, (name, rva, flags, blob) in enumerate(((b'CODE', 0x1000, 0x60000020, code),
                                                  (b'DATA', 0x2000, 0xc0000040, data or b'\0' * 4),
                                                  (b'.reloc', 0x3000, 0x42000040, reloc))):
        size = (len(blob) + 0x1ff) & ~0x1ff
        struct.pack_into('<8sIIIIIIHHI', header, 0x138 + 40 * i, name, len(blob), rva, size, pointer, 0, 0, 0, 0, flags)
        body += blob.ljust(size, b'\0'); pointer += size
    return bytes(header) + body


# Own bytes: alpha's fall and taken destinations have no other way in; beta's
# taken target is also the fall-through of 0x401010; the trailing EC is data.
CODE = bytes.fromhex(
    '55'            # 401000 push ebp (entry)
    '8bec'          # 401001 mov ebp,esp (its EC byte is inside the instruction)
    '85c0'          # 401003 test eax,eax
    '7406'          # 401005 je 0x40100d          alpha
    '48'            # 401007 dec eax              alpha fall
    '85c0'          # 401008 test eax,eax
    '7505'          # 40100a jne 0x401011         beta
    'c3'            # 40100c ret                  beta fall
    '40'            # 40100d inc eax              alpha taken (after a ret)
    '9090'          # 40100e nop; nop
    '40'            # 401010 inc eax, falls into 0x401011
    '5d'            # 401011 pop ebp              beta taken: a join
    'c3'            # 401012 ret
    'ec')           # 401013 data byte EC (in al,dx), never reached
SITES = """BRANCH(alpha,0x401005,2,je,0x40100d)
/* BRANCH(gamma,0x401005,2,je,0x40100d) commented out */
BRANCH(beta, 0x40100a, 2, jne, 0x401011)
BRANCH(gamma,0x401003,2,je,0x401010)
"""


@unittest.skipUnless(OBJDUMP.exists(), 'pinned llvm-objdump missing')
class BranchEdgesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sites_path = Path(self.tmp.name) / 'branch_sites.h'
        self.sites_path.write_text(SITES, encoding='utf-8')
        self.sites = edges.branch_sites(self.sites_path)

    def tearDown(self):
        self.tmp.cleanup()

    def rows(self, data, entries=()):
        preds, decoded, image = edges.predecessors(data, OBJDUMP)
        return {row[0]: row[1:] for row in edges.classify_edges(self.sites, preds, decoded, image, entries)}

    def test_exclusive_edges_and_redirect_bytes(self):
        rows = self.rows(synthetic_pe(CODE))
        # alpha: both destinations exclusive; beta: fall exclusive, taken a join;
        # gamma is not a Jcc at its address: no help at all. Two candidate
        # bytes (0x401002 inside mov, 0x401013 data) serve alpha and beta.
        self.assertEqual(rows['alpha'], (0x401005, 1, 1, 0x401002))
        self.assertEqual(rows['beta'], (0x40100a, 1, 0, 0x401013))
        self.assertEqual(rows['gamma'], (0x401003, 0, 0, 0))
        # A hook entry at 0x401000 reserves its prologue window: only one byte left,
        # and a byte serves one site (alpha, the first).
        rows = self.rows(synthetic_pe(CODE), entries=[0x401000])
        self.assertEqual((rows['alpha'][3], rows['beta'][3]), (0x401013, 0))
        # A relocated pointer to alpha's fall destination is another way in.
        rows = self.rows(synthetic_pe(CODE, struct.pack('<I', 0x401007), {0x402000}))
        self.assertEqual(rows['alpha'][1:3], (0, 1))

    def test_render_and_inputs(self):
        text = edges.render([('beta', 0x40100a, 1, 0, 0), ('alpha', 0x401005, 1, 1, 0x401013)])
        lines = [line for line in text.splitlines() if line.startswith('EDGE(')]
        self.assertEqual(lines, ['EDGE(0x00401005,1,1,0x00401013) /* alpha */', 'EDGE(0x0040100a,1,0,0x00000000) /* beta */'])
        self.assertIn('3 of 4 edge destinations exclusive, 1 rel8 redirect bytes', text)
        self.assertEqual([s[0] for s in self.sites], ['alpha', 'beta', 'gamma'])
        registry = Path(self.tmp.name) / 'hook_registry.h'
        registry.write_text('/* name, RVA */\nES2_HOOK(alpha, 0x00001000, d[0], NULL, g)\nES2_HOOK(beta,0x2000,d[1],NULL,g)\n')
        self.assertEqual(edges.hook_entries(registry), [0x401000, 0x402000])
        for bad in ('BRANCH(a,0x401000,3,je,0x401010)\n', 'BRANCH(a,0x401000,2,jx,0x401010)\n',
                    'BRANCH(a,0x401000,2,je,0x401010)\nBRANCH(b,0x401000,2,je,0x401010)\n', '/* none */\n'):
            self.sites_path.write_text(bad, encoding='utf-8')
            with self.assertRaises(ValueError, msg=bad):
                edges.branch_sites(self.sites_path)
        # The matching may move an earlier site to another byte to serve a later one.
        self.assertEqual(edges.matching({0: [10, 11], 1: [10]}), {0: 11, 1: 10})
        self.assertEqual(edges.matching({0: [10], 1: [10], 2: []}), {0: 10})

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
    def test_committed_table_is_current(self):
        root = Path(os.environ['ES2_DATA_ROOT'])
        executable = next(p for p in (root / 'VER95/DBSIM.EXE', root / 'extracted/cd/VER95/DBSIM.EXE') if p.exists())
        tools = SCRIPTS.parent
        generated = edges.generate(executable, tools / 'invitro/branch_sites.h', tools / 'proxy/hook_registry.h', OBJDUMP)
        self.assertEqual(generated, (tools / 'proxy/live_coverage_edges.h').read_text(encoding='ascii'),
                         'regenerate proxy/live_coverage_edges.h with scripts/branch_edges.py')


if __name__ == '__main__':
    unittest.main()
