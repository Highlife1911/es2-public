// The page's persistent state (docs/web_build.md sections 8.4, 8.5 and 13): the overlay in
// IndexedDB, gamefiles.zst in Cache Storage, the installation's Web Lock, the storage.persist()
// request and the beforeunload guard. IndexedDB, Cache Storage, the LockManager, fetch, the
// SubtleCrypto and the event target are passed in.
//
//   const store = await openOverlayStore(indexedDB);  // database "es2", store "overlay"
//   worker.postMessage({ overlay: await store.load() }); // vfs.loadOverlay(...) before the first process
//   store.apply(key, entry);                          // one change
//   store.applyAll(changes);                          // one 'persist' message: one Vfs operation
//   await resetInstallation(store, files);            // "Reset installation": overlay and gamefiles
//   const release = await holdInstallation(navigator.locks); // null: another tab holds it
//
//   const files = new GameFiles({ caches, fetch, subtle }); // Cache Storage "es2-gamefiles"
//   const { manifest, gamefiles } = await files.load((received, size) => ...);
//
// The worker posts {kind: 'persist', changes: [[key, storedEntry(entry)], ...]} from Vfs's
// onChanges, one message per close, delete or rename, in their order. IndexedDB runs readwrite
// transactions on one store in the order they were created, so applyAll() keeps that order.
// A null entry removes the key: the file existed only in the overlay.
import { storedEntry } from './vfs.mjs';

export const DB_NAME = 'es2';
export const DB_VERSION = 1;
export const OVERLAY = 'overlay';
export const GAMEFILES_CACHE = 'es2-gamefiles';
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

const hex = (buffer) => Array.from(new Uint8Array(buffer), (b) => b.toString(16).padStart(2, '0')).join('');

// manifest.json (always revalidated) and the gamefiles.zst it names: from Cache Storage when
// the cached copy has the manifest's size and SHA-256, else downloaded, checked and stored. A
// cached copy that fails the check (an older build's) is replaced. onProgress(received, size)
// follows the download.
export class GameFiles {
  constructor({ caches, fetch, subtle, manifest = 'manifest.json', name = GAMEFILES_CACHE }) {
    Object.assign(this, { caches, fetch, subtle, manifestUrl: manifest, name });
  }

  async load(onProgress = () => {}) {
    const response = await this.fetch(this.manifestUrl, { cache: 'no-cache' });
    if (!response.ok) throw new Error(`${this.manifestUrl}: HTTP ${response.status}`);
    const manifest = await response.json();
    const want = manifest.gamefiles;
    if (!want?.path || !(want.size >= 0) || !want.sha256) throw new Error(`${this.manifestUrl}: no gamefiles entry`);
    const cache = await this.caches.open(this.name);
    const hit = await cache.match(want.path);
    if (hit) {
      const bytes = new Uint8Array(await hit.arrayBuffer());
      if (await this.matches(bytes, want)) {
        onProgress(want.size, want.size);
        return { manifest, gamefiles: bytes };
      }
      await cache.delete(want.path);
    }
    const bytes = await this.download(want, onProgress);
    if (!(await this.matches(bytes, want))) throw new Error(`${want.path}: not the manifest's SHA-256`);
    await cache.put(want.path, new Response(bytes));
    return { manifest, gamefiles: bytes };
  }

  async matches(bytes, want) {
    return bytes.length === want.size && hex(await this.subtle.digest('SHA-256', bytes)) === want.sha256;
  }

  // Cache Storage keeps the only copy, so the download bypasses the HTTP cache.
  async download(want, onProgress) {
    const response = await this.fetch(want.path, { cache: 'no-store' });
    if (!response.ok) throw new Error(`${want.path}: HTTP ${response.status}`);
    const bytes = new Uint8Array(want.size);
    let received = 0;
    const take = (chunk) => {
      if (received + chunk.length > want.size) throw new Error(`${want.path}: more than ${want.size} bytes`);
      bytes.set(chunk, received);
      received += chunk.length;
      onProgress(received, want.size);
    };
    if (response.body?.getReader) {
      const reader = response.body.getReader();
      for (let r; !(r = await reader.read()).done;) take(r.value);
    } else {
      take(new Uint8Array(await response.arrayBuffer()));
    }
    if (received !== want.size) throw new Error(`${want.path}: ${received} of ${want.size} bytes`);
    return bytes;
  }

  clear() {
    return this.caches.delete(this.name);
  }
}

// The landing page's reset: the overlay and the cached gamefiles.zst, so that nothing of the
// site is left behind.
export async function resetInstallation(store, files) {
  await Promise.all([store.clear(), files.clear()]);
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
