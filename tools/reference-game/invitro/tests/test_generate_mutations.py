"""generate_mutations.py on synthetic C snippets; no game data, no Bazel."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_mutations as gm

SOURCE = r'''#include "es2/core/alpha.h"
#define W16(a,v) m->write16(m->context,(a),(uint16_t)(v))
/* Not a marker: helper for the hook below; "Original: in a string" */
static uint32_t helper(uint32_t x) { return x + 1u; }

/* Original: DBSIM.EXE 0x401000. A marker comment
 * over two lines. */
uint32_t es2_alpha(const es2_guest_memory *m, uint32_t p) {
    uint32_t a[4], s = 0;
    const char *text = "p < 10";
    if (p < 10u) s = p >> 2;
    W16(p + 4u, s);
    m->write32(m->context, p, s & 0xff00u);
    a[0] = s;
    for (unsigned i = 0; i != 3; ++i) s += helper(i);
    (void)text;
    return s == 0x401234u ? 1u : 0u;
}

/* Original: DBSIM.EXE 0x402000 */
static void beta_store(const es2_guest_memory *m, uint32_t p) {
    switch (p) {
    case 2: W16(p, 7); break;
    default: break;
    }
    p = fx_sar(p, 3);
}

/* Original: DBSIM.EXE 0x403000 */
uint64_t es2_gamma(const es2_guest_memory *m, uint32_t p) {
    beta_store(m, p);
    return (uint64_t)p;
}

uint32_t es2_delta(uint32_t x) { return x > 5u ? x : 0u; }
'''

REGISTRY = '''/* synthetic */
ES2_HOOK(alpha, 0x1000, alpha_hook_descriptors[0], NULL, first)
ES2_HOOK(gamma, 0x00003000, gamma_hook_descriptors[0], &x, second)
ES2_HOOK(other, 0x9000, gamma_hook_descriptors[1], NULL, second)
'''

ADAPTER = '''static es2_hook_descriptor alpha_hook_descriptors[]={ {0} };
/* es2_gamma in a comment does not count */
static uint32_t wrap(uint32_t *a) { return es2_alpha(0, a[0]) + es2_delta(a[1]); }
'''


def parsed():
    return gm.parse_source("core/src/alpha.c", SOURCE)


def mutants(addresses=False):
    src = parsed()
    out = []
    for f in src.functions:
        if f.address is not None:
            out += gm.mutants_for(src, f, not addresses)
    return src, out


def find(ms, op, old, new, function=None):
    return [m for m in ms if m.op == op and m.old == old and m.new == new
            and (function is None or m.function == function)]


class Lexer(unittest.TestCase):
    def test_preprocessor_comments_and_strings_are_not_code(self):
        toks = gm.lex('#define X(a) (a<1)\n/* if (x<2) */ int y = 3; // z<4\nconst char *s = "<5";\n')
        sig = [t.text for t in toks if t.kind in gm._SIGNIFICANT]
        self.assertEqual(sig, ["int", "y", "=", "3", ";", "const", "char", "*", "s", "=", '"<5"', ";"])
        self.assertEqual([t.kind for t in toks if t.kind == "pp"], ["pp"])

    def test_multi_character_operators(self):
        toks = [t.text for t in gm.lex("a<<=b>>c<=d->e!=f") if t.kind == "punct"]
        self.assertEqual(toks, ["<<=", ">>", "<=", "->", "!="])

    def test_unbalanced_source_is_refused(self):
        with self.assertRaises(ValueError):
            gm.parse_source("x.c", "int f(void) { return (1; }")


class Functions(unittest.TestCase):
    def test_markers_static_and_return_types(self):
        src = parsed()
        by_name = {f.name: f for f in src.functions}
        self.assertEqual(sorted(by_name), ["beta_store", "es2_alpha", "es2_delta", "es2_gamma", "helper"])
        self.assertIsNone(by_name["helper"].address)
        self.assertEqual(by_name["es2_alpha"].address, 0x401000)
        self.assertEqual(by_name["beta_store"].address, 0x402000)
        self.assertTrue(by_name["beta_store"].static)
        self.assertFalse(by_name["beta_store"].returns_int)
        self.assertTrue(by_name["es2_alpha"].returns_int and by_name["es2_gamma"].returns_int)
        self.assertIn("helper", by_name["es2_alpha"].calls)
        self.assertEqual(by_name["es2_alpha"].line, 8)


class Operators(unittest.TestCase):
    def setUp(self):
        self.src, self.ms = mutants()

    def test_comparisons(self):
        self.assertTrue(find(self.ms, "cmp", "<", "<=", "es2_alpha"))
        self.assertTrue(find(self.ms, "cmp", "!=", "==", "es2_alpha"))
        self.assertTrue(find(self.ms, "cmp", "==", "!=", "es2_alpha"))
        # The "<" inside the string literal and the include line are not code.
        self.assertEqual(len([m for m in self.ms if m.op == "cmp" and m.function == "es2_alpha"]), 3)

    def test_constants_skip_dimensions_and_image_addresses(self):
        self.assertTrue(find(self.ms, "const", "10u", "9u"))
        self.assertTrue(find(self.ms, "const", "10u", "11u"))
        self.assertTrue(find(self.ms, "const", "0", "1", "es2_alpha"))
        self.assertFalse([m for m in self.ms if m.old == "4" and m.function == "es2_alpha"],
                         "a[4] is a declarator dimension")
        self.assertFalse([m for m in self.ms if m.old == "0x401234u"])
        _, with_addresses = mutants(addresses=True)
        self.assertTrue(find(with_addresses, "const", "0x401234u", "0x401235u"))
        self.assertTrue(find(self.ms, "const", "7", "8", "beta_store"))
        self.assertTrue(find(self.ms, "const", "2", "1", "beta_store"), "case labels are mutated")

    def test_shift_widths(self):
        self.assertTrue(find(self.ms, "shift", "2", "1", "es2_alpha"))
        self.assertTrue(find(self.ms, "shift", "2", "3", "es2_alpha"))
        self.assertTrue(find(self.ms, "shift", "3", "4", "beta_store"), "fx_sar(p, 3) is a shift width")
        self.assertFalse(find(self.ms, "const", "3", "4", "beta_store"))

    def test_bit_flips(self):
        self.assertTrue(find(self.ms, "bitflip", "0xff00u", "0xfe00u"))
        self.assertTrue(find(self.ms, "bitflip", "0xff00u", "0x7f00u"))
        self.assertTrue(find(self.ms, "bitflip", "s", "(s)^1u", "es2_alpha"), "stored value of W16")
        self.assertTrue(find(self.ms, "bitflip", "s & 0xff00u", "(s & 0xff00u)^1u"), "stored value of ->write32")
        ret = [m for m in self.ms if m.op == "bitflip" and m.function == "es2_alpha" and m.old.startswith("s == ")]
        self.assertEqual(len(ret), 1, "returned integer")
        self.assertFalse([m for m in self.ms if m.function == "beta_store" and m.new == "(7)^1u"],
                         "a lone literal is left to const")
        self.assertTrue(find(self.ms, "bitflip", "(uint64_t)p", "((uint64_t)p)^1u", "es2_gamma"))

    def test_dropped_stores(self):
        drops = {m.old for m in self.ms if m.op == "drop"}
        self.assertIn("W16(p + 4u, s);", drops)
        self.assertIn("m->write32(m->context, p, s & 0xff00u);", drops)
        self.assertIn("a[0] = s;", drops)
        self.assertIn("W16(p, 7);", drops)
        self.assertNotIn("s = p >> 2;", drops, "plain local variable")
        self.assertNotIn("p = fx_sar(p, 3);", drops)
        self.assertFalse([m for m in self.ms if m.op == "drop" and "helper" in m.old])

    def test_unmarked_functions_are_not_mutated(self):
        self.assertFalse([m for m in self.ms if m.function == "helper"])

    def test_apply_changes_exactly_one_site(self):
        for m in self.ms:
            out = gm.apply(SOURCE, m)
            self.assertEqual(out[:m.start], SOURCE[:m.start])
            self.assertEqual(out[m.start + len(m.new):], SOURCE[m.end:])
            self.assertNotEqual(out, SOURCE)
        ids = [m.id for m in self.ms]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(ids, [m.id for m in mutants()[1]], "ids are deterministic")

    def test_ids_survive_edits_elsewhere(self):
        edited = SOURCE.replace("return x + 1u;", "return x + 2u;   /* longer */")
        src = gm.parse_source("core/src/alpha.c", edited)
        f = next(f for f in src.functions if f.name == "es2_alpha")
        self.assertEqual({m.id for m in gm.mutants_for(src, f)},
                         {m.id for m in self.ms if m.function == "es2_alpha"})

    def test_diff_line(self):
        m = find(self.ms, "const", "10u", "11u")[0]
        self.assertEqual(m.line, 11)
        self.assertIn("if (p < [10u=>11u]) s = p >> 2;", m.diff)

    def test_literal_format(self):
        self.assertEqual(gm.format_literal(0x92ee1, "0x00092ee0", "u"), "0x00092ee1u")
        self.assertEqual(gm.format_literal(0xFF, "0xFE", "U"), "0xFFU")
        self.assertEqual(gm.format_literal(9, "10", ""), "9")
        self.assertEqual(gm.format_literal(7, "010", ""), "07")
        self.assertIsNone(gm.literal_value("1.5"))
        self.assertFalse(gm._fits(0x100000000, 0xFFFFFFFF, "u"))


class Mapping(unittest.TestCase):
    def setUp(self):
        self.hooks = gm.registry_hooks(REGISTRY)
        self.mapping = gm.Mapping([parsed()], self.hooks, [ADAPTER], ["first", "second"])
        self.by_name = {f.name: f for f in parsed().functions}

    def test_registry(self):
        self.assertEqual(self.hooks[0], ("alpha", 0x401000, "alpha_hook_descriptors", "first"))
        self.assertEqual(self.hooks[1][1], 0x403000)
        self.assertEqual(gm.bzl_groups('HOOK_GROUPS = {\n    "first": 1,\n    "second": 2,\n}'), ["first", "second"])

    def f(self, name):
        return next(f for f in self.mapping.functions if f.name == name)

    def test_direct_hook(self):
        direct, groups = self.mapping.groups(self.f("es2_alpha"))
        self.assertEqual(direct, [("alpha", "first")])
        self.assertEqual(groups, ["first"])

    def test_marked_helper_takes_its_callers_groups(self):
        direct, groups = self.mapping.groups(self.f("beta_store"))
        self.assertEqual(direct, [])
        self.assertEqual(groups, ["second"])

    def test_adapter_reference_and_unknown_groups(self):
        _, groups = self.mapping.groups(self.f("helper"))
        self.assertEqual(groups, ["first"], "helper -> es2_alpha (hook alpha, adapter of first)")
        refs = gm.adapter_references([ADAPTER], self.hooks, {"es2_alpha", "es2_gamma", "es2_delta"})
        self.assertEqual(refs, {"es2_alpha": ["first"], "es2_delta": ["first"]})
        narrow = gm.Mapping([parsed()], self.hooks, [ADAPTER], ["first"])
        self.assertEqual(narrow.groups(next(f for f in narrow.functions if f.name == "es2_gamma"))[1], [])


class Selection(unittest.TestCase):
    def args(self, *extra):
        return gm.parse_args(["--limit", "0", *extra])

    def plan(self, state, text=SOURCE, *extra):
        src = gm.parse_source("core/src/alpha.c", text)
        mapping = gm.Mapping([src], gm.registry_hooks(REGISTRY), [ADAPTER], ["first", "second"])
        return gm.plan(self.args(*extra), [src], mapping, state)

    def test_incremental_by_content_hash(self):
        state = {"version": 1, "files": {}}
        chosen, catalogue, skipped, total = self.plan(state)
        self.assertEqual(len(chosen), total)
        self.assertEqual(len(catalogue), 3)
        self.assertTrue(all(m.groups for m in chosen))
        for m in chosen:
            state["files"][m.file]["results"][m.id] = {"status": "killed"}
        chosen, _, skipped, total = self.plan(state)
        self.assertEqual((len(chosen), total, skipped), (0, 0, ["core/src/alpha.c"]))
        self.assertEqual(len(self.plan(state, SOURCE, "--all")[0]), len(mutants()[1]))
        state2 = {"version": 1, "files": {}}
        for m in mutants()[1]:
            state2["files"].setdefault(m.file, {"sha256": gm.sha256(SOURCE), "results": {}})["results"][m.id] = {}
        changed = SOURCE.replace("p < 10u", "p < 12u")
        self.assertEqual(len(self.plan(state2, changed)[0]), len(mutants()[1]), "a changed file is rerun")
        self.assertEqual(state2["files"]["core/src/alpha.c"]["sha256"], gm.sha256(changed))

    def test_extra_bazel_flags(self):
        args = self.args("--bazel-flag=--local_resources=cpu=4", "--bazel-flag=--jobs=2")
        self.assertEqual(args.bazel_flag, ["--local_resources=cpu=4", "--jobs=2"])
        ws = gm.Workspace(".", "ws", "bazel", "out", None, ".", args.bazel_flag)
        self.assertEqual(ws.flags[-2:], ["--local_resources=cpu=4", "--jobs=2"])
        self.assertEqual(self.args().bazel_flag, [])

    def test_filters(self):
        state = {"version": 1, "files": {}}
        only_second = self.plan(state, SOURCE, "--group", "second")[0]
        self.assertTrue(only_second)
        self.assertEqual({m.function for m in only_second}, {"beta_store", "es2_gamma"})
        self.assertTrue(all(m.groups == ["second"] for m in only_second))
        by_hook = self.plan({"version": 1, "files": {}}, SOURCE, "--functions", "alpha")[0]
        self.assertEqual({m.function for m in by_hook}, {"es2_alpha"})
        ops = self.plan({"version": 1, "files": {}}, SOURCE, "--ops", "cmp", "drop")[0]
        self.assertEqual({m.op for m in ops}, {"cmp", "drop"})
        wanted = [m.id for m in mutants()[1]][:2]
        by_id = self.plan({"version": 1, "files": {}}, SOURCE, "--ids", *wanted)[0]
        self.assertEqual(sorted(m.id for m in by_id), sorted(wanted))
        with tempfile.TemporaryDirectory() as folder:
            listing = Path(folder) / "ids.txt"
            listing.write_text("# survivors\n" + wanted[0] + " convoy.c:1 x\n\n", encoding="utf-8")
            from_file = self.plan({"version": 1, "files": {}}, SOURCE, "--ids", "@" + str(listing))[0]
            self.assertEqual([m.id for m in from_file], [wanted[0]])

    def test_unmarked_adapter_functions_on_request(self):
        chosen = self.plan({"version": 1, "files": {}}, SOURCE, "--unmarked")[0]
        delta = [m for m in chosen if m.function == "es2_delta"]
        self.assertTrue(delta)
        self.assertTrue(all(m.groups == ["first"] and m.hooks == [] and m.address is None for m in delta))
        self.assertFalse([m for m in chosen if m.function == "helper"], "static helpers stay out")
        self.assertIsNone(gm.result_record(delta[0], "killed")["address"])

    def test_sample_is_bounded_deterministic_and_spread(self):
        ms = mutants()[1]
        a = gm.sample(ms, 2, 0, "s")
        self.assertEqual(len(a), 5, "2 + 2 + the single es2_gamma mutant")
        self.assertEqual([m.id for m in a], [m.id for m in gm.sample(ms, 2, 0, "s")])
        b = gm.sample(ms, 0, 3, "s")
        self.assertEqual(len({m.function for m in b}), 3, "round-robin over functions")

    def test_state_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sub" / "state.json"
            self.assertEqual(gm.load_state(path), {"version": 1, "files": {}})
            gm.save_state(path, {"version": 1, "files": {"x": {"sha256": "1", "results": {}}}})
            self.assertEqual(gm.load_state(path)["files"]["x"]["sha256"], "1")
            self.assertNotIn(b"\r\n", path.read_bytes())


class WorkspaceRestore(unittest.TestCase):
    def test_restore_and_deactivate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "core" / "src").mkdir(parents=True)
            original = b"int x;\r\nint y;\n"
            (root / "core" / "src" / "a.c").write_bytes(original)
            (root / "core" / "BUILD.bazel").write_bytes(b"")
            ws = gm.Workspace(root, root, "bazel", root / "ob", None, root)
            ws.write("core/src/a.c", "mutant\n")
            self.assertEqual((root / "core" / "src" / "a.c").read_bytes(), b"mutant\n")
            ws.write("core/src/a.c", "second\n")
            ws.restore()
            self.assertEqual((root / "core" / "src" / "a.c").read_bytes(), original)
            ws.deactivate()
            self.assertFalse((root / "core" / "BUILD.bazel").exists())
            self.assertTrue((root / "core" / "BUILD.bazel.inactive").exists())


if __name__ == "__main__":
    unittest.main()
