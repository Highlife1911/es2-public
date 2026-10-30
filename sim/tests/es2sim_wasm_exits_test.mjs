// es2sim_wasm's ends that native es2sim reaches through Windows exceptions or that only the
// web build has (docs/web_build.md section 6): a wasm trap out of an import becomes
// es2_wasm_trap_exit's guest divide fault (0xc0000094, 0xc0000095) or the fault exit 71, the
// VFS's media-bytes error the refusal exit 70. The first file call after the init records
// (file_attributes, for data\drive.cfg) throws; the run starts in an empty directory.
//
// Arguments: host.mjs es2sim_wasm_throw_host.mjs es2sim.js
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [hostMjs, throwHost, moduleJs] = process.argv.slice(2).map((p) => path.resolve(p));
const tmp = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR || os.tmpdir(), 'exits-'));

const ENTRY = '0x00496802';
const cases = [
  { throw: 'divide by zero', status: 0xc0000094, stderr: `FAULT: integer divide fault in the entry ${ENTRY}, wasm trap`,
    reason: 'guest_fault', exitCode: 0xc0000094 },
  { throw: 'divide result unrepresentable', status: 0xc0000095,
    stderr: `FAULT: integer divide fault in the entry ${ENTRY}, wasm trap`, reason: 'guest_fault', exitCode: 0xc0000095 },
  { throw: 'unreachable', status: 71, stderr: `FAULT: wasm trap of kind 0, entry ${ENTRY}` },
  { throw: 'integer overflow', status: 0xc0000095, stderr: `FAULT: integer divide fault in the entry ${ENTRY}, wasm trap`,
    reason: 'guest_fault', exitCode: 0xc0000095 },
  { throw: 'float unrepresentable in integer range', status: 71, stderr: `FAULT: wasm trap of kind 0, entry ${ENTRY}` },
  { throw: 'media', status: 70, stderr: '"reason":"media bytes"', reason: 'refused', exitCode: 70 },
];

const failures = [];
for (const [i, c] of cases.entries()) {
  const dir = fs.mkdtempSync(path.join(tmp, `case${i}-`));
  const log = path.join(dir, 'tick-log.jsonl');
  const env = { ...process.env, ES2_THROW_AT: 'file_attributes', ES2_THROW: c.throw };
  const run = spawnSync(process.execPath, [hostMjs, '--host', throwHost, moduleJs, '--headless', '--tick-log', log],
                        { cwd: dir, env, encoding: 'utf8' });
  const status = run.status === null ? null : run.status >>> 0;
  const label = `${c.throw}:`;
  if (status !== c.status) failures.push(`${label} exit ${status} (want ${c.status}); stderr ${run.stderr}`);
  if (!run.stderr.includes(c.stderr)) failures.push(`${label} stderr lacks ${c.stderr}: ${run.stderr}`);
  if (c.reason) {
    const exit = fs.readFileSync(log, 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l))
      .find((r) => r.event === 'exit');
    if (!exit || exit.reason !== c.reason || exit.exit_code !== c.exitCode)
      failures.push(`${label} exit record ${JSON.stringify(exit && { reason: exit.reason, exit_code: exit.exit_code })}`);
  }
  console.log(`${label} exit ${status}`);
}
for (const f of failures) console.log('FAIL', f);
console.log(failures.length ? `${failures.length} failure(s)` : `${cases.length} cases passed`);
process.exit(failures.length ? 1 : 0);
