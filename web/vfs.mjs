// The worker's file system (docs/web_build.md section 8): the base trees of manifest.json, the
// overlay of written and deleted files, and sparse files filled per part on demand. The parts
// are decoded and checked by vfs.wasm (//web:vfs_wasm, web/src/vfs_codec.c).
//
//   const codec = await loadCodec(factory);           // factory: vfs.js's default export
//   const vfs = new Vfs({ manifest, codec, fetchPart, poison, onPersist, onPart });
//   vfs.stat(path) / vfs.list(dir) / vfs.read(path, offset, length) / vfs.readAll(path)
//   vfs.write(path, bytes) / vfs.remove(path) / vfs.rename(from, to) / vfs.copy(from, to)
//   vfs.mkdir(path) / vfs.rmdir(path) / vfs.installLanguage('G')
//   const map = vfs.createMapping(path, view);       // a whole-file view, filled on demand
//   map.ensure(offset, length)                        // "ensure range"
//
// fetchPart(sha256) returns a part's compressed bytes synchronously (XMLHttpRequest in the
// worker, node:fs in the tests) or throws. Decoded parts stay in the Vfs across process
// instances; partLog lists every fetched part in order. onPart(sha256, compressed) is called for
// every part fetchPart returned once it passed the check (for Cache Storage, web/store.mjs);
// addPart(sha256, compressed) takes a part fetched asynchronously through the same check.
// onPersist(key, entry) is called for every overlay change; the entry is a tombstone, a
// directory, bytes, or base-part references, or null when the key left the overlay.
// onChanges(changes) gets the same changes per operation, [[key, entry], ...]: a rename's two
// keys and the language step's files come in one call, so the store writes them in one
// transaction.
// storedEntry(entry) is its form for postMessage and IndexedDB.
//
// Paths are absolute ("D:\VOL\SHELL0.VOL", "c:/es2/sav"); the key of a path is its normalized
// form (backslashes, upper-case ASCII, "." and ".." resolved), which is also the IndexedDB key.
// Directories are renamed by no own code, so rename takes files only.
//
// missionKey(scriptDat) names a mission's prefetch list (section 8.4).

export const FLAG = { VOL: 'vol', MEDIA: 'media', NO_BYTES: 'no-bytes' };

// The installation's language letter, which installLanguage writes.
export const LANGUAGE_FILE = 'C:\\ES2\\DATA\\LANGUAGE.CFG';

// The world and zone a prepared script.dat loads (docs/formats/mission.md, "Prepared script
// exact framing"): the world selector signed16(2 * word 0 + word 9) and zone word 1, as
// "world<selector>-zone<zone>"; null for fewer than 20 bytes.
export const MISSION_SCRIPT = 'C:\\ES2\\DATA\\script.dat';
export function missionKey(bytes) {
  if (!bytes || bytes.length < 20) return null;
  const word = (i) => (bytes[2 * i] | (bytes[2 * i + 1] << 8)) << 16 >> 16;
  const world = ((2 * word(0) + word(9)) << 16) >> 16;
  return `world${world}-zone${word(1)}`;
}

// Unfetched bytes of a mapping in poison mode: byte i of the file is POISON[i & 3].
export const POISON = Uint8Array.of(0xde, 0xad, 0xbe, 0xef);

export class VfsError extends Error {
  constructor(code, message) {
    super(`${code}: ${message}`);
    this.name = 'VfsError';
    this.code = code;
  }
}

const DECODE_ERRORS = { [-1]: 'part-corrupt', [-2]: 'part-size', [-3]: 'part-corrupt', [-4]: 'part-hash' };

export async function loadCodec(factory) {
  const m = await factory();
  const ascii = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0) & 0x7f);
  const latin1 = (ptr, n) => String.fromCharCode(...m.HEAPU8.subarray(ptr, ptr + n));
  return {
    // The decoded part, or a VfsError (part-corrupt, part-size, part-hash).
    decode(compressed, length, sha256) {
      const src = m._malloc(compressed.length || 1);
      const dst = m._malloc(length || 1);
      const hash = m._malloc(65);
      try {
        m.HEAPU8.set(compressed, src);
        m.HEAPU8.set(ascii(sha256.padEnd(64, '?').slice(0, 64)), hash);
        const status = m._es2v_part_decode(src, compressed.length, dst, length, hash);
        if (status !== 0) throw new VfsError(DECODE_ERRORS[status] ?? 'part-corrupt', `part ${sha256} (${status})`);
        return m.HEAPU8.slice(dst, dst + length);
      } finally {
        m._free(hash);
        m._free(dst);
        m._free(src);
      }
    },
    sha256(bytes) {
      const data = m._malloc(bytes.length || 1);
      const hex = m._malloc(65);
      try {
        m.HEAPU8.set(bytes, data);
        m._es2v_sha256(data, bytes.length, hex);
        return latin1(hex, 64);
      } finally {
        m._free(hex);
        m._free(data);
      }
    },
  };
}

// "D:\VOL\X.VOL" for "d:/vol//./x.vol"; a drive root is "D:\".
export function normalize(path) {
  const m = /^([A-Za-z]):(.*)$/s.exec(String(path));
  if (!m) throw new VfsError('bad-path', `not an absolute path: ${path}`);
  const out = [];
  for (const part of m[2].split(/[\\/]+/)) {
    if (part === '' || part === '.') continue;
    if (part === '..') out.pop();
    else out.push(part);
  }
  return `${m[1].toUpperCase()}:\\${out.join('\\')}`;
}

export function keyOf(path) {
  return normalize(path).replace(/[a-z]/g, (c) => c.toUpperCase());
}

function parentKey(key) {
  const i = key.lastIndexOf('\\');
  return i <= 2 ? (key.length > 3 ? key.slice(0, 3) : null) : key.slice(0, i);
}

const leaf = (path) => path.slice(path.lastIndexOf('\\') + 1);

// An overlay entry without derived fields: what is posted to the page and kept in IndexedDB.
export function storedEntry(e) {
  return e.kind === 'parts' ? { kind: 'parts', name: e.name, size: e.size, flags: e.flags, parts: e.parts } : e;
}

// The file ranges of a part list as segments sorted by file offset: [offset, length, part, partPos].
function segmentsOf(parts) {
  const segments = [];
  parts.forEach((part, index) => {
    let pos = 0;
    for (const [offset, length] of part.ranges) {
      if (length) segments.push([offset, length, index, pos]);
      pos += length;
    }
  });
  return segments.sort((a, b) => a[0] - b[0]);
}

function firstSegment(segments, offset) {
  let lo = 0, hi = segments.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (segments[mid][0] + segments[mid][1] <= offset) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

function fillPoison(view) {
  for (let i = 0; i < view.length; i++) view[i] = POISON[i & 3];
}

export class Vfs {
  constructor({ manifest, codec, fetchPart, poison = false, onPersist = null, onChanges = null, onPart = null }) {
    this.manifest = manifest;
    this.codec = codec;
    this.fetchPart = fetchPart;
    this.poison = poison;
    this.onPersist = onPersist;
    this.onChanges = onChanges;
    this.changes = null;
    this.onPart = onPart;
    this.base = new Map();
    this.partInfo = new Map();
    this.overlay = new Map();
    this.decoded = new Map();
    this.partLog = [];
    for (const dir of manifest.directories ?? []) this.addBaseDir(normalize(dir));
    for (const f of manifest.files) {
      const name = normalize(f.path);
      const parts = (f.parts ?? []).map((p) => ({
        sha256: p.sha256, length: p.length, compressed: p.compressed,
        ranges: p.ranges ?? [[p.offset, p.length]],
      }));
      for (const p of parts) this.partInfo.set(p.sha256, p);
      const covered = parts.reduce((n, p) => n + p.length, 0);
      const flags = f.flags ?? [];
      if (!flags.includes(FLAG.NO_BYTES) && covered !== f.size) {
        throw new VfsError('bad-manifest', `${f.path}: parts cover ${covered} of ${f.size} bytes`);
      }
      const key = keyOf(name);
      this.addBaseDir(name.slice(0, name.lastIndexOf('\\')));
      this.base.set(key, { name, dir: false, size: f.size, flags, parts, segments: segmentsOf(parts) });
    }
  }

  addBaseDir(name) {
    for (let n = normalize(name); ;) {
      const key = keyOf(n);
      if (this.base.has(key)) return;
      this.base.set(key, { name: n, dir: true });
      const parent = parentKey(key);
      if (!parent) return;
      n = parent.length === 3 ? n.slice(0, 3) : n.slice(0, n.lastIndexOf('\\'));
    }
  }

  // The visible entry of a key: an overlay entry, or the base one; null when absent or deleted.
  entry(key) {
    const o = this.overlay.get(key);
    if (o) return o.kind === 'tombstone' ? null : o;
    return this.base.get(key) ?? null;
  }

  isDir(e) { return e.dir === true || e.kind === 'dir'; }

  sizeOf(e) { return e.kind === 'bytes' ? e.data.length : e.size; }

  stat(path) {
    const e = this.entry(keyOf(path));
    if (!e) return null;
    if (this.isDir(e)) return { name: leaf(e.name), dir: true, size: 0, flags: [] };
    return { name: leaf(e.name), dir: false, size: this.sizeOf(e), flags: e.flags ?? [] };
  }

  // The visible children of a directory, sorted by upper-case name, as FindFirstFile on the CD.
  list(path) {
    const key = keyOf(path);
    const e = this.entry(key);
    if (!e) throw new VfsError('not-found', path);
    if (!this.isDir(e)) throw new VfsError('not-directory', path);
    const children = new Map();
    const consider = (childKey) => {
      if (children.has(childKey) || parentKey(childKey) !== key) return;
      const c = this.entry(childKey);
      if (c) children.set(childKey, this.isDir(c) ? { name: leaf(c.name), dir: true, size: 0 } : { name: leaf(c.name), dir: false, size: this.sizeOf(c) });
    };
    for (const k of this.base.keys()) consider(k);
    for (const k of this.overlay.keys()) consider(k);
    return [...children.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)).map(([, v]) => v);
  }

  file(path) {
    const e = this.entry(keyOf(path));
    if (!e) throw new VfsError('not-found', path);
    if (this.isDir(e)) throw new VfsError('is-directory', path);
    return e;
  }

  noBytes(e, path) {
    if ((e.flags ?? []).includes(FLAG.NO_BYTES)) {
      throw new VfsError((e.flags ?? []).includes(FLAG.MEDIA) ? 'media-bytes' : 'no-bytes', path);
    }
  }

  // A decoded part, fetched and checked on first use.
  part(p) {
    let data = this.decoded.get(p.sha256);
    if (data) return data;
    let compressed;
    try {
      compressed = this.fetchPart(p.sha256);
    } catch (e) {
      throw new VfsError('part-fetch', `${p.sha256}: ${e.message ?? e}`);
    }
    if (!compressed) throw new VfsError('part-fetch', p.sha256);
    data = this.accept(p, compressed);
    if (this.onPart) this.onPart(p.sha256, compressed);
    return data;
  }

  accept(p, compressed) {
    if (p.compressed !== undefined && compressed.length !== p.compressed) {
      throw new VfsError('part-size', `${p.sha256}: ${compressed.length} bytes, manifest ${p.compressed}`);
    }
    const data = this.codec.decode(compressed, p.length, p.sha256);
    this.decoded.set(p.sha256, data);
    this.partLog.push(p.sha256);
    return data;
  }

  hasPart(sha256) { return this.decoded.has(sha256); }

  // A manifest part fetched outside fetchPart, kept once it passed the same check.
  addPart(sha256, compressed) {
    const p = this.partInfo.get(sha256);
    if (!p) throw new VfsError('not-found', `part ${sha256}`);
    return this.decoded.get(sha256) ?? this.accept(p, compressed);
  }

  // Copies file bytes [offset, offset + length) of a part-backed entry into `out` at `at`.
  copyParts(e, offset, length, out, at) {
    const end = offset + length;
    for (let i = firstSegment(e.segments, offset); i < e.segments.length; i++) {
      const [o, n, index, pos] = e.segments[i];
      if (o >= end) break;
      const from = Math.max(o, offset), to = Math.min(o + n, end);
      if (from >= to) continue;
      const data = this.part(e.parts[index]);
      out.set(data.subarray(pos + from - o, pos + to - o), at + from - offset);
    }
  }

  // Up to `length` bytes from `offset`, fewer at the end of the file (a read through the OS layer).
  read(path, offset, length) {
    const e = this.file(path);
    this.noBytes(e, path);
    const size = this.sizeOf(e);
    const start = Math.min(Math.max(0, offset), size);
    const n = Math.max(0, Math.min(length, size - start));
    if (e.kind === 'bytes') return e.data.slice(start, start + n);
    const out = new Uint8Array(n);
    if (n) this.copyParts(e, start, n, out, 0);
    return out;
  }

  readAll(path) {
    return this.read(path, 0, this.stat(path)?.size ?? 0);
  }

  // A whole-file view (the instance's mapping buffer, or a new one), filled per part by ensure().
  // `view` is a Uint8Array or a function returning one each time (a wasm memory's buffer is
  // replaced when it grows).
  createMapping(path, view = null) {
    const e = this.file(path);
    this.noBytes(e, path);
    const size = this.sizeOf(e);
    const viewOf = typeof view === 'function' ? view : (() => { const v = view ?? new Uint8Array(size); return () => v; })();
    const first = viewOf();
    if (first.length < size) throw new VfsError('bad-view', `${path}: view of ${first.length} bytes for ${size}`);
    if (this.poison) fillPoison(first.subarray(0, size));
    if (e.kind === 'bytes') {
      first.set(e.data);
      return { path, size, get view() { return viewOf(); }, ensure() {} };
    }
    const filled = new Set();
    const vfs = this;
    return {
      path, size, filled,
      get view() { return viewOf(); },
      // Fills every part overlapping [offset, offset + length) into the view, whole.
      ensure(offset, length) {
        const end = Math.min(size, offset + length);
        for (let i = firstSegment(e.segments, Math.max(0, offset)); i < e.segments.length; i++) {
          const [o, , index] = e.segments[i];
          if (o >= end) break;
          if (filled.has(index)) continue;
          const part = e.parts[index];
          const data = vfs.part(part);
          const view = viewOf();
          let pos = 0;
          for (const [ro, rn] of part.ranges) {
            view.set(data.subarray(pos, pos + rn), ro);
            pos += rn;
          }
          filled.add(index);
        }
      },
    };
  }

  persist(key) {
    const entry = this.overlay.get(key) ?? null;
    if (this.onPersist) this.onPersist(key, entry);
    if (this.changes) this.changes.push([key, entry]);
    else if (this.onChanges) this.onChanges([[key, entry]]);
  }

  // Runs fn with its overlay changes delivered to onChanges together at its end.
  group(fn) {
    if (this.changes) return fn();
    this.changes = [];
    try {
      return fn();
    } finally {
      const changes = this.changes;
      this.changes = null;
      if (changes.length && this.onChanges) this.onChanges(changes);
    }
  }

  setOverlay(key, value) {
    this.overlay.set(key, value);
    this.persist(key);
  }

  // Removes an overlay entry where the base has nothing, a tombstone where it has.
  clear(key, name) {
    if (this.base.has(key)) this.setOverlay(key, { kind: 'tombstone', name });
    else if (this.overlay.delete(key)) this.persist(key);
  }

  parentDir(path) {
    const name = normalize(path);
    const key = keyOf(name);
    const parent = parentKey(key);
    const p = parent && this.entry(parent);
    if (!p || !this.isDir(p)) throw new VfsError('not-found', `no directory for ${path}`);
    return { name, key };
  }

  // The whole file's bytes at close: a new file or a changed copy of a base one.
  write(path, bytes) {
    const { name, key } = this.parentDir(path);
    const e = this.entry(key);
    if (e && this.isDir(e)) throw new VfsError('is-directory', path);
    this.setOverlay(key, { kind: 'bytes', name: e ? e.name : name, data: Uint8Array.from(bytes) });
  }

  remove(path) {
    const key = keyOf(path);
    const e = this.entry(key);
    if (!e) throw new VfsError('not-found', path);
    if (this.isDir(e)) throw new VfsError('is-directory', path);
    this.clear(key, e.name);
  }

  // The source entry as stored under another name: bytes stay bytes, base files stay part
  // references (copy on write), so nothing is fetched or stored twice.
  moved(e, name) {
    if (e.kind === 'bytes') return { kind: 'bytes', name, data: e.data };
    return { kind: 'parts', name, size: e.size, flags: e.flags ?? [], parts: e.parts, segments: e.segments };
  }

  rename(from, to, { replace = false } = {}) {
    this.group(() => this.renameFile(from, to, replace));
  }

  renameFile(from, to, replace) {
    const fromKey = keyOf(from);
    const e = this.file(from);
    const { name, key } = this.parentDir(to);
    if (key === fromKey) return;
    const target = this.entry(key);
    if (target && (this.isDir(target) || !replace)) throw new VfsError('exists', to);
    this.setOverlay(key, this.moved(e, name));
    this.clear(fromKey, e.name);
  }

  copy(from, to, { replace = true } = {}) {
    const e = this.file(from);
    const { name, key } = this.parentDir(to);
    const target = this.entry(key);
    if (target && (this.isDir(target) || !replace)) throw new VfsError('exists', to);
    this.setOverlay(key, this.moved(e, name));
  }

  mkdir(path) {
    const { name, key } = this.parentDir(path);
    if (this.entry(key)) throw new VfsError('exists', path);
    this.setOverlay(key, { kind: 'dir', name });
  }

  rmdir(path) {
    const key = keyOf(path);
    const e = this.entry(key);
    if (!e) throw new VfsError('not-found', path);
    if (!this.isDir(e)) throw new VfsError('not-directory', path);
    if (this.list(path).length) throw new VfsError('not-empty', path);
    this.clear(key, e.name);
  }

  // What the landing page's language step writes (docs/web_build.md section 13), from the
  // manifest's `languages` entry: the folder's directories, its files and the renamed copies
  // as part references, and the two configuration files.
  installLanguage(code) {
    const lang = this.manifest.languages?.[code];
    if (!lang) throw new VfsError('not-found', `language ${code}`);
    this.group(() => this.writeLanguage(lang));
  }

  writeLanguage(lang) {
    for (const dir of lang.directories) if (!this.stat(dir)) this.mkdir(dir);
    for (const [to, from] of lang.copies) this.copy(from, to);
    for (const [to, text] of Object.entries(lang.writes)) this.write(to, Uint8Array.from(text, (c) => c.charCodeAt(0)));
  }

  // The overlay for IndexedDB, and back. Part references keep only what the manifest has.
  exportOverlay() {
    return [...this.overlay.entries()].map(([key, e]) => [key, storedEntry(e)]);
  }

  loadOverlay(entries) {
    this.overlay.clear();
    for (const [key, e] of entries) {
      this.overlay.set(key, e.kind === 'parts' ? { ...e, segments: segmentsOf(e.parts) } : e);
    }
  }
}
