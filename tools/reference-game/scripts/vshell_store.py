"""The VSHELL span store shell/data/vshell_store_data.c (EXE data compiled in).

Reads the reference VER95/VSHELL.EXE once and writes, as a C source, the
byte runs es2shell and es2menu take from it (es2_shell_exe_span and the
layout literal reader, es2/shell/vshell_store.h) and VSHELL's class icon
"ES2ICON" (its RT_GROUP_ICON and RT_ICON resources). A run is the union of

  * the static spans of the readers (SPANS below, from the source of
    shell/src/engine.c, practice.c, host.c and gui/app.c),
  * the targets of the two pointer tables (POINTERS: the slot holds a VA;
    the reader takes a window of up to `window` bytes there),
  * the literals the layout table names (ES2_UI_REF_EXE_STRING: address and
    length from shell/data/vshell_layout.json, plus the terminating NUL),
  * the reads the measurement switch saw (shell/data/vshell_reads.txt, one
    "0x<va> <size> <reader>" per line),

merged where they overlap or touch. Each run keeps its VA and names the
readers it serves. The output is committed and is the source of record; no
build or test runs this script (vshell_presentation.md "the VSHELL span store").

Usage:
  vshell_store.py [--exe VSHELL.EXE] [--layout JSON] [--reads TXT] [--out C] [--check]
--exe defaults to $ES2_DATA_ROOT/VER95/VSHELL.EXE; --check compares with the
existing output instead of writing it (exit 1 on a difference).
"""
import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
REFERENCE_SHA256 = '1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e'

# (reader, VA, size): the static spans of the shell's readers.
SPANS = [
    ('engine.prepare', 0x47f850, 112),       # engine.c prepare[0]; app.c map RNG table; host.c defaults
    ('engine.prepare', 0x46f72c, 16),
    ('engine.prepare', 0x46f868, 36),
    ('engine.results', 0x46f9e4, 48),
    ('engine.results', 0x46fec4, 12),
    ('engine.results', 0x46feb8, 12),
    ('engine.results', 0x46fb80, 66),
    ('engine.results', 0x46fbc2, 66),
    ('engine.results', 0x46fa20, 58),
    ('engine.results', 0x46f9a8, 60),        # also host.c prefs items
    ('engine.weapon_rows', 0x476a4c, 54),    # 27 words; the editors read 52 of them
    ('practice.herc_types', 0x479c56, 24),
    ('practice.names', 0x479f70, 8),
    ('practice.names', 0x47396b, 8),
    ('gui.map_cosine', 0x47b480, 0x401 * 2),
    ('gui.map_atan', 0x47aaf4, 0x201),
    ('gui.internal_from', 0x46ffc0, 18),
    ('gui.hot_bytes', 0x46e5a9, 0xed4),
    ('gui.band_limit', 0x47665c, 10),
    ('gui.band_colour', 0x476666, 12),
    ('gui.editor_tables', 0x46f87a, 18),
    ('gui.weapons_blank', 0x4774c1, 3),     # weapons.c: literal 0x4774c1 for item ID 0
]
# (reader, slot VA, slot count, window): pointer tables (slots of kind VA).
POINTERS = [
    ('engine.preset_paths', 0x47083c, 9, 64),
    ('engine.arm_names', 0x47006c, 9, 32),
]


class Image:
    """The sections of a PE image held in memory (raw data only)."""

    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not a PE image')
        count, = struct.unpack_from('<H', data, pe + 6)
        optional, = struct.unpack_from('<H', data, pe + 20)
        self.base, = struct.unpack_from('<I', data, pe + 24 + 28)
        self.rsrc_rva, _ = struct.unpack_from('<II', data, pe + 24 + 96 + 8 * 2)
        self.sections = []
        for i in range(count):
            h = pe + 24 + optional + 40 * i
            rva, raw_size, raw = struct.unpack_from('<III', data, h + 12)
            self.sections.append((rva, raw_size, raw))

    def end(self, va):
        """The VA after the raw data of the section holding va."""
        rva = va - self.base
        for address, raw_size, _ in self.sections:
            if address <= rva < address + raw_size:
                return self.base + address + raw_size
        raise ValueError(f'VA {va:#x} lies in no section')

    def read(self, va, size):
        rva = va - self.base
        for address, raw_size, raw in self.sections:
            if address <= rva and rva + size <= address + raw_size:
                return self.data[raw + rva - address:raw + rva - address + size]
        raise ValueError(f'span {va:#x}+{size} lies in no section')

    def rva_bytes(self, rva, size):
        return self.read(self.base + rva, size)


def resources(image):
    """{(type, name or id): [(id or name, data RVA, size)]} of the three-level .rsrc tree."""
    if not image.rsrc_rva:
        return {}
    root = image.rsrc_rva

    def name_of(entry):
        if entry & 0x80000000:
            at = root + (entry & 0x7fffffff)
            n, = struct.unpack('<H', image.rva_bytes(at, 2))
            return image.rva_bytes(at + 2, 2 * n).decode('utf-16-le')
        return entry

    def entries(offset):
        named, ids = struct.unpack('<HH', image.rva_bytes(root + offset + 12, 4))
        for i in range(named + ids):
            name, target = struct.unpack('<II', image.rva_bytes(root + offset + 16 + 8 * i, 8))
            yield name_of(name), target

    out = {}
    for rtype, t in entries(0):
        for rname, n in entries(t & 0x7fffffff):
            for _, leaf in entries(n & 0x7fffffff):
                rva, size = struct.unpack('<II', image.rva_bytes(root + leaf, 8))
                out[(rtype, rname)] = (rva, size)
    return out


def icon(image, name='ES2ICON'):
    """(group VA, group bytes, [(id, VA, bytes)]) of the named group icon."""
    res = resources(image)
    rva, size = res[(14, name)]
    group = image.rva_bytes(rva, size)
    _, kind, count = struct.unpack_from('<HHH', group, 0)
    if kind != 1 or len(group) != 6 + 14 * count:
        raise ValueError('not an icon group')
    images = []
    for i in range(count):
        ident, = struct.unpack_from('<H', group, 6 + 14 * i + 12)
        irva, isize = res[(3, ident)]
        images.append((ident, image.base + irva, image.rva_bytes(irva, isize)))
    return image.base + rva, group, images


def literals(layout):
    """(VA, length + 1) of every ES2_UI_REF_EXE_STRING of the layout table."""
    found = {}

    def walk(node):
        if isinstance(node, dict):
            if 'exe_string' in node:
                found[int(node['exe_string'], 16)] = int(node['length']) + 1
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(layout)
    return sorted(found.items())


def read_list(text):
    """[(VA, size, reader)] of a reads file ('#' comments)."""
    out = []
    for line in text.splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            va, size, reader = line.split()
            out.append((int(va, 16), int(size), reader))
    return out


def intervals(image, layout, reads):
    """[(VA, size, reader)] of every span the store must hold."""
    out = [(va, size, reader) for reader, va, size in SPANS]
    for reader, slots, count, window in POINTERS:
        out.append((slots, 4 * count, reader + '.pointers'))
        for i in range(count):
            target, = struct.unpack('<I', image.read(slots + 4 * i, 4))
            size = min(window, image.end(target) - target)
            if 0 not in image.read(target, size):
                raise ValueError(f'{reader}[{i}] at {target:#x}: no NUL within {size} bytes')
            out.append((target, size, reader))
    out += [(va, size, 'literal') for va, size in literals(layout)]
    out += [(va, size, 'measured') for va, size, _ in reads]
    for va, size, _ in out:
        image.read(va, size)   # every span lies in the raw data of one section
    return out


def merge(spans):
    """Runs [(VA, size, readers)] of the union; touching spans join."""
    runs = []
    for va, size, reader in sorted(spans):
        if runs and va <= runs[-1][0] + runs[-1][1]:
            start, length, readers = runs[-1]
            runs[-1] = (start, max(length, va + size - start), readers | {reader})
        else:
            runs.append((va, size, {reader}))
    out = []
    for va, size, readers in runs:
        named = sorted(r for r in readers if r != 'measured')
        out.append((va, size, ', '.join(named) if named else 'measured'))
    return out


def c_bytes(name, data):
    lines = [f'static const uint8_t {name}[{len(data)}] = {{']
    for i in range(0, len(data), 16):
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + 16]) + ',')
    lines.append('};')
    return lines


def render(image_bytes, layout, reads):
    image = Image(image_bytes)
    digest = hashlib.sha256(image_bytes).hexdigest()
    runs = merge(intervals(image, layout, reads))
    group_va, group, images = icon(image)
    out = [
        '/* Generated by tools/reference-game/scripts/vshell_store.py from VER95/VSHELL.EXE',
        f' * (SHA-256 {digest}); do not edit.',
        ' * The bytes es2shell and es2menu read from VSHELL.EXE, as runs at their',
        ' * original VAs (image base 0x00400000), and the class icon ES2ICON.',
        ' * Checked against the file by //shell:vshell_store_real_test. */',
        '#include "es2/shell/vshell_store.h"',
        '',
        f'const char es2_vshell_store_source[] = "VER95/VSHELL.EXE sha256 {digest}";',
        '',
    ]
    total = 0
    for va, size, _ in runs:
        out += c_bytes(f'run_{va:06x}', image.read(va, size))
        total += size
    out.append('')
    out.append(f'/* {len(runs)} runs, {total} bytes */')
    out.append('const es2_vshell_run es2_vshell_runs[] = {')
    for va, size, readers in runs:
        out.append(f'    {{0x{va:06x}u, {size}u, run_{va:06x}, "{readers}"}},')
    out.append('};')
    out.append(f'const size_t es2_vshell_run_count = {len(runs)};')
    out.append('')
    out.append(f'const uint32_t es2_vshell_icon_group_va = 0x{group_va:06x}u;')
    out.append(f'const uint32_t es2_vshell_icon_group_size = {len(group)}u;')
    out += [line.replace('static const uint8_t icon_group', 'const uint8_t es2_vshell_icon_group')
            for line in c_bytes('icon_group', group)]
    for ident, va, data in images:
        out += c_bytes(f'icon_{ident}', data)
    out.append('const es2_vshell_icon_image es2_vshell_icon_images[] = {')
    for ident, va, data in images:
        out.append(f'    {{{ident}, 0x{va:06x}u, {len(data)}u, icon_{ident}}},')
    out.append('};')
    out.append(f'const size_t es2_vshell_icon_image_count = {len(images)};')
    return '\n'.join(out) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    root = os.environ.get('ES2_DATA_ROOT', '')
    p.add_argument('--exe', default=str(Path(root) / 'VER95' / 'VSHELL.EXE') if root else None)
    p.add_argument('--layout', default=str(REPO / 'shell' / 'data' / 'vshell_layout.json'))
    p.add_argument('--reads', default=str(REPO / 'shell' / 'data' / 'vshell_reads.txt'))
    p.add_argument('--out', default=str(REPO / 'shell' / 'data' / 'vshell_store_data.c'))
    p.add_argument('--check', action='store_true')
    p.add_argument('--any-hash', action='store_true', help='accept an image other than the reference (fixtures)')
    a = p.parse_args(argv)
    if not a.exe:
        p.error('--exe or ES2_DATA_ROOT is needed')
    data = Path(a.exe).read_bytes()
    if not a.any_hash and hashlib.sha256(data).hexdigest() != REFERENCE_SHA256:
        print(f'{a.exe} is not the reference VSHELL.EXE', file=sys.stderr)
        return 2
    layout = json.loads(Path(a.layout).read_text(encoding='utf-8'))
    reads = read_list(Path(a.reads).read_text(encoding='utf-8')) if Path(a.reads).exists() else []
    text = render(data, layout, reads).encode('ascii')
    out = Path(a.out)
    if a.check:
        if not out.exists() or out.read_bytes() != text:
            print(f'{out} differs from the generator output', file=sys.stderr)
            return 1
        print(f'{out}: up to date')
        return 0
    out.write_bytes(text)
    print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
