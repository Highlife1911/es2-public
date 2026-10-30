"""Import inventory of one DLL in a PE32 executable: IAT slots, jump thunks,
call sites and the functions that own them (CW3220 imports of DBSIM).

    python rtl_imports.py DBSIM.EXE                      # Markdown table per import
    python rtl_imports.py DBSIM.EXE --sites              # one row per call site
    python rtl_imports.py DBSIM.EXE --map <map dir>      # + owner entry counts of the DBSIM map
    python rtl_imports.py DBSIM.EXE --json
    python rtl_imports.py DBSIM.EXE --check docs/reference-game/rtl_imports.md

What is scanned (byte level, CODE sections only, no disassembler):

- IAT slots of the chosen DLL (`--dll`, default `cw3220.dll`, case-insensitive)
  from the import directory; ordinal imports are listed as `#<n>`.
- Thunks: `FF 25 <slot>` (`jmp dword [slot]`).
- Sites: `E8`/`E9` rel32 whose target is a thunk (`call`/`jmp`), `FF 15 <slot>`
  (`call dword [slot]`), and every other dword in CODE equal to a thunk or slot
  address (`imm`: data imports such as `__streams` that the linker resolved to
  the thunk, or handler addresses stored as immediates).
- Owners: the nearest function entry at or below the site. Entries are the
  `55 8B EC` prologues, and the `E8` call targets, dwords of the non-code
  sections and `push`/`mov` immediates in CODE that point into CODE; each
  only when the byte before it is padding or a return (`90`, `C3`, `CC`,
  `00`). Add the PE entry point and, with `--map`, the entries of the
  DBSIM map (`cfg.json`). Jump thunks are never owners. Byte scans can
  still see a false entry inside data that lies in CODE (6 of the 555
  DBSIM sites); the owner is a hint, confirm it in the llvm-objdump
  listing.

With `--map`, owners get the six-suite entry counts of `map.json` (rows
exist only for live, unreplaced entries) and the hook name from the
registry (`--registry`, default the repository's `hook_registry.h`).

`--check DOC` reads the first Markdown table in DOC whose header has the
columns "Import" and "Sites" (import names in backticks) and compares the
Sites column with the scan (`call`+`jmp`+`call [IAT]`; for imports without
call sites, the number of `imm` references written as `imm N`). It lists
missing, extra and differing imports; exit status 1 if any. Read-only,
standard library only.
"""
import argparse
import bisect
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

REFERENCE_SHA256 = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
REGISTRY = Path(__file__).resolve().parents[1] / 'proxy' / 'hook_registry.h'
SUITES = ['acceptance-60s', 'c1_01-success', 'c1_01-collision', 'c3_09-loadout-b',
          'c4_08-razor-plasma', 'c3_10-loadout-hercs']
PADDING = (0x90, 0xC3, 0xCC, 0x00)
CODE_FLAG = 0x20


class PEError(Exception):
    pass


class Image:
    """The parts of a PE32 file that the scan needs, with bounds checks."""

    def __init__(self, data):
        self.data = data
        if len(data) < 0x40 or data[:2] != b'MZ':
            raise PEError('not an MZ file')
        pe = self.u32_at(0x3C)
        if data[pe:pe + 4] != b'PE\0\0':
            raise PEError('no PE signature')
        count = self.u16_at(pe + 6)
        optional_size = self.u16_at(pe + 20)
        optional = pe + 24
        if self.u16_at(optional) != 0x10B:
            raise PEError('not PE32')
        self.entry = self.u32_at(optional + 16)
        self.base = self.u32_at(optional + 28)
        self.import_rva = self.u32_at(optional + 104)
        self.sections = []
        for i in range(count):
            s = optional + optional_size + 40 * i
            name = data[s:s + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
            flags = self.u32_at(s + 36)
            self.sections.append((name, rva, vsize, raw, rsize, flags))
        self.entry += self.base

    def u16_at(self, offset):
        if offset < 0 or offset + 2 > len(self.data):
            raise PEError(f'read past the end at {offset:#x}')
        return struct.unpack_from('<H', self.data, offset)[0]

    def u32_at(self, offset):
        if offset < 0 or offset + 4 > len(self.data):
            raise PEError(f'read past the end at {offset:#x}')
        return struct.unpack_from('<I', self.data, offset)[0]

    def offset(self, rva):
        for _, start, vsize, raw, rsize, _ in self.sections:
            if start <= rva < start + max(vsize, rsize):
                if rva - start >= rsize:
                    raise PEError(f'RVA {rva:#x} has no file data')
                return raw + rva - start
        raise PEError(f'unmapped RVA {rva:#x}')

    def cstring(self, rva):
        start = self.offset(rva)
        end = self.data.find(b'\0', start)
        if end < 0:
            raise PEError(f'unterminated string at RVA {rva:#x}')
        return self.data[start:end].decode('ascii', 'replace')

    def code(self):
        """[(va, bytes)] of the executable sections."""
        return [(self.base + rva, self.data[raw:raw + rsize])
                for _, rva, _, raw, rsize, flags in self.sections if flags & CODE_FLAG]


def import_slots(image, dll):
    """{slot VA: symbol} of the imports from `dll` (case-insensitive)."""
    slots = {}
    if not image.import_rva:
        return slots
    cursor = image.offset(image.import_rva)
    while True:
        lookup, _, _, name, first = struct.unpack_from('<IIIII', image.data, cursor)
        if not name:
            return slots
        if image.cstring(name).lower() == dll.lower():
            table = image.offset(lookup or first)
            for i in range(1 << 16):
                value = image.u32_at(table + 4 * i)
                if not value:
                    break
                symbol = f'#{value & 0xFFFF}' if value & 0x80000000 else image.cstring(value + 2)
                slots[image.base + first + 4 * i] = symbol
        cursor += 20


def scan(image, dll, extra_entries=()):
    """Inventory dict: imports {symbol: {...}}, entries (sorted list)."""
    slots = import_slots(image, dll)
    code = image.code()
    thunks = {}
    for va, blob in code:
        for i in range(len(blob) - 5):
            if blob[i] == 0xFF and blob[i + 1] == 0x25:
                slot = struct.unpack_from('<I', blob, i + 2)[0]
                if slot in slots:
                    thunks[va + i] = slot
    entries = set(extra_entries) | {image.entry}
    candidates = set()
    sites = {symbol: [] for symbol in slots.values()}
    for va, blob in code:
        for i in range(len(blob) - 4):
            b = blob[i]
            if b in (0xE8, 0xE9):
                target = (va + i + 5 + struct.unpack_from('<i', blob, i + 1)[0]) & 0xFFFFFFFF
                if target in thunks:
                    sites[slots[thunks[target]]].append((va + i, 'call' if b == 0xE8 else 'jmp'))
                elif b == 0xE8:
                    candidates.add(target)
            if b == 0xFF and blob[i + 1] == 0x15 and i + 6 <= len(blob):
                slot = struct.unpack_from('<I', blob, i + 2)[0]
                if slot in slots:
                    sites[slots[slot]].append((va + i, 'call [IAT]'))
            if blob[i:i + 3] == b'\x55\x8b\xec' and (i == 0 or blob[i - 1] in PADDING):
                entries.add(va + i)
            value = struct.unpack_from('<I', blob, i)[0]
            is_operand = i >= 2 and blob[i - 2] == 0xFF and blob[i - 1] in (0x15, 0x25)
            if value in thunks and not is_operand:
                sites[slots[thunks[value]]].append((va + i, 'imm'))
            elif value in slots and not is_operand:
                sites[slots[value]].append((va + i, 'imm'))
            if i >= 1 and (blob[i - 1] == 0x68 or 0xB8 <= blob[i - 1] <= 0xBF
                           or any(i >= k and blob[i - k] == 0xC7 for k in (2, 3, 6))):
                candidates.add(value)   # push imm32, mov r32/r/m32, imm32
    for _, _, _, raw, rsize, flags in image.sections:
        if not flags & CODE_FLAG:   # function pointers in data; CODE dwords are jump tables
            blob = image.data[raw:raw + rsize]
            candidates.update(struct.unpack_from('<I', blob, i)[0] for i in range(len(blob) - 3))
    for va, blob in code:
        entries.update(t for t in candidates if va < t < va + len(blob) and blob[t - va - 1] in PADDING)
        # jump thunks of every DLL (FF 25) are not owners
        entries -= {t for t in entries if va <= t < va + len(blob) - 1 and blob[t - va:t - va + 2] == b'\xff\x25'}
    ordered = sorted(entries)
    imports = {}
    by_slot = {}
    for thunk, slot in thunks.items():
        by_slot.setdefault(slot, []).append(thunk)
    for slot, symbol in sorted(slots.items()):
        rows = []
        for site, kind in sorted(sites[symbol]):
            k = bisect.bisect_right(ordered, site) - 1
            rows.append({'site': site, 'kind': kind, 'owner': ordered[k] if k >= 0 else None})
        imports[symbol] = {'slot': slot, 'thunks': sorted(by_slot.get(slot, [])), 'sites': rows}
    return {'imports': imports, 'entries': ordered}


def call_count(record):
    return sum(1 for s in record['sites'] if s['kind'] != 'imm')


def imm_count(record):
    return sum(1 for s in record['sites'] if s['kind'] == 'imm')


def load_map(directory):
    """(counts {va: [six counts]}, entries {va}) from a DBSIM map directory."""
    directory = Path(directory)
    counts, entries = {}, set()
    data = json.loads((directory / 'map.json').read_text(encoding='utf-8'))
    for row in data.get('rows', []):
        counts[int(row['va'], 16)] = [row['counts'].get(s, 0) for s in SUITES]
    cfg = directory / 'cfg.json'
    if cfg.exists():
        entries = {int(k, 16) for k in json.loads(cfg.read_text(encoding='utf-8'))}
    return counts, entries


def load_hooks(path):
    """{VA: hook name} from hook_registry.h (RVAs at image base 0x400000)."""
    hooks = {}
    if path and Path(path).exists():
        for m in re.finditer(r'ES2_HOOK\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,', Path(path).read_text(encoding='utf-8')):
            hooks[0x400000 + int(m.group(2), 16)] = m.group(1)
    return hooks


def doc_sites(path):
    """{import: sites text} from the first table with columns Import and Sites."""
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    for i, line in enumerate(lines):
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if line.lstrip().startswith('|') and 'Import' in cells and 'Sites' in cells:
            col_import, col_sites = cells.index('Import'), cells.index('Sites')
            rows = {}
            for row in lines[i + 2:]:
                if not row.lstrip().startswith('|'):
                    break
                c = [x.strip() for x in row.strip().strip('|').split('|')]
                m = re.fullmatch(r'`([^`]+)`', c[col_import]) if len(c) > col_import else None
                if m and len(c) > col_sites:
                    rows[m.group(1)] = c[col_sites]
            return rows
    raise ValueError(f'no table with columns Import and Sites in {path}')


def check(result, path):
    expected = doc_sites(path)
    problems = []
    for symbol, record in result['imports'].items():
        n = call_count(record)
        want = str(n) if n else f'imm {imm_count(record)}'
        got = expected.get(symbol)
        if got is None:
            problems.append(f'missing in {path}: {symbol} ({want})')
        elif got != want:
            problems.append(f'{symbol}: document {got}, binary {want}')
    for symbol in sorted(set(expected) - set(result['imports'])):
        problems.append(f'not imported: {symbol}')
    return problems


def owner_text(owner, counts, hooks):
    if owner is None:
        return '-'
    text = f'{owner:#x}'
    if owner in hooks:
        text += f' ({hooks[owner]})'
    if owner in counts:
        text += ' [' + '/'.join(str(c) for c in counts[owner]) + ']'
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('exe')
    parser.add_argument('--dll', default='cw3220.dll')
    parser.add_argument('--map', help='DBSIM map directory (map.json, cfg.json)')
    parser.add_argument('--registry', default=str(REGISTRY))
    parser.add_argument('--sites', action='store_true', help='one row per site')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--check', metavar='DOC')
    args = parser.parse_args(argv)
    data = Path(args.exe).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    counts, map_entries = load_map(args.map) if args.map else ({}, set())
    hooks = load_hooks(args.registry)
    try:
        result = scan(Image(data), args.dll, map_entries)
    except PEError as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    if digest != REFERENCE_SHA256:
        print(f'note: SHA-256 {digest} is not the reference DBSIM.EXE', file=sys.stderr)
    if args.check:
        problems = check(result, args.check)
        for p in problems:
            print(p)
        print(f'{len(result["imports"])} imports checked, {len(problems)} problem(s)')
        return 1 if problems else 0
    imports = result['imports']
    if args.json:
        out = {s: {'slot': f'{r["slot"]:#x}', 'thunks': [f'{t:#x}' for t in r['thunks']],
                   'sites': [{'site': f'{x["site"]:#x}', 'kind': x['kind'],
                              'owner': None if x['owner'] is None else f'{x["owner"]:#x}',
                              'hook': hooks.get(x['owner']), 'counts': counts.get(x['owner'])}
                             for x in r['sites']]} for s, r in imports.items()}
        print(json.dumps({'sha256': digest, 'dll': args.dll, 'imports': out}, indent=1))
        return 0
    if args.sites:
        print('| Import | Site | Kind | Owner |')
        print('| --- | --- | --- | --- |')
        for symbol, record in imports.items():
            for x in record['sites']:
                print(f'| `{symbol}` | `{x["site"]:#x}` | {x["kind"]} | {owner_text(x["owner"], counts, hooks)} |')
        return 0
    print('| Import | Slot | Thunk | Sites | imm | Owners |')
    print('| --- | --- | --- | ---: | ---: | ---: |')
    for symbol, record in sorted(imports.items(), key=lambda kv: (-call_count(kv[1]), kv[0])):
        n = call_count(record)
        owners = {x['owner'] for x in record['sites']}
        thunk = ', '.join(f'`{t:#x}`' for t in record['thunks']) or '-'
        print(f'| `{symbol}` | `{record["slot"]:#x}` | {thunk} | {n if n else "imm " + str(imm_count(record))} '
              f'| {imm_count(record)} | {len(owners)} |')
    total = sum(call_count(r) for r in imports.values())
    print(f'\n{len(imports)} imports from {args.dll}, {total} call sites; SHA-256 {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
