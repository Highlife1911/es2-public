"""Immutable reference pins for the real preparation tests.

The real tests compare our preparation with the files the original VSHELL
left in the measurement clone. The clone is not immutable: the oracle
regression deploys launch sets into its DATA directory and restores them
afterwards, so a test that reads DATA/ at the wrong moment sees another
mission's files. These pins break that dependence. Each reference file is
identified by size and SHA-256 (hashes only, no bytes; the same values are
documented in docs/reverse-engineering/vshell_preparation.md).

* ``fetch(data_root, relative)`` returns the bytes of a pinned file from the
  first measurement clone that currently holds a file with the pinned hash,
  or ``None``. A clone in the middle of a deployment simply does not match
  and the next one is tried; nothing is ever waited for or written.
* The verdict of a reference comparison never depends on a clone: it is
  decided against the pinned hash (``matches``); ``fetch`` only adds the
  byte-for-byte comparison while a matching copy exists.

Read-only. Stdlib only.
"""
import hashlib
import os
from pathlib import Path

# Measurement clones only; never native-r111 (the primary installation).
CLONES = ('timing-r111', 'timing-r111-p1', 'timing-r111-p2', 'timing-r111-p4', 'timing-r111-p5', 'calibration-r111')

# relative path inside a clone -> (size, sha256)
PINS = {
    # Saved states (SAV directory: written by the installer, never by a run).
    'SAV/GAME_R.SAV': (4763, 'ae19c8e622adf64173d895b31d4ebcacbadbf7667a57fa9d793deacd97b9b2cb'),
    'SAV/GAME_T.SAV': (4322, '1e0cfc3705127b504fafd3a60b92246e4de916113ea24bd0e82ddcec8bf8de0f'),
    # DEMO companions (SAV directory).
    'SAV/script11.dat': (4514, 'dd28822315105ec3054aa54609c13cada1aaef5871f475163b98ee439641cff9'),
    'SAV/missn11.str': (882, '12a693e44050085648cd7f256e2ad48b5bff67193bcb2fcfc57e4b08283ba6fb'),
    'SAV/player11.mec': (151, 'ec5b87a6cdaa1e0d9bf87510c9d99a8db114eac78874b213fc064ce02d30ce68'),
    # C1_01 prepared files: the clone's DATA files between runs. These are the
    # ones a regression replaces and restores.
    'DATA/script.dat': (12286, '61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb'),
    'DATA/MISSION.STR': (939, '4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9'),
    'DATA/mission.var': (2000, '20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc'),
    'DATA/player.mec': (215, 'f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def installs_dir(data_root):
    """The installations beside the extracted CD: ES2_DATA_ROOT is es2's
    build/extracted/cd and the installations are build/installs."""
    return Path(data_root).resolve().parent.parent / 'installs'


def matches(relative, data):
    size, digest = PINS[relative]
    return len(data) == size and sha(data) == digest


def candidates(data_root):
    """Clone directories to try, an explicit ES2_PREPARATION_INSTALL first."""
    override = os.environ.get('ES2_PREPARATION_INSTALL')
    found = [Path(override)] if override else []
    return found + [installs_dir(data_root) / name for name in CLONES]


def fetch(data_root, relative):
    """(bytes, clone path) of the pinned file from the first clone whose copy
    has the pinned size and hash; (None, None) if no clone holds it right now."""
    for clone in candidates(data_root):
        try:
            data = (clone / relative).read_bytes()
        except OSError:
            continue
        if matches(relative, data):
            return data, clone
    return None, None


def require(data_root, relative):
    """Like fetch for a pinned *input* that must exist; names every clone tried."""
    data, clone = fetch(data_root, relative)
    if data is None:
        tried = ', '.join(str(c) for c in candidates(data_root))
        raise AssertionError(f'pinned input {relative} ({PINS[relative][1][:16]}...) is not held by any '
                             f'measurement clone ({tried}); the SAV directory is expected to be unchanged')
    return data, clone
