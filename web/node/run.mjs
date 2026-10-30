// node run.mjs [--host HOST.mjs]... MODULE.js [ARG...]: runs an Emscripten module built with
// -sMODULARIZE -sEXPORT_ES6 as a program under Node (runProgram). The "es2os" imports come from
// the module's own Node host (platform/src/os_node.js); further import modules from the
// `--host` modules. host.mjs imports runProgram from here.
//
// runProgram(module, args, {hosts, options, expandArgs}): `args` are the module's argv[1..] and
// its exit status becomes the process's. An argument `${NAME}` is replaced by the environment
// variable NAME (Bazel's TEST_TMPDIR for a test) unless `expandArgs` is false. Each of `hosts` is
// a module whose default export is called with the module options before the start and may add
// to them; `options` are merged in first.
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

export const expand = (a) => a.replace(/^\$\{(\w+)\}$/, (_, name) => process.env[name] ?? '');

export async function runProgram(modulePath, args, { hosts = [], options = {}, expandArgs = true } = {}) {
  const all = {
    arguments: expandArgs ? args.map(expand) : [...args],
    thisProgram: modulePath,
    onExit: (code) => { process.exitCode = code; },
    ...options,
  };
  for (const host of hosts) {
    const setup = (await import(pathToFileURL(host).href)).default;
    await setup(all);
  }
  try {
    const factory = (await import(pathToFileURL(modulePath).href)).default;
    await factory(all);
  } catch (e) {
    if (!(e && e.name === 'ExitStatus')) {
      console.error(e);
      process.exitCode = 1;
    }
  }
}

// The leading `--host HOST.mjs` pairs of a command line: [hosts, rest].
export function takeHosts(args) {
  const rest = [...args];
  const hosts = [];
  while (rest[0] === '--host') {
    hosts.push(rest[1]);
    rest.splice(0, 2);
  }
  return [hosts, rest];
}

const isMain = process.argv[1] && import.meta.url === pathToFileURL(fs.realpathSync(process.argv[1])).href;
if (isMain) {
  const [hosts, args] = takeHosts(process.argv.slice(2));
  if (!args.length) {
    console.error('usage: run.mjs [--host HOST.mjs]... MODULE.js [ARG...]');
    process.exit(2);
  }
  const [modulePath, ...programArgs] = args;
  await runProgram(modulePath, programArgs, { hosts });
}
