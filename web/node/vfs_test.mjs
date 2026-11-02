// web/vfs.mjs over vfs.wasm on a synthetic site whose gamefiles.zst zstd_pack compresses: its
// decode and SHA-256 check (a corrupted, other, short or unsized frame is refused), a VOLN
// archive's member and rest parts, sparse mappings whose unfilled bytes hold the poison pattern, and the overlay (write, delete with tombstones, rename and copy as part references,
// directories, persistence, the language step).
//
// Arguments: vfs.js zstd_pack.exe
import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { Vfs, VfsError, POISON, decodeGameFiles, keyOf, normalize } from '../vfs.mjs';
import { loadCodecFrom, openSite } from './vfs_site.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const equal = (a, b) => a.length === b.length && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
const throwsCode = (fn, code, label) => {
  try {
    fn();
    check(false, `${label}: no error`);
  } catch (e) {
    check(e instanceof VfsError && [code].flat().includes(e.code), `${label}: ${e.message}`);
  }
};

const [vfsJs, zstdPack] = process.argv.slice(2);
const codec = await loadCodecFrom(vfsJs);
const tmp = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR ?? os.tmpdir(), 'vfs_test_'));
const site = path.join(tmp, 'site');
fs.mkdirSync(site, { recursive: true });

let seed = 12345;
const noise = (n) => {
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; out[i] = (seed >> 13) & 0xff; }
  return out;
};
const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

// A VOLN archive (docs/formats/vol.md): two directories, members [dir, name, payload].
function volArchive(members) {
  const dirs = Buffer.from('a\\\0snd\\\0', 'latin1');
  const head = 18 + dirs.length + 18 * members.length;
  const out = [];
  const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); return b; };
  const u16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); return b; };
  out.push(Buffer.from('VOLN'), Buffer.from([1, 0, 0, 0, 5, 2]), u16(dirs.length), dirs,
    u16(members.length), u32(18 * members.length));
  const blocks = [];
  const payloads = [];
  let at = head;
  for (const [dir, name, payload] of members) {
    const record = Buffer.alloc(18);
    record.write(name, 0, 'latin1');
    record[13] = dir;
    record.writeUInt32LE(at, 14);
    out.push(record);
    blocks.push(Buffer.from([2]), u32(payload.length), u32(0x1234567), Buffer.from(payload),
      Buffer.from([payload[payload.length - 1]]));
    payloads.push([at + 9, payload.length]);
    at += payload.length + 10;
  }
  return { bytes: new Uint8Array(Buffer.concat([...out, ...blocks])), payloads };
}

// Part list of a file: each [ranges...] becomes one part, its bytes appended to the content once.
const chunks = [];
const placed = new Map();
let contentLength = 0;
function parts(bytes, rangesList) {
  return rangesList.map((ranges) => {
    const bytesOf = Buffer.concat(ranges.map(([o, n]) => Buffer.from(bytes.subarray(o, o + n))));
    const h = sha(bytesOf);
    if (!placed.has(h)) {
      placed.set(h, contentLength);
      chunks.push(bytesOf);
      contentLength += bytesOf.length;
    }
    const part = { sha256: h, length: bytesOf.length, at: placed.get(h) };
    if (ranges.length === 1) part.offset = ranges[0][0];
    else part.ranges = ranges;
    return part;
  });
}
const whole = (bytes) => (bytes.length ? parts(bytes, [[[0, bytes.length]]]) : []);

const plain = noise(5000);
const member1 = noise(3000), member2 = noise(700), member3 = plain.slice(0, 1200);
const vol = volArchive([[0, 'ONE.DAT', member1], [1, 'TWO.WAV', member2], [0, 'THREE.DAT', member3]]);
const volRest = [];
{
  let at = 0;
  for (const [o, n] of vol.payloads) { volRest.push([at, o - at]); at = o + n; }
  volRest.push([at, vol.bytes.length - at]);
}
const volParts = parts(vol.bytes, [...vol.payloads.map((r) => [r]), volRest]);
const errorGer = new TextEncoder().encode('fehler');
const missionGer = new TextEncoder().encode('mission');
const manifest = {
  format: 1,
  directories: ['C:\\', 'C:\\ES2', 'C:\\ES2\\DATA', 'C:\\ES2\\SAV', 'C:\\ES2\\VOL', 'D:\\', 'D:\\AVI', 'D:\\EMPTY'],
  files: [
    { path: 'D:\\DATA\\PLAIN.DAT', size: plain.length, flags: [], parts: whole(plain) },
    { path: 'C:\\ES2\\DATA\\PLAIN.DAT', size: plain.length, flags: [], parts: whole(plain) },
    { path: 'C:\\ES2\\DATA\\EMPTY.CFG', size: 0, flags: [], parts: [] },
    { path: 'D:\\VOL\\TEST.VOL', size: vol.bytes.length, flags: ['vol'], parts: volParts },
    { path: 'D:\\AVI\\MOVIE.AVI', size: 123456, flags: ['media', 'no-bytes'], parts: [] },
    { path: 'D:\\GERMAN\\ERROR.GER', size: errorGer.length, flags: [], parts: whole(errorGer) },
    { path: 'D:\\GERMAN\\MISSION.GER', size: missionGer.length, flags: [], parts: whole(missionGer) },
    { path: 'D:\\GERMAN\\BILLBRD\\LOAD1.BMP', size: 1200, flags: [], parts: whole(member3) },
  ],
  languages: {
    G: {
      folder: 'GERMAN',
      directories: ['C:\\ES2\\GERMAN', 'C:\\ES2\\GERMAN\\BILLBRD'],
      copies: [['C:\\ES2\\GERMAN\\BILLBRD\\LOAD1.BMP', 'D:\\GERMAN\\BILLBRD\\LOAD1.BMP'],
        ['C:\\ES2\\GERMAN\\ERROR.GER', 'D:\\GERMAN\\ERROR.GER'], ['C:\\ES2\\GERMAN\\MISSION.GER', 'D:\\GERMAN\\MISSION.GER'],
        ['C:\\ES2\\ERROR.STR', 'D:\\GERMAN\\ERROR.GER'], ['C:\\ES2\\DATA\\MISSION.STR', 'D:\\GERMAN\\MISSION.GER']],
      writes: { 'C:\\ES2\\DATA\\LANGUAGE.CFG': 'G', 'C:\\ES2\\DATA\\DRIVE.CFG': 'D:\r\nC:\\ES2\r\n' },
    },
  },
};

// gamefiles.zst: the content in one frame, packed by zstd_pack as tools/web/build_site.py does.
const content = Buffer.concat(chunks);
fs.writeFileSync(path.join(tmp, 'content.bin'), content);
fs.writeFileSync(path.join(tmp, 'jobs.txt'), `0\t${content.length}\t${path.join(tmp, 'content.bin')}\t${path.join(site, 'gamefiles.zst')}\n`);
const packed = spawnSync(zstdPack, ['-c', '19', path.join(tmp, 'jobs.txt')], { encoding: 'utf8' });
check(packed.status === 0, `zstd_pack: ${packed.stderr}`);
const gamefiles = new Uint8Array(fs.readFileSync(path.join(site, 'gamefiles.zst')));
manifest.gamefiles = { path: 'gamefiles.zst', size: gamefiles.length, sha256: sha(gamefiles), length: content.length,
  content_sha256: sha(content) };
fs.writeFileSync(path.join(site, 'manifest.json'), JSON.stringify(manifest));

// Paths.
check(normalize('d:/vol//./x/../Test.vol') === 'D:\\vol\\Test.vol','normalize keeps the case of names');
check(keyOf('d:/vol//./x/../Test.vol') === 'D:\\VOL\\TEST.VOL', 'key: separators, dots and case');
check(keyOf('c:') === 'C:\\' && keyOf('C:\\ES2\\') === 'C:\\ES2', 'key: roots and trailing separators');
throwsCode(() => keyOf('VOL\\X'), 'bad-path', 'a relative path');

// Decode and hash check.
check(equal(decodeGameFiles(codec, manifest, gamefiles), content), 'gamefiles.zst decoded');
{
  const flipped = gamefiles.slice();
  flipped[flipped.length >> 1] ^= 0x55;
  throwsCode(() => decodeGameFiles(codec, manifest, flipped), ['gamefiles-corrupt', 'gamefiles-hash'], 'a changed byte');
  const other = zlib.zstdCompressSync(Buffer.from(noise(content.length)), { pledgedSrcSize: content.length });
  throwsCode(() => decodeGameFiles(codec, manifest, other), 'gamefiles-hash', 'another content of the same length');
  throwsCode(() => decodeGameFiles(codec, manifest, gamefiles.subarray(0, gamefiles.length - 3)), 'gamefiles-corrupt',
    'a short file');
  throwsCode(() => decodeGameFiles(codec, { gamefiles: { ...manifest.gamefiles, length: content.length + 1 } }, gamefiles),
    'gamefiles-size', 'another content length');
  const nosize = zlib.zstdCompressSync(content, { params: { [zlib.constants.ZSTD_c_contentSizeFlag]: 0 } });
  throwsCode(() => decodeGameFiles(codec, manifest, nosize), 'gamefiles-corrupt', 'a frame without a content size');
  throwsCode(() => decodeGameFiles(codec, { files: [] }, gamefiles), 'bad-manifest', 'a manifest without gamefiles');
  check(codec.sha256(content) === manifest.gamefiles.content_sha256, 'the codec hash equals node:crypto');
}

// Parts in the content.
const persisted = [];
let vfs = openSite(site, codec, { poison: true, onPersist: (k, e) => persisted.push([k, e && e.kind]) });
check(equal(vfs.readAll('d:\\data\\plain.dat'), plain), 'plain file read');
check(vfs.partLog.length === 1, 'one part used');
check(equal(vfs.readAll('C:\\ES2\\DATA\\PLAIN.DAT'), plain) && vfs.partLog.length === 1, 'the installation copy shares the part');
check(equal(vfs.read('D:\\DATA\\PLAIN.DAT', 4990, 100), plain.subarray(4990)), 'a read at the end is short');
check(vfs.read('D:\\DATA\\PLAIN.DAT', 6000, 10).length === 0, 'a read beyond the end is empty');
check(vfs.readAll('C:\\ES2\\DATA\\EMPTY.CFG').length === 0, 'an empty file has no part');
throwsCode(() => new Vfs({ manifest: { files: [{ path: 'D:\\X', size: 10, parts: [] }] } }),
  'bad-manifest', 'parts that do not cover the file');
throwsCode(() => new Vfs({ manifest: { files: [{ path: 'D:\\X', size: 10, parts: [{ sha256: 'f'.repeat(64), length: 10,
  at: content.length - 5, offset: 0 }] }] }, content }), 'bad-manifest', 'a part beyond the content');
{
  const v = openSite(site, codec);
  v.loadOverlay([['C:\\ES2\\SAV\\OLD.DAT', { kind: 'parts', name: 'C:\\ES2\\SAV\\OLD.DAT', size: 4, flags: [],
    parts: [{ sha256: 'f'.repeat(64), length: 4, ranges: [[0, 4]] }] }]]);
  throwsCode(() => v.readAll('C:\\ES2\\SAV\\OLD.DAT'), 'part-missing', 'a reference to a part the content lacks');
}

// Media entries: existence and size only.
check(vfs.stat('D:\\AVI\\MOVIE.AVI')?.size === 123456, 'a movie has its size');
throwsCode(() => vfs.read('D:\\AVI\\MOVIE.AVI', 0, 4), 'media-bytes', 'a movie read');
throwsCode(() => vfs.createMapping('D:\\AVI\\MOVIE.AVI'), 'media-bytes', 'a movie mapping');

// The VOL: member parts, the rest part and the poisoned mapping.
{
  const v = openSite(site, codec, { poison: true });
  const map = v.createMapping('D:\\VOL\\TEST.VOL');
  const poisoned = (o, n) => map.view.subarray(o, o + n).every((b, i) => b === POISON[(o + i) & 3]);
  check(poisoned(0, vol.bytes.length), 'a new mapping is poison throughout');
  const [o2, n2] = vol.payloads[1];
  map.ensure(o2, n2);
  check(v.partLog.length === 1 && v.partLog[0] === volParts[1].sha256, 'a member open uses exactly its member');
  check(equal(map.view.subarray(o2, o2 + n2), member2), 'the member bytes are in the mapping');
  check(poisoned(0, o2 - 9) && poisoned(o2 - 9, 9) && poisoned(o2 + n2, vol.bytes.length - o2 - n2),
    'every byte outside the member is still poison');
  const [o1, n1] = vol.payloads[0];
  map.ensure(o1 - 9, 1);
  check(v.partLog.length === 2 && v.partLog[1] === volParts[3].sha256, 'the block header comes from the rest part');
  check(equal(map.view.subarray(0, o1), vol.bytes.subarray(0, o1)), 'header, directory and member table filled');
  check(poisoned(o1, n1), 'the first member is still poison after its header');
  map.ensure(0, vol.bytes.length);
  check(equal(map.view, vol.bytes), 'the whole archive after ensuring all of it');
  check(v.partLog.length === 4, 'the third member shares the plain file part');
  const map2 = v.createMapping('D:\\VOL\\TEST.VOL');
  map2.ensure(o2, n2);
  check(v.partLog.length === 4 && equal(map2.view.subarray(o2, o2 + n2), member2), 'a second mapping fills from decoded parts');
  check(equal(openSite(site, codec).read('D:\\VOL\\TEST.VOL', o1 - 20, n1 + 40), vol.bytes.subarray(o1 - 20, o1 + n1 + 20)),
    'a read across parts');
  const plainView = new Vfs({ manifest, content }).createMapping('C:\\ES2\\DATA\\EMPTY.CFG');
  check(plainView.size === 0, 'an empty file maps');
}

// A mapping onto the caller's memory: the view function is asked again on every ensure, as a
// wasm instance's memory buffer changes when it grows.
{
  const v = openSite(site, codec, { poison: true });
  let memory = new Uint8Array(vol.bytes.length + 8);
  const map = v.createMapping('D:/VOL/TEST.VOL', () => memory.subarray(4, 4 + vol.bytes.length));
  check(map.view.byteOffset === 4 && map.view.every((b, i) => b === POISON[i & 3]), 'the view of the caller is poisoned');
  const grown = new Uint8Array(memory.length);
  grown.set(memory);
  memory = grown;
  const [o2, n2] = vol.payloads[1];
  map.ensure(o2, n2);
  check(equal(memory.subarray(4 + o2, 4 + o2 + n2), member2), 'ensure fills the current view');
}

// The overlay.
persisted.length = 0;
check(vfs.list('C:\\ES2\\SAV').length === 0 && vfs.list('D:\\EMPTY').length === 0, 'empty directories exist');
vfs.write('c:\\es2\\sav\\game_1.sav', Uint8Array.of(1, 2, 3));
check(equal(vfs.readAll('C:\\ES2\\SAV\\GAME_1.SAV'), [1, 2, 3]), 'a written file reads back');
check(vfs.list('C:\\ES2\\SAV').map((e) => e.name).join() === 'game_1.sav', 'its name keeps the case it was written with');
throwsCode(() => vfs.write('C:\\ES2\\NOPE\\X.SAV', Uint8Array.of(1)), 'not-found', 'a write without its directory');
throwsCode(() => vfs.write('C:\\ES2\\SAV', Uint8Array.of(1)), 'is-directory', 'a write over a directory');

const usedBefore = vfs.partLog.length;
vfs.copy('C:\\ES2\\DATA\\PLAIN.DAT', 'C:\\ES2\\SAV\\COPY.DAT');
check(vfs.overlay.get('C:\\ES2\\SAV\\COPY.DAT').kind === 'parts', 'a copy of a base file refers to its parts');
const changed = vfs.readAll('C:\\ES2\\SAV\\COPY.DAT');
changed[0] ^= 0xff;
vfs.write('C:\\ES2\\SAV\\COPY.DAT', changed);
check(equal(vfs.readAll('C:\\ES2\\SAV\\COPY.DAT'), changed) && equal(vfs.readAll('C:\\ES2\\DATA\\PLAIN.DAT'), plain),
  'copy on write changes only the copy');

vfs.remove('C:\\ES2\\DATA\\PLAIN.DAT');
check(vfs.stat('C:\\ES2\\DATA\\PLAIN.DAT') === null, 'a deleted base file is gone');
check(vfs.overlay.get('C:\\ES2\\DATA\\PLAIN.DAT').kind === 'tombstone', 'a deleted base file leaves a tombstone');
check(!vfs.list('C:\\ES2\\DATA').some((e) => e.name === 'PLAIN.DAT'), 'a tombstone hides the listing entry');
throwsCode(() => vfs.readAll('C:\\ES2\\DATA\\PLAIN.DAT'), 'not-found', 'a deleted file read');
throwsCode(() => vfs.remove('C:\\ES2\\DATA\\PLAIN.DAT'), 'not-found', 'a second delete');
vfs.write('C:\\ES2\\DATA\\PLAIN.DAT', Uint8Array.of(9));
check(vfs.stat('C:\\ES2\\DATA\\PLAIN.DAT').size === 1, 'a file written over a tombstone');
vfs.remove('C:\\ES2\\SAV\\GAME_1.SAV');
check(!vfs.overlay.has('C:\\ES2\\SAV\\GAME_1.SAV'), 'a deleted overlay-only file leaves nothing');

vfs.rename('D:\\VOL\\TEST.VOL', 'C:\\ES2\\VOL\\MOVED.VOL');
check(vfs.stat('D:\\VOL\\TEST.VOL') === null && vfs.overlay.get('D:\\VOL\\TEST.VOL').kind === 'tombstone', 'a renamed base file leaves a tombstone');
const moved = vfs.overlay.get('C:\\ES2\\VOL\\MOVED.VOL');
check(moved.kind === 'parts' && moved.flags.includes('vol'), 'a renamed base file is a part reference with its flags');
check(vfs.partLog.length === usedBefore, 'copy and rename used no part');
check(equal(vfs.readAll('C:\\ES2\\VOL\\MOVED.VOL'), vol.bytes), 'the renamed file reads as the original');
vfs.write('C:\\ES2\\SAV\\A.TMP', Uint8Array.of(7));
throwsCode(() => vfs.rename('C:\\ES2\\SAV\\A.TMP', 'C:\\ES2\\SAV\\COPY.DAT'), 'exists', 'a rename onto a file');
vfs.rename('C:\\ES2\\SAV\\A.TMP', 'C:\\ES2\\SAV\\COPY.DAT', { replace: true });
check(equal(vfs.readAll('C:\\ES2\\SAV\\COPY.DAT'), [7]) && !vfs.overlay.has('C:\\ES2\\SAV\\A.TMP'), 'a replacing rename');
throwsCode(() => vfs.rename('C:\\ES2\\SAV', 'C:\\ES2\\SAV2'), 'is-directory', 'a directory rename');

vfs.mkdir('C:\\ES2\\SAV\\SUB');
check(vfs.stat('c:\\es2\\sav\\sub').dir, 'a new directory');
throwsCode(() => vfs.mkdir('C:\\ES2\\SAV\\SUB'), 'exists', 'a second mkdir');
vfs.write('C:\\ES2\\SAV\\SUB\\F', Uint8Array.of(1));
throwsCode(() => vfs.rmdir('C:\\ES2\\SAV\\SUB'), 'not-empty', 'rmdir of a full directory');
vfs.remove('C:\\ES2\\SAV\\SUB\\F');
vfs.rmdir('C:\\ES2\\SAV\\SUB');
check(vfs.stat('C:\\ES2\\SAV\\SUB') === null, 'rmdir');
vfs.rmdir('D:\\EMPTY');
check(vfs.overlay.get('D:\\EMPTY').kind === 'tombstone' && !vfs.list('D:\\').some((e) => e.name === 'EMPTY'), 'a removed base directory');

const kinds = persisted.map(([k, kind]) => `${k}=${kind}`);
check(kinds.includes('C:\\ES2\\DATA\\PLAIN.DAT=tombstone') && kinds.includes('C:\\ES2\\SAV\\GAME_1.SAV=null')
  && kinds.includes('C:\\ES2\\VOL\\MOVED.VOL=parts') && kinds.includes('C:\\ES2\\SAV\\COPY.DAT=bytes'), `persisted: ${kinds.join(' ')}`);

// The overlay through IndexedDB's shape (structured clone) into a new instance.
{
  const saved = structuredClone(vfs.exportOverlay());
  check(saved.every(([, e]) => !('segments' in e)), 'the exported overlay holds no derived fields');
  const again = openSite(site, codec);
  again.loadOverlay(saved);
  const listing = (v, dir) => JSON.stringify(v.list(dir));
  for (const dir of ['C:\\ES2\\SAV', 'C:\\ES2\\DATA', 'C:\\ES2\\VOL', 'D:\\VOL', 'D:\\']) {
    check(listing(again, dir) === listing(vfs, dir), `the reloaded overlay lists ${dir} alike`);
  }
  check(equal(again.readAll('C:\\ES2\\VOL\\MOVED.VOL'), vol.bytes) && equal(again.readAll('C:\\ES2\\SAV\\COPY.DAT'), [7]),
    'the reloaded overlay reads alike');
}

// The language step.
{
  const v = openSite(site, codec);
  v.installLanguage('G');
  check(equal(v.readAll('C:\\ES2\\ERROR.STR'), errorGer) && equal(v.readAll('C:\\ES2\\DATA\\MISSION.STR'), missionGer),
    'ERROR.STR and MISSION.STR from the language folder');
  check(equal(v.readAll('C:\\ES2\\GERMAN\\BILLBRD\\LOAD1.BMP'), member3), 'the language folder copied');
  check(new TextDecoder().decode(v.readAll('C:\\ES2\\DATA\\DRIVE.CFG')) === 'D:\r\nC:\\ES2\r\n'
    && new TextDecoder().decode(v.readAll('C:\\ES2\\DATA\\LANGUAGE.CFG')) === 'G', 'DRIVE.CFG and LANGUAGE.CFG');
  check([...v.overlay.values()].filter((e) => e.kind === 'bytes').length === 2, 'only the two configuration files hold bytes');
  throwsCode(() => v.installLanguage('X'), 'not-found', 'an unknown language');
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(`${checks - failures.length}/${checks} checks passed`);
process.exit(failures.length ? 1 : 0);
