// The self-test's check of a suite replay (docs/web_build.md section 16): the SHA-256 of a raw
// tick log with the host fields masked as sim/tests/tick_log_raw_diff.py masks them. Lines are
// split at "\n", a trailing "\r" and a last empty line are dropped. A UTF-8 line that is a JSON
// object keeps its text except the value of each top-level pid, tid, qpc and dep_flags, which
// becomes 0; any other line stays as it is. The values are masked in the text, not
// re-serialised: the tick records' qpc and the call counts exceed 2^53.
//
// Two logs of one writer hash equal exactly when tick_log_raw_diff.py finds no difference outside
// the host fields (web/node/tick_log_hash_test.mjs).
export const HOST_FIELDS = ['pid', 'tid', 'qpc', 'dep_flags'];

const MASK = '0';

// The end of the JSON string that starts at text[at] (a quote), or -1.
function stringEnd(text, at) {
  for (let i = at + 1; i < text.length; i++) {
    const c = text[i];
    if (c === '\\') i++;
    else if (c === '"') return i + 1;
  }
  return -1;
}

// The end of the JSON value that starts at text[at].
function valueEnd(text, at) {
  const c = text[at];
  if (c === '"') return stringEnd(text, at);
  if (c === '{' || c === '[') {
    let depth = 0;
    for (let i = at; i < text.length; i++) {
      const d = text[i];
      if (d === '"') i = stringEnd(text, i) - 1;
      else if (d === '{' || d === '[') depth++;
      else if (d === '}' || d === ']') {
        depth--;
        if (depth === 0) return i + 1;
      }
    }
    return -1;
  }
  let i = at;
  while (i < text.length && !',}] \t\r\n'.includes(text[i])) i++;
  return i;
}

function skipSpace(text, i) {
  while (i < text.length && ' \t\r\n'.includes(text[i])) i++;
  return i;
}

// The line with its top-level host fields masked, or the line itself when it is no JSON object.
// JSON.parse checks the syntax first, so the scan below never meets a malformed value.
export function maskLine(line) {
  let parsed;
  try {
    parsed = JSON.parse(line);
  } catch {
    return line;
  }
  if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) return line;
  let out = '';
  let copied = 0;
  let i = skipSpace(line, 0) + 1;
  for (;;) {
    i = skipSpace(line, i);
    if (line[i] === '}') break;
    const keyEnd = stringEnd(line, i);
    const key = JSON.parse(line.slice(i, keyEnd));
    i = skipSpace(line, skipSpace(line, keyEnd) + 1);
    const end = valueEnd(line, i);
    if (HOST_FIELDS.includes(key)) {
      out += line.slice(copied, i) + MASK;
      copied = end;
    }
    i = skipSpace(line, end);
    if (line[i] === ',') i++;
  }
  return out + line.slice(copied);
}

// The log's lines as tick_log_raw_diff.py reads them, as byte views.
export function logLines(bytes) {
  const lines = [];
  let start = 0;
  for (let i = 0; i <= bytes.length; i++) {
    if (i < bytes.length && bytes[i] !== 0x0a) continue;
    if (i === bytes.length && start === bytes.length) break;
    const end = i > start && bytes[i - 1] === 0x0d ? i - 1 : i;
    lines.push(bytes.subarray(start, end));
    start = i + 1;
  }
  return lines;
}

// The masked log, each line followed by "\n", and its line count.
export function maskLog(bytes) {
  const decoder = new TextDecoder('utf-8', { fatal: true });
  const encoder = new TextEncoder();
  const parts = [];
  let length = 0;
  for (const line of logLines(bytes)) {
    let text = null;
    try {
      text = decoder.decode(line);
    } catch {
      // no UTF-8: compared as bytes, as tick_log_raw_diff.py compares it
    }
    const part = text === null ? line : encoder.encode(maskLine(text));
    parts.push(part);
    length += part.length + 1;
  }
  const out = new Uint8Array(length);
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
    out[at++] = 0x0a;
  }
  return { bytes: out, lines: parts.length };
}

// The masked log's SHA-256 as lower-case hex, and its line count.
export async function tickLogHash(bytes, subtle = globalThis.crypto.subtle) {
  const masked = maskLog(bytes);
  const digest = new Uint8Array(await subtle.digest('SHA-256', masked.bytes));
  return { sha256: Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join(''), lines: masked.lines };
}
