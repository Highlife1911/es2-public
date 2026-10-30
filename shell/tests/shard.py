"""Bazel test sharding for the shell real tests. Standard library only.

A real test builds its cases from seeded random streams and runs each
against the mapped original (one worker process per run). The streams are
drawn in full in every shard, so a case is the same case whatever the shard
count; only the expensive part (the worker run) is executed by the shard
that owns the case: case or run number i belongs to shard i % total.
Without Bazel's variables there is one shard and every case runs, so a
direct run is the whole test.

    import shard
    for i, item in enumerate(items):
        if shard.owns(i):
            run(item)

Union-wide checks that depend only on our side (coverage counters of the
seeded sequences) run in a reserved shard over every case; a comparison with
the original runs only in the shard that owns the case.
"""
import os
from pathlib import Path


def position():
    """(index, total) from TEST_SHARD_INDEX / TEST_TOTAL_SHARDS. Touches
    TEST_SHARD_STATUS_FILE (Bazel's sign that the test honours sharding). A
    malformed value is an error."""
    total, index = os.environ.get('TEST_TOTAL_SHARDS'), os.environ.get('TEST_SHARD_INDEX')
    if total is None and index is None:
        return 0, 1
    if not (total and index and total.isdigit() and index.isdigit() and 0 <= int(index) < int(total)):
        raise AssertionError(f'invalid Bazel shard variables: TEST_TOTAL_SHARDS={total!r} TEST_SHARD_INDEX={index!r}')
    status = os.environ.get('TEST_SHARD_STATUS_FILE')
    if status:
        Path(status).write_bytes(b'')
    return int(index), int(total)


INDEX, TOTAL = position()


def owns(number, reserve=0):
    """Whether case or run `number` (counted from 0 in a fixed order) runs in this shard.
    With `reserve` > 0 the first `reserve` shards own no case: they do the union-wide
    work that needs no worker (see `reserved`)."""
    if TOTAL == 1:
        return True
    work = TOTAL - reserve
    if work < 1:
        raise AssertionError(f'{TOTAL} shards with {reserve} reserved')
    return INDEX >= reserve and number % work == INDEX - reserve


def reserved(reserve=1):
    """True in the shards that own no case (always False without sharding)."""
    return TOTAL > 1 and INDEX < reserve


def label():
    return f'shard {INDEX + 1}/{TOTAL}' if TOTAL > 1 else 'unsharded'


_RAN = []


def ran(case):
    """Notes that this shard executed the comparison of `case` (a string that is the
    same in every shard count)."""
    _RAN.append(str(case))


def finish(test_name):
    """Prints and stores the executed cases (evidence that the shards' union is the
    unsharded test: TEST_UNDECLARED_OUTPUTS_DIR/<test>-shard-<index>.txt)."""
    import hashlib
    body = ''.join(c + '\n' for c in _RAN)
    print(f'{test_name} {label()}: {len(_RAN)} compared cases, sha256 {hashlib.sha256(body.encode()).hexdigest()[:16]}',
          flush=True)
    out = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    if out:
        Path(out, f'{test_name}-shard-{INDEX}.txt').write_bytes(body.encode('utf-8'))
