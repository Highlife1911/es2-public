// The JS host of os_wasm.c's imports (module "es2os"): os_win32.c's Win32 semantics over a
// file system given as an object (docs/web_build.md section 7). The file has two uses:
//
// - a --pre-js of every wasm binary with //platform:os: it instantiates the module with these
//   imports over node:fs on the real (Windows) paths, so a wasm run under Node reads and
//   writes the same installation clone and CD as the native run;
// - a CommonJS module (web/node/os_host.mjs): it exports es2osImports, nodeFiles and ERROR.
//
// Instantiation: every pre-js of the build registers its import modules in es2Imports; the
// default instantiateWasm adds them to the glue's imports. A host passes Module.es2os (or
// Module.es2shell, ...) as a function (memory, runtime) => imports to serve a module itself,
// or Module.es2osFiles, a file system as below, for es2osImports over it.
// `memory()` returns the instance's current ArrayBuffer; runtime.grow(bytes) grows the memory
// to at least `bytes` and says whether it could; runtime.exit(code) ends the process.
//
// es2osImports(memory, runtime, files): the imports over `files`, which only stores:
//   files.cwd()                            the working directory, a full path
//   files.stat(path)                       null or {dir, size, readonly, times}
//   files.list(dir)                        the entries of a directory, each a stat with `name`
//   files.open(path, {write, create, truncate})
//                                          {read(target, at, length, position) -> n,
//                                           write(source, position), size(), close(),
//                                           optional map(view) -> null or {ensure(offset, length), close()}}
//   files.remove(path) / files.rename(from, to) (replacing) / files.mkdir(path)
// map(view) serves a sparse whole-file view (os_wasm.c's es2_os_map_file): view() returns the
// Uint8Array of the instance's buffer (anew on each call, since the memory can grow), and
// ensure fills a range of it with the file's bytes. Without map, or when it returns null,
// the file is read whole into the buffer.
// `times` are the creation, access and write time in ms since 1970. Every path is a full path
// as GetFullPathName returns it, and every Win32 rule lies here: existence, errors, share
// modes, access, wildcards, the order of find results.

var es2Imports = es2Imports || {};

var ES2OS_ERROR = {
  FILE_NOT_FOUND: 2, PATH_NOT_FOUND: 3, TOO_MANY_OPEN_FILES: 4, ACCESS_DENIED: 5, INVALID_HANDLE: 6,
  NO_MORE_FILES: 18, SHARING_VIOLATION: 32, FILE_EXISTS: 80, INVALID_PARAMETER: 87, NEGATIVE_SEEK: 131,
  ALREADY_EXISTS: 183, DIRECTORY: 267,
};

if (typeof Module !== 'undefined') {
  es2Imports['es2os'] = (memory) => {
    const runtime = {
      grow: (bytes) => {
        const have = wasmMemory.buffer.byteLength;
        if (bytes <= have) return true;
        try { wasmMemory.grow(Math.ceil((bytes - have) / 65536)); } catch { return false; }
        updateMemoryViews();
        return true;
      },
      exit: (code) => { exitJS(code, false); },
    };
    if (Module['es2os']) return Module['es2os'](memory, runtime);
    if (Module['es2osFiles']) return es2osImports(memory, runtime, Module['es2osFiles']);
    return ENVIRONMENT_IS_NODE ? es2osImports(memory, runtime, nodeFiles()) : {};
  };

  if (!Module['instantiateWasm']) {
    Module['instantiateWasm'] = (info, receive) => {
      const memory = () => wasmMemory.buffer;
      for (const name of Object.keys(es2Imports)) info[name] = es2Imports[name](memory);
      // A compiled WebAssembly.Module (Module.wasmModule) is instantiated anew, so a host keeps one
      // compilation for many processes; instantiate(module) resolves to the Instance itself.
      const compiled = Module['wasmModule'];
      const binary = compiled || Module['wasmBinary'] || readBinary(findWasmBinary());
      WebAssembly.instantiate(binary, info).then((result) => receive(compiled ? result : result.instance), (e) => abort(e));
      return {};
    };
  }
} else {
  module.exports = { es2osImports, nodeFiles, fullPath, ERROR: ES2OS_ERROR };
}

// GetFullPathNameA's result for `path` with the working directory `cwd`: "/" and runs of
// separators become one "\", "." and ".." are resolved (not above the root), trailing dots
// and spaces of the last name go, and a trailing separator stays.
function fullPath(path, cwd) {
  let root, rest;
  const drive = /^([A-Za-z]):(.*)$/s.exec(path);
  const cwdRoot = /^[A-Za-z]:/.exec(cwd)?.[0] ?? 'C:';
  if (drive && /^[\\/]/.test(drive[2])) {
    root = drive[1] + ':';
    rest = drive[2];
  } else if (drive) {
    // "X:name": relative to the working directory when that is on X:
    const same = cwdRoot.toUpperCase() === (drive[1] + ':').toUpperCase();
    root = same ? cwdRoot : drive[1] + ':';
    rest = (same ? cwd.slice(2) + '\\' : '\\') + drive[2];
  } else if (/^[\\/]/.test(path)) {
    root = cwdRoot;
    rest = path;
  } else {
    root = cwdRoot;
    rest = cwd.slice(2) + '\\' + path;
  }
  const names = [];
  for (const name of rest.split(/[\\/]+/)) {
    if (name === '' || name === '.') continue;
    if (name === '..') names.pop();
    else names.push(name);
  }
  if (names.length && !/[\\/]$/.test(path)) {
    const last = names[names.length - 1].replace(/[. ]+$/, '');
    if (last) names[names.length - 1] = last;
    else names.pop();
  }
  const full = root + '\\' + names.join('\\');
  return names.length && /[\\/]$/.test(path) ? full + '\\' : full;
}

// The file system of the pre-js: node:fs on the host's paths. Names are compared without case
// because the Windows file system does so.
function nodeFiles() {
  const fs = require('node:fs');
  const C = fs.constants;
  const info = (s) => s && {
    dir: s.isDirectory(), size: s.isDirectory() ? 0 : s.size, readonly: !s.isDirectory() && !(s.mode & 0o200),
    times: [s.birthtimeMs, s.atimeMs, s.mtimeMs],
  };
  const stat = (path) => { try { return info(fs.statSync(path)); } catch { return null; } };
  const join = (dir, name) => (dir.endsWith('\\') ? dir + name : dir + '\\' + name);
  return {
    cwd: () => process.cwd(),
    stat,
    list: (dir) => fs.readdirSync(dir).flatMap((name) => {
      const s = stat(join(dir, name));
      return s ? [{ name, ...s }] : [];
    }),
    open(path, { write, create, truncate }) {
      // libuv on Windows refuses O_TRUNC without O_CREAT.
      const fd = fs.openSync(path, (write || create || truncate ? C.O_RDWR : C.O_RDONLY) | (create ? C.O_CREAT : 0));
      if (truncate) fs.ftruncateSync(fd, 0);
      return {
        read: (target, at, length, position) => fs.readSync(fd, target, at, length, position),
        write: (source, position) => fs.writeSync(fd, source, 0, source.length, position),
        size: () => fs.fstatSync(fd).size,
        close: () => fs.closeSync(fd),
      };
    },
    remove: (path) => fs.unlinkSync(path),
    rename: (from, to) => fs.renameSync(from, to),
    mkdir: (path) => fs.mkdirSync(path),
  };
}

function es2osImports(memory, runtime, files) {
  const ERROR = ES2OS_ERROR;
  const INVALID = 0xffffffff;
  const DISPOSITION = { CREATE_NEW: 1, CREATE_ALWAYS: 2, OPEN_EXISTING: 3, OPEN_ALWAYS: 4, TRUNCATE_EXISTING: 5 };
  // GENERIC_READ, GENERIC_ALL, FILE_READ_DATA; GENERIC_WRITE, GENERIC_ALL, FILE_WRITE_DATA, FILE_APPEND_DATA
  const READ_ACCESS = 0x80000000 | 0x10000000 | 1, WRITE_ACCESS = 0x40000000 | 0x10000000 | 2, APPEND_DATA = 4;
  const SHARE_READ = 1, SHARE_WRITE = 2, SHARE_DELETE = 4;
  const EPOCH_DIFFERENCE_MS = 11644473600000;   // 1601-01-01 to 1970-01-01
  let lastError = 0;
  const handles = new Map();
  let nextHandle = 0x100;
  const mappings = new Map();
  let nextMapping = 1;
  const waitWord = new Int32Array(new SharedArrayBuffer(4));
  const start = performance.now();
  // GetTickCount counts from the boot; a browser has no uptime, so there it counts from the page's start.
  const node = typeof process !== 'undefined' && typeof require === 'function';
  const tickBase = node ? Math.floor(require('node:os').uptime() * 1000) : Math.floor(performance.now());

  const u8 = () => new Uint8Array(memory());
  const view = () => new DataView(memory());
  // ANSI strings are Windows-1252; the paths the game uses are ASCII.
  const ansi = (ptr) => {
    const bytes = u8();
    let end = ptr;
    while (bytes[end]) end++;
    return String.fromCharCode(...bytes.subarray(ptr, end));
  };
  const utf16 = (ptr) => {
    const v = view();
    let s = '';
    for (let at = ptr; ; at += 2) {
      const c = v.getUint16(at, true);
      if (!c) return s;
      s += String.fromCharCode(c);
    }
  };
  const putAnsi = (ptr, capacity, text) => {
    // GetCurrentDirectoryA's rule: the length without the NUL when it fits, else the size needed.
    if (text.length + 1 > capacity) return text.length + 1;
    const bytes = u8();
    for (let i = 0; i < text.length; i++) bytes[ptr + i] = text.charCodeAt(i) & 0xff;
    bytes[ptr + text.length] = 0;
    return text.length;
  };
  const fail = (code, result) => { lastError = code; return result; };

  // A path as the calls see it: full, without a trailing separator (except at a root).
  const resolve = (path) => fullPath(path, files.cwd()).replace(/(?<!^[A-Za-z]:)\\$/, '');
  const keyOf = (full) => full.toUpperCase();
  const isRoot = (full) => /^[A-Za-z]:\\$/.test(full);
  const parentOf = (full) => {
    const cut = full.lastIndexOf('\\');
    return cut <= 2 ? full.slice(0, 3) : full.slice(0, cut);
  };
  const stat = (full) => files.stat(full);
  const parentIsDir = (full) => !isRoot(full) && !!stat(parentOf(full))?.dir;
  const missing = (full) => (parentIsDir(full) ? ERROR.FILE_NOT_FOUND : ERROR.PATH_NOT_FOUND);
  const BYTES_ERRORS = ['media-bytes', 'no-bytes', 'part-fetch', 'part-corrupt', 'part-size', 'part-hash'];
  // A backend failure as a Win32 error; a VFS that cannot deliver bytes ends the call instead.
  const errorOf = (e, full) => {
    switch (e && e.code) {
      case 'ENOENT': case 'not-found': return missing(full);
      case 'ENOTDIR': case 'not-directory': return ERROR.PATH_NOT_FOUND;
      case 'EEXIST': case 'exists': return ERROR.ALREADY_EXISTS;
      case 'EMFILE': case 'ENFILE': return ERROR.TOO_MANY_OPEN_FILES;
      case 'EBUSY': return ERROR.SHARING_VIOLATION;
      default:
        if (BYTES_ERRORS.includes(e && e.code)) throw e;
        return ERROR.ACCESS_DENIED;
    }
  };
  const isNul = (path) => /^(?:.*[\\/])?nul(?:\.[^\\/]*)?$/i.test(path);
  const newHandle = (entry) => {
    const h = nextHandle;
    nextHandle += 4;
    handles.set(h, entry);
    return h;
  };
  const openFiles = (key) => [...handles.values()].filter((e) => e.kind === 'file' && e.key === key);
  // Whether a new open with this access and share mode conflicts with the open handles of the
  // file; an open without read or write access is never checked.
  const shareConflict = (key, read, write, share) => (read || write) && openFiles(key).some((o) =>
    (read && !(o.share & SHARE_READ)) || (write && !(o.share & SHARE_WRITE)) ||
    (o.read && !(share & SHARE_READ)) || (o.write && !(share & SHARE_WRITE)));
  const deleteConflict = (key) => openFiles(key).some((o) => !(o.share & SHARE_DELETE));
  const fileTime = (ms) => BigInt(Math.round(ms) + EPOCH_DIFFERENCE_MS) * 10000n;
  const attributesOf = (s) => (s.dir ? 0x10 : 0x20 | (s.readonly ? 1 : 0));

  function createFile(path, access, share, disposition) {
    const read = (access & READ_ACCESS) !== 0;
    const write = (access & (WRITE_ACCESS | APPEND_DATA)) !== 0;
    const append = (access & APPEND_DATA) !== 0 && (access & WRITE_ACCESS) === 0;
    if (disposition < DISPOSITION.CREATE_NEW || disposition > DISPOSITION.TRUNCATE_EXISTING) {
      return fail(ERROR.INVALID_PARAMETER, INVALID);
    }
    if (isNul(path)) {
      lastError = 0;
      return newHandle({ kind: 'null', read, write, append, pos: 0 });
    }
    const full = resolve(path);
    const s = stat(full);
    if (s && s.dir) return fail(ERROR.ACCESS_DENIED, INVALID);
    if (!s) {
      if (disposition === DISPOSITION.OPEN_EXISTING || disposition === DISPOSITION.TRUNCATE_EXISTING) {
        return fail(missing(full), INVALID);
      }
      if (!parentIsDir(full)) return fail(ERROR.PATH_NOT_FOUND, INVALID);
    }
    if (s && disposition === DISPOSITION.CREATE_NEW) return fail(ERROR.FILE_EXISTS, INVALID);
    const key = keyOf(full);
    if (s && shareConflict(key, read, write, share)) return fail(ERROR.SHARING_VIOLATION, INVALID);
    const truncate = !!s && (disposition === DISPOSITION.CREATE_ALWAYS || disposition === DISPOSITION.TRUNCATE_EXISTING);
    if (s && s.readonly && (write || truncate)) return fail(ERROR.ACCESS_DENIED, INVALID);
    let file;
    try { file = files.open(full, { write, create: !s, truncate }); } catch (e) { return fail(errorOf(e, full), INVALID); }
    // CREATE_ALWAYS and OPEN_ALWAYS on an existing file succeed with ERROR_ALREADY_EXISTS.
    lastError = s && (disposition === DISPOSITION.CREATE_ALWAYS || disposition === DISPOSITION.OPEN_ALWAYS)
      ? ERROR.ALREADY_EXISTS : 0;
    return newHandle({ kind: 'file', key, file, read, write, append, share, pos: 0 });
  }

  function file(h) {
    const entry = handles.get(h >>> 0);
    if (!entry || entry.kind === 'find') { lastError = ERROR.INVALID_HANDLE; return null; }
    return entry;
  }
  const sizeOf = (f) => (f.kind === 'null' ? 0 : f.file.size());

  // FindFirstFileA's pattern rules: "*.*" is "*"; '?' is DOS_QM; '*' before a '.' is DOS_STAR;
  // a '.' before '?' or '*' or at the end is DOS_DOT (FsRtlIsNameInExpression's DOS forms).
  const DOS_STAR = 1, DOS_QM = 2, DOS_DOT = 3;
  function dosPattern(name) {
    if (name === '*.*') return ['*'];
    const tokens = [];
    for (let i = 0; i < name.length; i++) {
      const c = name[i], next = name[i + 1];
      if (c === '?') tokens.push(DOS_QM);
      else if (c === '*') tokens.push(next === '.' ? DOS_STAR : '*');
      else if (c === '.' && (next === '?' || next === '*' || next === undefined)) tokens.push(DOS_DOT);
      else tokens.push(c.toUpperCase());
    }
    return tokens;
  }
  // '*' any run; DOS_STAR any run that does not pass the name's last dot; DOS_QM one character,
  // or none at a dot or the end; DOS_DOT a dot, or none at the end. No case.
  function matches(tokens, name) {
    const n = name.toUpperCase();
    const lastDot = n.lastIndexOf('.');
    const seen = new Set();
    const at = (t, i) => {
      if (t === tokens.length) return i === n.length;
      const memo = t * 1024 + i;
      if (seen.has(memo)) return false;
      seen.add(memo);
      const token = tokens[t];
      if (token === '*' || token === DOS_STAR) {
        const limit = token === DOS_STAR && lastDot >= i ? lastDot : n.length;
        for (let j = i; j <= limit; j++) if (at(t + 1, j)) return true;
        return false;
      }
      if (token === DOS_QM) {
        if (i < n.length && n[i] !== '.') return at(t + 1, i + 1);
        return at(t + 1, i);
      }
      if (token === DOS_DOT) {
        if (i < n.length && n[i] === '.') return at(t + 1, i + 1);
        return i === n.length && at(t + 1, i);
      }
      return i < n.length && n[i] === token && at(t + 1, i + 1);
    };
    return at(0, 0);
  }

  // WIN32_FIND_DATAA (320 bytes): attributes, three FILETIMEs, size high and low, two reserved
  // words, cFileName[260], cAlternateFileName[14] (empty: the names are long names only).
  function putFind(entry, ptr) {
    const v = view();
    u8().fill(0, ptr, ptr + 320);
    v.setUint32(ptr, attributesOf(entry), true);
    entry.times.forEach((ms, i) => v.setBigUint64(ptr + 4 + 8 * i, fileTime(ms), true));
    v.setUint32(ptr + 28, Math.floor(entry.size / 0x100000000), true);
    v.setUint32(ptr + 32, entry.size >>> 0, true);
    const bytes = u8();
    const name = entry.name.slice(0, 259);
    for (let i = 0; i < name.length; i++) bytes[ptr + 44 + i] = name.charCodeAt(i) & 0xff;
  }

  function findFirst(pattern, data) {
    const cut = Math.max(pattern.lastIndexOf('\\'), pattern.lastIndexOf('/'), /^[A-Za-z]:/.test(pattern) ? 1 : -1);
    const name = pattern.slice(cut + 1);
    if (!name) return fail(ERROR.FILE_NOT_FOUND, INVALID);
    const dir = resolve(cut < 0 ? '.' : cut === 1 && pattern[1] === ':' ? pattern.slice(0, 2) : pattern.slice(0, cut + 1));
    const d = stat(dir);
    if (!d) return fail(ERROR.PATH_NOT_FOUND, INVALID);
    if (!d.dir) return fail(ERROR.DIRECTORY, INVALID);
    let listed;
    try { listed = files.list(dir); } catch (e) { return fail(errorOf(e, dir), INVALID); }
    const upper = (s) => s.toUpperCase();
    listed.sort((a, b) => (upper(a.name) < upper(b.name) ? -1 : upper(a.name) > upper(b.name) ? 1 : 0));
    const all = isRoot(dir) ? listed : [{ ...d, name: '.' }, { ...stat(parentOf(dir)), name: '..' }, ...listed];
    const tokens = dosPattern(name);
    const entries = all.filter((e) => matches(tokens, e.name));
    if (!entries.length) return fail(ERROR.FILE_NOT_FOUND, INVALID);
    putFind(entries[0], data);
    return newHandle({ kind: 'find', entries, next: 1 });
  }

  return {
    last_error: () => lastError,
    set_last_error: (e) => { lastError = e >>> 0; },
    create_file: (path, access, share, disposition) => createFile(ansi(path), access >>> 0, share >>> 0, disposition >>> 0),
    create_file_utf16: (path, access, share, disposition) => createFile(utf16(path), access >>> 0, share >>> 0, disposition >>> 0),
    read_file: (h, buffer, size, done) => {
      view().setUint32(done, 0, true);
      const f = file(h);
      if (!f) return 0;
      if (!f.read) return fail(ERROR.ACCESS_DENIED, 0);
      let n = 0;
      if (f.kind === 'file' && size) {
        try { n = f.file.read(u8(), buffer, size >>> 0, f.pos); } catch (e) { return fail(errorOf(e, f.key), 0); }
        f.pos += n;
      }
      view().setUint32(done, n, true);
      return 1;
    },
    write_file: (h, buffer, size, done) => {
      view().setUint32(done, 0, true);
      const f = file(h);
      if (!f) return 0;
      if (!f.write) return fail(ERROR.ACCESS_DENIED, 0);
      const n = size >>> 0;
      if (f.kind === 'file' && n) {
        // FILE_APPEND_DATA without write access: every write goes to the end.
        const at = f.append ? f.file.size() : f.pos;
        try { f.file.write(u8().slice(buffer, buffer + n), at); } catch (e) { return fail(errorOf(e, f.key), 0); }
        f.pos = at + n;
      }
      view().setUint32(done, n, true);
      return 1;
    },
    set_file_pointer: (h, distance, method) => {
      const f = file(h);
      if (!f) return INVALID;
      const base = method === 0 ? 0 : method === 1 ? f.pos : method === 2 ? sizeOf(f) : -1;
      if (base < 0) return fail(ERROR.INVALID_PARAMETER, INVALID);
      const at = base + (distance | 0);
      if (at < 0) return fail(ERROR.NEGATIVE_SEEK, INVALID);
      f.pos = at;
      return at >>> 0;
    },
    file_size: (h) => {
      const f = file(h);
      return f ? sizeOf(f) >>> 0 : INVALID;
    },
    file_type: (h) => {
      const f = file(h);
      if (!f) return 0;
      return f.kind === 'null' ? 2 : 1;   // FILE_TYPE_CHAR, FILE_TYPE_DISK
    },
    close_handle: (h) => {
      if (h >>> 0 === INVALID) return 1;   // the pseudo-handle of the current process
      const entry = handles.get(h >>> 0);
      if (!entry || entry.kind === 'find') return fail(ERROR.INVALID_HANDLE, 0);
      handles.delete(h >>> 0);
      if (entry.kind === 'file') entry.file.close();
      return 1;
    },
    file_attributes: (ptr) => {
      const full = resolve(ansi(ptr));
      const s = stat(full);
      return s ? attributesOf(s) : fail(missing(full), INVALID);
    },
    delete_file: (ptr) => {
      const full = resolve(ansi(ptr));
      const s = stat(full);
      if (!s) return fail(missing(full), 0);
      if (s.dir) return fail(ERROR.ACCESS_DENIED, 0);
      if (deleteConflict(keyOf(full))) return fail(ERROR.SHARING_VIOLATION, 0);
      try { files.remove(full); } catch (e) { return fail(errorOf(e, full), 0); }
      return 1;
    },
    create_directory: (ptr) => {
      const full = resolve(ansi(ptr));
      if (stat(full)) return fail(ERROR.ALREADY_EXISTS, 0);
      if (!parentIsDir(full)) return fail(ERROR.PATH_NOT_FOUND, 0);
      try { files.mkdir(full); } catch (e) { return fail(errorOf(e, full), 0); }
      return 1;
    },
    // _stat64's st_mtime; the C runtime leaves the last error 0 or that of the lookup.
    file_mtime: (ptr, out) => {
      const full = resolve(ansi(ptr));
      const s = stat(full);
      if (!s) return fail(missing(full), 0);
      lastError = 0;
      view().setBigInt64(out, BigInt(Math.floor(s.times[2] / 1000)), true);
      return 1;
    },
    move_file_replace: (fromPtr, toPtr) => {
      const from = resolve(ansi(fromPtr)), to = resolve(ansi(toPtr));
      const s = stat(from);
      if (!s) return fail(missing(from), 0);
      const t = stat(to);
      if (!t && !parentIsDir(to)) return fail(ERROR.PATH_NOT_FOUND, 0);
      if (t && t.dir) return fail(ERROR.ACCESS_DENIED, 0);
      const fromKey = keyOf(from), toKey = keyOf(to);
      if (deleteConflict(fromKey)) return fail(ERROR.SHARING_VIOLATION, 0);
      if (t && toKey !== fromKey && openFiles(toKey).length) return fail(ERROR.ACCESS_DENIED, 0);
      try { files.rename(from, to); } catch (e) { return fail(errorOf(e, from), 0); }
      for (const o of openFiles(fromKey)) o.key = toKey;
      return 1;
    },
    find_first: (patternPtr, data) => findFirst(ansi(patternPtr), data),
    find_next: (h, data) => {
      const entry = handles.get(h >>> 0);
      if (!entry || entry.kind !== 'find') return fail(ERROR.INVALID_HANDLE, 0);
      if (entry.next >= entry.entries.length) return fail(ERROR.NO_MORE_FILES, 0);
      putFind(entry.entries[entry.next++], data);
      return 1;
    },
    find_close: (h) => {
      const entry = handles.get(h >>> 0);
      if (!entry || entry.kind !== 'find') return fail(ERROR.INVALID_HANDLE, 0);
      handles.delete(h >>> 0);
      return 1;
    },
    // FileTimeToLocalFileTime's bias in minutes (UTC = local + bias): today's, whatever the
    // instant of the FILETIME.
    time_zone_bias: () => new Date().getTimezoneOffset(),
    current_directory: (out, capacity) => putAnsi(out, capacity >>> 0, files.cwd()),
    full_path: (ptr, out, capacity) => putAnsi(out, capacity >>> 0, fullPath(ansi(ptr), files.cwd())),
    counter: () => (node ? process.hrtime.bigint() / 100n : BigInt(Math.floor((performance.timeOrigin + performance.now()) * 10000))),
    counter_frequency: () => 10000000n,
    tick_count: () => (tickBase + Math.floor(performance.now() - start)) >>> 0,
    local_time: (out) => systemTime(out, false),
    system_time: (out) => systemTime(out, true),
    time_zone: (out) => timeZone(out),
    sleep: (ms) => { Atomics.wait(waitWord, 0, 0, ms >>> 0); },
    map_view: (h, bytes, size) => {
      const entry = handles.get(h >>> 0);
      if (!entry || entry.kind !== 'file' || !entry.file.map) return 0;
      const at = bytes >>> 0, end = at + (size >>> 0);
      let mapping;
      try { mapping = entry.file.map(() => u8().subarray(at, end)); } catch (e) { errorOf(e, entry.key); return 0; }
      if (!mapping) return 0;
      const id = nextMapping++;
      mappings.set(id, mapping);
      return id;
    },
    view_ensure: (id, offset, length) => {
      const mapping = mappings.get(id >>> 0);
      if (mapping) mapping.ensure(offset >>> 0, length >>> 0);
    },
    unmap_view: (id) => {
      const mapping = mappings.get(id >>> 0);
      mappings.delete(id >>> 0);
      if (mapping && mapping.close) mapping.close();
    },
    heap_reserve: (address, size) => (runtime.grow((address >>> 0) + (size >>> 0)) ? address : 0),
    exit: (code) => { runtime.exit(code | 0); },
  };

  function systemTime(out, utc) {
    const d = new Date();
    const v = view();
    const fields = utc
      ? [d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDay(), d.getUTCDate(), d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds(), d.getUTCMilliseconds()]
      : [d.getFullYear(), d.getMonth() + 1, d.getDay(), d.getDate(), d.getHours(), d.getMinutes(), d.getSeconds(), d.getMilliseconds()];
    fields.forEach((value, i) => v.setUint16(out + 2 * i, value, true));
  }

  // TIME_ZONE_INFORMATION (172 bytes): Bias, StandardName[32], StandardDate, StandardBias,
  // DaylightName[32], DaylightDate, DaylightBias. The transition dates are this year's, in the
  // "n-th weekday of the month" form (wYear 0, wDay 1..5, 5 the last). The names stay empty.
  function timeZone(out) {
    const v = view();
    const year = new Date().getFullYear();
    const offset = (ms) => new Date(ms).getTimezoneOffset();
    const jan = offset(Date.UTC(year, 0, 1)), jul = offset(Date.UTC(year, 6, 1));
    const standard = Math.max(jan, jul), daylight = Math.min(jan, jul);
    v.setInt32(out, standard, true);
    if (standard === daylight) return 0;   // TIME_ZONE_ID_UNKNOWN: no daylight saving time
    v.setInt32(out + 168, daylight - standard, true);
    const transition = (from, to) => {
      // the first minute of the year at which the offset changes from `from` to `to`
      let lo = Date.UTC(year, 0, 1), hi = Date.UTC(year + 1, 0, 1);
      for (let t = lo; t < hi; t += 86400000) {
        if (offset(t) === from && offset(t + 86400000) === to) { lo = t; hi = t + 86400000; break; }
      }
      while (hi - lo > 60000) {
        const mid = lo + Math.floor((hi - lo) / 120000) * 60000;
        if (offset(mid) === from) lo = mid; else hi = mid;
      }
      return hi;
    };
    const put = (at, utcMs, bias) => {
      const local = new Date(utcMs - bias * 60000);   // the wall clock before the change
      const day = local.getUTCDate(), month = local.getUTCMonth();
      const last = new Date(Date.UTC(year, month + 1, 0)).getUTCDate();
      const week = day + 7 > last ? 5 : Math.floor((day - 1) / 7) + 1;
      [0, month + 1, local.getUTCDay(), week, local.getUTCHours(), local.getUTCMinutes(), 0, 0]
        .forEach((value, i) => v.setUint16(at + 2 * i, value, true));
    };
    put(out + 68, transition(daylight, standard), daylight);
    put(out + 152, transition(standard, daylight), standard);
    const now = offset(Date.now());
    return now === daylight ? 2 : 1;   // TIME_ZONE_ID_DAYLIGHT, TIME_ZONE_ID_STANDARD
  }
}
