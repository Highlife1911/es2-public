"""tools/web/seam_scan.py on the fixtures under tools/web/testdata.

Every fixture line that must be reported carries `EXPECT: kind[,kind...]` in a comment; a
fixture line without a marker must report nothing. The scanner runs in process (no child
processes), once per fixture, and its command line is exercised through main().

usage: test_seam_scan.py <seam_scan.py> <seam_allowlist.txt> <testdata/kinds/plain.c>
"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile

SCAN = Path(sys.argv[1]).resolve()
ALLOWLIST = Path(sys.argv[2]).resolve()
DATA = Path(sys.argv[3]).resolve().parent.parent

spec = importlib.util.spec_from_file_location("seam_scan", SCAN)
seam_scan = importlib.util.module_from_spec(spec)
sys.modules["seam_scan"] = seam_scan
spec.loader.exec_module(seam_scan)

MARKER = re.compile(r"EXPECT:\s*([\w,\-]+)")
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def expected_hits(path):
    found = set()
    for number, line in enumerate(path.read_bytes().decode().split("\n"), 1):
        m = MARKER.search(line)
        if m:
            found.update((number, kind) for kind in m.group(1).split(","))
    return found


def scan(paths, allowlist=None, **kw):
    return seam_scan.scan([str(p) for p in paths], DATA, allowlist or seam_scan.Allowlist(None), **kw)


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = seam_scan.main([str(a) for a in argv])
    return code, out.getvalue()


def write_allowlist(directory, text):
    path = Path(directory) / "allowlist.txt"
    path.write_bytes(text.encode())
    return path


fixtures = sorted(DATA.glob("kinds/*")) + sorted(DATA.glob("shell/*"))
check(len(fixtures) >= 18, fixtures)

# 1. Exactly the marked (line, kind) pairs per fixture, and every kind is exercised somewhere.
covered = set()
for fixture in fixtures:
    want = expected_hits(fixture)
    result = scan([fixture])
    got = {(h["line"], h["kind"]) for h in result["hits"]}
    check(got == want, "%s: missing %s, unexpected %s" % (fixture.name, sorted(want - got), sorted(got - want)))
    covered.update(kind for _, kind in want)
check(covered == set(seam_scan.KINDS), "kinds without a fixture: %s" % sorted(set(seam_scan.KINDS) - covered))

# 2. Each kind names its replacement; the plain fixture reports nothing and counts its lines.
for kind in seam_scan.KINDS:
    check(seam_scan.REPLACEMENT.get(kind), kind)
for fixture in fixtures:
    for hit in scan([fixture])["hits"]:
        check(hit["replacement"] and hit["file"].startswith(("kinds/", "shell/")), hit)
plain = scan([DATA / "kinds/plain.c"])
check(plain["hits"] == [], plain["hits"])
check(plain["lines"] == (DATA / "kinds/plain.c").read_bytes().count(b"\n"), plain["lines"])

# 3. Details: asm inside a naked function, Win32 groups, informational kinds, float-int scope.
naked = {(h["line"], h["kind"]): h for h in scan([DATA / "kinds/naked.c"])["hits"]}
check(naked[(3, "inline-asm")]["in_naked"] and naked[(2, "naked")]["detail"] == "shim", naked)
check(naked[(5, "naked")]["detail"] == "shim2", naked)
groups = {h["detail"]: h["group"] for h in scan([DATA / "kinds/win32.c"])["hits"] if h["kind"] == "win32-call"}
check(groups == {"GetTickCount": "os", "CreateFileA": "os", "AddVectoredExceptionHandler": "guard",
                 "CreateWindowExA": "platform", "InterlockedExchange": "os"}, groups)
informational = {h["kind"] for fixture in fixtures for h in scan([fixture])["hits"] if h["informational"]}
check(informational == seam_scan.INFORMATIONAL, informational)
check(scan([DATA / "kinds/float_int_outside.c"])["hits"] == [])
check(scan([DATA / "kinds/float_int_outside.c"], float_int_scope=("kinds/",))["hits"] != [])
check(not scan([DATA / "shell/float_int.c"], float_int_scope=())["hits"])

# 4. CRLF sources report the same lines; the scanned directories skip tests and testdata.
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for name in ("inline_asm.c", "bitfield.c"):
        original = (DATA / "kinds" / name).read_bytes()
        (tmp / name).write_bytes(original.replace(b"\n", b"\r\n"))
        got = {(h["line"], h["kind"]) for h in seam_scan.scan([str(tmp / name)], str(tmp), seam_scan.Allowlist(None))["hits"]}
        check(got == expected_hits(DATA / "kinds" / name), name)
    (tmp / "src").mkdir()
    (tmp / "src" / "tests").mkdir()
    (tmp / "src" / "testdata").mkdir()
    (tmp / "src" / "a.c").write_bytes(b"int a;\n")
    (tmp / "src" / "tests" / "t.c").write_bytes(b'void f(void) { __asm__("nop"); }\n')
    (tmp / "src" / "testdata" / "d.c").write_bytes(b'void g(void) { __asm__("nop"); }\n')
    check(seam_scan.scan([str(tmp / "src")], str(tmp), seam_scan.Allowlist(None))["hits"] == [])
    check(len(seam_scan.scan([str(tmp / "src")], str(tmp), seam_scan.Allowlist(None), include_tests=True)["hits"]) == 2)

# 5. Allowlist semantics: last match wins, '!' removes, kinds and groups restrict.
with tempfile.TemporaryDirectory() as tmp:
    path = write_allowlist(tmp, "# comment\n"
                                "a/**  # all of a\n"
                                "!a/b/*.c\n"
                                "a/b/x.c esp,idiv\n"
                                "win32-only/*.c win32-call  # any group\n"
                                "guard-only/*.c guard\n")
    allow = seam_scan.Allowlist(str(path))
    check(allow.lookup("a/c.c", "esp") == "all of a")
    check(allow.lookup("a/b/d.c", "esp") is None)
    check(allow.lookup("a/b/x.c", "esp") and allow.lookup("a/b/x.c", "idiv"))
    check(allow.lookup("a/b/x.c", "naked") is None)
    check(allow.lookup("a/b/sub/e.h", "naked") == "all of a")
    check(allow.lookup("win32-only/f.c", "win32-call", "os") and allow.lookup("win32-only/f.c", "esp") is None)
    check(allow.lookup("guard-only/f.c", "win32-call", "guard") and allow.lookup("guard-only/f.c", "win32-call", "os") is None)
    check(allow.lookup("other/f.c", "esp") is None)
    for bad in ("x.c nonsense-kind\n", "x.c esp extra\n"):
        try:
            seam_scan.Allowlist(str(write_allowlist(tmp, bad)))
        except SystemExit:
            continue
        check(False, "accepted " + bad)

# 6. The shipped allowlist reads and names the x86-only sources, not the portable entries.
shipped = seam_scan.Allowlist(str(ALLOWLIST))
for rel in ("hook/machine.S", "rtl/src/cw3220.c", "tools/reference-game/proxy/rtl_life.h",
            "tools/reference-game/proxy/media_proxy.c", "sim/src/cpu_fault.S", "sim/src/host_fault.c",
            "platform/src/os_win32.c"):
    check(shipped.lookup(rel, "esp", "os") or shipped.lookup(rel, "win32-call", "os"), rel)
for rel in ("tools/reference-game/proxy/weapons_hooks.h", "tools/reference-game/proxy/hook_registry.h",
            "sim/src/fill.c", "sim/src/main.c", "core/src/rng.c",
            "shell/src/host.c"):
    check(shipped.lookup(rel, "esp", "os") is None and shipped.lookup(rel, "win32-call", "os") is None, rel)

# 7. --forbid with and without the allowlist.
kinds = DATA / "kinds"
base = [kinds, "--root", DATA]
code, out = run(base + ["--no-allowlist", "--forbid", "--quiet"])
check(code == 1 and "forbidden hit(s) outside the allowlist" in out, out)
code, out = run(base + ["--no-allowlist", "--quiet"])
check(code == 0, "without --forbid the exit code stays 0")
with tempfile.TemporaryDirectory() as tmp:
    everything = write_allowlist(tmp, "kinds/**  # fixtures\n")
    code, out = run(base + ["--allowlist", everything, "--forbid"])
    check(code == 0 and "no forbidden hit outside the allowlist" in out, out)
    partial = write_allowlist(tmp, "kinds/**\n!kinds/guest_host_cast.c\nkinds/guest_host_cast.c guest-fn-cast\n")
    code, out = run(base + ["--allowlist", partial, "--forbid", "--json", "-"])
    data = json.loads(out)
    outside = {h["file"] for h in data["hits"] if not h["informational"] and not h["allowed"]}
    check(code == 1 and outside == {"kinds/guest_host_cast.c"}, (code, outside))
    allowed = [h for h in data["hits"] if h["file"] == "kinds/win32.c" and h["kind"] == "win32-call"]
    check(allowed and all(h["allowed"] == "fixtures" or h["allowed"] == "kinds/**" for h in allowed), allowed)
    check(data["forbidden_kinds"] == [k for k in seam_scan.KINDS if k not in seam_scan.INFORMATIONAL])
    # An allowlist for one kind does not cover the others in the same file.
    one_kind = write_allowlist(tmp, "kinds/win32.c win32-call\n")
    code, out = run([kinds / "win32.c", "--root", DATA, "--allowlist", one_kind, "--forbid", "--json", "-"])
    check(code == 0, out)
    code, out = run([kinds / "fs_esp.c", "--root", DATA, "--allowlist", one_kind, "--forbid", "--quiet"])
    check(code == 1, out)

# Informational kinds alone never fail --forbid.
code, out = run([kinds / "crt_io.c", DATA / "shell/float_int.c", kinds / "plain.c", "--root", DATA,
                 "--no-allowlist", "--forbid"])
check(code == 0, out)

# 8. Output formats: hit lines name file, line, kind and replacement; JSON has the same hits; --sources works.
code, out = run([kinds / "naked.c", "--root", DATA, "--no-allowlist"])
check("kinds/naked.c:2: naked: shim" in out and "-> C entry taking es2_machine" in out, out)
code, out = run([kinds / "naked.c", "--root", DATA, "--no-allowlist", "--summary"])
check(out.startswith("1 files,") and "naked" in out and "kinds/naked.c:2" not in out, out)
with tempfile.TemporaryDirectory() as tmp:
    listing = Path(tmp) / "sources.txt"
    listing.write_bytes(("# sources of one link\n\n%s  # naked shims\n%s\n"
                         % ((kinds / "naked.c").as_posix(), (kinds / "wchar.c").as_posix())).encode())
    code, out = run(["--sources", listing, "--root", DATA, "--no-allowlist", "--json", "-"])
    data = json.loads(out)
    check(data["files"] == ["kinds/naked.c", "kinds/wchar.c"], data["files"])
    check({h["file"] for h in data["hits"]} == {"kinds/naked.c", "kinds/wchar.c"})
    check(all(set(h) >= {"file", "line", "kind", "detail", "replacement", "allowed"} for h in data["hits"]))
    out_json = Path(tmp) / "out.json"
    code, out = run([kinds / "wchar.c", "--root", DATA, "--no-allowlist", "--json", out_json, "--quiet"])
    check(json.loads(out_json.read_bytes().decode())["hits"], out)
    check(b"\r" not in out_json.read_bytes())
try:
    run([DATA / "kinds/missing.c"])
    check(False, "a missing source was accepted")
except SystemExit:
    pass

print("seam_scan: %d checks passed" % checks)
