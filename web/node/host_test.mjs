// os_host.mjs against os_win32.c: platform/tests/os_calls.c's script of file calls runs natively
// (os_win32.c on NTFS) and as wasm (os_wasm.c and the "es2os" imports) over node:fs and over
// an empty web/vfs.mjs file system; the three transcripts must agree line by line, the error
// codes and the calls that keep the last error included. The clock values agree within a
// tolerance, the frequency, the time zone and the local-time bias exactly, with host.mjs as
// the runner. heap_reserve, exit
// and a VFS without the bytes are checked directly.
//
// Arguments: os_calls.exe os_calls.js
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Vfs } from '../vfs.mjs';
import { ERROR, es2osHost, es2osImports, fullPath, nodeFiles, vfsFiles } from './os_host.mjs';

const [nativeExe, wasmJs] = process.argv.slice(2);
const tmp = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR || os.tmpdir(), 'host-'));

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};

const factory = (await import(pathToFileURL(path.resolve(wasmJs)).href)).default;

async function wasm(args, files) {
  const lines = [];
  try {
    await factory({ arguments: args, print: (s) => lines.push(s), printErr: (s) => lines.push(s), es2os: es2osHost(files) });
  } catch (e) {
    if (!(e && e.name === 'ExitStatus')) throw e;
  }
  return lines;
}

function native(args, cwd) {
  const run = spawnSync(nativeExe, args, { cwd, encoding: 'latin1' });
  check(run.status === 0, `native ${args.join(' ')}: exit ${run.status}`);
  return run.stdout.split(/\r?\n/).filter((l) => l !== '');
}

// The cwd and full lines with the base's and its parent's length taken out: "-> 50 $\x.dat"
// becomes "-> $+6 $\x.dat", "-> 41 D:\T" becomes "-> %+0 %".
function normalized(lines, base) {
  const parent = path.win32.dirname(base);
  return lines.map((line) => {
    const m = /^((?:cwd|full) .* -> )(\d+) (\S*)( e=\d+)$/.exec(line);
    if (!m) return line;
    const n = Number(m[2]);
    if (m[3].startsWith('$') || !m[3].toUpperCase().startsWith(parent.toUpperCase())) {
      return `${m[1]}$+${n - base.length} ${m[3]}${m[4]}`;
    }
    return `${m[1]}%+${n - parent.length} %${m[3].slice(parent.length)}${m[4]}`;
  });
}

function compare(label, expected, actual) {
  check(expected.length === actual.length, `${label}: ${actual.length} lines, native ${expected.length}`);
  for (let i = 0; i < Math.max(expected.length, actual.length); i++) {
    if (expected[i] !== actual[i]) {
      check(false, `${label} line ${i + 1}: native "${expected[i]}", host "${actual[i]}"`);
      return;
    }
  }
  check(true, label);
}

// The script: natively in n, as wasm over node:fs in w (the working directory is the base),
// as wasm over a VFS in C:\T\W.
const nativeBase = path.join(tmp, 'n'), nodeBase = path.join(tmp, 'w');
fs.mkdirSync(nativeBase);
fs.mkdirSync(nodeBase);
const expected = normalized(native([nativeBase], nativeBase), nativeBase);
check(expected.length > 200, `native transcript: ${expected.length} lines`);
const cwd = process.cwd();
process.chdir(nodeBase);
const overNode = normalized(await wasm([nodeBase], nodeFiles()), nodeBase);
process.chdir(cwd);
compare('node:fs', expected, overNode);
const vfsBase = 'C:\\T\\W';
const vfs = new Vfs({ manifest: { directories: [vfsBase], files: [] }, codec: null, fetchPart: () => null });
compare('vfs', expected, normalized(await wasm([vfsBase], vfsFiles(vfs, { cwd: vfsBase })), vfsBase));

// The clocks.
const field = (lines, name) => lines.find((l) => l.startsWith(name + ' ')) ?? '';
// The wasm run goes through host.mjs with TZ=UTC, which os_win32.c ignores and so must the host.
const nativeClocks = native(['--clocks'], tmp);
const hostRun = spawnSync(process.execPath, [path.join(path.dirname(fileURLToPath(import.meta.url)), 'host.mjs'), wasmJs, '--clocks'],
  { encoding: 'latin1', env: { ...process.env, TZ: 'UTC' } });
check(hostRun.status === 0, `host.mjs --clocks: exit ${hostRun.status} ${hostRun.stderr}`);
const wasmClocks = hostRun.stdout.split(/\r?\n/).filter((l) => l !== '');
for (const name of ['frequency', 'zone', 'to_local 0', 'to_local 1']) {
  check(field(nativeClocks, name) !== '' && field(nativeClocks, name) === field(wasmClocks, name),
    `${name}: native "${field(nativeClocks, name)}", host "${field(wasmClocks, name)}"`);
}
const when = (line) => {
  const [y, mo, , d, h, mi, s, ms] = line.split(' ').slice(1).map(Number);
  return Date.UTC(y, mo - 1, d, h, mi, s, ms);
};
for (const name of ['local', 'system']) {
  const a = when(field(nativeClocks, name)), b = when(field(wasmClocks, name));
  check(Math.abs(a - b) < 5000, `${name}: native ${field(nativeClocks, name)}, host ${field(wasmClocks, name)}`);
}
const tick = (lines) => Number(field(lines, 'tick').split(' ')[1]);
check(Math.abs(tick(nativeClocks) - tick(wasmClocks)) < 5000, `tick: native ${tick(nativeClocks)}, host ${tick(wasmClocks)}`);

// heap_reserve and exit reach the runtime; the result of a reservation is its address or 0.
{
  const grown = [], exits = [];
  let canGrow = true;
  const memory = new ArrayBuffer(65536);
  const imports = es2osImports(() => memory, { grow: (bytes) => { grown.push(bytes); return canGrow; }, exit: (c) => exits.push(c) }, nodeFiles());
  check(imports.heap_reserve(0x10000, 0x30000) === 0x10000 && grown.at(-1) === 0x40000, 'heap_reserve grows to the end');
  check((imports.heap_reserve(0x80000000 | 0, 0x10000) >>> 0) === 0x80000000 && grown.at(-1) === 0x80010000, 'heap_reserve: unsigned address');
  canGrow = false;
  check(imports.heap_reserve(0x10000, 0x30000) === 0, 'heap_reserve fails with 0');
  imports.exit(3);
  imports.exit(0xc0000005 | 0);
  check(exits[0] === 3 && exits[1] === -1073741819, `exit codes ${exits}`);
  check(fullPath('d:x', 'C:\\A') === 'd:\\x' && fullPath('c:x', 'C:\\A') === 'C:\\A\\x' && fullPath('\\x', 'C:\\A') === 'C:\\x',
    'drive-relative and rooted paths');
}

// A VFS file without bytes (a media file of the CD) ends the call with the VfsError instead of
// a Win32 error, for the runtime to report.
{
  const memory = new ArrayBuffer(65536);
  const u8 = new Uint8Array(memory);
  const put = (at, s) => { for (let i = 0; i < s.length; i++) u8[at + i] = s.charCodeAt(i); u8[at + s.length] = 0; };
  const media = new Vfs({ manifest: { files: [{ path: 'D:\\MOVIE.AVI', size: 4, flags: ['no-bytes', 'media'] }] }, codec: null, fetchPart: () => null });
  const imports = es2osImports(() => memory, { grow: () => false, exit: () => {} }, vfsFiles(media, { cwd: 'D:\\' }));
  put(0, 'D:\\movie.avi');
  const h = imports.create_file(0, 0x80000000, 1, 3);
  check(h !== 0xffffffff && imports.file_size(h) === 4, 'media file opens with its size');
  let code = null;
  try { imports.read_file(h, 100, 4, 200); } catch (e) { code = e.code; }
  check(code === 'media-bytes', `media read: ${code}`);
  check(imports.file_attributes(0) === 0x20 && ERROR.SHARING_VIOLATION === 32, 'media file attributes');
}

// vfsFiles refuses a replacing rename onto an open target itself, so the target's last close
// cannot write its bytes over the moved file.
{
  const v = new Vfs({ manifest: { directories: ['C:\\R'], files: [] }, codec: null, fetchPart: () => null });
  const files = vfsFiles(v, { cwd: 'C:\\R' });
  const from = files.open('C:\\R\\NEW.DAT', { create: true });
  from.write(Uint8Array.of(1, 2), 0);
  from.close();
  const target = files.open('C:\\R\\OLD.DAT', { create: true });
  target.write(Uint8Array.of(9), 0);
  let code = null;
  try { files.rename('C:\\R\\NEW.DAT', 'C:\\R\\old.dat'); } catch (e) { code = e.code; }
  target.close();
  check(code === 'EACCES' && v.readAll('C:\\R\\NEW.DAT').join() === '1,2' && v.readAll('C:\\R\\OLD.DAT').join() === '9',
    `rename onto an open target: ${code}`);
  files.rename('C:\\R\\NEW.DAT', 'C:\\R\\old.dat');
  check(v.readAll('C:\\R\\OLD.DAT').join() === '1,2' && !v.stat('C:\\R\\NEW.DAT'), 'rename onto the closed target replaces it');
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(`${checks - failures.length}/${checks} checks passed`);
process.exit(failures.length ? 1 : 0);
