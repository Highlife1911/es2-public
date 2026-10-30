# window-lint: allow-file inherits console: developer/worker CLI run from a console or from a Bazel test; the llvm-objdump child shares its (possibly hidden) console
"""Generate sim/data/dbsim_data.c: DBSIM's initialised DATA and the CODE bytes es2sim reads, as classified runs.

es2sim reads, besides the headers and imports (dbsim_image_tables.py), the initialised DATA
of the user's DBSIM.EXE (68,608 bytes at 0x497000) and a few byte ranges of
CODE (dbsim_gap.md "What es2sim takes from DBSIM.EXE", own_process.md "what es2sim
really reads from the mapped image"). This script reads the
hash-checked file, its .reloc directory and an llvm-objdump listing of it,
and writes those bytes as a C source of runs, each with its VA:

- pointer slots (every HIGHLOW relocation site in the initialised DATA) as
  VA constants, one per line with the target's kind: `code function` (a
  direct call target of the listing or the `push ebp; mov ebp, esp`
  prologue), `code label` (an instruction start of the linear listing),
  `code rtti` (a type descriptor, below), `data` (initialised DATA), `bss`
  (the zero-filled rest of DATA) or another section's name; a target none
  of these explains is written as a plain dword run with the comment
  `unexplained` and counted;
- strings: printable runs (0x20..0x7e, TAB, LF, CR) of 4 or more bytes
  ending in NUL, outside pointer slots, as C literals whose implicit NUL is
  the terminator (everything outside 0x20..0x7e, and `"`, `\\`, `?`, as
  octal escapes; a NUL always ends a run, so none is lost);
- the other non-zero bytes as byte arrays: the stretches between pointer
  slots and strings, split at zero gaps of ZERO_GAP or more bytes and at the
  table VAs the contract documents name (DOC_TABLES, NAMES), trimmed of
  zeros at both ends; a run that starts at a named VA carries the name;
- zero bytes outside runs are not emitted (the writer starts from zeros);
- CODE: the six index-byte tables read by own code (each verified in the
  bytes: `mov r8, [r32 + table]` then `jmp [4*r32 + jumps]`, the jump table
  right after the index bytes, and a `cmp r32, size - 1` before), the dword
  0x4967ae (a relocated operand in CODE names it), and the dword +0x20 of
  every type descriptor that 0x496406's `[[obj] - [[obj] - 4] - 0xc]` can
  reach: a relocated DATA slot S into CODE followed by two zero dwords and
  a relocated slot into CODE (the vtable at S + 0xc, its first method),
  whose target is a class descriptor (mask word 3 at +4, name offset at +6
  naming a printable NUL-terminated name, vptr offset at +8 not -1).

The output is committed and is the source of record
(CLAUDE.md section 3, "Data from the executables"); no
build or test runs this script except its fixture test and the real-data
check that regenerates and compares (tests/test_dbsim_data.py).
//sim:image_data_real_test compares every run with the file.

With --map <summary.json of an image-access run> it also writes
sim/tests/image_data_map.h: the byte ranges that run saw read in CODE and the
initialised DATA (VAs only).

Usage:
  python -B dbsim_data.py [--exe <VER95/DBSIM.EXE>] [--objdump <llvm-objdump>] [--listing <file>]
                          [--out sim/data/dbsim_data.c] [--map summary.json [--map-out ...]] [--check]
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dbsim_image_tables as dt  # noqa: E402

FormatError = dt.FormatError
REPO = Path(__file__).resolve().parents[3]
DEFAULT_OUT = REPO / 'sim' / 'data' / 'dbsim_data.c'
DEFAULT_MAP_OUT = REPO / 'sim' / 'tests' / 'image_data_map.h'
# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
DEFAULT_OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'

DATA_SECTION = 'DATA'
ZERO_GAP = 16
# The six CODE index-byte tables (dbsim_gap.md "What es2sim takes from DBSIM.EXE"; readers in
# core/src/mission_load.c, sim/src/hud_widgets.c, sim/src/hud_draw.c).
CODE_TABLES = (0x4100fd, 0x405366, 0x426492, 0x436be2, 0x44bb9f, 0x44e1f4)
# CODE dwords read as data (sim/src/input.c, sim/src/services_files.c).
CODE_DWORDS = (0x4967ae,)
RTTI_COUNT_OFFSET = 0x20

# Table VAs inside the initialised DATA named by the contract documents
# (docs/reference-game/<doc>.md, "table 0x...", "records 0x...", "map 0x...").
DOC_TABLES = {
    0x49702c: 'own_process', 0x4973ac: 'function_verdicts', 0x497b8a: 'function_verdicts',
    0x4982b8: 'function_verdicts', 0x4987c4: 'function_verdicts', 0x498a84: 'function_verdicts',
    0x498afc: 'function_verdicts', 0x498b74: 'function_verdicts', 0x498bec: 'function_verdicts',
    0x498c64: 'function_verdicts', 0x498cdc: 'function_verdicts', 0x498d54: 'function_verdicts',
    0x498dcc: 'function_verdicts', 0x49935c: 'oracle_suites', 0x4993a4: 'own_process',
    0x499556: 'oracle_suites', 0x499cf8: 'own_process', 0x49a282: 'function_verdicts',
    0x49a318: 'oracle_suites', 0x49a5e0: 'function_verdicts', 0x49aa7c: 'own_process',
    0x49ac38: 'function_verdicts', 0x49afe6: 'function_verdicts', 0x49ba7c: 'function_verdicts',
    0x49bad4: 'function_verdicts', 0x49c138: 'function_verdicts', 0x49c154: 'function_verdicts',
    0x49c1c4: 'function_verdicts', 0x49c328: 'function_verdicts', 0x49c758: 'function_verdicts',
    0x49c7dc: 'function_verdicts', 0x49c878: 'function_verdicts', 0x49c8a4: 'function_verdicts',
    0x49c8fc: 'function_verdicts', 0x49c91c: 'function_verdicts', 0x49c9dd: 'own_process',
    0x49ca1d: 'function_verdicts', 0x49cabc: 'function_verdicts', 0x49cbd8: 'function_verdicts',
    0x49cee0: 'function_verdicts', 0x49cfa0: 'function_verdicts', 0x49d0e8: 'function_verdicts',
    0x49d12c: 'function_verdicts', 0x49d64c: 'function_verdicts', 0x49d9b0: 'function_verdicts',
    0x49dff4: 'function_verdicts', 0x49e120: 'function_verdicts', 0x49e1e4: 'oracle_suites',
    0x49e3e4: 'oracle_suites', 0x49e4d0: 'live_coverage', 0x49eb0b: 'oracle_suites',
    0x49eb45: 'own_process', 0x49ec74: 'own_process', 0x4a0c74: 'function_verdicts',
    0x4a0ca0: 'own_process', 0x4a0e10: 'function_verdicts', 0x4a1088: 'function_verdicts',
    0x4a10ec: 'oracle_suites', 0x4a14d0: 'function_verdicts', 0x4a15f4: 'own_process',
    0x4a1738: 'function_verdicts', 0x4a1760: 'function_verdicts', 0x4a2e10: 'function_verdicts',
    0x4a2f04: 'function_verdicts', 0x4a306c: 'rtl_imports', 0x4a30f8: 'function_verdicts',
    0x4a3368: 'own_process', 0x4a58d4: 'function_verdicts', 0x4a5b29: 'function_verdicts',
    0x4a5b39: 'function_verdicts', 0x4a62b8: 'function_verdicts', 0x4a6940: 'rtl_imports',
    0x4a6a40: 'function_verdicts', 0x4a6a9c: 'dll_imports', 0x4a7b5e: 'rtl_imports',
}
# Tables with a role in own_process.md section 2.3 (the math adapters'
# image-base offsets 0xa05bc, 0xa1e3c, 0xa1c34; the keyboard map; the init
# and exit records).
NAMES = {
    0x4a05bc: 'math_table', 0x4a1e3c: 'math_table', 0x4a1c34: 'math_table',
    0x4a10ec: 'keyboard_map', 0x4a79d8: 'init_records', 0x4a7b5e: 'exit_records',
}

KIND_BYTES, KIND_STRING, KIND_POINTERS, KIND_DWORDS, KIND_CODE_TABLE, KIND_RTTI_COUNT = range(1, 7)
KIND_NAMES = {KIND_BYTES: 'ES2_SIM_DATA_BYTES', KIND_STRING: 'ES2_SIM_DATA_STRING',
              KIND_POINTERS: 'ES2_SIM_DATA_POINTERS', KIND_DWORDS: 'ES2_SIM_DATA_DWORDS',
              KIND_CODE_TABLE: 'ES2_SIM_DATA_CODE_TABLE', KIND_RTTI_COUNT: 'ES2_SIM_DATA_RTTI_COUNT'}
PRINTABLE = set(range(0x20, 0x7f)) | {0x09, 0x0a, 0x0d}


def relocations(image):
    """The VAs of every HIGHLOW site of the .reloc directory (types other than 0 and 3 refused)."""
    rva, size = image.reloc_dir
    sites = []
    at = 0
    while at + 8 <= size:
        page, block = image.u32(rva + at), image.u32(rva + at + 4)
        if block == 0:
            break
        if block < 8 or block & 1 or at + block > size:
            raise FormatError('relocation block at +0x%x' % at)
        for k in range((block - 8) // 2):
            e = image.u16(rva + at + 8 + 2 * k)
            if e >> 12 == 3:
                sites.append(image.image_base + page + (e & 0xfff))
            elif e >> 12:
                raise FormatError('relocation type %d' % (e >> 12))
        at += block
    return sorted(sites)


def parse_listing(text):
    """Instruction starts and direct call/jump targets of an llvm-objdump -d listing."""
    starts = {int(m, 16) for m in re.findall(r'^\s*([0-9a-f]+):\s', text, re.M)}
    calls = {int(m, 16) for m in re.findall(r'\tcall[l]?\s+0x([0-9a-f]+)', text)}
    jumps = {int(m, 16) for m in re.findall(r'\tj[a-z]+\s+0x([0-9a-f]+)', text)}
    return starts, calls, jumps


class Analysis:
    def __init__(self, data, listing, code_tables=CODE_TABLES, code_dwords=CODE_DWORDS):
        self.image = image = dt.Image(data)
        base = image.image_base
        sec = [x for x in image.sections if x['name'] == DATA_SECTION]
        if len(sec) != 1:
            raise FormatError('no single %s section' % DATA_SECTION)
        sec = sec[0]
        if not sec['raw_size'] or sec['raw_size'] > sec['virtual_size']:
            raise FormatError('%s raw size' % DATA_SECTION)
        self.init_lo = base + sec['rva']
        self.init_hi = self.init_lo + sec['raw_size']
        self.bss_hi = self.init_lo + sec['virtual_size']
        self.init = image.bytes(sec['rva'], sec['raw_size'])
        code = [x for x in image.sections if x['flags'] & dt.SCN_EXECUTE]
        self.code_file_hi = max(base + x['rva'] + min(x['raw_size'], x['virtual_size'] or x['raw_size'])
                                for x in code)
        self.sites = relocations(image)
        self.site_set = set(self.sites)
        self.starts, self.calls, self.jumps = parse_listing(listing)
        self.data_sites = [s for s in self.sites if self.init_lo <= s < self.init_hi]
        for s in self.data_sites:
            if s + 4 > self.init_hi:
                raise FormatError('pointer slot 0x%x crosses the end of DATA' % s)
        for a, b in zip(self.data_sites, self.data_sites[1:]):
            if b - a < 4:
                raise FormatError('pointer slots 0x%x and 0x%x overlap' % (a, b))
        self.code_runs = self._code_tables(code_tables) + self._code_dwords(code_dwords)
        self.descriptors, self.vtables = self._descriptors()
        self.rtti_runs = [(t + RTTI_COUNT_OFFSET, 4) for t in sorted(self.descriptors)]

    # --- readers -----------------------------------------------------------
    def in_code(self, va, n=1):
        return self.image.code_lo <= va and va + n <= self.code_file_hi

    def u8(self, va):
        return self.image.bytes(va - self.image.image_base, 1)[0]

    def u16(self, va):
        return self.image.u16(va - self.image.image_base)

    def u32(self, va):
        return self.image.u32(va - self.image.image_base)

    def raw(self, va, n):
        return self.image.bytes(va - self.image.image_base, n)

    # --- CODE ----------------------------------------------------------------
    def _code_tables(self, tables):
        runs = []
        for t in tables:
            if not self.in_code(t - 13, 13):
                raise FormatError('code table 0x%x outside CODE' % t)
            mov, jmp = self.raw(t - 13, 6), self.raw(t - 7, 7)
            if mov[0] != 0x8a or mov[1] & 0xc0 != 0x80 or int.from_bytes(mov[2:6], 'little') != t:
                raise FormatError('code table 0x%x: no mov r8, [r32 + table] before it' % t)
            if jmp[0:2] != b'\xff\x24' or jmp[2] & 0xc7 != 0x85:
                raise FormatError('code table 0x%x: no jmp [4*r32 + jumps] before it' % t)
            size = int.from_bytes(jmp[3:7], 'little') - t
            if not 0 < size <= 0x100 or not self.in_code(t, size):
                raise FormatError('code table 0x%x: size %d' % (t, size))
            window = self.raw(t - 32, 19)
            if not any(window[i] == 0x83 and window[i + 1] & 0xf8 == 0xf8 and window[i + 2] == size - 1
                       for i in range(len(window) - 2)):
                raise FormatError('code table 0x%x: no cmp r32, %d before it' % (t, size - 1))
            runs.append((t, size))
        return runs

    def _code_dwords(self, dwords):
        runs = []
        for d in dwords:
            if not self.in_code(d, 4):
                raise FormatError('code dword 0x%x outside CODE' % d)
            if not any(self.image.code_lo <= s < self.code_file_hi and self.u32(s) == d for s in self.sites):
                raise FormatError('code dword 0x%x: no relocated operand names it' % d)
            runs.append((d, 4))
        return runs

    def descriptor_name(self, t):
        """The name of a type descriptor at t, or None."""
        if not self.in_code(t, 0x28):
            return None
        off = self.u16(t + 6)
        if off < 0xc or off > 0x100 or not self.in_code(t + off, 1):
            return None
        name = bytearray()
        va = t + off
        while self.in_code(va, 1) and len(name) < 256:
            c = self.u8(va)
            if c == 0:
                break
            if c < 0x20 or c > 0x7e:
                return None
            name.append(c)
            va += 1
        if not name or not self.in_code(va, 1) or self.u8(va):
            return None
        return name.decode('ascii')

    def is_type_descriptor(self, t):
        return self.u16(t + 4) < 0x800 and self.descriptor_name(t) is not None if self.in_code(t, 8) else False

    def is_class_descriptor(self, t):
        return (self.in_code(t, RTTI_COUNT_OFFSET + 4) and self.u16(t + 4) == 3
                and self.u32(t + 8) != 0xffffffff and self.descriptor_name(t) is not None)

    def _descriptors(self):
        found, vtables = {}, []
        for s in self.data_sites:
            t = self.u32(s)
            if not self.in_code(t) or s + 16 > self.init_hi:
                continue
            if (s + 4) in self.site_set or (s + 8) in self.site_set or (s + 12) not in self.site_set:
                continue
            if self.u32(s + 4) or self.u32(s + 8) or not self.in_code(self.u32(s + 12)):
                continue
            if self.is_class_descriptor(t):
                found.setdefault(t, []).append(s + 12)
                vtables.append(s + 12)
        return found, vtables

    # --- pointer slots -------------------------------------------------------
    def target_kind(self, t):
        if self.in_code(t):
            if t in self.descriptors or self.is_type_descriptor(t):
                return 'code rtti'
            if t in self.calls or self.raw(t, 3) == b'\x55\x8b\xec':
                return 'code function'
            if t in self.starts:
                return 'code label'
            return None
        if self.init_lo <= t < self.init_hi:
            return 'data'
        if self.init_hi <= t < self.bss_hi:
            return 'bss'
        for x in self.image.sections:
            lo = self.image.image_base + x['rva']
            if lo <= t < lo + max(x['virtual_size'], x['raw_size']):
                return x['name']
        return None

    # --- the DATA runs -------------------------------------------------------
    def data_runs(self):
        """[(va, kind, payload, name)] in VA order: payload is a list of (value, comment) for
        pointer/dword runs, bytes otherwise."""
        lo, n = self.init_lo, len(self.init)
        b = self.init
        owner = bytearray(n)              # 1: pointer slot
        slots = {}
        for s in self.data_sites:
            i = s - lo
            owner[i:i + 4] = b'\1\1\1\1'
            t = self.u32(s)
            slots[i] = (t, self.target_kind(t))
        named = set(NAMES) | set(DOC_TABLES)
        runs = []
        # Pointer runs: consecutive slots of the same family (explained / unexplained), split at named VAs.
        cur = None
        for i in sorted(slots):
            t, kind = slots[i]
            family = KIND_POINTERS if kind else KIND_DWORDS
            if not kind:
                kind = 'unexplained'
                if self.in_code(t) and self.in_code(t - 1) and self.u8(t - 1) in (0x00, 0x90):
                    kind = 'unexplained (code after a padding byte the linear listing decodes across)'
            if cur and cur[0] + 4 * len(cur[2]) == lo + i and cur[1] == family and lo + i not in named:
                cur[2].append((t, kind))
            else:
                cur = [lo + i, family, [(t, kind)]]
                runs.append(cur)
        # Strings outside the slots.
        i = 0
        while i < n:
            if owner[i] or b[i] not in PRINTABLE:
                i += 1
                continue
            j = i
            while j < n and not owner[j] and b[j] in PRINTABLE:
                j += 1
            if j < n and not owner[j] and b[j] == 0 and j - i >= 4:
                runs.append([lo + i, KIND_STRING, bytes(b[i:j + 1])])
                owner[i:j + 1] = b'\2' * (j + 1 - i)
                i = j + 1
            else:
                i = j
        # The other non-zero bytes.
        i = 0
        while i < n:
            if owner[i]:
                i += 1
                continue
            j = i
            while j < n and not owner[j]:
                j += 1
            runs.extend([lo + a, KIND_BYTES, bytes(b[a:z])] for a, z in self._split(i, j, named))
            i = j
        runs.sort(key=lambda r: r[0])
        out = []
        for va, kind, payload in runs:
            name = NAMES.get(va) or DOC_TABLES.get(va)
            out.append((va, kind, payload, name))
        return out

    def _split(self, i, j, named):
        """Byte ranges [a, z) of the free stretch [i, j): split at zero gaps and named VAs, trimmed."""
        b, lo = self.init, self.init_lo
        pieces, a = [], i
        for k in range(i + 1, j):
            if lo + k in named:
                pieces.append((a, k))
                a = k
        pieces.append((a, j))
        out = []
        for a, z in pieces:
            k = a
            while k < z:
                while k < z and b[k] == 0:
                    k += 1
                if k >= z:
                    break
                start, end, zeros = k, k, 0
                while k < z:
                    if b[k]:
                        end, zeros = k + 1, 0
                    else:
                        zeros += 1
                        if zeros >= ZERO_GAP:
                            break
                    k += 1
                out.append((start, end))
        return out


def c_literal(text):
    """The C literal pieces of `text` (without the terminating NUL)."""
    pieces, cur = [], ''
    for c in text:
        if 0x20 <= c <= 0x7e and c not in b'"\\?':
            cur += chr(c)
        else:
            cur += '\\%03o' % c
        if len(cur) >= 72 or c == 0x0a:
            pieces.append(cur)
            cur = ''
    if cur or not pieces:
        pieces.append(cur)
    return ['"%s"' % p for p in pieces]


def symbol(va, kind, name):
    prefix = {KIND_BYTES: 'b', KIND_STRING: 's', KIND_POINTERS: 'p', KIND_DWORDS: 'd'}[kind]
    if name:
        return '%s_%s_%06x' % (name, prefix, va)
    return '%s_%06x' % (prefix, va)


def byte_lines(payload, va):
    return ['    %s  /* 0x%08x */' % (' '.join('0x%02x,' % c for c in payload[at:at + 16]), va + at)
            for at in range(0, len(payload), 16)]


def generate(data, listing, source='VER95/DBSIM.EXE', code_tables=CODE_TABLES, code_dwords=CODE_DWORDS):
    """(C source, report dict) for the bytes `data` of a PE32 file and its listing."""
    import hashlib
    a = Analysis(data, listing, code_tables, code_dwords)
    runs = a.data_runs()
    counts = {'slots': 0, 'code': 0, 'data': 0, 'bss': 0, 'unexplained': 0, 'pointer_runs': 0,
              'dword_runs': 0, 'strings': 0, 'string_bytes': 0, 'byte_runs': 0, 'byte_bytes': 0,
              'named_runs': 0, 'code_kinds': {}}
    unexplained = []
    body, table = [], []
    for va, kind, payload, name in runs:
        sym = symbol(va, kind, name)
        doc = ''
        if name:
            counts['named_runs'] += 1
            doc = ' (%s)' % ('own_process.md 2.3' if va in NAMES else '%s.md' % DOC_TABLES[va])
        if kind in (KIND_POINTERS, KIND_DWORDS):
            size = 4 * len(payload)
            counts['pointer_runs' if kind == KIND_POINTERS else 'dword_runs'] += 1
            what = 'pointer slot' if kind == KIND_POINTERS else 'dword'
            plural = 's' if len(payload) > 1 else ''
            body.append('/* 0x%08x: %d %s%s%s. */' % (va, len(payload), what, plural, doc))
            body.append('static const uint32_t %s[%d] = {' % (sym, len(payload)))
            for k, (t, comment) in enumerate(payload):
                counts['slots'] += 1
                head = comment.split()[0]
                if head == 'code':
                    counts['code'] += 1
                    sub = comment.split()[1]
                    counts['code_kinds'][sub] = counts['code_kinds'].get(sub, 0) + 1
                elif head in ('data', 'bss'):
                    counts[head] += 1
                elif head == 'unexplained':
                    counts['unexplained'] += 1
                    unexplained.append((va + 4 * k, t))
                else:
                    counts['data'] += 1
                body.append('    0x%08xu,  /* 0x%08x %s */' % (t, va + 4 * k, comment))
            body.append('};')
        elif kind == KIND_STRING:
            size = len(payload)
            counts['strings'] += 1
            counts['string_bytes'] += size
            lit = c_literal(payload[:-1])
            body.append('/* 0x%08x: string, %d bytes with the NUL%s. */' % (va, size, doc))
            if len(lit) == 1:
                body.append('static const char %s[%d] = %s;' % (sym, size, lit[0]))
            else:
                body.append('static const char %s[%d] =' % (sym, size))
                body += ['    %s' % p for p in lit[:-1]] + ['    %s;' % lit[-1]]
        else:
            size = len(payload)
            counts['byte_runs'] += 1
            counts['byte_bytes'] += size
            counts['byte_nonzero'] = counts.get('byte_nonzero', 0) + sum(1 for c in payload if c)
            body.append('/* 0x%08x: %d byte%s%s. */' % (va, size, 's' if size > 1 else '', doc))
            body.append('static const uint8_t %s[%d] = {' % (sym, size))
            body += byte_lines(payload, va)
            body.append('};')
        table.append('    { 0x%08xu, %du, %s, %s },' % (va, size, KIND_NAMES[kind], sym))
    # CODE runs: the tables, the dwords and the descriptor counts.
    code = [(va, n, KIND_CODE_TABLE, 'index-byte table' if (va, n) in [(t, s) for t, s in a.code_runs[:len(code_tables)]]
             else 'dword read as data') for va, n in a.code_runs]
    code += [(va, n, KIND_RTTI_COUNT, 'type descriptor 0x%08x "%s" +0x%x' % (va - RTTI_COUNT_OFFSET,
                                                                           a.descriptor_name(va - RTTI_COUNT_OFFSET),
                                                                           RTTI_COUNT_OFFSET))
             for va, n in a.rtti_runs]
    code.sort()
    for (va1, n1, _, _), (va2, _, _, _) in zip(code, code[1:]):
        if va1 + n1 > va2:
            raise FormatError('CODE runs 0x%x and 0x%x overlap' % (va1, va2))
    for va, n, kind, what in code:
        raw = a.raw(va, n)
        if kind == KIND_RTTI_COUNT:
            body.append('/* 0x%08x: the dword +0x20 of the %s. */' % (va, what.split(' +0x')[0]))
            body.append('static const uint32_t c_%06x[1] = { 0x%08xu };' % (va, int.from_bytes(raw, 'little')))
        else:
            body.append('/* 0x%08x: CODE, %d bytes, %s. */' % (va, n, what))
            body.append('static const uint8_t c_%06x[%d] = {' % (va, n))
            body += byte_lines(raw, va)
            body.append('};')
    all_runs = [(va, r) for va, r in zip([r[0] for r in runs], table)]
    all_runs += [(va, '    { 0x%08xu, %du, %s, c_%06x },' % (va, n, KIND_NAMES[kind], va)) for va, n, kind, _ in code]
    all_runs.sort()
    sha = hashlib.sha256(data).hexdigest()
    total = sum(len(p) * (4 if k in (KIND_POINTERS, KIND_DWORDS) else 1) for _, k, p, _ in runs)
    counts.update(zero_skipped=len(a.init) - total, init_bytes=len(a.init), runs=len(all_runs),
                  code_tables=len(code_tables), code_dwords=len(code_dwords), descriptors=len(a.descriptors),
                  vtables=len(a.vtables), code_runs=len(code))
    lines = [
        '/* GENERATED by tools/reference-game/scripts/dbsim_data.py; do not edit.',
        ' * Source: %s, %d bytes, SHA-256 %s.' % (source, len(data), sha),
        ' * The initialised DATA 0x%08x..0x%08x (%d bytes) as classified runs, and the' % (
            a.init_lo, a.init_hi, len(a.init)),
        ' * CODE bytes es2sim reads (own_process.md, "The initialised DATA and the CODE',
        ' * bytes as own runs"). Zero bytes outside the runs are not emitted: es2_sim_data_write',
        ' * writes the runs into zeroed memory. //sim:image_data_real_test compares',
        ' * every run with the file.',
        ' * DATA: %d pointer slots (%d code, %d data, %d bss, %d unexplained) in %d runs,' % (
            counts['slots'], counts['code'], counts['data'], counts['bss'], counts['unexplained'],
            counts['pointer_runs'] + counts['dword_runs']),
        ' * %d strings (%d bytes), %d byte runs (%d bytes, %d of them non-zero),' % (
            counts['strings'], counts['string_bytes'], counts['byte_runs'], counts['byte_bytes'],
            counts.get('byte_nonzero', 0)),
        ' * %d zero bytes not emitted.' % counts['zero_skipped'],
        ' * CODE: %d index-byte tables, %d dwords, %d type-descriptor counts (+0x20). */' % (
            len(code_tables), len(code_dwords), len(a.rtti_runs)),
        '#include "es2/sim/image_data.h"',
        '',
    ]
    lines += body
    lines += ['', '/* Every run in VA order. */', 'const es2_sim_data_run es2_sim_dbsim_data_runs[] = {']
    lines += [r for _, r in all_runs]
    lines += ['};', 'const unsigned es2_sim_dbsim_data_run_count = %du;' % len(all_runs), '',
              '/* The guest spans the runs reproduce in full (written into zeroed memory):',
              ' * the initialised DATA and every CODE run. */',
              'const es2_sim_data_span es2_sim_dbsim_data_spans[] = {',
              '    { 0x%08xu, %du },' % (a.init_lo, len(a.init))]
    lines += ['    { 0x%08xu, %du },' % (va, n) for va, n, _, _ in code]
    lines += ['};', 'const unsigned es2_sim_dbsim_data_span_count = %du;' % (1 + len(code)), '']
    report = {'counts': counts, 'unexplained': ['0x%08x -> 0x%08x' % u for u in unexplained],
              'descriptors': ['0x%08x %s' % (t, a.descriptor_name(t)) for t in sorted(a.descriptors)]}
    return '\n'.join(lines), report


def map_header(summary, init_lo=0x497000, init_hi=0x4a7c00):
    """sim/tests/image_data_map.h from an image-access run's summary.json: the read ranges in CODE and the
    initialised DATA (VAs only)."""
    ranges = []
    for p in summary['pages']:
        if p['area'] not in ('CODE', 'DATA'):
            continue
        for lo, hi in p['read'] + p['written']:
            lo, hi = int(lo, 16), int(hi, 16)
            if p['area'] == 'DATA':
                lo, hi = max(lo, init_lo), min(hi, init_hi)
            if lo < hi:
                ranges.append((lo, hi, p['area']))
    ranges = sorted(set(ranges))
    reads = sorted({int(r['va'], 16) for r in summary['code_reads']})
    lines = ['/* GENERATED by tools/reference-game/scripts/dbsim_data.py --map; do not edit.',
             ' * The image-access map (format %s, %d suites): the byte ranges [lo, hi)' % (
                 summary['format'], summary['totals']['suites']),
             ' * read or written in CODE and in the initialised DATA, and the CODE read VAs',
             ' * outside the table pages (the faulting byte). VAs only, no image bytes. */',
             '#ifndef ES2_SIM_TESTS_IMAGE_DATA_MAP_H', '#define ES2_SIM_TESTS_IMAGE_DATA_MAP_H',
             '#include <stdint.h>', '',
             'static const uint32_t image_data_map[][2] = {']
    lines += ['    { 0x%08xu, 0x%08xu },  /* %s */' % r for r in ranges]
    lines += ['};', '', 'static const uint32_t image_data_code_reads[] = {']
    lines += ['    0x%08xu,' % v for v in reads]
    lines += ['};', '', '#endif', '']
    return '\n'.join(lines)


def run_objdump(objdump, exe):
    return subprocess.run([str(objdump), '-d', '-z', '-M', 'intel', str(exe)], capture_output=True, text=True,
                          check=True).stdout


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('--exe', type=Path)
    ap.add_argument('--objdump', type=Path, default=DEFAULT_OBJDUMP)
    ap.add_argument('--listing', type=Path, help='an llvm-objdump -d -z -M intel listing instead of running objdump')
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--map', type=Path, help="the image-access map's summary.json")
    ap.add_argument('--map-out', type=Path, default=DEFAULT_MAP_OUT)
    ap.add_argument('--report', type=Path, help='write the classification report (JSON) here')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args(argv)
    exe = args.exe or dt.find_exe()
    if exe is None:
        print('dbsim_data: no --exe and no DBSIM.EXE below ES2_DATA_ROOT', file=sys.stderr)
        return 2
    try:
        data = dt.read_reference(exe)
        listing = args.listing.read_text() if args.listing else run_objdump(args.objdump, exe)
        text, report = generate(data, listing)
    except FormatError as e:
        print('dbsim_data: %s' % e, file=sys.stderr)
        return 1
    outputs = [(args.out.resolve(), text)]
    if args.map:
        outputs.append((args.map_out.resolve(), map_header(json.loads(args.map.read_text()))))
    if args.report:
        args.report.write_bytes(json.dumps(report, indent=1).encode('ascii'))
    status = 0
    for out, content in outputs:
        if args.check:
            same = out.exists() and out.read_bytes() == content.encode('ascii')
            print('dbsim_data: %s %s' % (out, 'is current' if same else 'DIFFERS'))
            status |= 0 if same else 1
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(content.encode('ascii'))
            print('dbsim_data: wrote %s' % out)
    print('dbsim_data: %s' % json.dumps(report['counts']))
    return status


if __name__ == '__main__':
    sys.exit(main())
