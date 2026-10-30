// web/tick_log_hash.mjs against sim/tests/tick_log_raw_diff.py: a synthetic raw tick log and
// variants of it, each pair hashed and diffed by the pinned Python. The hashes are equal exactly
// when the diff passes (only pid, tid, qpc or dep_flags differ); the variants cover both sides,
// with host-field names inside strings and nested objects, values beyond 2^53, CRLF, a missing
// last newline, a line that is no JSON and one that is no UTF-8.
//
// Arguments: python.exe tick_log_raw_diff.py
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { maskLine, tickLogHash } from '../tick_log_hash.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};

const [python, rawDiff] = process.argv.slice(2);
const tmp = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR ?? os.tmpdir(), 'tick_log_hash_test_'));

const BASE = [
  '{"event":"start","schema":2,"mode":"own_process_headless","pid":4120,"tid":9876,"image":"DBSIM.EXE"}',
  '{"event":"tick","frame":1,"qpc":4089957774315,"vt":16777216,"watch":{"qpc":5,"pid":[1,2]},"note":"\\"pid\\":7"}',
  '{"event":"tick","frame":2,"qpc":4089957821002,"vt":16777263,"calls":{"0x00462398":212}}',
  'not json, "qpc":1',
  '{"event":"exit","reason":"ticks","exit_code":0,"frames":2,"dep_flags":1,"calls":{"0x00477bec":9007199254740993}}',
];
const text = (lines, eol = '\n', last = true) => lines.join(eol) + (last ? eol : '');
const swap = (i, from, to) => BASE.map((l, n) => (n === i ? l.replace(from, to) : l));

// [label, log bytes, the diff must pass]
const variants = [
  ['the same log', text(BASE), true],
  ['other pid and tid', text(swap(0, '"pid":4120,"tid":9876', '"pid":1,"tid":1')), true],
  ['other qpc', text(swap(1, '4089957774315', '17')), true],
  ['qpc of another type', text(swap(2, '4089957821002', '"late"')), true],
  ['other dep_flags', text(swap(4, '"dep_flags":1', '"dep_flags":0')), true],
  ['CRLF line ends', text(BASE, '\r\n'), true],
  ['no last newline', text(BASE, '\n', false), true],
  ['other frame', text(swap(2, '"frame":2', '"frame":3')), false],
  ['a nested qpc', text(swap(1, '"watch":{"qpc":5', '"watch":{"qpc":6')), false],
  ['a nested pid', text(swap(1, '"pid":[1,2]', '"pid":[1,3]')), false],
  ['a pid inside a string', text(swap(1, '\\"pid\\":7', '\\"pid\\":8')), false],
  ['a call count beyond 2^53', text(swap(4, '9007199254740993', '9007199254740992')), false],
  ['other text line', text(swap(3, '"qpc":1', '"qpc":2')), false],
  ['other key order', text(swap(0, '"pid":4120,"tid":9876', '"tid":9876,"pid":4120')), false],
  ['one line more', text([...BASE, '{"event":"tail"}']), false],
  ['one line less', text(BASE.slice(0, -1)), false],
];

const encoder = new TextEncoder();
const write = (name, bytes) => {
  const file = path.join(tmp, name);
  fs.writeFileSync(file, bytes);
  return file;
};
const baseBytes = encoder.encode(text(BASE));
const baseFile = write('base.jsonl', baseBytes);
const baseHash = await tickLogHash(baseBytes);
check(baseHash.lines === BASE.length, `line count ${baseHash.lines}`);
check(/^[0-9a-f]{64}$/.test(baseHash.sha256), 'hex SHA-256');

let passing = 0;
for (const [n, [label, log, passes]] of variants.entries()) {
  const bytes = encoder.encode(log);
  const file = write(`variant${n}.jsonl`, bytes);
  const r = spawnSync(python, ['-I', rawDiff, baseFile, file], { encoding: 'utf8' });
  check(r.status === (passes ? 0 : 1), `${label}: tick_log_raw_diff.py exit ${r.status} ${r.stdout}${r.stderr}`);
  const same = (await tickLogHash(bytes)).sha256 === baseHash.sha256;
  check(same === (r.status === 0), `${label}: hash ${same ? 'equal' : 'different'}, diff exit ${r.status}`);
  passing += r.status === 0;
}
check(passing > 0 && passing < variants.length, 'the variants cover both sides');

// A line that is no UTF-8 stays as bytes: two different ones differ, the same one is equal.
{
  const bad = (b) => Uint8Array.from([...encoder.encode('{"pid":1}\n'), 0x7b, 0xff, b, 0x7d, 0x0a]);
  const [a, b, c] = await Promise.all([bad(0x01), bad(0x02), bad(0x01)].map((x) => tickLogHash(x)));
  check(a.sha256 !== b.sha256 && a.sha256 === c.sha256, 'no UTF-8 line compared as bytes');
  const fa = write('bad_a.jsonl', bad(0x01));
  const fb = write('bad_b.jsonl', bad(0x02));
  check(spawnSync(python, ['-I', rawDiff, fa, fb]).status === 1, 'tick_log_raw_diff.py: no UTF-8 lines differ');
}

check(maskLine('{"a":1, "pid" : 42 ,"b":{"pid":3}}') === '{"a":1, "pid" : 0 ,"b":{"pid":3}}', 'mask keeps the spacing');
check(maskLine('[1,2]') === '[1,2]' && maskLine('"pid"') === '"pid"', 'no object: unchanged');

fs.rmSync(tmp, { recursive: true, force: true });
console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
