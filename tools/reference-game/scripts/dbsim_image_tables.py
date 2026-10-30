"""Generate sim/data/dbsim_image_tables.c: DBSIM's image layout, imports and icon as own tables.

es2sim takes three things from the headers and directories of the user's
DBSIM.EXE (own_process.md section 2.2, dbsim_gap.md "What es2sim takes from
DBSIM.EXE"): the image
layout (image base, image size, header size, entry, every section's RVA,
virtual size, raw offset, raw size and flags, the import, resource and
relocation directories), the 134 imports from which the host writes its
refusal traps, and the class icon "es2icon" (one GROUP_ICON, one ICON). This
script reads them from the file with its own PE reader (the field offsets
are those of the PE/COFF format) and writes them as a C source:

- `es2_sim_dbsim_layout`: an `es2_sim_image_info` initialiser (the executable
  range `code_lo`/`code_hi` derived as `es2_sim_image_parse` derives it);
- `es2_sim_dbsim_imports[]`: per import slot, in the file's order, the DLL
  name and the VA of that string, the symbol name with its hint and the VA of
  the hint/name entry (or the ordinal), and the IAT slot VA;
- `es2_sim_dbsim_resources[]` with their bytes: every ICON and GROUP_ICON
  resource in directory order (type, name or ID, language, VA, size).

The output is committed and is the source of record
(CLAUDE.md section 3, "Data from the executables"); no
build or test runs this script except the fixture test and the real-data
check that regenerates and compares (tests/test_dbsim_image_tables.py).
//sim:image_tables_real_test compares every committed value with the file.

Usage:
  python -B dbsim_image_tables.py --exe <VER95/DBSIM.EXE> [--out sim/data/dbsim_image_tables.c] [--check]
Without --exe the file is looked for below ES2_DATA_ROOT (VER95/DBSIM.EXE or
extracted/cd/VER95/DBSIM.EXE). The SHA-256 must be the reference before
anything else is read. `--check` compares with the existing --out file and
exits 1 on a difference.
"""
import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

EXPECTED_SIZE = 724512
EXPECTED_SHA256 = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
REPO = Path(__file__).resolve().parents[3]
DEFAULT_OUT = REPO / 'sim' / 'data' / 'dbsim_image_tables.c'

RT_ICON, RT_GROUP_ICON = 3, 14
SCN_EXECUTE = 0x20000000


class FormatError(Exception):
    pass


def u16(data, at):
    if at < 0 or at + 2 > len(data):
        raise FormatError('read beyond the file at 0x%x' % at)
    return struct.unpack_from('<H', data, at)[0]


def u32(data, at):
    if at < 0 or at + 4 > len(data):
        raise FormatError('read beyond the file at 0x%x' % at)
    return struct.unpack_from('<I', data, at)[0]


class Image:
    """The headers of a PE32 i386 file and an RVA reader over its sections."""

    def __init__(self, data):
        self.data = data
        if len(data) < 0x40 or data[:2] != b'MZ':
            raise FormatError('no MZ header')
        pe = u32(data, 0x3c)
        if data[pe:pe + 4] != b'PE\0\0':
            raise FormatError('no PE header')
        if u16(data, pe + 4) != 0x14c:
            raise FormatError('not i386')
        count, optional_size = u16(data, pe + 6), u16(data, pe + 20)
        o = pe + 24
        if u16(data, o) != 0x10b:
            raise FormatError('not PE32')
        self.entry_rva = u32(data, o + 16)
        self.image_base = u32(data, o + 28)
        self.image_size = u32(data, o + 56)
        self.headers_size = u32(data, o + 60)
        directories = u32(data, o + 92)
        if directories < 6 or 96 + 8 * directories > optional_size:
            raise FormatError('data directories')
        self.import_dir = (u32(data, o + 104), u32(data, o + 108))
        self.resource_dir = (u32(data, o + 112), u32(data, o + 116))
        self.reloc_dir = (u32(data, o + 136), u32(data, o + 140))
        table = o + optional_size
        self.sections = []
        for i in range(count):
            s = table + 40 * i
            raw_name = data[s:s + 8]
            name = raw_name.split(b'\0', 1)[0]
            if any(c < 0x20 or c > 0x7e or c in b'"\\' for c in name):
                raise FormatError('section name %r' % raw_name)
            self.sections.append({
                'name': name.decode('ascii'),
                'virtual_size': u32(data, s + 8), 'rva': u32(data, s + 12),
                'raw_size': u32(data, s + 16), 'raw_offset': u32(data, s + 20),
                'flags': u32(data, s + 36),
            })
        self.code_lo = self.code_hi = 0
        for x in self.sections:
            if not x['flags'] & SCN_EXECUTE:
                continue
            lo = self.image_base + x['rva']
            hi = lo + max(x['virtual_size'], x['raw_size'])
            if not self.code_hi:
                self.code_lo, self.code_hi = lo, hi
            elif lo == self.code_hi:
                self.code_hi = hi
            elif hi == self.code_lo:
                self.code_lo = lo
            else:
                raise FormatError('executable sections not contiguous')

    def offset(self, rva, count=1):
        """File offset of [rva, rva + count): inside the headers or one section's raw data."""
        if rva + count <= self.headers_size:
            return rva
        for x in self.sections:
            raw = min(x['raw_size'], x['virtual_size']) if x['virtual_size'] else x['raw_size']
            if x['rva'] <= rva and rva + count <= x['rva'] + raw:
                return x['raw_offset'] + rva - x['rva']
        raise FormatError('RVA 0x%x (+%d) has no file bytes' % (rva, count))

    def u16(self, rva):
        return u16(self.data, self.offset(rva, 2))

    def u32(self, rva):
        return u32(self.data, self.offset(rva, 4))

    def bytes(self, rva, count):
        at = self.offset(rva, count) if count else 0
        return bytes(self.data[at:at + count])

    def string(self, rva):
        at = self.offset(rva)
        end = self.data.find(b'\0', at)
        if end < 0:
            raise FormatError('unterminated string at RVA 0x%x' % rva)
        self.offset(rva, end - at + 1)
        text = bytes(self.data[at:end])
        if any(c < 0x20 or c > 0x7e for c in text):
            raise FormatError('non-ASCII name at RVA 0x%x' % rva)
        return text.decode('ascii')


def imports(image):
    """Every import slot in the order of the descriptors and their lookup tables."""
    rva, size = image.import_dir
    out = []
    if not size:
        return out
    d = rva
    while True:
        lookup, name, iat = image.u32(d), image.u32(d + 12), image.u32(d + 16)
        if not name:
            break
        dll = image.string(name)
        if not lookup:
            lookup = iat
        slot = 0
        while True:
            entry = image.u32(lookup + 4 * slot)
            if not entry:
                break
            item = {'dll': dll, 'dll_va': image.image_base + name,
                    'slot_va': image.image_base + iat + 4 * slot}
            if entry & 0x80000000:
                item.update(symbol=None, ordinal=entry & 0xffff, hint=0, name_va=0)
            else:
                item.update(symbol=image.string(entry + 2), ordinal=0, hint=image.u16(entry),
                            name_va=image.image_base + entry)
            out.append(item)
            slot += 1
        d += 20
    return out


def resources(image):
    """Every ICON and GROUP_ICON leaf: type, name (str) or id, language, VA, bytes."""
    rva, size = image.resource_dir
    out = []
    if not size:
        return out

    def entries(at):
        if at + 16 > size:
            raise FormatError('resource directory bounds')
        n = image.u16(rva + at + 12) + image.u16(rva + at + 14)
        if at + 16 + 8 * n > size:
            raise FormatError('resource entries bounds')
        for i in range(n):
            e = rva + at + 16 + 8 * i
            yield image.u32(e), image.u32(e + 4)

    def key(k):
        if not k & 0x80000000:
            return None, k
        s = k & 0x7fffffff
        length = image.u16(rva + s)
        chars = [image.u16(rva + s + 2 + 2 * j) for j in range(length)]
        if any(c < 0x21 or c > 0x7e or c == 0x22 or c == 0x5c for c in chars):
            raise FormatError('resource name is not plain ASCII')
        return ''.join(map(chr, chars)), 0

    for type_key, type_target in entries(0):
        if type_key not in (RT_ICON, RT_GROUP_ICON):
            continue
        if not type_target & 0x80000000:
            raise FormatError('resource type is no directory')
        for name_key, name_target in entries(type_target & 0x7fffffff):
            if not name_target & 0x80000000:
                raise FormatError('resource name is no directory')
            name, ident = key(name_key)
            for lang_key, leaf in entries(name_target & 0x7fffffff):
                if leaf & 0x80000000 or lang_key & 0x80000000:
                    raise FormatError('resource language is a directory or named')
                if leaf + 16 > size:
                    raise FormatError('resource data entry bounds')
                data_rva, data_size = image.u32(rva + leaf), image.u32(rva + leaf + 4)
                out.append({'type': type_key, 'name': name, 'id': ident, 'language': lang_key,
                            'va': image.image_base + data_rva, 'bytes': image.bytes(data_rva, data_size)})
    return out


def c_string(text):
    return '"%s"' % text if text is not None else 'NULL'


def generate(data, source='VER95/DBSIM.EXE'):
    """The C source for the bytes `data` of a PE32 file."""
    image = Image(data)
    sha = hashlib.sha256(data).hexdigest()
    lines = [
        '/* GENERATED by tools/reference-game/scripts/dbsim_image_tables.py (DX-03); do not edit.',
        ' * Source: %s, %d bytes, SHA-256 %s.' % (source, len(data), sha),
        ' * DBSIM\'s image layout, imports and icon resources as own tables',
        ' * (own_process.md "DX-03").',
        ' * //sim:image_tables_real_test compares every value with the file. */',
        '#include "es2/sim/image.h"',
        '',
        '/* The PE headers: optional header and section table. */',
        'const es2_sim_image_info es2_sim_dbsim_layout = {',
        '    .image_base = 0x%08xu, .image_size = 0x%08xu, .headers_size = 0x%08xu, .entry_rva = 0x%08xu,'
        % (image.image_base, image.image_size, image.headers_size, image.entry_rva),
        '    .section_count = %du,' % len(image.sections),
        '    .sections = {',
    ]
    for x in image.sections:
        lines.append('        { "%s", 0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu },  /* VA 0x%08x */'
                     % (x['name'], x['rva'], x['virtual_size'], x['raw_offset'], x['raw_size'], x['flags'],
                        image.image_base + x['rva']))
    lines += [
        '    },',
        '    .import_rva = 0x%08xu, .import_size = 0x%08xu,' % image.import_dir,
        '    .reloc_rva = 0x%08xu, .reloc_size = 0x%08xu,' % image.reloc_dir,
        '    .resource_rva = 0x%08xu, .resource_size = 0x%08xu,' % image.resource_dir,
        '    .code_lo = 0x%08xu, .code_hi = 0x%08xu,' % (image.code_lo, image.code_hi),
        '};',
        '',
        '/* The import slots in the file\'s order: DLL, VA of its name, symbol, hint,',
        ' * VA of the hint/name entry (or the ordinal, symbol NULL), IAT slot VA. */',
        'const es2_sim_own_import es2_sim_dbsim_imports[] = {',
    ]
    items = imports(image)
    for x in items:
        lines.append('    { "%s", 0x%08xu, %s, 0x%04xu, 0x%08xu, %du, 0x%08xu },'
                     % (x['dll'], x['dll_va'], c_string(x['symbol']), x['hint'], x['name_va'], x['ordinal'],
                        x['slot_va']))
    lines += ['};', 'const unsigned es2_sim_dbsim_import_count = %du;' % len(items), '']
    leaves = resources(image)
    for i, x in enumerate(leaves):
        lines.append('/* Resource type %d %s, language %d: %d bytes at VA 0x%08x. */'
                     % (x['type'], c_string(x['name']) if x['name'] else '#%d' % x['id'], x['language'],
                        len(x['bytes']), x['va']))
        lines.append('static const uint8_t resource_%d[%d] = {' % (i, max(len(x['bytes']), 1)))
        b = x['bytes']
        for at in range(0, len(b), 16):
            lines.append('    %s  /* 0x%08x */' % (' '.join('0x%02x,' % c for c in b[at:at + 16]), x['va'] + at))
        lines.append('};')
    lines += [
        '',
        '/* The ICON and GROUP_ICON resources in directory order: type, name (NULL:',
        ' * by ID), ID, language, VA, size, bytes. */',
        'const es2_sim_own_resource es2_sim_dbsim_resources[] = {',
    ]
    for i, x in enumerate(leaves):
        lines.append('    { %du, %s, %du, %du, 0x%08xu, %du, resource_%d },'
                     % (x['type'], c_string(x['name']), x['id'], x['language'], x['va'], len(x['bytes']), i))
    if not leaves:
        lines.append('    { 0u, NULL, 0u, 0u, 0u, 0u, NULL },')
    lines += ['};', 'const unsigned es2_sim_dbsim_resource_count = %du;' % len(leaves), '']
    return '\n'.join(lines)


def find_exe():
    root = os.environ.get('ES2_DATA_ROOT')
    if not root:
        return None
    for rel in ('VER95/DBSIM.EXE', 'extracted/cd/VER95/DBSIM.EXE'):
        p = Path(root) / rel
        if p.exists():
            return p
    return None


def read_reference(path):
    data = Path(path).read_bytes()
    if len(data) != EXPECTED_SIZE or hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise FormatError('%s is not the reference DBSIM.EXE (size or SHA-256)' % path)
    return data


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', type=Path)
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args(argv)
    exe = args.exe or find_exe()
    if exe is None:
        print('dbsim_image_tables: no --exe and no DBSIM.EXE below ES2_DATA_ROOT', file=sys.stderr)
        return 2
    try:
        text = generate(read_reference(exe))
    except FormatError as e:
        print('dbsim_image_tables: %s' % e, file=sys.stderr)
        return 1
    out = args.out.resolve()
    if args.check:
        same = out.exists() and out.read_bytes() == text.encode('ascii')
        print('dbsim_image_tables: %s %s' % (out, 'is current' if same else 'DIFFERS'))
        return 0 if same else 1
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(text.encode('ascii'))
    print('dbsim_image_tables: wrote %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
