// es2sim_wasm's layout check (sim/src/os_process_wasm.c, es2_sim_os_reserved) on a link whose
// static data starts at 0x100000 (-sGLOBAL_BASE=1048576), below the floor 0x800000: the fault exit
// 71 with the FAULT line that names __global_base, before anything is written.
//
// Arguments: host.mjs es2sim.js (the low-base link)
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [hostMjs, moduleJs] = process.argv.slice(2).map((p) => path.resolve(p));
const dir = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR || os.tmpdir(), 'layout-'));
const log = path.join(dir, 'tick-log.jsonl');
const run = spawnSync(process.execPath, [hostMjs, moduleJs, '--headless', '--tick-log', log],
                      { cwd: dir, encoding: 'utf8' });
const want = 'FAULT: the layout puts __global_base at 0x00100000, below 0x00800000';
const failures = [];
if (run.status !== 71) failures.push(`exit ${run.status} (want 71); stderr ${run.stderr}`);
if (!run.stderr.includes(want)) failures.push(`stderr lacks "${want}": ${run.stderr}`);
if (fs.existsSync(log) && fs.statSync(log).size) failures.push('the tick log is not empty');
for (const f of failures) console.log('FAIL', f);
console.log(failures.length ? `${failures.length} failure(s)` : `exit ${run.status}: ${want}`);
process.exit(failures.length ? 1 : 0);
