"""tools/evidence_budget.py on synthetic repositories.

A throwaway git repository stands in for es2-data and a throwaway directory for the es2
documents; the real es2-data is never read. Also checks that the tool changes nothing
(the staged set is the same before and after).

usage: test_evidence_budget.py <evidence_budget.py>
"""
import datetime
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

TOOL = Path(sys.argv[1]).resolve()
GIT = shutil.which("git") or "C:/Program Files/Git/cmd/git.exe"
NO_WINDOW = 0x08000000 if os.name == "nt" else 0
checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def git(repo, *args):
    r = subprocess.run([GIT, "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args],
                       capture_output=True, text=True, creationflags=NO_WINDOW, timeout=60)
    check(r.returncode == 0, (args, r.stdout, r.stderr))
    return r.stdout.strip()


def put(path, data=b"x\n"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())


def tool(data, repo, *extra):
    r = subprocess.run([sys.executable, "-B", str(TOOL), "--data-root", str(data), "--repo", str(repo), *extra],
                       capture_output=True, text=True, creationflags=NO_WINDOW, timeout=60)
    return r.returncode, r.stdout + r.stderr


def make_old(path, days=30):
    stamp = time.time() - days * 86400
    for root, _d, files in os.walk(path):
        for f in files:
            os.utime(os.path.join(root, f), (stamp, stamp))
    os.utime(path, (stamp, stamp))


def main():
    with tempfile.TemporaryDirectory(prefix="es2-budget-test-") as tmp:
        tmp = Path(tmp).resolve()
        data, repo = tmp / "data", tmp / "repo"
        data.mkdir()
        git(data, "init", "-q", "-b", "main")
        put(repo / "docs/a.md", "Evidence in `logs\\cited-run` and logs/family-* and logs\\prefix-<utc>.\n")
        put(repo / "tools/x/README.md", "see logs/readme-run.\n")

        # Allowed staged files.
        put(data / "logs/video-compat.jsonl", b"x" * 300_000)                                 # small shared log, no run dir
        put(data / "logs/ok/live/s/run/s-input.jsonl", "{}\n")                                 # input recording
        put(data / "logs/ok/live/s/run/s-comparison.json", '{"identical": true}')
        put(data / "logs/diff/live/s/run/s.jsonl", "{}\n")                                     # run that differs
        put(data / "logs/diff/live/s/run/s-comparison.json", '{"identical": false}')
        put(data / "logs/nocmp/live/s/run/s.jsonl", "{}\n")                                    # no comparison: stays
        put(data / "logs/other/notes.md", "n\n")
        put(data / "logs/diff/live/s/run/s.jsonl.hooks.jsonl", '{"hook":"x"}\n')               # a mismatch log with content is evidence
        put(data / "logs/ok/live/s/run/s-normalized.json", b"x" * 8_000_000)                # normalized comparison of a long suite
        put(data / "oracle/golden/s/tick-log.jsonl", b"x" * 9_000_000)                       # a golden: no size limit
        git(data, "add", ".")
        before = git(data, "diff", "--cached", "--name-only")
        code, out = tool(data, repo)
        check(code == 0, out)
        check("staged files: 10, refused: 0" in out and out.splitlines()[-1].startswith("OK:"), out)
        check("sizes: logs" in out and ".git/lfs" in out, out)
        git(data, "commit", "-q", "-m", "base")

        # Refused staged files.
        put(data / "logs/ok2/live/s/run/s.jsonl", "{}\n")
        put(data / "logs/ok2/live/s/run/s-comparison.json", '{"identical": true}')
        put(data / "logs/ok2/live/s/run/s.jsonl.hooks.jsonl", "{}\n")
        put(data / "logs/m/mutation-workspace-1/a.c", "c\n")
        put(data / "logs/emp/live/s/run/s.jsonl.hooks.jsonl", b"")                             # empty hook mismatch log, run without comparison
        put(data / "logs/emp/elsewhere/t.hooks.jsonl", b"")                                    # ... wherever it lies
        for name in ("a.lst", "b.asm", "c.elf", "d.pkl", "e.dmp"):
            put(data / "logs/bin" / name)
        put(data / "logs/big/huge.bin", b"x" * 6_000_000)
        put(data / "logs/big/huge-normalized.json", b"x" * 6_000_000)                       # outside a run directory: 5 MiB
        put(data / "logs/big/allowed.bin", b"x" * 6_000_000)
        put(data / ".es2budget-allow", "# exempt\nlogs/big/allowed.bin\n")
        put(data / "logs/fine.txt", "ok\n")
        git(data, "add", ".")
        before = git(data, "diff", "--cached", "--name-only")
        status = git(data, "status", "--porcelain", "--untracked-files=all")
        code, out = tool(data, repo)
        check(code == 1, out)
        lines = [l for l in out.splitlines() if l.startswith("REFUSED ")]
        names = [l.split(":")[0][len("REFUSED "):] for l in lines]
        check(sorted(names) == sorted([
            "logs/ok2/live/s/run/s.jsonl", "logs/ok2/live/s/run/s.jsonl.hooks.jsonl", "logs/m/mutation-workspace-1/a.c",
            "logs/emp/live/s/run/s.jsonl.hooks.jsonl", "logs/emp/elsewhere/t.hooks.jsonl",
            "logs/bin/a.lst", "logs/bin/b.asm", "logs/bin/c.elf", "logs/bin/d.pkl", "logs/bin/e.dmp", "logs/big/huge.bin",
            "logs/big/huge-normalized.json"]), out)
        check("empty hook mismatch log" in out and "identical run" in out and "mutation workspace" in out and "size limit" in out and "minidump" in out, out)
        check(out.splitlines()[-1].startswith("REFUSED: staged ") and "refused 12" in out.splitlines()[-1], out)
        code, out = tool(data, repo, "--max-bytes", "10000000")
        check("size limit" not in out and code == 1, out)
        # The tool changes nothing.
        check(git(data, "diff", "--cached", "--name-only") == before)
        check(git(data, "status", "--porcelain", "--untracked-files=all") == status)
        check((data / "logs/ok2/live/s/run/s.jsonl").is_file())
        git(data, "reset", "-q")

        # Uncited evidence directories older than --older-than.
        for name in ("cited-run", "family-one", "prefix-2026", "readme-run", "old-uncited", "new-uncited"):
            put(data / "logs" / name / "f.txt", "data\n")
        for name in ("cited-run", "family-one", "prefix-2026", "readme-run", "old-uncited"):
            make_old(data / "logs" / name)
        today = datetime.date.today().isoformat()
        code, out = tool(data, repo, "--older-than", today)
        check("old-uncited" in out and "new-uncited" not in out, out)
        for name in ("cited-run", "family-one", "prefix-2026", "readme-run"):
            check("  " + name + "  " not in out, (name, out))
        check("uncited candidates 1" in out.splitlines()[-1], out)
        # Without --older-than the date of the document repository's first commit; without a commit no list.
        code, out = tool(data, repo)
        check("skipped (no --older-than" in out and "uncited candidates skipped" in out.splitlines()[-1], out)
        git(repo, "init", "-q", "-b", "main")
        git(repo, "add", ".")
        git(repo, "commit", "-q", "-m", "docs")
        code, out = tool(data, repo)
        check("older than %s" % today in out and "old-uncited" in out and "new-uncited" not in out, out)

        # Usage errors and non-git directories.
        code, out = tool(tmp / "missing", repo)
        check(code == 2, out)
        code, out = tool(data, repo, "--older-than", "yesterday")
        check(code == 2, out)
        plain = tmp / "plain"
        put(plain / "logs/x/f.txt")
        code, out = tool(plain, repo)
        check(code == 0 and "check skipped" in out, out)
        print("%d checks" % checks)


if __name__ == "__main__":
    main()
