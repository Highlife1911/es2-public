// The save export of the landing page (docs/web_build.md section 13): a ZIP of C:\ES2\SAV\ with
// stored (uncompressed) entries and their CRC-32, written without a library (PKWARE APPNOTE
// 6.3: local headers, central directory, end record; no ZIP64, no data descriptors).
//
//   const zip = zipStored([{ name: 'GAME_0.SAV', bytes }], { date });   // Uint8Array
//
// Entries keep their order. A name is stored with "/" separators, as UTF-8 with flag bit 11
// when it is not ASCII. date (a Date, local time as DOS stores it) defaults to 1980-01-01
// 00:00, so equal inputs give equal bytes.

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// CRC-32 (ISO-HDLC, reflected polynomial 0xEDB88320) of bytes, continuing from crc.
export function crc32(bytes, crc = 0) {
  let c = ~crc >>> 0;
  for (let i = 0; i < bytes.length; i++) c = TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

function dosDateTime(date) {
  if (!date) return { time: 0, day: (1 << 5) | 1 };
  const year = date.getFullYear();
  if (year < 1980 || year > 2107) throw new RangeError(`zip: date outside the DOS range: ${date}`);
  return {
    time: (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1),
    day: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate(),
  };
}

export function zipStored(entries, { date = null } = {}) {
  const { time, day } = dosDateTime(date);
  const list = Array.from(entries);
  if (list.length > 0xffff) throw new RangeError('zip: more than 65535 entries');
  const encoder = new TextEncoder();
  const prepared = list.map(({ name, bytes }) => {
    const path = String(name).replace(/\\/g, '/');
    if (!path) throw new RangeError('zip: empty entry name');
    const nameBytes = encoder.encode(path);
    if (nameBytes.length > 0xffff) throw new RangeError(`zip: name too long: ${path}`);
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    return { nameBytes, data, utf8: /[^\x00-\x7f]/.test(path), crc: crc32(data) };
  });
  let size = 22;
  for (const e of prepared) size += 30 + e.nameBytes.length + e.data.length + 46 + e.nameBytes.length;
  if (size > 0xffffffff) throw new RangeError('zip: larger than 4 GiB');
  const out = new Uint8Array(size);
  const view = new DataView(out.buffer);
  let at = 0;
  const u16 = (v) => { view.setUint16(at, v, true); at += 2; };
  const u32 = (v) => { view.setUint32(at, v >>> 0, true); at += 4; };
  const put = (b) => { out.set(b, at); at += b.length; };
  // Fields shared by the local header (after its signature) and the central record (after
  // "version made by"): version needed 1.0, flags, method 0, time, date, CRC, sizes, name length.
  const common = (e) => {
    u16(10); u16(e.utf8 ? 0x0800 : 0); u16(0); u16(time); u16(day);
    u32(e.crc); u32(e.data.length); u32(e.data.length); u16(e.nameBytes.length);
  };
  for (const e of prepared) {
    e.offset = at;
    u32(0x04034b50); common(e); u16(0); put(e.nameBytes); put(e.data);
  }
  const central = at;
  for (const e of prepared) {
    u32(0x02014b50); u16(20); common(e);
    u16(0); u16(0); u16(0); u16(0); u32(0); u32(e.offset); put(e.nameBytes);
  }
  const centralSize = at - central;
  u32(0x06054b50); u16(0); u16(0); u16(prepared.length); u16(prepared.length);
  u32(centralSize); u32(central); u16(0);
  return out;
}
