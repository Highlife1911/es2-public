"""Fills the @es2_data repository from the extracted reference CD.

Called by the repository rule in data_inputs.bzl, never by a test:

    python -I -B data_inputs.py <cd dir> <reference_hashes.txt> <output dir>

* Every row of data/reference_hashes.txt below ``extracted/cd/`` (DBSIM.EXE,
  VSHELL.EXE, CW3220.DLL and the other key binaries) whose file exists in
  <cd dir> is hashed; a SHA-256 that differs from the table is an error (exit
  2, nothing usable is written). Matching files are copied to the same
  relative path below <output dir> and their paths are printed, one per line.
* ``cd.sha256`` in <output dir> lists the SHA-256 of every file below <cd dir>
  (sorted, ``/`` separators), so a test that depends on it is invalidated by
  any change of any CD file.

Second form, for the @es2_recorded repository:

    python -I -B data_inputs.py --recorded <data root> <output dir> <tree>...

writes ``recorded.sha256`` in <output dir>: the SHA-256 of every file below
each <tree> (relative to <data root>, e.g. ``oracle/golden``), sorted, so a
test that depends on it is invalidated by any change of the recorded outputs
it reads. A missing tree is listed as absent.

The output directory is Bazel's external repository (output base), never the
source tree. Read-only on <cd dir> and <data root>. Standard library only.
"""
import hashlib
import os
import sys
from pathlib import Path

CD_PREFIX = 'extracted/cd/'


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def pinned(table):
    """[(relative CD path, sha256)] for the table rows below extracted/cd/."""
    rows = []
    for line in Path(table).read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        value, relative = line.split(None, 1)
        relative = relative.strip().replace('\\', '/')
        if relative.startswith(CD_PREFIX):
            rows.append((relative[len(CD_PREFIX):], value.lower()))
    return rows


def manifest(cd):
    """'<sha256>  <relative path>' for every file below cd, sorted by path."""
    lines = []
    for directory, subdirectories, files in os.walk(cd):
        subdirectories.sort()
        for name in files:
            path = Path(directory) / name
            lines.append((path.relative_to(cd).as_posix(), sha256_file(path)))
    return ['%s  %s' % (value, relative) for relative, value in sorted(lines)]


def recorded(argv):
    """The --recorded form: recorded.sha256 over the named trees of the data root."""
    if len(argv) < 5:
        print('usage: data_inputs.py --recorded <data root> <output dir> <tree>...', file=sys.stderr)
        return 2
    root, out = Path(argv[2]), Path(argv[3])
    lines = ['# SHA-256 of the recorded outputs under the es2-data root (generated, not committed).']
    for tree in argv[4:]:
        base = root / tree
        if not base.is_dir():
            lines.append('# absent: %s' % tree)
            continue
        lines += ['%s  %s/%s' % (value, tree, relative)
                  for value, relative in (line.split('  ', 1) for line in manifest(base))]
    (out / 'recorded.sha256').write_bytes(('\n'.join(lines) + '\n').encode('utf-8'))
    return 0


def main(argv):
    if len(argv) > 1 and argv[1] == '--recorded':
        return recorded(argv)
    if len(argv) != 4:
        print('usage: data_inputs.py <cd dir> <reference_hashes.txt> <output dir>', file=sys.stderr)
        return 2
    cd, table, out = Path(argv[1]), argv[2], Path(argv[3])
    problems, copies = [], []
    for relative, expected in pinned(table):
        source = cd / relative
        if not source.is_file():
            continue
        data = source.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            problems.append('%s: SHA-256 %s, reference_hashes.txt pins %s' % (source, actual, expected))
            continue
        copies.append((relative, data))
    if problems:
        for problem in problems:
            print('MISMATCH ' + problem, file=sys.stderr)
        return 2
    for relative, data in copies:
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(relative)
    lines = ['# SHA-256 of every file of the reference CD at ES2_DATA_ROOT (generated, not committed).']
    lines += manifest(cd)
    (out / 'cd.sha256').write_bytes(('\n'.join(lines) + '\n').encode('utf-8'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
