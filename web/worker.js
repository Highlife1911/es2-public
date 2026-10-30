// The game worker of the page (docs/web_build.md sections 2, 3 and 8), a module worker started
// by game.mjs. The page's first message is
//   {kind: 'start', sab, overlay, language, layout, run}
// with the shared buffer, the stored overlay (store.mjs load), the game's language byte for a
// new installation, the page's keyboard layout and, for the self-test's run (selftest_run.mjs),
// `run`: {menuLines, environment, hostFiles, shellArgs, simArgs, simOnly, gameFiles, collect}
// (runGame's options, the argument lists one per start). The worker reads manifest.json, opens the
// Vfs over the site's parts (a part a process needs at once comes by synchronous XHR, the
// prefetch lists through the part cache), compiles es2shell.wasm and es2sim.wasm once and runs
// the launcher loop (game_worker.mjs). Its last message is {kind: 'end', state, files} (files:
// the run's collect paths as [[path, bytes]]), or
// {kind: 'failed', text} when it cannot start.
import { views } from './shared.mjs';
import { Vfs, loadCodec } from './vfs.mjs';
import { PartCache, prefetchParts } from './store.mjs';
import { Keyboard } from './keys.mjs';
import { collectFiles, nthArgs, persistTo, prepareInstallation, runGame } from './game_worker.mjs';

const post = (message) => self.postMessage(message);
const log = (text) => post({ kind: 'log', text });

// A part the running process needs now: the process waits, so the fetch is synchronous.
function fetchPartSync(sha256) {
  const x = new XMLHttpRequest();
  x.open('GET', `data/${sha256}.zst`, false);
  x.responseType = 'arraybuffer';
  x.send();
  if (x.status !== 200) throw new Error(`data/${sha256}.zst: HTTP ${x.status}`);
  return new Uint8Array(x.response);
}

const json = async (url, options) => {
  const r = await fetch(url, options);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.json();
};

// The modules under their site names (section 17); the glue's own wasm name is not used.
const factory = async (name) => (await import(`./${name}.js`)).default;
const located = (make, name) => (options = {}) => make({ ...options, locateFile: () => `${name}.wasm` });

async function start({ sab, overlay, language, layout, run = null }) {
  const shared = views(sab);
  const cache = new PartCache({ caches: self.caches, fetch: (url) => fetch(url) });
  const [manifest, codecFactory, launchFactory, shellFactory, simFactory, shellModule, simModule] = await Promise.all([
    json('manifest.json', { cache: 'no-cache' }),
    factory('vfs'), factory('launch'), factory('es2shell'), factory('es2sim'),
    WebAssembly.compileStreaming(fetch('es2shell.wasm')),
    WebAssembly.compileStreaming(fetch('es2sim.wasm')),
  ]);
  const codec = await loadCodec(located(codecFactory, 'vfs'));
  const vfs = new Vfs({
    manifest, codec, fetchPart: fetchPartSync, onChanges: persistTo(post),
    onPart: (sha256, bytes) => { cache.put(sha256, bytes).catch(() => {}); },
  });
  vfs.loadOverlay(overlay ?? []);
  prepareInstallation(vfs, language);

  const prefetch = async (name) => {
    let list;
    try {
      list = await json(`prefetch/${name}.json`);
    } catch {
      return;
    }
    const failed = await prefetchParts(vfs, cache, list.parts ?? []);
    if (failed.length) log(`prefetch ${name}: ${failed.length} parts left for later`);
  };
  await prefetch('shell');
  const state = await runGame({
    shared, post, vfs,
    launchFactory: located(launchFactory, 'launch'),
    shellFactory: located(shellFactory, 'es2shell'), simFactory: located(simFactory, 'es2sim'), shellModule, simModule,
    keyboard: new Keyboard(layout),
    prefetch,
    menuLines: run?.menuLines ?? [],
    shellArgs: nthArgs(run?.shellArgs),
    simArgs: nthArgs(run?.simArgs),
    environment: run?.environment ?? {},
    hostFiles: run?.hostFiles ?? [],
    simOnly: !!run?.simOnly,
    gameFiles: run?.gameFiles ?? [],
    log,
  });
  return { state, files: collectFiles(vfs, run?.collect) };
}

self.addEventListener('message', (e) => {
  if (e.data?.kind !== 'start') return;
  start(e.data).then(({ state, files }) => post({ kind: 'end', state, files }),
    (error) => post({ kind: 'failed', text: error && error.stack ? error.stack : String(error) }));
});
