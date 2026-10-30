# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Read-only acceptance and CAREER sweep. Reports contain hashes, never assets."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from es2shellprep import DEFAULT_WORKER, NAMES, file_entries, generate, sha
import reference_pins as pins


def acceptance(worker, root):
    # Reference files are pinned by hash (reference_pins.py); the clone is read
    # only where a copy with the pinned hash exists, because oracle regressions
    # rewrite its DATA directory while they run.
    cases = [
        ("c1_01", "GAME_R.SAV", 71, "0,0,0,0,0",
         ["DATA/" + name for name in NAMES]),
        ("demo", "GAME_T.SAV", 103, "1,1,0,0,0",
         ["SAV/script11.dat", "SAV/missn11.str", None, "SAV/player11.mec"]),
    ]
    report = []
    for name, state, seed, options, references in cases:
        saved, clone = pins.require(root, "SAV/" + state)
        outputs, provenance = generate(worker, ["--cd-root", root, "--save", clone / "SAV" / state,
                                               "--seed", seed, "--training-options", options])
        checks = []
        for filename, reference in zip(NAMES, references):
            if reference is None:
                offset = provenance["state"]["save_variable_offset"]
                expected = saved[offset:offset + 2000]
                source = "save-bank"
                equal = expected == outputs[filename]
            else:
                # Verdict against the pinned hash; byte comparison as well
                # while a clone holds a copy with that hash.
                expected, holder = pins.fetch(root, reference)
                source = holder.name if holder else "pinned-sha256"
                equal = pins.matches(reference, outputs[filename]) and (
                    expected is None or expected == outputs[filename])
            expected_sha256 = sha(expected) if expected is not None else pins.PINS[reference][1]
            checks.append(dict(file=filename, size=len(outputs[filename]),
                               sha256=sha(outputs[filename]), expected_sha256=expected_sha256,
                               reference_source=source, byte_exact=equal))
        report.append(dict(case=name, checks=checks, provenance=provenance))
        if not all(check["byte_exact"] for check in checks):
            raise AssertionError(json.dumps(report[-1]))
    return report


def sweep(worker, root):
    listing = subprocess.run([str(worker), "--cd-root", str(root), "--list"],
                             capture_output=True, text=True, timeout=60, check=True)
    entries = []
    for line in listing.stdout.splitlines():
        _, group, index, mission = line.split(maxsplit=3)
        entries.append((group, index, mission))
    if len(entries) != 61:
        raise AssertionError(f"CAREER count is {len(entries)}, expected 61")
    report = []
    for group, index, mission in entries:
        entry = dict(group=int(group), index=int(index), mission=mission)
        try:
            outputs, provenance = generate(worker, ["--cd-root", root, "--campaign",
                                                   f"{group}:{index}", "--seed", 0])
            entry.update(success=True, files=file_entries(outputs),
                         state=provenance["state"], trap=None)
        except (RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
            entry.update(success=False, files=[], trap=str(exc))
        report.append(entry)
        print(f"{'PASS' if entry['success'] else 'FAIL'} {group}:{index} {mission}", flush=True)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--worker", type=Path, default=DEFAULT_WORKER)
    p.add_argument("--cd-root", type=Path, default=os.environ.get("ES2_DATA_ROOT") or None)
    p.add_argument("--sweep", action="store_true")
    p.add_argument("--report", type=Path)
    args = p.parse_args()
    if args.cd_root is None:
        print("SKIP: ES2_DATA_ROOT is unset; no original data read")
        return
    root = args.cd_root.resolve()
    if (root / "extracted/cd").is_dir():
        root /= "extracted/cd"
    report = dict(acceptance=acceptance(args.worker, root))
    print("PASS: two preparations, four byte-exact outputs each")
    if args.sweep:
        report["sweep"] = sweep(args.worker, root)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.sweep and not all(row["success"] for row in report["sweep"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
