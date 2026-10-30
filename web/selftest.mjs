// The browser self-test (selftest.html; docs/web_build.md section 16): the isolation state,
// every feature check of the landing page with its detail, a key readout (per key the code, the
// scan code and the VK of keys.mjs, which the owner compares with the key pressed), and three
// checks each started by its button (a user activation for the AudioContexts, the video and
// fullscreen):
//
// - audio: audio.js (audio_worklet.mjs) loads into a 22050 Hz context and its AudioWorkletNode
//   plays a short 440 Hz tone from the shell sound ring; on a device-rate context cd/toc.json's
//   first audio track decodes to its TOC length and its first seconds play;
// - video: media/manifest.json's movie with the fewest frames plays through MoviePlayer to its
//   end (MCI_NOTIFY_SUCCESSFUL);
// - frame times: the game chain of selftest_run.mjs in this page (canvas, sound, the worker) on
//   a store in memory, its busy time per frame from es2sim's recorder; passed when p95 is under
//   40 ms;
// - suite replays, one row per suite of selftest_suites.json when the site has it: es2sim alone
//   in the worker on a store in memory; passed when the exit code and the masked tick log hash
//   (tick_log_hash.mjs) equal the Node run's and the site's es2sim.wasm is the one they came from.
//
// `env` is the page's global object and `doc` its document; web/node/page_test.mjs passes fakes.
import { runChecks } from './checks.mjs';
import { ensureIsolation } from './isolation.mjs';
import { CODE_SCAN, Keyboard, layoutForLanguage } from './keys.mjs';
import { text } from './texts.mjs';
import { SHELL_SOUND_FRAMES, create, writeShellSound } from './shared.mjs';
import { AUDIO_PROCESSOR, SOUND_RATE, startGame } from './game.mjs';
import { MCI_NOTIFY, MoviePlayer } from './movie_player.mjs';
import { FRAME_BUDGET_MS, PERF_FILE, SUITES_FILE, frameTimeRun, frameTimeSummary, suiteRun } from './selftest_run.mjs';
import { tickLogHash } from './tick_log_hash.mjs';

const hex = (n, width) => '0x' + n.toString(16).padStart(width, '0');

// One readout row of a keydown event: [code, key, scan code, VK].
export function keyReadout(keyboard, event) {
  keyboard.correct(event.code, event.keyCode);
  const scan = CODE_SCAN[event.code];
  const numlock = typeof event.getModifierState === 'function' && event.getModifierState('NumLock');
  return [
    event.code,
    event.key,
    scan === undefined ? '-' : hex(scan, 3),
    hex(keyboard.vk(event.code, numlock), 2),
  ];
}

function row(doc, cells, className) {
  const tr = doc.createElement('tr');
  if (className) tr.className = className;
  for (const c of cells) {
    const td = doc.createElement('td');
    td.textContent = c;
    tr.append(td);
  }
  return tr;
}

// The layout keys.mjs uses: the layout map's best match in Chrome, else the browser language's.
export async function chooseKeyboard(env) {
  const fallback = layoutForLanguage(env.navigator?.language);
  try {
    const map = await env.navigator.keyboard.getLayoutMap();
    return new Keyboard(Keyboard.chooseLayout(map, fallback));
  } catch {
    return new Keyboard(fallback);
  }
}

const fetchJson = async (env, url) => {
  try {
    const r = await env.fetch(url);
    return r.ok ? await r.json() : null;
  } catch {
    return null;
  }
};
const fetchBytes = async (env, url, signal) => {
  const r = await env.fetch(url, { signal });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.arrayBuffer();
};
const message = (e) => (e && e.message ? e.message : String(e));

// One stereo tone of `frames` frames at `rate`, faded in and out over 10 ms.
export function tone(frames, rate, hz = 440, gain = 0.25) {
  const samples = new Float32Array(frames * 2);
  const fade = Math.round(rate / 100);
  for (let i = 0; i < frames; i++) {
    const edge = Math.min(1, i / fade, (frames - 1 - i) / fade);
    samples[2 * i] = samples[2 * i + 1] = gain * edge * Math.sin(2 * Math.PI * hz * i / rate);
  }
  return samples;
}

export const TONE_SECONDS = 0.3; // fits the shell sound ring (SHELL_SOUND_FRAMES at SOUND_RATE)
export const MUSIC_SECONDS = 4;

// The audio row: {ok, detail}. A decoded track may differ from its TOC length by up to 0.1 s
// (Opus's pre-skip and padding). The tone, then the track's start, are heard before the row ends.
export async function audioCheck(env) {
  const sound = new env.AudioContext({ sampleRate: SOUND_RATE });
  const music = new env.AudioContext();
  const wait = (s) => new Promise((resolve) => env.setTimeout(resolve, s * 1000));
  try {
    await sound.audioWorklet.addModule('audio.js');
    const shared = create();
    const node = new env.AudioWorkletNode(sound, AUDIO_PROCESSOR, { outputChannelCount: [2], processorOptions: { sab: shared.sab } });
    node.connect(sound.destination);
    const frames = Math.min(SHELL_SOUND_FRAMES, Math.round(TONE_SECONDS * sound.sampleRate));
    writeShellSound(shared, tone(frames, sound.sampleRate), frames);
    const toned = wait(frames / sound.sampleRate + 0.2);
    const worklet = `${sound.sampleRate} Hz worklet, music ${music.sampleRate} Hz`;
    const toc = await fetchJson(env, 'cd/toc.json');
    const track = toc?.tracks?.find((t) => t.type === 'audio' && t.file);
    if (!track) {
      await toned;
      return { ok: true, detail: `${worklet}; ${text('selftest.noCdAudio')}` };
    }
    const decoded = await music.decodeAudioData(await fetchBytes(env, `cd/${track.file}`));
    const expected = track.length / (toc.frames_per_second ?? 75);
    await toned;
    const source = music.createBufferSource();
    source.buffer = decoded;
    source.connect(music.destination);
    const seconds = Math.min(MUSIC_SECONDS, decoded.duration);
    source.start(0, 0, seconds);
    await wait(seconds);
    return {
      ok: Math.abs(decoded.duration - expected) <= 0.1,
      detail: `${worklet}; ${track.file} ${decoded.duration.toFixed(2)} s (TOC ${expected.toFixed(2)} s)`,
    };
  } finally {
    sound.close?.()?.catch?.(() => {});
    music.close?.()?.catch?.(() => {});
  }
}

// The movie with the fewest frames under a CD path the shell would open (avi\<name>.avi,
// avf\..., avg\...).
export function shortestMovie(manifest) {
  const entries = Object.entries(manifest ?? {}).filter(([, e]) => e.frames > 0);
  if (!entries.length) return null;
  const [name, entry] = entries.reduce((a, b) => (b[1].frames < a[1].frames || (b[1].frames === a[1].frames && b[0] < a[0]) ? b : a));
  const path = name.includes('/') ? `D:\\${name.replace('/', '\\')}.avi` : `D:\\avi\\${name}.avi`;
  return { name, entry, path };
}

// The video row: the movie in a box of its own size at the page's top left, then removed.
// `timeoutMs` bounds the wait (by default a third of a second per frame plus 10 s).
export async function videoCheck(env, doc, { timeoutMs = null } = {}) {
  const manifest = await fetchJson(env, 'media/manifest.json');
  const movie = shortestMovie(manifest);
  if (!movie) return { ok: false, detail: text('selftest.noMedia') };
  const started = env.performance.now();
  let notified;
  const ended = new Promise((resolve) => { notified = resolve; });
  const player = new MoviePlayer({
    manifest,
    fetchBytes: (url, signal) => fetchBytes(env, url, signal),
    MediaSource: env.MediaSource,
    createObjectURL: (o) => env.URL.createObjectURL(o),
    revokeObjectURL: (u) => env.URL.revokeObjectURL(u),
    createVideo: () => {
      const v = doc.createElement('video');
      v.playsInline = true;
      return v;
    },
    attach: (v) => doc.body.append(v),
    detach: (v) => v.remove(),
    notify: notified,
  });
  const { width, height } = movie.entry;
  if (!player.play({ path: movie.path, x: 0, y: 0, width, height }, { left: 0, top: 0, width, height }, width, height)) {
    return { ok: false, detail: `${movie.name}: ${text('selftest.noMedia')}` };
  }
  const limit = timeoutMs ?? movie.entry.frames / 3 * 1000 + 10000;
  let timer;
  const code = await Promise.race([ended, new Promise((resolve) => { timer = env.setTimeout(() => resolve(null), limit); })]);
  env.clearTimeout(timer);
  player.stop();
  const seconds = ((env.performance.now() - started) / 1000).toFixed(1);
  const outcome = code === MCI_NOTIFY.SUCCESSFUL ? '' : code === null ? ', timeout' : `, MCI ${code}`;
  return { ok: code === MCI_NOTIFY.SUCCESSFUL, detail: `${movie.name}, ${movie.entry.frames} frames: ${seconds} s${outcome}` };
}

// The overlay store of the frame-time run: in memory, so the saves of the site stay untouched.
export class MemoryStore {
  constructor() { this.map = new Map(); }
  applyAll(changes) {
    for (const [key, entry] of changes) {
      if (entry) this.map.set(key, entry);
      else this.map.delete(key);
    }
    return Promise.resolve();
  }
  async load() { return []; }
}

// The frame-time row from the run's end ({state, files, error}).
export function frameTimeRow(result) {
  if (result.error) return { ok: false, detail: String(result.error).split('\n')[0] };
  const s = frameTimeSummary(new Map(result.files ?? []).get(PERF_FILE));
  if (!s.frames) return { ok: false, detail: `no frame times (${PERF_FILE})` };
  return {
    ok: s.ok,
    detail: `${s.frames} frames (${s.mode}), busy median ${s.median} ms, p95 ${s.p95} ms, max ${s.max} ms; `
      + `p95 < ${FRAME_BUDGET_MS} ms`,
  };
}

export async function frameTimeCheck(env, doc, keyboard) {
  const game = await startGame(env, doc, { keyboard, store: new MemoryStore(), run: frameTimeRun() });
  return frameTimeRow(await game.ended);
}

// A suite replay's row from the run's end ({state, files, error}) and its seconds in the page.
export async function suiteRow(entry, result, seconds, subtle = globalThis.crypto.subtle) {
  if (result.error) return { ok: false, detail: String(result.error).split('\n')[0] };
  const log = new Map(result.files ?? []).get(entry.tick_log);
  if (!log) return { ok: false, detail: `no tick log (${entry.tick_log})` };
  const { sha256, lines } = await tickLogHash(log, subtle);
  const code = result.state?.sim ?? null;
  const ok = sha256 === entry.sha256 && code === entry.exit_code;
  const node = ok ? '' : ` (Node: ${entry.lines} lines, exit code ${entry.exit_code}, SHA-256 ${entry.sha256.slice(0, 16)})`;
  return {
    ok,
    detail: `${lines} lines, exit code ${code}, SHA-256 ${sha256.slice(0, 16)}${node}; ${seconds.toFixed(1)} s (Node ${entry.node_seconds} s)`,
  };
}

// The SHA-256 of the site's es2sim.wasm, as lower-case hex.
async function wasmSha256(env) {
  const digest = await env.crypto.subtle.digest('SHA-256', await fetchBytes(env, 'es2sim.wasm'));
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
}

export async function suiteCheck(env, doc, keyboard, suites, entry) {
  if (await wasmSha256(env) !== suites.es2sim_wasm_sha256) return { ok: false, detail: text('selftest.suiteWasm') };
  const started = env.performance.now();
  const game = await startGame(env, doc, { keyboard, store: new MemoryStore(), run: suiteRun(entry) });
  const result = await game.ended;
  return suiteRow(entry, result, (env.performance.now() - started) / 1000, env.crypto.subtle);
}

// The rows of the checks with their buttons (a row per suite of `suites`, selftest_suites.json);
// one check runs at a time.
function gameChecks(env, doc, keyboard, suites) {
  const body = doc.getElementById('game-results');
  doc.getElementById('game-title').textContent = text('selftest.game');
  let busy = false;
  const checks = [
    ['audio', text('selftest.name.audio'), () => audioCheck(env)],
    ['video', text('selftest.name.video'), () => videoCheck(env, doc)],
    ['frameTime', text('selftest.name.frameTime'), () => frameTimeCheck(env, doc, keyboard)],
    ...(suites?.suites ?? []).map((entry) => [`suite:${entry.name}`, text('selftest.name.suite', { name: entry.name }),
      () => suiteCheck(env, doc, keyboard, suites, entry)]),
  ];
  const rows = checks.map(([id, label, run]) => {
    const tr = doc.createElement('tr');
    const name = doc.createElement('td');
    name.textContent = label;
    const button = doc.createElement('button');
    button.type = 'button';
    button.textContent = text('selftest.run');
    const cell = doc.createElement('td');
    cell.append(button);
    const detail = doc.createElement('td');
    tr.append(name, cell, detail);
    const done = (ok, d) => {
      tr.className = ok ? 'passed' : 'failed';
      button.textContent = text(ok ? 'selftest.passed' : 'selftest.failed');
      detail.textContent = d;
      busy = false;
    };
    const click = async () => {
      if (busy) return;
      busy = true;
      tr.className = '';
      button.textContent = text('selftest.running');
      detail.textContent = '';
      await run().then((r) => done(r.ok, r.detail), (e) => done(false, message(e)));
    };
    button.addEventListener('click', click);
    return { id, tr, click };
  });
  body.replaceChildren(...rows.map((r) => r.tr));
  return rows;
}

export async function runSelftest(env, doc) {
  doc.getElementById('title').textContent = text('selftest.title');
  const state = await ensureIsolation(env);
  const results = doc.getElementById('results');
  if (state === 'reloading') {
    results.replaceChildren(row(doc, [text('isolation.reloading')]));
    return { state, rows: [] };
  }
  const rows = [{ id: 'isolation', ok: state === 'isolated', detail: state === 'isolated' ? '' : state }];
  for (const r of await runChecks(env)) rows.push(r);
  results.replaceChildren(...rows.map((r) => row(doc, [
    text(`selftest.name.${r.id}`),
    text(r.ok ? 'selftest.passed' : 'selftest.failed'),
    r.detail,
  ], r.ok ? 'passed' : 'failed')));

  const keyboard = await chooseKeyboard(env);
  doc.getElementById('keys-title').textContent = text('selftest.keys');
  doc.getElementById('layout').textContent = `${text('selftest.layout')}: ${keyboard.name}`;
  const keys = doc.getElementById('key-rows');
  doc.addEventListener('keydown', (event) => keys.prepend(row(doc, keyReadout(keyboard, event))));
  const game = gameChecks(env, doc, keyboard, await fetchJson(env, SUITES_FILE));
  return { state, rows, keyboard, game };
}

if (typeof document !== 'undefined') runSelftest(globalThis, document);
