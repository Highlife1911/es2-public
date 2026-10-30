"""probe_instruction_inventory.py on own synthetic PE images; the real DBSIM only to check the committed header.

The synthetic image reproduces the failure modes of one linear sweep (zero
padding that swallows a `push ebp`, a jump table decoded as code, data in
CODE) and checks that the synchronized descent repairs them. The real-data
case regenerates proxy/probe_entry_inventory.h from the reference DBSIM and
requires the committed file to be byte-identical (skipped without
ES2_DATA_ROOT).
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import probe_instruction_inventory as inventory  # noqa: E402

# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'


def synthetic_pe(code, data, relocations):
    """Own minimal PE32: CODE at 0x401000 (entry), DATA at 0x402000, .reloc at 0x403000."""
    base, pages, reloc = 0x400000, {}, b''
    for va in sorted(relocations):
        pages.setdefault((va - base) & ~0xfff, []).append(0x3000 | (va - base) & 0xfff)
    for page, items in sorted(pages.items()):
        items += [0] * (len(items) % 2)
        reloc += struct.pack(f'<II{len(items)}H', page, 8 + 2 * len(items), *items)
    header = bytearray(0x400)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, 0x40)
    header[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x44, 0x14c, 3, 0, 0, 0, 0xe0, 0x102)
    struct.pack_into('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', header, 0x58, 0x10b, 1, 0, 0x200, 0x400, 0,
                     0x1000, 0x1000, 0x2000, base, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0, 0x4000, 0x400, 0,
                     2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into('<II', header, 0x58 + 96 + 8 * 5, 0x3000, len(reloc))
    body, pointer = b'', 0x400
    for i, (name, rva, flags, blob) in enumerate(((b'CODE', 0x1000, 0x60000020, code),
                                                  (b'DATA', 0x2000, 0xc0000040, data),
                                                  (b'.reloc', 0x3000, 0x42000040, reloc))):
        size = (len(blob) + 0x1ff) & ~0x1ff
        struct.pack_into('<8sIIIIIIHHI', header, 0x138 + 40 * i, name, len(blob), rva, size, pointer, 0, 0, 0, 0, flags)
        body += blob.ljust(size, b'\0')
        pointer += size
    return bytes(header) + body


# Own bytes. "linear" names what one linear sweep decodes there.
SYNTHETIC = [
    (0x401000, '55', 'push ebp: entry point'),
    (0x401001, '8bec 8b4508 83f802', 'mov ebp,esp; mov eax,[ebp+8]; cmp eax,2'),
    (0x401009, '7713', 'ja 0x40101e'),
    (0x40100b, 'ff2485 12104000', 'jmp [0x401012+eax*4] (relocated)'),
    (0x401012, '50104000 56104000', 'table: 0x401050, 0x401056; linear: push eax/esi in it'),
    (0x40101a, '81104000', 'table: 0x401081; linear: adc dword [eax],imm32 runs 2 bytes on'),
    (0x40101e, '66b85200', 'mov ax,0x52; linear: push edx at 0x401020'),
    (0x401022, '5d c3 000000', 'pop ebp; ret; zero padding; linear: 00 55 8b swallows 0x401027'),
    (0x401027, '55 8bec 53 8b5d08 8bc3 5b 5d c3', 'called function: push ebp; ...; push ebx; ...; ret'),
    (0x401033, b'SUPERVISOR\0'.hex(), 'data with a DATA pointer to it (not 55 8b ec); linear: seven pushes'),
    (0x40103e, 'cc' * 18, 'int3 filler'),
    (0x401050, '6a01 eb0c cccc', 'case 0: push 1; jmp 0x401060'),
    (0x401056, '6a02 eb06' + 'cc' * 6, 'case 1: push 2; jmp 0x401060'),
    (0x401060, 'e8c2ffffff 83c404 5d c3', 'call 0x401027; add esp,4; pop ebp; ret'),
    (0x40106a, 'cc' * 22 + '00', 'int3 filler, one zero byte; linear: 00 53 5b swallows 0x401081'),
    (0x401081, '53 5b ebdb 000000', 'case 2, known only from the table: push ebx; pop ebx; jmp 0x401060; padding'),
    (0x401088, '55 8bec b8558becc3 5d c3', 'function known only from DATA: push ebp; ...; mov eax,0xc3ec8b55'),
]
RELOCATIONS = {0x40100e, 0x401012, 0x401016, 0x40101a, 0x402000, 0x402004}
DATA = struct.pack('<II', 0x401088, 0x401033)


def synthetic_code():
    code = b''
    for va, text, _ in SYNTHETIC:
        assert 0x401000 + len(code) == va, hex(va)
        code += bytes.fromhex(text.replace(' ', ''))
    return code


@unittest.skipUnless(OBJDUMP.exists(), 'pinned llvm-objdump missing')
class ProbeInventoryTest(unittest.TestCase):
    def test_synchronized_descent_repairs_linear_desync(self):
        code = synthetic_code()
        entries, report, reached = inventory.classify(synthetic_pe(code, DATA, RELOCATIONS), OBJDUMP)
        linear = {0x401000, 0x401012, 0x401016, 0x401020, 0x40102a,
                  0x401033, 0x401034, 0x401035, 0x401037, 0x401038, 0x40103a, 0x40103c}
        self.assertEqual(report['linear_entries'], len(linear), report)
        self.assertEqual(report['added'], ['0x00401027', '0x00401081', '0x00401088'])
        self.assertEqual(report['removed'], {'0x00401012': 'jump table of 0x40100b',
                                             '0x00401016': 'jump table of 0x40100b',
                                             '0x00401020': 'inside instruction 0x40101e'})
        unconfirmed = [0x401033, 0x401034, 0x401035, 0x401037, 0x401038, 0x40103a, 0x40103c]
        self.assertEqual(report['unconfirmed'], [f'{va:#010x}' for va in unconfirmed])
        self.assertEqual(report['roots'], {'entry': 1, 'exports': 0, 'frame_pointers': 1, 'linear_calls': 1})
        self.assertEqual((report['jump_tables'], report['passes']), (1, 2))
        self.assertEqual(entries, sorted({0x401000: 0x55, 0x401027: 0x55, 0x40102a: 0x53, 0x401081: 0x53,
                                          0x401088: 0x55, **{va: code[va - 0x401000] for va in unconfirmed}}.items()))
        self.assertNotIn(0x40108c, reached)
        self.assertEqual((reached[0x40108b], reached[0x40101e]), (5, 4))

    def test_render_format(self):
        entries, _, _ = inventory.classify(synthetic_pe(synthetic_code(), DATA, RELOCATIONS), OBJDUMP)
        text = inventory.render(entries)
        self.assertEqual(text.count('{0x'), len(entries))
        self.assertIn('{0x00401027u,0x55}', text)
        self.assertTrue(text.endswith('};\n#endif\n'))
        self.assertNotIn('\r', text)
        # Six entries per line, comma-and-space separated, the last line closed by a newline.
        lines = text.split('static const probe_instruction probe_instructions[] = {\n')[1].split('};')[0].splitlines()
        counts = [line.count('{') for line in lines if line.strip()]
        self.assertEqual(counts, [6] * (len(entries) // 6) + ([len(entries) % 6] if len(entries) % 6 else []))
        # The output is a function of the entries only.
        self.assertEqual(text, inventory.render(list(entries)))

    def test_root_inside_a_reached_instruction_is_refused(self):
        # A pointer into the 55 8b ec c3 immediate at 0x40108c is an overlap.
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            inventory.classify(synthetic_pe(synthetic_code(), DATA + struct.pack('<I', 0x40108c),
                                            RELOCATIONS | {0x402008}), OBJDUMP)

    def test_hash_gate_and_image_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'image.exe'
            path.write_bytes(synthetic_pe(synthetic_code(), DATA, RELOCATIONS))
            with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                inventory.inventory(path, OBJDUMP)
        with self.assertRaisesRegex(ValueError, 'Not a PE32'):
            inventory.PeImage(b'MZ' + bytes(0x100))

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
    def test_committed_header_is_current(self):
        root = Path(os.environ['ES2_DATA_ROOT'])
        executable = next(p for p in (root / 'VER95/DBSIM.EXE', root / 'extracted/cd/VER95/DBSIM.EXE') if p.exists())
        report = {}
        entries, table, starts = inventory.full_inventory(executable, OBJDUMP, report)
        generated = inventory.render(entries, table).encode('ascii')
        committed = (SCRIPTS.parent / 'proxy/probe_entry_inventory.h').read_bytes()
        self.assertEqual(generated, committed,
                         'regenerate proxy/probe_entry_inventory.h with scripts/probe_instruction_inventory.py')
        # Three linear-sweep samples and the three entries behind zero padding.
        self.assertLessEqual({0x40109c, 0x40111c, 0x401148, 0x45f570, 0x42789c, 0x492e88},
                             {va for va, _ in entries})
        # No linear entry disappears without proof that it is not a boundary.
        self.assertEqual(len(entries), report['linear_entries'] + len(report['added']) - len(report['removed']))
        self.assertTrue(all(r.startswith(('inside instruction 0x', 'jump table of 0x'))
                            for r in report['removed'].values()))
        self.assertEqual((len(report['added']), len(report['removed'])), (218, 11), report['removed'])
        # The known starts with their kinds (dbsim_map.md "entry kinds").
        self.assertEqual((report['function_starts'], report['operand_pointers'], len(table)), (3010, 20, 326))
        unprobed = [va for va, (kind, _) in starts.items() if kind.startswith('unprobed')]
        self.assertEqual(unprobed, [0x467ad8])
        kinds = {va: kind for va, (kind, _) in starts.items()}
        for va, kind in ((0x47e068, 'EP_ENTER'), (0x467828, 'EP_CALL_REL32'), (0x4968ec, 'EP_JMP_ABS'),
                         (0x490d34, 'EP_MOV_EAX_ABS'), (0x431210, 'EP_MOVSX_R32_M8'), (0x45f570, 'push'),
                         (0x405ea4, 'EP_MOV_EAX_ABS')):
            self.assertEqual(kinds[va], kind, hex(va))
        self.assertIn('operand_pointers', starts[0x405ea4][1])
        self.assertEqual(sum(1 for kind in kinds.values() if kind == 'push'), 2683)

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
    def test_committed_vshell_header_is_current(self):
        """--module vshell regenerates proxy/vshell_probe_inventory.h byte for byte."""
        root = Path(os.environ['ES2_DATA_ROOT'])
        executable = next(p for p in (root / 'VER95/VSHELL.EXE', root / 'extracted/cd/VER95/VSHELL.EXE') if p.exists())
        with self.assertRaisesRegex(ValueError, 'DBSIM SHA-256 mismatch'):
            inventory.full_inventory(executable, OBJDUMP)
        report = {}
        entries, table, starts = inventory.full_inventory(executable, OBJDUMP, report, module='vshell')
        generated = inventory.render(entries, table, 'vshell', inventory.probed_starts(starts)).encode('ascii')
        committed = (SCRIPTS.parent / 'proxy/vshell_probe_inventory.h').read_bytes()
        self.assertEqual(generated, committed,
                         'regenerate proxy/vshell_probe_inventory.h with probe_instruction_inventory.py --module vshell')
        self.assertEqual((len(entries), report['function_starts'], len(table)), (14188, 2161, 300))
        self.assertEqual(len(inventory.probed_starts(starts)), 2139)
        # The entry 0x401000 (mov eax,[abs32]) and the import thunk 0x46baae it calls (jmp [abs32]);
        # the __startup thunk 0x46b9be is only jumped to, not a known start.
        self.assertEqual((starts[0x401000][0], starts[0x46baae][0]), ('EP_MOV_EAX_ABS', 'EP_JMP_ABS'))
        self.assertNotIn(0x46b9be, starts)
        # The DBSIM rendering is unchanged by the module parameter.
        self.assertEqual(inventory.render([(0x401000, 0x55)], [(0x401001, 'EP_RET')]),
                         inventory.render([(0x401000, 0x55)], [(0x401001, 'EP_RET')], 'dbsim', [0x401000]))


# Own image with function starts whose first instruction is not a push.
KINDS = [
    (0x401000, '55 8bec', 'entry: push ebp; mov ebp,esp'),
    (0x401003, 'e818000000 e823000000 e82e000000', 'call 0x401020; call 0x401030; call 0x401040'),
    (0x401012, '6850104000', 'push 0x401050 (relocated operand: a callback)'),
    (0x401017, 'e844000000 5d c3 cccc', 'call 0x401060; pop ebp; ret; int3 filler'),
    (0x401020, 'c8000000 53 5b c9 c3' + 'cc' * 8, 'enter 0,0; push ebx; pop ebx; leave; ret'),
    (0x401030, 'ff2500204000' + 'cc' * 10, 'jmp [0x402000] (an import thunk)'),
    (0x401040, 'a104204000 c3' + 'cc' * 9 + '00', 'mov eax,[0x402004]; ret; padding; linear: 00 53 33 swallows 0x401050'),
    (0x401050, '53 33c0 5b c3' + 'cc' * 11, 'callback known only from the push operand: push ebx; xor eax,eax; ...'),
    (0x401060, 'cc c3', 'int3; ret: a called breakpoint stub'),
]
KIND_RELOCATIONS = {0x401013, 0x401032, 0x401041}
KIND_DATA = struct.pack('<II', 0x00500000, 7)


def kind_image(operand=0x401050):
    code = b''
    for va, text, _ in KINDS:
        assert 0x401000 + len(code) == va, hex(va)
        code += bytes.fromhex(text.replace(' ', ''))
    code = code[:0x13] + struct.pack('<I', operand) + code[0x17:]
    return synthetic_pe(code, KIND_DATA, KIND_RELOCATIONS)


@unittest.skipUnless(OBJDUMP.exists(), 'pinned llvm-objdump missing')
class EntryKindTest(unittest.TestCase):
    def classify(self, data):
        details = {}
        entries, _, reached = inventory.classify(data, OBJDUMP, details)
        image, roots = details['image'], dict(details['roots'])
        wider, tables = inventory.extend(image, reached, details['tables'], roots, OBJDUMP)
        starts = inventory.function_starts(image, wider, roots, tables)
        return entries, reached, wider, roots, starts, inventory.entry_kinds(image, wider, starts, entries)

    def test_kinds_of_known_starts(self):
        entries, reached, wider, roots, starts, kinds = self.classify(kind_image())
        self.assertEqual(roots['operand_pointers'], {0x401050})
        self.assertNotIn(0x401050, reached)
        self.assertEqual({va: wider[va] for va in reached}, reached)
        self.assertEqual(kinds, {0x401000: 'push', 0x401020: 'EP_ENTER', 0x401030: 'EP_JMP_ABS',
                                 0x401040: 'EP_MOV_EAX_ABS', 0x401050: 'EP_PUSH',
                                 0x401060: 'unprobed: int3 (the original byte is the breakpoint itself)'})
        self.assertEqual(starts[0x401050], ['operand_pointers', 'pointer'])
        self.assertEqual(starts[0x401020], ['call', 'linear_calls'])
        # The first table is the push-inventory rule alone: the callback's push is no row of it.
        pushes = {va for va, _ in entries}
        self.assertIn(0x401024, pushes)
        self.assertNotIn(0x401050, pushes)

    def test_operand_pointer_into_reached_code_is_no_root(self):
        # A push operand into the immediate of the mov at 0x401040 is not taken as a start.
        entries, reached, wider, roots, starts, kinds = self.classify(kind_image(0x401041))
        self.assertEqual(roots['operand_pointers'], set())
        self.assertEqual(wider, reached)
        self.assertNotIn(0x401041, starts)

    def test_entry_kind_bytes(self):
        cases = [('c8100000', 4, 'EP_ENTER'), ('c8100001', 4, None), ('8b0d00204000', 6, 'EP_MOV_R32_ABS'),
                 ('8b2500204000', 6, None), ('8b01', 2, None), ('8bc4', 2, 'EP_MOV_R32_R32'), ('8be0', 2, None),
                 ('33c9', 2, 'EP_XOR_R32_SELF'), ('33c1', 2, None), ('33e4', 2, None), ('bc00000000', 5, None),
                 ('66ba0000', 4, 'EP_MOV_R16_IMM'), ('66a100204000', 6, 'EP_MOV_AX_ABS'),
                 ('0fbe0500204000', 7, 'EP_MOVSX_R32_M8'), ('0fbf0d00204000', 7, 'EP_MOVSX_R32_M16'),
                 ('0fbe01', 3, None), ('66833d0020400000', 8, 'EP_CMP_M16_IMM8'),
                 ('66c705002040003412', 9, 'EP_MOV_M16_IMM'), ('800500204000ff', 7, 'EP_ADD_M8_IMM8'),
                 ('a1002040', 4, None), ('c3', 1, 'EP_RET'), ('cc', 1, None)]
        for text, size, kind in cases:
            self.assertEqual(inventory.entry_kind(bytes.fromhex(text) + bytes(4), size), kind, text)
        # The C engine has one enumerator and one length per kind, in the same order after EP_PUSH.
        engine = (SCRIPTS.parent / 'proxy/entry_probe_engine.h').read_text(encoding='utf-8')
        names = engine.split('enum { EP_PUSH,')[1].split('EP_KINDS')[0].split(',')
        self.assertEqual([n.strip() for n in names if n.strip()], [k[0] for k in inventory.ENTRY_KINDS])
        lengths = engine.split('ep_kind_length[EP_KINDS]={')[1].split('}')[0].split(',')
        self.assertEqual([int(n) for n in lengths], [1] + [k[1] for k in inventory.ENTRY_KINDS])

    def test_render_with_kinds(self):
        text = inventory.render([(0x401000, 0x55)], [(0x401020, 'EP_ENTER'), (0x401030, 'EP_JMP_ABS')])
        self.assertTrue(text.endswith('static const probe_entry_kind probe_entry_kinds[] = {\n'
                                      '{0x00401020u,EP_ENTER}, {0x00401030u,EP_JMP_ABS}, \n};\n#endif\n'), text)
        self.assertEqual(inventory.render([(0x401000, 0x55)]), inventory.render([(0x401000, 0x55)], []))
        self.assertNotIn('probe_entry_kinds', inventory.render([(0x401000, 0x55)]))


if __name__ == '__main__':
    unittest.main()
