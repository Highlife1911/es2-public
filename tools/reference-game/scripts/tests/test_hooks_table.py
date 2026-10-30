"""hooks_table.py on synthetic inputs, then the committed hooks.md against the real registry and counts."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hooks_table as ht

ROOT = Path(__file__).resolve().parents[4]

REGISTRY = """/* name, RVA, descriptor, extension, group. */
ES2_HOOK(alpha, 0x00092ee0, rng_hook_descriptors[0], NULL, rng)
ES2_HOOK(euler_matrix, 0x7ebb8, math_hook_descriptors[0], &math_euler_matrix_extension, math)
ES2_HOOK(gamma, 0x1a2b3c, gamma_hook_descriptors[0], NULL, cold_paths)
ES2_HOOK(delta, 0x000100, gamma_hook_descriptors[1], NULL, cold_paths)
"""

DOC = """# Hooks

intro
""" + ht.BEGIN + """
stale
""" + ht.END + """

Diagnostic compare selections:

```powershell
-Hooks "euler_matrix=compare"
-Hooks "delta=compare"
```
"""


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = ht.main(argv)
    return code, out.getvalue()


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        (self.dir / 'hook_registry.h').write_text(REGISTRY)
        (self.dir / 'hooks.md').write_bytes(DOC.encode())
        self.counters = self.dir / 'hook-counters.json'
        self.counters.write_text(json.dumps(dict(source='x', suites={
            'c1_01-success': dict(common_ticks=5, compared_tick=dict(hk_alpha_calls=7, hk_alpha_bypassed=0, hk_gamma_calls=0,
                                                                     hk_euler_matrix_calls=1234567)),
            'other': dict(common_ticks=5, compared_tick=dict(hk_alpha_calls=3, hk_gamma_calls=2, hk_euler_matrix_calls=0))})))
        self.flat = self.dir / 'flat.json'
        self.flat.write_text(json.dumps(dict(source='y', suites={'third': dict(counters=dict(hk_gamma_calls=5, hk_alpha_calls=0))})))
        self.counts = self.dir / 'counts.json'

    def render(self, mode):
        return run(['render', '--hooks-md', str(self.dir / 'hooks.md'), '--registry', str(self.dir / 'hook_registry.h'),
                    '--counts', str(self.counts), mode])

    def test_counts_and_render(self):
        code, _ = run(['counts', '--output', str(self.counts), '--compared', str(self.counters), '--flat', str(self.flat),
                       '--source', 'synthetic run'])
        self.assertEqual(code, 0)
        c = json.loads(self.counts.read_text())
        self.assertEqual(c['suite_count'], 3)
        self.assertEqual(c['hooks']['alpha'], [7, 2, 10])
        self.assertEqual(c['hooks']['gamma'], [0, 2, 7])
        self.assertEqual(c['hooks']['euler_matrix'], [1234567, 1, 1234567])
        self.assertNotIn('delta', c['hooks'])
        self.assertEqual(self.render('--check')[0], 1)          # the stale table is refused
        self.assertEqual(self.render('--write')[0], 0)
        self.assertEqual(self.render('--check')[0], 0)
        text = (self.dir / 'hooks.md').read_bytes().decode('utf-8')
        self.assertIn('| `alpha` | 492ee0 | rng | yes | 7 | 2 | 10 |', text)      # before euler_matrix: always yes
        self.assertIn('| `euler_matrix` | 47ebb8 | math | yes | 1,234,567 | 1 | 1,234,567 |', text)
        self.assertIn('| `gamma` | 5a2b3c | cold_paths | no | 0 | 2 | 7 |', text)  # no compare line
        self.assertIn('| `delta` | 400100 | cold_paths | yes | – | – | – |', text)  # no counter in the source
        self.assertIn('Live counts: synthetic run; 3 suites.', text)
        self.assertTrue(text.endswith('-Hooks "delta=compare"\n```\n'))
        self.assertNotIn('\r', text)

    def test_refusals(self):
        run(['counts', '--output', str(self.counts), '--compared', str(self.counters)])
        doc = self.dir / 'hooks.md'
        doc.write_bytes(DOC.replace(ht.END, '').encode())
        self.assertEqual(self.render('--check')[0], 2)          # a missing marker
        doc.write_bytes(DOC.replace('-Hooks "delta=compare"', '-Hooks "omega=compare"').encode())
        self.assertEqual(self.render('--check')[0], 2)          # a compare line for an unknown hook
        (self.dir / 'hook_registry.h').write_text(REGISTRY + 'ES2_HOOK(alpha, 0x5, x, NULL, rng)\n')
        self.assertEqual(self.render('--check')[0], 2)          # a duplicate name


class Committed(unittest.TestCase):
    def test_hooks_md_is_current(self):
        code, out = run(['render', '--check'])
        self.assertEqual(code, 0, out)


if __name__ == '__main__':
    unittest.main()
