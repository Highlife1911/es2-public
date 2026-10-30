# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (measured)
"""Opt-in static-data RNG check. Never executes the original game or saves its bytes."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys


EXE_SHA256 = "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"
SEED_SHA256 = "8b375c44b10287cc57ac787f3cbcc73afe21160b0a39ca82c927ac9acc0d13fc"
EXPECTED = (0xea0b, 0x2ab1, 0x91b0, 0x8105, 0xa50a, 0xdb51, 0x4d01, 0xd996)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, help="Path to our built rng_real_helper executable")
    args = parser.parse_args()
    root_value = os.environ.get("ES2_DATA_ROOT")
    if not root_value:
        print("SKIP: ES2_DATA_ROOT is not set; no original data was read")
        return 0
    if args.helper is None or not args.helper.is_file():
        raise ValueError("--helper must name our built core/rng_real_helper executable")
    root = Path(root_value).resolve(strict=True)
    candidates = [root / "VER95/DBSIM.EXE", root / "extracted/cd/VER95/DBSIM.EXE"]
    candidates = [path.resolve() for path in candidates if path.is_file()]
    if len(candidates) != 1 or not candidates[0].is_relative_to(root):
        raise ValueError("ES2_DATA_ROOT must contain exactly one VER95/DBSIM.EXE or extracted/cd/VER95/DBSIM.EXE")
    executable = candidates[0]
    with executable.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != EXE_SHA256:
            raise ValueError("DBSIM.EXE SHA-256 does not match rng.md")
        stream.seek(0xa5b40)
        seed = stream.read(112)
    if len(seed) != 112 or hashlib.sha256(seed).hexdigest() != SEED_SHA256:
        raise ValueError("RNG table at file offset 0xa5b40 does not match rng.md")
    helper = args.helper.resolve(strict=True)
    if helper == executable:
        raise ValueError("The helper must be our executable, never the original DBSIM.EXE")
    result = subprocess.run([str(helper)], input=seed, capture_output=True, check=False, timeout=30)
    if result.returncode:
        raise ValueError(f"Own RNG helper failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
    values = tuple(int(token, 16) for token in result.stdout.decode("ascii").split())
    if values != EXPECTED:
        raise ValueError("Own RNG outputs differ from the eight static predictions in rng.md")
    print("PASS: DBSIM/table SHA-256 verified; first eight outputs match static predictions (not a live oracle comparison)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
