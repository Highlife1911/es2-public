"""Extract a mountable ISO9660 data track from the supplied raw Mode-1 CD image.
The source stays untouched. Executable bytes are never patched.
"""
import argparse, hashlib, json, struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('destination', type=Path)
a = p.parse_args()
if a.source.resolve() == a.destination.resolve():
    raise SystemExit('Source and destination must differ')
if a.destination.exists():
    raise SystemExit('Destination already exists; refusing overwrite')
sync = b'\0' + b'\xff' * 10 + b'\0'
with a.source.open('rb') as source:
    source.seek(16 * 2352)
    descriptor = source.read(2352)
    if descriptor[:12] != sync or descriptor[15] != 1 or descriptor[16:23] != b'\x01CD001\x01':
        raise SystemExit('Expected raw 2352-byte Mode-1 ISO9660 CD')
    pvd = descriptor[16:2064]
    blocks = struct.unpack_from('<I', pvd, 80)[0]
    if blocks != struct.unpack_from('>I', pvd, 84)[0] or struct.unpack_from('<H', pvd, 128)[0] != 2048:
        raise SystemExit('Invalid ISO volume dimensions')
    if blocks * 2352 > a.source.stat().st_size:
        raise SystemExit('Truncated raw image')
    source.seek(0)
    sha, md5, sha1 = hashlib.sha256(), hashlib.md5(), hashlib.sha1()
    with a.destination.open('xb') as target:
        for index in range(blocks):
            sector = source.read(2352)
            if len(sector) != 2352 or sector[:12] != sync or sector[15] != 1:
                raise SystemExit(f'Invalid Mode-1 sector at {index}; discard incomplete destination')
            data = sector[16:2064]
            target.write(data)
            for digest in (sha, md5, sha1): digest.update(data)
print(json.dumps({'source': str(a.source), 'destination': str(a.destination), 'sectors': blocks,
                  'size': blocks*2048, 'sha256': sha.hexdigest(), 'sha1': sha1.hexdigest(),
                  'md5': md5.hexdigest(), 'volume': pvd[40:72].decode('ascii').strip()}, indent=2))