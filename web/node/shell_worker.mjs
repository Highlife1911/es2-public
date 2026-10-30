// The game worker of the Node shell host (shell_host.mjs): runs es2shell_wasm's module with the
// "es2shell" imports over the shared buffer, as the browser's worker does. workerData:
// {module, args, sab}. Messages to the host: the requests of es2shell_imports.mjs and
// {kind: 'exit', code}.
import { parentPort, workerData } from 'node:worker_threads';
import { pathToFileURL } from 'node:url';
import { views } from '../shared.mjs';
import { es2shellImports } from '../es2shell_imports.mjs';

const { module, args, sab } = workerData;
const post = (message) => parentPort.postMessage(message);
let code = 0;
try {
  const factory = (await import(pathToFileURL(module).href)).default;
  await factory({
    arguments: args,
    thisProgram: module,
    es2shell: es2shellImports(views(sab), post),
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
