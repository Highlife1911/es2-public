// web/saves.mjs over tools.wasm on the real save folders: every installation's SAV folder beside
// the CD (build/installs, two levels above ES2_DATA_ROOT; ES2_SCRIPT_INSTALLS overrides, as for
// //formats:script_real_test). Every script<n>.dat passes the check, and so does each folder
// as one upload. Only reads; skips without ES2_DATA_ROOT.
//
// Arguments: tools.js
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { checkSaves, loadChecker } from '../saves.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};

const root = process.env.ES2_DATA_ROOT;
if (!root) {
  console.log('SKIP: ES2_DATA_ROOT unset');
  process.exit(0);
}
const installs = process.env.ES2_SCRIPT_INSTALLS || path.join(root, '..', '..', 'installs');
if (!fs.existsSync(installs)) {
  console.log(`SKIP: no installations at ${installs}`);
  process.exit(0);
}
const [toolsJs] = process.argv.slice(2);
const checker = await loadChecker((await import(pathToFileURL(path.resolve(toolsJs)).href)).default);

let scripts = 0;
let folders = 0;
for (const install of fs.readdirSync(installs).sort()) {
  const dir = path.join(installs, install, 'SAV');
  if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) continue;
  const files = fs.readdirSync(dir).filter((n) => fs.statSync(path.join(dir, n)).isFile())
    .map((n) => ({ name: n, bytes: new Uint8Array(fs.readFileSync(path.join(dir, n))) }));
  for (const f of files.filter((f) => /^script\d+\.dat$/i.test(f.name))) {
    const r = checker.check(f.name, f.bytes);
    check(r.status === 0, `${install}/SAV/${f.name}: ${r.reason} at ${r.offset}`);
    scripts++;
  }
  const r = await checkSaves(files, checker);
  check(r.ok, `${install}/SAV as one upload: ${JSON.stringify(r.messages.filter((m) => m.key !== 'saves.ignored'))}`);
  console.log(`${install}/SAV: ${r.files.length} save files ${r.ok ? 'accepted' : 'refused'}`);
  folders++;
}
check(scripts > 0, 'real script files found');
console.log(`${folders} folders, ${scripts} script files; ${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
