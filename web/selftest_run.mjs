// The self-test's frame-time run (docs/web_build.md section 16): the real game chain with a
// fixed script, its frame times from es2sim's recorder (ES2SIM_PERF, sim/include/es2/sim/perf.h).
//
// The shell runs headless: a new pilot, MISSION twice, Rock & Roll. The sim flies the mission on
// paced virtual time (47 ms a frame, as the game) with a journal that presses Q at tick `ticks`
// and takes the quit box's second button (exit 3); the shell's second start leaves the results
// and quits. The run's installation lives only in the run's memory store: the user's saves are
// not touched. web/node/game_host.mjs --frame-time runs the same configuration under Node.
import { HOST_DIR } from './game_worker.mjs';

export const FRAME_BUDGET_MS = 40;
export const PERF_FILE = `${HOST_DIR}\\perf.ndjson`;
const CONFIG_FILE = `${HOST_DIR}\\selftest.ini`;
const JOURNAL_FILE = `${HOST_DIR}\\selftest.jsonl`;
const LAUNCH_FILE = `${HOST_DIR}\\launch.script`;
const RESULTS_FILE = `${HOST_DIR}\\results.script`;

const START = 'step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype TESTPILOT\n'
  + 'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n';
const LAUNCH = START + 'click service_bay.19\nstep 40\nclick service_bay.19\nstep 40\nexpect page=1\n'
  + 'click mission.27\nstep 5\n';
const RESULTS = 'step 20\nexpect screen=5\nclick service_bay.05\nstep 5\nexpect screen=0\nclick main_menu.13\nstep 5\n';

const ascii = (text) => Uint8Array.from(text, (c) => c.charCodeAt(0));
function utf16(text) {
  const out = new Uint8Array(2 + 2 * text.length);
  out[0] = 0xff; out[1] = 0xfe;
  for (let i = 0; i < text.length; i++) {
    out[2 + 2 * i] = text.charCodeAt(i) & 0xff;
    out[3 + 2 * i] = text.charCodeAt(i) >> 8;
  }
  return out;
}

export function frameTimeRun({ ticks = 640 } = {}) {
  const journal = [
    { event: 'script', name: 'selftest-frame-time', note: `Q at ${ticks}, Tab, Enter: the second button` },
    { f: ticks, api: 'keydown', vk: 81 },
    { f: ticks, p: 300, api: 'keydown', vk: 9 },
    { f: ticks, p: 310, api: 'keyup', vk: 9 },
    { f: ticks, p: 320, api: 'keydown', vk: 13 },
    { f: ticks, p: 330, api: 'keyup', vk: 13 },
    { f: ticks + 2, api: 'keyup', vk: 81 },
  ];
  const config = '[time]\r\nvirtual_time_step_ms=47\r\nvirtual_time_start_ms=16777216\r\nvirtual_time_pace=1\r\n'
    + 'virtual_time_intra_frame=1\r\n[input]\r\nmode=replay\r\n'
    + `journal=${JOURNAL_FILE}\r\n[arming]\r\nearly=1\r\n`;
  return {
    language: 'G',
    menuLines: ['seed = 71', 'hangar = lowest-free', 'online_manual = text'],
    environment: { ES2SIM_PERF: PERF_FILE },
    hostFiles: [
      [CONFIG_FILE, utf16(config)],
      [JOURNAL_FILE, ascii(journal.map((e) => `${JSON.stringify(e)}\n`).join(''))],
      [LAUNCH_FILE, ascii(LAUNCH)],
      [RESULTS_FILE, ascii(RESULTS)],
    ],
    shellArgs: [['--headless', '--script', LAUNCH_FILE], ['--headless', '--script', RESULTS_FILE]],
    simArgs: [['--config', CONFIG_FILE]],
    collect: [PERF_FILE],
  };
}

// perf_suite.py's nearest-rank percentile and median.
const percentile = (sorted, q) => sorted[Math.min(sorted.length, Math.max(1, Math.ceil(sorted.length * q / 100))) - 1];
const median = (s) => (s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2);

// The recorder file's busy time per frame (us - wait) in ms: frames, median, p95, max and the
// verdict (p95 under FRAME_BUDGET_MS).
export function frameTimeSummary(bytes) {
  const text = typeof bytes === 'string' ? bytes : new TextDecoder().decode(bytes ?? new Uint8Array());
  const frames = [];
  let header = null, end = null;
  for (const line of text.split('\n')) {
    if (!line.trim()) continue;
    const record = JSON.parse(line);
    if ('perf' in record) header = record;
    else if ('f' in record) frames.push(record);
    else if ('end' in record) end = record;
  }
  const busy = frames.map((f) => Math.max(0, f.us - (f.wait ?? 0)) / 1000).sort((a, b) => a - b);
  if (!busy.length) return { frames: 0, mode: header?.mode ?? null, end: end?.end ?? null, ok: false };
  const round = (v) => Math.round(v * 100) / 100;
  const p95 = percentile(busy, 95);
  return {
    frames: busy.length, mode: header?.mode ?? null, end: end?.end ?? null,
    median: round(median(busy)), p95: round(p95), max: round(busy.at(-1)), ok: p95 < FRAME_BUDGET_MS,
  };
}

// The self-test's suite replays (docs/web_build.md section 16): selftest_suites.json, written by
// tools/web/selftest_suites.py, holds per suite its inputs (the launch files below C:\ES2, the
// INI and the journal below C:\ES2HOST, es2sim's arguments with --headless) and the Node run's
// masked tick log hash (tick_log_hash.mjs) and exit code. The page and game_host.mjs --replay
// run each with es2sim alone (runGame's simOnly) on a store in memory.
export const SUITES_FILE = 'selftest_suites.json';

function base64Bytes(text) {
  const raw = atob(text);
  const out = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) out[i] = raw.charCodeAt(i);
  return out;
}

export function suiteRun(entry) {
  const files = (list) => (list ?? []).map(([path, data]) => [path, base64Bytes(data)]);
  return {
    language: entry.language,
    simOnly: true,
    environment: {},
    gameFiles: files(entry.game_files),
    hostFiles: files(entry.host_files),
    simArgs: [entry.args],
    collect: entry.collect,
  };
}
