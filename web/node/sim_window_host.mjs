// node sim_window_host.mjs [--keyboard NAME] [--metrics CX,CY,FX,FY,CAPTION] [--cd-toc TOC.json]
//                          [--script FILE] [--record FILE] MODULE.js [ARG...]
// runs es2sim_window's wasm build (//sim:es2sim_window_wasm) as a program under a scripted page:
// the module in a worker thread (sim_window_worker.mjs) with the "es2os" imports over node:fs in
// the current directory and the "es2sim" imports of web/es2sim_imports.mjs over the shared
// buffer of web/shared.mjs (docs/web_build.md sections 3, 9, 10 and 12). The page's side here:
//
// - the input ring: FOCUS 1 at the start (the page has the focus), then the entries of the
//   script, a JSON array of {"at": ms after the start, "input": [type, a, b, c, d]};
// - the frame slots: the newest frame read on each turn of the host's timer;
// - the simulation audio ring: played at 22,050 stereo frames per second;
// - the requests: the cursor and the display recorded, a message box answered IDOK, MCI
//   through web/cdaudio.mjs over TOC.json (cd/toc.json of a site; without it the headless
//   answers), MM_MCINOTIFY back as an MCI entry;
// - with --record, a JSON summary at the end: the frames, the audio bytes taken, the requests.
//
// The keyboard is web/keys.mjs's with the layout NAME (de by default). METRICS are the
// GetSystemMetrics values the window platform reports (640,480,4,4,18 by default). The exit
// status is the module's.
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { Worker } from 'node:worker_threads';
import { create, pushInput, readFrame, reply, takeSimAudio, C, INPUT } from '../shared.mjs';
import { answerMci, DEFAULT_METRICS, SIM_AUDIO_FORMAT } from '../es2sim_imports.mjs';
import { CdAudio } from '../cdaudio.mjs';
import { LAYOUT_NAMES } from '../keys.mjs';

// As host.mjs: os_win32.c's clocks ignore TZ, Node's follow it, so with TZ set the host runs
// itself again without it.
const tz = Object.keys(process.env).filter((name) => name.toUpperCase() === 'TZ' && process.env[name]);
if (tz.length) {
  const env = { ...process.env };
  for (const name of tz) delete env[name];
  const run = spawnSync(process.execPath, [...process.execArgv, ...process.argv.slice(1)], { stdio: 'inherit', env });
  process.exit(run.status === null ? 1 : run.status | 0);
}

const usage = () => {
  console.error('usage: sim_window_host.mjs [--keyboard NAME] [--metrics CX,CY,FX,FY,CAPTION] [--cd-toc TOC.json] '
    + '[--script FILE] [--record FILE] MODULE.js [ARG...]');
  process.exit(2);
};

let args = process.argv.slice(2);
let layout = 'de', metrics = DEFAULT_METRICS, toc = null, script = [], record = null;
for (;;) {
  const [option, value] = args;
  if (!['--keyboard', '--metrics', '--cd-toc', '--script', '--record'].includes(option)) break;
  if (value === undefined) usage();
  if (option === '--keyboard') {
    if (!LAYOUT_NAMES.includes(value)) usage();
    layout = value;
  } else if (option === '--metrics') {
    metrics = value.split(',').map(Number);
    if (metrics.length !== 5 || !metrics.every((n) => Number.isInteger(n) && n >= 0 && n <= 0xffffffff)) usage();
  } else if (option === '--cd-toc') {
    toc = JSON.parse(fs.readFileSync(value, 'utf8'));
  } else if (option === '--script') {
    script = JSON.parse(fs.readFileSync(value, 'utf8'));
    if (!Array.isArray(script) || !script.every((e) => Number.isFinite(e.at) && Array.isArray(e.input))) usage();
  } else {
    record = value;
  }
  args = args.slice(2);
}
if (!args.length) usage();
const [modulePath, ...programArgs] = args;

const shared = create();
const t0 = performance.now();
const push = (type, a = 0, b = 0, c = 0, d = 0) => pushInput(shared, type, a, b, c, d, Math.floor(performance.now() - t0));
const cd = new CdAudio({
  toc,
  clock: () => (performance.now() - t0) / 1000,
  notify: (code, device) => push(INPUT.MCI, code, device),
});
const requests = [];
let frames = 0, lastSeq = 0, audioBytes = 0, exitCode = 1;

const worker = new Worker(new URL('./sim_window_worker.mjs', import.meta.url),
  { workerData: { module: modulePath, args: programArgs, sab: shared.sab, layout, metrics } });
worker.on('message', (m) => {
  if (m.kind === 'exit') { exitCode = m.code; return; }
  if (m.kind === 'mci') {
    const error = answerMci(shared, cd, m);
    requests.push({ kind: 'mci', device: m.device, message: m.message, flags: m.flags, error });
    return;
  }
  requests.push(m);
  if (m.kind === 'message_box') reply(shared, m.seq, 1);
});
worker.on('error', (e) => { console.error(e); exitCode = 1; });

push(INPUT.FOCUS, 1);
const timers = script.map((e) => setTimeout(() => push(...e.input), e.at));

// The page's turn: the newest frame, the audio device's take, a play that has reached TO.
const bytesPerMs = SIM_AUDIO_FORMAT.rate * SIM_AUDIO_FORMAT.channels / 1000;
let due = 0, last = performance.now();
const turn = setInterval(() => {
  const seq = Atomics.load(shared.control, C.FRAME_SEQ);
  if (seq !== lastSeq && readFrame(shared)) { frames += (seq - lastSeq) >>> 0; lastSeq = seq; }
  const now = performance.now();
  due += (now - last) * bytesPerMs;
  last = now;
  const taken = takeSimAudio(shared, Math.floor(due)).length;
  audioBytes += taken;
  due = taken ? due - taken : 0;
  const at = cd.nextEvent();
  if (at !== null && cd.clock() >= at) cd.poll();
}, 10);

worker.on('exit', () => {
  clearInterval(turn);
  for (const t of timers) clearTimeout(t);
  if (record) {
    const summary = { exit: exitCode, frames, audio_bytes: audioBytes, mci_commands: cd.commands, requests };
    fs.writeFileSync(record, JSON.stringify(summary, null, 1) + '\n');
  }
  process.exitCode = exitCode;
});
