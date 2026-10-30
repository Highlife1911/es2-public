"""tools/worker.ps1 and tools/clean-worker-bazel.ps1 on synthetic locations.

Everything happens below one temporary directory: a throwaway git repository
with a branch `main` (the "main tree"), the worktree parent, the Bazel output
root, the evidence root, and a fake `bazel.cmd` that only logs its arguments.
The scripts' path parameters point there; the real checkout, the real output base
and es2-data are never touched, and no Bazel or game is started.

usage: test_worker.py <worker.ps1> <clean-worker-bazel.ps1>
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

WORKER = Path(sys.argv[1]).resolve()
CLEAN = Path(sys.argv[2]).resolve()
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


def run(script, *args, cwd=None, env=None):
    cmd = [str(PWSH), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(script),
           *map(str, args)]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd, env=env, creationflags=NO_WINDOW, timeout=120)
    return r.returncode, r.stdout + r.stderr


def git(repo, *args):
    r = subprocess.run([GIT, "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", creationflags=NO_WINDOW, timeout=60)
    check(r.returncode == 0, (args, r.stdout, r.stderr))
    return r.stdout.strip()


def touch(path, data=b"x"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    with tempfile.TemporaryDirectory(prefix="es2-worker-test-") as tmp:
        tmp = Path(tmp).resolve()
        repo = tmp / "es2"
        parent = tmp / "trees"
        out = tmp / "output"
        evidence = tmp / "logs"
        for d in (repo, parent, out, evidence):
            d.mkdir()
        git(repo, "init", "-q", "-b", "main")
        touch(repo / "README.md")
        git(repo, "add", "README.md")
        git(repo, "commit", "-q", "-m", "init")

        # A fake bazel that logs its arguments and the directory it ran in.
        fake = tmp / "fakebin"
        fake.mkdir()
        log = tmp / "bazel.log"
        (fake / "bazel.cmd").write_bytes(b'@echo %CD% %* >> "%ES2_FAKE_BAZEL_LOG%"\r\n')
        env = dict(os.environ)
        env["PATH"] = os.pathsep.join([str(fake), os.path.normpath(os.path.dirname(GIT)), env.get("PATH", "")])
        env["ES2_FAKE_BAZEL_LOG"] = str(log)
        env.setdefault("PATHEXT", ".COM;.EXE;.BAT;.CMD")  # Bazel strips it from the test environment

        paths = ["-Repo", repo, "-WorktreeParent", parent, "-OutputRoot", out, "-EvidenceRoot", evidence,
                 "-Date", "20261011"]

        # --- name validation ------------------------------------------------
        for bad in ("pkg", "Bad-Name", "cache", "install", "pkg-", "a b"):
            code, text = run(WORKER, bad, *paths, env=env)
            check(code != 0, (bad, text))
        check(not any(parent.iterdir()), "bad names created something")
        code, text = run(WORKER, "topic-a", "-Archive", *paths, env=env)
        check(code != 0 and "-Finish" in text, text)

        # --- create ---------------------------------------------------------
        code, text = run(WORKER, "topic-b", *paths, env=env)
        check(code == 0, text)
        tree = parent / "es2-topic-b"
        base = out / "topic-b"
        check(tree.is_dir() and (tree / "README.md").exists(), "worktree missing")
        check((tree / "build/claude/topic-b").is_dir(), "scratch directory missing")
        check(git(tree, "rev-parse", "--abbrev-ref", "HEAD") == "claude/topic-b", "branch")
        check(git(tree, "rev-parse", "HEAD") == git(repo, "rev-parse", "main"), "branch not from main")
        check(f"--output_base={base} test --repo_contents_cache={base}-contents --local_resources=cpu=4 <targets>" in text, text)
        check(f"bazel --output_base={base} shutdown" in text, text)
        check(f"Set-Location {tree}" in text, text)
        check(not list(out.iterdir()), "create must not create Bazel directories")

        # --- refuse existing directory / branch ----------------------------
        code, text = run(WORKER, "topic-b", *paths, env=env)
        check(code != 0 and "already exists" in text, text)
        shutil.rmtree(tree, ignore_errors=True)
        git(repo, "worktree", "prune")
        code, text = run(WORKER, "topic-b", *paths, env=env)
        check(code != 0 and "branch claude/topic-b already exists" in text, text)
        check(not tree.exists(), "worktree created despite the existing branch")
        git(repo, "branch", "-D", "claude/topic-b")
        code, text = run(WORKER, "topic-b", *paths, "-Base", "nomain", env=env)
        check(code != 0 and "nomain" in text, text)
        check(not tree.exists(), "worktree created from a missing base")

        # --- finish: servers, listing, nothing removed ---------------------
        code, text = run(WORKER, "topic-a", *paths, env=env)
        check(code == 0, text)
        tree = parent / "es2-topic-a"
        for suffix in ("", "-mut", "-mutation"):
            touch(out / f"topic-a{suffix}/server/server.pid.txt")
        touch(out / "topic-a-other/server/server.pid.txt")  # not this worker's
        code, text = run(WORKER, "topic-a", "-Finish", *paths, env=env)
        check(code == 0, text)
        calls = [l.rstrip() for l in log.read_text().splitlines()]
        check(len(calls) == 3, calls)
        for suffix, line in zip(("", "-mut", "-mutation"), calls):
            check(line.endswith(f"--output_base={out / ('topic-a' + suffix)} shutdown"), line)
            check(line.startswith(str(tree)), line)
        for needle in (f"git -C {repo} worktree remove {tree}", f"git -C {repo} branch -d claude/topic-a",
                       "clean-worker-bazel.ps1 topic-a", f"copy {tree}\\build\\claude\\topic-a to {evidence}\\topic-a-20261011",
                       "mutation-workspace-*", "*.exe", "*.dll", "*.pkl"):
            check(needle in text, (needle, text))
        check(tree.is_dir() and (out / "topic-a").is_dir(), "-Finish removed something")
        check(git(repo, "branch", "--list", "claude/topic-a") != "", "-Finish removed the branch")
        check(not any(evidence.iterdir()), "-Finish without -Archive wrote evidence")

        # --- archive: exclusions -------------------------------------------
        scratch = tree / "build/claude/topic-a"
        keep = {
            "report.md": b"report",
            "logs/run.log": b"log",
            "mutation/mutation-out.txt": b"out",
            "mutation/MUTANT.exe.txt": b"similar name, kept",
            "mutation/deep/notes.txt": b"n",
            "lst.txt": b"not a listing",
        }
        drop = [
            "mutant.exe", "mutation/mutant.exe", "mutation/Mutant.EXE", "DBSIM.EXE", "h.exe", "x.dll", "x.lst", "y.asm", "z.elf", "q.pkl",
            "logs/deep/code.LST", "mutation-workspace-1/a.txt", "mutation/mutation-workspace-topic-c/sub/b.txt",
        ]
        for name, data in keep.items():
            touch(scratch / name, data)
        for name in drop:
            touch(scratch / name)
        target = evidence / "topic-a-20261011"
        # Refuse when the evidence root is missing; nothing is created.
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths[:6], "-EvidenceRoot", tmp / "nologs",
                         "-Date", "20261011", env=env)
        check(code != 0 and "does not exist" in text and not (tmp / "nologs").exists(), text)
        # A junction in the scratch directory is skipped, never followed.
        outside = tmp / "outside"
        touch(outside / "secret.txt")
        subprocess.run(["cmd", "/c", "mklink", "/J", str(scratch / "link"), str(outside)], capture_output=True,
                       creationflags=NO_WINDOW, check=True)
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths, env=env)
        check(code == 0, text)
        check("(done) evidence copied" in text and "archived" in text, text)
        got = sorted(p.relative_to(target).as_posix() for p in target.rglob("*") if p.is_file())
        check(got == sorted(keep), (got, sorted(keep)))
        for name, data in keep.items():
            check((target / name).read_bytes() == data, name)
        check(not (target / "link").exists(), "junction was followed or copied")
        check((outside / "secret.txt").exists(), "junction target touched")
        check(not Path(str(target) + ".partial").exists(), "staging left behind")
        check(sorted(p.name for p in evidence.iterdir()) == ["topic-a-20261011"], "something else written to the evidence root")
        check((scratch / "mutant.exe").exists(), "source modified")

        # Second archive refuses (target exists); a stale staging directory refuses too.
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths, env=env)
        check(code != 0 and "already exists" in text, text)
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths[:-1], "20261012", env=env)
        check(code == 0, text)
        (evidence / "topic-a-20261013.partial").mkdir()
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths[:-1], "20261013", env=env)
        check(code != 0 and "staging" in text, text)
        code, text = run(WORKER, "topic-a", "-Finish", "-Archive", *paths[:-1], "2026-10-13", env=env)
        check(code != 0, text)

        # --- clean-worker-bazel: suffixes and safety -----------------------
        cpaths = ["-OutputRoot", out, "-Repo", repo, "-WorktreeParent", parent]
        pkg = "clean-a"
        for suffix in ("", "-contents", "-mut", "-mut-contents", "-mutation", "-mutation-contents"):
            touch(out / f"{pkg}{suffix}/f.txt")
        touch(out / "clean-ax/f.txt")  # not a variant
        touch(out / "clean-b/f.txt")   # another worker
        touch(outside / "keep/survivor.txt")
        subprocess.run(["cmd", "/c", "mklink", "/J", str(out / "clean-a-mutation/link"), str(outside / "keep")],
                       capture_output=True, creationflags=NO_WINDOW, check=True)
        code, text = run(CLEAN, pkg, "-DryRun", *cpaths, env=env)
        check(code == 0 and (out / "clean-a-mutation/f.txt").exists(), text)
        check(text.count("delete ") == 6 and "clean-a-mutation " in text, text)
        code, text = run(CLEAN, pkg, *cpaths, env=env)
        check(code == 0, text)
        check(sorted(p.name for p in out.iterdir() if p.name.startswith("clean-a")) == ["clean-ax"], text)
        check((out / "clean-b/f.txt").exists(), "other package removed")
        check((outside / "keep/survivor.txt").exists(), "junction target deleted")

        # Refusals: worktree exists, reserved names, the main tree's hashed base, bad names.
        (parent / "es2-clean-b").mkdir()
        code, text = run(CLEAN, "clean-b", *cpaths, env=env)
        check(code != 0 and "still exists" in text and (out / "clean-b/f.txt").exists(), text)
        for bad in ("cache", "install", "abc123def", "Bad-Name"):
            touch(out / bad / "f.txt")
            code, text = run(CLEAN, bad, *cpaths, env=env)
            check(code != 0 and (out / bad / "f.txt").exists(), (bad, text))
        code, text = run(CLEAN, "zz-99", *cpaths, env=env)
        check(code == 0 and "nothing" in text, text)

        # A running server is shut down through the fake bazel before deletion.
        log.write_text("")
        touch(out / "clean-c/server/server.pid.txt")
        code, text = run(CLEAN, "clean-c", *cpaths, env=env)
        check(code == 0 and not (out / "clean-c").exists(), text)
        calls = [l.rstrip() for l in log.read_text().splitlines()]
        check(len(calls) == 1 and calls[0].endswith(f"--output_base={out / 'clean-c'} shutdown"), calls)
        check(calls[0].startswith(str(repo)), calls)

    print(f"worker script test: {checks} checks passed")


if __name__ == "__main__":
    main()
