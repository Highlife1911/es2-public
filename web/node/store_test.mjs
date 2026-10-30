// web/store.mjs over a fake IndexedDB and a fake Cache Storage: the overlay's changes from
// web/vfs.mjs go through the worker's message shape into the "es2" database and reload into a
// fresh Vfs as the same tree (tombstones, part references without bytes, directories), in the
// order of the closes; a failed transaction, the upgrade and the reset; a rename's two keys in
// one transaction (onChanges and applyAll), the reset of overlay and part cache; the part cache reads
// first, refuses and refetches a corrupted part, caches only checked parts; storage.persist()
// and the beforeunload guard.
//
// Arguments: vfs.js
import crypto from 'node:crypto';
import util from 'node:util';
import zlib from 'node:zlib';
import { Vfs, VfsError, storedEntry } from '../vfs.mjs';
import {
  DB_NAME, OVERLAY, PARTS_CACHE, PartCache, guardUnload, openOverlayStore, prefetchParts, requestPersistence, resetInstallation,
} from '../store.mjs';
import { loadCodecFrom } from './vfs_site.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const equal = (a, b) => a.length === b.length && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
const rejects = async (promise, label) => {
  try {
    await promise;
    check(false, `${label}: no error`);
  } catch {
    check(true, label);
  }
};

const codec = await loadCodecFrom(process.argv[2]);

// IndexedDB as far as store.mjs uses it. Transactions run one after another in the order they
// were created, each in a later task; writes are logged as they commit.
class FakeIndexedDB {
  constructor() {
    this.dbs = new Map();
    this.log = [];
    this.upgrades = 0;
    this.failNext = false;
    this.tail = Promise.resolve();
    this.writes = 0;
  }

  open(name, version) {
    const r = {};
    setTimeout(() => {
      let db = this.dbs.get(name);
      if (!db) this.dbs.set(name, db = { version: 0, stores: new Map() });
      const conn = new FakeConnection(this, db);
      r.result = conn;
      if (version < db.version) { r.error = new Error('VersionError'); r.onerror?.(); return; }
      if (version > db.version) {
        conn.upgrading = true;
        db.version = version;
        this.upgrades++;
        r.onupgradeneeded?.();
        conn.upgrading = false;
      }
      r.onsuccess?.();
    });
    return r;
  }

  schedule(run) {
    this.tail = this.tail.then(() => new Promise((resolve) => setTimeout(() => { run(); resolve(); })));
  }
}

class FakeConnection {
  constructor(idb, db) {
    this.idb = idb;
    this.db = db;
    this.closed = false;
    this.objectStoreNames = { contains: (n) => db.stores.has(n) };
  }

  createObjectStore(name) {
    if (!this.upgrading) throw new Error('InvalidStateError: not in an upgrade');
    this.db.stores.set(name, new Map());
  }

  transaction(name, mode) {
    if (this.closed) throw new Error('InvalidStateError: closed');
    if (!this.db.stores.has(name)) throw new Error(`NotFoundError: ${name}`);
    return new FakeTransaction(this.idb, this.db.stores.get(name), mode);
  }

  close() { this.closed = true; }
}

class FakeTransaction {
  constructor(idb, data, mode) {
    this.requests = [];
    this.error = null;
    const fail = idb.failNext && mode === 'readwrite';
    if (mode === 'readwrite') { idb.failNext = false; idb.writes++; }
    const op = (kind, fn) => (...args) => {
      if (kind === 'write' && mode !== 'readwrite') throw new Error('ReadOnlyError');
      const r = {};
      this.requests.push([r, fn, args, kind]);
      return r;
    };
    const sortedKeys = () => [...data.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
    this.store = {
      put: op('write', (value, key) => { data.set(key, structuredClone(value)); idb.log.push(['put', key, value.kind]); return key; }),
      delete: op('write', (key) => { data.delete(key); idb.log.push(['delete', key, null]); }),
      clear: op('write', () => { data.clear(); idb.log.push(['clear', null, null]); }),
      getAllKeys: op('read', () => sortedKeys()),
      getAll: op('read', () => sortedKeys().map((k) => structuredClone(data.get(k)))),
    };
    idb.schedule(() => {
      if (fail) {
        this.error = new Error('QuotaExceededError');
        for (const [r] of this.requests) { r.error = this.error; r.onerror?.(); }
        this.onabort?.();
        return;
      }
      for (const [r, fn, args] of this.requests) { r.result = fn(...args); r.onsuccess?.(); }
      this.oncomplete?.();
    });
  }

  objectStore() { return this.store; }
}

// Cache Storage: named caches of URL -> bytes.
class FakeCache {
  constructor() { this.entries = new Map(); }
  async match(url) { const b = this.entries.get(url); return b ? new Response(b.slice()) : undefined; }
  async put(url, response) { this.entries.set(url, new Uint8Array(await response.arrayBuffer())); }
  async delete(url) { return this.entries.delete(url); }
}
class FakeCaches {
  constructor() { this.caches = new Map(); }
  async open(name) { if (!this.caches.has(name)) this.caches.set(name, new FakeCache()); return this.caches.get(name); }
  async delete(name) { return this.caches.delete(name); }
}

// A site in memory: parts as zstd frames with their content size, served by a sync fetchPart
// (the worker's XMLHttpRequest) and an async fetch.
let seed = 777;
const noise = (n) => {
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; out[i] = (seed >> 13) & 0xff; }
  return out;
};
const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');
const frame = (bytes) => new Uint8Array(zlib.zstdCompressSync(Buffer.from(bytes), { pledgedSrcSize: bytes.length }));
const served = new Map();
const part = (bytes, offset = 0) => {
  const h = sha(bytes);
  served.set(h, frame(bytes));
  return { sha256: h, length: bytes.length, compressed: served.get(h).length, offset };
};
const plain = noise(3000), prefs = noise(50), vol0 = noise(800), vol1 = noise(400);
const errorGer = new TextEncoder().encode('fehler'), missionGer = new TextEncoder().encode('mission');
const manifest = {
  format: 1,
  directories: ['C:\\', 'C:\\ES2', 'C:\\ES2\\DATA', 'C:\\ES2\\SAV', 'C:\\ES2\\VOL', 'D:\\', 'D:\\EMPTY'],
  files: [
    { path: 'D:\\DATA\\A.DAT', size: plain.length, parts: [part(plain)] },
    { path: 'C:\\ES2\\DATA\\A.DAT', size: plain.length, parts: [part(plain)] },
    { path: 'C:\\ES2\\DATA\\PREFS.CFG', size: prefs.length, parts: [part(prefs)] },
    { path: 'D:\\VOL\\X.VOL', size: 1200, flags: ['vol'], parts: [part(vol0), part(vol1, 800)] },
    { path: 'D:\\GERMAN\\ERROR.GER', size: errorGer.length, parts: [part(errorGer)] },
    { path: 'D:\\GERMAN\\MISSION.GER', size: missionGer.length, parts: [part(missionGer)] },
  ],
  languages: {
    G: {
      folder: 'GERMAN',
      directories: ['C:\\ES2\\GERMAN'],
      copies: [['C:\\ES2\\GERMAN\\ERROR.GER', 'D:\\GERMAN\\ERROR.GER'], ['C:\\ES2\\ERROR.STR', 'D:\\GERMAN\\ERROR.GER'],
        ['C:\\ES2\\DATA\\MISSION.STR', 'D:\\GERMAN\\MISSION.GER']],
      writes: { 'C:\\ES2\\DATA\\LANGUAGE.CFG': 'G', 'C:\\ES2\\DATA\\DRIVE.CFG': 'D:\r\nC:\\ES2\r\n' },
    },
  },
};
let syncFetches = 0;
const fetchPart = (h) => { syncFetches++; const b = served.get(h); if (!b) throw new Error('404'); return b; };
const fetchLog = [];
const fakeFetch = async (url) => {
  fetchLog.push(url);
  const b = served.get(/([0-9a-f]{64})\.zst$/.exec(url)?.[1]);
  return b ? new Response(b.slice()) : new Response(null, { status: 404 });
};
const newVfs = (options = {}) => new Vfs({ manifest, codec, fetchPart, ...options });

// Every visible directory and file of both trees, with the file bytes.
function tree(v) {
  const out = [];
  const walk = (dir) => {
    for (const e of v.list(dir)) {
      const p = `${dir.endsWith('\\') ? dir : `${dir}\\`}${e.name}`;
      out.push([p, e.dir, e.size]);
      if (e.dir) walk(p);
      else out.push(Buffer.from(v.readAll(p)).toString('hex'));
    }
  };
  walk('C:\\');
  walk('D:\\');
  return out;
}

// The overlay round trip: worker (Vfs + onPersist) -> message -> store -> reload.
const idb = new FakeIndexedDB();
const store = await openOverlayStore(idb);
check(idb.upgrades === 1 && idb.dbs.get(DB_NAME)?.stores.has(OVERLAY), 'the first open creates es2/overlay');
const events = [];
const applied = [];
const a = newVfs({
  onPersist: (key, entry) => {
    events.push([key, entry ? entry.kind : null]);
    const message = structuredClone({ key, entry: entry && storedEntry(entry) });
    applied.push(store.apply(message.key, message.entry));
  },
});
a.installLanguage('G');
a.write('C:\\ES2\\SAV\\GAME_1.SAV', Uint8Array.of(1, 2, 3));
a.write('C:\\ES2\\SAV\\GAME_2.SAV', Uint8Array.of(4));
a.remove('C:\\ES2\\SAV\\GAME_2.SAV');
a.copy('C:\\ES2\\DATA\\A.DAT', 'C:\\ES2\\SAV\\COPY.DAT');
a.remove('C:\\ES2\\DATA\\PREFS.CFG');
a.rename('D:\\VOL\\X.VOL', 'C:\\ES2\\VOL\\X.VOL');
a.rename('C:\\ES2\\SAV\\GAME_1.SAV', 'C:\\ES2\\SAV\\game_3.sav');
a.mkdir('C:\\ES2\\SAV\\SUB');
a.write('C:\\ES2\\SAV\\SUB\\F', Uint8Array.of(9));
a.rmdir('D:\\EMPTY');
a.write('C:\\ES2\\DATA\\A.DAT', Uint8Array.of(5, 5));
for (const text of ['a', 'ab', 'abc']) a.write('C:\\ES2\\SAV\\LOG.TXT', new TextEncoder().encode(text));
await store.flush();
check((await Promise.allSettled(applied)).every((r) => r.status === 'fulfilled'), 'every apply completed');
check(util.isDeepStrictEqual(idb.log.map(([op, key, kind]) => [key, op === 'delete' ? null : kind]), events),
  `the writes commit in the order of the changes (${idb.log.length} of ${events.length})`);
check(events.some(([k, kind]) => k === 'C:\\ES2\\SAV\\GAME_2.SAV' && kind === null), 'an overlay-only delete removes the key');

const raw = idb.dbs.get(DB_NAME).stores.get(OVERLAY);
const copy = raw.get('C:\\ES2\\SAV\\COPY.DAT');
check(copy?.kind === 'parts' && !('data' in copy) && !('segments' in copy) && copy.parts[0].sha256 === sha(plain),
  'a copy is stored as a part reference without bytes');
check(raw.get('C:\\ES2\\VOL\\X.VOL')?.kind === 'parts' && raw.get('C:\\ES2\\VOL\\X.VOL').flags.includes('vol'), 'a renamed base file is stored as a part reference');
check(raw.get('C:\\ES2\\ERROR.STR')?.kind === 'parts', 'the language step stores references');
const withBytes = [...raw.entries()].filter(([, e]) => e.kind === 'bytes').map(([k]) => k).sort();
check(util.isDeepStrictEqual(withBytes, ['C:\\ES2\\DATA\\A.DAT', 'C:\\ES2\\DATA\\DRIVE.CFG', 'C:\\ES2\\DATA\\LANGUAGE.CFG',
  'C:\\ES2\\SAV\\GAME_3.SAV', 'C:\\ES2\\SAV\\LOG.TXT', 'C:\\ES2\\SAV\\SUB\\F']), `only written files hold bytes: ${withBytes.join(' ')}`);
check(['C:\\ES2\\DATA\\PREFS.CFG', 'D:\\VOL\\X.VOL', 'D:\\EMPTY'].every((k) => raw.get(k)?.kind === 'tombstone'), 'tombstones are stored');
check(!raw.has('C:\\ES2\\SAV\\GAME_1.SAV') && !raw.has('C:\\ES2\\SAV\\GAME_2.SAV'), 'removed overlay-only files are not stored');
check(new TextDecoder().decode(raw.get('C:\\ES2\\SAV\\LOG.TXT').data) === 'abc', 'the last of three closes wins');
check(raw.get('C:\\ES2\\SAV\\GAME_3.SAV').name === 'C:\\ES2\\SAV\\game_3.sav', 'the name keeps its case');

// A failed transaction rejects and stores nothing; the next one works.
idb.failNext = true;
await rejects(store.apply('C:\\ES2\\SAV\\FAIL.SAV', { kind: 'bytes', name: 'C:\\ES2\\SAV\\FAIL.SAV', data: Uint8Array.of(1) }), 'a failed write rejects');
check(!raw.has('C:\\ES2\\SAV\\FAIL.SAV'), 'a failed write stores nothing');
store.close();

// The reload: a new connection, no upgrade, the overlay into a fresh Vfs.
const again = await openOverlayStore(idb);
check(idb.upgrades === 1, 'a second open does not upgrade');
const loaded = await again.load();
check(util.isDeepStrictEqual(loaded, [...a.exportOverlay()].sort((x, y) => (x[0] < y[0] ? -1 : 1))), 'the loaded overlay equals the exported one');
syncFetches = 0;
const b = newVfs();
b.loadOverlay(loaded);
check(util.isDeepStrictEqual(tree(b), tree(a)), 'the reloaded tree equals the written one');
check(['C:\\ES2\\DATA\\PREFS.CFG', 'D:\\VOL\\X.VOL', 'D:\\EMPTY', 'C:\\ES2\\SAV\\GAME_1.SAV'].every((p) => b.stat(p) === null),
  'deleted files stay deleted after the reload');
check(equal(b.readAll('C:\\ES2\\SAV\\COPY.DAT'), plain) && equal(b.readAll('C:\\ES2\\VOL\\X.VOL'), [...vol0, ...vol1]),
  'part references read from the parts after the reload');
check(b.stat('C:\\ES2\\SAV\\SUB')?.dir === true, 'a new directory survives');
check(syncFetches > 0 && b.partLog.every((h) => served.has(h)), 'the references fetched their parts');

// The reset: the overlay is empty and the base shows again.
await again.clear();
check((await again.load()).length === 0 && raw.size === 0, 'the reset empties the overlay');
const c = newVfs();
c.loadOverlay(await again.load());
check(c.stat('C:\\ES2\\ERROR.STR') === null && c.stat('C:\\ES2\\DATA\\PREFS.CFG')?.size === prefs.length && c.stat('D:\\EMPTY')?.dir,
  'after the reset the installation is the base again');
again.close();
{
  const old = new FakeIndexedDB();
  old.dbs.set(DB_NAME, { version: 2, stores: new Map([[OVERLAY, new Map()]]) });
  await rejects(openOverlayStore(old), 'a newer database version is refused');
}

// The part cache.
const caches = new FakeCaches();
const cache = new PartCache({ caches, fetch: fakeFetch });
const url = (h) => `data/${h}.zst`;
const shasOf = (path) => manifest.files.find((f) => f.path === path).parts.map((p) => p.sha256);
const wanted = [...shasOf('D:\\DATA\\A.DAT'), ...shasOf('D:\\VOL\\X.VOL'), ...shasOf('C:\\ES2\\DATA\\A.DAT')];
{
  const v = newVfs({ fetchPart: () => { throw new Error('no synchronous fetch expected'); } });
  check((await prefetchParts(v, cache, wanted)).length === 0, 'prefetch succeeds');
  check(fetchLog.length === 3, `a shared part is fetched once (${fetchLog.length} fetches)`);
  const stored = (await caches.open(PARTS_CACHE)).entries;
  check(stored.size === 3 && wanted.every((h) => equal(stored.get(url(h)), served.get(h))), 'the fetched parts are cached');
  check(equal(v.readAll('C:\\ES2\\DATA\\A.DAT'), plain) && equal(v.readAll('D:\\VOL\\X.VOL'), [...vol0, ...vol1]), 'prefetched parts read without a fetch');
  check((await prefetchParts(v, { get: () => { throw new Error('cache used'); } }, wanted)).length === 0, 'parts already decoded are skipped');
}
{
  const v = newVfs({ fetchPart: () => { throw new Error('no synchronous fetch expected'); } });
  fetchLog.length = 0;
  await prefetchParts(v, cache, wanted);
  check(fetchLog.length === 0 && equal(v.readAll('D:\\DATA\\A.DAT'), plain), 'a later visit reads the parts from the cache');
}
{
  const stored = (await caches.open(PARTS_CACHE)).entries;
  const [hPlain] = shasOf('D:\\DATA\\A.DAT');
  const [hVol0, hVol1] = shasOf('D:\\VOL\\X.VOL');
  stored.set(url(hPlain), frame(noise(plain.length)));        // another content of the same length
  const flipped = served.get(hVol0).slice();
  flipped[flipped.length >> 1] ^= 0x55;
  stored.set(url(hVol0), flipped);                           // a changed byte
  stored.set(url(hVol1), served.get(hVol1).slice(0, 5));     // a short frame
  const v = newVfs({ fetchPart: () => { throw new Error('no synchronous fetch expected'); } });
  fetchLog.length = 0;
  check((await prefetchParts(v, cache, wanted)).length === 0, 'prefetch over corrupted cached parts succeeds');
  check(util.isDeepStrictEqual([...fetchLog].sort(), [url(hPlain), url(hVol0), url(hVol1)].sort()), 'each corrupted cached part is refetched');
  check([hPlain, hVol0, hVol1].every((h) => equal(stored.get(url(h)), served.get(h))), 'the refetched parts replace the corrupted ones');
  check(equal(v.readAll('D:\\DATA\\A.DAT'), plain) && equal(v.readAll('D:\\VOL\\X.VOL'), [...vol0, ...vol1]), 'the refetched parts read right');
}
{
  const [hPrefs] = shasOf('C:\\ES2\\DATA\\PREFS.CFG');
  const good = served.get(hPrefs);
  served.set(hPrefs, frame(noise(prefs.length)));
  const v = newVfs({ fetchPart: () => { throw new Error('no synchronous fetch expected'); } });
  const failed = await prefetchParts(v, cache, [hPrefs, 'f'.repeat(64)]);
  check(failed.length === 2, 'a part the server sends corrupted, and an unknown part, fail the prefetch');
  check(!(await caches.open(PARTS_CACHE)).entries.has(url(hPrefs)) && !v.hasPart(hPrefs), 'a refused part is neither cached nor kept');
  await rejects(cache.get(hPrefs, (bytes) => v.addPart(hPrefs, bytes)), 'get of a refused part rejects');
  served.set(hPrefs, good);
}
{
  // The synchronous path: a part fetchPart returned goes into the cache once it passed.
  const puts = [];
  const [hPrefs] = shasOf('C:\\ES2\\DATA\\PREFS.CFG');
  const v = newVfs({ onPart: (h, bytes) => puts.push(cache.put(h, bytes)) });
  check(equal(v.readAll('C:\\ES2\\DATA\\PREFS.CFG'), prefs), 'a synchronously fetched part reads');
  await Promise.all(puts);
  check(puts.length === 1 && equal((await caches.open(PARTS_CACHE)).entries.get(url(hPrefs)), served.get(hPrefs)), 'onPart puts the checked part');
  const swapped = newVfs({ fetchPart: () => frame(noise(prefs.length)), onPart: () => check(false, 'onPart for a refused part') });
  try { swapped.readAll('C:\\ES2\\DATA\\PREFS.CFG'); check(false, 'a refused synchronous part reads'); } catch (e) { check(e instanceof VfsError, 'a refused synchronous part throws'); }
  fetchLog.length = 0;
  const w = newVfs({ fetchPart: () => { throw new Error('no synchronous fetch expected'); } });
  await prefetchParts(w, cache, [hPrefs]);
  check(fetchLog.length === 0 && equal(w.readAll('C:\\ES2\\DATA\\PREFS.CFG'), prefs), 'the next instance reads it from the cache');
}
{
  const failing = new PartCache({ caches, fetch: async () => new Response(null, { status: 503 }), name: 'other' });
  await rejects(failing.get(sha(prefs), () => {}), 'an HTTP error rejects');
  await cache.clear();
  check(!caches.caches.has(PARTS_CACHE), 'the part cache can be deleted');
}

// One transaction per Vfs operation (onChanges -> applyAll): a rename's two keys commit or fail
// together, and the language step is one write. The reset clears the overlay and the parts.
{
  const db = new FakeIndexedDB();
  const s = await openOverlayStore(db);
  const messages = [];
  const v = newVfs({
    onChanges: (changes) => {
      const message = structuredClone({ kind: 'persist', changes: changes.map(([k, e]) => [k, e && storedEntry(e)]) });
      messages.push(message);
      s.applyAll(message.changes);
    },
  });
  v.installLanguage('G');
  v.write('C:\\ES2\\SAV\\GAME_1.SAV', Uint8Array.of(1, 2, 3));
  await s.flush();
  const before = db.writes;
  v.rename('C:\\ES2\\SAV\\GAME_1.SAV', 'C:\\ES2\\SAV\\GAME_2.SAV');
  v.rename('C:\\ES2\\DATA\\PREFS.CFG', 'C:\\ES2\\SAV\\PREFS.OLD');
  await s.flush();
  const stored = db.dbs.get(DB_NAME).stores.get(OVERLAY);
  check(messages[0].changes.length === 6 && messages.length === 4, `the language step is one message, each operation one (${messages.map((m) => m.changes.length)})`);
  check(db.writes - before === 2 && util.isDeepStrictEqual(messages.slice(2).map((m) => m.changes.map(([k, e]) => [k, e && e.kind])), [
    [['C:\\ES2\\SAV\\GAME_2.SAV', 'bytes'], ['C:\\ES2\\SAV\\GAME_1.SAV', null]],
    [['C:\\ES2\\SAV\\PREFS.OLD', 'parts'], ['C:\\ES2\\DATA\\PREFS.CFG', 'tombstone']],
  ]), 'each rename is one transaction of both keys');
  check(!stored.has('C:\\ES2\\SAV\\GAME_1.SAV') && stored.get('C:\\ES2\\SAV\\GAME_2.SAV')?.kind === 'bytes', 'the renamed file is stored under its new name');
  db.failNext = true;
  await rejects(s.applyAll([['C:\\ES2\\SAV\\GAME_3.SAV', stored.get('C:\\ES2\\SAV\\GAME_2.SAV')], ['C:\\ES2\\SAV\\GAME_2.SAV', null]]),
    'a failed rename transaction rejects');
  check(!stored.has('C:\\ES2\\SAV\\GAME_3.SAV') && stored.has('C:\\ES2\\SAV\\GAME_2.SAV'), 'a failed rename keeps the file under its old name only');
  const resetCaches = new FakeCaches();
  const parts = new PartCache({ caches: resetCaches, fetch: fakeFetch });
  await prefetchParts(newVfs(), parts, shasOf('D:\\DATA\\A.DAT'));
  check(resetCaches.caches.has(PARTS_CACHE), 'the part cache holds a part before the reset');
  await resetInstallation(s, parts);
  check(stored.size === 0 && !resetCaches.caches.has(PARTS_CACHE), 'the reset empties the overlay and deletes the part cache');
  s.close();
}

// storage.persist() at Start.
{
  let asked = 0;
  check(await requestPersistence({ persisted: async () => false, persist: async () => { asked++; return true; } }) === true && asked === 1,
    'persist() is requested');
  check(await requestPersistence({ persisted: async () => true, persist: async () => { asked++; return true; } }) === true && asked === 1,
    'persist() is not asked again when granted');
  check(await requestPersistence({ persisted: async () => false, persist: async () => false }) === false, 'a refusal is reported');
  check(await requestPersistence(undefined) === false, 'no StorageManager');
  check(await requestPersistence({ persist: async () => { throw new Error('denied'); } }) === false, 'a throwing persist()');
}

// The beforeunload guard.
{
  const target = new EventTarget();
  let running = false;
  const remove = guardUnload(target, () => running);
  const fire = () => { const e = new Event('beforeunload', { cancelable: true }); target.dispatchEvent(e); return e.defaultPrevented; };
  check(!fire(), 'no warning without a process');
  running = true;
  check(fire(), 'a warning while a process runs');
  remove();
  check(!fire(), 'no warning after removing the guard');
}

console.log(`${checks - failures.length}/${checks} checks passed`);
process.exit(failures.length ? 1 : 0);
