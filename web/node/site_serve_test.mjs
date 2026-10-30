// The page files over tools/web/serve.py, with the isolation headers and with
// --no-isolation-headers: each file served unchanged with its MIME type, "/" as index.html,
// every module the two pages import present, COOP/COEP exactly when the server sends them; then
// web/sw.js as served, answering over the live server, gives both pages the headers in both
// modes, and selftest.mjs on a fake document over the served headers passes every check: at
// once with the headers, after the one reload through the worker without them.
//
// Arguments: <python> <serve.py> <page file>...
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { CHECKS } from '../checks.mjs';
import { runSelftest } from '../selftest.mjs';
import { FakeDocument, FakeServiceWorker, FakeStorage, fakeBrowser, isolatedBy, loadServiceWorker, swFetch } from './page_fakes.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};

const [python, servePy, ...pageFiles] = process.argv.slice(2);
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8' };

const site = fs.mkdtempSync(path.join(process.env.TEST_TMPDIR ?? os.tmpdir(), 'site_'));
const files = new Map();
for (const f of pageFiles) {
  const bytes = fs.readFileSync(f);
  files.set(path.basename(f), bytes);
  fs.writeFileSync(path.join(site, path.basename(f)), bytes);
}
fs.writeFileSync(path.join(site, 'manifest.json'), '{}\n');

// The relative imports of a module's source, static and dynamic with a literal name.
// Emscripten's glue (tools.js) is a build output that build_site.py adds, not a page file.
const BUILT = new Set(['tools.js']);
const imports = (source) => [...source.matchAll(/(?:\bfrom\s+|\bimport\(\s*)'\.\/([^']+)'/g)].map((m) => m[1]);

function startServer(isolation) {
  const args = ['-I', servePy, '--dir', site, '--port', '0', ...(isolation ? [] : ['--no-isolation-headers'])];
  const child = spawn(python, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  return new Promise((resolve, reject) => {
    let out = '';
    child.stdout.on('data', (d) => {
      out += d;
      const m = /at (http:\/\/127\.0\.0\.1:\d+\/)/.exec(out);
      if (m) resolve({ child, base: m[1], line: out.trim() });
    });
    child.stderr.on('data', () => {});
    child.on('exit', (code) => reject(new Error(`serve.py exited with ${code}: ${out}`)));
  });
}

async function run(isolation) {
  const mode = isolation ? 'headers' : 'no headers';
  const { child, base, line } = await startServer(isolation);
  try {
    check(line.includes(isolation ? 'with COOP/COEP' : 'without isolation headers'), `${mode}: serve.py's mode line`);
    const origin = new URL(base).origin;
    for (const [name, bytes] of files) {
      const r = await fetch(base + name);
      const body = Buffer.from(await r.arrayBuffer());
      check(r.status === 200 && r.headers.get('content-type') === MIME[path.extname(name)], `${mode}: ${name} ${r.status} ${r.headers.get('content-type')}`);
      check(body.equals(bytes) && r.headers.get('content-encoding') === null, `${mode}: ${name} unchanged`);
      check(isolatedBy(r.headers) === isolation, `${mode}: ${name} isolation headers`);
    }
    const root = await fetch(base);
    check(root.status === 200 && Buffer.from(await root.arrayBuffer()).equals(files.get('index.html')), `${mode}: / is index.html`);

    // Every module the pages load, transitively, is in the site.
    const scripts = (page) => [...files.get(page).toString().matchAll(/<script type="module" src="([^"]+)"/g)].map((m) => m[1]);
    check(scripts('index.html').join() === 'app.js' && scripts('selftest.html').join() === 'selftest.mjs', `${mode}: the pages' modules`);
    const needed = new Set(), queue = ['app.js', 'selftest.mjs', 'sw.js', 'worker.js'];
    while (queue.length) {
      const name = queue.shift();
      if (needed.has(name) || BUILT.has(name)) continue;
      needed.add(name);
      const r = await fetch(base + name);
      check(r.status === 200 && r.headers.get('content-type') === MIME['.mjs'], `${mode}: module ${name} served`);
      queue.push(...imports(await r.text()));
    }
    check(['checks.mjs', 'isolation.mjs', 'texts.mjs', 'keys.mjs', 'key_layouts.mjs', 'movie_player.mjs', 'shared.mjs',
      'game.mjs', 'game_worker.mjs', 'launcher.mjs', 'store.mjs', 'vfs.mjs', 'vfs_files.mjs', 'cd_player.mjs', 'saves.mjs', 'zip.mjs',
      'unzip.mjs', 'volume.mjs']
      .every((m) => needed.has(m)), `${mode}: the module graph ${[...needed].join(' ')}`);

    // The worker as served, over the live server.
    const swSource = await (await fetch(base + 'sw.js')).text();
    const self = loadServiceWorker(path.join(site, 'sw.js'), fetch, origin, swSource);
    const viaWorker = {};
    for (const page of ['index.html', 'selftest.html']) {
      const r = await swFetch(self, new Request(base + page));
      check(r.status === 200 && Buffer.from(await r.arrayBuffer()).equals(files.get(page)), `${mode}: ${page} through the worker unchanged`);
      check(isolatedBy(r.headers), `${mode}: ${page} isolated through the worker`);
      viaWorker[page] = r.headers;
    }
    const absent = await swFetch(self, new Request(base + 'absent.js'));
    check(absent.status === 404 && isolatedBy(absent.headers), `${mode}: a 404 through the worker`);

    // The self-test page: the first load as the server sends it, then (if it reloads) through the worker.
    const direct = await fetch(base + 'selftest.html');
    const sw = new FakeServiceWorker(), storage = new FakeStorage();
    const first = fakeBrowser({ isolated: isolatedBy(direct.headers), serviceWorker: sw, sessionStorage: storage });
    const doc = new FakeDocument();
    let result = await runSelftest(first, doc);
    check(result.state === (isolation ? 'isolated' : 'reloading') && first.reloads === (isolation ? 0 : 1), `${mode}: first load ${result.state}`);
    if (!isolation) {
      const second = fakeBrowser({ isolated: isolatedBy(viaWorker['selftest.html']), serviceWorker: sw, sessionStorage: storage });
      result = await runSelftest(second, doc);
      check(second.reloads === 0, `${mode}: no second reload`);
    }
    check(result.state === 'isolated' && result.rows.length === CHECKS.length + 1 && result.rows.every((r) => r.ok),
      `${mode}: every self-test check passes ${JSON.stringify(result.rows)}`);
    check(doc.ids.results.children.every((r) => r.className === 'passed'), `${mode}: the rows show it`);
  } finally {
    child.kill();
  }
}

await run(true);
await run(false);
fs.rmSync(site, { recursive: true, force: true });
console.log(`${checks} checks, ${failures.length} failed`);
if (failures.length) process.exit(1);
