"""Two raw tick logs compared field by field, allowing differences only in the host fields.

  tick_log_raw_diff.py A.jsonl B.jsonl [--max N]
  tick_log_raw_diff.py --dirs A_DIR B_DIR [--suite NAME ...] [--max N]

Every line of A is compared with the same line of B as a JSON record: the same keys in the same order and equal
values, except the HOST_FIELDS of a record's top level, which come from the host and not from the game:

  pid, tid   the process and thread ids of the start record (the OS's numbers; the wasm build has its own);
  qpc        the counter value of each tick record (es2_os_counter at the record: wall time, never the guest's
             virtual time);
  dep_flags  the exit record's DEP state (GetProcessDEPPolicy natively; the wasm build has no DEP).

A line that is no JSON must be equal as text. The first form compares two files; --dirs compares
<A_DIR>/<suite>/tick-log.jsonl with <B_DIR>/<suite>/tick-log.jsonl for every suite below A_DIR that has one (or
the named suites) and prints one row per suite. Each difference outside the host fields is printed (at most --max per
pair, default 10) as `line N (event): field: A-value | B-value`.
Exit status: 0 when every pair differs at most in the host fields, 1 otherwise (a missing log included), 2 for a usage
error.
"""
import argparse
import json
import os
import sys

HOST_FIELDS = ("pid", "tid", "qpc", "dep_flags")
LOG_NAME = "tick-log.jsonl"


def read_lines(path):
    with open(path, "rb") as stream:
        data = stream.read()
    lines = data.split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    return [line[:-1] if line.endswith(b"\r") else line for line in lines]


def parse(line):
    try:
        record = json.loads(line.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None
    return record if isinstance(record, dict) else None


def short(value):
    text = json.dumps(value)
    return text if len(text) <= 60 else text[:57] + "..."


def compare(path_a, path_b):
    """(lines, {host field: count of differing records}, [difference text, ...])."""
    lines_a, lines_b = read_lines(path_a), read_lines(path_b)
    host = {field: 0 for field in HOST_FIELDS}
    other = []
    for number, (line_a, line_b) in enumerate(zip(lines_a, lines_b), 1):
        if line_a == line_b:
            continue
        rec_a, rec_b = parse(line_a), parse(line_b)
        if rec_a is None or rec_b is None:
            other.append("line %d: text: %s | %s" % (number, short(line_a.decode("utf-8", "replace")),
                                                       short(line_b.decode("utf-8", "replace"))))
            continue
        event = rec_a.get("event", "?")
        if list(rec_a) != list(rec_b):
            other.append("line %d (%s): keys: %s | %s" % (number, event, short(list(rec_a)), short(list(rec_b))))
            continue
        for key in rec_a:
            if rec_a[key] == rec_b[key]:
                continue
            if key in HOST_FIELDS:
                host[key] += 1
            else:
                other.append("line %d (%s): %s: %s | %s" % (number, event, key, short(rec_a[key]), short(rec_b[key])))
    if len(lines_a) != len(lines_b):
        other.append("lines: %d | %d" % (len(lines_a), len(lines_b)))
    return len(lines_a), host, other


def host_text(host):
    differing = ["%s %d" % (field, count) for field, count in host.items() if count]
    return ", ".join(differing) if differing else "none"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("logs", nargs="*", help="A.jsonl B.jsonl")
    parser.add_argument("--dirs", nargs=2, metavar=("A_DIR", "B_DIR"))
    parser.add_argument("--suite", action="append", default=[])
    parser.add_argument("--max", type=int, default=10)
    args = parser.parse_args(argv)
    if bool(args.dirs) == bool(args.logs) or (args.logs and len(args.logs) != 2) or (args.suite and not args.dirs):
        parser.error("either A.jsonl B.jsonl or --dirs A_DIR B_DIR [--suite NAME ...]")
    if args.logs:
        pairs = [(None, args.logs[0], args.logs[1])]
    else:
        names = args.suite or sorted(n for n in os.listdir(args.dirs[0])
                                     if os.path.isfile(os.path.join(args.dirs[0], n, LOG_NAME)))
        pairs = [(n, os.path.join(args.dirs[0], n, LOG_NAME), os.path.join(args.dirs[1], n, LOG_NAME)) for n in names]
    failed = 0
    if args.dirs:
        print("| Suite | Lines | Host fields that differ (records) | Other differences |")
        print("|---|---:|---|---:|")
    for name, path_a, path_b in pairs:
        missing = [p for p in (path_a, path_b) if not os.path.isfile(p)]
        if missing:
            failed += 1
            print("| %s | - | - | missing %s |" % (name, ", ".join(missing)) if name else "missing %s" % ", ".join(missing))
            continue
        lines, host, other = compare(path_a, path_b)
        failed += bool(other)
        if name:
            print("| `%s` | %d | %s | %d |" % (name, lines, host_text(host), len(other)))
        else:
            print("%d lines; host fields that differ: %s; other differences: %d" % (lines, host_text(host), len(other)))
        for text in other[:args.max]:
            print("  %s%s" % (name + ": " if name else "", text))
    if args.dirs:
        print("raw diff: %d pairs, %d with differences outside %s" % (len(pairs), failed, ", ".join(HOST_FIELDS)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
