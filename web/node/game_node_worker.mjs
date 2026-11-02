// The game worker of web/node/game_host.mjs: worker.js's part under Node. The Vfs is the site's
// (vfs_site.mjs, gamefiles.zst read with node:fs), the modules are compiled from their .wasm
// beside the glue, and the launcher loop runs through web/game_worker.mjs as in the browser.
import fs from 'node:fs';
import path from 'node:path';
import { parentPort, workerData } from 'node:worker_threads';
import { pathToFileURL } from 'node:url';
import { views } from '../shared.mjs';
import { Keyboard } from '../keys.mjs';
import { HOST_DIR, collectFiles, nthArgs, persistTo, prepareInstallation, runGame } from '../game_worker.mjs';
import { loadCodecFrom, openSite } from './vfs_site.mjs';
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

// A sim-only run needs neither the launcher nor the shell.
const [launchFactory, shellFactory, simFactory, shellModule, simModule] = await Promise.all([
  w.simOnly ? null : factory(w.launch), w.simOnly ? null : factory(w.shell), factory(w.sim),
  w.simOnly ? null : compile(w.shell), compile(w.sim),
]);
const state = await runGame({
  shared, post, vfs, launchFactory, shellFactory, simFactory, shellModule, simModule,
  keyboard: new Keyboard(w.layout),
  memory: w.memory ?? undefined,
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
if (w.exportHost) exportTree(vfs, HOST_DIR, w.exportHost);
post({ kind: 'end', state, files: collectFiles(vfs, w.collect) });
