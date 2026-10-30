# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
#!/usr/bin/env python3
"""Evidence budget: guards the size and content of the es2-data repository.

Read-only. It never stages, unstages, deletes or commits anything.

  python -B tools/evidence_budget.py [--data-root <es2-data>] [--repo <es2>]
                                     [--older-than <yyyy-mm-dd>] [--max-bytes 5242880]

1. Staged files of the es2-data repository (`git diff --cached`) are refused when they are
     - a raw tick log of an identical run: a `*.jsonl` (also `*.jsonl.hooks.jsonl`) directly in a
       directory named `run`, whose comparison file (`*comparison*.json` in that directory) says
       `"identical": true`. `tools/check.ps1` deletes exactly these after an identical regression.
       Input recordings (`*-input.jsonl`) and tick logs of runs that differ or have no comparison
       are allowed (everything stays until a difference is explained). Small shared logs outside
       a `run` directory (for example `logs/video-compat.jsonl`) are no tick logs and are allowed;
     - an empty `*.hooks.jsonl`: the proxy opens the hook mismatch log when a hook runs in compare mode, so
       a run without mismatches leaves a zero-byte file that says nothing; the runner and `tools/check.ps1`
       delete it, and anything at that path is refused wherever it lies. A mismatch log with content is evidence
       and is allowed (apart from the raw-tick-log rule above);
     - inside a directory whose name starts with `mutation-workspace` (repository copies);
     - a listing, code image, pickle or minidump: `*.lst`, `*.asm`, `*.elf`, `*.pkl`, `*.dmp`
       (derived from original binaries);
     - larger than --max-bytes (default 5 MiB). Paths listed in `<data-root>/.es2budget-allow`
       (one repository-relative path per line, `#` comments) are exempt from the size rule only.
2. Reports the size of `logs/`, `.git` and `.git/lfs` of the data repository.
3. Lists top-level directories of `logs/` whose newest file is older than --older-than (default: the
   date of the es2 repository's first commit) and that no document of the es2 repository cites
   (`logs\\<name>` or `logs/<name>` in docs/**/*.md, PLAN.md where present, CLAUDE.md, README.md, tools/**/README.md;
   `*` and a trailing `<placeholder>` match any suffix). Without --older-than and without a commit in
   --repo the list is skipped. These are only candidates for the next discard; deleting needs a decision.

Exit status: 0 nothing refused, 1 something staged is refused (the list is printed), 2 usage.
The last output line is a one-line summary.
"""
import argparse
import datetime
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

FORBIDDEN_SUFFIXES = {".lst": "listing", ".asm": "listing", ".elf": "code image", ".pkl": "pickle", ".dmp": "minidump"}
DEFAULT_MAX_BYTES = 5 * 1024 * 1024
# The normalized comparison (`<suite>-normalized.json` in a `run` directory) is the evidence an identical run
# keeps; it grows with the suite (c3_09-loadout-b and c5_05-moon-missiles write about 7.4 MB each).
NORMALIZED_MAX_BYTES = 32 * 1024 * 1024
ALLOW_FILE = ".es2budget-allow"


GIT = shutil.which("git") or "C:/Program Files/Git/cmd/git.exe"


def git(root, *args):
    result = subprocess.run([GIT, "-C", str(root), *args], capture_output=True)
    return result.returncode, result.stdout


def staged_paths(data_root):
    code, out = git(data_root, "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
    if code != 0:
        return None
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def staged_size(data_root, rel):
    path = Path(data_root) / rel
    try:
        return path.stat().st_size
    except OSError:
        code, out = git(data_root, "cat-file", "-s", ":" + rel)
        return int(out.strip() or 0) if code == 0 else 0


def run_is_identical(data_root, rel_dir):
    """True if a comparison file next to the tick log says identical."""
    folder = Path(data_root) / rel_dir
    try:
        candidates = sorted(folder.glob("*comparison*.json"))
    except OSError:
        return False
    for candidate in candidates:
        try:
            data = json.loads(candidate.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            continue
        if isinstance(data, dict) and data.get("identical") is True:
            return True
    return False


def load_allow(data_root):
    allowed = set()
    path = Path(data_root) / ALLOW_FILE
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.split("#", 1)[0].strip().replace("\\", "/")
            if line:
                allowed.add(line)
    return allowed


def classify(data_root, rel, size, max_bytes, allow):
    """Reasons why a staged file is refused (empty list: allowed)."""
    reasons = []
    parts = rel.replace("\\", "/").split("/")
    name = parts[-1]
    lower = name.lower()
    if any(p.lower().startswith("mutation-workspace") for p in parts[:-1]):
        reasons.append("inside a mutation workspace (repository copy)")
    suffix = os.path.splitext(lower)[1]
    if suffix in FORBIDDEN_SUFFIXES:
        reasons.append("%s (%s) derived from original binaries" % (FORBIDDEN_SUFFIXES[suffix], suffix))
    if (lower.endswith(".jsonl") and len(parts) >= 2 and parts[-2].lower() == "run"
            and not lower.endswith("-input.jsonl")
            and run_is_identical(data_root, "/".join(parts[:-1]))):
        reasons.append("raw tick log of an identical run (check.ps1 deletes it; keep the comparison)")
    if lower.endswith(".hooks.jsonl") and size == 0:
        reasons.append("empty hook mismatch log (no mismatch was recorded; delete it)")
    limit = max_bytes
    if lower.endswith("-normalized.json") and len(parts) >= 2 and parts[-2].lower() == "run":
        limit = max(max_bytes, NORMALIZED_MAX_BYTES)
    # The goldens (oracle/golden/<suite>/: tick-log.jsonl, normalized.json, manifest.json) are the
    # regression references themselves; their size follows the suite length and has no limit.
    golden = len(parts) >= 3 and parts[0].lower() == "oracle" and parts[1].lower() == "golden"
    if size > limit and not golden and rel.replace("\\", "/") not in allow:
        reasons.append("%s is over the size limit of %s" % (fmt_bytes(size), fmt_bytes(limit)))
    return reasons


def tree_size(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
    return total


def fmt_bytes(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "B" else "%.1f %s" % (n, unit)
        n /= 1024.0


def newest_mtime(path):
    newest = os.path.getmtime(path)
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                newest = max(newest, os.lstat(os.path.join(root, f)).st_mtime)
            except OSError:
                pass
    return newest


CITE = re.compile(r"logs[\\/]+([A-Za-z0-9_.*\-]+)([<{]?)", re.IGNORECASE)


def cited_patterns(repo):
    files = []
    docs = Path(repo) / "docs"
    if docs.is_dir():
        files += sorted(docs.rglob("*.md"))
    for name in ("PLAN.md", "CLAUDE.md", "README.md"):
        if (Path(repo) / name).is_file():
            files.append(Path(repo) / name)
    tools = Path(repo) / "tools"
    if tools.is_dir():
        files += sorted(tools.rglob("README.md"))
    patterns = set()
    for f in files:
        try:
            text = f.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for match in CITE.finditer(text):
            token = match.group(1).rstrip(".")
            if match.group(2):
                token += "*"
            if token:
                patterns.add(token.lower())
    return patterns


def first_commit_date(repo):
    """The commit date of the oldest root commit of the repository at `repo` (not of one above it), else None."""
    code, out = git(repo, "rev-parse", "--show-toplevel")
    top = out.decode("utf-8", "replace").strip()
    if code != 0 or os.path.normcase(os.path.realpath(top)) != os.path.normcase(os.path.realpath(repo)):
        return None
    code, out = git(repo, "log", "--max-parents=0", "--format=%cs", "HEAD")
    try:
        days = [datetime.date.fromisoformat(d) for d in out.decode("ascii", "replace").split()]
    except ValueError:
        return None
    return min(days) if code == 0 and days else None


def uncited_candidates(data_root, repo, older_than):
    logs = Path(data_root) / "logs"
    if not logs.is_dir():
        return []
    cutoff = datetime.datetime.combine(older_than, datetime.time.min).timestamp()
    patterns = cited_patterns(repo)
    rows = []
    for entry in sorted(logs.iterdir()):
        if not entry.is_dir():
            continue
        name = entry.name.lower()
        if any(fnmatch.fnmatchcase(name, p) for p in patterns):
            continue
        newest = newest_mtime(entry)
        if newest < cutoff:
            rows.append((entry.name, datetime.date.fromtimestamp(newest), tree_size(entry)))
    return rows


def parse_date(value):
    try:
        return datetime.date.fromisoformat(value)
    except ValueError:
        raise argparse.ArgumentTypeError("not a date yyyy-mm-dd: %s" % value)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Evidence budget of the es2-data repository (read only).")
    parser.add_argument("--data-root", default=os.environ.get("ES2_DATA_REPO") or str(Path(__file__).resolve().parents[2] / "es2-data"),
                        help="es2-data repository (default: ES2_DATA_REPO, else beside the es2 checkout)")
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent), help="es2 repository (documents)")
    parser.add_argument("--older-than", type=parse_date,
                        help="list uncited evidence directories whose newest file is older than this date, yyyy-mm-dd "
                             "(default: the date of the es2 repository's first commit)")
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="size limit of a staged file")
    args = parser.parse_args(argv)
    data_root = Path(args.data_root)
    if not data_root.is_dir():
        print("evidence_budget: not a directory: %s" % data_root, file=sys.stderr)
        return 2
    if args.max_bytes < 1:
        print("evidence_budget: --max-bytes must be positive", file=sys.stderr)
        return 2

    print("evidence budget of %s" % data_root)
    paths = staged_paths(data_root)
    refused = []
    if paths is None:
        print("staged files: not a git repository, check skipped")
        paths = []
    else:
        allow = load_allow(data_root)
        for rel in paths:
            reasons = classify(data_root, rel, staged_size(data_root, rel), args.max_bytes, allow)
            if reasons:
                refused.append((rel, reasons))
        print("staged files: %d, refused: %d" % (len(paths), len(refused)))
        for rel, reasons in refused:
            print("REFUSED %s: %s" % (rel, "; ".join(reasons)))

    logs_size = tree_size(data_root / "logs") if (data_root / "logs").is_dir() else 0
    git_dir = data_root / ".git"
    git_size = tree_size(git_dir) if git_dir.is_dir() else 0
    lfs_size = tree_size(git_dir / "lfs") if (git_dir / "lfs").is_dir() else 0
    print("sizes: logs %s, .git %s, .git/lfs %s" % (fmt_bytes(logs_size), fmt_bytes(git_size), fmt_bytes(lfs_size)))

    older_than = args.older_than or first_commit_date(args.repo)
    rows = []
    if older_than:
        rows = uncited_candidates(data_root, args.repo, older_than)
        print("evidence directories older than %s that no document cites: %d (%s)"
              % (older_than.isoformat(), len(rows), fmt_bytes(sum(r[2] for r in rows))))
        for name, day, size in rows:
            print("  %s  newest %s  %s" % (name, day.isoformat(), fmt_bytes(size)))
    else:
        print("evidence directories that no document cites: skipped (no --older-than and no commit in %s)" % args.repo)

    print("%s: staged %d, refused %d; logs %s, .git %s, lfs %s; uncited candidates %s"
          % ("REFUSED" if refused else "OK", len(paths), len(refused), fmt_bytes(logs_size), fmt_bytes(git_size),
             fmt_bytes(lfs_size), len(rows) if older_than else "skipped"))
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
