// web/saves.mjs over tools.wasm on synthetic save files (the shapes of formats/tests/test_sav.c,
// test_player_mec.c, test_script.c and test_str_cfg.c): the accepted names, each parser's verdict on valid,
// truncated and corrupted files, and whole uploads: a valid folder, a broken one (nothing is
// written), a partial one, single files, duplicates and an upload without save files. Every
// message the upload returns has a text in web/texts.mjs.
//
// Arguments: tools.js
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { NOT_A_SAVE, SAV_DIR, baseName, checkSaves, importSaves, loadChecker } from '../saves.mjs';
import { TEXTS, text } from '../texts.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const equal = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

const [toolsJs] = process.argv.slice(2);
const checker = await loadChecker((await import(pathToFileURL(path.resolve(toolsJs)).href)).default);

// ---- fixtures ------------------------------------------------------------------------------
function builder(capacity) {
  const b = new Uint8Array(capacity);
  const w = (at, x) => { b[at] = x & 0xff; b[at + 1] = (x >> 8) & 0xff; };
  return { b, w };
}

// formats/tests/test_sav.c valid_save: inside the loader-safe domain.
function validSave() {
  const { b, w } = builder(6000);
  let p = 0;
  for (let i = 0; i < 33; i++) {
    const n = i % 3 === 1 ? 2 : 0;
    b[p++] = i & 1; w(p, n); p += 2;
    for (let j = 0; j < n; j++) { w(p, i); w(p + 2, i); w(p + 4, 100); w(p + 6, 100); w(p + 8, 5); p += 10; }
  }
  for (let i = 0; i < 22; i++) b[p++] = i;
  for (let i = 0; i < 152; i++) b[p++] = (i * 7) & 0xff;
  for (let i = 0; i < 36; i++) {
    w(p, i); w(p + 2, i * 3); w(p + 4, 2 + i % 5); p += 6;
    for (let j = 0; j < 1 + i % 5; j++) b[p++] = 0x41 + j;
    b[p++] = 0;
    for (let j = 0; j < 25; j++) b[p++] = (i + j) & 0xff;
  }
  for (let i = 0; i < 6; i++) { w(p, i); p += 2; }
  w(p, 2); w(p + 2, 1); p += 4;
  w(p, 0); w(p + 2, 7); w(p + 4, 4); b.set([0x45, 0x67, 0x6f, 0], p + 6); p += 10;
  b.fill(9, p, p + 25); p += 25;
  w(p, 2); p += 2;
  for (let i = 0; i < 2; i++) {
    w(p, i * 5); p += 2;
    for (let j = 0; j < 38; j++) w(p + 2 * j, 100);
    w(p, i); w(p + 74, i ? 10 : 3); p += 76;
    w(p, i ? 3 : 0); p += 2;
    if (i) for (let j = 0; j < 3; j++) { w(p, 9 - j); w(p + 2, 25 + j); w(p + 4, 0); w(p + 6, 100); w(p + 8, 100); w(p + 10, 5); p += 12; }
  }
  for (let i = 0; i < 9; i++) { w(p, i & 1); p += 2; }
  w(p, 1000); w(p + 2, 0); p += 4;
  for (let i = 0; i < 1000; i++) { w(p, i * 3); p += 2; }
  for (let i = 0; i < 22; i++) b[p++] = 200 + i;
  return b.slice(0, p);
}

// formats/tests/test_player_mec.c: opaque bytes with N = 2, K = 3 and 8, 33 flags.
function validMec() {
  const { b, w } = builder(235);
  for (let i = 0; i < b.length; i++) b[i] = (i * 37 + 11) & 0xff;
  w(0, 123); w(2, 2); w(8, 7); w(10, 3); w(96, 7); w(98, 8); w(200, 33);
  return b;
}

// A packed string file: u32 body size, then per group i16 N and N strings (i16 L, text, NUL,
// one E byte). extra: bytes after the groups, counted in the size (gamefile.str's stale
// tail) or after the body (counted false).
function strFile(groups, { tags = null, extra = [], counted = true } = {}) {
  const out = [];
  const u16 = (v) => out.push(v & 0xff, (v >> 8) & 0xff);
  groups.forEach((group, g) => {
    u16(group.length);
    group.forEach((text, i) => {
      u16(text.length + 1);
      for (const c of text) out.push(c.charCodeAt(0));
      out.push(0, tags ? tags(g, i) : 0);
    });
  });
  const body = counted ? out.length + extra.length : out.length;
  return Uint8Array.from([body & 0xff, (body >> 8) & 0xff, (body >> 16) & 0xff, body >>> 24, ...out, ...extra]);
}

const SLOT_FILES = [...Array.from({ length: 10 }, (_, i) => `GAME_${i}.SAV`), 'GAME_R.SAV', 'GAME_T.SAV'];
const slotTexts = (n = 12) => Array.from({ length: n }, (_, i) => (i < 2 ? `${String(i + 1).padStart(2)}. Pilot${i}` : `${String(i + 1).padStart(2)}. `));
const gamefile = (opts = {}) => strFile([opts.files ?? SLOT_FILES, opts.texts ?? slotTexts()],
  { tags: (g, i) => (g === 1 && i < 2 ? 1 : 0), extra: opts.extra ?? [] });
const mission = () => strFile([['Briefing', 'Objective one', 'Objective two', '']], { extra: [1, 2, 3], counted: false });
// formats/tests/test_script.c: the header, two coordinates, one angle, routes {0, 1} and {},
// one record in each fixed-stride list but timers, flyers, orders and objectives, two string
// indices. at[i] is the offset of list i's count word.
const SCRIPT_STRIDES = [12, 2, 0, 6, 74, 24, 134, 92, 52, 14, 156, 54, 2];
const SCRIPT_COUNTS = [2, 1, 2, 1, 1, 0, 1, 0, 1, 0, 1, 0, 2];
function scriptFile() {
  const { b, w } = builder(600);
  const at = [];
  let p = 20;
  for (let i = 0; i < 20; i++) b[i] = (i * 13 + 1) & 0xff;
  SCRIPT_COUNTS.forEach((n, i) => {
    at.push(p);
    w(p, n); p += 2;
    if (i === 2) { w(p, 2); w(p + 2, 0); w(p + 4, 1); w(p + 6, 0); p += 8; return; }
    for (let k = 0; k < SCRIPT_STRIDES[i] * n; k++) b[p + k] = (k * 7 + i) & 0xff;
    p += SCRIPT_STRIDES[i] * n;
  });
  return { bytes: b.slice(0, p), at };
}
const script = () => scriptFile().bytes;

const VALID = {
  'GAME_0.SAV': validSave(),
  'script0.dat': script(),
  'missn0.str': mission(),
  'player0.mec': validMec(),
  'GAMEFILE.STR': gamefile(),
};

// ---- names ---------------------------------------------------------------------------------
{
  const accepted = ['GAME_0.SAV', 'game_9.sav', 'Game_10.Sav', 'GAME_R.SAV', 'game_t.sav', 'script0.dat', 'SCRIPT11.DAT',
    'missn10.str', 'MISSN1.STR', 'player0.mec', 'PLAYER11.MEC', 'GAMEFILE.STR', 'gamefile.str'];
  const refused = ['GAME_.SAV', 'GAME_X.SAV', 'GAME_1R.SAV', 'GAME_RT.SAV', 'GAME_-1.SAV', 'GAME_0.SAV.bak', 'XGAME_0.SAV',
    'script.dat', 'scriptA.dat', 'missn.str', 'player.mec', 'player1.str', 'GAMEFILE.STR2', 'GAMEFILE', 'desktop.ini',
    'MISSION.STR', 'GAME_0 .SAV', 'GAME_ä.SAV', '', 'SAV'];
  for (const n of accepted) check(checker.kind(n) > 0, `name accepted: ${n}`);
  for (const n of refused) check(checker.kind(n) === 0, `name refused: ${JSON.stringify(n)}`);
  check(new Set(['GAME_0.SAV', 'script0.dat', 'missn0.str', 'player0.mec', 'GAMEFILE.STR'].map((n) => checker.kind(n))).size === 5,
    'five kinds');
  check(checker.check('desktop.ini', new Uint8Array(4)).status === NOT_A_SAVE, 'check of another name: NOT_A_SAVE');
  check(baseName('SAV/GAME_0.SAV') === 'GAME_0.SAV' && baseName('a\\b\\x.mec') === 'x.mec' && baseName('y') === 'y',
    'baseName strips folders');
}

// ---- each parser -----------------------------------------------------------------------------
for (const [name, bytes] of Object.entries(VALID)) {
  const r = checker.check(name, bytes);
  check(r.status === 0 && r.offset === 0, `${name}: valid (${r.reason})`);
}
// Every proper prefix of a framed file fails (missn0.str: up to the end of its body, the three
// bytes after it are not read).
for (const [name, bytes] of Object.entries(VALID)) {
  const limit = name === 'missn0.str' ? bytes.length - 3 : bytes.length;
  let refused = 0;
  for (let n = 0; n < limit; n++) if (checker.check(name, bytes.subarray(0, n)).status > 0) refused++;
  check(refused === limit, `${name}: all ${limit} truncations refused (${refused})`);
}
{
  const { bytes, at } = scriptFile();
  const set = (offset, x) => { const b = bytes.slice(); b[offset] = x & 0xff; b[offset + 1] = (x >> 8) & 0xff; return b; };
  check(checker.check('script0.dat', bytes.subarray(0, 20)).reason === 'truncated', 'script<n>.dat: the header alone is truncated');
  check(checker.check('script0.dat', Uint8Array.from([...bytes, 1, 2, 3])).status === 0, 'script<n>.dat with a stale tail: valid');
  for (let i = 0; i < at.length; i++) {
    const r = checker.check('script0.dat', set(at[i], 0xffff));
    check(r.reason === 'count' && r.offset === at[i], `script<n>.dat with a negative count in list ${i}: count at ${at[i]} (${r.reason} at ${r.offset})`);
  }
  const k = checker.check('script0.dat', set(at[2] + 2, 0x8000));
  check(k.reason === 'count' && k.offset === at[2] + 2, 'script<n>.dat with a negative route length: count');
  const ref = checker.check('script0.dat', set(at[2] + 6, 2));
  check(ref.reason === 'offset' && ref.offset === at[2] + 6, `script<n>.dat with a route reference past the coordinates: offset (${ref.reason} at ${ref.offset})`);
  check(checker.check('script0.dat', set(at[2] + 4, 0xffff)).reason === 'offset', 'script<n>.dat with a negative route reference: offset');
  check(checker.check('script0.dat', set(at[10], 2)).reason === 'truncated', 'script<n>.dat with a list past the end: truncated');
}
{
  const r = checker.check('GAME_0.SAV', VALID['GAME_0.SAV'].subarray(0, 100));
  check(r.reason === 'truncated' && r.offset > 0, `truncated SAV: reason and offset (${r.reason} at ${r.offset})`);
  const tail = Uint8Array.from([...VALID['GAME_0.SAV'], 1, 2, 3, 4]);
  check(checker.check('GAME_0.SAV', tail).status === 0, 'SAV with a stale tail: valid');
  const mec = VALID['player0.mec'].slice();
  mec[2] = 0xff; mec[3] = 0xff;
  check(checker.check('player0.mec', mec).reason === 'count', 'MEC with a negative record count: count');
  check(checker.check('player0.mec', Uint8Array.from([...VALID['player0.mec'], 0])).reason === 'length',
    'MEC with a byte after the flags: length');
  check(checker.check('GAMEFILE.STR', gamefile({ extra: [9, 9, 9, 9] })).status === 0, 'GAMEFILE.STR with a counted stale tail: valid');
  check(checker.check('GAMEFILE.STR', gamefile({ files: [...SLOT_FILES, 'GAME_X.SAV'] })).reason === 'count',
    'GAMEFILE.STR with 13 slots: count');
  check(checker.check('GAMEFILE.STR', gamefile({ files: ['GAME_000000.SAV', ...SLOT_FILES.slice(1)] })).reason === 'length',
    'GAMEFILE.STR with a 15-byte file name: length');
  check(checker.check('GAMEFILE.STR', gamefile({ texts: [...slotTexts(11), 'x'.repeat(80)] })).reason === 'length',
    'GAMEFILE.STR with an 81-byte slot text: length');
  check(checker.check('GAMEFILE.STR', gamefile({ texts: [...slotTexts(11), 'x'.repeat(79)] })).status === 0,
    'GAMEFILE.STR with an 80-byte slot text: valid');
  check(checker.check('GAMEFILE.STR', strFile([SLOT_FILES])).status > 0, 'GAMEFILE.STR with one group: refused');
  const str = mission();
  str[4 + 2 + 2 + 8] = 0x41;   // the NUL of the first string
  check(checker.check('missn0.str', str).reason === 'string', 'missn<n>.str with an unterminated string: string');
  check(checker.check('missn0.str', strFile([['a'], ['b']])).status === 0, 'missn<n>.str: groups after the first are not read');
  check(checker.check('GAME_0.SAV', VALID['player0.mec']).status > 0, 'a MEC under a SAV name: refused');
}

// ---- uploads -------------------------------------------------------------------------------
const asFile = (relative, bytes) => ({
  name: baseName(relative),
  webkitRelativePath: relative,
  arrayBuffer: async () => bytes.slice().buffer,
});
const folder = (files, dir = 'SAV') => Object.entries(files).map(([n, b]) => asFile(`${dir}/${n}`, b));
const recorder = () => {
  const writes = [];
  return { writes, write: async (p, bytes) => { writes.push([p, bytes]); } };
};
const keys = (r) => r.messages.map((m) => m.key);
const allTexts = (r) => {
  for (const m of r.messages) {
    const s = text(m.key, m.params);
    check(Object.hasOwn(TEXTS, m.key) && !/\{[a-z_]+\}/.test(s) && !s.includes('saves.reason.'), `text of ${m.key}: ${s}`);
  }
};

{
  const w = recorder();
  const r = await importSaves([...folder(VALID), asFile('SAV/desktop.ini', new Uint8Array(3))], checker, w.write);
  const names = Object.keys(VALID).sort((a, b) => (a.toUpperCase() < b.toUpperCase() ? -1 : 1));
  check(r.ok && r.written.length === 5, 'valid folder: five files written');
  check(equal(w.writes.map(([p]) => p), names.map((n) => `${SAV_DIR}\\${n}`)), `valid folder: paths in name order (${w.writes.map(([p]) => p)})`);
  check(w.writes.every(([p, b]) => equal([...b], [...VALID[baseName(p)]])), 'valid folder: bytes unchanged');
  check(equal(keys(r), ['saves.ignored', 'saves.written', 'saves.language_note']) && r.messages[0].params.name === 'desktop.ini' &&
    r.messages[1].params.count === 5, `valid folder: messages (${keys(r)})`);
  allTexts(r);
}
{
  const w = recorder();
  const broken = { ...VALID, 'GAME_1.SAV': VALID['GAME_0.SAV'].subarray(0, 1000), 'player1.mec': new Uint8Array(0) };
  const r = await importSaves(folder(broken, 'ES2/SAV'), checker, w.write);
  const failed = r.messages.filter((m) => m.key === 'saves.failed');
  check(!r.ok && r.written.length === 0 && w.writes.length === 0, 'broken folder: nothing written');
  check(equal(failed.map((m) => m.params.name).sort(), ['GAME_1.SAV', 'player1.mec']), 'broken folder: both bad files named');
  check(failed.every((m) => m.params.reason === 'saves.reason.truncated'), 'broken folder: reason truncated');
  check(keys(r).at(-1) === 'saves.nothing_written', 'broken folder: nothing_written');
  allTexts(r);
}
{
  const w = recorder();
  const { bytes, at } = scriptFile();
  const bad = bytes.slice();
  bad[at[2] + 6] = 2;
  const r = await importSaves(folder({ ...VALID, 'script1.dat': bad }), checker, w.write);
  const failed = r.messages.filter((m) => m.key === 'saves.failed');
  check(!r.ok && w.writes.length === 0 && failed.length === 1 && failed[0].params.name === 'script1.dat'
    && failed[0].params.reason === 'saves.reason.offset' && failed[0].params.offset === at[2] + 6, 'malformed script: named, nothing written');
  check(text(failed[0].key, failed[0].params) ===
    `script1.dat is not a valid save file (an offset is out of range, at byte ${at[2] + 6}).`, 'malformed script: the page\'s text');
  allTexts(r);
}
{
  const w = recorder();
  const r = await importSaves(folder({ 'GAME_0.SAV': VALID['GAME_0.SAV'], 'GAMEFILE.STR': VALID['GAMEFILE.STR'] }), checker, w.write);
  check(r.ok && equal(w.writes.map(([p]) => p), [`${SAV_DIR}\\GAMEFILE.STR`, `${SAV_DIR}\\GAME_0.SAV`]), 'partial folder: its two files written');
}
{
  const w = recorder();
  const r = await importSaves([{ name: 'game_r.sav', bytes: VALID['GAME_0.SAV'] }, { name: 'missn10.str', bytes: VALID['missn0.str'] }],
    checker, w.write);
  check(r.ok && equal(w.writes.map(([p]) => p), [`${SAV_DIR}\\game_r.sav`, `${SAV_DIR}\\missn10.str`]), 'single files: written under their own names');
}
{
  const w = recorder();
  const r = await importSaves([{ name: 'GAME_0.SAV', bytes: VALID['GAME_0.SAV'] }, asFile('x/game_0.sav', VALID['GAME_0.SAV'])], checker, w.write);
  check(!r.ok && w.writes.length === 0 && equal(keys(r), ['saves.duplicate', 'saves.nothing_written']), `duplicate names: refused (${keys(r)})`);
  allTexts(r);
}
{
  const w = recorder();
  const r = await importSaves([asFile('SAV/readme.txt', new Uint8Array(1))], checker, w.write);
  check(r.ok && w.writes.length === 0 && equal(keys(r), ['saves.ignored', 'saves.none']), 'no save file: none');
  const empty = await importSaves([], checker, w.write);
  check(empty.ok && w.writes.length === 0 && equal(keys(empty), ['saves.none']), 'empty upload: none');
  allTexts(r);
}
{
  const r = await checkSaves(folder(VALID), checker);
  check(r.ok && r.files.length === 5 && r.messages.length === 0, 'checkSaves alone: no writes, no messages');
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
