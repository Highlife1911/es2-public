# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Client of the VSHELL memory worker's slot mode. Standard library only.

`es2shellprep --op ...` runs VSHELL's own save-slot, load and new-career
functions (0x40e933, 0x40eaaa, 0x40e8a5) on a memory disk: the save
directory and the DATA launch files go in as `--file NAME PATH`, every
original write lands on that disk, and the whole disk comes back in an
`ES2D` packet (docs/reference-game/shell_invitro.md, "Slot mode").
This module writes the inputs into a scratch directory, runs the worker and
decodes the packet; it never writes anywhere else.

    run(worker, cd_root, disk={'sav\\gamefile.str': bytes, ...}, ops=['save:10'],
        save=bytes|None, language='G', mode=1, rng=bytes|None, emit=False,
        not_loaded=False, scratch=Path) -> dict(metadata, disk, emitted)
"""
import json
from pathlib import Path
import struct
import subprocess


def decode(packet):
    if packet[:4] != b'ES2D':
        raise ValueError('worker did not return an ES2D packet')
    at = 4

    def word():
        nonlocal at
        if len(packet) - at < 4:
            raise ValueError('truncated ES2D packet')
        value, = struct.unpack_from('<I', packet, at)
        at += 4
        return value

    def block(limit=1 << 22):
        nonlocal at
        size = word()
        if size > limit or size > len(packet) - at:
            raise ValueError('invalid ES2D block length')
        value = packet[at:at + size]
        at += size
        return value

    metadata = json.loads(block(1 << 16))
    count = word()
    if count > 32:
        raise ValueError('too many memory-disk files')
    disk = {}
    for _ in range(count):
        name = block(64).decode('ascii')
        disk[name] = block()
    emitted = block()
    if at != len(packet):
        raise ValueError('trailing ES2D data')
    return dict(metadata=metadata, disk=disk, emitted=emitted if metadata.get('emitted_save') else None)


def run(worker, cd_root, disk, ops, scratch, save=None, language='G', mode=1, rng=None, emit=False,
        not_loaded=False, timeout=120):
    scratch = Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    argv = [str(worker), '--cd-root', str(cd_root), '--language', language, '--mode', str(mode), '--pipe']
    for index, (name, data) in enumerate(disk.items()):
        path = scratch / f'disk-{index:02d}.bin'
        path.write_bytes(data)
        argv += ['--file', name, str(path)]
    if save is not None:
        (scratch / 'state.sav').write_bytes(save)
        argv += ['--save', str(scratch / 'state.sav')]
    if rng is not None:
        (scratch / 'state.rng').write_bytes(rng)
        argv += ['--rng-state', str(scratch / 'state.rng')]
    if emit:
        argv.append('--emit-save')
    if not_loaded:
        argv.append('--not-loaded')
    for op in ops:
        argv += ['--op', op]
    run = subprocess.run(argv, capture_output=True, timeout=timeout)
    if run.returncode:
        raise RuntimeError(f'worker exited {run.returncode}: '
                           f'{run.stderr.decode("utf-8", "replace").strip()[-2000:]}')
    return decode(run.stdout)
