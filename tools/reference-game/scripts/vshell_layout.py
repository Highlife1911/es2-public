# window-lint: allow-file inherits console: developer/worker CLI run from a console or from a Bazel test; llvm-objdump shares its (possibly hidden) console
"""Generate shell/data/vshell_layout.json, the control table of VSHELL's screens.

VSHELL.EXE builds all of its screens once at start-up: `0x40180c` calls 22
screen constructors (`0x401812..0x40187b`), each of which allocates window
objects with `operator new` (`0x4086b7`) and runs a class constructor of the
ES window framework (ESButtonFont, ESButtonBitmap, ESTitle, ESMessage, ...;
docs/reverse-engineering/vshell_widgets.md). This script reads those
constructor calls out of the reference executable and writes, per screen and
per control: the class (from the Borland RTTI behind the object's vtable), the
constructor and its call site, the parent, the rectangle relative to the
parent and on the screen, font, bitmap, handler, label (ESTEXT ID) and the
other constructor arguments by name, the global the object pointer is kept in,
and whether the control is hidden after construction.

Method: a small x86 interpreter runs the 22 constructors in the original
order over the instructions llvm-objdump decodes (Intel syntax). Values are
either concrete 32-bit numbers or symbols; a symbol is a value the program
only knows at run time (a global that some instruction writes, a font or
bitmap loaded at start-up, the result of a call that is not interpreted) and
carries its formula, e.g. `[0x48d2d8]` or `([0x482b82]*0x2c)+0x10`. The
window-framework constructors are interpreted; the base window constructor,
show/hide, the text setter, `operator new`, the ESTEXT lookup, the DBM/DBA
loaders and the file stream are modelled (MODELS below; each model names the
original function it stands for); every other call is recorded with its
arguments and returns a symbol. A global is run-time state (a symbol) when it
lies in uninitialised data or when some instruction of the executable writes
it with an absolute address; otherwise its initial bytes are read from the
file (the JSON lists those reads under `constant_reads`).
A conditional branch on a symbol is not guessed: the run stops unless the
branch site is listed in BRANCH_ASSUMPTIONS, which names the condition, the
side taken and why; the JSON repeats every assumption that was used.

Inputs and safety: the executable's SHA-256 must be the reference
`1534aa8f...a16e` before anything else is read (`--exe`). The instructions
come from llvm-objdump run on those bytes (piped, so the hashed bytes are the
decoded bytes), or from a listing file written earlier with `--write-listing`
(`--listing`); in both cases every interpreted instruction's bytes are
compared with the executable. The listing contains original code and stays in
a scratch directory; the JSON contains numbers, IDs, addresses, class names
of the framework and resource file names only (no strings from game files, no
pixels, no listing text).

Usage:
  python -B vshell_layout.py --exe <VER95/VSHELL.EXE> [--out shell/data/vshell_layout.json]
         [--listing <scratch>/vshell.lst | --write-listing <scratch>/vshell.lst]
         [--objdump <llvm-objdump.exe>] [--check]
`--check` compares with the existing --out file and exits 1 on a difference.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

EXPECTED_SHA256 = '1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e'
# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
DEFAULT_OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'
SCHEMA_VERSION = 1
MASK = 0xffffffff

# ---------------------------------------------------------------------------
# The reference profile: addresses of VSHELL.EXE (image base 0x400000).

# The 22 screen constructors in the order 0x40180c calls them, with our names.
SCREENS = [
    ('main_menu', 0x430fc4), ('practice', 0x44b310), ('saved_games', 0x438c40),
    ('preferences', 0x435598), ('debug_dialog', 0x44dd38), ('cd_prompt', 0x4322a8),
    ('service_bay', 0x43afd4), ('squad_panel', 0x43d029), ('repair', 0x4326c7),
    ('quit_box', 0x431fa8), ('end_of_game_box', 0x44d2bc), ('launch_warning_box', 0x44d66c),
    ('pilot_name', 0x43b8f0), ('registration', 0x43bd2e), ('replay_box', 0x44cdac),
    ('weapons', 0x43ebbc), ('build', 0x445de8), ('armory', 0x4484c4), ('crew', 0x441548),
    ('mission', 0x442bc4), ('herc_scrap_box', 0x4479b8), ('weapon_scrap_box', 0x447e3e),
]
SCREEN_CALL_SITES = (0x401812, 0x40187b)

# Constructors whose first call on a fresh object defines a control. The
# argument names follow the constructor's use of them (vshell_widgets.md §2.3);
# 'this' is implied. Every one of these is interpreted, not modelled.
CTORS = {
    0x409f20: ('ESWindow', ['parent', 'rect', 'handler']),
    0x409d38: ('WinButton', ['parent', 'rect', 'handler']),
    0x40aca4: ('ESRect', ['parent', 'rect', 'handler', 'colour']),
    0x40a004: ('ESButtonFont', ['parent', 'rect', 'font', 'handler', 'label', 'colour', 'arg8']),
    0x40a2c4: ('ESButtonBitmap', ['parent', 'rect', 'font', 'bitmap_normal', 'bitmap_active', 'bitmap_extra',
                                  'handler', 'label']),
    0x40a6b0: ('ESRadioButton', ['parent', 'rect', 'font', 'bitmap_normal', 'bitmap_active', 'bitmap_extra',
                                 'handler', 'label']),
    0x40af1c: ('ESRegionFill', ['parent', 'rect', 'handler', 'fill', 'colour']),
    0x40b0fc: ('ESTitle', ['parent', 'rect', 'font', 'handler', 'colour', 'label', 'title_height']),
    0x40b590: ('ESAlert', ['parent', 'rect', 'font', 'handler', 'colour', 'label']),
    0x40b828: ('ESMessage', ['parent', 'rect', 'font', 'handler', 'text', 'align', 'colour', 'opaque', 'style']),
    0x40bc48: ('ESBitmap', ['parent', 'rect', 'bitmap', 'handler', 'colour']),
    0x40bd90: ('ESGrid', ['parent', 'rect', 'handler', 'colour']),
    0x40c1a4: ('ESDialog', ['parent', 'rect', 'font', 'handler']),
    0x40c928: ('ESArm', ['parent', 'rect', 'handler', 'colour']),
    0x40ce0c: ('ESAnim2', ['parent', 'rect', 'handler']),
    0x40d01c: ('ESPal', ['parent', 'rect', 'handler']),
}
# Methods called on an existing control whose arguments belong to the table.
METHODS = {
    0x40a8c0: ('quad_columns', ['font', 'text0', 'unused', 'align0', 'text1', 'x1', 'align1',
                                'text2', 'x2', 'align2', 'text3', 'x3', 'align3']),
    0x40cfb6: ('anim_add_frame', ['bitmap']),
    0x40ab3b: ('quad_set_colour', ['column', 'colour']),
}
# Further framework code that is interpreted when called (no control of its own).
INTERPRET = {
    0x41f8d7,   # parent of a window
    0x46b898,   # array construction (ESGrid rows)
    0x40bdf4, 0x40bd58,   # ESGrid row initialisation
    0x40d1bc,   # MISSION hot-spot record initialisation
    0x40b7e9, 0x40b808,   # modal box show / hide (parent and box)
    0x4325cc,   # REPAIR: handler table 0x48d2d8.. for its rows
    0x43c370, 0x43c671,   # pilot name / registration: skill label text
}
# Calls that stay uninterpreted but whose result has a meaning (named in formulas).
NAMED_RESULTS = {
    0x43fd87: 'weapon_slot',     # index of an item ID in the 27-word table 0x476a4c, else -1
    0x408bb5: 'load_lang_table',  # <lang>\<name> text table (wpn_desc.bin, wpn_info.bin)
}

# Window object fields used by the models (vshell_widgets.md §2.1).
F_VTABLE, F_NEXT, F_PREV, F_CHILD, F_FLAGS = 0x01, 0x05, 0x09, 0x0d, 0x11
F_ABS, F_RECT, F_STYLE, F_HANDLER = 0x15, 0x25, 0x39, 0x3d
FLAG_HEAD, FLAG_HIDDEN, FLAG_PENDING = 0x1, 0x2, 0x4

TEXT_LOOKUP = 0x4087f0          # (table [0x46ddf8], id) -> string pointer
LOAD_DBM, LOAD_DBA = 0x408992, 0x408aa1   # (name) -> bitmap (DBM) / bitmap array (DBA)
NEW, MALLOC, SEH_ENTER, ASSERT_FAIL, MEMSET = 0x4086b7, 0x40869c, 0x46b574, 0x44e560, 0x46b9c4
WINDOW_CTOR, SHOW, HIDE, SET_TEXT = 0x41f1b0, 0x41f93a, 0x41fabd, 0x40b8bb
DISPLAY_ROOT = 0x468660         # returns [[0x47f940]+5]
ROOT_GLOBAL = 0x481180          # VSHELL's root ESWindow (start-up 0x401f73)
STREAM_OPEN, STREAM_READ, STREAM_CLOSE, STREAM_FREE = 0x403065, 0x403985, 0x40323e, 0x4030e4

# Globals the constructors read fonts, the background and the text table from,
# filled by the start-up resource loader 0x40796c: (global, store site, push of
# the directory pointer, push of the name, loader). The generator checks each
# instruction and takes the names from the executable.
RESOURCE_GLOBALS = [
    (0x46ddfc, 0x407a19, 0x4079ff, 0x407a05, 0x4088da),
    (0x46de00, 0x407a66, 0x407a4c, 0x407a52, 0x4088da),
    (0x46de04, 0x407ab2, 0x407a98, 0x407a9e, 0x4088da),
    (0x46de0c, 0x407aff, 0x407ae5, 0x407aeb, LOAD_DBM),
    (0x46ddf8, 0x407b3d, None, 0x407b32, 0x408bb5),
]

# Conditional branches on run-time values inside the constructors: the side
# the table describes. Key: branch address; value: (taken?, reason).
BRANCH_ASSUMPTIONS = {
    # WEAPONS 0x43ebbc: two lists of ESBitmap images, one per record of
    # gam\arm_weap.dat (vshell_widgets.md §4.4); each loop is interpreted once
    # as the record template.
    0x43ee4c: (False, r'WEAPONS: first record list of gam\arm_weap.dat (count = the word before it); '
                      'interpreted once as the record template', ('enter', r'gam\arm_weap.dat list 1')),
    0x43ef57: (False, 'WEAPONS: leave the first record list after the template record', ('exit', None)),
    0x43ef7f: (False, r'WEAPONS: second record list of gam\arm_weap.dat; interpreted once as the template',
               ('enter', r'gam\arm_weap.dat list 2')),
    0x43f082: (False, 'WEAPONS: leave the second record list after the template record', ('exit', None)),
    # WEAPONS weapon rows: enabled (+0x49) = availability byte of the item
    # (0x483cde + 0x1d * item); the table records the test, not a value.
    0x43f86e: (False, 'WEAPONS weapon row: enabled = the item availability byte (run time)', ('runtime_field', 0x49)),
    0x43f9d0: (False, 'WEAPONS weapon row: enabled = the item availability byte (run time)', ('runtime_field', 0x49)),
    # ARMORY 0x4484c4: the same record list of gam\arm_weap.dat (first list only).
    0x448748: (False, r'ARMORY: first record list of gam\arm_weap.dat; interpreted once as the template',
               ('enter', r'gam\arm_weap.dat list 1')),
    0x448853: (False, 'ARMORY: leave the record list after the template record', ('exit', None)),
    # MISSION 0x442bc4: the frame rectangle depends on the display mode flag
    # [0x481f4c] (1 = full screen): full screen (0x117,0x2a,0x278,0x12b), window
    # (0x117,0x2b,0x278,0x12b). The table describes the window.
    0x442d80: (True, 'MISSION frame: window mode ([0x481f4c] = 0) rectangle; full screen moves the top edge to 0x2a',
               None),
}

TEXT_ARGS = ('text', 'label', 'text0', 'text1', 'text2', 'text3')
HEAP_BASE, HEAP_STEP = 0x10000000, 0x1000
STACK_TOP = 0x0ff00000
RETURN_SENTINEL = 0x0deadbe0


class GeneratorError(Exception):
    pass


# ---------------------------------------------------------------------------
# PE image

class Image:
    def __init__(self, data):
        if data[:2] != b'MZ':
            raise GeneratorError('not an MZ executable')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise GeneratorError('no PE header')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optional = struct.unpack_from('<H', data, pe + 20)[0]
        self.base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + 40 * i
            name = data[o:o + 8].rstrip(b'\0').decode('latin-1')
            vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
            flags = struct.unpack_from('<I', data, o + 36)[0]
            self.sections.append((name, self.base + rva, max(vsize, rsize), raw, rsize, flags))
        self.data = data

    def section(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + s[2]:
                return s
        return None

    def initialised(self, va):
        s = self.section(va)
        return s is not None and va - s[1] < s[4]

    def is_code(self, va):
        s = self.section(va)
        return s is not None and s[5] & 0x20 != 0

    def read(self, va, n):
        out = bytearray()
        for k in range(n):
            s = self.section(va + k)
            if s is None or va + k - s[1] >= s[4]:
                out.append(0)
            else:
                out.append(self.data[s[3] + va + k - s[1]])
        return bytes(out)

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def u16(self, va):
        return struct.unpack('<H', self.read(va, 2))[0]

    def cstr(self, va, limit=128):
        return self.read(va, limit).split(b'\0')[0].decode('latin-1')

    def imports(self):
        """IAT address -> 'dll!name' (import directory, data directory 1)."""
        if getattr(self, '_imports', None) is not None:
            return self._imports
        self._imports = {}
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        rva = struct.unpack_from('<I', self.data, pe + 24 + 96 + 8)[0]
        if not rva:
            return self._imports
        d = self.base + rva
        while True:
            ilt, _, _, name, iat = struct.unpack('<IIIII', self.read(d, 20))
            if not iat:
                break
            dll = self.cstr(self.base + name).lower()
            k = 0
            while True:
                entry = self.u32(self.base + (ilt or iat) + 4 * k)
                if not entry:
                    break
                fn = f'#{entry & 0xffff}' if entry & 0x80000000 else self.cstr(self.base + entry + 2)
                self._imports[self.base + iat + 4 * k] = f'{dll}!{fn}'
                k += 1
            d += 20
        return self._imports

    def rtti_name(self, vtable):
        """Borland RTTI: the type descriptor pointer sits 12 bytes before the vtable."""
        td = self.u32(vtable - 0xc)
        if not self.is_code(td):
            return None
        name = self.cstr(td + self.u16(td + 6), 64)
        return name if re.fullmatch(r'[A-Za-z_]\w*', name) else None


def load_reference(path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise GeneratorError(f'{path}: SHA-256 {digest} is not the reference VSHELL.EXE {EXPECTED_SHA256}')
    return data


# ---------------------------------------------------------------------------
# Listing

LINE = re.compile(r'^\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} ?)+?)\s*\t(\S+)(?:\s+(.*))?$')
SYMBOL = re.compile(r'\s*<[^>]*>')


class Insn:
    __slots__ = ('addr', 'raw', 'mnem', 'ops', 'text')

    def __init__(self, addr, raw, mnem, ops, text):
        self.addr, self.raw, self.mnem, self.ops, self.text = addr, raw, mnem, ops, text

    @property
    def next(self):
        return self.addr + len(self.raw)


def split_operands(text):
    out, depth, cur = [], 0, ''
    for ch in text:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_listing(text):
    """llvm-objdump -d -M intel lines -> {address: Insn}. Byte-continuation lines are merged."""
    insns, last = {}, None
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            addr = int(m.group(1), 16)
            raw = bytes.fromhex(m.group(2).replace(' ', ''))
            ops = SYMBOL.sub('', m.group(4) or '').strip()
            insn = Insn(addr, raw, m.group(3), split_operands(ops), (m.group(3) + ' ' + ops).strip())
            insns[addr] = insn
            last = insn
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} ?)+)\s*$', line)
        if m and last is not None and int(m.group(1), 16) == last.next:
            last.raw += bytes.fromhex(m.group(2).replace(' ', ''))
    return insns


class Listing:
    """Instructions by address; from a listing file or decoded on demand by llvm-objdump."""

    def __init__(self, image, objdump=None, text=None):
        self.image, self.objdump = image, objdump
        self.insns = parse_listing(text) if text is not None else {}
        self.chunks = []      # llvm-objdump output decoded on demand, for --write-listing
        self.checked = set()

    def decode(self, start, length=0x400):
        if self.objdump is None:
            raise GeneratorError(f'{start:#x} is not in the listing; regenerate it with --write-listing')
        out = subprocess.run([str(self.objdump), '-d', '-M', 'intel', f'--start-address={start:#x}',
                              f'--stop-address={start + length:#x}', '-'],
                             input=self.image.data, capture_output=True, check=True).stdout.decode('latin-1')
        self.chunks.append(out)
        got = parse_listing(out)
        if start not in got:
            raise GeneratorError(f'llvm-objdump decoded nothing at {start:#x}')
        # A chunk is decoded from its start, so it is aligned there; keep what
        # an earlier chunk had (also aligned) and add the rest.
        for a, insn in got.items():
            self.insns.setdefault(a, insn)

    def at(self, addr):
        insn = self.insns.get(addr)
        if insn is None:
            self.decode(addr)
            insn = self.insns[addr]
        if addr not in self.checked:
            if self.image.read(addr, len(insn.raw)) != insn.raw:
                raise GeneratorError(f'listing bytes at {addr:#x} differ from the executable')
            self.checked.add(addr)
        return insn

    def text(self):
        return '\n'.join(self.chunks) + '\n'


# ---------------------------------------------------------------------------
# Values

class Sym:
    """A value known only at run time, with its formula."""
    __slots__ = ('e',)

    def __init__(self, e):
        self.e = e

    def __repr__(self):
        return self.e


class Offset(Sym):
    """base + off, kept apart so that loads through it can be named."""
    __slots__ = ('base', 'off')

    def __init__(self, base, off):
        self.base, self.off = base, off
        super().__init__(f'({base.e}{"+" if off >= 0 else "-"}{abs(off):#x})')


class Frame(Sym):
    """Frame `index` of a DBA bitmap array (the array's frame table is at +8)."""
    __slots__ = ('res', 'index')

    def __init__(self, res, index):
        self.res, self.index = res, index
        super().__init__(f'frame({res.e},{fmt(index)})')


class FrameAddr(Sym):
    """The address of a frame pointer with a run-time index: frames + 4 * index."""
    __slots__ = ('res', 'index')

    def __init__(self, res, index):
        self.res, self.index = res, index
        super().__init__(f'&frame({res.e},{fmt(index)})')


class Low16(Sym):
    """A 32-bit register whose low word was written with `low`."""
    __slots__ = ('old', 'low')

    def __init__(self, old, low):
        self.old, self.low = old, low
        super().__init__(f'set16({fmt(old)},{fmt(low)})')


class Low8(Low16):
    """A 32-bit register whose low byte was written with `low`."""
    __slots__ = ()


class Scaled(Sym):
    """value * scale, kept apart for array indexing."""
    __slots__ = ('value', 'scale')

    def __init__(self, value, scale):
        self.value, self.scale = value, scale
        super().__init__(f'({fmt(value)}*{fmt(scale)})')


class Buffer(Sym):
    """A stack buffer's content when a control copied it."""
    __slots__ = ('what',)

    def __init__(self, what):
        self.what = what
        super().__init__(f'buffer({what})')


class Text(Sym):
    """The string pointer 0x4087f0 returns for an ESTEXT ID."""
    __slots__ = ('id',)

    def __init__(self, id_):
        self.id = id_
        super().__init__(f'estext({fmt(id_)})')


class Resource(Sym):
    """The object a DBM/DBA loader returns for a resource file name."""
    __slots__ = ('kind', 'name')

    def __init__(self, kind, name):
        self.kind, self.name = kind, name
        super().__init__(f'{kind}({name})')


class FileStream(Sym):
    """A file stream opened by 0x403065 on the stack; reads return FileValue symbols.
    The file is read in parts: part 0 from the start; a loop over run-time records
    starts a new part (offsets counted from the record's start), and the end of
    that loop another one (offsets counted from the end of the records)."""
    __slots__ = ('name', 'part', 'pos', 'record')

    def __init__(self, name):
        self.name, self.part, self.pos, self.record = name, 0, 0, False
        super().__init__(f'stream({name})')

    def next_part(self, record):
        self.part, self.pos, self.record = self.part + 1, 0, record


class FileValue(Sym):
    """`size` bytes read from `name`, part `part` at `offset` (see FileStream)."""
    __slots__ = ('name', 'part', 'offset', 'size', 'record')

    def __init__(self, fs, size):
        self.name, self.part, self.offset, self.size, self.record = fs.name, fs.part, fs.pos, size, fs.record
        where = f'record+{fs.pos:#x}' if fs.record else f'part{fs.part}+{fs.pos:#x}'
        super().__init__(f'file({fs.name},{where},{size})')


class Slot(Sym):
    """A field of a symbolic object: `base + off` read as a pointer-sized value."""
    __slots__ = ('base', 'off')

    def __init__(self, base, off, e):
        self.base, self.off = base, off
        super().__init__(e)


def fmt(v):
    if isinstance(v, Sym):
        return v.e
    return hex(v) if v >= 10 or v < 0 else str(v)


def is_num(v):
    return isinstance(v, int)


def signed(v, size=4):
    bits = size * 8
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def binop(op, a, b, size=4):
    mask = (1 << (size * 8)) - 1
    if is_num(a) and is_num(b):
        if op == '+':
            return (a + b) & mask
        if op == '-':
            return (a - b) & mask
        if op == '*':
            return (signed(a, size) * signed(b, size)) & mask
        if op == '&':
            return a & b
        if op == '|':
            return a | b
        if op == '^':
            return a ^ b
        if op == '<<':
            return (a << (b & 31)) & mask
        if op == '>>':
            return (a & mask) >> (b & 31)
        if op == 'sar':
            return (signed(a, size) >> (b & 31)) & mask
        raise GeneratorError(op)
    if op == '^' and a is b:
        return 0
    if op == '+' and isinstance(a, Slot) and a.e.startswith('frames(') and isinstance(b, Scaled) and b.scale == 4:
        return FrameAddr(a.base, b.value)
    if op in ('+', '-') and isinstance(a, Sym) and is_num(b):
        off = signed(b, size) if op == '+' else -signed(b, size)
        if isinstance(a, Offset):
            a, off = a.base, a.off + off
        if off == 0:
            return a
        return Offset(a, off)
    if op == '+' and is_num(b) and b == 0:
        return a
    if op == '+' and is_num(a) and a == 0:
        return b
    if op == '-' and is_num(b) and b == 0:
        return a
    if op == '+' and is_num(b) and signed(b, size) < 0:
        return Sym(f'({fmt(a)}-{fmt(-signed(b, size))})')
    if op == '-' and is_num(b) and signed(b, size) < 0:
        return Sym(f'({fmt(a)}+{fmt(-signed(b, size))})')
    if op == '*' and is_num(b) and b == 1:
        return a
    if op == '*' and is_num(b) and isinstance(a, Sym):
        return Scaled(a, b)
    if op == '*' and is_num(a) and a == 1:
        return b
    return Sym(f'({fmt(a)}{op}{fmt(b)})')


# ---------------------------------------------------------------------------
# Interpreter

REG32 = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')
REG16 = {'ax': 'eax', 'cx': 'ecx', 'dx': 'edx', 'bx': 'ebx', 'sp': 'esp', 'bp': 'ebp', 'si': 'esi', 'di': 'edi'}
REG8 = {'al': ('eax', 0), 'cl': ('ecx', 0), 'dl': ('edx', 0), 'bl': ('ebx', 0),
        'ah': ('eax', 8), 'ch': ('ecx', 8), 'dh': ('edx', 8), 'bh': ('ebx', 8)}
SIZES = {'byte': 1, 'word': 2, 'dword': 4, 'qword': 8, 'tbyte': 10}
MEM = re.compile(r'^(?:(byte|word|dword|qword|tbyte|xword) ptr )?(?:(\w\w):)?\[(.*)\]$')


class Control:
    def __init__(self, cid, obj, screen, site, ctor, args, owner):
        self.id, self.obj, self.screen, self.site, self.ctor = cid, obj, screen, site, ctor
        self.args, self.owner = args, owner
        self.globals = []
        self.methods = []
        self.fields = {}
        self.text = None
        self.shown = []
        self.template = None
        self.size = 0


class Emulator:
    def __init__(self, image, listing):
        self.image, self.listing = image, listing
        self.mem = {}                 # byte address -> int byte or (Sym, index)
        self.reg = {r: Sym(f'{r}0') for r in REG32}
        self.flags = None
        self.cf = Sym('cf')
        self.heap = HEAP_BASE
        self.objects = {}             # object address -> Control
        self.controls = []
        self.screen = None
        self.ctor_stack = []          # objects whose constructor is running
        self.method_stack = []        # objects whose METHODS call is running
        self.method_frames = []
        self.runtime = self.runtime_globals()
        self.assumptions_used = set()
        self.unknown_calls = []
        self.symmem = {}              # (formula of a run-time address, size) -> value written there
        self.const_reads = set()
        self.template = None          # set while a loop over run-time records runs once
        self.streams = []
        self.explore = None           # --list-branches: collect instead of stopping
        self.runtime_field = None     # (offset, formula) for the next field write by screen code
        self.in_model = False
        self.frames = []
        self.ctor_frames = []
        self.display = self.root = None
        self.buffers = {}             # stack buffer -> the call that may fill it

    # -- global classification ------------------------------------------------
    def runtime_globals(self):
        """Absolute addresses some instruction writes: run-time state, never read from the file."""
        if self.listing.objdump is not None and not self.listing.insns:
            for _, va, size, _, _, flags in self.image.sections:
                if flags & 0x20:
                    self.listing.decode(va, size)
        writes = set()
        for insn in self.listing.insns.values():
            if not insn.ops or insn.mnem in ('cmp', 'test', 'push', 'call', 'jmp', 'lea') or insn.mnem.startswith('j'):
                continue
            if insn.mnem.startswith('f') and not insn.mnem.startswith(('fst', 'fist', 'fnst', 'fbstp')):
                continue
            m = MEM.match(insn.ops[0])
            if not m:
                continue
            terms = [t.strip() for t in re.split(r'(?=[+-])', m.group(3).replace(' ', '')) if t.strip()]
            regs = [t for t in terms if re.fullmatch(r'[+-]?[a-z]{2,3}', t)]
            scaled = [t for t in terms if '*' in t]
            disp = sum(int(t, 16) for t in terms if re.fullmatch(r'[+-]?0x[0-9a-f]+', t))
            if regs or not self.image.section(disp & MASK):
                continue
            size = SIZES.get(m.group(1) or 'dword', 4)
            span = 0x400 if scaled else size
            for k in range(span):
                writes.add((disp + k) & MASK)
        return writes

    # -- registers --------------------------------------------------------------
    def get_reg(self, name):
        if name in self.reg:
            return self.reg[name]
        if name in REG16:
            v = self.reg[REG16[name]]
            if isinstance(v, Low16):
                return v.low
            return v & 0xffff if is_num(v) else Sym(f'lo16({fmt(v)})')
        if name in REG8:
            r, sh = REG8[name]
            v = self.reg[r]
            if isinstance(v, Low8) and sh == 0:
                return v.low
            return (v >> sh) & 0xff if is_num(v) else Sym(f'byte({fmt(v)},{sh // 8})')
        raise GeneratorError(f'register {name}')

    def set_reg(self, name, value):
        if name in self.reg:
            self.reg[name] = value if not is_num(value) else value & MASK
            return
        if name in REG16:
            r = REG16[name]
            old = self.reg[r]
            if is_num(old) and is_num(value):
                self.reg[r] = (old & 0xffff0000) | (value & 0xffff)
            else:
                self.reg[r] = Low16(old, value)
            return
        r, sh = REG8[name]
        old = self.reg[r]
        if is_num(old) and is_num(value):
            self.reg[r] = (old & ~(0xff << sh) & MASK) | ((value & 0xff) << sh)
        elif sh == 0:
            self.reg[r] = Low8(old, value)
        else:
            self.reg[r] = Sym(f'set8h({fmt(old)},{fmt(value)})')

    # -- memory -------------------------------------------------------------
    def address(self, expr):
        total = 0
        for term in re.split(r'(?=[+-])', expr.replace(' ', '')):
            if not term:
                continue
            sign = -1 if term[0] == '-' else 1
            term = term.lstrip('+-')
            if '*' in term:
                scale, r = term.split('*')
                v = binop('*', self.get_reg(r), int(scale))
            elif term.startswith('0x') or term.isdigit():
                v = int(term, 16) if term.startswith('0x') else int(term)
            else:
                v = self.get_reg(term)
            total = binop('+' if sign > 0 else '-', total, v)
        return total

    def load(self, addr, size):
        if not is_num(addr):
            return self.load_symbolic(addr, size)
        parts = [self.mem.get((addr + k) & MASK) for k in range(size)]
        if all(p is None for p in parts):
            if self.image.section(addr) is not None:
                if any(((addr + k) & MASK) in self.runtime for k in range(size)) or not self.image.initialised(addr):
                    return Sym(f'[{addr:#x}]')
                self.const_reads.add(addr)
                return int.from_bytes(self.image.read(addr, size), 'little')
            return Sym(f'[{addr:#x}]')
        if all(isinstance(p, int) for p in parts):
            return int.from_bytes(bytes(parts), 'little')
        first = parts[0]
        if isinstance(first, tuple) and first[1] == 0 and size == 4 and all(
                isinstance(p, tuple) and p[0] is first[0] and p[1] == k for k, p in enumerate(parts)):
            return first[0]
        if isinstance(first, tuple) and first[1] == 0 and getattr(first[0], 'size', 4) == size and all(
                isinstance(p, tuple) and p[0] is first[0] and p[1] == k for k, p in enumerate(parts)):
            return first[0]
        if isinstance(first, tuple) and size < 4 and all(
                isinstance(p, tuple) and p[0] is first[0] and p[1] == first[1] + k for k, p in enumerate(parts)):
            return Sym(f'part({fmt(first[0])},{first[1]},{size})')
        return Sym(f'[{addr:#x}]')

    def load_symbolic(self, addr, size):
        if (addr.e, size) in self.symmem:     # written earlier through the same run-time address
            return self.symmem[(addr.e, size)]
        base, off = (addr.base, addr.off) if isinstance(addr, Offset) else (addr, 0)
        if isinstance(base, Resource) and off == 8:
            return Slot(base, off, f'frames({base.e})')
        if isinstance(base, FileStream) and size == 4:
            return Slot(base, off, f'{base.e}.method{off:#x}')
        if isinstance(base, Slot) and base.e.startswith('frames(') and off % 4 == 0 and size == 4:
            return Frame(base.base, off // 4)
        if isinstance(addr, FrameAddr):
            return Frame(addr.res, addr.index)
        if isinstance(base, Frame) and off in (4, 6) and size == 2:
            return Sym(f'{"height" if off == 4 else "width"}({base.e})')
        return Sym(f'[{addr.e}]' if size == 4 else f'{("", "byte", "word")[size] if size < 3 else "mem"}[{addr.e}]')

    def store(self, addr, value, size):
        if not is_num(addr):
            if is_num(value) and value in self.objects and size == 4:
                self.objects[value].globals.append(addr.e)
            self.symmem[(addr.e, size)] = value
            return
        if is_num(value):
            for k in range(size):
                self.mem[(addr + k) & MASK] = (value >> (8 * k)) & 0xff
        else:
            for k in range(size):
                self.mem[(addr + k) & MASK] = (value, k)
        self.note_store(addr, value, size)

    def note_store(self, addr, value, size):
        # an object pointer stored into a global names the control
        if is_num(value) and value in self.objects and size == 4 and not (HEAP_BASE <= addr < STACK_TOP - 0x100000):
            if self.image.section(addr) is not None:
                c = self.objects[value]
                if addr not in c.globals:
                    c.globals.append(addr)
        # a field written by screen code (not by the control's own constructor)
        if HEAP_BASE <= addr < self.heap:
            base = HEAP_BASE + ((addr - HEAP_BASE) & ~(HEAP_STEP - 1))
            while base >= HEAP_BASE and base not in self.objects:
                base -= HEAP_STEP
            c = self.objects.get(base)
            if c is not None and addr < base + c.size and not self.frames and not self.in_model:
                if self.runtime_field is not None and self.runtime_field[0] == addr - base:
                    value = self.runtime_field[1]
                    self.runtime_field = None
                c.fields[addr - base] = (size, value)

    def operand(self, op, size=None):
        op = op.strip()
        m = MEM.match(op)
        if m:
            if m.group(2) == 'fs':
                return ('fs', None, SIZES.get(m.group(1) or 'dword', 4))
            return ('mem', self.address(m.group(3)), SIZES.get(m.group(1), size or 4) if m.group(1) else (size or 4))
        if op in self.reg or op in REG16 or op in REG8:
            return ('reg', op, 4 if op in self.reg else 2 if op in REG16 else 1)
        if re.fullmatch(r'-?0x[0-9a-f]+|-?\d+', op):
            v = int(op, 16) if '0x' in op else int(op)
            return ('imm', v & MASK, size or 4)
        raise GeneratorError(f'operand {op!r} at {self.cur:#x}')

    def read(self, opnd):
        kind, x, size = opnd
        if kind == 'reg':
            return self.get_reg(x)
        if kind == 'imm':
            return x & ((1 << (8 * size)) - 1) if size < 4 else x
        if kind == 'fs':
            return Sym('fs')
        return self.load(x, size)

    def write(self, opnd, value):
        kind, x, size = opnd
        if kind == 'reg':
            self.set_reg(x, value)
        elif kind == 'mem':
            if is_num(value):
                value &= (1 << (8 * size)) - 1
            self.store(x, value, size)
        elif kind == 'fs':
            pass
        else:
            raise GeneratorError(f'write to immediate at {self.cur:#x}')

    def flags_of(self, obj):
        v = self.load(obj + F_FLAGS, 4)
        return v if is_num(v) else 0

    def or_flags(self, obj, set_bits, clear_bits=0):
        self.store(obj + F_FLAGS, (self.flags_of(obj) | set_bits) & ~clear_bits & MASK, 4)

    def push(self, value):
        esp = (self.reg['esp'] - 4) & MASK
        self.reg['esp'] = esp
        self.store(esp, value, 4)

    def pop(self):
        esp = self.reg['esp']
        v = self.load(esp, 4)
        self.reg['esp'] = (esp + 4) & MASK
        return v

    def arg(self, i):
        return self.load((self.reg['esp'] + 4 * i) & MASK, 4)   # before the call pushes its return address

    # -- flags --------------------------------------------------------------
    def cond(self, cc):
        kind, a, b, res, size = self.flags
        if kind == 'logic' and a is b and isinstance(a, (Text, Resource)) and cc in ('e', 'z', 'ne', 'nz'):
            # a loaded resource or a looked-up string: the pointer is not null
            # (the loaders assert on failure: 0x408992/0x408aa1 -> 0x44e560)
            return cc in ('ne', 'nz')
        if not (is_num(a) and is_num(b) and is_num(res)):
            return None
        bits = size * 8
        sa, sb, sr = signed(a, size), signed(b, size), signed(res, size)
        zf = (res & ((1 << bits) - 1)) == 0
        sf = sr < 0
        if kind == 'sub':
            cf = (a & ((1 << bits) - 1)) < (b & ((1 << bits) - 1))
            of = (sa - sb) != sr
        elif kind == 'add':
            cf = self.cf if is_num(self.cf) else 0
            of = False
        else:
            cf, of = False, False
        table = {'e': zf, 'z': zf, 'ne': not zf, 'nz': not zf, 's': sf, 'ns': not sf,
                 'l': sf != of, 'ge': sf == of, 'le': zf or sf != of, 'g': not zf and sf == of,
                 'b': cf, 'c': cf, 'ae': not cf, 'nc': not cf, 'be': cf or zf, 'a': not cf and not zf}
        if cc not in table:
            raise GeneratorError(f'condition {cc} at {self.cur:#x}')
        return bool(table[cc])

    def set_flags(self, kind, a, b, res, size):
        self.flags = (kind, a, b, res, size)

    # -- calls ----------------------------------------------------------------
    def new_object(self, size):
        obj = self.heap
        self.heap += max(HEAP_STEP, (size + 0xfff) & ~0xfff)
        for k in range(size):
            self.mem[obj + k] = 0
        return obj

    def call(self, target, site):
        """Returns True when the callee is interpreted (return address pushed)."""
        if isinstance(target, Slot) and isinstance(target.base, FileStream) and target.off == 0x18:
            self.after_model(m_stream_read(self, site))   # virtual read(stream, size, dst)
            return False
        if not is_num(target):
            self.unknown_calls.append((self.screen, site, fmt(target), [fmt(self.arg(i)) for i in range(4)]))
            self.after_model(Sym(f'ret@{site:#x}'))
            return False
        if target in CTORS and is_num(self.arg(0)) and self.arg(0) in self.objects \
                and self.objects[self.arg(0)].ctor is None:
            self.begin_control(target, site)
            return self.enter(target, site)
        if target in CTORS or target in INTERPRET or target in METHODS:
            if target in METHODS:
                self.record_method(target, site)
                obj = self.arg(0)
                self.method_stack.append(obj if is_num(obj) and obj in self.objects else None)
                self.method_frames.append(len(self.frames) + 1)
            return self.enter(target, site)
        model = MODELS.get(target)
        if model is not None:
            self.in_model = True
            try:
                self.after_model(model(self, site))
            finally:
                self.in_model = False
            return False
        args = [self.arg(i) for i in range(4)]
        name = self.call_name(target)
        if name.endswith('!_sprintf') and is_num(args[0]) and is_num(args[1]):
            # sprintf(buffer, format, ...): the buffer holds that format applied to these values
            extra = [self.arg(i) for i in range(2, 4)]
            self.buffers[args[0]] = {'sprintf': f'{args[1]:#x}', 'values': [
                signed(v) if is_num(v) else fmt(v) for v in extra[:self.image.cstr(args[1]).count('%')]]}
        else:
            for a in args:
                if is_num(a) and STACK_TOP - 0x100000 <= a < STACK_TOP and a >= self.reg['esp']:
                    self.buffers[a] = {'written_by': f'{name}@{site:#x}'}
        self.unknown_calls.append((self.screen, site, name, [fmt(a) for a in args]))
        if target in NAMED_RESULTS:
            self.after_model(Sym(f'{NAMED_RESULTS[target]}({fmt(args[0])})'))
        else:
            self.after_model(Sym(f'ret_{target:x}@{site:x}'))
        return False

    def call_name(self, target):
        """An import thunk (jmp [IAT]) is named after its import."""
        try:
            insn = self.listing.at(target)
        except GeneratorError:
            return f'{target:#x}'
        m = MEM.match(insn.ops[0]) if insn.mnem == 'jmp' and insn.ops else None
        if m and re.fullmatch(r'0x[0-9a-f]+', m.group(3).strip()):
            imp = self.image.imports().get(int(m.group(3), 16))
            if imp:
                return imp
        return f'{target:#x}'

    def enter(self, target, site):
        self.push(self.ret_addr)
        self.frames.append((target, self.reg['esp']))
        self.pc = target
        return True

    def after_model(self, value):
        self.reg['eax'] = value
        self.reg['ecx'] = Sym(f'ecx@{self.cur:x}')
        self.reg['edx'] = Sym(f'edx@{self.cur:x}')

    def begin_control(self, target, site):
        cls, names = CTORS[target]
        obj = self.arg(0)
        c = self.objects[obj]
        c.ctor, c.site = target, site
        c.args = {name: self.snapshot(self.arg(i + 1)) for i, name in enumerate(names)}
        rect = c.args.get('rect')
        c.rect = [self.load((rect + 4 * k) & MASK, 4) for k in range(4)] if is_num(rect) else None
        owners = self.ctor_stack or self.method_stack
        c.owner = owners[-1] if owners else None
        self.ctor_stack.append(obj)
        self.ctor_frames.append(len(self.frames) + 1)

    def snapshot(self, v):
        """A stack buffer passed to a control: what it holds now (it may be reused later)."""
        if is_num(v) and STACK_TOP - 0x100000 <= v < STACK_TOP and v in self.buffers:
            return Buffer(self.buffers[v])
        return v

    def record_method(self, target, site):
        name, names = METHODS[target]
        obj = self.arg(0)
        if is_num(obj) and obj in self.objects:
            self.objects[obj].methods.append((name, site, {n: self.snapshot(self.arg(i + 1))
                                                           for i, n in enumerate(names)}))
        else:
            self.unknown_calls.append((self.screen, site, f'{target:#x}', [fmt(self.arg(i)) for i in range(4)]))

    # -- main loop --------------------------------------------------------------
    def seed_root(self):
        """The two run-time windows every screen hangs from (start-up 0x401f1f..0x401f89):
        the display's root window ([[0x47f940]+5], 0x468660) and VSHELL's root ESWindow
        [0x481180], built with the display root's absolute rectangle as its rectangle.
        Both are taken at the screen origin; their size stays symbolic."""
        rect = [0, 0, Sym('display_right'), Sym('display_bottom')]
        objs = []
        for name in ('display', 'root'):
            obj = self.new_object(0x45)
            c = Control(name, obj, None, None, None, {}, None)
            c.size = 0x45
            c.rect = rect
            c.args = {'parent': objs[-1] if objs else 0}
            self.objects[obj] = c
            for k in range(4):
                self.store(obj + F_ABS + 4 * k, rect[k], 4)
                self.store(obj + F_RECT + 4 * k, rect[k], 4)
            objs.append(obj)
        self.display, self.root = objs
        self.store(ROOT_GLOBAL, self.root, 4)

    def run(self, entry, screen):
        self.screen = screen
        self.frames = []
        self.ctor_frames = []
        self.method_frames = []
        self.method_stack = []
        self.reg['esp'] = STACK_TOP
        self.reg['ebp'] = Sym('ebp0')
        self.push(RETURN_SENTINEL)
        self.pc = entry
        steps = 0
        while True:
            steps += 1
            if steps > 400000:
                raise GeneratorError(f'{screen}: no end after {steps} instructions')
            insn = self.listing.at(self.pc)
            self.cur = insn.addr
            self.ret_addr = insn.next
            nxt = self.step(insn)
            if nxt == RETURN_SENTINEL:
                return
            self.pc = nxt

    def leave_frame(self):
        self.frames.pop()
        while self.ctor_frames and self.ctor_frames[-1] > len(self.frames):
            self.ctor_frames.pop()
            self.ctor_stack.pop()
        while self.method_frames and self.method_frames[-1] > len(self.frames):
            self.method_frames.pop()
            self.method_stack.pop()

    def branch(self, insn, cc):
        taken = self.cond(cc)
        if taken is None:
            if insn.addr in BRANCH_ASSUMPTIONS:
                taken, _, effect = BRANCH_ASSUMPTIONS[insn.addr]
                self.assumptions_used.add(insn.addr)
                if effect is not None and effect[0] == 'enter':
                    self.template = effect[1]
                    for fs in self.streams:
                        fs.next_part(True)
                elif effect is not None and effect[0] == 'exit':
                    self.template = None
                    for fs in self.streams:
                        fs.next_part(False)
                elif effect is not None and effect[0] == 'runtime_field':
                    kind, a, b, res, size = self.flags
                    self.runtime_field = (effect[1], Sym(f'({fmt(a)}!=0)'))
            elif self.explore is not None:
                kind, a, b, res, size = self.flags
                self.explore.append(f'{self.screen}: {insn.addr:#x} {insn.text}; {kind} {fmt(a)} {fmt(b)}')
                taken = False
            else:
                kind, a, b, res, size = self.flags
                raise GeneratorError(f'{self.screen}: branch on a run-time value at {insn.addr:#x} '
                                     f'({insn.text}; {kind} {fmt(a)} {fmt(b)}): add it to BRANCH_ASSUMPTIONS')
        return int(insn.ops[0], 16) if taken else insn.next

    def step(self, insn):
        mn, ops = insn.mnem, insn.ops
        nxt = insn.next
        if mn == 'push':
            o = self.operand(ops[0])
            self.push(self.read(o) if o[0] != 'imm' else o[1])
        elif mn == 'pop':
            v = self.pop()
            self.write(self.operand(ops[0]), v)
        elif mn == 'mov':
            dst = self.operand(ops[0])
            src = self.operand(ops[1], dst[2])
            self.write(dst, self.read(src))
        elif mn in ('movsx', 'movzx'):
            dst = self.operand(ops[0])
            src = self.operand(ops[1], 2)
            v = self.read(src)
            if is_num(v) and mn == 'movsx':
                v = signed(v, src[2]) & MASK
            self.write(dst, v)
        elif mn == 'lea':
            m = MEM.match(ops[1])
            self.write(self.operand(ops[0]), self.address(m.group(3)))
        elif mn in ('add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb'):
            dst = self.operand(ops[0])
            src = self.operand(ops[1], dst[2])
            a, b = self.read(dst), self.read(src)
            if src[0] == 'imm' and src[2] < dst[2]:
                b = signed(b, src[2]) & MASK
            if mn == 'adc':
                b = binop('+', b, self.cf if is_num(self.cf) else Sym('cf'))
            if mn == 'sbb':
                b = binop('+', b, self.cf if is_num(self.cf) else Sym('cf'))
            op = {'add': '+', 'adc': '+', 'sub': '-', 'sbb': '-', 'and': '&', 'or': '|', 'xor': '^'}[mn]
            if mn == 'xor' and src == dst:
                res = 0
            else:
                res = binop(op, a, b, dst[2])
            self.write(dst, res)
            if mn in ('sub', 'sbb'):
                self.set_flags('sub', a, b, res, dst[2])
            elif mn in ('add', 'adc'):
                self.cf = (is_num(a) and is_num(b) and (a & MASK) + (b & MASK) > MASK) if is_num(a) and is_num(b) else Sym('cf')
                self.set_flags('add', a, b, res, dst[2])
            else:
                self.set_flags('logic', a, b, res, dst[2])
        elif mn in ('inc', 'dec'):
            dst = self.operand(ops[0])
            a = self.read(dst)
            res = binop('+' if mn == 'inc' else '-', a, 1, dst[2])
            self.write(dst, res)
            self.set_flags('sub' if mn == 'dec' else 'add', a, 1, res, dst[2])
        elif mn == 'neg':
            dst = self.operand(ops[0])
            a = self.read(dst)
            res = binop('-', 0, a, dst[2])
            self.write(dst, res)
            self.set_flags('sub', 0, a, res, dst[2])
        elif mn == 'not':
            dst = self.operand(ops[0])
            a = self.read(dst)
            self.write(dst, (~a) & ((1 << (8 * dst[2])) - 1) if is_num(a) else Sym(f'~{fmt(a)}'))
        elif mn in ('shl', 'sal', 'shr', 'sar'):
            dst = self.operand(ops[0])
            cnt = self.read(self.operand(ops[1], 1)) if len(ops) > 1 else 1
            a = self.read(dst)
            op = {'shl': '<<', 'sal': '<<', 'shr': '>>', 'sar': 'sar'}[mn]
            res = binop(op, a, cnt, dst[2])
            if is_num(a) and is_num(cnt) and cnt:
                self.cf = (a >> (cnt - 1)) & 1 if mn != 'shl' and mn != 'sal' else (a >> (8 * dst[2] - cnt)) & 1
            else:
                self.cf = Sym('cf')
            self.write(dst, res)
            self.set_flags('logic', a, cnt, res, dst[2])
        elif mn == 'imul':
            if len(ops) == 1:
                src = self.read(self.operand(ops[0]))
                a = self.reg['eax']
                if is_num(a) and is_num(src):
                    full = signed(a) * signed(src)
                    self.reg['eax'] = full & MASK
                    self.reg['edx'] = (full >> 32) & MASK
                else:
                    self.reg['eax'] = binop('*', a, src)
                    self.reg['edx'] = Sym('hi')
            else:
                dst = self.operand(ops[0])
                a = self.read(self.operand(ops[1])) if len(ops) == 3 else self.read(dst)
                b = self.read(self.operand(ops[2] if len(ops) == 3 else ops[1]))
                self.write(dst, binop('*', a, b))
        elif mn == 'cdq':
            a = self.reg['eax']
            self.reg['edx'] = (MASK if signed(a) < 0 else 0) if is_num(a) else Sym(f'sign({fmt(a)})')
        elif mn in ('cwde', 'cwtl'):
            a = self.reg['eax']
            self.reg['eax'] = signed(a & 0xffff, 2) & MASK if is_num(a) else Sym(f'sx16({fmt(a)})')
        elif mn in ('idiv', 'div'):
            d = self.read(self.operand(ops[0]))
            a, h = self.reg['eax'], self.reg['edx']
            if is_num(a) and is_num(h) and is_num(d) and d:
                n = (h << 32) | a
                if mn == 'idiv':
                    n = n - (1 << 64) if n >> 63 else n
                    q = abs(n) // abs(signed(d)) * (1 if (n < 0) == (signed(d) < 0) else -1)
                    r = n - q * signed(d)
                else:
                    q, r = n // d, n % d
                self.reg['eax'], self.reg['edx'] = q & MASK, r & MASK
            else:
                self.reg['eax'] = Sym(f'({fmt(a)}/{fmt(d)})')
                self.reg['edx'] = Sym(f'({fmt(a)}%{fmt(d)})')
        elif mn in ('cmp', 'test'):
            a = self.operand(ops[0])
            b = self.operand(ops[1], a[2])
            va, vb = self.read(a), self.read(b)
            if b[0] == 'imm' and b[2] < a[2]:
                vb = signed(vb, b[2]) & MASK
            if mn == 'cmp':
                self.set_flags('sub', va, vb, binop('-', va, vb, a[2]), a[2])
            else:
                self.set_flags('logic', va, vb, binop('&', va, vb, a[2]) if not (va is vb) else va, a[2])
        elif mn.startswith('set') and len(mn) > 3:
            r = self.cond(mn[3:])
            self.write(self.operand(ops[0]), int(r) if r is not None else Sym(f'{mn}'))
        elif mn.startswith('j') and mn != 'jmp':
            return self.branch(insn, mn[1:])
        elif mn == 'jmp':
            o = self.operand(ops[0])
            if o[0] == 'imm':
                return o[1]
            t = self.read(o)
            if not is_num(t):
                raise GeneratorError(f'{self.screen}: jump through a run-time value at {insn.addr:#x}')
            return t
        elif mn == 'call':
            o = self.operand(ops[0])
            target = o[1] if o[0] == 'imm' else self.read(o)
            if self.call(target, insn.addr):
                return self.pc
        elif mn == 'ret':
            ret = self.pop()
            if ops:
                self.reg['esp'] = (self.reg['esp'] + int(ops[0], 16)) & MASK
            if self.frames:
                self.leave_frame()
            if not is_num(ret):
                raise GeneratorError(f'{self.screen}: return through a run-time value at {insn.addr:#x}')
            return ret
        elif mn == 'leave':
            self.reg['esp'] = self.reg['ebp']
            self.reg['ebp'] = self.pop()
        elif mn == 'xchg':
            a, b = self.operand(ops[0]), self.operand(ops[1])
            va, vb = self.read(a), self.read(b)
            self.write(a, vb)
            self.write(b, va)
        elif mn == 'nop':
            pass
        elif mn == 'rep' or mn.startswith('movs') or mn.startswith('stos'):
            self.string_op(insn)
        else:
            raise GeneratorError(f'{self.screen}: instruction not modelled at {insn.addr:#x}: {insn.text}')
        return nxt

    def string_op(self, insn):
        words = insn.text.split()
        rep = words[0] == 'rep'
        op = words[1] if rep else words[0]
        size = {'movsd': 4, 'movsw': 2, 'movsb': 1, 'stosd': 4, 'stosw': 2, 'stosb': 1}.get(op)
        if size is None:
            raise GeneratorError(f'string instruction at {insn.addr:#x}: {insn.text}')
        count = self.reg['ecx'] if rep else 1
        if not is_num(count):
            raise GeneratorError(f'string instruction with a run-time count at {insn.addr:#x}')
        for _ in range(count):
            if op.startswith('movs'):
                self.store(self.reg['edi'], self.load(self.reg['esi'], size), size)
                self.reg['esi'] = binop('+', self.reg['esi'], size)
            else:
                self.store(self.reg['edi'], self.get_reg({4: 'eax', 2: 'ax', 1: 'al'}[size]), size)
            self.reg['edi'] = binop('+', self.reg['edi'], size)
        if rep:
            self.reg['ecx'] = 0


# ---------------------------------------------------------------------------
# Models of framework functions (cdecl; return value or None)

def m_new(emu, site):
    size = emu.arg(0)
    if not is_num(size):
        raise GeneratorError(f'operator new with a run-time size at {site:#x}')
    obj = emu.new_object(size)
    c = Control(None, obj, emu.screen, None, None, {}, None)
    c.size = size
    c.template = emu.template
    emu.objects[obj] = c
    return obj


def m_malloc(emu, site):
    size = emu.arg(0)
    return emu.new_object(size if is_num(size) else 0x100)


def m_nothing(emu, site):
    return Sym(f'ret@{site:x}')


def m_text(emu, site):
    return Text(emu.arg(1))


def m_resource(kind):
    def model(emu, site):
        name = emu.arg(0)
        if not is_num(name):
            return Resource(kind, fmt(name))
        return Resource(kind, emu.image.cstr(name))
    return model


def m_display_root(emu, site):
    return emu.display


def m_window_ctor(emu, site):
    """0x41f1b0(this, parent, rect, handler): rect copied to +0x25, handler to +0x3d,
    linked in front of the parent's children (0x41f788), absolute rectangle
    (0x41f599), event mask |= 0x1f, hidden. No events, no assertions."""
    this, parent, rect, handler = (emu.arg(i) for i in range(4))
    for k in (F_NEXT, F_PREV, F_CHILD):
        emu.store(this + k, 0, 4)
    for k in range(4):
        emu.store(this + F_RECT + 4 * k, emu.load((rect + 4 * k) & MASK, 4) if is_num(rect) else Sym('rect'), 4)
    if is_num(parent) and parent in emu.objects:
        first = emu.load(parent + F_CHILD, 4)
        emu.store(this + F_PREV, parent, 4)
        emu.store(this + F_NEXT, first, 4)
        emu.store(parent + F_CHILD, this, 4)
        emu.or_flags(this, FLAG_HEAD)
        if is_num(first) and first:
            emu.or_flags(first, 0, FLAG_HEAD)
            emu.store(first + F_PREV, this, 4)
    elif parent != 0:
        emu.store(this + F_PREV, parent, 4)      # a run-time window: kept as a symbol
        emu.or_flags(this, FLAG_HEAD)
    # absolute rectangle (0x41f599): the parent's absolute left/top plus the own rectangle
    if is_num(parent) and parent in emu.objects:
        origin = (emu.load(parent + F_ABS, 4), emu.load(parent + F_ABS + 4, 4))
    else:
        origin = (Sym(f'[{fmt(parent)}+0x15]'), Sym(f'[{fmt(parent)}+0x19]'))
    for k in range(4):
        emu.store(this + F_ABS + 4 * k, binop('+', origin[k % 2], emu.load(this + F_RECT + 4 * k, 4)), 4)
    style = emu.load(this + F_STYLE, 4)
    emu.store(this + F_STYLE, (style | 0x1f) if is_num(style) else style, 4)
    emu.store(this + F_HANDLER, handler, 4)
    emu.or_flags(this, FLAG_HIDDEN)
    return this


def parent_of(emu, obj):
    """0x41f8d7: back along +9 to the list head, whose +9 is the parent."""
    while True:
        prev = emu.load(obj + F_PREV, 4)
        if not is_num(prev) or prev == 0 or prev not in emu.objects:
            return prev
        if emu.flags_of(obj) & FLAG_HEAD:
            return prev
        obj = prev


def hidden(emu, obj):
    return not (is_num(obj) and obj in emu.objects) or bool(emu.flags_of(obj) & FLAG_HIDDEN)


def children(emu, obj):
    c = emu.load(obj + F_CHILD, 4)
    while is_num(c) and c:
        yield c
        c = emu.load(c + F_NEXT, 4)


def show(emu, obj):
    """0x41f93a without its events: shown only when the parent is shown, else marked pending."""
    if not hidden(emu, obj):
        return
    parent = parent_of(emu, obj)
    if is_num(parent) and parent in emu.objects and hidden(emu, parent):
        emu.or_flags(obj, FLAG_HIDDEN | FLAG_PENDING)
        return
    emu.or_flags(obj, 0, FLAG_HIDDEN | FLAG_PENDING)
    for c in children(emu, obj):
        if emu.flags_of(c) & FLAG_PENDING:
            show(emu, c)


def hide(emu, obj):
    """0x41fabd without its events: a hidden window loses its pending mark; shown children become pending."""
    if hidden(emu, obj):
        emu.or_flags(obj, 0, FLAG_PENDING)
        return
    emu.or_flags(obj, FLAG_HIDDEN)
    parent = parent_of(emu, obj)
    if is_num(parent) and parent in emu.objects and hidden(emu, parent):
        emu.or_flags(obj, FLAG_PENDING)
    for c in children(emu, obj):
        if not hidden(emu, c):
            hide(emu, c)


def m_show(emu, site):
    obj = emu.arg(0)
    if is_num(obj) and obj in emu.objects:
        if not emu.frames:
            emu.objects[obj].shown.append(('show', site))
        show(emu, obj)
    return Sym(f'ret@{site:x}')


def m_hide(emu, site):
    obj = emu.arg(0)
    if is_num(obj) and obj in emu.objects:
        if not emu.frames:
            emu.objects[obj].shown.append(('hide', site))
        hide(emu, obj)
    return Sym(f'ret@{site:x}')


def m_set_text(emu, site):
    """0x40b8bb(this, text, align, colour, redraw): text copied to +0x4d, align +0x45, colour +0xb5."""
    obj, text, align, colour = (emu.arg(i) for i in range(4))
    if is_num(obj) and obj in emu.objects:
        c = emu.objects[obj]
        c.text = emu.snapshot(text)
        emu.store(obj + 0x45, align, 4)
        emu.store(obj + 0xb5, colour, 4)
    return Sym(f'ret@{site:x}')


def m_stream_open(emu, site):
    """0x403065(stream, name): a file stream on the caller's stack."""
    stream, name = emu.arg(0), emu.arg(1)
    fs = FileStream(emu.image.cstr(name) if is_num(name) else fmt(name))
    if is_num(stream):
        emu.store(stream, fs, 4)
    return stream


def m_stream_read(emu, site):
    """read(stream, size, dst): 0x403985 directly, or slot 0x18 of the stream's vtable."""
    stream, size, dst = emu.arg(0), emu.arg(1), emu.arg(2)
    fs = emu.load(stream, 4) if is_num(stream) else None
    if not isinstance(fs, FileStream) or not is_num(size) or not is_num(dst):
        raise GeneratorError(f'stream read at {site:#x} not understood')
    emu.streams.append(fs) if fs not in emu.streams else None
    for k in range(0, size, 4):
        n = min(4, size - k)
        emu.store(dst + k, FileValue(fs, n), n)
        fs.pos += n
    return 1


MODELS = {
    NEW: m_new, MALLOC: m_malloc, SEH_ENTER: m_nothing, ASSERT_FAIL: m_nothing, MEMSET: m_nothing,
    TEXT_LOOKUP: m_text, LOAD_DBM: m_resource('dbm'), LOAD_DBA: m_resource('dba'),
    DISPLAY_ROOT: m_display_root, WINDOW_CTOR: m_window_ctor, SHOW: m_show, HIDE: m_hide,
    SET_TEXT: m_set_text, STREAM_OPEN: m_stream_open, STREAM_READ: m_stream_read,
    STREAM_CLOSE: m_nothing, STREAM_FREE: m_nothing,
}


# ---------------------------------------------------------------------------
# Table

def value_json(emu, v, kind=None):
    """A constructor argument as JSON: numbers stay numbers, everything else says what it is."""
    if isinstance(v, Text):
        return {'estext': v.id if is_num(v.id) else {'formula': v.id.e}}
    if isinstance(v, Resource):
        return {v.kind: v.name}
    if isinstance(v, Buffer):
        return {'local_buffer': v.what}
    if isinstance(v, Frame):
        return {v.res.kind: v.res.name, 'frame': v.index if is_num(v.index) else value_json(emu, v.index)}
    if isinstance(v, FileValue):
        return {'file': v.name, 'record' if v.record else 'part': v.offset if v.record else [v.part, v.offset],
                'size': v.size}
    if isinstance(v, Sym):
        m = re.fullmatch(r'\[(0x[0-9a-f]+)\]', v.e)
        if m:
            return {'global': m.group(1)}
        if v.e == 'display_root':
            return {'display_root': True}
        return {'formula': v.e}
    if v in emu.objects and emu.objects[v].id is not None:
        return {'control': emu.objects[v].id}
    if STACK_TOP - 0x100000 <= v < STACK_TOP:
        return {'local_buffer': 'not written by the constructor'}
    if kind in ('handler',) and v:
        return f'{v:#x}'
    if kind in TEXT_ARGS and v and emu.image.section(v):
        # a literal of the executable: its address and length, never its characters
        return {'exe_string': f'{v:#x}', 'length': len(emu.image.cstr(v, 256))}
    if v and emu.image.is_code(v):
        return {'code': f'{v:#x}'}
    return signed(v)


def build_table(emu):
    screens = []
    by_screen = {}
    for c in emu.controls:
        by_screen.setdefault(c.screen, []).append(c)
    for name, entry in SCREENS:
        rows = []
        for c in by_screen.get(name, []):
            vt = emu.load(c.obj + F_VTABLE, 4)
            cls = emu.image.rtti_name(vt) if is_num(vt) else None
            row = {'id': c.id, 'class': cls, 'ctor': f'{c.ctor:#x}', 'site': f'{c.site:#x}'}
            if c.owner is not None:
                row['owner'] = emu.objects[c.owner].id
            row['globals'] = [g if isinstance(g, str) else f'{g:#x}' for g in c.globals]
            names = CTORS[c.ctor][1]
            row['parent'] = value_json(emu, c.args['parent'])
            row['rect'] = [value_json(emu, v) for v in c.rect] if c.rect else None
            row['screen_rect'] = screen_rect(emu, c)
            row['args'] = {k: value_json(emu, c.args[k], k) for k in names if k not in ('parent', 'rect')}
            if c.text is not None and value_json(emu, c.args.get('text'), 'text') != value_json(emu, c.text, 'text'):
                row['text'] = value_json(emu, c.text, 'text')
            flags = emu.flags_of(c.obj)
            if not flags & FLAG_HIDDEN:
                row['after_construction'] = 'shown'
            else:
                row['after_construction'] = 'pending' if flags & FLAG_PENDING else 'hidden'
            if c.shown:
                row['screen_show_hide'] = [f'{what}@{site:#x}' for what, site in c.shown]
            if c.template is not None:
                row['per_record'] = c.template
            handler = c.fields.get(F_HANDLER, (4, c.args.get('handler', 0)))[1]
            row['handler'] = value_json(emu, handler, 'handler')
            if c.methods:
                row['methods'] = [{'name': n, 'site': f'{s:#x}',
                                   'args': {k: value_json(emu, v, k) for k, v in a.items()}}
                                  for n, s, a in c.methods]
            if c.fields:
                row['fields'] = {f'{off:#x}': {'size': size, 'value': value_json(emu, v)}
                                 for off, (size, v) in sorted(c.fields.items()) if off != F_VTABLE}
            rows.append(row)
        screens.append({'name': name, 'constructor': f'{entry:#x}', 'controls': rows})
    return screens


def screen_rect(emu, c):
    """Absolute rectangle (0x41f599: parent's absolute origin + own rect), when every term is known."""
    if not c.rect or not all(is_num(v) for v in c.rect):
        return None
    x, y = signed(c.rect[0]), signed(c.rect[1])
    p = c.args['parent']
    while is_num(p) and p in emu.objects and p != emu.display:
        pc = emu.objects[p]
        if not pc.rect or not (is_num(pc.rect[0]) and is_num(pc.rect[1])):
            return None
        x += signed(pc.rect[0])
        y += signed(pc.rect[1])
        p = pc.args.get('parent')
    if p != emu.display:
        return None
    w, h = signed(c.rect[2]) - signed(c.rect[0]), signed(c.rect[3]) - signed(c.rect[1])
    return [x, y, x + w, y + h]


def resource_table(image, listing):
    """The run-time globals holding fonts, the background and the text table (start-up 0x40796c)."""
    out = {}
    for glob, store, dir_push, name_push, loader in RESOURCE_GLOBALS:
        st = listing.at(store)
        if st.mnem != 'mov' or st.ops != [f'dword ptr [{glob:#x}]', 'eax']:
            raise GeneratorError(f'{store:#x} does not store into {glob:#x}')
        call = [listing.at(a) for a in range(store - 0x20, store) if a in listing.insns
                and listing.insns[a].mnem == 'call']
        if not call or call[-1].ops != [f'{loader:#x}']:
            raise GeneratorError(f'the load before {store:#x} is not {loader:#x}')
        entry = {'loader': f'{loader:#x}', 'site': f'{call[-1].addr:#x}'}
        for key, site in (('dir', dir_push), ('name', name_push)):
            if site is None:
                continue
            insn = listing.at(site)
            m = MEM.match(insn.ops[0]) if insn.ops else None
            if insn.mnem != 'push':
                raise GeneratorError(f'{site:#x} is not a push')
            ptr = image.u32(int(m.group(3), 16)) if m else int(insn.ops[0], 16)
            entry[key] = image.cstr(ptr)
        out[f'{glob:#x}'] = entry
    return out


def class_table(emu):
    """Per class seen in the table: vtable, object size (RTTI) and the constructors that built it."""
    out = {}
    for c in emu.controls:
        vt = emu.load(c.obj + F_VTABLE, 4)
        name = emu.image.rtti_name(vt)
        entry = out.setdefault(name, {'vtable': f'{vt:#x}', 'size': emu.image.u32(emu.image.u32(vt - 0xc)),
                                      'constructors': []})
        if f'{c.ctor:#x}' not in entry['constructors']:
            entry['constructors'].append(f'{c.ctor:#x}')
    return {k: out[k] for k in sorted(out)}


def generate(exe_path, objdump=None, listing_path=None, write_listing=None, explore=None):
    """The table of the reference VSHELL.EXE at `exe_path` (hash checked before anything else)."""
    data = load_reference(exe_path)
    image = Image(data)
    if listing_path is not None:
        listing = Listing(image, None, Path(listing_path).read_text(encoding='latin-1'))
    else:
        listing = Listing(image, objdump or DEFAULT_OBJDUMP)
    table, emu = layout(image, listing, explore)
    if write_listing is not None:
        Path(write_listing).write_bytes(listing.text().encode('latin-1'))
    return table, emu


def layout(image, listing, explore=None):
    """Runs the screen constructors of the profile over `listing` and returns (table, emulator)."""
    emu = Emulator(image, listing)
    emu.explore = explore
    emu.seed_root()
    for name, entry in SCREENS:
        before = len(emu.objects)
        emu.run(entry, name)
        n = 0
        for obj, c in list(emu.objects.items())[before:]:
            if c.ctor is None:
                continue
            c.id = f'{name}.{n:02d}'
            n += 1
            emu.controls.append(c)
    table = {
        'schema': SCHEMA_VERSION,
        'source': {'file': 'VER95/VSHELL.EXE', 'sha256': EXPECTED_SHA256, 'image_base': '0x400000',
                   'screen_constructors': f'{SCREEN_CALL_SITES[0]:#x}..{SCREEN_CALL_SITES[1]:#x}',
                   'generator': 'tools/reference-game/scripts/vshell_layout.py',
                   'contract': 'docs/reverse-engineering/vshell_widgets.md'},
        'classes': class_table(emu),
        'resources': resource_table(image, listing),
        'assumptions': [{'site': f'{a:#x}', 'taken': BRANCH_ASSUMPTIONS[a][0], 'reason': BRANCH_ASSUMPTIONS[a][1]}
                        for a in sorted(emu.assumptions_used)],
        'screens': build_table(emu),
        'constant_reads': sorted({f'{a:#x}' for a in emu.const_reads}, key=lambda x: int(x, 16)),
        'calls_not_interpreted': [{'screen': s, 'site': f'{site:#x}', 'target': t, 'args': a}
                                  for s, site, t, a in emu.unknown_calls],
    }
    return table, emu


def dumps(table):
    """Deterministic text: one control (or call) per line, so a regenerated table diffs by control."""
    def one(v):
        return json.dumps(v, separators=(', ', ': '), ensure_ascii=True)
    out = ['{']
    keys = list(table)
    for i, key in enumerate(keys):
        tail = ',' if i + 1 < len(keys) else ''
        value = table[key]
        if key == 'screens':
            out.append('  "screens": [')
            for j, screen in enumerate(value):
                out.append('    {"name": %s, "constructor": %s, "controls": ['
                           % (one(screen['name']), one(screen['constructor'])))
                rows = screen['controls']
                for k, row in enumerate(rows):
                    out.append('      ' + one(row) + (',' if k + 1 < len(rows) else ''))
                out.append('    ]}' + (',' if j + 1 < len(value) else ''))
            out.append('  ]' + tail)
        elif isinstance(value, list):
            out.append(f'  {one(key)}: [')
            for k, row in enumerate(value):
                out.append('    ' + one(row) + (',' if k + 1 < len(value) else ''))
            out.append('  ]' + tail)
        else:
            out.append(f'  {one(key)}: {one(value)}{tail}')
    out.append('}')
    return '\n'.join(out) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', required=True, type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--objdump', type=Path, default=DEFAULT_OBJDUMP)
    ap.add_argument('--listing', type=Path)
    ap.add_argument('--write-listing', type=Path)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--list-branches', action='store_true',
                    help='development aid: list every branch on a run-time value (fall-through taken), write nothing')
    args = ap.parse_args(argv)
    explore = [] if args.list_branches else None
    try:
        table, _ = generate(args.exe, args.objdump, args.listing, args.write_listing, explore)
    except GeneratorError as e:
        print(f'vshell_layout: {e}', file=sys.stderr)
        for line in explore or []:
            print(line, file=sys.stderr)
        return 2
    if explore is not None:
        print('\n'.join(explore))
        return 0
    text = dumps(table)
    if args.out is None:
        sys.stdout.write(text)
        return 0
    if args.check:
        old = args.out.read_bytes().decode('utf-8') if args.out.exists() else ''
        if old != text:
            print(f'vshell_layout: {args.out} differs from the regenerated table', file=sys.stderr)
            return 1
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(text.encode('utf-8'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
