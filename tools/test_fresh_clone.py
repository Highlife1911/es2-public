"""tools/check-fresh-clone.ps1 with a fake bazel and a fake tools/setup-dependencies.ps1.

Everything happens below one temporary directory: a throwaway git repository that plays the main repository
(with a stand-in setup-dependencies.ps1 that only prints verdict lines), a dependency directory with an empty
bazel\\output, a fake `bazel.cmd` (a Python script that logs its working directory and arguments, answers
--version, and writes a build event file and test logs for the canned results of FAKE_BAZEL_TESTS). Real git
clones the throwaway repository. No real Bazel is started and the real checkout, the real dependency directory and
es2-data are never touched.

The real tools/setup-dependencies.ps1, setup-platform-dependencies.ps1 and setup-web-dependencies.ps1 run with -VerifyOnly against empty and
half-filled dependency directories below the temporary directory (nothing may be created or downloaded).

usage: test_fresh_clone.py <check-fresh-clone.ps1> <setup-dependencies.ps1> <setup-platform-dependencies.ps1> <python_tree_digest.py>
       <setup-web-dependencies.ps1>
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SCRIPT = Path(sys.argv[1]).resolve()
SETUP_FILES = [Path(a).resolve() for a in sys.argv[2:6]]
PWSH = Path(os.environ.get("ES2_PWSH") or shutil.which("pwsh") or "")
if not PWSH.name or not PWSH.exists():
    PWSH = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
GIT = shutil.which("git") or "C:/Program Files/Git/cmd/git.exe"
NO_WINDOW = 0x08000000

checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def git(repo, *args):
    r = subprocess.run([GIT, "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", creationflags=NO_WINDOW, timeout=60)
    check(r.returncode == 0, (args, r.stdout, r.stderr))
    return r.stdout.strip()


def write_lf(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


FAKE_SETUP = r"""param([string]$Destination, [switch]$VerifyOnly)
if (!$VerifyOnly) { throw 'the check must only verify' }
Write-Output '[OK] Bazel - 9.3.0'
if ($env:FAKE_VERIFY_FAIL) {
    Write-Output '[MISSING] llvm-mingw - bin/clang.exe absent'
    Write-Output '[WARN] 7-Zip - absent'
    Write-Output '1 item(s) missing or mismatched.'
    exit 1
}
Write-Output '[OK] Python 3.12.14 - tree digest matches'
exit 0
"""

FAKE_BAZEL_PY = r"""import json, os, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ["FAKE_BAZEL_LOG"], "a", encoding="utf-8") as log:
    log.write(os.getcwd() + " | " + " ".join(args) + "\n")
if args == ["--version"]:
    print("bazel " + os.environ.get("FAKE_BAZEL_VERSION", "9.3.0"))
    sys.exit(0)
if "test" in args:
    print("INFO: Analyzed targets")
    if os.environ.get("FAKE_BAZEL_ERROR"):
        print("ERROR: no such package 'foo'")
    bep = [a.split("=", 1)[1] for a in args if a.startswith("--build_event_json_file=")]
    tests = json.loads(os.environ.get("FAKE_BAZEL_TESTS", "[]"))
    if bep and tests:
        logs = Path(os.environ["FAKE_BAZEL_LOGS"])
        with open(bep[0], "w", encoding="utf-8") as out:
            out.write(json.dumps({"id": {"started": {}}, "started": {}}) + "\n")
            for n, (label, status, text) in enumerate(tests):
                path = logs / ("t%d" % n) / "test.log"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
                key = "failed" if status != "PASSED" else "passed"
                event = {"id": {"testSummary": {"label": label}},
                         "testSummary": {"overallStatus": status, key: [{"name": "test.log", "uri": path.as_uri()}]}}
                out.write(json.dumps(event, separators=(",", ":")) + "\n")
                out.write(json.dumps({"id": {"progress": {}}, "children": [{"testSummary": {"label": label}}]}) + "\n")
    sys.exit(int(os.environ.get("FAKE_BAZEL_CODE", "0")))
sys.exit(0)
"""

TESTS_PASS = [
    ["//core:a_test", "PASSED", "PASS: core\n"],
    ["//formats:real_test", "PASSED", "SKIP: ES2_DATA_ROOT is unset\n"],
    ["//tools:py_test", "PASSED", "Ran 3 tests\n\nOK (skipped=2)\n"],
]
TESTS_FAIL = TESTS_PASS + [["//shell:broken_test", "FAILED", "setup\nTraceback\nFileNotFoundError: dbsim.exe\n"],
                           ["//shell:slow_test", "TIMEOUT", "still running\n"]]


class Fixture:
    def __init__(self, tmp):
        self.tmp = tmp
        self.src = tmp / "src"
        self.deps = tmp / "deps"
        self.fake = tmp / "fake"
        self.work = tmp / "work"
        self.base = self.deps / "bazel/output/fresh-clone"
        (self.deps / "bazel/output").mkdir(parents=True)
        self.fake.mkdir()
        self.src.mkdir()
        git(self.src, "init", "-q", "-b", "main")
        write_lf(self.src / ".bazelversion", "9.3.0\n")
        write_lf(self.src / "tools/setup-dependencies.ps1", FAKE_SETUP)
        write_lf(self.src / "MODULE.bazel", '# comment D:/ignored/comment\nllvm = "X:/tools/llvm-mingw"\n')
        write_lf(self.src / ".bazelrc", "startup --output_user_root=X:/out/bazel\n")
        write_lf(self.src / "uncommitted-later.txt", "x\n")
        git(self.src, "add", ".")
        git(self.src, "commit", "-q", "-m", "init")
        self.commit = git(self.src, "rev-parse", "HEAD")
        write_lf(self.src / "dirty.txt", "not committed\n")  # must not reach the clone
        write_lf(self.fake / "fakebazel.py", FAKE_BAZEL_PY)
        (self.fake / "bazel.cmd").write_bytes(('@"%s" -B "%%~dp0fakebazel.py" %%*\r\n' % sys.executable).encode())
        self.log = tmp / "bazel.log"
        self.n = 0

    def run(self, *extra, tests=None, code=0, verify_fail=False, error=False, version="9.3.0", source=None, **overrides):
        self.n += 1
        report = self.tmp / ("report%d" % self.n)
        env = dict(os.environ, FAKE_BAZEL_LOG=str(self.log), FAKE_BAZEL_LOGS=str(self.tmp / ("tl%d" % self.n)),
                   FAKE_BAZEL_CODE=str(code), FAKE_BAZEL_VERSION=version,
                   FAKE_BAZEL_TESTS=json.dumps(tests or []))
        # Bazel's strict test environment has no PATHEXT, and PowerShell then refuses to pipe an .exe ("cannot run a
        # document in the middle of a pipeline"); a real session always has it.
        env.setdefault("PATHEXT", ".COM;.EXE;.BAT;.CMD")
        env.pop("FAKE_VERIFY_FAIL", None)
        env.pop("FAKE_BAZEL_ERROR", None)
        if verify_fail:
            env["FAKE_VERIFY_FAIL"] = "1"
        if error:
            env["FAKE_BAZEL_ERROR"] = "1"
        args = {"Source": str(source or self.src), "WorkRoot": str(self.work), "ReportDirectory": str(report),
                "Dependencies": str(self.deps), "OutputRoot": str(self.deps / "bazel/output"), "OutputBase": str(self.base), "Bazel": str(self.fake / "bazel.cmd"), "Git": GIT}
        args.update({k: str(v) for k, v in overrides.items()})
        cmd = [str(PWSH), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(SCRIPT)]
        for k, v in args.items():
            cmd += ["-" + k, v]
        cmd += list(extra)
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env,
                           creationflags=NO_WINDOW, timeout=300)
        text = r.stdout + r.stderr
        calls = self.log.read_text(encoding="utf-8").splitlines() if self.log.exists() else []
        self.log.unlink(missing_ok=True)
        return r.returncode, text, report, calls



def verify_only_tests(tmp):
    """setup-dependencies.ps1 -VerifyOnly: reports missing items, exits 1, writes and downloads nothing."""
    repo = tmp / "vrepo"
    for source in SETUP_FILES:
        (repo / "tools").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, repo / "tools" / source.name)
    write_lf(repo / ".bazelversion", "9.3.0\n")
    write_lf(repo / "MODULE.bazel",
             'proxy_host(\n    ddraw = "%s",\n)\n' % (tmp / "no-ddraw.dll").as_posix())
    deps = tmp / "emptydeps"

    def run_setup(script, *args):
        cmd = [str(PWSH), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(repo / "tools" / script), *map(str, args)]
        env = dict(os.environ, ES2_PWSH=str(tmp / "no-pwsh.exe"))
        env.setdefault("PATHEXT", ".COM;.EXE;.BAT;.CMD")
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env,
                           creationflags=NO_WINDOW, timeout=120)
        return r.returncode, r.stdout + r.stderr

    code, text = run_setup("setup-dependencies.ps1", "-VerifyOnly", "-Destination", deps)
    check(code == 1, text)
    for needle in ("[MISSING] Python 3.12.14", "[MISSING] llvm-mingw", "[MISSING] sdl3-3.4.18",
                   "[MISSING] PowerShell 7", "[MISSING] ddraw.dll", "item(s) missing or mismatched"):
        check(needle in text, (needle, text))
    check("reference/" not in text, text)   # the reference repositories are not checked
    check(not deps.exists(), "-VerifyOnly created the dependency directory")
    # -Platform -VerifyOnly: trees present without kept archives are fine, a kept archive must match its pin.
    (deps / "sdl3-3.4.18").mkdir(parents=True)
    code, text = run_setup("setup-dependencies.ps1", "-Platform", "-VerifyOnly", "-Destination", deps)
    check(code == 0 and text.count("[OK] sdl3-3.4.18") == 1 and "[MISSING]" not in text, text)
    (deps / "downloads").mkdir()
    (deps / "downloads/SDL3-devel-3.4.18-mingw.zip").write_bytes(b"not the pinned archive")
    code, text = run_setup("setup-platform-dependencies.ps1", "-VerifyOnly", "-Destination", deps)
    check(code == 1 and "[MISMATCH] sdl3-3.4.18" in text, text)
    check(sorted(p.name for p in deps.rglob("*") if p.is_file()) == ["SDL3-devel-3.4.18-mingw.zip"], "-VerifyOnly wrote a file")
    # Web dependencies: every pin missing in an empty directory; a kept archive with the wrong bytes is a mismatch; nothing is written.
    web = tmp / "webdeps"
    for args in (("setup-web-dependencies.ps1", "-VerifyOnly", "-Destination", web), ("setup-dependencies.ps1", "-Web", "-VerifyOnly", "-Destination", web)):
        code, text = run_setup(*args)
        check(code == 1, text)
        for needle in ("[MISSING] emsdk-6.0.12.zip", "[MISSING] node-v24.19.0-win-x64.zip", "[MISSING] python-3.13.3-0-win-amd64.zip",
                       "wasm-binaries.zip", "[MISSING] emsdk ", "[MISSING] ffmpeg-9.0.2-full_build.zip", "[MISSING] ffmpeg ",
                       "item(s) missing or mismatched"):
            check(needle in text, (needle, text))
        check(not web.exists(), "-VerifyOnly created the dependency directory")
    (web / "downloads").mkdir(parents=True)
    (web / "downloads/emsdk-6.0.12.zip").write_bytes(b"not the pinned archive")
    code, text = run_setup("setup-web-dependencies.ps1", "-VerifyOnly", "-Destination", web)
    check(code == 1 and "[MISMATCH] emsdk-6.0.12.zip" in text and "[MISSING] ffmpeg-9.0.2-full_build.zip" in text, text)
    check(sorted(p.name for p in web.rglob("*") if p.is_file()) == ["emsdk-6.0.12.zip"], "-VerifyOnly wrote a file")


def main():
    with tempfile.TemporaryDirectory(prefix="es2-fresh-test-") as t:
        tmp = Path(t)
        fx = Fixture(tmp)

        # --- refusals: nothing is created, nothing is started -----------------------------------------
        for label, extra, kw, needle in (
            ("url", (), {"source": "https://example.invalid/es2.git"}, "not a URL"),
            ("scp", (), {"source": "git@example.invalid:x/es2.git"}, "not a URL"),
            ("missing", (), {"source": tmp / "nothing"}, "not a directory"),
            ("notgit", (), {"source": tmp / "fake" / "."}, "not a git repository"),
            ("name", (), {"OutputBase": fx.deps / "bazel/output/main"}, "package name"),
            ("shared", (), {"OutputBase": fx.deps / "bazel/output/cache"}, "package name"),
            ("outside", (), {"OutputBase": tmp / "elsewhere/fresh-clone"}, "directly below"),
            ("inside-source", (), {"WorkRoot": fx.src / "clone"}, "overlaps Source"),
            ("inside-deps", (), {"WorkRoot": fx.deps / "clone"}, "overlaps the dependency"),
            ("cpu", (), {"Cpu": 0}, "-Cpu"),
        ):
            code, text, report, calls = fx.run(*extra, **kw)
            check(code == 2 and needle in text, (label, code, text))
            check(not report.exists() and not fx.work.exists() and not calls, (label, "created something", calls))
        fx.work.mkdir()
        code, text, report, calls = fx.run()
        check(code == 2 and "WorkRoot exists" in text and not calls, text)
        fx.work.rmdir()
        fx.tmp.joinpath("report-exists").mkdir()
        code, text, report, calls = fx.run(ReportDirectory=tmp / "report-exists")
        check(code == 2 and "ReportDirectory exists" in text and not fx.work.exists() and not calls, text)

        # --- a passing run -----------------------------------------------------------------------------
        code, text, report, calls = fx.run(tests=TESTS_PASS)
        check(code == 0, text)
        check(not fx.work.exists(), "temporary clone not removed")
        check(not fx.base.exists() and not Path(str(fx.base) + "-contents").exists(), "script created the output base itself")
        body = (report / "report.md").read_bytes().decode("utf-8")
        check("**PASS**" in body and fx.commit in body, body)
        check("3 PASSED" in body, body)
        check("//formats:real_test" in body and "SKIP: ES2_DATA_ROOT is unset" in body, body)
        check("//tools:py_test" in body and "OK (skipped=2)" in body, body)
        check("//core:a_test" not in body.split("## Tests that skipped")[1].split("## Failing")[0], "non-skipping test listed as skipped")
        check("`X:/tools/llvm-mingw`" in body and "D:/ignored" not in body, body)
        check("[OK] Bazel".replace("[OK] ", "| OK | ") in body, body)
        check("removed" in body and "clean-worker-bazel.ps1 fresh-clone" in body, body)
        summary = json.loads((report / "summary.json").read_bytes().decode("utf-8"))
        check(summary["pass"] is True and summary["clone_removed"] is True and len(summary["tests"]) == 3, summary)
        check((report / "bep.json").exists() and (report / "bazel-test.log").exists(), "evidence files")
        versions = [c for c in calls if c.endswith("| --version")]
        tests = [c for c in calls if " test " in c]
        shut = [c for c in calls if c.endswith("shutdown")]
        check(len(versions) == 1 and len(tests) == 1 and len(shut) == 1 and calls[-1] == shut[0], calls)
        cwd, _, argline = tests[0].partition(" | ")
        check(Path(cwd).resolve() == (fx.work / "es2").resolve(), cwd)
        check(argline.startswith("--output_base=%s test --repo_contents_cache=%s-contents --local_resources=cpu=4 //... " % (fx.base, fx.base)), argline)
        check("--test_env=ES2_DATA_ROOT= " in argline + " " and "--repo_env=ES2_DATA_ROOT= " in argline + " "
              and "--keep_going" in argline, argline)
        check("--output_base=%s shutdown" % fx.base in shut[0], shut)

        # --- failing tests: cause in the report, cleanup still happens ---------------------------------
        code, text, report, calls = fx.run(tests=TESTS_FAIL, code=3)
        check(code == 1, text)
        check(not fx.work.exists(), "clone left after failing tests")
        body = (report / "report.md").read_bytes().decode("utf-8")
        check("**FAIL**" in body and "bazel test exited with 3" in body, body)
        check("`//shell:broken_test`: FAILED" in body and "FileNotFoundError: dbsim.exe" in body, body)
        check("`//shell:slow_test`: TIMEOUT" in body and "Failing tests (2)" in body, body)
        check(calls[-1].endswith("shutdown"), calls)

        # --- build error without a build event file ----------------------------------------------------
        code, text, report, calls = fx.run(code=1, error=True)
        check(code == 1 and not fx.work.exists(), text)
        body = (report / "report.md").read_bytes().decode("utf-8")
        check("ERROR: no such package 'foo'" in body and "bazel test exited with 1" in body, body)

        # --- missing dependency: Bazel not run for the build; -RunTestsAnyway runs it --------------------
        code, text, report, calls = fx.run(tests=TESTS_PASS, verify_fail=True)
        check(code == 1 and not fx.work.exists(), text)
        body = (report / "report.md").read_bytes().decode("utf-8")
        check("| MISSING | llvm-mingw |" in body and "| WARN | 7-Zip |" in body, body)
        check("Dependency check failed" in body and "Bazel was not run" in body, body)
        check(not [c for c in calls if " test " in c], calls)
        check(calls and calls[-1].endswith("shutdown"), calls)
        code, text, report, calls = fx.run("-RunTestsAnyway", tests=TESTS_PASS, verify_fail=True)
        check(code == 1 and len([c for c in calls if " test " in c]) == 1, (code, calls))

        # --- wrong Bazel version is a problem ----------------------------------------------------------
        code, text, report, calls = fx.run(version="9.2.0", tests=TESTS_PASS)
        check(code == 1 and "reports 'bazel 9.2.0'" in (report / "report.md").read_bytes().decode("utf-8"), text)

        # --- -NoTests and -KeepClone, -Ref ---------------------------------------------------------------
        git(fx.src, "branch", "other")
        code, text, report, calls = fx.run("-NoTests", "-KeepClone", Ref="other")
        check(code == 0, text)
        check((fx.work / "es2/.bazelversion").exists() and not (fx.work / "es2/dirty.txt").exists(), "kept clone content")
        check(not [c for c in calls if " test " in c], calls)
        check("Clone kept" in (report / "report.md").read_bytes().decode("utf-8"), "kept note")
        shutil.rmtree(fx.work)
        code, text, report, calls = fx.run("-NoTests", Ref="no-such-ref")
        check(code == 1 and not fx.work.exists() and "git checkout no-such-ref failed" in (report / "report.md").read_bytes().decode("utf-8"), text)

        # --- the report may sit inside the (ignored part of the) source tree --------------------------------
        code, text, report, calls = fx.run("-NoTests", ReportDirectory=fx.src / "scratch/report")
        check(code == 0 and (fx.src / "scratch/report/report.md").exists() and not fx.work.exists(), text)

        verify_only_tests(tmp)

        # --- the clone does not use hardlinks into the source --------------------------------------------
        code, text, report, calls = fx.run("-NoTests", "-KeepClone")
        loose = [p for p in (fx.work / "es2/.git/objects").rglob("*") if p.is_file() and len(p.parent.name) == 2]
        if loose:
            check(os.stat(loose[0]).st_nlink == 1, "hard link to the source object store")
        shutil.rmtree(fx.work)
    print("OK: %d checks" % checks)


if __name__ == "__main__":
    main()
