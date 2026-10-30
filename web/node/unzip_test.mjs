// web/unzip.mjs: the export's stored ZIPs read back; Deflate ZIPs from the pinned Python's
// zipfile (with folder entries, a UTF-8 name, and data descriptors when it writes to a pipe)
// through inflateStream over Node's DecompressionStream; and the refusals: a wrong CRC,
// encryption, another method, ZIP64, a truncated file, an entry over its limit, a Deflate stream
// longer than its size.
//
// Arguments: python.exe
import { spawnSync } from 'node:child_process';
import os from 'node:os';
import zlib from 'node:zlib';
import { zipStored } from '../zip.mjs';
import { MAX_ENTRY_BYTES, ZipError, inflateStream, readZip } from '../unzip.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const [python] = process.argv.slice(2);
const inflateRaw = inflateStream(globalThis);
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const reason = async (bytes, options = { inflateRaw }) => {
  try {
    await readZip(bytes, options);
    return null;
  } catch (e) {
    return e instanceof ZipError ? e.reason : `not a ZipError: ${e.message}`;
  }
};

const a = Uint8Array.from({ length: 3000 }, (_, i) => (i * 7) & 0xff);
const b = new Uint8Array(0);
{
  const zip = zipStored([{ name: 'GAME_0.SAV', bytes: a }, { name: 'GAMEFILE.STR', bytes: b }]);
  const entries = await readZip(zip, { inflateRaw: () => { throw new Error('stored needs no inflate'); } });
  check(entries.length === 2 && entries[0].name === 'GAME_0.SAV' && same(entries[0].bytes, a)
    && entries[1].name === 'GAMEFILE.STR' && entries[1].bytes.length === 0, 'stored: the export read back');
  check((await readZip(zipStored([]), { inflateRaw })).length === 0, 'stored: an empty archive');
}

// Python's zipfile: ZIP_DEFLATED, to a file (sizes in the local header) and to a pipe (flag bit 3).
const PY = `
import sys, zipfile
class Pipe:
    def __init__(self, out): self.out = out
    def write(self, b): self.out.write(b); return len(b)
    def flush(self): self.out.flush()
a = bytes((i * 7) & 0xff for i in range(3000))
target = Pipe(sys.stdout.buffer) if sys.argv[1] == 'pipe' else open(sys.argv[2], 'wb')
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('SAV/', b'')
    z.writestr('SAV/GAME_0.SAV', a)
    z.writestr('SAV/später.txt', b'x' * 100)
    z.writestr(zipfile.ZipInfo('SAV/GAMEFILE.STR'), b'', compress_type=zipfile.ZIP_STORED)
`;
for (const mode of ['pipe', 'file']) {
  let zip;
  if (mode === 'pipe') {
    zip = new Uint8Array(spawnSync(python, ['-I', '-c', PY, 'pipe']).stdout);
  } else {
    const file = `${process.env.TEST_TMPDIR || os.tmpdir()}/deflate.zip`;
    spawnSync(python, ['-I', '-c', PY, 'file', file]);
    zip = new Uint8Array((await import('node:fs')).readFileSync(file));
  }
  const flags = new DataView(zip.buffer, zip.byteOffset).getUint16(6, true);
  check(mode === 'pipe' ? (flags & 8) !== 0 : (flags & 8) === 0, `${mode}: data descriptor flag ${flags}`);
  const entries = await readZip(zip, { inflateRaw });
  check(entries.map((e) => e.name).join('|') === 'SAV/GAME_0.SAV|SAV/später.txt|SAV/GAMEFILE.STR'
    && same(entries[0].bytes, a) && entries[1].bytes.length === 100 && entries[2].bytes.length === 0,
  `${mode}: Deflate entries, the folder left out (${entries.map((e) => e.name).join('|')})`);
}

// The refusals, on the export's ZIP with one field changed.
const base = () => zipStored([{ name: 'GAME_0.SAV', bytes: a }]);
const central = (zip) => zip.length - 22 - (46 + 'GAME_0.SAV'.length);
const set16 = (zip, at, v) => { new DataView(zip.buffer).setUint16(at, v, true); return zip; };
const set32 = (zip, at, v) => { new DataView(zip.buffer).setUint32(at, v >>> 0, true); return zip; };
{
  const z = base();
  z[30 + 'GAME_0.SAV'.length + 5] ^= 1;
  check(await reason(z) === 'crc', 'a changed byte fails its CRC');
  check(await reason(set16(base(), central(base()) + 8, 1)) === 'unsupported', 'encryption');
  check(await reason(set16(base(), central(base()) + 10, 12)) === 'unsupported', 'bzip2 (method 12)');
  check(await reason(set16(base(), base().length - 12, 0xffff)) === 'unsupported', 'ZIP64 counts');
  check(await reason(base().subarray(0, 100)) === 'format', 'a truncated file');
  check(await reason(Uint8Array.of(1, 2, 3)) === 'format', 'no ZIP at all');
  check(await reason(set32(base(), central(base()) + 24, MAX_ENTRY_BYTES + 1)) === 'size', 'an entry over the limit');
  check(await reason(set32(base(), central(base()) + 42, 1)) === 'format', 'a local header offset off its header');
}
{
  // A Deflate stream that inflates past the size the directory names.
  const long = zlib.deflateRawSync(new Uint8Array(100000));
  let thrown = null;
  try { await inflateRaw(long, 1000); } catch (e) { thrown = e; }
  check(thrown instanceof ZipError && thrown.reason === 'format', 'inflate stops at its limit');
  check((await inflateRaw(zlib.deflateRawSync(a), a.length)).every((x, i) => x === a[i]), 'inflate within its limit');
  let broken = null;
  try { await inflateRaw(Uint8Array.of(0xff, 0xff, 0xff), 10); } catch (e) { broken = e; }
  check(broken instanceof ZipError && broken.reason === 'format', 'a broken Deflate stream');
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
