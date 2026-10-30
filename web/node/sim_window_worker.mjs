// The game worker of the Node window host (sim_window_host.mjs): runs es2sim_window's wasm
// module with the "es2os" imports over node:fs and the "es2sim" imports of es2sim_imports.mjs
// over the shared buffer, as the browser's worker does. workerData: {module, args, sab, layout,
// metrics}. Messages to the host: the requests of es2sim_imports.mjs and {kind: 'exit', code}.
import { parentPort, workerData } from 'node:worker_threads';
import { pathToFileURL } from 'node:url';
import { views } from '../shared.mjs';
import { es2simWindowImports } from '../es2sim_imports.mjs';
import { Keyboard } from '../keys.mjs';
import { es2osHost, nodeFiles } from './os_host.mjs';

const { module, args, sab, layout, metrics } = workerData;
const post = (message) => parentPort.postMessage(message);
let code = 0;
try {
  const factory = (await import(pathToFileURL(module).href)).default;
  await factory({
    arguments: args,
    thisProgram: module,
    es2os: es2osHost(nodeFiles()),
    es2sim: es2simWindowImports(views(sab), { post, keyboard: new Keyboard(layout), metrics }),
    onExit: (status) => { code = status; },
  });
} catch (e) {
  if (!(e && e.name === 'ExitStatus')) {
    console.error(e);
    code = 1;
  } else {
    code = e.status;
  }
}
post({ kind: 'exit', code });
