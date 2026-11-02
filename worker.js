// The game worker of the page (docs/web_build.md sections 2, 3 and 8), a module worker started
// by game.mjs. The page's first message is
//   {kind: 'start', sab, overlay, language, layout, manifest, gamefiles, run}
// with the shared buffer, the stored overlay (store.mjs load), the game's language byte for a
// new installation, the page's keyboard layout, manifest.json and gamefiles.zst's bytes as the
// page loaded them (store.mjs GameFiles) and, for the self-test's run (selftest_run.mjs),
// `run`: {menuLines, environment, hostFiles, shellArgs, simArgs, simOnly, gameFiles, collect}
// (runGame's options, the argument lists one per start). The worker decodes gamefiles.zst once,
// opens the Vfs over its content, compiles es2shell.wasm and es2sim.wasm once and runs the
// launcher loop (game_worker.mjs). Its last message is {kind: 'end', state, files} (files: the run's collect
// paths as [[path, bytes]]), or {kind: 'failed', text} when it cannot start.
import { views } from './shared.mjs';
import { Vfs, decodeGameFiles, loadCodec } from './vfs.mjs';
import { Keyboard } from './keys.mjs';
import { collectFiles, nthArgs, persistTo, prepareInstallation, runGame } from './game_worker.mjs';

const post = (message) => self.postMessage(message);
const log = (text) => post({ kind: 'log', text });

// The modules under their site names (section 17); the glue's own wasm name is not used.
const factory = async (name) => (await import(`./${name}.js`)).default;
const located = (make, name) => (options = {}) => make({ ...options, locateFile: () => `${name}.wasm` });

async function start({ sab, overlay, language, layout, manifest, gamefiles, run = null }) {
  const shared = views(sab);
  const [codecFactory, launchFactory, shellFactory, simFactory, shellModule, simModule] = await Promise.all([
    factory('vfs'), factory('launch'), factory('es2shell'), factory('es2sim'),
    WebAssembly.compileStreaming(fetch('es2shell.wasm')),
    WebAssembly.compileStreaming(fetch('es2sim.wasm')),
  ]);
  const content = decodeGameFiles(await loadCodec(located(codecFactory, 'vfs')), manifest, gamefiles);
  const vfs = new Vfs({ manifest, content, onChanges: persistTo(post) });
  vfs.loadOverlay(overlay ?? []);
  prepareInstallation(vfs, language);

  const state = await runGame({
    shared, post, vfs,
    launchFactory: located(launchFactory, 'launch'),
    shellFactory: located(shellFactory, 'es2shell'), simFactory: located(simFactory, 'es2sim'), shellModule, simModule,
    keyboard: new Keyboard(layout),
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
