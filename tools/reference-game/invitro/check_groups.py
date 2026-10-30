"""Check groups.bzl against the hook registry (no game data).

usage: check_groups.py <hook_registry.h> <groups.bzl>

The keys of HOOK_GROUPS must be exactly the groups of the registry, and each
full-tier shard count must satisfy 1 <= shards <= members and
HOOKS_PER_SHARD * shards >= members.
"""
import re
import sys
from pathlib import Path

HOOKS_PER_SHARD = 12
REGISTRY = re.compile(r"^ES2_HOOK\(\s*\w+\s*,[^,]*,[^,]*,[^,]*,\s*(\w+)\s*\)", re.M)
TABLE = re.compile(r"HOOK_GROUPS\s*=\s*\{(.*?)\}", re.S)
ENTRY = re.compile(r'"(\w+)"\s*:\s*(\d+)')


def members(registry_text):
    counts = {}
    for group in REGISTRY.findall(registry_text):
        counts[group] = counts.get(group, 0) + 1
    if not counts:
        raise ValueError("no ES2_HOOK entries in the registry")
    return counts


def shards(bzl_text):
    table = TABLE.search(bzl_text)
    if not table:
        raise ValueError("HOOK_GROUPS table not found")
    entries = ENTRY.findall(table.group(1))
    result = dict((group, int(count)) for group, count in entries)
    if len(result) != len(entries):
        raise ValueError("duplicate group in HOOK_GROUPS")
    return result


def check(counts, table):
    problems = []
    for group in sorted(set(counts) - set(table)):
        problems.append(f"registry group {group} ({counts[group]} hooks) is missing from groups.bzl")
    for group in sorted(set(table) - set(counts)):
        problems.append(f"groups.bzl lists {group}, which the registry does not have")
    for group in sorted(set(counts) & set(table)):
        n, s = counts[group], table[group]
        if s < 1 or s > n:
            problems.append(f"{group}: {s} shards for {n} hooks (need 1 <= shards <= hooks)")
        elif HOOKS_PER_SHARD * s < n:
            problems.append(f"{group}: {n} hooks need at least {-(-n // HOOKS_PER_SHARD)} shards, groups.bzl has {s}")
    return problems


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    counts = members(Path(argv[1]).read_text(encoding="utf-8"))
    table = shards(Path(argv[2]).read_text(encoding="utf-8"))
    problems = check(counts, table)
    for problem in problems:
        print("FAIL:", problem, file=sys.stderr)
    if problems:
        return 1
    print(f"PASS: {len(counts)} hook groups, {sum(counts.values())} hooks, {sum(table.values())} full-tier shards")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
