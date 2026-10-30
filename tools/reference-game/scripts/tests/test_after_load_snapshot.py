"""after_load_snapshot.py on synthetic snapshots (the ES2SNAP1 layout of
proxy/early_arming.h, versions 1 and 2); no game data required. The proxy
fixture //tools/reference-game/proxy:early_arming_snapshot_test runs the
script on files the proxy code itself wrote.

The noise model is checked on snapshots shaped like a real noise run: an
environment that adds a block in front of the simulation's blocks, foreign
pointers, fields that vary between original runs."""
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import after_load_snapshot as als

IMAGE_BASE, IMAGE_SIZE = 0x400000, 0x2d8000
FIXED_VA = 0x4a9b78          # holds the MECH pool pointer cell 0x4a9b7a
OTHER_VA = 0x4d24bc          # a fixed region outside the pool roots


def u32(*values):
    return b''.join(struct.pack('<I', v) for v in values)


def snapshot(fixed, regions, flags=als.FLAG_HEAP | als.FLAG_COMPLETE, frame=0, image_base=IMAGE_BASE,
             memory=None, modules=None):
    """fixed: [(va, bytes or None)]; regions: [(base, [(size, in_use, payload or None)])];
    memory/modules (version 2): [(base, size, allocation, protect, type)], [(base, size, name)]."""
    version = 2 if memory is not None else 1
    out = bytearray(b'ES2SNAP1' + u32(version, flags, frame, image_base, IMAGE_SIZE, len(fixed)))
    for va, data in fixed:
        out += u32(va, len(data) if data is not None else 4, 1 if data is not None else 0)
        if data is not None:
            out += data
    out += u32(len(regions))
    for base, blocks in regions:
        size = 8 + sum(b[0] for b in blocks)
        out += u32(base, size, 1, len(blocks))
        p = base + 8
        for block_size, in_use, payload in blocks:
            out += u32(p, block_size, (1 if in_use else 0) | (2 if payload is not None else 0))
            if payload is not None:
                assert len(payload) == block_size - 8
                out += payload
            p += block_size
    if version == 2:
        out += u32(len(memory))
        for m in memory:
            out += u32(*m)
        out += u32(len(modules or []))
        for base, size, name in modules or []:
            raw = name.encode('utf-8')
            out += u32(base, size, len(raw)) + raw
    return bytes(out)


def heap(base, text=b'mech', pointer_offset=0x10, odd=False, change=None, environment=0, clock=0x1234,
         foreign=0x00bf1000, other=0x12345678):
    """The simulation graph: the fixed MECH pool cell (FIXED_VA + 2, or +1
    when `odd`) points to block S0 + pointer_offset; S0 points to S1 + 4,
    into the image and (foreign) outside; S1 holds `text` and a clock word.
    `environment` start-up blocks (unreached; 24 bytes each, the first one
    four bytes larger per extra block, like `_environ`) come first, then S0,
    S1, a free block; region 2 holds an unreached block pointing to S0."""
    env = [(24 + 4 * environment, True, bytes(16 + 4 * environment))] + [(24, True, b'VAR=%d' % k + bytes(16 - len(b'VAR=%d' % k)))
                                                                          for k in range(environment)]
    p = base + 8 + sum(e[0] for e in env)
    s0, s1 = p, p + 40
    block0 = bytearray(32)
    block0[0:4] = u32(s1 + 4)
    block0[4:8] = u32(IMAGE_BASE + 0x9b7a)
    block0[8:12] = u32(foreign)
    block1 = bytearray(32)
    block1[0:len(text)] = text
    block1[16:20] = u32(clock)
    if change:
        change(block0, block1)
    second = base + 0x100000
    regions = [(base, env + [(40, True, bytes(block0)), (40, True, bytes(block1)), (16, False, None)]),
               (second, [(16, True, u32(s0, 7))])]
    fixed = bytearray(12)
    at = 1 if odd else 2
    fixed[at:at + 4] = u32(s0 + pointer_offset)
    fixed[8:12] = u32(0x12345678)
    return [(FIXED_VA, bytes(fixed)), (OTHER_VA, u32(other)), (0x49b0f8, None)], regions


def memory_map(base, foreign_base=0x00bf0000, module_base=0x75000000):
    """A version-2 map: the image, the two heap regions, a private region
    for foreign pointers and one module."""
    return ([(IMAGE_BASE, IMAGE_SIZE, IMAGE_BASE, 4, als.MEM_IMAGE),
             (base, 0x10000, base, 4, als.MEM_PRIVATE), (base + 0x100000, 0x10000, base + 0x100000, 4, als.MEM_PRIVATE),
             (foreign_base, 0x20000, foreign_base, 4, als.MEM_PRIVATE),
             (module_base, 0x1000, module_base, 2, als.MEM_IMAGE), (module_base + 0x1000, 0x5000, module_base, 0x20,
                                                                    als.MEM_IMAGE)],
            [(module_base, 0x6000, 'KERNEL32.DLL')])


def parse(data, name='s'):
    return als.Snapshot(data, name)


def pair(**kw):
    return parse(snapshot(*heap(**kw)))


class Graph(unittest.TestCase):
    def test_relocated_heap_is_identical(self):
        for odd in (False, True):
            a = pair(base=0x01000000, odd=odd)
            b = pair(base=0x05370000, odd=odd)
            result = als.compare(a, b)
            self.assertTrue(result['identical'], result)
            # fixed -> S0 and S0 -> S1 (the image pointer is equal bytes)
            self.assertEqual(result['stats']['pointers_canonicalized'], 2)
            self.assertEqual(result['stats']['blocks'], 2)
            self.assertEqual(result['stats']['unreached_a'], 2)   # the _environ block and region 2's block

    def test_environment_blocks_do_not_shift_the_pairing(self):
        # One more environment variable in B; index pairing
        # reports differences, the graph pairing none.
        a = pair(base=0x025f0000)
        b = pair(base=0x026e0000, environment=1)
        self.assertTrue(als.compare(a, b)['identical'])
        old = als.compare_index(a, b)
        self.assertFalse(old['identical'])
        self.assertIn('heap_layout', [d['kind'] for d in old['first_differences']])
        result = als.compare(a, b)
        self.assertEqual((result['stats']['unreached_a'], result['stats']['unreached_b']), (2, 3))

    def test_content_difference_names_the_path(self):
        a = pair(base=0x01000000)
        b = pair(base=0x02000000, text=b'mecX')
        result = als.compare(a, b)
        self.assertFalse(result['identical'])
        self.assertEqual(result['differences'], 1)
        d = result['first_differences'][0]
        self.assertEqual((d['kind'], d['a'], d['b'], d['length'], d['offset']), ('bytes', '68', '58', 1, 3))
        self.assertEqual(d['path'], '0x%08x/+0x0' % (FIXED_VA + 2))

    def test_pointer_to_another_offset_is_a_difference(self):
        a = pair(base=0x01000000)
        b = pair(base=0x02000000, pointer_offset=0x14)
        result = als.compare(a, b)
        self.assertFalse(result['identical'])
        self.assertEqual(result['first_differences'][0]['where'], 'fixed 0x%08x' % (FIXED_VA + 2))

    def test_image_pointer_difference(self):
        def image_pointer(block0, block1):
            block0[4:8] = u32(IMAGE_BASE + 0x9b7e)
        a = pair(base=0x01000000)
        b = pair(base=0x02000000, change=image_pointer)
        result = als.compare(a, b)
        self.assertEqual(result['differences'], 1)
        self.assertEqual(result['first_differences'][0]['offset'], 4)

    def test_pairing_conflict(self):
        # S0 points twice into S1 in A, but into S1 and into region 2's block in B.
        def twice(block0, block1):
            block0[12:16] = u32(struct.unpack('<I', bytes(block0[0:4]))[0] - 4)
        a = pair(base=0x01000000, change=twice)
        fixed, regions = heap(base=0x02000000, change=twice)
        b0 = bytearray(regions[0][1][1][2])
        b0[12:16] = u32(0x02000000 + 0x100000 + 8)
        regions[0][1][1] = (40, True, bytes(b0))
        result = als.compare(a, parse(snapshot(fixed, regions)))
        self.assertIn('pairing_conflict', [d['kind'] for d in result['first_differences']])
        self.assertEqual(result['stats']['pairing_conflicts'], 1)

    def test_block_size_difference(self):
        a = pair(base=0x01000000)
        fixed, regions = heap(base=0x02000000)
        payload = regions[0][1][2][2] + bytes(4)
        regions[0][1][2] = (44, True, payload)
        regions[0][1][3] = (12 + 4, False, None)
        result = als.compare(a, parse(snapshot(fixed, regions)))
        self.assertEqual([d['kind'] for d in result['first_differences']], ['block_size'])

    def test_scope_pools_ignores_other_fixed_bytes(self):
        a = pair(base=0x01000000)
        b = pair(base=0x02000000, other=0x87654321)
        self.assertFalse(als.compare(a, b)['identical'])
        result = als.compare(a, b, scope='pools')
        self.assertTrue(result['identical'], result)
        self.assertEqual(result['stats']['fixed_bytes'], 8)   # the MECH pool and removal-list cells
        self.assertEqual(result['stats']['blocks'], 2)

    def test_ignore_masks_fixed_bytes(self):
        a = pair(base=0x01000000)
        fixed, regions = heap(base=0x01000000)
        changed = bytearray(fixed[0][1]); changed[9] ^= 0xff
        fixed[0] = (fixed[0][0], bytes(changed))
        b = parse(snapshot(fixed, regions))
        self.assertFalse(als.compare(a, b)['identical'])
        self.assertTrue(als.compare(a, b, ignore=[(FIXED_VA + 8, 4)])['identical'])

    def test_header_and_region_differences(self):
        fixed, regions = heap(base=0x01000000)
        a = parse(snapshot(fixed, regions))
        b = parse(snapshot(fixed, regions, frame=1))
        self.assertEqual(als.compare(a, b)['first_differences'][0]['where'], 'frame')
        b = parse(snapshot(fixed, [], flags=als.FLAG_NO_HEAP | als.FLAG_COMPLETE))
        kinds = {d['kind'] for d in als.compare(a, b)['first_differences']}
        self.assertIn('header', kinds)
        b = parse(snapshot(fixed[:2], regions))
        self.assertEqual(als.compare(a, b)['first_differences'][0]['kind'], 'fixed_missing')

    def test_truncated_blocks_are_not_compared(self):
        fixed, regions = heap(base=0x01000000)
        a = parse(snapshot(fixed, regions))
        fixed, regions = heap(base=0x02000000, text=b'XXXX')
        regions[0][1][2] = (40, True, None)
        result = als.compare(a, parse(snapshot(fixed, regions, flags=als.FLAG_HEAP | als.FLAG_TRUNCATED |
                                                als.FLAG_COMPLETE)))
        self.assertTrue(result['identical'])
        self.assertTrue(result['notes'])

    def test_unreached_alignment_is_information_only(self):
        fixed, regions = heap(base=0x01000000)
        a = parse(snapshot(fixed, regions))
        fixed, regions = heap(base=0x02000000)
        regions[1] = (regions[1][0], [(16, True, u32(0x02000008 + 24, 8))])   # region 2's block changed
        b = parse(snapshot(fixed, regions))
        result = als.compare(a, b, unreached='align')
        self.assertTrue(result['identical'])
        self.assertEqual(result['stats']['unreached_compared'], 2)
        self.assertEqual(result['stats']['unreached_differences'], 1)
        self.assertEqual(result['unreached_differences'][0]['kind'], 'unreached')


class ForeignAndModules(unittest.TestCase):
    def v2(self, base, foreign_base=0x00bf0000, module_base=0x75000000, foreign=None, **kw):
        memory, modules = memory_map(base, foreign_base, module_base)
        fixed, regions = heap(base=base, foreign=foreign if foreign is not None else foreign_base + 0x1000, **kw)
        return parse(snapshot(fixed, regions, memory=memory, modules=modules))

    def test_foreign_pointers_compare_by_type(self):
        a = self.v2(0x01000000)
        b = self.v2(0x02000000, foreign_base=0x02470000)
        result = als.compare(a, b)
        self.assertTrue(result['identical'], result)
        self.assertEqual(result['stats']['foreign_pointers'], 1)
        # Without the map (version 1) the same values are a difference.
        self.assertFalse(als.compare(pair(base=0x01000000, foreign=0x00bf1000),
                                     pair(base=0x02000000, foreign=0x02471000))['identical'])

    def test_module_pointers_compare_by_module_and_offset(self):
        a = self.v2(0x01000000, foreign=0x75001234)
        b = self.v2(0x02000000, module_base=0x76000000, foreign=0x76001234)
        self.assertTrue(als.compare(a, b)['identical'])
        c = self.v2(0x02000000, module_base=0x76000000, foreign=0x76001238)
        self.assertFalse(als.compare(a, c)['identical'])

    def test_foreign_against_heap_is_a_difference(self):
        a = self.v2(0x01000000)
        b = self.v2(0x02000000, foreign=0x02000008)   # B points into its own heap instead
        self.assertFalse(als.compare(a, b)['identical'])

    def test_classify(self):
        s = self.v2(0x01000000)
        self.assertEqual(s.classify(IMAGE_BASE + 5), ('image', 5))
        self.assertEqual(s.classify(0x01000008), ('block', 0, 0))
        self.assertEqual(s.classify(0x01000008 + 16), ('block', 0, 16))            # end of the _environ block's data
        self.assertEqual(s.classify(0x01000008 + 24 + 40 + 40 + 3), ('block', 3, 3))   # stale pointer into the free block
        self.assertEqual(s.classify(0x01000004), ('heap_meta', 0, 4))
        self.assertEqual(s.classify(0x75001010), ('module', 'kernel32.dll', 0x1010))
        self.assertEqual(s.classify(0x00bf0010), ('foreign', als.MEM_PRIVATE))
        self.assertIsNone(s.classify(0x00ff0000))
        self.assertEqual(als.summary(s)['modules'], ['kernel32.dll'])
        self.assertEqual(als.summary(s)['memory_regions'], 6)


class Noise(unittest.TestCase):
    def test_noise_file_from_two_original_runs(self):
        a1 = pair(base=0x01000000, clock=0x1111)
        a2 = pair(base=0x02000000, clock=0x2222, environment=2)
        replaced_same = pair(base=0x03000000, clock=0x3333)
        replaced_bad = pair(base=0x03000000, clock=0x3333, text=b'mecX')
        result = als.compare(a1, a2, max_differences=1000)
        self.assertEqual(result['differences'], 1)
        entries = als.noise_entries(result)
        self.assertEqual([(e['path'], e['offset']) for e in entries], [('0x%08x/+0x0' % (FIXED_VA + 2), 16)])
        model = als.NoiseModel(entries)
        result = als.compare(a2, replaced_same, noise=model)
        self.assertTrue(result['identical'], result)
        self.assertEqual(result['stats']['noise'], 1)
        result = als.compare(a2, replaced_bad, noise=model)
        self.assertFalse(result['identical'])
        self.assertEqual(result['differences'], 1)


def addresses(base, sizes):
    """Data addresses of consecutive blocks starting at base + 8."""
    out, p = [], base + 8
    for size in sizes:
        out.append(p)
        p += size
    return out


TOKEN_NAME = b'ES2_CLONE_LOCK_TOKEN='


def environment_heap(base, token=b'621dc6531d66155b3aaa1e19cf5b3487', count=8, window=True, string_offset=0,
                     names=None):
    """The environment class, c3_09-loadout-b: `_environ` (count pointers and 0) first, then
    one 64-byte block per string (string 3 is the runner's lock token), then
    S0, which the fixed MECH cell points to. With `window` S0 holds, at the
    unaligned offset 6, bytes that equal the address of `_environ` entry 3
    (or the dword `window` when it is an int: the same bytes in another heap)."""
    sizes = [8 + 4 * (count + 1)] + [64] * count + [40]
    at = addresses(base, sizes)
    strings = at[1:1 + count]
    array = u32(*[s + string_offset for s in strings], 0)
    texts = []
    for k in range(count):
        text = (names[k] if names else b'VAR%02d=x' % k) if k != 3 else TOKEN_NAME + token
        texts.append(text + bytes(56 - len(text)))
    s0 = bytearray(32)
    s0[0:4] = b'mech'
    if window:
        s0[6:10] = u32(at[0] + 12 if window is True else window)
    blocks = [(sizes[0], True, array)] + [(64, True, t) for t in texts] + [(40, True, bytes(s0))]
    fixed = bytearray(12)
    fixed[2:6] = u32(at[-1])
    return [(FIXED_VA, bytes(fixed))], [(base, blocks)]


def manager_heap(base, residues=None, nodes=13, vtable=0x4a14c4, change=None):
    """The matrix_padding class, acceptance-60s: the fixed MECH cell points to an animation
    manager M (0x478838's layout: vtable at +0, node count word +0x0c, node
    matrices at +0x16); the node array holds `nodes` 32-byte matrices (byte
    18 the mode, byte 19 the padding from `residues`) and new[]'s 8 extra
    bytes, so 13 nodes give the 432-byte block of the live result."""
    sizes = [8 + 0x44, 8 + 32 * nodes + 8]
    at = addresses(base, sizes)
    manager = bytearray(0x44)
    manager[0:4] = u32(vtable)
    manager[0x0c:0x0e] = struct.pack('<h', nodes)
    manager[0x16:0x1a] = u32(at[1])
    matrices = bytearray(32 * nodes + 8)
    for k in range(nodes):
        matrices[32 * k:32 * k + 2] = struct.pack('<h', 0x4000)
        matrices[32 * k + 18] = 1
        matrices[32 * k + 19] = (residues or {}).get(k, 0)
        matrices[32 * k + 20:32 * k + 24] = u32(1000 * k)
    if change:
        change(matrices)
    fixed = bytearray(12)
    fixed[2:6] = u32(at[0])
    return [(FIXED_VA, bytes(fixed))], [(base, [(sizes[0], True, bytes(manager)), (sizes[1], True, bytes(matrices))])]


class Classes(unittest.TestCase):
    """The item 4 classes on the live differences (mission_load.md section 14)."""

    def test_environment_detection(self):
        s = parse(snapshot(*environment_heap(0x02690000)))
        index, blocks = s.environment()
        self.assertEqual(index, 0)
        self.assertEqual(blocks, frozenset(range(9)))
        self.assertEqual(als.summary(s)['environment_blocks'], 9)
        # Too few entries, pointers into the middle of a block, strings
        # without '=' or with control characters: no environment.
        for heap_ in (environment_heap(0x02690000, count=7), environment_heap(0x02690000, string_offset=4),
                      environment_heap(0x02690000, names=[b'PLAIN%02d' % k for k in range(8)]),
                      environment_heap(0x02690000, names=[b'A=\x01%02d' % k for k in range(8)])):
            self.assertEqual(parse(snapshot(*heap_)).environment(), (None, frozenset()))
        # The synthetic heaps of the other cases have no environment array.
        self.assertEqual(pair(base=0x01000000, environment=2).environment(), (None, frozenset()))

    def test_coincidental_window_into_the_environment(self):
        # O2 against R of c3_09: both heaps at the same addresses, so S0's
        # bytes reach `_environ` and the lock token string in both.
        a = parse(snapshot(*environment_heap(0x02690000)))
        b = parse(snapshot(*environment_heap(0x02690000, token=b'c307abdc61926e49b7999f4eb53fb087')))
        old = als.compare(a, b, classes=False)
        self.assertFalse(old['identical'])
        d = old['first_differences'][0]
        self.assertEqual((d['path'], d['offset']), ('0x%08x/+0x6/+0xc' % (FIXED_VA + 2), len(TOKEN_NAME)))
        result = als.compare(a, b)
        self.assertTrue(result['identical'], result)
        self.assertEqual(result['stats']['environment_links'], 1)
        self.assertEqual((result['stats']['environment_blocks_a'], result['stats']['environment_blocks_b']), (9, 9))
        self.assertEqual(result['stats']['blocks'], 1)   # only S0
        # O1 against O2: the heaps lie elsewhere, the bytes no longer point
        # into `_environ`, nothing is reached even without the classes.
        c = parse(snapshot(*environment_heap(0x02700000, token=b'e37' + b'0' * 29, window=0x02690008 + 12)))
        self.assertTrue(als.compare(c, a, classes=False)['identical'])

    def test_matrix_padding(self):
        a = parse(snapshot(*manager_heap(0x02b80000, residues={11: 2})))
        b = parse(snapshot(*manager_heap(0x02b40000)))
        old = als.compare(a, b, classes=False)
        self.assertEqual(old['differences'], 1)
        d = old['first_differences'][0]
        self.assertEqual((d['path'], d['offset'], d['a'], d['b'], d['block_size']),
                         ('0x%08x/+0x16' % (FIXED_VA + 2), 0x173, '02', '00', 432))
        result = als.compare(a, b)
        self.assertTrue(result['identical'], result)
        self.assertEqual(result['stats']['matrix_padding'], 1)
        hit = result['classes_matched'][0]
        self.assertEqual((hit['class'], hit['offset'], hit['node'], hit['a'], hit['b']),
                         ('matrix_padding', 0x173, 11, '02', '00'))
        # Every node's byte 19 is covered, nothing else.
        many = parse(snapshot(*manager_heap(0x02b80000, residues={k: 0x31 for k in range(13)})))
        self.assertEqual(als.compare(many, b)['stats']['matrix_padding'], 13)

        def mode(matrices):
            matrices[32 * 11 + 18] = 2

        def trailing(matrices):
            matrices[32 * 13 + 3] = 9      # new[]'s extra bytes after the last node
        for change in (mode, trailing):
            result = als.compare(parse(snapshot(*manager_heap(0x02b80000, residues={11: 2}, change=change))), b)
            self.assertEqual(result['differences'], 1, result)
            self.assertEqual(result['stats']['matrix_padding'], 1)
        # Another vtable: not a manager; a node count that disagrees: no mask.
        other = parse(snapshot(*manager_heap(0x02b80000, residues={11: 2}, vtable=0x4a14c8)))
        self.assertFalse(als.compare(other, parse(snapshot(*manager_heap(0x02b40000, vtable=0x4a14c8))))['identical'])
        twelve = als.compare(parse(snapshot(*manager_heap(0x02b80000, residues={11: 2}, nodes=12))),
                             parse(snapshot(*manager_heap(0x02b40000, nodes=12))))
        self.assertTrue(twelve['identical'], twelve)
        result = als.compare(parse(snapshot(*manager_heap(0x02b80000, residues={11: 2}, nodes=12))), b)
        self.assertEqual(result['stats']['matrix_padding'], 0)
        self.assertFalse(result['identical'])

    def test_noise_file_leaves_out_the_classes(self):
        a = parse(snapshot(*manager_heap(0x02b80000, residues={11: 3})))
        b = parse(snapshot(*manager_heap(0x02b40000)))
        self.assertEqual(als.noise_entries(als.compare(a, b, max_differences=1000)), [])
        self.assertEqual(len(als.noise_entries(als.compare(a, b, max_differences=1000, classes=False))), 1)

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'o.snap').write_bytes(snapshot(*manager_heap(0x02b80000, residues={11: 2})))
            (tmp / 'r.snap').write_bytes(snapshot(*manager_heap(0x02b40000)))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(als.main(['compare', str(tmp / 'o.snap'), str(tmp / 'r.snap')]), 0)
            self.assertIn('CLASSES 1 matrix padding bytes', out.getvalue())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(als.main(['compare', str(tmp / 'o.snap'), str(tmp / 'r.snap'), '--no-classes']), 1)
                self.assertEqual(als.main(['noise', str(tmp / 'o.snap'), str(tmp / 'r.snap'), '--out',
                                           str(tmp / 'n.json')]), 0)
            self.assertEqual(json.loads((tmp / 'n.json').read_text())['entries'], [])


OWN = als.FLAG_HEAP | als.FLAG_COMPLETE | als.FLAG_OWN_RUNTIME


def runtime_heap(base, count=0x00771c16, size1=40, **kw):
    """heap() plus the fixed dword 0x6bb30c (the byte counter of 0x47747c);
    size1 resizes S1 (zero bytes appended)."""
    fixed, regions = heap(base, **kw)
    blocks = regions[0][1]
    blocks[-2] = (size1, True, blocks[-2][2] + bytes(size1 - 40))
    return fixed + [(als.BYTE_COUNT, u32(count))], regions


class RuntimeClasses(unittest.TestCase):
    """The classes of heaps of different runtimes."""

    def last_bytes(self, first, value=0xab):
        def change(block0, block1):
            for k in range(first, 32):
                block1[k] = value
        return change

    def test_allocation_slack(self):
        cw = parse(snapshot(*runtime_heap(0x01000000)))
        for first, slack in ((31, True), (29, True), (28, False)):
            own = parse(snapshot(*runtime_heap(0x20000000, change=self.last_bytes(first)), flags=OWN))
            result = als.compare(cw, own)
            self.assertEqual(result['identical'], slack, (first, result['first_differences']))
            self.assertEqual(result['stats']['allocation_slack'], 1 if slack else 0)
            if slack:
                self.assertEqual(result['classes_matched'][0]['class'], 'allocation_slack')
                self.assertEqual(result['classes_matched'][0]['offset'], first)
            self.assertFalse(als.compare(cw, own, classes=False)['identical'])
        # Same runtime: the slack bytes are differences (two original runs agree on them).
        same = parse(snapshot(*runtime_heap(0x02000000, change=self.last_bytes(31))))
        result = als.compare(cw, same)
        self.assertFalse(result['identical'])
        self.assertFalse(result['runtime_classes'])
        self.assertEqual(result['stats']['allocation_slack'], 0)

    def test_block_fit(self):
        cw = parse(snapshot(*runtime_heap(0x01000000)))
        for size1, fit in ((44, True), (52, True), (56, False)):
            own = parse(snapshot(*runtime_heap(0x20000000, size1=size1), flags=OWN))
            result = als.compare(cw, own)
            self.assertEqual(result['identical'], fit, (size1, result['first_differences']))
            self.assertEqual(result['stats']['block_fit'], 1 if fit else 0)
            self.assertEqual(result['stats']['block_size_delta'], size1 - 40)
            if not fit:
                self.assertEqual([d['kind'] for d in result['first_differences']], ['block_size'])
        same = parse(snapshot(*runtime_heap(0x02000000, size1=44)))
        self.assertEqual([d['kind'] for d in als.compare(cw, same)['first_differences']], ['block_size'])
        # The common bytes are still compared.
        own = parse(snapshot(*runtime_heap(0x20000000, size1=44, text=b'tank'), flags=OWN))
        self.assertEqual([d['kind'] for d in als.compare(cw, own)['first_differences']], ['bytes'])

    def test_byte_count(self):
        cw = parse(snapshot(*runtime_heap(0x01000000, count=0x00771c0e)))
        own = parse(snapshot(*runtime_heap(0x20000000, count=0x00771c16), flags=OWN))
        result = als.compare(cw, own)
        self.assertTrue(result['identical'], result['first_differences'])
        self.assertEqual(result['stats']['byte_count'], 1)
        self.assertEqual(result['classes_matched'], [{'class': 'byte_count', 'where': 'fixed 0x006bb30c',
                                                      'a': '0e1c7700', 'b': '161c7700', 'path': 'fixed',
                                                      'offset': als.BYTE_COUNT}])
        ignored = als.compare(cw, own, ignore=[(als.BYTE_COUNT, 4)])
        self.assertTrue(ignored['identical'])
        self.assertEqual(ignored['stats']['byte_count'], 0)
        same = parse(snapshot(*runtime_heap(0x02000000, count=0x00771c16)))
        self.assertEqual([d['where'] for d in als.compare(cw, same)['first_differences']], ['fixed 0x006bb30c'])
        self.assertFalse(als.compare(cw, own, classes=False)['identical'])

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'cw.snap').write_bytes(snapshot(*runtime_heap(0x01000000, count=1)))
            (tmp / 'own.snap').write_bytes(snapshot(*runtime_heap(0x20000000, count=2, size1=44,
                                                                  change=self.last_bytes(31)), flags=OWN))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(als.main(['compare', str(tmp / 'cw.snap'), str(tmp / 'own.snap')]), 0)
            self.assertIn('CLASSES 1 allocation slack runs, 1 block fits, 1 byte count (heaps of different '
                          'runtimes, D-35); paired block sizes B-A +4 bytes', out.getvalue())
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(als.main(['compare', str(tmp / 'cw.snap'), str(tmp / 'own.snap'),
                                           '--no-classes']), 1)
            self.assertNotIn('D-35', out.getvalue())


def aim_heap(base, residue=b'', target=0, at=0x270):
    """aim_residue, the cockpit panel: the fixed dword 0x4d24c0 points to R,
    R+0x1f (unaligned) to H (0x288 data bytes, like the live 656-byte
    block); H+0x26c the aim target, `residue` written at H+`at`."""
    sizes = [8 + 0x24, 8 + 0x288]
    r_at, h_at = addresses(base, sizes)
    r = bytearray(0x24)
    r[0x1f:0x23] = u32(h_at)
    h = bytearray(0x288)
    h[0:4] = b'cock'
    h[0x26c:0x270] = u32(target)
    h[at:at + len(residue)] = residue
    return [(0x4d24c0, u32(r_at))], [(base, [(sizes[0], True, bytes(r)), (sizes[1], True, bytes(h))])]


def freed_heap(base, offsets, live=False):
    """freed_target, the map icon table: the fixed MECH cell points to the table
    T, whose entries point into F at `offsets` (deleted icons: F is a free
    block, or in use with `live`)."""
    sizes = [8 + 4 * len(offsets), 0x208]
    t_at, f_at = addresses(base, sizes)
    fixed = bytearray(12)
    fixed[2:6] = u32(t_at)
    return [(FIXED_VA, bytes(fixed))], [(base, [(sizes[0], True, u32(*[f_at + o for o in offsets])),
                                                (sizes[1], live, bytes(0x200) if live else None)])]


class StaleClasses(unittest.TestCase):
    """cockpit_sync's stack residue in H and pointers into freed blocks."""

    RESIDUE = b'\x8d\0\0\0\x87\0\0\0\x8d\0\0\0\x4d\0\xbc\x24'

    def test_aim_residue(self):
        cw = parse(snapshot(*aim_heap(0x01000000, self.RESIDUE)))
        own = parse(snapshot(*aim_heap(0x20000000), flags=OWN))
        result = als.compare(cw, own)
        self.assertTrue(result['identical'], result['first_differences'])
        self.assertEqual(result['stats']['aim_residue'], 5)
        self.assertEqual([(h['class'], h['path'], h['offset']) for h in result['classes_matched']],
                         [('aim_residue', '0x004d24c0/+0x1f', o) for o in (0x270, 0x274, 0x278, 0x27c, 0x27e)])
        self.assertFalse(als.compare(cw, own, classes=False)['identical'])
        # Not a runtime class: also between two heaps of one runtime.
        self.assertTrue(als.compare(cw, parse(snapshot(*aim_heap(0x02000000))))['identical'])
        # With a target the fields are read (0x43d7e7): differences.
        for target in (0x1234, 0):
            a = parse(snapshot(*aim_heap(0x01000000, self.RESIDUE, target=0x1234)))
            b = parse(snapshot(*aim_heap(0x20000000, target=target), flags=OWN))
            result = als.compare(a, b)
            self.assertFalse(result['identical'])
            self.assertEqual(result['stats']['aim_residue'], 0)
        # Outside +0x270..+0x27f: a difference.
        for at in (0x26e, 0x280):
            a = parse(snapshot(*aim_heap(0x01000000, b'\x01', at=at)))
            result = als.compare(a, own)
            self.assertEqual([d['offset'] for d in result['first_differences']], [at])

    def test_freed_target(self):
        cw = parse(snapshot(*freed_heap(0x01000000, [0x10, 0x74, 0xd8])))
        own = parse(snapshot(*freed_heap(0x20000000, [0x10, 0x100, 0x164]), flags=OWN))
        result = als.compare(cw, own)
        self.assertTrue(result['identical'], result['first_differences'])
        self.assertEqual(result['stats']['freed_target'], 2)
        self.assertEqual([(h['class'], h['offset']) for h in result['classes_matched']],
                         [('freed_target', 4), ('freed_target', 8)])
        self.assertFalse(als.compare(cw, own, classes=False)['identical'])
        # Heaps of one runtime: the placement is not heap history, a difference.
        same = als.compare(cw, parse(snapshot(*freed_heap(0x02000000, [0x10, 0x100, 0x164]))))
        self.assertFalse(same['identical'])
        self.assertEqual(same['stats']['freed_target'], 0)
        # A live block on one side: a difference.
        live = parse(snapshot(*freed_heap(0x20000000, [0x10, 0x100, 0x164], live=True), flags=OWN))
        result = als.compare(cw, live)
        self.assertFalse(result['identical'])
        self.assertEqual(result['stats']['freed_target'], 0)

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'cw.snap').write_bytes(snapshot(*aim_heap(0x01000000, self.RESIDUE)))
            (tmp / 'own.snap').write_bytes(snapshot(*aim_heap(0x20000000), flags=OWN))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(als.main(['compare', str(tmp / 'cw.snap'), str(tmp / 'own.snap')]), 0)
            self.assertIn('CLASSES 5 aim residue runs (cockpit_sync stack residue, H5-26b)', out.getvalue())
            self.assertIn('CLASSES 0 pointers into freed blocks on both sides (H5-26b)', out.getvalue())


class Parse(unittest.TestCase):
    def test_malformed(self):
        good = snapshot(*heap(base=0x01000000))
        for bad in (b'NOTSNAP!' + good[8:], good[:-1], good + b'\0', good[:8] + u32(3) + good[12:]):
            with self.assertRaises(als.SnapshotError):
                parse(bad)
        memory, modules = memory_map(0x01000000)
        good2 = snapshot(*heap(base=0x01000000), memory=memory, modules=modules)
        parse(good2)
        for bad in (good2[:-1], good2 + b'\0'):
            with self.assertRaises(als.SnapshotError):
                parse(bad)

    def test_summary(self):
        s = pair(base=0x01000000)
        self.assertEqual(als.summary(s)['blocks_in_use'], 4)
        self.assertEqual(als.summary(s)['fixed_unreadable'], 1)
        self.assertEqual(als.summary(s)['version'], 1)
        self.assertEqual(als.summary(s)['heap_runtime'], 'cw3220')

    def test_own_runtime_heap(self):
        # Flag 128 marks the own cw3220.dll's heap; it compares like CW3220's, with a note.
        fixed, regions = heap(base=0x01000000)
        own = parse(snapshot(fixed, regions, flags=als.FLAG_HEAP | als.FLAG_COMPLETE | als.FLAG_OWN_RUNTIME), 'own')
        self.assertEqual(own.heap_runtime, 'own')
        self.assertIn('own_runtime_heap', own.flag_names())
        self.assertEqual(als.summary(own)['heap_runtime'], 'own')
        cw = pair(base=0x05370000)
        result = als.compare(cw, own)
        self.assertTrue(result['identical'], result)
        self.assertIn('heaps of different runtimes: A cw3220, B own (same block layout; unreached blocks differ)',
                      result['notes'])
        self.assertFalse([n for n in als.compare(own, own)['notes'] if 'runtimes' in n])
        changed = parse(snapshot(*heap(base=0x02000000, text=b'tank'),
                                 flags=als.FLAG_HEAP | als.FLAG_COMPLETE | als.FLAG_OWN_RUNTIME))
        self.assertFalse(als.compare(cw, changed)['identical'])
        none = parse(snapshot(fixed, [], flags=als.FLAG_NO_HEAP | als.FLAG_COMPLETE))
        self.assertEqual(none.heap_runtime, 'none')
        self.assertFalse([n for n in als.compare(own, none)['notes'] if 'runtimes' in n])


class Cli(unittest.TestCase):
    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = als.main([str(a) for a in argv])
        return code, out.getvalue(), err.getvalue()

    def test_exit_codes_json_and_noise(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'a.snap').write_bytes(snapshot(*heap(base=0x01000000)))
            (tmp / 'b.snap').write_bytes(snapshot(*heap(base=0x03000000, environment=1)))
            (tmp / 'c.snap').write_bytes(snapshot(*heap(base=0x03000000, text=b'tank')))
            (tmp / 'd.snap').write_bytes(snapshot(*heap(base=0x04000000, clock=9)))
            (tmp / 'i.snap').write_bytes(snapshot(*heap(base=0x03000000), flags=als.FLAG_HEAP))
            (tmp / 'x.snap').write_bytes(b'garbage')
            code, out, _ = self.run_main('compare', tmp / 'a.snap', tmp / 'b.snap', '--json', tmp / 'r.json')
            self.assertEqual(code, 0, out)
            self.assertIn('pointers canonicalized', out.strip().splitlines()[-1])
            self.assertTrue(json.loads((tmp / 'r.json').read_text())['identical'])
            code, out, _ = self.run_main('compare', tmp / 'a.snap', tmp / 'b.snap', '--pairing', 'index')
            self.assertEqual(code, 1)
            code, out, _ = self.run_main('compare', tmp / 'a.snap', tmp / 'c.snap')
            self.assertEqual(code, 1)
            self.assertIn('DIFF bytes block 0x%08x/+0x0' % (FIXED_VA + 2), out)
            code, out, _ = self.run_main('noise', tmp / 'a.snap', tmp / 'd.snap', '--out', tmp / 'n.json')
            self.assertEqual(code, 0, out)
            self.assertEqual(len(json.loads((tmp / 'n.json').read_text())['entries']), 1)
            code, out, _ = self.run_main('compare', tmp / 'd.snap', tmp / 'a.snap', '--noise', tmp / 'n.json')
            self.assertEqual(code, 0, out)
            self.assertIn('1 differences matched the noise file', out)
            code, _, err = self.run_main('compare', tmp / 'a.snap', tmp / 'i.snap')
            self.assertEqual(code, 2)
            self.assertIn('incomplete', err)
            code, _, _ = self.run_main('compare', tmp / 'a.snap', tmp / 'i.snap', '--allow-incomplete')
            self.assertEqual(code, 0)
            code, _, err = self.run_main('compare', tmp / 'a.snap', tmp / 'x.snap')
            self.assertEqual(code, 2)
            code, _, err = self.run_main('compare', tmp / 'a.snap', tmp / 'b.snap', '--noise', tmp / 'x.snap')
            self.assertEqual(code, 2)
            code, out, _ = self.run_main('summary', tmp / 'a.snap')
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(out)['heap_regions'], 2)


if __name__ == '__main__':
    unittest.main()
