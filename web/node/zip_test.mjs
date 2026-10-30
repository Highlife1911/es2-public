// web/zip.mjs: CRC-32 check values, and stored ZIPs read back by the pinned Python's zipfile
// (names, bytes, CRCs, method, flags and dates must equal what was written; zipfile checks each
// CRC while reading), an empty archive, determinism and the refusals.
//
// Arguments: python.exe
import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { crc32, zipStored } from '../zip.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const throws = (fn, label) => {
  try { fn(); check(false, `${label}: no error`); } catch (e) { check(e instanceof RangeError, `${label}: ${e.message}`); }
};

const [python] = process.argv.slice(2);
const tmp = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR ?? os.tmpdir(), 'zip_test_'));
const ascii = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));

// ---- CRC-32 --------------------------------------------------------------------------------
check(crc32(ascii('123456789')) === 0xcbf43926, 'CRC-32 check value of "123456789"');
check(crc32(new Uint8Array(0)) === 0, 'CRC-32 of nothing');
check(crc32(ascii('The quick brown fox jumps over the lazy dog')) === 0x414fa339, 'CRC-32 of the fox');
{
  const data = crypto.randomBytes(5000);
  check(crc32(data.subarray(1234), crc32(data.subarray(0, 1234))) === crc32(data), 'CRC-32 continues across pieces');
}

// ---- read back by zipfile ------------------------------------------------------------------
const READER = `
import base64, json, sys, zipfile, zlib
with zipfile.ZipFile(sys.argv[1]) as z:
    bad = z.testzip()
    out = []
    for i in z.infolist():
        data = z.read(i)
        out.append({"name": i.filename, "crc": i.CRC, "zlib_crc": zlib.crc32(data), "size": i.file_size,
                    "compressed": i.compress_size, "method": i.compress_type, "flags": i.flag_bits,
                    "date_time": list(i.date_time), "offset": i.header_offset,
                    "data": base64.b64encode(data).decode("ascii")})
    print(json.dumps({"bad": bad, "entries": out, "comment": z.comment.decode("latin-1")}))
`;
function readBack(bytes, label) {
  const file = path.join(tmp, `${label}.zip`);
  fs.writeFileSync(file, bytes);
  const r = spawnSync(python, ['-I', '-c', READER, file], { encoding: 'utf8' });
  if (r.status !== 0) {
    check(false, `${label}: zipfile failed: ${r.stderr}`);
    return null;
  }
  return JSON.parse(r.stdout);
}

function roundTrip(label, entries, options, dateTime) {
  const zip = zipStored(entries, options);
  check(Buffer.compare(Buffer.from(zip), Buffer.from(zipStored(entries, options))) === 0, `${label}: deterministic`);
  const got = readBack(zip, label);
  if (!got) return;
  check(got.bad === null, `${label}: testzip finds no bad CRC`);
  check(got.entries.length === entries.length, `${label}: ${entries.length} entries`);
  entries.forEach((e, i) => {
    const g = got.entries[i] ?? {};
    const data = Buffer.from(g.data ?? '', 'base64');
    const name = e.name.replace(/\\/g, '/');
    check(g.name === name, `${label}: name ${i} (${g.name})`);
    check(Buffer.compare(data, Buffer.from(e.bytes)) === 0, `${label}: bytes of ${name}`);
    check(g.crc === crc32(e.bytes) && g.zlib_crc === g.crc, `${label}: CRC of ${name}`);
    check(g.method === 0 && g.size === e.bytes.length && g.compressed === e.bytes.length, `${label}: ${name} stored`);
    check(g.flags === (/[^\x00-\x7f]/.test(name) ? 0x800 : 0), `${label}: flags of ${name}`);
    check(JSON.stringify(g.date_time) === JSON.stringify(dateTime), `${label}: date of ${name} (${g.date_time})`);
  });
}

const saves = [
  { name: 'GAMEFILE.STR', bytes: crypto.randomBytes(318) },
  { name: 'GAME_0.SAV', bytes: crypto.randomBytes(5133) },
  { name: 'GAME_R.SAV', bytes: new Uint8Array(0) },
  { name: 'script11.dat', bytes: crypto.randomBytes(70000) },
  { name: 'missn0.str', bytes: Uint8Array.from({ length: 939 }, (_, i) => i & 0xff) },
  { name: 'player0.mec', bytes: crypto.randomBytes(215) },
];
roundTrip('saves', saves, {}, [1980, 1, 1, 0, 0, 0]);
roundTrip('dated', saves.slice(0, 2), { date: new Date(2026, 9, 26, 13, 45, 31) }, [2026, 10, 26, 13, 45, 30]);
roundTrip('names', [{ name: 'SAV\\GAME_1.SAV', bytes: ascii('x') }, { name: 'Späté.txt', bytes: ascii('y') }], {},
  [1980, 1, 1, 0, 0, 0]);
{
  const empty = zipStored([]);
  check(empty.length === 22 && readBack(empty, 'empty')?.entries.length === 0, 'empty archive: an end record only');
}
throws(() => zipStored([{ name: '', bytes: new Uint8Array(1) }]), 'empty name');
throws(() => zipStored([], { date: new Date(1979, 11, 31) }), 'date before 1980');

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
