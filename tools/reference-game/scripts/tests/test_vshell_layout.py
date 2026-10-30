"""vshell_layout.py on an own synthetic PE and listing; the committed table's shape.

The synthetic image holds a screen constructor that allocates one window,
runs a window class constructor on it (which stores its vtable and calls the
modelled base window constructor) and keeps the pointer in a global, a
second constructor that branches on a run-time global, and a type descriptor
for the class name. The emulator, the models and the table writer are run on
it with a profile of synthetic addresses. The schema cases read the committed
shell/data/vshell_layout.json. The real-data case regenerates the table from
the reference VSHELL.EXE with the pinned llvm-objdump and requires the
committed file to be byte-identical (skipped without ES2_DATA_ROOT).
"""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parents[2]
sys.path.insert(0, str(SCRIPTS))
import vshell_layout as vl  # noqa: E402

# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'
TABLE = REPO / 'shell' / 'data' / 'vshell_layout.json'


def synthetic_pe(code, data):
    """Own minimal PE32: CODE at 0x401000, DATA at 0x402000 (image base 0x400000)."""
    header = bytearray(0x400)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, 0x40)
    header[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x44, 0x14c, 2, 0, 0, 0, 0xe0, 0x102)
    struct.pack_into('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', header, 0x58, 0x10b, 1, 0, 0x200, 0x200, 0,
                     0x1000, 0x1000, 0x2000, 0x400000, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0, 0x3000, 0x400, 0,
                     2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    body, pointer = b'', 0x400
    for i, (name, rva, flags, blob) in enumerate(((b'CODE', 0x1000, 0x60000020, code),
                                                  (b'DATA', 0x2000, 0xc0000040, data))):
        size = (len(blob) + 0x1ff) & ~0x1ff
        struct.pack_into('<8sIIIIIIHHI', header, 0x138 + 40 * i, name, len(blob), rva, size, pointer,
                         0, 0, 0, 0, flags)
        body += blob.ljust(size, b'\0')
        pointer += size
    return bytes(header) + body


# Own code: (address, bytes, the instruction as llvm-objdump prints it).
PROGRAM = [
    # screen constructor 1
    (0x401000, '6a20', 'push\t0x20'),
    (0x401002, 'e8f9000000', 'call\t0x401100 <x>'),               # operator new (modelled)
    (0x401007, '59', 'pop\tecx'),
    (0x401008, '89c3', 'mov\tebx, eax'),
    (0x40100a, '6a00', 'push\t0x0'),                             # handler
    (0x40100c, '6800214000', 'push\t0x402100'),                  # rectangle in DATA (constant)
    (0x401011, 'ff3580114800', 'push\tdword ptr [0x481180]'),    # parent: VSHELL's root window
    (0x401017, '53', 'push\tebx'),
    (0x401018, 'e8e3010000', 'call\t0x401200 <x>'),              # class constructor (interpreted)
    (0x40101d, '83c410', 'add\tesp, 0x10'),
    (0x401020, '891d00234000', 'mov\tdword ptr [0x402300], ebx'),
    (0x401026, 'c74311' '00000000', 'mov\tdword ptr [ebx + 0x11], 0x0'),   # a field written by the screen
    (0x40102d, 'c3', 'ret'),
    # screen constructor 2: a branch on a global that 0x401040 writes
    (0x401030, 'a104234000', 'mov\teax, dword ptr [0x402304]'),
    (0x401035, '85c0', 'test\teax, eax'),
    (0x401037, '7401', 'je\t0x40103a <x>'),
    (0x401039, '90', 'nop'),
    (0x40103a, 'c3', 'ret'),
    (0x401040, 'a304234000', 'mov\tdword ptr [0x402304], eax'),
    (0x401045, 'c3', 'ret'),
    # class constructor: vtable, then the base window constructor (modelled)
    (0x401200, '55', 'push\tebp'),
    (0x401201, '8bec', 'mov\tebp, esp'),
    (0x401203, '8b4508', 'mov\teax, dword ptr [ebp + 0x8]'),
    (0x401206, 'c74001' '10204000', 'mov\tdword ptr [eax + 0x1], 0x402010'),
    (0x40120d, 'ff7514', 'push\tdword ptr [ebp + 0x14]'),
    (0x401210, 'ff7510', 'push\tdword ptr [ebp + 0x10]'),
    (0x401213, 'ff750c', 'push\tdword ptr [ebp + 0xc]'),
    (0x401216, '50', 'push\teax'),
    (0x401217, 'e8e4000000', 'call\t0x401300 <x>'),
    (0x40121c, '83c410', 'add\tesp, 0x10'),
    (0x40121f, '5d', 'pop\tebp'),
    (0x401220, 'c3', 'ret'),
]


def code_and_listing():
    code = bytearray(0x500)
    lines = []
    for addr, hexbytes, text in PROGRAM:
        raw = bytes.fromhex(hexbytes)
        code[addr - 0x401000:addr - 0x401000 + len(raw)] = raw
        lines.append(f'  {addr:x}: {" ".join(f"{b:02x}" for b in raw):<29}\t{text}')
    # Borland type descriptor at 0x401400: size 0x45, mask, name offset 0x10, name.
    struct.pack_into('<IHH', code, 0x400, 0x45, 3, 0x10)
    code[0x410:0x417] = b'ESTest\0'
    return bytes(code), '\n'.join(lines) + '\n'


def data_section():
    data = bytearray(0x400)
    struct.pack_into('<I', data, 0x04, 0x401400)          # vtable 0x402010 - 0xc -> type descriptor
    struct.pack_into('<4i', data, 0x100, 10, 20, 110, 40)  # the rectangle
    return bytes(data)


class Profile:
    """Replaces the reference profile by the synthetic addresses for one test."""
    NAMES = ('SCREENS', 'CTORS', 'METHODS', 'INTERPRET', 'MODELS', 'BRANCH_ASSUMPTIONS', 'RESOURCE_GLOBALS')

    def __init__(self, **values):
        self.values = values

    def __enter__(self):
        self.saved = {n: getattr(vl, n) for n in self.NAMES}
        defaults = dict(SCREENS=[('test', 0x401000)],
                        CTORS={0x401200: ('ESTest', ['parent', 'rect', 'handler'])},
                        METHODS={}, INTERPRET=set(),
                        MODELS={0x401100: vl.m_new, 0x401300: vl.m_window_ctor},
                        BRANCH_ASSUMPTIONS={}, RESOURCE_GLOBALS=[])
        defaults.update(self.values)
        for n, v in defaults.items():
            setattr(vl, n, v)
        return self

    def __exit__(self, *exc):
        for n, v in self.saved.items():
            setattr(vl, n, v)


def run_synthetic(**profile):
    code, listing_text = code_and_listing()
    image = vl.Image(synthetic_pe(code, data_section()))
    with Profile(**profile):
        return vl.layout(image, vl.Listing(image, None, listing_text))


class ParsingTest(unittest.TestCase):
    def test_listing_lines(self):
        text = ('\n00401000 <x>:\n'
                '  401000: c7 83 c5 00 00 00 10 00 00 00\tmov\tdword ptr [ebx + 0xc5], 0x10\n'
                '  40100a: e8 f1 ff ff ff               \tcall\t0x401000 <x>\n'
                '  40100f: 0f 1f 84 00 00 00 00\tnop\tdword ptr [eax + eax]\n'
                '  401016: 00 00\n')
        insns = vl.parse_listing(text)
        self.assertEqual(sorted(insns), [0x401000, 0x40100a, 0x40100f])
        self.assertEqual(insns[0x401000].ops, ['dword ptr [ebx + 0xc5]', '0x10'])   # 10 bytes, no gap
        self.assertEqual(insns[0x40100a].ops, ['0x401000'])                          # symbol dropped
        self.assertEqual(insns[0x401000].next, 0x40100a)
        self.assertEqual(vl.split_operands('dword ptr [eax + 4*ecx + 0x8], edx'),
                         ['dword ptr [eax + 4*ecx + 0x8]', 'edx'])

    def test_hash_is_checked_before_reading(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'VSHELL.EXE'
            path.write_bytes(b'MZ not the reference')
            with self.assertRaisesRegex(vl.GeneratorError, 'SHA-256'):
                vl.generate(path, objdump=Path(tmp) / 'never-started.exe')

    def test_listing_bytes_must_match_the_image(self):
        code, listing_text = code_and_listing()
        image = vl.Image(synthetic_pe(code, data_section()))
        listing = vl.Listing(image, None, listing_text.replace('6a 20', '6a 21'))
        with self.assertRaisesRegex(vl.GeneratorError, 'differ'):
            listing.at(0x401000)
        with self.assertRaisesRegex(vl.GeneratorError, 'not in the listing'):
            listing.at(0x401500)

    def test_values(self):
        self.assertEqual(vl.binop('+', 0xffffffff, 1), 0)
        self.assertEqual(vl.binop('*', 0xfffffffe, 3), (-6) & 0xffffffff)
        self.assertEqual(vl.binop('sar', 0x80000000, 4), 0xf8000000)
        g = vl.Sym('[0x48d2d8]')
        self.assertEqual(vl.binop('+', vl.binop('+', g, 8), -8 & 0xffffffff), g)
        self.assertEqual(vl.binop('-', g, 4).e, '([0x48d2d8]-0x4)')
        self.assertEqual(vl.binop('^', g, g), 0)


class EmulatorTest(unittest.TestCase):
    def test_control_from_a_constructor_call(self):
        table, emu = run_synthetic()
        self.assertEqual([s['name'] for s in table['screens']], ['test'])
        (row,) = table['screens'][0]['controls']
        self.assertEqual(row['id'], 'test.00')
        self.assertEqual(row['class'], 'ESTest')                 # Borland RTTI behind vtable 0x402010
        self.assertEqual((row['ctor'], row['site']), ('0x401200', '0x401018'))
        self.assertEqual(row['parent'], {'control': 'root'})
        self.assertEqual(row['rect'], [10, 20, 110, 40])
        self.assertEqual(row['screen_rect'], [10, 20, 110, 40])  # the root sits at the origin
        self.assertEqual(row['globals'], ['0x402300'])
        self.assertEqual(row['handler'], 0)
        # created hidden by the base window constructor; the screen cleared the flags later
        self.assertEqual(row['after_construction'], 'shown')
        self.assertEqual(row['fields'], {'0x11': {'size': 4, 'value': 0}})
        self.assertEqual(table['classes'], {'ESTest': {'vtable': '0x402010', 'size': 0x45,
                                                       'constructors': ['0x401200']}})
        self.assertEqual(table['constant_reads'], ['0x402100', '0x402104', '0x402108', '0x40210c'])
        text = vl.dumps(table)
        self.assertEqual(text, vl.dumps(json.loads(text)))

    def test_branch_on_run_time_value_needs_an_assumption(self):
        screens = [('branchy', 0x401030)]
        with self.assertRaisesRegex(vl.GeneratorError, r'0x401037.*BRANCH_ASSUMPTIONS'):
            run_synthetic(SCREENS=screens)
        table, _ = run_synthetic(SCREENS=screens, BRANCH_ASSUMPTIONS={0x401037: (True, 'synthetic', None)})
        self.assertEqual(table['assumptions'], [{'site': '0x401037', 'taken': True, 'reason': 'synthetic'}])
        explored = []
        code, listing_text = code_and_listing()
        image = vl.Image(synthetic_pe(code, data_section()))
        with Profile(SCREENS=screens):
            vl.layout(image, vl.Listing(image, None, listing_text), explored)
        self.assertEqual(len(explored), 1)
        self.assertIn('0x401037', explored[0])

    def test_show_and_hide_follow_the_parent(self):
        _, emu = run_synthetic()
        parent = emu.new_object(0x45)
        child = emu.new_object(0x45)
        for obj in (parent, child):
            emu.objects[obj] = vl.Control(f'w{obj:x}', obj, 'x', 0, None, {}, None)
            emu.objects[obj].size = 0x45
        emu.store(parent + vl.F_PREV, emu.root, 4)
        emu.or_flags(parent, vl.FLAG_HEAD | vl.FLAG_HIDDEN)
        emu.store(parent + vl.F_CHILD, child, 4)
        emu.store(child + vl.F_PREV, parent, 4)
        emu.or_flags(child, vl.FLAG_HEAD | vl.FLAG_HIDDEN)
        vl.show(emu, child)                       # parent hidden: the child waits
        self.assertEqual(emu.flags_of(child) & 6, vl.FLAG_HIDDEN | vl.FLAG_PENDING)
        vl.show(emu, parent)                      # showing the parent shows the waiting child
        self.assertEqual(emu.flags_of(parent) & 6, 0)
        self.assertEqual(emu.flags_of(child) & 6, 0)
        vl.hide(emu, parent)                      # hiding it marks the shown child pending
        self.assertEqual(emu.flags_of(child) & 6, vl.FLAG_HIDDEN | vl.FLAG_PENDING)
        vl.hide(emu, child)                       # an explicit hide drops the pending mark
        self.assertEqual(emu.flags_of(child) & 6, vl.FLAG_HIDDEN)
        vl.show(emu, parent)
        self.assertEqual(emu.flags_of(child) & 6, vl.FLAG_HIDDEN)


def committed():
    return json.loads(TABLE.read_text(encoding='utf-8'))


class CommittedTableTest(unittest.TestCase):
    """The shape the own shell's screens and the VSHELL capture rely on."""

    def setUp(self):
        self.table = committed()

    def test_header(self):
        t = self.table
        self.assertEqual(list(t), ['schema', 'source', 'classes', 'resources', 'assumptions', 'screens',
                                   'constant_reads', 'calls_not_interpreted'])
        self.assertEqual(t['schema'], vl.SCHEMA_VERSION)
        self.assertEqual(t['source']['sha256'], vl.EXPECTED_SHA256)
        self.assertEqual([(s['name'], int(s['constructor'], 16)) for s in t['screens']], vl.SCREENS)
        self.assertEqual({a['site'] for a in t['assumptions']}, {f'{a:#x}' for a in vl.BRANCH_ASSUMPTIONS})
        self.assertEqual(set(t['resources']), {f'{g[0]:#x}' for g in vl.RESOURCE_GLOBALS})
        self.assertEqual(TABLE.read_bytes(), vl.dumps(t).encode('utf-8'), 'not in the generator\'s format')

    def test_controls(self):
        ids = {'root', 'display'}
        rows = [row for s in self.table['screens'] for row in s['controls']]
        self.assertGreater(len(rows), 1000)
        keys = {'id', 'class', 'ctor', 'site', 'globals', 'parent', 'rect', 'screen_rect', 'args',
                'after_construction', 'handler'}
        optional = {'owner', 'text', 'per_record', 'methods', 'fields', 'screen_show_hide'}
        for screen in self.table['screens']:
            for n, row in enumerate(screen['controls']):
                self.assertEqual(row['id'], f"{screen['name']}.{n:02d}")
                self.assertTrue(keys <= set(row) <= keys | optional, row['id'])
                self.assertIn(row['class'], self.table['classes'])
                self.assertIn(row['ctor'], self.table['classes'][row['class']]['constructors'])
                self.assertEqual(set(row['args']), set(vl.CTORS[int(row['ctor'], 16)][1]) - {'parent', 'rect'})
                self.assertIn(row['after_construction'], ('shown', 'pending', 'hidden'))
                self.assertEqual(len(row['rect']), 4)
                for v in row['rect']:
                    self.assertTrue(isinstance(v, int) or isinstance(v, dict), row['id'])
                parent = row['parent'].get('control')
                self.assertIn(parent, ids, f"{row['id']}: parent built before the child")
                if 'owner' in row:
                    self.assertIn(row['owner'], ids)
                ids.add(row['id'])

    def test_screen_rectangles_add_up(self):
        rows = {row['id']: row for s in self.table['screens'] for row in s['controls']}
        def origin(rid):
            """Top-left corner on the screen: the parents' left/top added up (0x41f599)."""
            if rid in ('root', 'display'):
                return 0, 0
            row = rows[rid]
            px, py = origin(row['parent']['control'])
            return px + row['rect'][0], py + row['rect'][1]

        checked = 0
        for rid, row in rows.items():
            if row['screen_rect'] is None:
                continue
            px, py = origin(row['parent']['control'])
            x0, y0, x1, y1 = row['rect']
            checked += 1
            self.assertEqual(row['screen_rect'], [px + x0, py + y0, px + x1, py + y1], rid)
        self.assertGreater(checked, 900)

    def test_only_numbers_ids_and_names(self):
        """No strings of the game: texts are ESTEXT IDs, executable literals only address and length."""
        def walk(v, path):
            if isinstance(v, dict):
                if 'exe_string' in v:
                    self.assertEqual(set(v), {'exe_string', 'length'}, path)
                if 'estext' in v:
                    self.assertTrue(isinstance(v['estext'], int) or 'formula' in v['estext'], path)
                for k, x in v.items():
                    walk(x, f'{path}.{k}')
            elif isinstance(v, list):
                for i, x in enumerate(v):
                    walk(x, f'{path}[{i}]')
            elif isinstance(v, str):
                self.assertTrue(v.isascii() and len(v) < 400, path)
        walk(self.table, '$')

    def test_known_controls(self):
        """Spot checks against vshell_ui.md §4: labels and handlers of the main menu and the bay."""
        rows = {row['id']: row for s in self.table['screens'] for row in s['controls']}
        main = [r for r in rows.values() if r['id'].startswith('main_menu.') and r['class'] == 'ESButtonFont']
        self.assertEqual(sorted((r['args']['label']['estext'], r['handler']) for r in main),
                         [(3, '0x431935'), (4, '0x431a08'), (5, '0x431a73'), (6, '0x431b27'), (7, '0x431f3a'),
                          (10, '0x431b9b'), (11, '0x431bfe'), (13, '0x431c7b'), (14, '0x431db6'), (15, '0x431e1b')])
        bay = [r for r in rows.values() if r['id'].startswith('service_bay.') and r['class'] == 'ESButtonBitmap']
        self.assertEqual([r['handler'] for r in bay],
                         ['0x43a5bd', '0x43a66c', '0x43a768', '0x43a862', '0x43a962', '0x43aa62', '0x43ab62',
                          '0x43ac5a', '0x43ad5a'])
        self.assertEqual(rows['service_bay.05']['args']['bitmap_normal'], {'dba': 'dba\\mnu_bttn.dba', 'frame': 1})


@unittest.skipUnless(os.environ.get('ES2_DATA_ROOT'), 'ES2_DATA_ROOT unset')
class RealDataTest(unittest.TestCase):
    def test_committed_table_is_current(self):
        if not OBJDUMP.exists():
            self.skipTest('pinned llvm-objdump missing')
        root = Path(os.environ['ES2_DATA_ROOT'])
        exe = next((p for p in (root / 'VER95/VSHELL.EXE', root / 'extracted/cd/VER95/VSHELL.EXE') if p.exists()),
                   None)
        if exe is None:
            self.skipTest('no VER95/VSHELL.EXE below ES2_DATA_ROOT')
        table, _ = vl.generate(exe, OBJDUMP)
        self.assertEqual(vl.dumps(table), TABLE.read_text(encoding='utf-8'),
                         'regenerate shell/data/vshell_layout.json with scripts/vshell_layout.py')


if __name__ == '__main__':
    unittest.main()
