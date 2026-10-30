"""Generate the function index `docs/functions/` of DBSIM.EXE.

Joins six sources into one Markdown table per 64 KiB address range plus an
index page:

- the `llvm-objdump -d -M intel` listing of the reference DBSIM.EXE
  (`--listing`, produced at generation time, never committed): function
  entries, instruction counts, direct callers and callees;
- the hook registry `hook_registry.h` (`--registry`) with the adapter
  headers next to it (`<group>_hooks.h`: the descriptor array and the
  specification document) and the registry table of `hooks.md`
  (`--hooks-doc`: compare capability, core module);
- the core library (`--core`: `include/es2/core/*.h`, `src/*.c`) for the
  core function that implements a hook;
- the verdicts of `function_verdicts.md` (`--classified`) and the function
  tables of the subsystem documents (`--docs`, every table with a "Role"
  column) for roles and contract links;
- the own process's status (`--sim`, the JSON of `sim_gap.py --json`):
  the service row of a function (name, fragment, implementing source file)
  and the kind of gap an original function without row or hook is in;
- live counts: the DBSIM map (`--map`, `map.json`: saturated entry probes
  per suite for live, unreplaced functions) and the hook counters of a
  regression (`--counters`, `hook-counters.json` of
  `run-oracle-parallel.ps1`).

With `--image` (the same DBSIM.EXE) the executable's SHA-256 is recorded and
import jump thunks get their import names. The output contains addresses,
sizes, names, call relations and our own descriptions only: no instruction
text and no code bytes.

Every entry gets an area from `areas.py`, the module map of
docs/reference-game/dbsim_gap.md section 1, and the index page checks the map
against the listing: entries and hooks per area, the direct calls between
areas, the range boundaries (is the boundary a function entry, who calls the
entries on both sides) and the gaps that no range covers.

Function entries are the direct `call` targets of the listing, its symbol
labels (exports), the registry, the map rows with their callers and
callees and the classified VAs, each only where the linear listing has an
instruction start. A function's instruction count is the map's count (from
the synchronized descent of the probe inventory) where the map has a row,
otherwise `~N`, the linear listing instructions up to the next known entry
without trailing `nop`/`int3` padding. Callers and callees are direct
`call rel32` sites of the listing only (no tail jumps, vtables or
callbacks). The cross-check lists map entries without
a listing entry and listing entries the map does not know.

    python function_index.py --listing dbsim.lst --image DBSIM.EXE \
        --registry tools/reference-game/proxy/hook_registry.h \
        --hooks-doc docs/reference-game/hooks.md \
        --classified docs/reference-game/function_verdicts.md \
        --docs docs/reverse-engineering --core core \
        --map .../dbsim-map-<stamp>/map.json \
        --counters .../check-<run>/hook-counters.json \
        --sim build/function-docs/sim_gap.json \
        --output docs/functions

Standard library only; output is deterministic and LF-terminated.
"""
import argparse
import bisect
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import areas  # the module map of dbsim_gap.md section 1

IMAGE_BASE = 0x400000
PAGE_BITS = 16
SUITES = ['acceptance-60s', 'c1_01-success', 'c1_01-collision',
          'c3_09-loadout-b', 'c4_08-razor-plasma', 'c3_10-loadout-hercs']
SUITE_SHORT = 'a60/c1s/c1c/c3_09/c4_08/c3_10'
VERDICTS = ('H4', 'in-progress', 'H3', 'platform', 'RTL', 'dead')
PADDING = frozenset(('nop', 'int3'))
MAX_LIST = 6
MAX_ROLE = 110

INSTRUCTION = re.compile(r'^\s+([0-9a-f]+):\s(?:[0-9a-f]{2} )+\s*\t(\S+)(?:\t(.*))?$')
SYMBOL = re.compile(r'^([0-9a-f]+) <(.+)>:$')
SECTION = re.compile(r'^Disassembly of section (\S+):$')
DIRECT = re.compile(r'0x([0-9a-f]+)\b')
THUNK = re.compile(r'dword ptr \[0x([0-9a-f]+)\]$')
VA_TOKEN = re.compile(r'(?<![0-9A-Za-z_])(?:0x)?(4[0-9a-fA-F]{5})(?![0-9A-Za-z_])')
LINK = re.compile(r'\[([^\]]*)\]\([^)]*\)')


def split_row(line):
    """Cells of a Markdown table row; `\\|` stays inside a cell (unescaped)."""
    return [c.strip().replace('\\|', '|')
            for c in re.split(r'(?<!\\)\|', line.strip().strip('|'))]


def md_cell(text):
    return text.replace('|', '\\|').replace('\n', ' ')


def short(text, limit=MAX_ROLE):
    """Plain table text: links reduced to their text, cut at `limit` characters."""
    text = LINK.sub(r'\1', text).replace('**', '').strip()
    if len(text) > limit:
        text = text[:limit].rstrip() + '…'
        if text.count('`') % 2:
            text += '`'
    return md_cell(text)


def slug(heading, seen):
    """GitHub's heading anchor; `seen` counts earlier anchors of the same document."""
    text = LINK.sub(r'\1', heading).strip().lower()
    text = re.sub(r'[^\w\- ]', '', text).replace(' ', '-')
    n = seen.get(text, 0)
    seen[text] = n + 1
    return text if n == 0 else f'{text}-{n}'


def rel(path, start):
    return os.path.relpath(path, start).replace(os.sep, '/')


# --------------------------------------------------------------------------- listing

class Listing:
    """Instruction starts, symbols and direct calls of an llvm-objdump listing."""

    def __init__(self, path):
        self.addresses, self.mnemonics, self.thunk_slots = [], [], {}
        self.symbols, self.calls = {}, []  # calls: (site, target)
        sections = set()
        last = -1
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.rstrip('\r\n')
                m = INSTRUCTION.match(line)
                if m:
                    va = int(m.group(1), 16)
                    if va <= last:
                        raise ValueError(f'{path}: addresses not ascending at {va:#x}')
                    last = va
                    mnemonic, operands = m.group(2), (m.group(3) or '').split(' <')[0].strip()
                    self.addresses.append(va)
                    self.mnemonics.append(mnemonic)
                    if mnemonic in ('call', 'calll'):
                        t = DIRECT.fullmatch(operands)
                        if t:
                            self.calls.append((va, int(t.group(1), 16)))
                    elif mnemonic in ('jmp', 'jmpl'):
                        t = THUNK.search(operands)
                        if t:
                            self.thunk_slots[va] = int(t.group(1), 16)
                    continue
                m = SECTION.match(line)
                if m:
                    sections.add(m.group(1))
                    continue
                m = SYMBOL.match(line)
                if m:
                    self.symbols[int(m.group(1), 16)] = m.group(2)
        self.symbols = {va: name for va, name in self.symbols.items() if name not in sections}
        self.index = {va: i for i, va in enumerate(self.addresses)}
        if not self.addresses:
            raise ValueError(f'{path}: no instructions')

    def is_start(self, va):
        return va in self.index

    def linear_count(self, start, end):
        """Instructions in [start, end) without trailing padding."""
        i = self.index[start]
        j = bisect.bisect_left(self.addresses, end)
        while j > i + 1 and self.mnemonics[j - 1] in PADDING:
            j -= 1
        return j - i


# --------------------------------------------------------------------------- PE imports

def image_info(path):
    """(SHA-256, {IAT slot VA: 'dll!symbol'}) of a PE32 file; bounds-checked."""
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()

    def u16(off):
        if off < 0 or off + 2 > len(data):
            raise ValueError(f'{path}: read past the end at {off:#x}')
        return struct.unpack_from('<H', data, off)[0]

    def u32(off):
        if off < 0 or off + 4 > len(data):
            raise ValueError(f'{path}: read past the end at {off:#x}')
        return struct.unpack_from('<I', data, off)[0]

    if data[:2] != b'MZ':
        raise ValueError(f'{path}: no MZ header')
    pe = u32(0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'{path}: no PE signature')
    count, opt_size = u16(pe + 6), u16(pe + 20)
    opt = pe + 24
    if u16(opt) != 0x10b:
        raise ValueError(f'{path}: not PE32')
    base, import_rva = u32(opt + 28), u32(opt + 104)
    sections = []
    for i in range(count):
        s = opt + opt_size + 40 * i
        sections.append((u32(s + 12), max(u32(s + 8), u32(s + 16)), u32(s + 20), u32(s + 16)))

    def offset(rva):
        for start, size, raw, raw_size in sections:
            if start <= rva < start + size and rva - start < raw_size:
                return raw + rva - start
        raise ValueError(f'{path}: unmapped RVA {rva:#x}')

    def cstring(rva):
        start = offset(rva)
        end = data.find(b'\0', start)
        if end < 0:
            raise ValueError(f'{path}: unterminated string at RVA {rva:#x}')
        return data[start:end].decode('ascii', 'replace')

    slots = {}
    if import_rva:
        cursor = offset(import_rva)
        for _ in range(4096):
            lookup, name, first = u32(cursor), u32(cursor + 12), u32(cursor + 16)
            if not name:
                break
            dll = cstring(name).lower()
            table = offset(lookup or first)
            for i in range(1 << 16):
                value = u32(table + 4 * i)
                if not value:
                    break
                symbol = f'#{value & 0xffff}' if value & 0x80000000 else cstring(value + 2)
                slots[base + first + 4 * i] = f'{dll}!{symbol}'
            cursor += 20
    return digest, slots


# --------------------------------------------------------------------------- registry and docs

def read_registry(path):
    """[(name, va, descriptor array, group)] in registry order."""
    text = Path(path).read_text(encoding='utf-8')
    out = []
    for m in re.finditer(r'ES2_HOOK\((\w+),\s*(0x[0-9a-fA-F]+),\s*([^,]+?),\s*[^,]+?,\s*(\w+)\)', text):
        array = re.sub(r'\[.*$', '', m.group(3).strip()).lstrip('&')
        out.append((m.group(1), IMAGE_BASE + int(m.group(2), 16), array, m.group(4)))
    if not out:
        raise ValueError(f'{path}: no ES2_HOOK entries')
    return out


def adapter_headers(directory, arrays):
    """{descriptor array: (header path, specification document or None)}."""
    out = {}
    for path in sorted(Path(directory).glob('*_hooks.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        head = '\n'.join(text.splitlines()[:12])
        spec = re.search(r'(docs/[\w/.-]+\.md)', head)
        for array in arrays:
            if array not in out and re.search(r'\b' + re.escape(array) + r'\s*\[\s*\]\s*=', text):
                out[array] = (path, spec.group(1) if spec else None)
    return out


def read_hooks_doc(path):
    """{hook: (core module, compare)} from the registry table of hooks.md."""
    out = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        m = re.match(r'\|\s*`(\w+)`\s*\|\s*([0-9a-f]{6})\s*\|([^|]*)\|([^|]*)\|', line)
        if m:
            out[m.group(1)] = (m.group(3).strip(), m.group(4).strip())
    return out


def read_classified(path):
    """VA -> (verdict, reason) from every table of function_verdicts.md whose
    first cell holds VAs and whose second cell is a verdict; the first row of
    a VA wins (the Verdicts section comes first)."""
    out = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.startswith('|'):
            continue
        cells = split_row(line)
        if len(cells) < 4:
            continue
        verdict = cells[1].strip('*').strip()
        if verdict not in VERDICTS:
            continue
        vas = re.findall(r'`0x([0-9a-fA-F]+)`', cells[0])
        for va in vas:
            out.setdefault(int(va, 16), (verdict, '|'.join(cells[3:]).strip()))
    return out


def read_doc_tables(paths):
    """Roles and headings of the subsystem documents.

    Returns ({VA: (role, doc path, anchor)}, [[doc path, heading, anchor,
    section text, first cells of its table rows]]); a section runs to the
    next heading.
    A role comes from a table whose header has a column starting with
    "Role"; the row's first cell names the VA(s). The first document (sorted)
    and row wins.
    """
    roles, headings = {}, []
    for path in paths:
        seen, anchor, role_col, in_code = {}, '', None, False
        section = [path, '', '', '', '']
        for line in Path(path).read_text(encoding='utf-8').splitlines():
            if line.startswith('```'):
                in_code = not in_code
                continue
            if in_code:
                section[3] += line + '\n'
                continue
            m = re.match(r'(#{1,6})\s+(.*?)\s*#*\s*$', line)
            if m:
                anchor = slug(m.group(2), seen)
                section = [path, m.group(2), anchor, '', '']
                headings.append(section)
                role_col = None
                continue
            section[3] += line + '\n'
            if not line.startswith('|'):
                role_col = None
                continue
            cells = split_row(line)
            section[4] += cells[0] + '\n'
            if role_col is None:
                for i, c in enumerate(cells):
                    if i and c.lower().startswith('role'):
                        role_col = i
                        break
                else:
                    role_col = -1
                continue
            if role_col < 0 or role_col >= len(cells) or set(cells[0]) <= set('-: '):
                continue
            for va in VA_TOKEN.findall(LINK.sub(r'\1', cells[0])):
                roles.setdefault(int(va, 16), (cells[role_col], path, anchor))
    return roles, headings


def core_functions(core):
    """({es2_ function name: defining core/src file}, {VA: [function names]}).

    Only functions declared in a core header count. The second map lists,
    per VA, the declarations whose leading comment block names that VA."""
    defined, declared, by_va = {}, {}, {}
    for path in sorted(Path(core, 'src').glob('*.c')):
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            m = re.match(r'^[A-Za-z_][\w \*]*?\b(es2_\w+)\s*\(', line)
            if m and not line.rstrip().endswith(';'):
                defined.setdefault(m.group(1), path)
    for path in sorted(Path(core, 'include', 'es2', 'core').glob('*.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        for m in re.finditer(r'\b(es2_\w+)\s*\(', text):
            declared.setdefault(m.group(1), path)
        comment = set()
        for line in text.splitlines():
            m = re.match(r'^[A-Za-z_][\w \*]*?\b(es2_\w+)\s*\(', line)
            if m:
                for va in sorted(comment):
                    by_va.setdefault(va, []).append(m.group(1))
                comment = set()
            elif line.lstrip().startswith(('/*', '*', '//')):
                comment.update(int(v, 16) for v in VA_TOKEN.findall(line))
            elif line.strip():
                comment = set()
    functions = {n: p for n, p in defined.items() if n in declared}
    return functions, {va: [n for n in names if n in functions] for va, names in by_va.items()}


def core_function(hook, va, functions, by_va):
    """The core function of a hook: by name, else the first declaration whose
    leading comment block names the hook's VA."""
    for name in (f'es2_{hook}', f'es2_{hook}_registers'):
        if name in functions:
            return name
    for test in (lambda n: n.startswith(f'es2_{hook}_'), lambda n: n.endswith(f'_{hook}'),
                 lambda n: n.endswith(f'_{hook}_registers')):
        hits = sorted(n for n in functions if test(n))
        if hits:
            return hits[0]
    hits = by_va.get(va)
    return hits[0] if hits else None


def contract_link(hook, va, spec, headings):
    """(doc path, anchor) of the contract section of a hook.

    In order: the first heading naming the hook's VA, the first heading
    naming the hook, the first section of the specification document with
    a table row whose first cell names the VA or the hook, the section whose text names the VA or the hook in
    backticks most often; each pass prefers the group's specification
    document (the adapter header's first `docs/...md` reference). The
    registry section of hooks.md lists every hook and never counts."""
    va_word = re.compile(r'(?<![\w])(?:0x)?0*' + f'{va:x}' + r'(?![\w])', re.IGNORECASE)
    name_word = re.compile(r'(?<![\w])' + re.escape(hook) + r'(?![\w])')
    body_word = re.compile(r'`' + re.escape(hook) + r'[`(]|(?<![\w])(?:0x)?0*' + f'{va:x}'
                           + r'(?![\w])', re.IGNORECASE)
    ordered = sorted(headings, key=lambda h: (spec is None or not str(h[0]).replace(os.sep, '/')
                                              .endswith(spec), str(h[0])))
    ordered = [h for h in ordered if h[2] and not (Path(h[0]).name == 'hooks.md'
                                                   and h[1] == 'Registry')]
    for pattern in (va_word, name_word):
        for path, text, anchor, _, _ in ordered:
            if pattern.search(text.replace('`', '')):
                return path, anchor
    for path, text, anchor, _, firsts in ordered:
        if spec is None or not str(path).replace(os.sep, '/').endswith(spec):
            break  # the specification documents come first
        firsts = firsts.replace('`', '')
        if va_word.search(firsts) or name_word.search(firsts):
            return path, anchor
    for in_spec in (True, False):
        best, hits = None, 0
        for path, text, anchor, body, _ in ordered:
            if in_spec and (spec is None or not str(path).replace(os.sep, '/').endswith(spec)):
                continue
            n = len(body_word.findall(body))
            if anchor and n > hits:
                best, hits = (path, anchor), n
        if best:
            return best
    return None


# --------------------------------------------------------------------------- live data

def read_map(path):
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    rows = {int(r['va'], 16): r for r in data['rows']}
    summary = dict(data.get('summary', {}))
    # Rows of the all-original baseline that no longer run with the hooks replaced.
    summary['superseded_vas'] = {int(r['va'], 16) for r in data.get('superseded', ())}
    return rows, summary


def read_counters(path):
    """{counter: [value per suite]} of the last tick record of every suite."""
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    out = {}
    for i, suite in enumerate(SUITES):
        record = (data['suites'].get(suite) or {}).get('last_record') or {}
        for key, value in record.items():
            out.setdefault(key, [None] * len(SUITES))[i] = value
    return out


def read_sim(path):
    """{va: Sim cell text} from the JSON that `sim_gap.py --json` writes (rows, gap kinds, referenced)."""
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    out = {}
    for va, r in data['rows'].items():
        where = r['fragment'] + ('; ' + r['source'] if r['source'] else '')
        out[int(va, 16)] = f'`{r["name"]}` ({where})'
    for va, g in data['gap'].items():
        kind = g['kind']
        if kind in ('called', 'virtual'):
            out.setdefault(int(va, 16), f'gap: {kind}')
        else:
            out.setdefault(int(va, 16), f'gap: {"virtual " if kind == "vbehind" else ""}behind {g["root"]}')
    for va in data['referenced']:
        out.setdefault(int(va, 16), 'gap: referenced only')
    return out


def counts_text(values):
    return '/'.join('–' if v is None else str(v) for v in values)


# --------------------------------------------------------------------------- generation

def addr_list(vas, names):
    vas = sorted(vas)
    shown = [f'{v:x}' + (f' {names[v]}' if v in names else '') for v in vas[:MAX_LIST]]
    more = f' +{len(vas) - MAX_LIST}' if len(vas) > MAX_LIST else ''
    return md_cell(', '.join(shown) + more)


def area_name(va):
    return areas.area_of(va) or '–'


def area_counts(vas, limit=3):
    """`R3D 3, HUD 1` for the areas of a set of VAs, most frequent first (ties by name)."""
    count = Counter(area_name(v) for v in vas)
    shown = sorted(count.items(), key=lambda kv: (-kv[1], kv[0]))
    text = ', '.join(f'{a} {n}' for a, n in shown[:limit])
    return text + (f', +{len(shown) - limit} more' if len(shown) > limit else '') if shown else 'none'


def area_section(starts, sizes, hooks, rows, row_vas, callees, callers, end_of_listing, gap_link):
    """(Markdown lines, boundaries inside an entry, entries outside every range): the
    index page's area report, the module map of areas.py checked against the listing."""
    start_set = set(starts)
    order = list(areas.AREAS)
    per = {a: dict(ranges=[], entries=0, listed=0, hooks=0, instr=0, live=0) for a in order}
    for lo, hi, a in areas.MODULE_MAP:
        per[a]['ranges'].append(f'{lo:06x}–{hi - 1:06x}')
    for v in starts:
        a = areas.area_of(v)
        if not a:
            continue
        p = per[a]
        p['entries'] += 1
        p['listed'] += v in row_vas
        p['hooks'] += v in hooks
        p['live'] += v in rows
        p['instr'] += sizes.get(v) or 0
    out = ['', '## Areas (module map of dbsim_gap.md section 1)', '',
           'The area of an entry is the module-map range of `areas.py` that contains its address '
           f'(the same table as [dbsim_gap.md]({gap_link}#1-method), checked against it by '
           '`areas.py --check-document` and the unit tests); it is an estimate of the source '
           'module, not of the function\'s role, and the document\'s verdict and caller-majority '
           'overrides are not applied. Instructions are the sum of the **Instr.** column (map '
           'count, else the linear span). Entries: all function entries of the listing, '
           '**Listed** those with a row on a page, **Live** those with a map row.', '',
           '| Area | Ranges | Entries | Listed | Hooks | Live | Instr. |',
           '|---|---|---:|---:|---:|---:|---:|']
    for a in order:
        p = per[a]
        out.append(f'| {a} | {md_cell(", ".join(p["ranges"]))} | {p["entries"]} | {p["listed"]} | '
                   f'{p["hooks"]} | {p["live"]} | {p["instr"]} |')
    outside = [v for v in starts if not areas.area_of(v)]
    out.append(f'| – (no range) | the gaps below | {len(outside)} | '
               f'{sum(1 for v in outside if v in row_vas)} | {sum(1 for v in outside if v in hooks)} | '
               f'{sum(1 for v in outside if v in rows)} | {sum(sizes.get(v) or 0 for v in outside)} |')

    names = order + ['–']
    edges = Counter()
    for owner, targets in callees.items():
        for t in targets:
            edges[(area_name(owner), area_name(t))] += 1
    out += ['', '### Direct calls between areas', '',
            'Distinct caller entry to callee entry pairs of the listing (a hook counts as an '
            'entry); rows are the caller\'s area, columns the callee\'s, `–` is no range. A '
            'large off-diagonal count between two neighbours marks a boundary worth checking.', '',
            '| caller \\ callee | ' + ' | '.join(names) + ' |',
            '|---|' + '---:|' * len(names)]
    for r in names:
        if not any(edges[(r, c)] for c in names):
            continue
        out.append(f'| {r} | ' + ' | '.join(str(edges[(r, c)] or '') for c in names) + ' |')

    boundaries = sorted({b for lo, hi, _ in areas.MODULE_MAP for b in (lo, hi)
                         if areas.area_of(b - 1) != areas.area_of(b)})
    inside = []
    out += ['', '### Range boundaries', '',
            'Every address where the area changes (the start of the code excepted). **Entry** is '
            '`yes` when a function entry of the listing starts exactly there; `no` means no known '
            'entry does: the first function of a module can be reached only through a vtable or '
            'a pointer, or the boundary lies inside the function before it or in data or padding. '
            'The last entry below and the first entry at or above the boundary are shown with '
            'their area, their distance from the boundary and the areas of their direct '
            'callers.', '',
            '| Boundary | Areas | Entry | Last entry below | First entry at or above |',
            '|---|---|---|---|---|']
    for b in boundaries:
        i = bisect.bisect_left(starts, b)
        below = starts[i - 1] if i else None
        above = starts[i] if i < len(starts) else None
        if below is None:
            continue  # the start of the code
        at = b in start_set
        if not at:
            inside.append(b)

        def cell(v):
            if v is None:
                return 'none'
            where = 'at' if v == b else f'{abs(b - v):#x} {"below" if v < b else "above"}'
            return f'{v:x} ({area_name(v)}; {where}; callers {area_counts(callers.get(v, ()))})'
        out.append(f'| {b:06x} | {area_name(b - 1)} → {area_name(b)} | {"yes" if at else "no"} | '
                   f'{cell(below)} | {cell(above)} |')

    gap_list = list(areas.gaps())
    if starts and starts[0] < areas.MODULE_MAP[0][0]:
        gap_list.insert(0, (starts[0], areas.MODULE_MAP[0][0]))
    if end_of_listing > areas.MODULE_MAP[-1][1]:
        gap_list.append((areas.MODULE_MAP[-1][1], end_of_listing))
    out += ['', '### Gaps of the module map', '',
            'Address ranges of the code that no range of the map covers (the document does not '
            'assign them): their entries have no area. The areas of the callers and callees '
            'outside a gap suggest where it belongs; that is a hypothesis, not an assignment.', '',
            '| Gap | Entries | Hooks | Instr. | Callers in | Callees in |',
            '|---|---:|---:|---:|---|---|']
    for lo, hi in gap_list:
        inner = starts[bisect.bisect_left(starts, lo):bisect.bisect_left(starts, hi)]
        ins = set(inner)
        ext_callers = [c for v in inner for c in callers.get(v, ()) if c not in ins]
        ext_callees = [c for v in inner for c in callees.get(v, ()) if c not in ins]
        out.append(f'| {lo:06x}–{hi - 1:06x} | {len(inner)} | {sum(1 for v in inner if v in hooks)} | '
                   f'{sum(sizes.get(v) or 0 for v in inner)} | {area_counts(ext_callers)} | '
                   f'{area_counts(ext_callees)} |')
    return out, inside, outside


def build(args):
    listing = Listing(args.listing)
    out_dir = Path(args.output)
    registry = read_registry(args.registry)
    hooks = {va: name for name, va, _, _ in registry}
    hook_info = {name: (va, array, group) for name, va, array, group in registry}
    headers = adapter_headers(Path(args.registry).parent, sorted({a for _, _, a, _ in registry}))
    hooks_doc = read_hooks_doc(args.hooks_doc) if args.hooks_doc else {}
    classified = read_classified(args.classified) if args.classified else {}
    doc_paths = []
    for d in args.docs or ():
        p = Path(d)
        doc_paths.extend(sorted(p.glob('*.md')) if p.is_dir() else [p])
    if args.hooks_doc:
        doc_paths.append(Path(args.hooks_doc))
    roles, headings = read_doc_tables(doc_paths)
    functions, by_va = core_functions(args.core) if args.core else ({}, {})
    rows, summary = read_map(args.map) if args.map else ({}, {})
    counters = read_counters(args.counters) if args.counters else {}
    sim = read_sim(args.sim) if args.sim else {}
    digest, slots = image_info(args.image) if args.image else (None, {})

    # Entries.
    call_targets = {t for _, t in listing.calls}
    map_known = set(rows)
    for r in rows.values():
        for key in ('callers', 'callees_live', 'callees_unprobed'):
            map_known.update(int(v, 16) for v in r.get(key, ()))
    extra = set(hooks) | map_known | set(classified) | {v for v in sim if listing.is_start(v)}
    end_of_listing = listing.addresses[-1] + 1
    # Known entries count even where the linear sweep lost the instruction
    # boundary (data in CODE); call targets of the listing only at a start.
    starts = sorted({v for v in call_targets | set(listing.symbols) if listing.is_start(v)}
                    | {v for v in extra if listing.addresses[0] <= v < end_of_listing})
    start_set = set(starts)
    bad_targets = sorted({t for t in call_targets if not listing.is_start(t)} - start_set)

    callees, callers = {}, {}
    for site, target in listing.calls:
        i = bisect.bisect_right(starts, site) - 1
        if i < 0:
            continue
        owner = starts[i]
        if target in start_set:
            callees.setdefault(owner, set()).add(target)
            callers.setdefault(target, set()).add(owner)

    names = {}
    for va in starts:
        if va in listing.symbols:
            names[va] = listing.symbols[va]
        if va in listing.thunk_slots and listing.thunk_slots[va] in slots:
            names[va] = slots[listing.thunk_slots[va]]
    for va, name in hooks.items():
        names[va] = name
    short_names = {va: n for va, n in hooks.items()}  # names shown in caller/callee lists

    dead_known = set()
    for r in rows.values():
        live = {int(v, 16) for v in r.get('callees_live', ())}
        unprobed = {int(v, 16) for v in r.get('callees_unprobed', ())}
        for c in callees.get(int(r['va'], 16), ()):
            if c not in live and c not in unprobed and c not in hooks and c not in rows:
                dead_known.add(c)
    unprobed_known = {int(v, 16) for r in rows.values() for v in r.get('callees_unprobed', ())}

    bridged = {}
    for key, values in counters.items():
        m = re.fullmatch(r'hk_\w+?_call_([0-9a-f]{6})', key)
        if m:
            va = int(m.group(1), 16)
            acc = bridged.setdefault(va, [None] * len(SUITES))
            for i, v in enumerate(values):
                if v is not None:
                    acc[i] = (acc[i] or 0) + v

    # Our implementation per hook.
    ours, unresolved = {}, []
    for name, (va, array, group) in hook_info.items():
        header, spec = headers.get(array, (None, None))
        fn = core_function(name, va, functions, by_va)
        module, compare = hooks_doc.get(name, ('', ''))
        parts = [f'group `{group}`']
        if fn:
            parts.append(f'[{functions[fn].name}]({rel(functions[fn], out_dir)}) `{fn}`')
        elif module:
            src = Path(args.core, 'src', module + '.c') if args.core else None
            if src is not None and src.is_file():
                parts.append(f'[{src.name}]({rel(src, out_dir)})')
            else:
                parts.append(f'core {md_cell(module)}')
        if compare:
            parts.append(f'compare {md_cell(compare)}')
        link = contract_link(name, va, spec, headings)
        if link:
            parts.append(f'[contract]({rel(link[0], out_dir)}#{link[1]})')
        elif spec:
            parts.append(f'[spec]({rel(Path(args.registry).parents[3] / spec, out_dir)})')
        if not fn or not link:
            unresolved.append((va, name, fn is None, link is None))
        ours[va] = ', '.join(parts)

    # Rows per page.
    pages = {}
    row_vas = set()  # entries that get a row
    sizes = {}  # entry -> instruction count (map or linear span), None where unknown
    for k, va in enumerate(starts):
        nxt = starts[k + 1] if k + 1 < len(starts) else end_of_listing
        if va in rows and rows[va].get('instructions') is not None:
            sizes[va] = rows[va]['instructions']
            size = str(sizes[va])
        elif listing.is_start(va):
            sizes[va] = listing.linear_count(va, nxt)
            size = f'~{sizes[va]}'
        else:
            sizes[va] = None
            size = '?'
        if va in hooks:
            verdict = 'hook'
        elif va in classified:
            verdict = classified[va][0]
        else:
            verdict = ''
        if va in rows:
            live = '/'.join(str(rows[va]['counts'].get(s, '–')) for s in SUITES)
        elif va in hooks:
            live = 'hook'
        elif va in summary.get('superseded_vas', ()):
            live = 'superseded'
        elif va in dead_known:
            live = '0'
        elif va in unprobed_known:
            live = 'unprobed'
        else:
            live = ''
        key = f'hk_{hooks[va]}_calls' if va in hooks else None
        if key and key in counters:
            hk = counts_text(counters[key])
        elif va in bridged:
            hk = 'via hooks ' + counts_text(bridged[va])
        else:
            hk = ''
        role = ''
        if va in roles:
            text, path, anchor = roles[va]
            role = f'{short(text)} ([{path.stem}]({rel(path, out_dir)}#{anchor}))'
        elif va in classified and classified[va][1]:
            role = short(classified[va][1])
        name = names.get(va, '')
        name = f'`{name}`' if name else ''
        if not args.full and not any((name, verdict, live, hk, role, sim.get(va))) and \
                not callers.get(va) and not callees.get(va):
            continue
        row_vas.add(va)
        pages.setdefault(va >> PAGE_BITS, []).append(
            f'| {va:x} | {md_cell(name)} | {areas.area_of(va)} | {size} | {addr_list(callers.get(va, ()), short_names)} | '
            f'{addr_list(callees.get(va, ()), short_names)} | {ours.get(va, "")} | {md_cell(sim.get(va, ""))} | {verdict} | '
            f'{live} | {hk} | {role} |')

    # Cross-check.
    map_not_start = sorted(v for v in rows if not listing.is_start(v))
    map_only_indirect = sorted(v for v in rows if listing.is_start(v) and v not in call_targets
                               and v not in listing.symbols)
    extra_not_start = sorted(v for v in (set(hooks) | set(classified) | map_known) - set(rows)
                             if not listing.is_start(v))
    no_row = [v for v in starts if v not in rows]
    categories = [
        ('hooks (the map has rows only for unreplaced functions)', [v for v in no_row if v in hooks]),
        ('import jump thunks', [v for v in no_row if v not in hooks and v in listing.thunk_slots]),
        ('probed, never entered (callee of a map row)', [v for v in no_row if v in dead_known
                                                         and v not in hooks]),
        ('unprobed (callee of a map row)', [v for v in no_row if v in unprobed_known
                                            and v not in hooks and v not in dead_known]),
    ]
    listed = set().union(*(set(c) for _, c in categories))
    categories.append(('not in map.json (dead or unprobed; the map lists rows only for live '
                       'functions)', [v for v in no_row if v not in listed]))

    # Write.
    out_dir.mkdir(parents=True, exist_ok=True)
    legend = ('Columns: **VA** entry address (image base `0x400000`); **Name** hook name '
              '(registry), export symbol or import (`dll!symbol`, jump thunks); **Area** module '
              'map of [dbsim_gap.md]({gap}#1-method) (`areas.py`; empty = '
              'in no range, see the index); **Instr.** '
              'instruction count (map descent; `~N` = linear listing span to the next known '
              'entry); **Callers**/**Callees** direct `call rel32` relations of the listing '
              f'(first {MAX_LIST}, `+n` more; hooks named); **Ours** adapter group, core file '
              'and function, compare capability, contract section; **Sim** the own process: '
              'the service row (`name`, fragment, implementing file) or `gap:` the kind of '
              'gap of [sim_gap.md]({simgap}) (called / behind a root / virtual / referenced '
              'only); **Verdict** `hook` or the '
              'verdict of [function_verdicts.md]({cls}); **Map entries** saturated entry probes '
              f'{SUITE_SHORT} of the DBSIM map (1000 = at least 1000; `0` = probed and never '
              'entered; `hook` = replaced, not probed; `superseded` = ran with every hook original, '
              'never with the hooks replaced); **Hook counters** `hk_<hook>_calls` of '
              'the regression\'s last tick record per suite, or for an original function the '
              'sum of the adapters\' `call_<va>` counters ("via hooks"); **Role** from a '
              'subsystem document\'s function table, else the verdict reason. Roles are '
              'hypotheses unless the linked document confirms them.')
    cls = rel(args.classified, out_dir) if args.classified else 'function_verdicts.md'
    gap = (rel(Path(args.classified).parent / 'dbsim_gap.md', out_dir) if args.classified
           else 'dbsim_gap.md')
    simgap = (rel(Path(args.classified).parent / 'sim_gap.md', out_dir) if args.classified else 'sim_gap.md')
    legend = legend.format(cls=cls, gap=gap, simgap=simgap)
    header = ('| VA | Name | Area | Instr. | Callers | Callees | Ours | Sim | Verdict | Map entries | '
              'Hook counters | Role |')
    rule = '|---|---|---|---:|---|---|---|---|---|---|---|---|'
    written = []
    for page in sorted(pages):
        lo, hi = page << PAGE_BITS, ((page + 1) << PAGE_BITS) - 1
        name = f'dbsim-{lo:06x}.md'
        text = [f'# DBSIM.EXE functions {lo:06x}–{hi:06x}', '',
                'Generated by `tools/reference-game/scripts/function_index.py`; do not edit. '
                'It goes stale quickly; regenerate it before relying on a row ([README.md](README.md#regenerating)). '
                'Sources and cross-check: [index](index.md).', '', legend, '', header, rule]
        text.extend(pages[page])
        (out_dir / name).write_bytes(('\n'.join(text) + '\n').encode('utf-8'))
        written.append((name, lo, hi, len(pages[page])))

    def va_line(vas, limit=None):
        shown = vas if limit is None else vas[:limit]
        tail = f' … (+{len(vas) - len(shown)})' if len(shown) < len(vas) else ''
        return ', '.join(f'`{v:x}`' for v in shown) + tail if vas else 'none'

    total_rows = sum(n for _, _, _, n in written)
    idx = ['# DBSIM.EXE function index', '',
           'Generated by `tools/reference-game/scripts/function_index.py`; do not edit '
           '(regeneration: [README.md](README.md)). It goes stale quickly; regenerate it '
           'before relying on a row.', '',
           '## Sources', '',
           f'- Executable: `VER95/DBSIM.EXE`' + (f', SHA-256 `{digest}`' if digest else '')
           + ' ([reference_version.md](' + rel(Path(args.registry).parents[3] / 'docs'
                                               / 'reference_version.md', out_dir) + ')).',
           f'- Listing: `llvm-objdump -d -M intel` (linear sweep), {len(listing.addresses)} '
           f'instructions, {len(listing.calls)} direct call sites, {len(listing.symbols)} '
           'symbol labels.',
           f'- Registry: {len(registry)} hooks (`{Path(args.registry).name}`).',
           f'- Verdicts: {len(classified)} classified VAs; roles from {len(roles)} function-table '
           'rows of the subsystem documents.',
           f'- DBSIM map: `{Path(args.map).parent.name}/{Path(args.map).name}`, {len(rows)} rows '
           '(live, unreplaced functions)' + (
               f'; probed {summary.get("probed")}, live {summary.get("live")}, dead '
               f'{summary.get("dead")}' + (f', superseded {summary["superseded"]}' if 'superseded' in summary else '')
               if summary else '') + '.' if args.map else '- DBSIM map: none.',
           f'- Hook counters: `{Path(args.counters).parent.name}/{Path(args.counters).name}` '
           '(last tick record per suite).' if args.counters else '- Hook counters: none.',
           (f'- Own process: `{Path(args.sim).name}` ({len(sim)} functions with a service row or a gap '
            'kind; `sim_gap.py`).' if args.sim else '- Own process: none.'),
           '', '## Pages', '',
           f'{len(starts)} function entries, {total_rows} listed'
           + ('' if args.full else ' (entries with no name, verdict, live count, role or direct '
                                   'call relation are left out)') + '.', '',
           '| Page | Range | Functions |', '|---|---|---:|']
    idx.extend(f'| [{n}]({n}) | {lo:06x}–{hi:06x} | {c} |' for n, lo, hi, c in written)
    area_lines, boundaries_inside, no_area = area_section(
        starts, sizes, hooks, rows, row_vas, callees, callers, end_of_listing, gap)
    idx += area_lines
    idx += ['', '## Cross-check with the DBSIM map', '',
            'The linear sweep loses instruction boundaries behind data in CODE until it '
            'resynchronizes; entries known from the map, the registry or the verdicts are '
            'listed there anyway (instruction count from the map, else `?`), and their '
            'direct calls are attributed to the nearest entry below.', '',
            f'- Map rows without an instruction start in the listing: {len(map_not_start)}: '
            f'{va_line(map_not_start)}.',
            f'- Map rows that are no direct call target or symbol (reached through vtables or '
            f'callbacks only): {len(map_only_indirect)}: {va_line(map_only_indirect, 40)}.',
            f'- Registry, verdict and map caller/callee VAs without an instruction start in the '
            f'listing: {len(extra_not_start)}: {va_line(extra_not_start)}.',
            f'- Direct call targets of the listing inside a decoded instruction (call '
            f'decoded from data, or a target behind a lost boundary; not listed as entries): '
            f'{len(bad_targets)}: {va_line(bad_targets, 40)}.',
            f'- Entries without a map row: {len(no_row)}:']
    idx.extend(f'  - {label}: {len(vas)}' for label, vas in categories)
    idx += ['', '## Hooks without a resolved core function or contract heading', '',
            'The core function is found by name (`es2_<hook>`, `es2_<hook>_registers`, '
            '`es2_<hook>_*`, `*_<hook>`), else as the first core declaration whose leading '
            'comment names the hook\'s VA; the contract as the first heading that names the '
            'VA, else the hook, else the section that names either most often, each '
            'preferring the adapter header\'s specification document. '
            f'{len(unresolved)} hooks miss one of them:', '']
    idx.append(', '.join(f'`{name}` {va:x} ({"core" if a else ""}{"/" if a and b else ""}'
                         f'{"contract" if b else ""})' for va, name, a, b in sorted(unresolved))
               or 'none')
    (out_dir / 'index.md').write_bytes(('\n'.join(idx) + '\n').encode('utf-8'))
    return dict(entries=len(starts), rows=total_rows, pages=len(written),
                map_not_start=map_not_start, extra_not_start=extra_not_start,
                unresolved=len(unresolved), categories={k: len(v) for k, v in categories},
                boundaries_inside=boundaries_inside, no_area=len(no_area))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--listing', required=True, help='llvm-objdump -d -M intel listing of DBSIM.EXE')
    ap.add_argument('--registry', required=True, help='tools/reference-game/proxy/hook_registry.h')
    ap.add_argument('--output', required=True, help='output directory (docs/functions)')
    ap.add_argument('--image', help='DBSIM.EXE: SHA-256 and import thunk names')
    ap.add_argument('--hooks-doc', help='docs/reference-game/hooks.md (compare, core module)')
    ap.add_argument('--classified', help='docs/reference-game/function_verdicts.md')
    ap.add_argument('--docs', action='append', help='subsystem document or directory (repeatable)')
    ap.add_argument('--core', help='core directory (include/es2/core, src)')
    ap.add_argument('--map', help='map.json of a DBSIM map run')
    ap.add_argument('--counters', help='hook-counters.json of a regression')
    ap.add_argument('--sim', help='sim_gap.json written by sim_gap.py --json (Sim column)')
    ap.add_argument('--full', action='store_true', help='also list entries without any information')
    args = ap.parse_args(argv)
    result = build(args)
    print(f"{result['entries']} entries, {result['rows']} rows on {result['pages']} pages; "
          f"map rows without listing entry: {len(result['map_not_start'])}; "
          f"other VAs without listing entry: {len(result['extra_not_start'])}; "
          f"hooks without core function or contract: {result['unresolved']}; "
          f"area boundaries without an entry: {len(result['boundaries_inside'])}; "
          f"entries in no area range: {result['no_area']}")
    for label, n in result['categories'].items():
        print(f'  entries without map row, {label}: {n}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
