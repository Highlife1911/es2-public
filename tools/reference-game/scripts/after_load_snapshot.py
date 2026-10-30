#!/usr/bin/env python3
"""After-load snapshot (docs/reverse-engineering/mission_load.md, sections 13
and "the snapshot noise model").

The proxy writes the snapshot once, at the first entry of the logic frame
0x45f570, when es2-oracle.ini has `[snapshot] after_load=1` and `path=...`
(proxy/early_arming.h). It holds the fixed regions of mission_load.md
section 9, every block of the runtime's heap and (version 2) the process
memory map with the loaded modules. The heap is CW3220.DLL's or, with the own
cw3220.dll deployed (flag 128 `own_runtime_heap`), the own runtime's:
both keep CW3220's block layout, so the two compare like two runs of either.
The own cw3220.dll makes no copy of the environment, so the `environment` class
finds no array in its heap; es2sim's heap has the copy (own_process.md
"the heap trace").

  python -B after_load_snapshot.py summary SNAPSHOT
  python -B after_load_snapshot.py compare A B [--scope all|pools] [--noise FILE]
        [--ignore VA:LEN ...] [--pairing graph|index] [--unreached skip|align]
        [--json OUT] [--max-differences N] [--allow-incomplete] [--no-classes]
  python -B after_load_snapshot.py noise A1 A2 --out FILE [--scope all|pools]
        [--allow-incomplete] [--no-classes]

The noise model. Two original runs differ in three ways that are not
simulation state:

1. Heap layout outside the simulation. CW3220 copies the process environment
   into heap blocks at start-up (the `_environ` array is the walk's first
   block, one block per string); the runner's environment differs between
   runs, so blocks are inserted or resized and every later walk index moves.
   Pairing by walk index turns one extra environment string into thousands
   of differences. Here blocks are paired by reference instead: the
   fixed regions are identical addresses in both runs; a 4-byte window that
   points into a heap block in both snapshots, at the same offset, pairs the
   two blocks, and the walk continues breadth-first from every paired block.
   Only what the fixed regions reach is compared (the simulation graph);
   unreached blocks (environment, CW3220 stream buffers, freed blocks) are
   counted, and `--unreached align` compares them by an alignment of the
   walk's size sequence for information.
2. Pointers. A heap pointer is equal when it points into paired blocks at
   the same offset; an image pointer when its image offset is equal; with a
   version-2 snapshot a pointer into a loaded module when module and offset
   are equal, and a pointer into other committed memory (system heaps,
   VirtualAlloc'd buffers, mapped views; their addresses vary between runs)
   when both point into memory of the same type ("foreign").
3. Fields that vary between original runs for other reasons (clock values,
   handles, stale bytes). `noise A1 A2` (two original runs) writes every
   remaining difference as a noise entry (block path and offset); `compare
   --noise FILE` then counts a difference covered by an entry as noise.
4. Classes (on unless `--no-classes`), for what a noise file cannot
   learn because two original runs agree on it:
   - `environment`: CW3220's `_environ` array (the first block among the
     walk's first 256 in-use blocks that holds at least 8 pointers to the
     starts of in-use blocks with `NAME=VALUE` strings, then 0) and those
     string blocks are process input (the runner's per-run
     ES2_CLONE_LOCK_TOKEN, 32 hex digits). The walk never follows a window
     into them; such links are counted. When both heaps lie at the same
     addresses, equal bytes that happen to look like a pointer into the
     array reach them otherwise (c3_09-loadout-b).
   - `matrix_padding`: byte 19 of each 32-byte node matrix of an animation
     manager (constructor 0x478838: vtable 0x4a14c4 at +0, node count word
     +0x0c, node matrices `new[](32 n)` at +0x16). The original leaves
     pose_parent's stack residue there (the copy path of matrix_compose);
     inside node_matrices 0x478c64 that is the high byte of the last root
     node's matrix pointer (pushed at 0x478c54), so it follows the heap's
     address range (02/03 in CW3220's heap, 20 in es2sim's). The replaced
     functions reproduce it from their padding argument and the pointer.
     Differences of these bytes are counted, not reported.
5. Classes of heaps of different runtimes (on with the item 4
   classes, active only when the two snapshots' `heap_runtime` differ: the
   own runtime's heap, es2sim's or the own cw3220.dll's, against CW3220's).
   Both runtimes keep CW3220's block layout, but their allocation histories
   differ (CW3220 copies the environment and opens its stream buffers in its
   heap at start-up; the own cw3220.dll does not, es2sim does from
   its own environment), so three things differ that
   are not simulation state:
   - `allocation_slack`: a block of n requested bytes has round4(n) data
     bytes (n < 8: 8), and calloc or new zero or fill only n of them; the
     last 1-3 data bytes keep whatever the memory held before (up to 7
     below 8 requested bytes; not modelled, such runs are reported). A
     differing run that lies entirely within the last 3 data bytes of the
     shorter block is counted, not reported.
   - `block_fit`: next-fit hands out the whole free block when the
     remainder would be below the 16-byte minimum block, so a block can
     exceed its request by 4-12 bytes depending on the free list. A
     block-size difference below 16 bytes is counted, not reported (the
     common bytes are still compared).
   - `byte_count`: the dword 0x6bb30c sums the header words of the live
     blocks that 0x47747c allocated (nothing branches on it, rtl_imports.md
     section 4.3), so it follows the block sizes. Its difference is
     counted; the sum of the paired blocks' size differences is given for
     information (`block_size_delta`, B minus A).
6. Classes of stack residue and freed memory (mission_load.md section 14
   "Noise classes"):
   - `aim_residue` (with the item 4 classes): the cockpit panel H = [[0x4d24c0]
     + 0x1f] (the getter 0x42982e) holds at +0x26c the aim target and at
     +0x270..+0x27f what 0x434a24 copied from cockpit_sync's (0x41b130)
     frame locals (12 bytes, two words). cockpit_sync stores them on every
     call, also without a target, and they are read only when the target
     dword is set (the HUD 0x43d7e7; the MFD method 0x43a5a0 on its target
     path); with target 0 they are stack residue of the original's load
     and render frames. When the paired H blocks both hold target 0, a
     differing run in +0x270..+0x27f is counted, not reported.
   - `freed_target` (with the item 5 classes): a pointer whose two values
     both point into free blocks (a table entry left after delete, such as
     the unused map icons of 0x44df00, deleted from [M+0x10a] to 0x8c) is a
     dangling pointer; where the freed memory lies depends on the heap's
     history. Such a window is counted, not reported.

A block path is the chain of pointers by which the breadth-first walk first
reached the block in snapshot A: `0x004d250a/+0x6` is the block that the
pointer at +6 of the block pointed to by the fixed dword 0x4d250a. Fixed
bytes are named by their VA.

Exit status (compare): 0 identical, 1 different, 2 unusable input or usage.
`noise` exits 0 when it wrote the file. Standard library only.
"""
import argparse
import bisect
import collections
import difflib
import json
import struct
import sys
from array import array

MAGIC = b'ES2SNAP1'
FLAG_HEAP, FLAG_NO_HEAP, FLAG_HEAP_ERROR, FLAG_TRUNCATED, FLAG_COMPLETE, FLAG_EARLY, FLAG_MAP_TRUNCATED = (
    1, 2, 4, 8, 16, 32, 64)
FLAG_OWN_RUNTIME = 128   # the heap is the own cw3220.dll's
FLAG_NAMES = ((FLAG_HEAP, 'heap'), (FLAG_NO_HEAP, 'no_heap'), (FLAG_HEAP_ERROR, 'heap_error'),
              (FLAG_TRUNCATED, 'truncated'), (FLAG_COMPLETE, 'complete'), (FLAG_EARLY, 'armed_early'),
              (FLAG_MAP_TRUNCATED, 'map_truncated'), (FLAG_OWN_RUNTIME, 'own_runtime_heap'))
MEM_IMAGE, MEM_MAPPED, MEM_PRIVATE = 0x1000000, 0x40000, 0x20000
NOISE_SCHEMA = 1

# The pool header pointers and removal lists of mission_load.md sections 2
# and 4 and the actor list 0x4a9af8 (array pointer, capacity and count words,
# 0x411df8): the fixed bytes that scope "pools" compares and walks from.
POOL_ROOTS = (
    (0x4a9b7a, 4, 'MECH pool'), (0x4a9b7e, 4, 'MECH removal list'),
    (0x4a9db9, 4, 'FLYER pool'), (0x4a9dbd, 4, 'FLYER removal list'),
    (0x4a95a0, 4, 'building pool'),
    (0x4a95fe, 4, 'light pool'), (0x4a9602, 4, 'light removal list'),
    (0x4a961e, 4, 'explosion pool'), (0x4a9622, 4, 'explosion removal list'),
    (0x4a9642, 4, 'debris pool'), (0x4a9646, 4, 'debris removal list'),
    (0x4a966e, 4, 'smoke ball pool'),
    (0x4a9684, 4, 'smoke pool'), (0x4a9688, 4, 'smoke removal list'),
    (0x4a968d, 4, 'flat-object pool'), (0x4a9691, 4, 'flat-object removal list'),
    (0x4a96aa, 4, 'meteor pool'), (0x4a96ae, 4, 'meteor removal list'),
    (0x4a96c2, 4, 'projectile pool'), (0x4a96c6, 4, 'projectile removal list'),
    (0x6b4f30, 4, 'fire pool'), (0x6b4f34, 4, 'fire removal list'),
    (0x4a9af8, 8, 'actor list'),
)

# Classes of item 4 of the module docstring.
REFERENCE_IMAGE_BASE = 0x400000
MANAGER_VTABLE = 0x4a14c4              # 0x478838 stores it at +0
MANAGER_NODES, MANAGER_MATRICES = 0x0c, 0x16
MATRIX_SIZE, MATRIX_PADDING = 32, 19
# Classes of item 5 of the module docstring.
SLACK_BYTES = 3                        # round4(n) - n is 0..3
MIN_BLOCK = 16                         # the whole free block below this remainder
BYTE_COUNT = 0x6bb30c                  # sum of the live header words of 0x47747c
ENVIRONMENT_SEARCH, ENVIRONMENT_MINIMUM = 256, 8
# Classes of item 6 of the module docstring.
AIM_ROOT, AIM_PANEL = 0x4d24c0, 0x1f   # H = [[0x4d24c0] + 0x1f] (0x42982e)
AIM_TARGET, AIM_FIELDS = 0x26c, (0x270, 0x280)   # 0x434a24's stores


class SnapshotError(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def u32(self):
        if self.at + 4 > len(self.data):
            raise SnapshotError('truncated file at offset %d' % self.at)
        value = struct.unpack_from('<I', self.data, self.at)[0]
        self.at += 4
        return value

    def take(self, n):
        if self.at + n > len(self.data):
            raise SnapshotError('truncated file at offset %d (%d bytes wanted)' % (self.at, n))
        view = self.data[self.at:self.at + n]
        self.at += n
        return view


def _u32_array(data, phase):
    """The little-endian dwords of data[phase:] at 4-byte steps."""
    m = (len(data) - phase) // 4
    values = array('I')
    if values.itemsize != 4:   # pragma: no cover (CPython on Windows and Linux: 4)
        values = array('L')
    if m > 0:
        values.frombytes(bytes(data[phase:phase + 4 * m]))
        if sys.byteorder == 'big':   # pragma: no cover
            values.byteswap()
    return values


class Snapshot:
    """Parsed snapshot: fixed regions {(va, length): bytes or None}, heap
    regions [(base, size, ok)], blocks [(address, size, in_use, data or None,
    region)] in walk order, and (version 2) the committed memory regions
    [(base, size, allocation_base, protect, type)] and modules
    {base: (size, name)}."""

    def __init__(self, data, name='snapshot'):
        self.name = name
        r = Reader(memoryview(data))
        if bytes(r.take(8)) != MAGIC:
            raise SnapshotError('%s: not an ES2SNAP1 file' % name)
        self.version = r.u32()
        if self.version not in (1, 2):
            raise SnapshotError('%s: version %d' % (name, self.version))
        self.flags, self.frame, self.image_base, self.image_size = r.u32(), r.u32(), r.u32(), r.u32()
        self.fixed = {}
        for _ in range(r.u32()):
            va, length, readable = r.u32(), r.u32(), r.u32()
            self.fixed[(va, length)] = bytes(r.take(length)) if readable else None
        self.regions, self.blocks = [], []
        for index in range(r.u32()):
            base, size, ok, count = r.u32(), r.u32(), r.u32(), r.u32()
            self.regions.append((base, size, bool(ok)))
            for _ in range(count):
                address, block_size, flags = r.u32(), r.u32(), r.u32()
                if block_size < 16:
                    raise SnapshotError('%s: block 0x%08x of size %d' % (name, address, block_size))
                payload = bytes(r.take(block_size - 8)) if flags & 2 else None
                self.blocks.append((address, block_size, bool(flags & 1), payload, index))
        self.memory, self.modules = [], {}
        if self.version >= 2:
            for _ in range(r.u32()):
                self.memory.append((r.u32(), r.u32(), r.u32(), r.u32(), r.u32()))
            for _ in range(r.u32()):
                base, size, length = r.u32(), r.u32(), r.u32()
                if length > 1024:
                    raise SnapshotError('%s: module name of %d bytes' % (name, length))
                self.modules[base] = (size, bytes(r.take(length)).decode('utf-8', 'replace').lower())
            self.memory.sort()
        if r.at != len(r.data):
            raise SnapshotError('%s: %d trailing bytes' % (name, len(r.data) - r.at))
        # Pointer lookup over every block (a stale pointer into a free block
        # is still canonical); the end address of a block's data is included.
        order = sorted(range(len(self.blocks)), key=lambda i: self.blocks[i][0])
        self._starts = [self.blocks[i][0] for i in order]
        self._order = order
        self._memory_starts = [m[0] for m in self.memory]
        spans = [(b, b + s) for b, s, _ in self.regions] or [(0, 0)]
        self.heap_lo, self.heap_hi = min(s[0] for s in spans), max(s[1] for s in spans)
        self._environment = None

    def environment(self):
        """(index of the `_environ` array block or None, frozenset of the
        array and its string blocks); see the `environment` class."""
        if self._environment is None:
            self._environment = (None, frozenset())
            seen = 0
            for index, (_, _, in_use, data, _) in enumerate(self.blocks):
                if not in_use or data is None:
                    continue
                seen += 1
                if seen > ENVIRONMENT_SEARCH:
                    break
                strings = self._environment_strings(index, data)
                if strings is not None:
                    self._environment = (index, frozenset(strings) | {index})
                    break
        return self._environment

    def _environment_strings(self, index, data):
        """The string block indices when data is a 0-terminated array of at
        least ENVIRONMENT_MINIMUM pointers to the starts of distinct in-use
        blocks holding NUL-terminated strings with '=', else None."""
        strings = []
        for k in range(0, len(data) - 3, 4):
            value = struct.unpack_from('<I', data, k)[0]
            if value == 0:
                return strings if len(strings) >= ENVIRONMENT_MINIMUM else None
            hit = self.block_at(value)
            if hit is None or hit[1] != 0 or hit[0] == index or hit[0] in strings:
                return None
            _, _, in_use, text, _ = self.blocks[hit[0]]
            if not in_use or text is None:
                return None
            end = text.find(b'\0')
            if end <= 0 or b'=' not in text[:end] or any(c < 0x20 for c in text[:end]):
                return None
            strings.append(hit[0])
        return None

    @property
    def complete(self):
        return bool(self.flags & FLAG_COMPLETE)

    @property
    def heap_runtime(self):
        """'own' (the own cw3220.dll's heap), 'cw3220' or 'none'."""
        if not self.flags & FLAG_HEAP:
            return 'none'
        return 'own' if self.flags & FLAG_OWN_RUNTIME else 'cw3220'

    def flag_names(self):
        return [n for bit, n in FLAG_NAMES if self.flags & bit]

    def block_at(self, value):
        """(block index, offset) of a heap address, else None."""
        if not self.heap_lo <= value < self.heap_hi:
            return None
        k = bisect.bisect_right(self._starts, value) - 1
        if k >= 0:
            index = self._order[k]
            address, size = self.blocks[index][0], self.blocks[index][1]
            if value <= address + size - 8:
                return index, value - address
        return None

    def classify(self, value):
        """Canonical class of a 4-byte value that is a pointer, else None:
        ('image', offset), ('block', index, offset), ('heap_meta', region,
        offset), ('module', name, offset) or ('foreign', type)."""
        if self.image_base <= value < self.image_base + self.image_size:
            return ('image', value - self.image_base)
        hit = self.block_at(value)
        if hit is not None:
            return ('block',) + hit
        if self.heap_lo <= value < self.heap_hi:
            for index, (base, size, _) in enumerate(self.regions):
                if base <= value < base + size:
                    return ('heap_meta', index, value - base)
        if self.memory:
            k = bisect.bisect_right(self._memory_starts, value) - 1
            if k >= 0:
                base, size, allocation, _, kind = self.memory[k]
                if value < base + size:
                    if kind == MEM_IMAGE and allocation in self.modules:
                        return ('module', self.modules[allocation][1], value - allocation)
                    return ('foreign', kind)
        return None

    def block_windows(self, data):
        """[(position, block index, offset)] for every 4-byte window of data
        (aligned or not) that points into a heap block."""
        out = []
        lo, hi = self.heap_lo, self.heap_hi
        for phase in range(4):
            values = _u32_array(data, phase)
            for k, v in enumerate(values):
                if lo <= v < hi:
                    hit = self.block_at(v)
                    if hit is not None:
                        out.append((phase + 4 * k, hit[0], hit[1]))
        out.sort()
        return out


def read(path):
    with open(path, 'rb') as f:
        return Snapshot(f.read(), str(path))


def parse_range(text):
    try:
        va, length = text.split(':')
        va, length = int(va, 16), int(length, 0)
    except ValueError:
        raise argparse.ArgumentTypeError('expected VA:LEN, for example 0x4d24c0:4')
    if length <= 0:
        raise argparse.ArgumentTypeError('length must be positive')
    return va, length


def _differing(a, b):
    """Offsets where a and b differ (over the common length), chunk by chunk."""
    step = 4096
    n = min(len(a), len(b))
    for start in range(0, n, step):
        if a[start:start + step] == b[start:start + step]:
            continue
        for k in range(start, min(start + step, n)):
            if a[k] != b[k]:
                yield k


def _runs(offsets):
    runs = []
    for k in offsets:
        if runs and k == runs[-1][1] + 1:
            runs[-1][1] = k
        else:
            runs.append([k, k])
    return runs


def _hex(va):
    return '0x%08x' % va


class Pairing:
    """Blocks of A paired with blocks of B, with the path by which the walk
    first reached each pair."""

    def __init__(self):
        self.ab, self.ba, self.path, self.conflicts = {}, {}, {}, []

    def link(self, i, j, path):
        if i in self.ab or j in self.ba:
            if self.ab.get(i) != j:
                self.conflicts.append({'path': path, 'a_block': i, 'b_block': j,
                                       'a_paired_with': self.ab.get(i), 'b_paired_with': self.ba.get(j)})
            return False
        self.ab[i], self.ba[j], self.path[i] = j, i, path
        return True


def _canonical(s, value, pairing, side):
    c = s.classify(value)
    if c is None or c[0] != 'block':
        return c
    if side == 'a':
        return ('pair', c[1], c[2]) if c[1] in pairing.ab else ('unpaired_a', c[1], c[2])
    i = pairing.ba.get(c[1])
    return ('pair', i, c[2]) if i is not None else ('unpaired_b', c[1], c[2])


def _freed(s, value):
    hit = s.block_at(value)
    return hit is not None and not s.blocks[hit[0]][2]


def _compare_data(a, b, sa, sb, pairing, stats, ignored=None, freed=None):
    """Differing byte runs of a and b (common length) after pointer
    canonicalization; counts canonicalized pointers in stats. With a list
    `freed`, a window whose two values both point into free blocks is
    covered and its position appended (class freed_target)."""
    covered, problems = set(), []
    n = min(len(a), len(b))
    for k in _differing(a, b):
        if k in covered or (ignored and k in ignored):
            continue
        for w in range(max(0, k - 3), min(k, n - 4) + 1):
            va_, vb_ = struct.unpack_from('<I', a, w)[0], struct.unpack_from('<I', b, w)[0]
            ca = _canonical(sa, va_, pairing, 'a')
            if ca is None:
                continue
            if ca[0] != 'unpaired_a':
                cb = _canonical(sb, vb_, pairing, 'b')
                if ca == cb:
                    covered.update(range(w, w + 4))
                    stats['foreign_pointers' if ca[0] == 'foreign' else 'pointers_canonicalized'] += 1
                    break
            if freed is not None and _freed(sa, va_) and _freed(sb, vb_):
                covered.update(range(w, w + 4))
                freed.append(w)
                break
        else:
            problems.append(k)
    return problems


class NoiseModel:
    """Noise entries {(path, offset): length} from a `noise` file."""

    def __init__(self, entries=()):
        self.cover = {}
        for e in entries:
            for k in range(e['offset'], e['offset'] + e['length']):
                self.cover.setdefault(e['path'], set()).add(k)

    @staticmethod
    def load(path):
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
        if data.get('schema') != NOISE_SCHEMA:
            raise SnapshotError('%s: noise schema %r' % (path, data.get('schema')))
        return NoiseModel(data.get('entries', []))

    def covers(self, path, lo, hi):
        cover = self.cover.get(path)
        return bool(cover) and all(k in cover for k in range(lo, hi + 1))


def _roots(sa, sb, scope):
    """[(key, va, length)] of the fixed bytes compared and walked from."""
    keys = sorted(set(sa.fixed) & set(sb.fixed))
    if scope == 'all':
        return [(key, key[0], key[1]) for key in keys]
    roots = []
    for va, length, _ in POOL_ROOTS:
        for key in keys:
            if key[0] <= va and va + length <= key[0] + key[1]:
                roots.append((key, va, length))
                break
    return roots


def _matrix_padding(sa, sb, pairing):
    """{A block index: offsets} of the padding bytes (byte 19 of every node
    matrix) of the node-matrix blocks of paired animation managers whose
    vtable, node count and paired matrix block agree in both snapshots."""
    masks = {}
    vtables = (sa.image_base + MANAGER_VTABLE - REFERENCE_IMAGE_BASE,
               sb.image_base + MANAGER_VTABLE - REFERENCE_IMAGE_BASE)
    for i, j in pairing.ab.items():
        da, db = sa.blocks[i][3], sb.blocks[j][3]
        if da is None or db is None or len(da) < MANAGER_MATRICES + 4 or len(db) < MANAGER_MATRICES + 4:
            continue
        if (struct.unpack_from('<I', da, 0)[0], struct.unpack_from('<I', db, 0)[0]) != vtables:
            continue
        n = struct.unpack_from('<h', da, MANAGER_NODES)[0]
        if n <= 0 or n != struct.unpack_from('<h', db, MANAGER_NODES)[0]:
            continue
        ha = sa.block_at(struct.unpack_from('<I', da, MANAGER_MATRICES)[0])
        hb = sb.block_at(struct.unpack_from('<I', db, MANAGER_MATRICES)[0])
        if ha is None or hb is None or ha[1] != 0 or hb[1] != 0 or pairing.ab.get(ha[0]) != hb[0]:
            continue
        size = min(len(sa.blocks[ha[0]][3] or b''), len(sb.blocks[hb[0]][3] or b''))
        masks.setdefault(ha[0], set()).update(
            k * MATRIX_SIZE + MATRIX_PADDING for k in range(n) if k * MATRIX_SIZE + MATRIX_PADDING < size)
    return masks


def _fixed_u32(s, va):
    for (base, length), data in s.fixed.items():
        if data is not None and base <= va and va + 4 <= base + length:
            return struct.unpack_from('<I', data, va - base)[0]
    return None


def _aim_residue(sa, sb, pairing):
    """{A block index: offsets} of H+0x270..+0x27f when the H blocks of
    both snapshots (H = [[0x4d24c0] + 0x1f]) are paired and both hold aim
    target 0 at +0x26c."""
    found = []
    for s in (sa, sb):
        root = _fixed_u32(s, s.image_base + AIM_ROOT - REFERENCE_IMAGE_BASE)
        hit = s.block_at(root) if root is not None else None
        data = s.blocks[hit[0]][3] if hit is not None else None
        if data is None or hit[1] + AIM_PANEL + 4 > len(data):
            return {}
        hit = s.block_at(struct.unpack_from('<I', data, hit[1] + AIM_PANEL)[0])
        data = s.blocks[hit[0]][3] if hit is not None else None
        if hit is None or hit[1] != 0 or data is None or len(data) < AIM_FIELDS[1]:
            return {}
        if struct.unpack_from('<I', data, AIM_TARGET)[0] != 0:
            return {}
        found.append(hit[0])
    if pairing.ab.get(found[0]) != found[1]:
        return {}
    return {found[0]: set(range(*AIM_FIELDS))}


def compare(sa, sb, ignore=(), max_differences=50, scope='all', noise=None, unreached='skip', classes=True):
    """The graph comparison (noise model). Returns a result dictionary."""
    noise = noise or NoiseModel()
    differences, notes, noise_hits, informational, class_hits = [], [], [], [], []
    stats = {'fixed_regions': 0, 'fixed_bytes': 0, 'blocks': 0, 'block_bytes': 0, 'pointers_canonicalized': 0,
             'foreign_pointers': 0, 'pairing_conflicts': 0, 'noise': 0,
             'unreached_a': 0, 'unreached_b': 0, 'unreached_bytes_a': 0, 'unreached_bytes_b': 0,
             'unreached_compared': 0, 'unreached_differences': 0,
             'environment_blocks_a': 0, 'environment_blocks_b': 0, 'environment_links': 0, 'matrix_padding': 0,
             'allocation_slack': 0, 'block_fit': 0, 'byte_count': 0, 'block_size_delta': 0,
             'aim_residue': 0, 'freed_target': 0}
    env_a = sa.environment()[1] if classes else frozenset()
    env_b = sb.environment()[1] if classes else frozenset()
    stats['environment_blocks_a'], stats['environment_blocks_b'] = len(env_a), len(env_b)

    def add(kind, where, a=None, b=None, path=None, offset=None, length=1, **extra):
        item = {'kind': kind, 'where': where}
        if a is not None:
            item['a'] = a
        if b is not None:
            item['b'] = b
        if path is not None:
            item['path'], item['offset'], item['length'] = path, offset, length
        item.update(extra)
        if path is not None and noise.covers(path, offset, offset + length - 1):
            noise_hits.append(item)
            stats['noise'] += 1
        else:
            differences.append(item)

    if sa.image_size != sb.image_size:
        add('header', 'image_size', sa.image_size, sb.image_size)
    if sa.frame != sb.frame:
        add('header', 'frame', sa.frame, sb.frame)
    if (sa.flags ^ sb.flags) & (FLAG_HEAP | FLAG_NO_HEAP | FLAG_HEAP_ERROR):
        add('header', 'heap flags', sa.flag_names(), sb.flag_names())
    if (sa.flags | sb.flags) & FLAG_TRUNCATED:
        notes.append('truncated snapshot: blocks without contents are not compared')
    if (sa.flags ^ sb.flags) & FLAG_EARLY:
        notes.append('one run armed early, the other did not')
    runtimes = sa.heap_runtime != sb.heap_runtime and 'none' not in (sa.heap_runtime, sb.heap_runtime)
    if runtimes:
        notes.append('heaps of different runtimes: A %s, B %s (same block layout; unreached blocks differ)' % (
            sa.heap_runtime, sb.heap_runtime))
    runtime_classes = classes and runtimes

    def class_hit(name, where, a, b, **extra):
        stats[name] += 1
        if len(class_hits) < max_differences:
            item = {'class': name, 'where': where, 'a': a, 'b': b}
            item.update(extra)
            class_hits.append(item)
    if not (sa.memory and sb.memory):
        notes.append('no memory map (version 1 or map missing): pointers outside the image and the heap are plain values')
    for key in sorted(set(sa.fixed) ^ set(sb.fixed)):
        add('fixed_missing', 'fixed %s+%d' % (_hex(key[0]), key[1]), key in sa.fixed, key in sb.fixed)

    pairing = Pairing()
    queue = collections.deque()

    def walk(da, db, path_of):
        """Pairs the blocks that the windows of da and db point to (same
        position, same offset into the block) and queues the new pairs."""
        for pos, i, off in sa.block_windows(da):
            if pos + 4 > len(db):
                continue
            hit = sb.block_at(struct.unpack_from('<I', db, pos)[0])
            if hit is None or hit[1] != off:
                continue
            j = hit[0]
            if i in env_a or j in env_b:
                stats['environment_links'] += 1
                continue
            if pairing.link(i, j, path_of(pos)):
                ba_, bb_ = sa.blocks[i], sb.blocks[j]
                if ba_[2] and bb_[2] and ba_[3] is not None and bb_[3] is not None:
                    queue.append((i, j))

    # The fixed bytes in scope are the roots; then breadth-first.
    root_list = _roots(sa, sb, scope)
    for key, va, length in root_list:
        a_all, b_all = sa.fixed[key], sb.fixed[key]
        where = 'fixed %s+%d' % (_hex(key[0]), key[1])
        if a_all is None or b_all is None:
            if (a_all is None) != (b_all is None):
                add('fixed_unreadable', where, a_all is not None, b_all is not None)
            continue
        lo = va - key[0]
        walk(a_all[lo:lo + length], b_all[lo:lo + length], lambda pos, base=va: _hex(base + pos))
    while queue:
        i, j = queue.popleft()
        walk(sa.blocks[i][3], sb.blocks[j][3], lambda pos, p=pairing.path[i]: '%s/+0x%x' % (p, pos))
    stats['pairing_conflicts'] = len(pairing.conflicts)
    for c in pairing.conflicts[:max_differences]:
        add('pairing_conflict', c['path'], c['a_paired_with'], c['b_paired_with'],
            note='A block %d and B block %d are already paired elsewhere' % (c['a_block'], c['b_block']))

    # Byte comparison of the fixed bytes in scope.
    for key, va, length in root_list:
        a_all, b_all = sa.fixed[key], sb.fixed[key]
        if a_all is None or b_all is None:
            continue
        lo = va - key[0]
        a, b = a_all[lo:lo + length], b_all[lo:lo + length]
        stats['fixed_regions'] += 1
        stats['fixed_bytes'] += length
        ignored = set()
        for iva, ilen in ignore:
            s, e = max(iva, va), min(iva + ilen, va + length)
            ignored.update(range(s - va, e - va))
        count = BYTE_COUNT - va
        if runtime_classes and 0 <= count and count + 4 <= length and count not in ignored:
            if a[count:count + 4] != b[count:count + 4]:
                class_hit('byte_count', 'fixed %s' % _hex(BYTE_COUNT), a[count:count + 4].hex(),
                          b[count:count + 4].hex(), path='fixed', offset=BYTE_COUNT)
            ignored.update(range(count, count + 4))
        freed = [] if runtime_classes else None
        for r0, r1 in _runs(_compare_data(a, b, sa, sb, pairing, stats, ignored, freed)):
            add('bytes', 'fixed %s' % _hex(va + r0), a[r0:r1 + 1].hex(), b[r0:r1 + 1].hex(),
                path='fixed', offset=va + r0, length=r1 - r0 + 1)
        for w in freed or ():
            class_hit('freed_target', 'fixed %s' % _hex(va + w), a[w:w + 4].hex(), b[w:w + 4].hex(),
                      path='fixed', offset=va + w)

    # Paired blocks.
    padding = _matrix_padding(sa, sb, pairing) if classes else {}
    aim = _aim_residue(sa, sb, pairing) if classes else {}
    for i in sorted(pairing.ab, key=lambda x: pairing.path[x]):
        j = pairing.ab[i]
        ba_, bb_ = sa.blocks[i], sb.blocks[j]
        path = pairing.path[i]
        where = 'block %s (A #%d 0x%08x, B #%d 0x%08x)' % (path, i, ba_[0], j, bb_[0])
        if ba_[2] != bb_[2]:
            add('in_use', where, ba_[2], bb_[2], path=path, offset=-2, length=1)
            continue
        if not ba_[2] or ba_[3] is None or bb_[3] is None:
            continue
        if ba_[1] != bb_[1]:
            stats['block_size_delta'] += bb_[1] - ba_[1]
            if runtime_classes and abs(bb_[1] - ba_[1]) < MIN_BLOCK and not noise.covers(path, -1, -1):
                class_hit('block_fit', where, ba_[1], bb_[1], path=path)
            else:
                add('block_size', where, ba_[1], bb_[1], path=path, offset=-1, length=1)
        stats['blocks'] += 1
        stats['block_bytes'] += len(ba_[3])
        masked = padding.get(i)
        aim_mask = aim.get(i)
        common = min(len(ba_[3]), len(bb_[3]))
        freed = [] if runtime_classes else None
        for r0, r1 in _runs(_compare_data(ba_[3], bb_[3], sa, sb, pairing, stats,
                                          (masked or set()) | (aim_mask or set()), freed)):
            if runtime_classes and r0 >= common - SLACK_BYTES and not noise.covers(path, r0, r1):
                class_hit('allocation_slack', '%s +0x%x' % (where, r0), ba_[3][r0:r1 + 1].hex(),
                          bb_[3][r0:r1 + 1].hex(), path=path, offset=r0, block_size=ba_[1])
                continue
            add('bytes', '%s +0x%x' % (where, r0), ba_[3][r0:r1 + 1].hex(), bb_[3][r0:r1 + 1].hex(),
                path=path, offset=r0, length=r1 - r0 + 1, block_size=ba_[1])
        for k in sorted(masked or ()):
            if k < common and ba_[3][k] != bb_[3][k]:
                class_hit('matrix_padding', '%s +0x%x' % (where, k), ba_[3][k:k + 1].hex(),
                          bb_[3][k:k + 1].hex(), path=path, offset=k, node=k // MATRIX_SIZE)
        for r0, r1 in _runs([k for k in sorted(aim_mask or ()) if k < common and ba_[3][k] != bb_[3][k]]):
            class_hit('aim_residue', '%s +0x%x' % (where, r0), ba_[3][r0:r1 + 1].hex(),
                      bb_[3][r0:r1 + 1].hex(), path=path, offset=r0)
        for w in freed or ():
            class_hit('freed_target', '%s +0x%x' % (where, w), ba_[3][w:w + 4].hex(), bb_[3][w:w + 4].hex(),
                      path=path, offset=w)

    # Unreached blocks: counted; aligned and compared for information.
    ua = [i for i, blk in enumerate(sa.blocks) if blk[2] and i not in pairing.ab]
    ub = [j for j, blk in enumerate(sb.blocks) if blk[2] and j not in pairing.ba]
    stats['unreached_a'], stats['unreached_b'] = len(ua), len(ub)
    stats['unreached_bytes_a'] = sum(sa.blocks[i][1] - 8 for i in ua)
    stats['unreached_bytes_b'] = sum(sb.blocks[j][1] - 8 for j in ub)
    if unreached == 'align':
        # Unreached blocks aligned by the walk's size sequence (allocation
        # history); pointers between them use the alignment as well.
        matcher = difflib.SequenceMatcher(None, [sa.blocks[i][1] for i in ua], [sb.blocks[j][1] for j in ub],
                                          autojunk=False)
        aligned = [(ua[m.a + k], ub[m.b + k]) for m in matcher.get_matching_blocks() for k in range(m.size)]
        extended = Pairing()
        extended.ab, extended.ba = dict(pairing.ab), dict(pairing.ba)
        for i, j in aligned:
            extended.ab[i], extended.ba[j] = j, i
        for i, j in aligned:
            da, db = sa.blocks[i][3], sb.blocks[j][3]
            if da is None or db is None:
                continue
            stats['unreached_compared'] += 1
            runs = _runs(_compare_data(da, db, sa, sb, extended, {'pointers_canonicalized': 0,
                                                                  'foreign_pointers': 0}))
            if runs:
                stats['unreached_differences'] += len(runs)
                if len(informational) < max_differences:
                    r0, r1 = runs[0]
                    informational.append({'kind': 'unreached', 'where': 'A #%d 0x%08x / B #%d 0x%08x +0x%x' % (
                        i, sa.blocks[i][0], j, sb.blocks[j][0], r0), 'a': da[r0:r1 + 1].hex(),
                        'b': db[r0:r1 + 1].hex(), 'runs': len(runs), 'block_size': sa.blocks[i][1]})
    return {
        'identical': not differences,
        'differences': len(differences),
        'first_differences': differences[:max_differences],
        'noise_matched': noise_hits[:max_differences],
        'classes_matched': class_hits,
        'classes': classes,
        'runtime_classes': runtime_classes,
        'unreached_differences': informational,
        'notes': notes,
        'scope': scope,
        'stats': stats,
        'a': {'name': sa.name, 'version': sa.version, 'flags': sa.flag_names(), 'frame': sa.frame,
              'blocks': len(sa.blocks)},
        'b': {'name': sb.name, 'version': sb.version, 'flags': sb.flag_names(), 'frame': sb.frame,
              'blocks': len(sb.blocks)},
    }


def compare_index(sa, sb, ignore=(), max_differences=50):
    """Comparison by walk position (diagnosis only, --pairing index):
    block k of A against block k of B, pointers as (walk index, offset)."""
    differences = []
    stats = {'fixed_regions': 0, 'fixed_bytes': 0, 'blocks': 0, 'block_bytes': 0, 'pointers_canonicalized': 0}
    pairing = Pairing()
    for k in range(min(len(sa.blocks), len(sb.blocks))):
        pairing.ab[k], pairing.ba[k], pairing.path[k] = k, k, '#%d' % k
    local = {'pointers_canonicalized': 0, 'foreign_pointers': 0}
    if len(sa.blocks) != len(sb.blocks):
        differences.append({'kind': 'heap_layout', 'where': 'block count', 'a': len(sa.blocks), 'b': len(sb.blocks)})
    for key in sorted(set(sa.fixed) & set(sb.fixed)):
        a, b = sa.fixed[key], sb.fixed[key]
        if a is None or b is None:
            continue
        stats['fixed_regions'] += 1
        stats['fixed_bytes'] += key[1]
        ignored = set()
        for iva, ilen in ignore:
            s, e = max(iva, key[0]), min(iva + ilen, key[0] + key[1])
            ignored.update(range(s - key[0], e - key[0]))
        for r0, r1 in _runs(_compare_data(a, b, sa, sb, pairing, local, ignored)):
            differences.append({'kind': 'bytes', 'where': 'fixed %s' % _hex(key[0] + r0), 'a': a[r0:r1 + 1].hex(),
                                'b': b[r0:r1 + 1].hex(), 'length': r1 - r0 + 1})
    reported = False
    for k, (ba_, bb_) in enumerate(zip(sa.blocks, sb.blocks)):
        if ba_[1] != bb_[1] or ba_[2] != bb_[2]:
            if not reported:
                differences.append({'kind': 'heap_layout', 'where': 'block #%d' % k, 'a': [ba_[1], ba_[2]],
                                    'b': [bb_[1], bb_[2]]})
                reported = True
            continue
        if not ba_[2] or ba_[3] is None or bb_[3] is None:
            continue
        stats['blocks'] += 1
        stats['block_bytes'] += len(ba_[3])
        for r0, r1 in _runs(_compare_data(ba_[3], bb_[3], sa, sb, pairing, local)):
            differences.append({'kind': 'bytes', 'where': 'block #%d +0x%x' % (k, r0), 'a': ba_[3][r0:r1 + 1].hex(),
                                'b': bb_[3][r0:r1 + 1].hex(), 'length': r1 - r0 + 1})
    stats['pointers_canonicalized'] = local['pointers_canonicalized']
    return {'identical': not differences, 'differences': len(differences),
            'first_differences': differences[:max_differences], 'noise_matched': [], 'unreached_differences': [],
            'notes': ['index pairing (L0): diagnosis only'], 'scope': 'all', 'stats': stats,
            'a': {'name': sa.name, 'version': sa.version, 'flags': sa.flag_names(), 'frame': sa.frame,
                  'blocks': len(sa.blocks)},
            'b': {'name': sb.name, 'version': sb.version, 'flags': sb.flag_names(), 'frame': sb.frame,
                  'blocks': len(sb.blocks)}}


def noise_entries(result):
    """The noise file's entries from a comparison of two original runs."""
    entries = []
    for d in result['first_differences']:
        if 'path' not in d:
            continue
        entries.append({'path': d['path'], 'offset': d['offset'], 'length': d['length'], 'kind': d['kind'],
                        'where': d['where'], 'a': d.get('a'), 'b': d.get('b')})
    return entries


def summary(s):
    in_use = [b for b in s.blocks if b[2]]
    return {
        'name': s.name, 'version': s.version, 'flags': s.flag_names(), 'frame': s.frame,
        'image_base': '0x%08x' % s.image_base, 'heap_runtime': s.heap_runtime,
        'fixed_regions': len(s.fixed), 'fixed_unreadable': sum(1 for v in s.fixed.values() if v is None),
        'heap_regions': len(s.regions), 'blocks': len(s.blocks), 'blocks_in_use': len(in_use),
        'bytes_in_use': sum(b[1] - 8 for b in in_use), 'blocks_with_contents': sum(1 for b in in_use if b[3] is not None),
        'memory_regions': len(s.memory), 'modules': sorted(name for _, name in s.modules.values()),
        'environment_array': s.environment()[0], 'environment_blocks': len(s.environment()[1]),
    }


def _print_result(result):
    for item in result['first_differences']:
        print('DIFF %s %s: A %s B %s' % (item['kind'], item['where'], item.get('a'), item.get('b')))
    for note in result['notes']:
        print('NOTE %s' % note)
    st = result['stats']
    if 'unreached_a' in st:
        print('NOISE %d differences matched the noise file; %d foreign pointers; unreached blocks A %d (%d bytes), '
              'B %d (%d bytes)%s' % (st['noise'], st['foreign_pointers'], st['unreached_a'], st['unreached_bytes_a'],
                                     st['unreached_b'], st['unreached_bytes_b'],
                                     '; aligned %d, %d differing runs (information only)' % (
                                         st['unreached_compared'], st['unreached_differences'])
                                     if st['unreached_compared'] else ''))
    if result.get('classes'):
        print('CLASSES %d matrix padding bytes (padding contract); %d links into the environment not followed '
              '(environment blocks A %d, B %d)' % (st['matrix_padding'], st['environment_links'],
                                                   st['environment_blocks_a'], st['environment_blocks_b']))
        print('CLASSES %d aim residue runs (cockpit_sync stack residue)' % st['aim_residue'])
    if result.get('runtime_classes'):
        print('CLASSES %d allocation slack runs, %d block fits, %d byte count (heaps of different runtimes); '
              'paired block sizes B-A %+d bytes' % (st['allocation_slack'], st['block_fit'], st['byte_count'],
                                                    st['block_size_delta']))
        print('CLASSES %d pointers into freed blocks on both sides' % st['freed_target'])
    print('%s: %d differences; scope %s: %d fixed regions (%d bytes), %d paired blocks (%d bytes), '
          '%d pointers canonicalized, %d pairing conflicts' % (
              'IDENTICAL' if result['identical'] else 'DIFFERENT', result['differences'], result['scope'],
              st['fixed_regions'], st['fixed_bytes'], st['blocks'], st['block_bytes'], st['pointers_canonicalized'],
              st.get('pairing_conflicts', 0)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('summary')
    p.add_argument('snapshot')
    for name in ('compare', 'noise'):
        p = sub.add_parser(name)
        p.add_argument('a')
        p.add_argument('b')
        p.add_argument('--scope', choices=('all', 'pools'), default='all')
        p.add_argument('--ignore', type=parse_range, action='append', default=[], help='fixed range VA:LEN not compared')
        p.add_argument('--allow-incomplete', action='store_true', help='compare snapshots without the complete flag')
        p.add_argument('--no-classes', action='store_true',
                       help='switch off the classes (environment, matrix_padding; '
                            'allocation_slack, block_fit, byte_count; aim_residue, freed_target)')
        if name == 'compare':
            p.add_argument('--noise', help='noise file written by `noise` from two original runs')
            p.add_argument('--pairing', choices=('graph', 'index'), default='graph')
            p.add_argument('--unreached', choices=('skip', 'align'), default='skip')
            p.add_argument('--json', help='write the full result here')
            p.add_argument('--max-differences', type=int, default=50)
        else:
            p.add_argument('--out', required=True, help='the noise file to write')
    args = parser.parse_args(argv)
    try:
        if args.command == 'summary':
            print(json.dumps(summary(read(args.snapshot)), indent=1))
            return 0
        sa, sb = read(args.a), read(args.b)
        noise = NoiseModel.load(args.noise) if getattr(args, 'noise', None) else None
    except (OSError, SnapshotError, ValueError) as error:
        print('error: %s' % error, file=sys.stderr)
        return 2
    for s in (sa, sb):
        if not s.complete and not args.allow_incomplete:
            print('error: %s is incomplete (the proxy did not finish writing it)' % s.name, file=sys.stderr)
            return 2
    if args.command == 'noise':
        result = compare(sa, sb, args.ignore, max_differences=1 << 30, scope=args.scope, classes=not args.no_classes)
        entries = noise_entries(result)
        out = {'schema': NOISE_SCHEMA, 'scope': args.scope, 'a': sa.name, 'b': sb.name, 'stats': result['stats'],
               'unmodelled': [d for d in result['first_differences'] if 'path' not in d], 'entries': entries}
        with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(out, f, indent=1)
            f.write('\n')
        st = result['stats']
        print('NOISE %d entries (%d bytes) written to %s; %d foreign pointers; %d unmodelled differences; '
              'unreached blocks A %d, B %d' % (len(entries), sum(e['length'] for e in entries if e['length'] > 0),
                                               args.out, st['foreign_pointers'], len(out['unmodelled']),
                                               st['unreached_a'], st['unreached_b']))
        return 0
    if args.pairing == 'index':
        result = compare_index(sa, sb, args.ignore, args.max_differences)
    else:
        result = compare(sa, sb, args.ignore, args.max_differences, args.scope, noise, args.unreached,
                         classes=not args.no_classes)
    if args.json:
        with open(args.json, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(result, f, indent=1)
            f.write('\n')
    _print_result(result)
    return 0 if result['identical'] else 1


if __name__ == '__main__':
    sys.exit(main())
