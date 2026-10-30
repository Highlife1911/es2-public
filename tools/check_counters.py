"""Hook counters of a parallel oracle run (helper of tools/check.ps1).

usage: check_counters.py <run-directory> --output <hook-counters.json> [--suites a,b]

<run-directory> is the output of run-oracle-parallel.ps1: one directory per
suite with `run/<suite>-comparison.json` and `run/<suite>.jsonl`. For each suite
the counters are taken from three records of the raw tick log:

* compared_tick: the tick record number `common_ticks` of the comparison (the
  last tick both logs have),
* last_record: the last tick record of the log,
* exit_counters: the final `exit_counters` record of the log, or null.

Only the `hk_*` fields are kept. The file written to --output holds `source`
and `suites`. A JSON report
goes to stdout: per suite the tick count, whether an exit record exists, every
nonzero `*_skipped`/`*_bypassed` counter (maximum over the three records),
every nonzero `*_mismatches` counter, and the problems found. Exit status 0
when no suite has a problem, 1 otherwise, 2 for a usage error.

The `rtl_*` fields of the CW3220 import shim are checked the
same way without being kept: their `*_skipped`/`*_bypassed` and
`*_mismatches` counters, plus the counters in RTL_ZERO, go to the report
when nonzero; `rtl_refused` (the shim did not install, so the run did not
test the configured imports) and a negative `rtl_heap_own_blocks` (the own
heap's walk failed) are problems.

The tick log is read line by line and only the three records are parsed.
"""
import argparse
import json
import re
import sys
from pathlib import Path

TICK = re.compile(rb'^\s*\{\s*"event"\s*:\s*"tick"')
EXIT = re.compile(rb'^\s*\{\s*"event"\s*:\s*"exit_counters"')
SOURCE = "last compared tick (common_ticks), last tick record and exit_counters of each raw tick log"
# rtl counters that are 0 in a good run although their names do not say so.
RTL_ZERO = ("rtl_foreign_calls", "rtl_heap_foreign_frees", "rtl_heap_foreign_ops", "rtl_heap_os_unobserved",
            "rtl_heap_resyncs", "rtl_heap_own_os_failed", "rtl_life_atexit_refused", "rtl_life_cw_atexit")


def hook_fields(record):
    return {k: v for k, v in record.items() if k.startswith("hk_")}


def check_rtl(record, report):
    """The rtl_* fields of one record into the report (nonzero, mismatches, problems)."""
    problems = report["problems"]
    refused = record.get("rtl_refused")
    if refused is not None and f"rtl_refused: {refused}" not in problems:
        problems.append(f"rtl_refused: {refused}")
    for name, value in record.items():
        if not name.startswith("rtl_") or not isinstance(value, int) or isinstance(value, bool):
            continue
        if name == "rtl_heap_own_blocks":
            if value < 0 and "rtl_heap_own_blocks: the own heap's walk failed" not in problems:
                problems.append("rtl_heap_own_blocks: the own heap's walk failed")
            continue
        if value == 0:
            continue
        if name.endswith("_skipped") or name.endswith("_bypassed") or name in RTL_ZERO:
            report["nonzero"][name] = max(value, report["nonzero"].get(name, 0))
        elif name.endswith("_mismatches"):
            report["mismatches"][name] = max(value, report["mismatches"].get(name, 0))


def read_log(path, wanted):
    """Returns (tick count, tick line number `wanted` or None, last tick line, exit line)."""
    count, picked, last, exit_line = 0, None, None, None
    with open(path, "rb") as handle:
        for line in handle:
            if TICK.match(line):
                count += 1
                last = line
                if count == wanted:
                    picked = line
            elif EXIT.match(line):
                exit_line = line
    return count, picked, last, exit_line


def suite_counters(run_dir, suite):
    """(entry for hook-counters.json, report entry); problems end up in the report."""
    problems = []
    entry = {"common_ticks": None, "compared_tick": None, "exit_counters": None, "last_record": None}
    report = {"ticks": 0, "has_exit_counters": False, "nonzero": {}, "mismatches": {}, "problems": problems}
    comparison_path = run_dir / f"{suite}-comparison.json"
    log_path = run_dir / f"{suite}.jsonl"
    try:
        comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
        common = comparison["common_ticks"]
        if not isinstance(common, int) or isinstance(common, bool) or common < 0:
            raise ValueError("common_ticks is not a count")
    except (OSError, ValueError, KeyError) as error:
        problems.append(f"{comparison_path.name}: {error}")
        common = None
    entry["common_ticks"] = common
    if not log_path.is_file():
        problems.append(f"{log_path.name}: missing")
        return entry, report
    try:
        count, picked, last, exit_line = read_log(log_path, common if common else 0)
        report["ticks"] = count
        if not count:
            problems.append(f"{log_path.name}: no tick record")
        if common is not None and common > count:
            problems.append(f"{log_path.name}: {count} tick records, comparison says {common} common ticks")
        if common == 0:
            problems.append(f"{comparison_path.name}: no common tick")
        records = {}
        if picked is not None:
            records["compared_tick"] = json.loads(picked)
        if last is not None:
            records["last_record"] = json.loads(last)
        if exit_line is not None:
            records["exit_counters"] = json.loads(exit_line)
    except (OSError, ValueError) as error:
        problems.append(f"{log_path.name}: {error}")
        return entry, report
    for key, record in records.items():
        entry[key] = hook_fields(record)
        check_rtl(record, report)
    report["has_exit_counters"] = exit_line is not None
    for record in entry.values():
        if not isinstance(record, dict):
            continue
        for name, value in record.items():
            if not isinstance(value, int) or isinstance(value, bool) or value == 0:
                continue
            if name.endswith("_skipped") or name.endswith("_bypassed"):
                report["nonzero"][name] = max(value, report["nonzero"].get(name, 0))
            elif name.endswith("_mismatches"):
                report["mismatches"][name] = max(value, report["mismatches"].get(name, 0))
    return entry, report


def discover(root):
    return sorted(p.name for p in root.iterdir() if p.is_dir() and (p / "run").is_dir())


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--suites", default="", help="comma-separated suite names (default: every suite directory)")
    args = parser.parse_args(argv)
    if not args.run_directory.is_dir():
        print(f"check_counters: not a directory: {args.run_directory}", file=sys.stderr)
        return 2
    suites = [s for s in args.suites.split(",") if s] or discover(args.run_directory)
    if not suites:
        print(f"check_counters: no suite directory in {args.run_directory}", file=sys.stderr)
        return 2
    counters, reports = {}, {}
    for suite in suites:
        counters[suite], reports[suite] = suite_counters(args.run_directory / suite / "run", suite)
    args.output.write_bytes((json.dumps({"source": SOURCE, "suites": counters}, indent=1) + "\n").encode("utf-8"))
    print(json.dumps({"suites": reports}, indent=1))
    return 1 if any(r["problems"] for r in reports.values()) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
