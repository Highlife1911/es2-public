// The save upload of the landing page (docs/web_build.md section 13, step 3). Each file is
// checked by tools.wasm (//web:tools_wasm, web/src/tools_checks.c) under its name without
// directory; when every save file passes, they are written unchanged into C:\ES2\SAV\, and when
// one fails, nothing is written.
//
//   const checker = await loadChecker(factory);        // factory: tools.js's default export
//   const result = await importSaves(files, checker, write);
//
// files: File objects (the name is taken from webkitRelativePath or name) or { name, bytes }
// records, such as the entries of the uploaded ZIP (web/unzip.mjs). write(path, bytes) is awaited once per accepted file, in name order,
// only when the whole upload passed (packages wire it to the Vfs's write and the overlay store).
//
// The result: { ok, written: [{ name, path, bytes }], messages: [{ key, params }] }. The keys
// are those of web/texts.mjs (saves.*):
//   saves.ignored {name}                  not a save file name; skipped
//   saves.failed {name, reason, offset}   reason is a saves.reason.* key; nothing is written
//   saves.duplicate {name}                two files with this name (case-insensitive)
//   saves.nothing_written                 after any failure
//   saves.none                            no save file in the upload
//   saves.written {count}                 the files written
//   saves.language_note                   with every write: mission texts stay in their language

export const SAV_DIR = 'C:\\ES2\\SAV';

// web/src/tools_checks.c: es2t_check's results (ES2T_NAME, then es2_status of
// formats/include/es2/formats/common.h).
export const NOT_A_SAVE = -1;
export const REASONS = [
  'ok', 'argument', 'truncated', 'magic', 'version', 'count', 'offset', 'length',
  'overlap', 'string', 'unsupported', 'capacity', 'depth', 'callback', 'not_found',
];

export async function loadChecker(factory) {
  const m = await factory();
  const cString = (s) => {
    const p = m._malloc(s.length + 1);
    for (let i = 0; i < s.length; i++) m.HEAPU8[p + i] = s.charCodeAt(i);
    m.HEAPU8[p + s.length] = 0;
    return p;
  };
  // Only printable ASCII names reach the module; any other name is no save file.
  const ascii = (name) => /^[\x21-\x7e]+$/.test(name);
  return {
    kind(name) {
      if (!ascii(name)) return 0;
      const p = cString(name);
      try { return m._es2t_kind(p); } finally { m._free(p); }
    },
    // { status, reason, offset }: status 0 passes, NOT_A_SAVE for another name, else the
    // parser's es2_status; reason is its REASONS name.
    check(name, bytes) {
      if (!ascii(name)) return { status: NOT_A_SAVE, reason: 'name', offset: 0 };
      const p = cString(name);
      const data = m._malloc(bytes.length || 1);
      const offset = m._malloc(4);
      try {
        m.HEAPU8.set(bytes, data);
        const status = m._es2t_check(p, data, bytes.length, offset);
        const at = m.HEAPU32[offset >>> 2];
        return { status, reason: status < 0 ? 'name' : (REASONS[status] ?? 'invalid'), offset: status > 0 ? at : 0 };
      } finally {
        m._free(offset);
        m._free(data);
        m._free(p);
      }
    },
  };
}

// The file's name without its folder: "SAV/GAME_0.SAV" -> "GAME_0.SAV".
export function baseName(path) {
  const s = String(path);
  return s.slice(Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\')) + 1);
}

async function bytesOf(file) {
  if (file.bytes) return file.bytes instanceof Uint8Array ? file.bytes : new Uint8Array(file.bytes);
  return new Uint8Array(await file.arrayBuffer());
}

// The checks alone: { ok, files: [{ name, path, bytes }], messages } without writing.
export async function checkSaves(files, checker) {
  const messages = [];
  const accepted = new Map();
  const duplicates = new Set();
  const failures = [];
  for (const file of Array.from(files ?? [])) {
    const name = baseName(file.webkitRelativePath || file.name);
    if (!checker.kind(name)) {
      messages.push({ key: 'saves.ignored', params: { name } });
      continue;
    }
    const key = name.toUpperCase();
    if (accepted.has(key)) {
      duplicates.add(key);
      continue;
    }
    const bytes = await bytesOf(file);
    const result = checker.check(name, bytes);
    accepted.set(key, { name, path: `${SAV_DIR}\\${name}`, bytes });
    if (result.status !== 0) failures.push({ name, reason: `saves.reason.${result.reason}`, offset: result.offset });
  }
  for (const f of failures) messages.push({ key: 'saves.failed', params: f });
  for (const key of [...duplicates].sort()) messages.push({ key: 'saves.duplicate', params: { name: accepted.get(key).name } });
  const ok = failures.length === 0 && duplicates.size === 0;
  if (!ok) messages.push({ key: 'saves.nothing_written', params: {} });
  else if (accepted.size === 0) messages.push({ key: 'saves.none', params: {} });
  const sorted = [...accepted.keys()].sort().map((k) => accepted.get(k));
  return { ok, files: ok ? sorted : [], messages };
}

// The checks and, when they pass, the writes.
export async function importSaves(files, checker, write) {
  const { ok, files: accepted, messages } = await checkSaves(files, checker);
  if (!ok || accepted.length === 0) return { ok, written: [], messages };
  for (const f of accepted) await write(f.path, f.bytes);
  messages.push({ key: 'saves.written', params: { count: accepted.length } });
  messages.push({ key: 'saves.language_note', params: {} });
  return { ok, written: accepted, messages };
}
