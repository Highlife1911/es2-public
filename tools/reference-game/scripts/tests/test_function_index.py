"""function_index.py on a synthetic listing, registry, map and documents; no game data required."""
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import function_index as fi

SUITES = fi.SUITES

# (VA, length, mnemonic, operands) of a tiny made-up CODE section. The byte
# column is filler: the parser reads addresses, mnemonics and call targets.
CODE = [
    (0x401000, 1, 'push', 'ebp'),
    (0x401001, 5, 'call', '0x401010 <CODE+0x10>'),
    (0x401006, 5, 'call', '0x401020 <CODE+0x20>'),
    (0x40100b, 5, 'call', '0x401041 <CODE+0x41>'),   # inside the thunk instruction
    (0x401010, 5, 'call', '0x401040 <CODE+0x40>'),   # alpha (hook) calls the thunk
    (0x401015, 1, 'ret', ''),
] + [(0x401016 + i, 1, 'nop', '') for i in range(10)] + [
    (0x401020, 5, 'call', '0x401030 <_Exported>'),   # map row, classified H4
    (0x401025, 1, 'ret', ''),
] + [(0x401026 + i, 1, 'nop', '') for i in range(10)] + [
    (0x401030, 1, 'push', 'ebp'),                    # export, classified H3, doc role
    (0x401031, 1, 'ret', ''),
    (0x401032, 1, 'nop', ''),
    (0x401033, 2, 'mov', 'eax, ebx'),
    (0x401035, 5, 'mov', 'eax, 0x1'),                # 0x401038 (map row) lies inside
    (0x40103a, 1, 'ret', ''),
] + [(0x40103b + i, 1, 'int3', '') for i in range(5)] + [
    (0x401040, 6, 'jmp', 'dword ptr [0x402000]'),    # import thunk
    (0x401046, 1, 'nop', ''),
    (0x401050, 1, 'push', 'ebp'),                    # beta (hook, nothing resolvable)
    (0x401051, 1, 'ret', ''),
]


def listing_text(extra=()):
    out = ['', 'x.exe:\tfile format coff-i386', '', 'Disassembly of section CODE:', '',
           '00401000 <CODE>:']
    for va, length, mnemonic, operands in list(CODE) + list(extra):
        if va == 0x401030:
            out += ['', '00401030 <_Exported>:']
        raw = ' '.join(['cc'] * length) + ' '
        line = f'  {va:x}: {raw:<29}\t{mnemonic}'
        out.append(line + (f'\t{operands}' if operands else ''))
    return '\n'.join(out) + '\n'


REGISTRY = """#define X
ES2_HOOK(alpha, 0x00001010, grp_hook_descriptors[0], NULL, grp)
ES2_HOOK(beta, 0x1050, other_hook_descriptors[0], &beta_ext, other)
"""
ADAPTER = """/* Grp adapters. Specification: docs/reverse-engineering/sub.md.
 */
static es2_hook_descriptor grp_hook_descriptors[]={
    {ES2_HOOK_ID(alpha),sig,1,1,lens,1,1,grp_alpha32,NULL,NULL,NULL,0},
};
"""
HOOKS_DOC = """# Hooks

## Registry

| Hook | VA | Core module | Compare | C1_01 calls | Added |
| --- | --- | --- | --- | ---: | --- |
| `alpha` | 401010 | grp | yes | 3 | test |
| `beta` | 401050 | missing | no | 0 | test |
"""
SUB_DOC = """# Sub

## Function table

| VA | Role (from the bytes) | Instr. |
|---|---|---|
| `0x401030` | draws the [thing](x.md) \\| twice | 2 |

```
## not a heading
```

## Function table

## Alpha 401010: the contract

Text naming `alpha`.
"""
CLASSIFIED = """# Verdicts

## Verdicts

| VA | Verdict | Document | Reason |
|---|---|---|---|
| `0x401030` | H3 | [doc](doc.md) | reason three |
| `0x401020`, `0x401024` | H4 | x | simulation with a \\| bar |

## Later

| Function (hook) | Sites | Verdict | Proof |
|---|---|---|---|
| `0x401000` | 401001 | dead | not a verdict row of this table |
"""
HEADER = """#include <stdint.h>
/* The alpha function 0x401010. */
uint32_t es2_alpha(const uint8_t *p);
"""
SOURCE = """#include "es2/core/grp.h"
uint32_t es2_alpha(const uint8_t *p) {
    return p[0];
}
"""


def row(va, callers=(), live=(), unprobed=(), instructions=7):
    return dict(va=f'{va:#x}', counts={s: 5 for s in SUITES}, instructions=instructions,
                callers=[f'{c:#x}' for c in callers], callees_live=[f'{c:#x}' for c in live],
                callees_unprobed=[f'{c:#x}' for c in unprobed])


MAP = dict(summary=dict(probed=9, live=2, dead=7),
           rows=[row(0x401020, callers=[0x401000]), row(0x401038, instructions=3)])
COUNTERS = dict(source='test', suites={s: dict(last_record={
    'hk_alpha_calls': 3, 'hk_alpha_bypassed': 0, 'hk_alpha_call_401020': 2,
    'hk_beta_call_401020': 1}) for s in SUITES})


def tiny_pe(slot_rva=0x2000):
    """A PE32 with one section (RVA 0x1000) holding an import of KERNEL32!Sleep
    whose IAT slot is at image base + slot_rva."""
    sec = bytearray(0x1200)
    rva = 0x1000
    # import descriptor at 0x1000, lookup table at 0x1100, name at 0x1180, hint/name at 0x1190
    struct.pack_into('<IIIII', sec, 0, rva + 0x100, 0, 0, rva + 0x180, slot_rva)
    struct.pack_into('<I', sec, 0x100, rva + 0x190)
    sec[0x180:0x18d] = b'KERNEL32.dll\0'
    sec[0x190:0x198] = b'\0\0Sleep\0'
    # IAT at slot_rva (inside the section: 0x1000 + 0x1000 = 0x2000)
    struct.pack_into('<I', sec, slot_rva - rva, rva + 0x190)
    head = bytearray(0x200)
    head[:2] = b'MZ'
    struct.pack_into('<I', head, 0x3c, 0x40)
    head[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HH', head, 0x44, 0x14c, 1)
    struct.pack_into('<H', head, 0x54, 0xe0)
    opt = 0x58
    struct.pack_into('<H', head, opt, 0x10b)
    struct.pack_into('<I', head, opt + 28, 0x400000)
    struct.pack_into('<II', head, opt + 104, rva, 0x28)
    sec_hdr = opt + 0xe0
    head[sec_hdr:sec_hdr + 5] = b'.data'
    struct.pack_into('<IIII', head, sec_hdr + 8, len(sec), rva, len(sec), 0x200)
    return bytes(head) + bytes(sec)


class FunctionIndexTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = self.root = Path(self.tmp.name)
        files = {
            'build/dbsim.lst': listing_text(),
            'tools/reference-game/proxy/hook_registry.h': REGISTRY,
            'tools/reference-game/proxy/grp_hooks.h': ADAPTER,
            'docs/reference-game/hooks.md': HOOKS_DOC,
            'docs/reference-game/function_verdicts.md': CLASSIFIED,
            'docs/reverse-engineering/sub.md': SUB_DOC,
            'core/include/es2/core/grp.h': HEADER,
            'core/src/grp.c': SOURCE,
            'logs/map/map.json': json.dumps(MAP),
            'logs/run/hook-counters.json': json.dumps(COUNTERS),
        }
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode('utf-8'))
        (root / 'build/x.exe').write_bytes(tiny_pe())

    def tearDown(self):
        self.tmp.cleanup()

    def run_index(self, out='docs/functions', image=True, extra=()):
        r = self.root
        argv = ['--listing', str(r / 'build/dbsim.lst'),
                '--registry', str(r / 'tools/reference-game/proxy/hook_registry.h'),
                '--hooks-doc', str(r / 'docs/reference-game/hooks.md'),
                '--classified', str(r / 'docs/reference-game/function_verdicts.md'),
                '--docs', str(r / 'docs/reverse-engineering'), '--core', str(r / 'core'),
                '--map', str(r / 'logs/map/map.json'),
                '--counters', str(r / 'logs/run/hook-counters.json'),
                '--output', str(r / out)]
        if image:
            argv += ['--image', str(r / 'build/x.exe')]
        argv += list(extra)
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(fi.main(argv), 0)
        return stdout.getvalue()

    def page_rows(self, out='docs/functions'):
        text = (self.root / out / 'dbsim-400000.md').read_text(encoding='utf-8')
        rows = {}
        for line in text.splitlines():
            if line.startswith('| 40'):
                cells = [c.strip() for c in line.strip('|').split(' | ')]
                rows[int(cells[0], 16)] = cells
        return text, rows

    def test_rows(self):
        self.run_index()
        _, rows = self.page_rows()
        # Entries: call targets at a start, the export, registry, map rows and
        # their callers, classified VAs (0x401024 and 0x401038 are not
        # instruction starts).
        self.assertEqual(sorted(rows), [0x401000, 0x401010, 0x401020, 0x401024, 0x401030,
                                        0x401038, 0x401040, 0x401050])
        self.assertEqual(rows[0x401000][2:6], ['RTL', '~4', '', '401010 alpha, 401020'])
        self.assertNotIn(0x401041, rows)  # call target inside an instruction
        alpha = rows[0x401010]
        self.assertEqual(alpha[1], '`alpha`')
        self.assertEqual(alpha[2:4], ['RTL', '~2'])  # call + ret, trailing nops dropped
        self.assertEqual(alpha[4], '401000')
        self.assertEqual(alpha[5], '401040')
        self.assertIn('group `grp`', alpha[6])
        self.assertIn('[grp.c](../../core/src/grp.c) `es2_alpha`', alpha[6])
        self.assertIn('compare yes', alpha[6])
        self.assertIn('[contract](../reverse-engineering/sub.md#alpha-401010-the-contract)', alpha[6])
        self.assertEqual(alpha[7:11], ['', 'hook', 'hook', '3/3/3/3/3/3'])
        live = rows[0x401020]
        self.assertEqual(live[3], '7')  # the map's count
        self.assertEqual(live[4], '401000')
        self.assertEqual(live[7:11], ['', 'H4', '5/5/5/5/5/5', 'via hooks 3/3/3/3/3/3'])
        self.assertEqual(live[11], 'simulation with a \\| bar')
        self.assertEqual(rows[0x401024][3], '?')
        exported = rows[0x401030]
        self.assertEqual(exported[1], '`_Exported`')
        self.assertEqual(exported[3], '~5')  # linear span up to the map row 0x401038
        self.assertEqual(exported[4], '401020')
        self.assertEqual(exported[8], 'H3')
        self.assertEqual(exported[11], 'draws the thing \\| twice '
                                       '([sub](../reverse-engineering/sub.md#function-table))')
        self.assertEqual(rows[0x401038][3], '3')
        self.assertEqual(rows[0x401040][1:4], ['`kernel32.dll!Sleep`', 'RTL', '~1'])
        beta = rows[0x401050]
        self.assertEqual(beta[6], 'group `other`, core missing, compare no')

    def test_sim_column(self):
        sim = dict(rows={'401040': dict(name='thing', fragment='f.h', source='s.c'),
                         '401050': dict(name='b', fragment='g.h', source='')},
                   gap={'401020': dict(kind='called', root='401020', depth=0),
                        '401030': dict(kind='behind', root='401020', depth=1),
                        '401016': dict(kind='vbehind', root='401010', depth=2),
                        '401010': dict(kind='virtual', root='401010', depth=0)},
                   referenced=['401000', '401040'])
        path = self.root / 'logs/sim_gap.json'
        path.write_bytes(json.dumps(sim).encode('utf-8'))
        self.run_index(extra=['--sim', str(path)])
        text, rows = self.page_rows()
        self.assertEqual(rows[0x401040][7], '`thing` (f.h; s.c)')
        self.assertEqual(rows[0x401050][7], '`b` (g.h)')
        self.assertEqual(rows[0x401020][7], 'gap: called')
        self.assertEqual(rows[0x401030][7], 'gap: behind 401020')
        self.assertEqual(rows[0x401010][7], 'gap: virtual')
        self.assertEqual(rows[0x401000][7], 'gap: referenced only')
        # An entry with nothing but a Sim cell is listed (0x401016 is a plain instruction start).
        self.assertEqual(rows[0x401016][7], 'gap: virtual behind 401010')
        self.assertIn('**Sim** the own process: ', text)
        self.assertIn('[sim_gap.md](../reference-game/sim_gap.md)', text)
        idx = (self.root / 'docs/functions/index.md').read_text(encoding='utf-8')
        self.assertIn('- Own process: `sim_gap.json` (7 functions', idx)

    def test_index_cross_check(self):
        out = self.run_index()
        self.assertIn('map rows without listing entry: 1', out)
        idx = (self.root / 'docs/functions/index.md').read_text(encoding='utf-8')
        self.assertIn('- Map rows without an instruction start in the listing: 1: `401038`.', idx)
        self.assertIn('VAs without an instruction start in the listing: 1: `401024`.', idx)
        self.assertIn('inside a decoded instruction (call decoded from data, or a target behind '
                      'a lost boundary; not listed as entries): 1: `401041`.', idx)
        self.assertIn('  - hooks (the map has rows only for unreplaced functions): 2', idx)
        self.assertIn('  - import jump thunks: 1', idx)
        unresolved = idx.split('## Hooks without')[1]
        self.assertIn('1 hooks miss one of them', unresolved)
        self.assertIn('`beta` 401050 (core/contract)', unresolved)
        self.assertNotIn('`alpha`', unresolved)
        self.assertNotIn('SHA-256', idx)
        self.assertIn('[reference_version.md](../reference_version.md)', idx)

    def test_area_report(self):
        """The area column and the index page's report of the module map."""
        out = self.run_index()
        self.assertIn('area boundaries without an entry: ', out)
        self.assertIn('entries in no area range: 0', out)
        idx = (self.root / 'docs/functions/index.md').read_text(encoding='utf-8')
        section = idx.split('## Areas')[1].split('## Cross-check')[0]
        self.assertIn('(the same table as [dbsim_gap.md](../reference-game/dbsim_gap.md#1-method)',
                      section)
        # All eight entries lie in RTL (0x401000..0x401078): 8 entries, 8 listed, the hooks
        # alpha and beta, the two map rows.
        rtl = next(line for line in section.splitlines() if line.startswith('| RTL |'))
        cells = [c.strip() for c in rtl.strip('|').split('|')]
        self.assertEqual(cells[0], 'RTL')
        self.assertTrue(cells[1].startswith('401000–401077, '))
        self.assertEqual(cells[2:7], ['8', '8', '2', '2', cells[6]])
        self.assertIn('| – (no range) | the gaps below | 0 | 0 | 0 | 0 | 0 |', section)
        # Direct calls: 0x401000 -> alpha, 0x401020; alpha -> thunk; 0x401020 -> 0x401030.
        matrix = section.split('### Direct calls between areas')[1].split('###')[0].splitlines()
        header = [c.strip() for c in next(l for l in matrix if l.startswith('| caller')).strip('|').split('|')]
        row = next(l for l in matrix if l.startswith('| RTL |'))
        values = [c.strip() for c in row.strip('|').split('|')]
        self.assertEqual(values[header.index('RTL')], '4')
        self.assertFalse(any(l.startswith('| SIM |') for l in matrix))
        # The boundary RTL -> R3D at 0x401078: the last entry below is beta, nothing above.
        self.assertIn('| 401078 | RTL → R3D | no | 401050 (RTL; 0x28 below; callers none) | none |',
                      section)
        self.assertIn('| 401d64–401dbf | 0 | 0 | 0 | none | none |', section)

    def test_area_column_is_empty_outside_the_map(self):
        # beta (0x401050) calls a function at 0x401d70, inside the gap 0x401d64..0x401dc0.
        (self.root / 'build/dbsim.lst').write_text(
            listing_text([(0x401060, 5, 'call', '0x401d70 <CODE+0xd70>'), (0x401d70, 1, 'ret', '')]),
            encoding='utf-8')
        out = self.run_index()
        self.assertIn('entries in no area range: 1', out)
        _, rows = self.page_rows()
        self.assertEqual(rows[0x401d70][2], '')
        self.assertEqual(rows[0x401050][2], 'RTL')
        idx = (self.root / 'docs/functions/index.md').read_text(encoding='utf-8')
        self.assertIn('| 401d64–401dbf | 1 | 0 | 1 | RTL 1 | none |', idx)

    def test_deterministic_lf_and_no_instruction_text(self):
        self.run_index('a')
        self.run_index('b')
        names = sorted(p.name for p in (self.root / 'a').iterdir())
        self.assertEqual(names, ['dbsim-400000.md', 'index.md'])
        for name in names:
            data = (self.root / 'a' / name).read_bytes()
            self.assertEqual(data, (self.root / 'b' / name).read_bytes())
            self.assertNotIn(b'\r', data)
            for word in (b'ebp', b'ebx', b'dword ptr', b'cc cc'):
                self.assertNotIn(word, data)

    def test_without_image(self):
        self.run_index(image=False)
        _, rows = self.page_rows()
        self.assertEqual(rows[0x401040][1], '')
        idx = (self.root / 'docs/functions/index.md').read_text(encoding='utf-8')
        self.assertNotIn('SHA-256', idx)

    def test_helpers(self):
        seen = {}
        self.assertEqual(fi.slug('Section 10: Graphics (draw)', seen), 'section-10-graphics-draw')
        self.assertEqual(fi.slug('Section 10: Graphics (draw)', seen), 'section-10-graphics-draw-1')
        self.assertEqual(fi.slug('`clip_pose` 478ef4 → [doc](x.md)', {}), 'clip_pose-478ef4--doc')
        self.assertEqual(fi.short('a [b](c) `x y` ' + 'z' * 200, 12), 'a b `x y` zz…')
        self.assertEqual(fi.short('one `two three`', 9), 'one `two…`')
        classified = fi.read_classified(self.root / 'docs/reference-game/function_verdicts.md')
        self.assertEqual(sorted(classified), [0x401020, 0x401024, 0x401030])
        with self.assertRaises(ValueError):
            fi.image_info(self.root / 'build/dbsim.lst')
        bad = self.root / 'build/bad.lst'
        bad.write_bytes(b'  401001: 90 \tnop\n  401000: 90 \tnop\n')
        with self.assertRaises(ValueError):
            fi.Listing(bad)


if __name__ == '__main__':
    unittest.main()
