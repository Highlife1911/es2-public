"""Writes the launch set `c1_01-legacy` from a measurement clone's DATA directory.

The 19 registered suites without a `launch` entry ran on the clones' prepared C1_01 state: DATA\\script.dat,
player.mec, mission.var and MISSION.STR as the clones kept them (they are restored after every run). The own
process has no clone, so the replay driver (replay_suite.py) wants that state as a launch set under
`es2-data\\oracle\\launch\\c1_01-legacy` (schema 1, the same four members SuiteLaunch.psm1 verifies).

  legacy_launch_set.py <install DATA directory> <oracle\\launch\\c1_01-legacy> [--dry-run]

The four files are only read; the destination must not exist (launch sets are immutable) and the hashes must be the
ones of the legacy state (measured on timing-r111 and -p1, -p2, -p4, -p5: identical on all five, not on -p3). A clone
whose files differ is refused: it is not the state the goldens were recorded on.
"""
import hashlib
import json
import os
import shutil
import sys
import time

MEMBERS = {
    "script.dat": "61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb",
    "player.mec": "f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984",
    "mission.var": "20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc",
    "MISSION.STR": "4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9",
}
NAME = "c1_01-legacy"


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def build(source, destination, dry_run=False, expected=None):
    expected = expected or MEMBERS
    name = os.path.basename(os.path.normpath(destination))
    problems = []
    entries = []
    for member, want in expected.items():
        path = os.path.join(source, member)
        if not os.path.isfile(path):
            problems.append("%s is missing" % member)
            continue
        got = sha256(path)
        if got != want:
            problems.append("%s has SHA-256 %s, expected %s" % (member, got, want))
        entries.append({"file": member, "size": os.path.getsize(path), "sha256": got})
    if os.path.exists(destination):
        problems.append("%s exists; launch sets are immutable" % destination)
    if problems:
        raise SystemExit("legacy launch set refused: " + "; ".join(problems))
    manifest = {"schema": 1, "name": name, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "files": entries,
                "provenance": {"kind": "legacy-clone-state", "source": os.path.abspath(source),
                               "note": "the prepared C1_01 DATA files of the measurement clones (T-61); no process observed"}}
    if dry_run:
        return manifest
    os.makedirs(destination)
    for member in expected:
        shutil.copyfile(os.path.join(source, member), os.path.join(destination, member))
    with open(os.path.join(destination, "manifest.json"), "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, indent=2)
    return manifest


def main(argv):
    args = [a for a in argv[1:] if a != "--dry-run"]
    if len(args) != 2:
        print(__doc__)
        return 2
    manifest = build(args[0], args[1], "--dry-run" in argv)
    print("%s %s: %d members" % ("would write" if "--dry-run" in argv else "wrote", args[1], len(manifest["files"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
