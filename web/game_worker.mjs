// The game worker's processes (docs/web_build.md sections 2, 3 and 8): ES.EXE's launcher loop
// (launcher.mjs over launch.wasm) starts es2shell.wasm and es2sim_window.wasm one after another,
// each as a fresh instance of its compiled module, over one Vfs whose overlay changes go to the
// page. The same code runs in the browser's worker (worker.js) and in the Node test host
// (web/node/game_host.mjs).
//
//   const state = await runGame({
//     shared, post,                       // shared.mjs views; postMessage to the page
//     vfs, launchFactory, shellFactory, simFactory, shellModule, simModule,
//     keyboard, metrics, memory,          // keys.mjs Keyboard; GetSystemMetrics; launcher.mjs memory
//     prefetch(mission),                  // resolves once the mission's parts are in the Vfs
//     menuLines, shellArgs(n), simArgs(n), environment, hostFiles,   // tests, self-test (below)
//     simOnly, gameFiles, wrapFiles(files), log,
//   });
//
// For tests and the self-test's run: menuLines are further lines of menu.ini's [Menu] section;
// shellArgs(n) and simArgs(n) give the arguments the n-th start (from 0) of the program gets
// after its prefix; environment ({NAME: value}) goes into the environment block es2sim copies
// and hostFiles ([[path, bytes]]) below HOST_DIR; wrapFiles(files) replaces the es2os file
// system of every process (web/node/os_host.mjs's routedFiles keeps a test's own paths on node:fs).
// With simOnly (the self-test's suite replays) no launcher and no shell run: es2sim starts once
// with SIM_PREFIX and simArgs(0), after gameFiles ([[path, bytes]] below GAME_DIR) are written,
// and the state is {sim: exit code}.
//
// Messages to the page, beside the requests of es2shell_imports.mjs and es2sim_imports.mjs:
//   {kind: 'process', seq, program}       a process starts ('shell' or 'sim'); answered once
//                                         the page has queued INPUT.START and the window's
//                                         first input
//   {kind: 'process_end', program, code}  it ended
//   {kind: 'persist', changes}            one Vfs operation's overlay changes, [[key, entry]]
//                                         with storedEntry's entries (store.mjs applyAll)
//   {kind: 'message_box', seq, text, title, flags}   the launcher's MessageBoxA
//
// The worker's own files live in the Vfs below HOST_DIR and never reach the page's store: the
// shell's menu.ini (and its shell.log) and the environment block es2sim copies.
import { runLauncher, loadLaunch, REFERENCE_MEMORY } from './launcher.mjs';
import { es2shellImports } from './es2shell_imports.mjs';
import { es2simWindowImports, DEFAULT_METRICS } from './es2sim_imports.mjs';
import { vfsFiles } from './vfs_files.mjs';
import { INPUT, INPUT_WORDS, request, takeInput } from './shared.mjs';
import { LANGUAGE_FILE, MISSION_SCRIPT, missionKey, storedEntry } from './vfs.mjs';

export const GAME_DIR = 'C:\\ES2';
export const CD_ROOT = 'D:\\';
export const HOST_DIR = 'C:\\ES2HOST';
export const MENU_DIR = `${HOST_DIR}\\MENU`;
export const ENVIRONMENT_FILE = `${HOST_DIR}\\ENVIRON.BIN`;

// The shell's menu.ini: the installation and the CD of the site. No seed (the shell seeds from
// GetTickCount as VSHELL does), the CD check and the hangar as the original's.
export const MENU_INI = `[Menu]\r\ngame_dir = ${GAME_DIR}\r\ncd_root = ${CD_ROOT}\r\n`;

// The environment block of the reference size (section 7: 102 strings in 5648 bytes of
// CW3220's heap blocks, frames_known.REFERENCE_ENVIRONMENT) with `vars` ({NAME: value}) and no
// variable of the page's: replay_suite.frame_environment over `vars` as its kept base. The pads
// Z000, Z001, ... fill the strings and bytes the variables leave, pad i "Z<iii>=x..." in a block
// of 16 + 4 * (each + (i < rest)) bytes; a string of n characters takes CW3220's
// max(16, roundup4(n + 1) + 8). The strings sorted by upper-cased name, each NUL-terminated,
// then one NUL.
const heapBlock = (n) => Math.max(16, ((n + 1 + 3) & ~3) + 8);
export function referenceEnvironment(vars = {}, strings = 102, size = 5648) {
  const env = Object.entries(vars).map(([name, value]) => [name, String(value)]);
  const pads = strings - env.length;
  const spare = size - env.reduce((sum, [k, v]) => sum + heapBlock(k.length + 1 + v.length), 0) - 16 * pads;
  if (pads < 0 || spare < 0 || spare % 4 || (pads === 0 && spare)) {
    throw new Error('the variables do not fit the reference environment');
  }
  const each = pads ? Math.floor(spare / 4 / pads) : 0, rest = pads ? (spare / 4) % pads : 0;
  for (let i = 0; i < pads; i++) {
    const block = 16 + 4 * (each + (i < rest ? 1 : 0));
    const name = `Z${String(i).padStart(3, '0')}`;
    env.push([name, 'x'.repeat(block - 9 - name.length - 1)]);
  }
  const upper = (s) => s.toUpperCase();
  env.sort(([a], [b]) => (upper(a) < upper(b) ? -1 : upper(a) > upper(b) ? 1 : 0));
  return Uint8Array.from(env.map(([k, v]) => `${k}=${v}\0`).join('') + '\0', (c) => c.charCodeAt(0) & 0xff);
}

const bytesOf = (text) => Uint8Array.from(text, (c) => c.charCodeAt(0) & 0xff);
const isHostKey = (key) => key === HOST_DIR || key.startsWith(`${HOST_DIR}\\`);

// The Vfs's overlay changes as page messages, without the worker's own files.
export function persistTo(post) {
  return (changes) => {
    const kept = changes.filter(([key]) => !isHostKey(key)).map(([key, e]) => [key, e && storedEntry(e)]);
    if (kept.length) post({ kind: 'persist', changes: kept });
  };
}

// The worker's own files in the Vfs (written again at every run): menu.ini, the environment
// block with `environment`'s variables, and `hostFiles` ([[path, bytes]], below HOST_DIR).
export function writeHostFiles(vfs, menuLines = [], environment = {}, hostFiles = []) {
  for (const dir of [HOST_DIR, MENU_DIR]) if (!vfs.stat(dir)) vfs.mkdir(dir);
  vfs.write(`${MENU_DIR}\\menu.ini`, bytesOf(MENU_INI + menuLines.map((line) => `${line}\r\n`).join('')));
  vfs.write(ENVIRONMENT_FILE, referenceEnvironment(environment));
  for (const [path, bytes] of hostFiles) {
    if (!isHostKey(path.toUpperCase())) throw new Error(`${path}: not below ${HOST_DIR}`);
    vfs.write(path, bytes);
  }
}

// The installation before the first process: the language step of the landing page
// (Vfs.installLanguage) when the overlay has no LANGUAGE.CFG yet.
export function prepareInstallation(vfs, language) {
  if (!vfs.stat(LANGUAGE_FILE)) vfs.installLanguage(language);
}

// One instance of a module run as a program: its exit status (an exception other than
// ExitStatus ends it with 1, as the Node workers do).
async function runInstance(factory, options, log) {
  let code = 0;
  try {
    await factory({ ...options, onExit: (status) => { code = status; } });
  } catch (e) {
    if (e && e.name === 'ExitStatus') code = e.status;
    else {
      log(`${options.thisProgram}: ${e && e.stack ? e.stack : e}`);
      code = 1;
    }
  }
  return code;
}

// The launcher's ES2_LAUNCH_SHELL and ES2_LAUNCH_SIM: three arguments each, which a test's
// extra arguments follow.
export const SHELL_PREFIX = `es2shell --menu ${MENU_DIR}`;
export const SIM_PREFIX = `es2sim --environment ${ENVIRONMENT_FILE}`;
const PREFIX_ARGS = 3;

// The n-th start's arguments of a list of argument lists (the last one repeats; none: []).
export const nthArgs = (lists) => (n) => (lists && lists.length ? lists[Math.min(n, lists.length - 1)] : []);

// The files a run hands back at its end: [[path, bytes]] of the given Vfs paths that exist.
export function collectFiles(vfs, paths = []) {
  return paths.filter((p) => vfs.stat(p)).map((p) => [p, vfs.readAll(p)]);
}

export async function runGame({
  shared, post, vfs, launchFactory, shellFactory, simFactory, shellModule, simModule, keyboard,
  metrics = DEFAULT_METRICS, memory = REFERENCE_MEMORY, prefetch = async () => {}, menuLines = [],
  shellArgs = () => [], simArgs = () => [], environment = {}, hostFiles = [], simOnly = false, gameFiles = [],
  wrapFiles = (files) => files, log = () => {},
}) {
  writeHostFiles(vfs, menuLines, environment, hostFiles);
  for (const [path, bytes] of gameFiles) {
    if (!path.toUpperCase().startsWith(`${GAME_DIR}\\`)) throw new Error(`${path}: not below ${GAME_DIR}`);
    vfs.write(path, bytes);
  }
  const files = wrapFiles(vfsFiles(vfs, { cwd: GAME_DIR }));
  const read = (path) => (vfs.stat(path) ? vfs.readAll(path) : null);
  const language = read(LANGUAGE_FILE);
  const starts = { shell: 0, sim: 0 };
  const entry = new Int32Array(INPUT_WORDS);

  // A new process starts with an empty message queue: the ring up to the page's INPUT.START goes
  // (what the last process left and input during the switch); the window's activation follows it.
  const start = (program) => {
    request(shared, post, { kind: 'process', program });
    while (takeInput(shared, entry, 1) > 0 && entry[0] !== INPUT.START);
  };
  const run = async (program, argv) => {
    if (program === 'sim') {
      const mission = missionKey(read(MISSION_SCRIPT));
      if (mission) await prefetch(mission);
    }
    start(program);
    const shell = program === 'shell';
    const extra = (shell ? shellArgs : simArgs)(starts[program]++);
    const code = await runInstance(shell ? shellFactory : simFactory, {
      arguments: [...argv.slice(1, PREFIX_ARGS), ...extra, ...argv.slice(PREFIX_ARGS)],
      thisProgram: argv[0],
      wasmModule: shell ? shellModule : simModule,
      es2osFiles: files,
      ...(shell ? { es2shell: es2shellImports(shared, post) }
        : { es2sim: es2simWindowImports(shared, { post, keyboard, metrics }) }),
    }, log);
    post({ kind: 'process_end', program, code });
    return code;
  };

  if (simOnly) return { sim: await run('sim', SIM_PREFIX.split(' ')) };
  const launch = await loadLaunch(launchFactory);
  const state = await runLauncher(launch, {
    commandLine: '',
    languageByte: language && language.length ? language[0] : -1,
    errorStr: read(`${GAME_DIR}\\ERROR.STR`),
    memory,
    shellPrefix: SHELL_PREFIX,
    simPrefix: SIM_PREFIX,
    runShell: (argv) => run('shell', argv),
    runSim: (argv) => run('sim', argv),
    message: (text, caption) => request(shared, post, { kind: 'message_box', text, title: caption, flags: 0x30 }),
    log,
  });
  return state;
}
