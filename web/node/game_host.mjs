// node game_host.mjs --site DIR --codec VFS.js --launch LAUNCH.js --shell ES2SHELL.js --sim ES2SIM.js
//                    [--language G|E|F] [--keyboard NAME] [--overlay FILE] [--menu LINE]...
//                    [--shell-args JSON] [--sim-args JSON] [--host-dir DIR]... [--cd-toc TOC.json]
//                    [--export DIR] [--export-host DIR]
//                    [--save-overlay FILE] [--record FILE] [--frame-time TICKS]
//                    [--replay SUITES.json --suite NAME [--collect-dir DIR]]
// runs the game of a built site under Node as the page runs it (docs/web_build.md sections 2, 3
// and 8): web/game_worker.mjs's launcher loop in a worker thread (game_node_worker.mjs) over the
// site's Vfs, and web/game.mjs's GameRequests on this thread with
//
// - the overlay store in memory, one transaction per persist message (as store.mjs applyAll);
//   --overlay FILE loads a JSON overlay of an earlier --save-overlay, a reload with saves;
// - every message box answered with its default button;
// - no movies (MoviePlayer.play answers 0, the shell then skips the movie);
// - the CD music of each sim process through CdMusic over a silent clock context and TOC.json
//   (without it the headless answers);
// - the frames read and the shell's and the sim's sound taken at their device rates.
//
// --menu adds a line to menu.ini's [Menu] section. --shell-args and --sim-args are JSON arrays
// of argument lists, one per start of the program (the last one repeats). Paths at or below a
// --host-dir are node:fs paths for every process. After the end, --export writes C:\ES2's DATA
// and SAV as a fresh Vfs over the stored overlay reads them (what a reload sees), --export-host
// the worker's C:\ES2HOST. --record writes a JSON summary: the steps, the requests, the store's
// transactions, the frames.
// --frame-time runs the self-test's frame-time run (web/selftest_run.mjs frameTimeRun with Q at
// tick TICKS; its language, menu lines, environment, host files and argument lists replace
// --language, --menu, --shell-args and --sim-args), and the record gets its frame_time summary.
// --replay runs the self-test's replay of suite NAME from a selftest_suites.json
// (web/selftest_run.mjs suiteRun: es2sim alone, without --launch and --shell); the record gets
// {sim, sha256, lines}, the exit code and the masked tick log hash (web/tick_log_hash.mjs), and
// --collect-dir receives the run's collected files under their file names.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { Worker } from 'node:worker_threads';
import { C, create, readFrame, takeShellSound, takeSimAudio } from '../shared.mjs';
import { SIM_AUDIO_FORMAT } from '../es2sim_imports.mjs';
import { CdMusic } from '../cd_player.mjs';
import { GameRequests, SOUND_RATE, messageBoxButtons } from '../game.mjs';
import { LAYOUT_NAMES } from '../keys.mjs';
import { GAME_DIR } from '../game_worker.mjs';
import { PERF_FILE, frameTimeRun, frameTimeSummary, suiteRun } from '../selftest_run.mjs';
import { tickLogHash } from '../tick_log_hash.mjs';
import { loadCodecFrom, openSite } from './vfs_site.mjs';

// As host.mjs: os_win32.c's clocks ignore TZ, Node's follow it.
const tz = Object.keys(process.env).filter((name) => name.toUpperCase() === 'TZ' && process.env[name]);
if (tz.length) {
  const env = { ...process.env };
  for (const name of tz) delete env[name];
  const run = spawnSync(process.execPath, [...process.execArgv, ...process.argv.slice(1)], { stdio: 'inherit', env });
  process.exit(run.status === null ? 1 : run.status | 0);
}

const usage = (why) => {
  if (why) console.error(why);
  console.error('usage: game_host.mjs --site DIR --codec VFS.js --launch LAUNCH.js --shell ES2SHELL.js --sim ES2SIM.js '
    + '[--language G|E|F] [--keyboard NAME] [--overlay FILE] [--menu LINE]... [--shell-args JSON] [--sim-args JSON] '
    + '[--host-dir DIR]... [--cd-toc TOC.json] [--export DIR] [--export-host DIR] '
    + '[--save-overlay FILE] [--record FILE] [--frame-time TICKS] [--replay SUITES.json --suite NAME [--collect-dir DIR]]');
  process.exit(2);
};

const o = { language: 'G', keyboard: 'de', menu: [], hostDirs: [] };
const single = ['--site', '--codec', '--launch', '--shell', '--sim', '--language', '--keyboard', '--overlay', '--shell-args',
  '--sim-args', '--cd-toc', '--export', '--export-host', '--save-overlay', '--record', '--frame-time',
  '--replay', '--suite', '--collect-dir'];
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  const option = args[i];
  const value = args[++i];
  if (value === undefined) usage(`${option} needs a value`);
  const key = option.slice(2).replace(/-(.)/g, (_, c) => c.toUpperCase());
  if (option === '--menu') o.menu.push(value);
  else if (option === '--host-dir') o.hostDirs.push(path.resolve(value));
  else if (single.includes(option)) o[key] = value;
  else usage(`unknown option ${option}`);
}
for (const need of o.replay ? ['site', 'codec', 'sim', 'suite'] : ['site', 'codec', 'launch', 'shell', 'sim']) {
  if (!o[need]) usage(`--${need} is required`);
}
if (!['G', 'E', 'F'].includes(o.language)) usage('--language is G, E or F');
if (!LAYOUT_NAMES.includes(o.keyboard)) usage(`no keyboard ${o.keyboard}`);
const argLists = (json) => {
  if (!json) return [];
  const lists = JSON.parse(json);
  if (!Array.isArray(lists) || !lists.every((l) => Array.isArray(l) && l.every((a) => typeof a === 'string'))) usage('argument lists: [[ARG...], ...]');
  return lists;
};

if (o.frameTime && !(Number.isInteger(Number(o.frameTime)) && Number(o.frameTime) > 0)) usage('--frame-time is a tick number');
if (o.frameTime && o.replay) usage('--frame-time or --replay');
const suite = o.replay ? JSON.parse(fs.readFileSync(o.replay, 'utf8')).suites.find((s) => s.name === o.suite) : null;
if (o.replay && !suite) usage(`no suite ${o.suite} in ${o.replay}`);
const run = o.frameTime ? frameTimeRun({ ticks: Number(o.frameTime) }) : suite ? suiteRun(suite) : null;

// The page's IndexedDB store in memory: one applyAll is one transaction.
class MemoryStore {
  constructor(entries = []) {
    this.map = new Map(entries);
    this.transactions = [];
  }
  applyAll(changes) {
    this.transactions.push(changes.map(([key, entry]) => [key, entry ? entry.kind : null]));
    for (const [key, entry] of changes) {
      if (entry) this.map.set(key, structuredClone(entry));
      else this.map.delete(key);
    }
    return Promise.resolve();
  }
  async load() {
    return [...this.map.entries()].sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
  }
}

const revive = (entries) => entries.map(([key, e]) => [key, e.kind === 'bytes' ? { ...e, data: Uint8Array.from(Buffer.from(e.data, 'base64')) } : e]);
const serial = (entries) => entries.map(([key, e]) => [key, e.kind === 'bytes' ? { ...e, data: Buffer.from(e.data).toString('base64') } : e]);

const store = new MemoryStore(o.overlay ? revive(JSON.parse(fs.readFileSync(o.overlay, 'utf8'))) : []);
const toc = o.cdToc ? JSON.parse(fs.readFileSync(o.cdToc, 'utf8')) : null;
const shared = create();
const t0 = performance.now();
const clock = () => (performance.now() - t0) / 1000;
const steps = [], boxes = [], mci = [], frames = {};
let program = null;

const silentContext = {
  get currentTime() { return clock(); },
  destination: {},
  createBufferSource: () => ({ connect() {}, disconnect() {}, start() {}, stop() {} }),
};
const requests = new GameRequests({
  shared, store,
  dialog: (text, title, flags) => {
    const button = messageBoxButtons(flags).default;
    boxes.push({ program, text, title, flags, button });
    return button;
  },
  newMusic: (notify) => new CdMusic({
    toc, context: silentContext, load: async () => ({}), setTimer: setTimeout, clearTimer: clearTimeout, notify,
  }),
  onEnd: (result) => finish(result),
  log: (text) => console.error(text),
});

let worker = null;
let ended = null;
async function finish(result) {
  if (ended) return;
  ended = result;
  clearInterval(turn);
  await requests.writes;
  await worker.terminate();
  if (o.saveOverlay) fs.writeFileSync(o.saveOverlay, JSON.stringify(serial(await store.load())) + '\n');
  if (o.export) {
    // A reload: a fresh Vfs over the stored overlay.
    const vfs = openSite(o.site, await loadCodecFrom(o.codec));
    vfs.loadOverlay(await store.load());
    for (const dir of ['DATA', 'SAV']) exportTree(vfs, `${GAME_DIR}\\${dir}`, path.join(o.export, dir));
  }
  const collected = new Map(result.files ?? []);
  if (o.collectDir) {
    fs.mkdirSync(o.collectDir, { recursive: true });
    for (const [name, bytes] of collected) fs.writeFileSync(path.join(o.collectDir, name.split('\\').at(-1)), bytes);
  }
  if (o.record) {
    const summary = {
      state: result.state, error: result.error ?? null, steps, message_boxes: boxes, mci,
      transactions: store.transactions, frames,
    };
    if (o.frameTime) summary.frame_time = frameTimeSummary(collected.get(PERF_FILE));
    if (suite) {
      const log = collected.get(suite.tick_log);
      summary.replay = { sim: result.state?.sim ?? null, ...(log ? await tickLogHash(log) : { sha256: null, lines: 0 }) };
    }
    fs.writeFileSync(o.record, JSON.stringify(summary, null, 1) + '\n');
  }
  if (result.error) console.error(result.error);
  process.exitCode = result.error ? 1 : 0;
}

function exportTree(vfs, dir, target) {
  fs.mkdirSync(target, { recursive: true });
  if (!vfs.stat(dir)) return;
  for (const e of vfs.list(dir)) {
    const name = `${dir}\\${e.name}`;
    if (e.dir) exportTree(vfs, name, path.join(target, e.name));
    else fs.writeFileSync(path.join(target, e.name), vfs.readAll(name));
  }
}

worker = new Worker(new URL('./game_node_worker.mjs', import.meta.url), {
  workerData: {
    sab: shared.sab, site: o.site, codec: o.codec, launch: o.launch, shell: o.shell, sim: o.sim,
    overlay: await store.load(), language: run?.language ?? o.language, layout: o.keyboard,
    menuLines: run?.menuLines ?? o.menu, shellArgs: run?.shellArgs ?? argLists(o.shellArgs),
    simArgs: run?.simArgs ?? argLists(o.simArgs), environment: run?.environment ?? {}, hostFiles: run?.hostFiles ?? [],
    simOnly: !!run?.simOnly, gameFiles: run?.gameFiles ?? [],
    collect: run?.collect ?? [], hostDirs: o.hostDirs,
    exportHost: o.exportHost ?? null,
  },
});
worker.on('message', (m) => {
  if (m.kind === 'process') {
    program = m.program;
    steps.push(`start ${m.program}`);
    frames[steps.length - 1] = 0;
  } else if (m.kind === 'process_end') {
    steps.push(`exit ${m.program} ${m.code}`);
    program = null;
  } else if (m.kind === 'mci') {
    mci.push({ device: m.device, message: m.message, flags: m.flags });
  }
  requests.handle(m);
});
worker.on('error', (e) => finish({ state: null, error: e && e.stack ? e.stack : String(e) }));
worker.on('exit', (code) => finish({ state: null, error: `the game worker exited with code ${code} before its end` }));

// The page's turn: the newest frame, the sound devices' take at their rates.
const simBytesPerMs = SIM_AUDIO_FORMAT.rate * SIM_AUDIO_FORMAT.channels / 1000;
const shellFramesPerMs = SOUND_RATE / 1000;
let lastSeq = 0, last = performance.now(), simDue = 0, shellDue = 0;
const turn = setInterval(() => {
  const seq = Atomics.load(shared.control, C.FRAME_SEQ);
  if (seq !== lastSeq && readFrame(shared)) {
    const k = Object.keys(frames).at(-1);
    if (k !== undefined) frames[k] += (seq - lastSeq) >>> 0;
    lastSeq = seq;
  }
  const now = performance.now();
  simDue += (now - last) * simBytesPerMs;
  shellDue += (now - last) * shellFramesPerMs;
  last = now;
  const sim = takeSimAudio(shared, Math.floor(simDue)).length;
  simDue = sim ? simDue - sim : 0;
  const shell = takeShellSound(shared, Math.floor(shellDue)).length / 2;
  shellDue = shell ? shellDue - shell : 0;
}, 10);
