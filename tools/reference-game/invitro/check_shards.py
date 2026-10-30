"""Compare completed shard or group logs (full tier, plus the probes log) with an unsharded harness run; no game data."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


PROOF = re.compile(r"^PROVED\(\s*(\w+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*([01])\s*,", re.M)


def read_proofs(path=Path(__file__).with_name("branch_sites.h")):
    """{(hook, 8-digit site): feasible edge} from the PROVED lines."""
    return {(name, f"{int(site, 16):08x}"): int(edge) for name, site, edge in PROOF.findall(path.read_text(encoding="utf-8"))}


def read_runs(paths, proofs):
    cases, branches = Counter(), {}
    chain, poison, contracts = 0, 0, 0
    for path in paths:
        text = path.read_text(encoding="utf-8")
        if "PASS: in-vitro differential run;" not in text or "FAIL:" in text:
            raise ValueError(f"Incomplete or failed run: {path}")
        mode = "direct"
        for line in text.splitlines():
            match = re.match(r"Installed adapter integration mode=(\d+),", line)
            if match:
                mode = match[1]
            match = re.match(r"PASS: (\w+)\s+(\d+) cases(?: \([^)]*\))?;", line)
            if match:
                cases[(mode, match[1], int(match[2]))] += 1
            match = re.match(r"BRANCH (\w+)\s+VA=([0-9a-f]+) fall=(\d+) taken=(\d+)", line)
            if match:
                key = (match[1], match[2])
                fall, taken = int(match[3]), int(match[4])
                if key in proofs:
                    # Only the proved feasible edge may be observed, and the
                    # harness must have printed its PROVED line.
                    observed, impossible = (fall, taken) if proofs[key] == 0 else (taken, fall)
                    valid = observed > 0 and impossible == 0 and f"PROVED: {key[0]} {key[1]} " in text
                else:
                    valid = fall > 0 and taken > 0
                if key in branches or not valid:
                    raise ValueError(f"Duplicate or uncovered branch: {key} in {path}")
                branches[key] = True
        chain += text.count("PASS: 40250c leaf chain 4096 states;")
        poison += text.count("same inputs, stack poison 31/a7 changes only matrix byte 19")
        contracts += text.count("PASS: division boundaries and disabled-track bypass;")
    return cases, branches, (chain, poison, contracts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("shards", type=Path, nargs="+")
    args = parser.parse_args()
    proofs = read_proofs()
    before, old_branches, old_probes = read_runs([args.baseline], proofs)
    after, branches, probes = read_runs(args.shards, proofs)
    if before != after:
        raise ValueError(f"Case inventory differs: missing={before-after}, extra={after-before}")
    if old_branches.keys() != branches.keys() or old_probes != probes:
        raise ValueError("Branch-site inventory or chain/poison/contract probe count differs")
    registry = Path(__file__).parents[1] / "proxy" / "hook_registry.h"
    names = set(re.findall(r"ES2_HOOK\((\w+),", registry.read_text()))
    direct = {name: count for (mode, name, count), occurrences in after.items() if mode == "direct" and occurrences == 1}
    if set(direct) != names:
        raise ValueError("Direct-case inventory does not cover the registry exactly once")
    print(json.dumps(dict(functions=len(direct), direct_cases=sum(direct.values()),
                         installed_cases=sum(count*n for (mode, _, count), n in after.items() if mode != "direct"),
                         branch_sites=len(branches), chain_runs=probes[0],
                         poison_probes=probes[1], contract_runs=probes[2],
                         result="PASS: unchanged case counts, branch coverage and probes"), indent=2))


if __name__ == "__main__":
    main()
