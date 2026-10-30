"""legacy_graph_counters.py on synthetic sources; the real tree for currency, the reference DBSIM for call targets.

The currency case needs no original data: callee_counters.h must equal what
the generator derives from hook_registry.h and the adapter headers. The
call-target case (ES2_DATA_ROOT) checks that every direct-call counter of
callee_counters.h names an instruction start of the reference DBSIM.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import legacy_graph_counters as counters  # noqa: E402

PROXY = SCRIPTS.parent / 'proxy'
# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'

# The legacy groups (docs/reference-game/hooks.md). A group
# appears here or in no case: a newer group defines its own counter function.
LEGACY_GROUPS = (
    'rng geometry timing movement math damage weapons ai node mission logic weapon_helpers track_pose '
    'vehicle_control mission_objectives effect vehicle input line_plane building vehicle_update world flyer '
    'ai_motion cockpit_target route object_query flyer_control projectile').split()

REGISTRY = """/* name, RVA, descriptor, extension, group */
ES2_HOOK(alpha, 0x1000, old_hook_descriptors[0], NULL, old)
ES2_HOOK(beta, 0x2000, old_hook_descriptors[1], &old_extension, old)
ES2_HOOK(gamma, 0x3000, new_hook_descriptors[0], NULL, fresh)
ES2_HOOK(delta, 0x4000, older_hook_descriptors[0], NULL, older)
ES2_HOOK(observed, 0x5000, older_hook_descriptors[1], NULL, older)
"""
CALLEE = """static callee_counter delta_counters[]={
    {"call_402628",ES2_COLLIDE_CALL,0x02628,0},
};
static callee_counter observed_counters[]={
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
};
static callee_counter beta_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"path_contact",ES2_COLLIDE_CONTACT,0,0},
};
static callee_counter gamma_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
};
static callee_counter *legacy_graph_counters(unsigned hook,unsigned *count) {
/* BEGIN generated: legacy graph counters (scripts/legacy_graph_counters.py) */
    GRAPH_COUNTERS(stale)
/* END generated: legacy graph counters */
    *count=0;return NULL;
}
/* BEGIN generated: legacy group definitions (scripts/legacy_graph_counters.py) */
#define stale_hook_counters legacy_graph_counters
/* END generated: legacy group definitions */
"""
FRESH = 'static callee_counter *fresh_hook_counters(unsigned hook,unsigned *count) { return NULL; }\n'


class SyntheticTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / 'hook_registry.h').write_text(REGISTRY, encoding='utf-8', newline='\n')
        (self.root / 'callee_counters.h').write_bytes(CALLEE.encode())
        (self.root / 'fresh_hooks.h').write_text(FRESH, encoding='utf-8', newline='\n')

    def tearDown(self):
        self.tmp.cleanup()

    def test_plan_and_render(self):
        rows = counters.registry(self.root / 'hook_registry.h')
        self.assertEqual([h for h, _ in rows], ['alpha', 'beta', 'gamma', 'delta', 'observed'])
        own = counters.own_counter_groups([self.root / 'fresh_hooks.h'])
        self.assertEqual(own, {'fresh'})
        legacy, listed = counters.plan(rows, own, counters.counter_arrays(CALLEE))
        # alpha has no array; gamma's group is not legacy. 'observed' would be
        # listed but is not a DEDICATED hook here, so it stays.
        self.assertEqual((legacy, listed), (['old', 'older'], ['beta', 'delta', 'observed']))
        text = counters.generate(self.root / 'callee_counters.h', self.root / 'hook_registry.h',
                                 [self.root / 'fresh_hooks.h'])
        self.assertIn('    GRAPH_COUNTERS(beta)\n    GRAPH_COUNTERS(delta)\n    GRAPH_COUNTERS(observed)\n', text)
        self.assertNotIn('stale', text)
        self.assertIn('#define old_hook_counters legacy_graph_counters\n#define older_hook_counters '
                      'legacy_graph_counters\n/* END generated: legacy group definitions */', text)
        # Everything outside the regions is untouched; the output is stable.
        self.assertEqual(text.count('\n'), CALLEE.count('\n') + 3)
        (self.root / 'callee_counters.h').write_bytes(text.encode())
        self.assertEqual(text, counters.generate(self.root / 'callee_counters.h', self.root / 'hook_registry.h',
                                                 [self.root / 'fresh_hooks.h']))

    def test_dedicated_observers_are_skipped(self):
        rows = [('ground_update', 'old'), ('beta', 'old')]
        _, listed = counters.plan(rows, set(), ['ground_update', 'beta'])
        self.assertEqual(listed, ['beta'])

    def test_refusals(self):
        with self.assertRaisesRegex(ValueError, 'markers'):
            counters.render(CALLEE.replace(counters.END_LIST, ''), [], [])
        with self.assertRaisesRegex(ValueError, 'markers'):
            counters.render(CALLEE + counters.BEGIN_DEFINES, [], [])
        (self.root / 'hook_registry.h').write_text('/* empty */\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'no ES2_HOOK'):
            counters.registry(self.root / 'hook_registry.h')
        (self.root / 'other_hooks.h').write_text(FRESH, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'defined in'):
            counters.own_counter_groups([self.root / 'fresh_hooks.h', self.root / 'other_hooks.h'])
        (self.root / 'callee_counters.h').write_bytes(CALLEE.replace('\n', '\r\n').encode())
        with self.assertRaisesRegex(ValueError, 'LF'):
            counters.generate(self.root / 'callee_counters.h', self.root / 'hook_registry.h', [])

    def test_counter_keys(self):
        keys = counters.counter_keys(CALLEE)
        self.assertEqual(keys['delta'], [('call_402628', 'CALL', 0x02628)])
        self.assertEqual(keys['beta'], [('call_47ebb8', 'CALL', 0x47ebb8), ('path_contact', 'CONTACT', 0)])
        self.assertEqual(sorted(keys), ['beta', 'delta', 'gamma', 'observed'])


class RealTreeTest(unittest.TestCase):
    def setUp(self):
        self.headers = sorted(PROXY.glob('*_hooks.h'))
        self.registry = PROXY / 'hook_registry.h'
        self.text = (PROXY / 'callee_counters.h').read_bytes().decode('utf-8')

    def test_committed_header_is_current(self):
        generated = counters.generate(PROXY / 'callee_counters.h', self.registry, self.headers)
        self.assertEqual(generated, self.text,
                         'regenerate proxy/callee_counters.h with scripts/legacy_graph_counters.py tools/reference-game/proxy')

    def test_legacy_groups_are_the_frozen_set(self):
        # A new group must define <group>_hook_counters in its own header; it
        # silently becoming "legacy" (a forgotten function) is caught here.
        rows = counters.registry(self.registry)
        legacy, listed = counters.plan(rows, counters.own_counter_groups(self.headers), counters.counter_arrays(self.text))
        self.assertEqual(legacy, list(LEGACY_GROUPS))
        self.assertEqual(len(listed), len(set(listed)))
        self.assertEqual(len(listed), 89)
        for hook in counters.DEDICATED:
            self.assertIn(hook, counters.counter_arrays(self.text))
            self.assertIn(hook, [h for h, _ in rows])
            self.assertNotIn(hook, listed)

    def test_every_listed_hook_has_a_unique_index_and_array(self):
        keys = counters.counter_keys(self.text)
        _, listed = counters.plan(counters.registry(self.registry),
                                  counters.own_counter_groups(self.headers), list(keys))
        # The entry pattern misses no counter line of the arrays.
        self.assertEqual(sum(len(items) for items in keys.values()), self.text.count('    {"'))
        for hook in listed:
            self.assertTrue(keys[hook], hook)
        for array, items in keys.items():
            names = [name for name, _, _ in items]
            self.assertEqual(len(names), len(set(names)), array)
            for name, kind, key in items:
                if kind == 'CALL':
                    va = key if key >= 0x400000 else 0x400000 + key  # older arrays hold an RVA, newer a VA
                    self.assertTrue(name == 'event_post' or name == 'call_%06x' % va, (array, name))
                elif kind == 'SLOT':
                    self.assertEqual(name, 'slot_%x' % key, array)

    @unittest.skipUnless(os.environ.get('ES2_DATA_ROOT') and OBJDUMP.exists(), 'ES2_DATA_ROOT unset')
    def test_call_targets_are_instruction_starts_of_the_reference(self):
        import probe_instruction_inventory as inventory
        root = Path(os.environ['ES2_DATA_ROOT'])
        executable = next(p for p in (root / 'VER95/DBSIM.EXE', root / 'extracted/cd/VER95/DBSIM.EXE') if p.exists())
        data = executable.read_bytes()
        _, _, reached = inventory.classify(data, OBJDUMP)
        self.assertEqual(inventory.hashlib.sha256(data).hexdigest(), inventory.MODULE_HASH)
        missing = [(array, name) for array, items in counters.counter_keys(self.text).items()
                   for name, kind, key in items
                   if kind == 'CALL' and (key if key >= 0x400000 else 0x400000 + key) not in reached]
        self.assertEqual(missing, [])


if __name__ == '__main__':
    unittest.main()
