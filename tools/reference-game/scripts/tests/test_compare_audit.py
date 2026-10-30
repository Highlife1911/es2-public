"""compare_audit.py on synthetic adapter snippets; no game data required."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_audit as ca

REGISTRY = """/* synthetic registry */
ES2_HOOK(legacy, 0x00001000, legacy_descriptors[0], NULL, legacy)
ES2_HOOK(direct_ok, 0x1010, d[0], &alpha_direct_ok_extension, alpha)
ES2_HOOK(direct_no, 0x1020, d[1], &alpha_direct_no_extension, alpha)
ES2_HOOK(split, 0x1030, d[2], &alpha_split_extension, alpha)
ES2_HOOK(short_init, 0x1040, d[3], &alpha_short_init_extension, alpha)
ES2_HOOK(macro_fixed, 0x1050, d[4], &beta_macro_fixed_extension, beta)
ES2_HOOK(macro_arg_yes, 0x1060, d[5], &beta_macro_arg_yes_extension, beta)
ES2_HOOK(macro_arg_no, 0x1070, d[6], &beta_macro_arg_no_extension, beta)
ES2_HOOK(nested, 0x1080, d[7], &gamma_nested_extension, gamma)
ES2_HOOK(redefined, 0x1090, d[8], &gamma_redefined_extension, gamma)
"""

ALPHA = """/* An adapter. A commented-out definition must not count:
static const es2_hook_extension alpha_direct_ok_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,f,TRUE};
 */
// static const es2_hook_extension alpha_direct_no_extension={0,0,NULL,NULL,NULL,0,f,FALSE};
static const es2_hook_extension alpha_direct_ok_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,1,f,FALSE};
static const es2_hook_extension alpha_direct_no_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,f,TRUE};
static const es2_hook_extension alpha_split_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    alpha_split,TRUE};
static const es2_hook_extension alpha_short_init_extension={ES2_ABI_CDECL,ES2_RESULT_EAX,NULL,NULL,r,1};
static const es2_hook_extension *function_hook_extension(unsigned i);
static const char *text = "/* not a comment";
"""

BETA = """#define BETA_EXT(name) \\
    static const es2_hook_extension beta_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,beta_##name,TRUE}; \\
    static uint32_t __cdecl beta_##name##32(const uint32_t *a,void *u) {return 0;}
BETA_EXT(macro_fixed)
#define BETA_FLAG(name,refuse) static const es2_hook_extension beta_##name##_extension= \\
    {ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,beta_##name,refuse};
BETA_FLAG(macro_arg_yes,FALSE) BETA_FLAG(macro_arg_no, TRUE)
#undef BETA_FLAG
"""

GAMMA = """#define GAMMA_EXT(name) static const es2_hook_extension gamma_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,gamma_##name,TRUE};
#define GAMMA_LEGACY(name) static uint32_t gamma_##name##32(void) {return 0;}
#define GAMMA_BOTH(name) GAMMA_EXT(name) GAMMA_LEGACY(name)
GAMMA_BOTH(nested)
#undef GAMMA_EXT
#define GAMMA_EXT(name,flag) static const es2_hook_extension gamma_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,gamma_##name,flag};
GAMMA_EXT(redefined,FALSE)
"""

EXPECTED = {'legacy': 'yes', 'direct_ok': 'yes', 'direct_no': 'no', 'split': 'no', 'short_init': 'yes',
            'macro_fixed': 'no', 'macro_arg_yes': 'yes', 'macro_arg_no': 'no', 'nested': 'no', 'redefined': 'yes'}


class Fixture:
    def __init__(self, root, registry=REGISTRY, extra=None):
        self.root = Path(root)
        self.registry = self.root / 'hook_registry.h'
        self.registry.write_text(registry)
        files = {'alpha_hooks.h': ALPHA, 'beta_hooks.h': BETA, 'gamma_hooks.h': GAMMA}
        files.update(extra or {})
        for name, text in files.items():
            (self.root / name).write_text(text)


def run_main(args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        status = ca.main(args)
    return status, out.getvalue()


class ExtractionTest(unittest.TestCase):
    def test_flags_per_hook(self):
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d)
            rows = ca.audit(f.registry, d)
        self.assertEqual({r['hook']: r['compare'] for r in rows}, EXPECTED)
        self.assertEqual([r['hook'] for r in rows][:3], ['legacy', 'direct_ok', 'direct_no'])
        by = {r['hook']: r for r in rows}
        self.assertEqual(by['legacy']['va'], 0x401000)
        self.assertIsNone(by['legacy']['extension'])
        self.assertEqual(by['split']['source'], 'alpha_hooks.h:7 compare_unsupported=TRUE')
        self.assertEqual(by['short_init']['source'], 'alpha_hooks.h:9 compare_unsupported=0')
        self.assertTrue(by['macro_arg_no']['source'].startswith('beta_hooks.h:7 '))

    def test_unknown_flag_and_missing_definition(self):
        extra = {'delta_hooks.h': '#define D_EXT(name,f) static const es2_hook_extension delta_##name##_extension='
                                  '{0,0,NULL,NULL,NULL,0,g,f};\nD_EXT(odd,some_macro)\n'}
        registry = REGISTRY + 'ES2_HOOK(odd, 0x10a0, d[9], &delta_odd_extension, delta)\n' \
                              'ES2_HOOK(lost, 0x10b0, d[10], &delta_lost_extension, delta)\n'
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d, registry, extra)
            rows = {r['hook']: r for r in ca.audit(f.registry, d)}
            status, _ = run_main(['--registry', str(f.registry), '--proxy', d])
        self.assertEqual(rows['odd']['compare'], 'unknown')
        self.assertIn('some_macro', rows['odd']['source'])
        self.assertEqual(rows['lost']['compare'], 'unknown')
        self.assertEqual(status, 1)

    def test_duplicate_definition_and_bad_registry_line(self):
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d, extra={'zeta_hooks.h': ALPHA})
            with self.assertRaises(ca.AuditError):
                ca.audit(f.registry, d)
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d, REGISTRY + 'ES2_HOOK(broken 0x1, x)\n')
            with self.assertRaises(ca.AuditError):
                ca.load_registry(f.registry)
            self.assertEqual(run_main(['--registry', str(f.registry), '--proxy', d])[0], 2)

    def test_expander(self):
        macros = {'CAT': (['a', 'b'], 'a##_##b'), 'WRAP': (['x'], '[CAT(x, x)]')}
        self.assertEqual(ca.expand('WRAP(p) CAT (q,r) CATX(s)', macros), '[p_p] q_r CATX(s)')
        self.assertEqual(ca.split_args('f(a, (b, c), {d, e}) tail', 1), (['a', '(b, c)', '{d, e}'], 20))
        self.assertEqual(ca.strip_comments('a /* x\ny */ b // c\n"/*" d'), 'a \n b \n"/*" d')


class OutputTest(unittest.TestCase):
    def test_markdown_and_check(self):
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d)
            status, text = run_main(['--registry', str(f.registry), '--proxy', d])
            self.assertEqual(status, 0)
            self.assertIn('| `split` | 401030 | alpha | no |', text)
            self.assertIn('10 hooks: 5 accept compare, 5 refuse it, 0 unknown.', text)
            doc = Path(d) / 'audit.md'
            rows = '\n'.join(f'| `{h}` | x | {v} | note |' for h, v in EXPECTED.items())
            doc.write_text('# Audit\n\nIntro.\n\n| Hook | VA | Compare | Notes |\n| --- | --- | --- | --- |\n' + rows +
                           '\n\nText after.\n\n| Hook | Compare |\n| --- | --- |\n| `ignored` | no |\n')
            self.assertEqual(run_main(['--registry', str(f.registry), '--proxy', d, '--check', str(doc)]),
                             (0, '10 hooks checked, 0 problems\n'))
            doc.write_text(doc.read_text().replace('| `split` | x | no |', '| `split` | x | yes |')
                           .replace('| `legacy` | x | yes | note |\n', ''))
            status, text = run_main(['--registry', str(f.registry), '--proxy', d, '--check', str(doc)])
            self.assertEqual(status, 1)
            self.assertIn('legacy: missing in document', text)
            self.assertIn('split: document says yes, adapters say no', text)

    def test_hooks_doc_table_selection(self):
        with tempfile.TemporaryDirectory() as d:
            f = Fixture(d)
            doc = Path(d) / 'hooks.md'
            rows = '\n'.join(f'| `{h}` | 1 | m | {v} | 5 | 1 | 5 |' for h, v in EXPECTED.items())
            doc.write_text('| Hook | Compare |\n| --- | --- |\n| `legacy` | no |\n\n'
                           '| Hook | VA | Group | Compare | c1_01-success calls | Suites with calls | Calls, all suites |\n'
                           '| --- | --- | --- | --- | ---: | ---: | ---: |\n'
                           + rows + '\n')
            self.assertEqual(ca.doc_compare_column(doc)['legacy'], 'no')
            self.assertEqual(ca.doc_compare_column(doc, 'c1_01-success calls')['legacy'], 'yes')
            self.assertEqual(run_main(['--registry', str(f.registry), '--proxy', d, '--hooks-doc', str(doc)])[0], 0)


if __name__ == '__main__':
    unittest.main()
