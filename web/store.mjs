// The page's persistent state (docs/web_build.md sections 8.4, 8.5 and 13): the overlay in
// IndexedDB, the parts in Cache Storage, the installation's Web Lock, the storage.persist()
// request and the beforeunload guard. IndexedDB, Cache Storage, the LockManager, fetch and the
// event target are passed in.
//
//   const store = await openOverlayStore(indexedDB);  // database "es2", store "overlay"
//   worker.postMessage({ overlay: await store.load() }); // vfs.loadOverlay(...) before the first process
//   store.apply(key, entry);                          // one change
//   store.applyAll(changes);                          // one 'persist' message: one Vfs operation
//   await resetInstallation(store, cache);            // "Reset installation": overlay and parts
//   const release = await holdInstallation(navigator.locks); // null: another tab holds it
//
//   const cache = new PartCache({ caches, fetch });   // Cache Storage "es2-parts", data/<sha256>.zst
//   await prefetchParts(vfs, cache, shas);            // before a process starts
//   new Vfs({ ..., onPart: (sha, bytes) => cache.put(sha, bytes) });
//
// The worker posts {kind: 'persist', changes: [[key, storedEntry(entry)], ...]} from Vfs's
// onChanges, one message per close, delete or rename, in their order. IndexedDB runs readwrite
// transactions on one store in the order they were created, so applyAll() keeps that order.
// A null entry removes the key: the file existed only in the overlay.
import { storedEntry } from './vfs.mjs';

export const DB_NAME = 'es2';
export const DB_VERSION = 1;
export const OVERLAY = 'overlay';
export const PARTS_CACHE = 'es2-parts';
export const INSTALLATION_LOCK = 'es2-installation';

const request = (r) => new Promise((resolve, reject) => {
  r.onsuccess = () => resolve(r.result);
  r.onerror = () => reject(r.error);
});

const completion = (tx) => new Promise((resolve, reject) => {
  tx.oncomplete = () => resolve();
  tx.onerror = () => reject(tx.error);
  tx.onabort = () => reject(tx.error ?? new Error('transaction aborted'));
});

export function openOverlayStore(indexedDB) {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB_NAME, DB_VERSION);
    r.onupgradeneeded = () => {
      if (!r.result.objectStoreNames.contains(OVERLAY)) r.result.createObjectStore(OVERLAY);
    };
    r.onsuccess = () => resolve(new OverlayStore(r.result));
    r.onerror = () => reject(r.error);
    r.onblocked = () => reject(new Error('the es2 database is open in an older version elsewhere'));
  });
}

export class OverlayStore {
  constructor(db) {
    this.db = db;
    this.pending = new Set();
  }

  write(fill) {
    const tx = this.db.transaction(OVERLAY, 'readwrite');
    fill(tx.objectStore(OVERLAY));
    const done = completion(tx);
    this.pending.add(done);
    const forget = () => this.pending.delete(done);
    done.then(forget, forget);
    return done;
  }

  apply(key, entry) {
    return this.applyAll([[key, entry]]);
  }

  // The changes of one Vfs operation ([[key, entry], ...]) in one transaction: a rename never
  // leaves the file under both names or under neither.
  applyAll(changes) {
    return this.write((s) => {
      for (const [key, entry] of changes) {
        if (entry) s.put(storedEntry(entry), key);
        else s.delete(key);
      }
    });
  }

  clear() {
    return this.write((s) => s.clear());
  }

  // Every write started so far has completed or failed.
  async flush() {
    await Promise.allSettled([...this.pending]);
  }

  // The overlay as Vfs.loadOverlay takes it: [key, entry] in key order.
  async load() {
    const tx = this.db.transaction(OVERLAY, 'readonly');
    const s = tx.objectStore(OVERLAY);
    const [keys, values] = await Promise.all([request(s.getAllKeys()), request(s.getAll())]);
    return keys.map((k, i) => [k, values[i]]);
  }

  close() {
    this.db.close();
  }
}

// Parts never change (their name is their hash), so a cached part needs no revalidation; it is
// still checked before use, and a part that fails the check is deleted and fetched again.
export class PartCache {
  constructor({ caches, fetch, base = 'data/', name = PARTS_CACHE }) {
    this.caches = caches;
    this.fetch = fetch;
    this.base = base;
    this.name = name;
    this.opened = null;
  }

  url(sha256) { return `${this.base}${sha256}.zst`; }

  cache() {
    this.opened ??= this.caches.open(this.name);
    return this.opened;
  }

  // The compressed part that accept(bytes) took; accept throws for a part that fails the check.
  async get(sha256, accept) {
    const cache = await this.cache();
    const url = this.url(sha256);
    const hit = await cache.match(url);
    if (hit) {
      const bytes = new Uint8Array(await hit.arrayBuffer());
      try {
        accept(bytes);
        return bytes;
      } catch {
        await cache.delete(url);
      }
    }
    const response = await this.fetch(url);
    if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    accept(bytes);
    await cache.put(url, new Response(bytes));
    return bytes;
  }

  // A part that already passed the check elsewhere (the synchronous fetch).
  async put(sha256, bytes) {
    const cache = await this.cache();
    await cache.put(this.url(sha256), new Response(bytes));
  }

  clear() {
    this.opened = null;
    return this.caches.delete(this.name);
  }
}

// Fetches the parts a process will need (a prefetch list) into the Vfs, `width` at a time.
// Prefetching only saves time: a part that fails here is fetched again on use. Returns the
// hashes that failed.
export async function prefetchParts(vfs, cache, shas, width = 6) {
  const queue = [...new Set(shas)].filter((sha) => !vfs.hasPart(sha));
  const failed = [];
  const worker = async () => {
    for (let sha; (sha = queue.shift()) !== undefined;) {
      try {
        await cache.get(sha, (bytes) => vfs.addPart(sha, bytes));
      } catch {
        failed.push(sha);
      }
    }
  };
  await Promise.all(Array.from({ length: Math.min(width, queue.length) }, worker));
  return failed;
}

// The landing page's reset: the overlay and the part cache. Parts never change, so keeping them
// would be harmless; the reset still leaves nothing of the site behind.
export async function resetInstallation(store, cache) {
  await Promise.all([store.clear(), cache.clear()]);
}

// The Web Lock over the installation, held by a game from Start to its last overlay write and
// by a reset, so neither runs while another tab has the other going: the function that releases
// it, or null while another tab holds it. Without a LockManager, or when it refuses, the hold is
// granted.
export function holdInstallation(locks) {
  if (!locks?.request) return Promise.resolve(() => {});
  return new Promise((resolve) => {
    let release = () => {};
    const held = new Promise((r) => { release = r; });
    locks.request(INSTALLATION_LOCK, { ifAvailable: true }, (lock) => {
      resolve(lock ? release : null);
      return lock ? held : undefined;
    }).catch(() => resolve(() => {}));
  });
}

// Asks once at Start that the browser keep the site's storage; true when it is kept.
export async function requestPersistence(storage) {
  if (!storage?.persist) return false;
  try {
    if (storage.persisted && (await storage.persisted())) return true;
    return await storage.persist();
  } catch {
    return false;
  }
}

// Warns before the page unloads while running() is true. Returns the function that removes it.
export function guardUnload(target, running) {
  const listener = (event) => {
    if (!running()) return;
    event.preventDefault();
    event.returnValue = 'es2'; // older browsers ask only for a non-empty BeforeUnloadEvent.returnValue
  };
  target.addEventListener('beforeunload', listener);
  return () => target.removeEventListener('beforeunload', listener);
}
