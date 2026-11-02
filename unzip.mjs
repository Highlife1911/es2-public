// The save upload's ZIP reader (docs/web_build.md section 13): the entries of a ZIP as the
// export writes it (stored) or as another packer writes it (Deflate, method 8), each checked
// against its CRC-32 (PKWARE APPNOTE 6.3: the end record, the central directory, the local
// headers; no ZIP64, no encryption, one disk).
//
//   const entries = await readZip(bytes, { inflateRaw });   // [{ name, bytes }], folders left out
//
// inflateRaw(bytes, limit) resolves to the raw-Deflate data inflated, and throws once it would
// exceed `limit` bytes (inflateStream below in the page). The sizes and offsets come from the
// central directory, so entries with a data descriptor (flag bit 3) need nothing else. A name
// is UTF-8 with flag bit 11, else its bytes as Latin-1 (an accepted save name is ASCII either
// way). A malformed archive throws ZipError; its reason is a key of web/texts.mjs (saves.zip.*).
import { crc32 } from './zip.mjs';

export const MAX_ENTRY_BYTES = 16 << 20;
export const MAX_TOTAL_BYTES = 64 << 20;

export class ZipError extends Error {
  constructor(reason, message) {
    super(message);
    this.reason = reason;
  }
}

const EOCD = 0x06054b50;
const CENTRAL = 0x02014b50;
const LOCAL = 0x04034b50;

function endRecord(view) {
  const last = view.byteLength - 22;
  for (let at = last; at >= 0 && at >= last - 0xffff; at--) {
    if (view.getUint32(at, true) === EOCD && at + 22 + view.getUint16(at + 20, true) === view.byteLength) return at;
  }
  throw new ZipError('format', 'zip: no end record');
}

export async function readZip(bytes, { inflateRaw }) {
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const end = endRecord(view);
  const u16 = (at) => view.getUint16(at, true);
  const u32 = (at) => view.getUint32(at, true);
  if (u16(end + 4) !== 0 || u16(end + 6) !== 0 || u16(end + 8) !== u16(end + 10)) {
    throw new ZipError('unsupported', 'zip: more than one disk');
  }
  const count = u16(end + 10);
  const size = u32(end + 12);
  const start = u32(end + 16);
  if (count === 0xffff || size === 0xffffffff || start === 0xffffffff) throw new ZipError('unsupported', 'zip: ZIP64');
  if (start + size > end) throw new ZipError('format', 'zip: central directory outside the file');
  const entries = [];
  let total = 0;
  let at = start;
  for (let i = 0; i < count; i++) {
    if (at + 46 > start + size || u32(at) !== CENTRAL) throw new ZipError('format', `zip: central record ${i}`);
    const flags = u16(at + 8);
    const method = u16(at + 10);
    const crc = u32(at + 16);
    const packed = u32(at + 20);
    const length = u32(at + 24);
    const nameLength = u16(at + 28);
    const next = at + 46 + nameLength + u16(at + 30) + u16(at + 32);
    const local = u32(at + 42);
    if (next > start + size) throw new ZipError('format', `zip: central record ${i}`);
    const nameBytes = data.subarray(at + 46, at + 46 + nameLength);
    const name = flags & 0x0800 ? new TextDecoder().decode(nameBytes) : String.fromCharCode(...nameBytes);
    at = next;
    if (flags & 0x0001) throw new ZipError('unsupported', `zip: ${name} is encrypted`);
    if (method !== 0 && method !== 8) throw new ZipError('unsupported', `zip: ${name} uses method ${method}`);
    if (local + 30 > start || u32(local) !== LOCAL) throw new ZipError('format', `zip: local header of ${name}`);
    const from = local + 30 + u16(local + 26) + u16(local + 28);
    if (from + packed > start) throw new ZipError('format', `zip: data of ${name}`);
    if (name.endsWith('/') && length === 0) continue;
    total += length;
    if (length > MAX_ENTRY_BYTES || total > MAX_TOTAL_BYTES) throw new ZipError('size', `zip: ${name} is too large`);
    const stored = data.subarray(from, from + packed);
    let content;
    if (method === 0) {
      if (packed !== length) throw new ZipError('format', `zip: sizes of ${name}`);
      content = stored.slice();
    } else {
      content = await inflateRaw(stored, length);
      if (content.length !== length) throw new ZipError('format', `zip: inflated size of ${name}`);
    }
    if (crc32(content) !== crc) throw new ZipError('crc', `zip: CRC of ${name}`);
    entries.push({ name, bytes: content });
  }
  return entries;
}

// inflateRaw over a DecompressionStream ('deflate-raw', Chrome 80, Firefox 113) of `env`;
// stops reading past `limit` bytes, so a small archive cannot unpack without bound.
export function inflateStream(env) {
  return async (bytes, limit) => {
    const reader = new env.Blob([bytes]).stream().pipeThrough(new env.DecompressionStream('deflate-raw')).getReader();
    const parts = [];
    let length = 0;
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        length += value.length;
        if (length > limit) throw new ZipError('format', 'zip: inflated data longer than its size');
        parts.push(value);
      }
    } catch (e) {
      reader.cancel().catch(() => {});
      throw e instanceof ZipError ? e : new ZipError('format', `zip: ${e.message}`);
    }
    const out = new Uint8Array(length);
    let at = 0;
    for (const p of parts) { out.set(p, at); at += p.length; }
    return out;
  };
}
