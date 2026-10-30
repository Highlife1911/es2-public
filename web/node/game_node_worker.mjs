// The game worker of web/node/game_host.mjs: worker.js's part under Node. The Vfs is the site's
// (vfs_site.mjs, parts read with node:fs), the modules are compiled from their .wasm beside the
// glue, and the launcher loop runs through web/game_worker.mjs as in the browser.
import fs from 'node:fs';
import path from 'node:path';
import { parentPort, workerData } from 'node:worker_threads';
import { pathToFileURL } from 'node:url';
import { views } from '../shared.mjs';
import { Keyboard } from '../keys.mjs';
import { MISSION_SCRIPT, missionKey } from '../vfs.mjs';
import { HOST_DIR, collectFiles, nthArgs, persistTo, prepareInstallation, runGame } from '../game_worker.mjs';
import { loadCodecFrom, openSite, siteFetcher } from './vfs_site.mjs';
import { nodeFiles, routedFiles } from './os_host.mjs';

const w = workerData;
const post = (message) => parentPort.postMessage(message);
const log = (text) => post({ kind: 'log', text });
// A glue's factory with its .wasm beside it, under the glue's own name (as worker.js's located:
// a site names launch.js's module launch.wasm, a build output launch_web.wasm).
const factory = async (file) => {
  const make = (await import(pathToFileURL(path.resolve(file)).href)).default;
  return (options = {}) => make({ ...options, locateFile: () => path.resolve(file).replace(/\.js$/, '.wasm') });
};
const compile = (file) => WebAssembly.compile(fs.readFileSync(file.replace(/\.js$/, '.wasm')));

// Each process's fetched parts as a part log of section 8.4: the shell's under the key "shell",
// a sim's under the mission key of its script.dat.
function partLogger(vfs, dir) {
  let from = vfs.partLog.length, count = 0, current = null;
  const flush = () => {
    if (!dir || !current) return;
    const parts = vfs.partLog.slice(from);
    fs.writeFileSync(path.join(dir, `${String(count++).padStart(2, '0')}-${current.program}.json`),
      JSON.stringify({ mission: current.mission, prefetched: current.prefetched, parts }, null, 1) + '\n');
  };
  return {
    start(program, prefetched) {
      flush();
      const mission = program === 'shell' ? 'shell' : missionKey(vfs.stat(MISSION_SCRIPT) ? vfs.readAll(MISSION_SCRIPT) : null);
      current = { program, mission, prefetched };
      from = vfs.partLog.length;
    },
    end: flush,
  };
}

function exportTree(vfs, dir, target) {
  fs.mkdirSync(target, { recursive: true });
  for (const e of vfs.list(dir)) {
    const name = `${dir}\\${e.name}`;
    if (e.dir) exportTree(vfs, name, path.join(target, e.name));
    else fs.writeFileSync(path.join(target, e.name), vfs.readAll(name));
  }
}

const shared = views(w.sab);
const vfs = openSite(w.site, await loadCodecFrom(w.codec), { poison: !!w.poison, onChanges: persistTo(post) });
vfs.loadOverlay(w.overlay ?? []);
prepareInstallation(vfs, w.language);
const parts = partLogger(vfs, w.partLogs);
const fetch = siteFetcher(w.site);
let prefetched = null;
const prefetch = async (name) => {
  prefetched = null;
  if (!w.prefetch) return;
  const list = path.join(w.site, 'prefetch', `${name}.json`);
  if (!fs.existsSync(list)) return;
  const { parts: shas } = JSON.parse(fs.readFileSync(list, 'utf8'));
  for (const sha of shas) if (!vfs.hasPart(sha)) vfs.addPart(sha, fetch(sha));
  prefetched = shas.length;
};
await prefetch('shell');
// Only the first process starts after a prefetch.
const post2 = (m) => {
  if (m.kind === 'process') {
    parts.start(m.program, prefetched);
    prefetched = null;
  }
  post(m);
};

// A sim-only run needs neither the launcher nor the shell.
const [launchFactory, shellFactory, simFactory, shellModule, simModule] = await Promise.all([
  w.simOnly ? null : factory(w.launch), w.simOnly ? null : factory(w.shell), factory(w.sim),
  w.simOnly ? null : compile(w.shell), compile(w.sim),
]);
const state = await runGame({
  shared, post: post2, vfs, launchFactory, shellFactory, simFactory, shellModule, simModule,
  keyboard: new Keyboard(w.layout),
  memory: w.memory ?? undefined,
  prefetch: async (mission) => { await prefetch(mission); },
  menuLines: w.menuLines ?? [],
  shellArgs: nthArgs(w.shellArgs),
  simArgs: nthArgs(w.simArgs),
  environment: w.environment ?? {},
  hostFiles: w.hostFiles ?? [],
  simOnly: !!w.simOnly,
  gameFiles: w.gameFiles ?? [],
  wrapFiles: (files) => (w.hostDirs?.length ? routedFiles(files, nodeFiles(), w.hostDirs) : files),
  log,
});
parts.end();
if (w.exportHost) exportTree(vfs, HOST_DIR, w.exportHost);
post({ kind: 'end', state, files: collectFiles(vfs, w.collect) });
