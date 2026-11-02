// The page's step 1 under fakes: web/sw.js (the isolation headers on same-origin responses,
// status and body kept, opaque and cross-origin responses left alone, every site file but
// navigations and requests with their own cache mode revalidated, install and activate),
// web/isolation.mjs (first visit registers and reloads once, the reloaded page is isolated, a
// second reload never happens, no service worker, a worker that fails), web/checks.mjs (every
// feature present, each one missing, Node's own globals), the page's keys in web/texts.mjs, and
// the two pages' modules app.js and selftest.mjs on a fake document (the messages, the steps
// shown only when every check passed, the key readout, the installation's lock between tabs for
// Start and the reset), and the self-test's game checks that
// need no browser: the audio check over a fake AudioContext, the video check over a fake
// MediaSource and video element, the shortest movie's path, the frame-time summary and its row
// (web/selftest_run.mjs), a suite replay's run and row, the wasm check, the rows and their buttons.
//
// Arguments: sw.js
import { AV1_CONFIGURATION, AV1_TYPE, CHECKS, failedTexts, runChecks } from '../checks.mjs';
import { RELOAD_FLAG, SERVICE_WORKER, ensureIsolation } from '../isolation.mjs';
import { TEXTS, gameLanguage, text } from '../texts.mjs';
import {
  VOLUME_TEST_SECONDS, exportEntries, installedLanguage, saveChanges, showGameFiles, showLanguage, showSaves, showStart, showVolume, startPage,
  storeOpener,
} from '../app.js';
import { VOLUME_KEY, loadVolume, saveVolume } from '../volume.mjs';
import { MUSIC_SECONDS, MemoryStore, TONE_SECONDS, audioCheck, frameTimeRow, keyReadout, runSelftest, shortestMovie, suiteCheck, suiteRow, videoCheck } from '../selftest.mjs';
import { C as SHARED_C, views } from '../shared.mjs';
import { FRAME_BUDGET_MS, PERF_FILE, SUITES_FILE, frameTimeRun, frameTimeSummary, suiteRun } from '../selftest_run.mjs';
import { tickLogHash } from '../tick_log_hash.mjs';
import { HOST_DIR } from '../game_worker.mjs';
import { Keyboard } from '../keys.mjs';
import { zipStored } from '../zip.mjs';
import { INSTALLATION_LOCK, holdInstallation } from '../store.mjs';
import { FakeDocument, FakeLocks, FakeServiceWorker, FakeStorage, fakeBrowser, loadServiceWorker, swFetch } from './page_fakes.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const SW_PATH = process.argv[2];

// --- sw.js ---
{
  const ORIGIN = 'http://127.0.0.1:8000';
  const served = [], modes = new Map();
  const fetch = async (request) => {
    served.push(request.url);
    modes.set(new URL(request.url).pathname, request.cache);
    const name = new URL(request.url).pathname;
    if (name === '/opaque') return { status: 0, type: 'opaque', headers: new Headers() };
    if (name === '/part') return new Response(Uint8Array.of(1, 2, 3), { status: 206, statusText: 'Partial Content', headers: { 'Content-Type': 'video/webm', 'Content-Range': 'bytes 0-2/9' } });
    if (name === '/missing') return new Response('no', { status: 404, headers: { 'Content-Type': 'text/plain' } });
    return new Response('<!doctype html>', { status: 200, headers: { 'Content-Type': 'text/html', 'Cross-Origin-Opener-Policy': 'unsafe-none' } });
  };
  const self = loadServiceWorker(SW_PATH, fetch, ORIGIN);
  check(['install', 'activate', 'fetch'].every((n) => self.listeners[n]?.length === 1), 'sw: three listeners');
  self.fire('install');
  check(self.skipped === 1, 'sw: install skips waiting');
  let waited = null;
  self.fire('activate', { waitUntil: (p) => { waited = p; } });
  await waited;
  check(waited !== null && self.claimed === 1, 'sw: activate claims the clients');

  const page = await swFetch(self, new Request(`${ORIGIN}/index.html`));
  check(page?.status === 200 && (await page.text()) === '<!doctype html>', 'sw: page body and status');
  check(page.headers.get('Cross-Origin-Opener-Policy') === 'same-origin', 'sw: COOP replaced');
  check(page.headers.get('Cross-Origin-Embedder-Policy') === 'require-corp', 'sw: COEP');
  check(page.headers.get('Content-Type') === 'text/html', 'sw: other headers kept');
  const part = await swFetch(self, new Request(`${ORIGIN}/part`));
  check(part.status === 206 && part.statusText === 'Partial Content' && part.headers.get('Content-Range') === 'bytes 0-2/9'
    && same([...new Uint8Array(await part.arrayBuffer())], [1, 2, 3]), 'sw: a range response unchanged');
  check(part.headers.get('Cross-Origin-Embedder-Policy') === 'require-corp', 'sw: COEP on a range response');
  const missing = await swFetch(self, new Request(`${ORIGIN}/missing`));
  check(missing.status === 404 && missing.headers.get('Cross-Origin-Opener-Policy') === 'same-origin', 'sw: a 404 with the headers');
  const opaque = await swFetch(self, new Request(`${ORIGIN}/opaque`));
  check(opaque.status === 0 && opaque.type === 'opaque' && opaque.headers.get('Cross-Origin-Embedder-Policy') === null, 'sw: opaque left alone');
  const before = served.length;
  check(await swFetch(self, new Request('https://example.org/x.js')) === null, 'sw: cross-origin left to the browser');
  check(await swFetch(self, { url: `${ORIGIN}/a.js`, cache: 'only-if-cached', mode: 'no-cors' }) === null, 'sw: only-if-cached outside same-origin');
  check(served.length === before, 'sw: no fetch for the requests it leaves');
  const cached = await swFetch(self, { url: `${ORIGIN}/a.js`, cache: 'only-if-cached', mode: 'same-origin' });
  check(cached?.headers.get('Cross-Origin-Opener-Policy') === 'same-origin', 'sw: only-if-cached in same-origin');
  await swFetch(self, new Request(`${ORIGIN}/es2/es2shell.wasm`));
  check(modes.get('/es2/es2shell.wasm') === 'no-cache', `sw: a site file revalidated (${modes.get('/es2/es2shell.wasm')})`);
  await swFetch(self, new Request(`${ORIGIN}/es2/gamefiles.zst`, { cache: 'no-store' }));
  check(modes.get('/es2/gamefiles.zst') === 'no-store', 'sw: gamefiles.zst bypasses the HTTP cache');
  await swFetch(self, new Request(`${ORIGIN}/es2/manifest.json`, { cache: 'reload' }));
  check(modes.get('/es2/manifest.json') === 'reload', 'sw: a request keeps its own cache mode');
  await swFetch(self, { url: `${ORIGIN}/es2/`, mode: 'navigate', cache: 'default' });
  check(modes.get('/es2/') === 'default', 'sw: a navigation as requested');
}

// --- isolation.mjs ---
{
  // First visit: not isolated, not controlled -> register, wait for activation, flag, reload.
  const sw = new FakeServiceWorker();
  const storage = new FakeStorage();
  const env = fakeBrowser({ isolated: false, serviceWorker: sw, sessionStorage: storage });
  check(await ensureIsolation(env) === 'reloading', 'isolation: first visit reloads');
  check(env.reloads === 1 && same(sw.registered, [SERVICE_WORKER]), 'isolation: one register, one reload');
  check(sw.registration.active?.state === 'activated' && storage.getItem(RELOAD_FLAG) === '1', 'isolation: reload after activation, flag set');
  // The reloaded page comes through the worker and is isolated: the flag goes.
  const reloaded = fakeBrowser({ isolated: true, serviceWorker: sw, sessionStorage: storage });
  check(await ensureIsolation(reloaded) === 'isolated' && reloaded.reloads === 0, 'isolation: the reloaded page is isolated');
  check(storage.getItem(RELOAD_FLAG) === null && sw.registered.length === 2, 'isolation: flag cleared, worker kept registered');
  // A reloaded page that is still not isolated fails instead of reloading again.
  storage.setItem(RELOAD_FLAG, '1');
  const stuck = fakeBrowser({ isolated: false, serviceWorker: sw, sessionStorage: storage });
  check(await ensureIsolation(stuck) === 'failed' && stuck.reloads === 0, 'isolation: no second reload');
  check(storage.getItem(RELOAD_FLAG) === null, 'isolation: the failure clears the flag for a later try');
  // A hard reload bypasses a registered worker: one reload again.
  const hard = fakeBrowser({ isolated: false, serviceWorker: sw, sessionStorage: storage });
  check(await ensureIsolation(hard) === 'reloading' && hard.reloads === 1, 'isolation: a page past the worker reloads once');
  // Already isolated by the server's headers: no reload, the worker still registers.
  const headers = fakeBrowser({ isolated: true, serviceWorker: new FakeServiceWorker() });
  check(await ensureIsolation(headers) === 'isolated' && headers.reloads === 0
    && headers.navigator.serviceWorker.registered.length === 1, 'isolation: isolated by the server');
  // No service worker (a private window), a refused registration, a worker that fails, no storage.
  const none = fakeBrowser({ isolated: false, serviceWorker: undefined });
  delete none.navigator.serviceWorker;
  check(await ensureIsolation(none) === 'no-service-worker' && none.reloads === 0, 'isolation: no service worker');
  const refused = fakeBrowser({ isolated: false, serviceWorker: new FakeServiceWorker({ refuse: true }) });
  check(await ensureIsolation(refused) === 'no-service-worker', 'isolation: register refused');
  const broken = fakeBrowser({ isolated: false, serviceWorker: new FakeServiceWorker({ fail: true }) });
  check(await ensureIsolation(broken) === 'failed' && broken.reloads === 0, 'isolation: a redundant worker');
  const blocked = fakeBrowser({ isolated: false });
  Object.defineProperty(blocked, 'sessionStorage', { get() { throw new Error('SecurityError'); } });
  check(await ensureIsolation(blocked) === 'failed' && blocked.reloads === 0, 'isolation: no reload without the flag');
  const insecure = fakeBrowser({ isolated: false });
  insecure.isSecureContext = false;
  check(await ensureIsolation(insecure) === 'insecure', 'isolation: insecure context');
}

// --- checks.mjs ---
{
  const all = await runChecks(fakeBrowser());
  check(same(all.map((r) => r.id), CHECKS.map((c) => c.id)) && all.every((r) => r.ok && r.detail === ''), 'checks: all present');
  check(same(failedTexts(all), []), 'checks: nothing failed');
  check(AV1_TYPE === 'video/webm; codecs="av01.0.08M.08"' && AV1_CONFIGURATION.type === 'media-source'
    && AV1_CONFIGURATION.video.width === 1280 && AV1_CONFIGURATION.video.height === 960, 'checks: the AV1 type and size');
  const env = fakeBrowser();
  await runChecks(env);
  check(same(env.indexedDB.opened, [['es2-probe', 1]]) && same(env.indexedDB.deleted, ['es2-probe']), 'checks: the probe database');

  const without = (id, change) => {
    const e = fakeBrowser();
    change(e);
    return runChecks(e).then((r) => same(failedTexts(r), [CHECKS.find((c) => c.id === id).text]));
  };
  check(await without('sharedArrayBuffer', (e) => { e.crossOriginIsolated = false; }), 'checks: not isolated');
  check(await without('sharedArrayBuffer', (e) => { delete e.SharedArrayBuffer; }), 'checks: no SharedArrayBuffer');
  check(await without('audioWorklet', (e) => { delete e.AudioWorkletNode; }), 'checks: no AudioWorkletNode');
  check(await without('audioWorklet', (e) => { e.AudioContext = class {}; }), 'checks: no audioWorklet');
  check(await without('webAssembly', (e) => { delete e.WebAssembly; }), 'checks: no WebAssembly');
  check(await without('webAssembly', (e) => { e.WebAssembly = { validate: () => false }; }), 'checks: WebAssembly refuses');
  check(await without('indexedDB', (e) => { delete e.indexedDB; }), 'checks: no IndexedDB');
  check(await without('indexedDB', (e) => { e.indexedDB = { open() { throw new Error('InvalidStateError'); } }; }), 'checks: open throws');
  check(await without('indexedDB', (e) => {
    e.indexedDB = { open() { const r = { error: new Error('denied') }; setTimeout(() => r.onerror(), 0); return r; } };
  }), 'checks: open fails');
  check(await without('av1', (e) => { delete e.navigator.mediaCapabilities; }), 'checks: no MediaCapabilities');
  check(await without('av1', (e) => { e.navigator.mediaCapabilities.decodingInfo = async () => ({ supported: false }); }), 'checks: no AV1 decoder');
  check(await without('av1', (e) => { e.navigator.mediaCapabilities.decodingInfo = async () => { throw new TypeError('bad'); }; }), 'checks: decodingInfo throws');
  check(await without('mseAv1', (e) => { e.MediaSource = { isTypeSupported: (t) => t.includes('opus') }; }), 'checks: no WebM AV1');
  check(await without('mseOpus', (e) => { e.MediaSource = { isTypeSupported: (t) => t.includes('av01') }; }), 'checks: no WebM Opus');
  const noMse = await runChecks({ ...fakeBrowser(), MediaSource: undefined });
  check(same(failedTexts(noMse), ['check.mseAv1', 'check.mseOpus']), 'checks: no MediaSource');
  const managed = fakeBrowser();
  managed.ManagedMediaSource = managed.MediaSource;
  delete managed.MediaSource;
  check(same(failedTexts(await runChecks(managed)), []), 'checks: ManagedMediaSource');
  // Node itself: WebAssembly and SharedArrayBuffer exist, isolation and the media and audio APIs do not.
  const node = await runChecks(globalThis);
  const ok = Object.fromEntries(node.map((r) => [r.id, r.ok]));
  check(ok.webAssembly === true && ok.sharedArrayBuffer === false && ok.audioWorklet === false && ok.av1 === false
    && ok.mseAv1 === false && ok.mseOpus === false, `checks: Node's globals ${JSON.stringify(ok)}`);
  check(node.find((r) => r.id === 'sharedArrayBuffer').detail === 'not cross-origin isolated', 'checks: Node has SharedArrayBuffer');
}

// --- texts.mjs ---
{
  check(gameLanguage(['de-DE', 'en']) === 'G' && gameLanguage(['fr-CA']) === 'F' && gameLanguage(['en-GB']) === 'E'
    && gameLanguage(['it']) === 'E' && gameLanguage([]) === 'E' && gameLanguage('de') === 'G', 'texts: game language');
  const keys = [...CHECKS.map((c) => c.text), 'isolation.reloading', 'isolation.insecure', 'isolation.no-service-worker',
    'isolation.failed', 'checks.failed', ...CHECKS.map((c) => `selftest.name.${c.id}`), 'selftest.name.isolation'];
  check(keys.every((k) => Object.hasOwn(TEXTS, k) && TEXTS[k].length > 0), 'texts: every key');
  check(text('check.webAssembly') === TEXTS['check.webAssembly'] && text('no.such') === 'no.such', 'texts: fallbacks');
}

// --- app.js ---
{
  const doc = new FakeDocument();
  const page = await startPage(fakeBrowser({ languages: ['de-DE'] }), doc);
  check(page.ready && page.state === 'isolated' && !('language' in page), 'page: ready');
  check(doc.ids.steps.hidden === false && doc.ids.checks.children.length === 0, 'page: steps shown, no message');

  const first = new FakeDocument();
  const firstEnv = fakeBrowser({ isolated: false });
  const reloading = await startPage(firstEnv, first);
  check(!reloading.ready && reloading.state === 'reloading' && firstEnv.reloads === 1
    && first.ids.checks.text() === TEXTS['isolation.reloading'] && first.ids.steps.hidden, 'page: first visit');

  const privateWindow = new FakeDocument();
  const pw = fakeBrowser({ isolated: false });
  delete pw.navigator.serviceWorker;
  delete pw.AudioWorkletNode;
  const refused = await startPage(pw, privateWindow);
  check(!refused.ready && privateWindow.ids.steps.hidden && privateWindow.ids.checks.text() === [
    TEXTS['isolation.no-service-worker'], TEXTS['check.audioWorklet'], TEXTS['checks.failed']].join('|'),
  `page: private window ${privateWindow.ids.checks.text()}`);

  const old = new FakeDocument();
  const oldEnv = fakeBrowser({ supported: ['audio/webm; codecs="opus"'] });
  const missing = await startPage(oldEnv, old);
  check(!missing.ready && old.ids.steps.hidden && old.ids.checks.text() === [
    TEXTS['check.av1'], TEXTS['check.mseAv1'], TEXTS['checks.failed']].join('|'), 'page: no AV1');
}

// --- volume.mjs and app.js: the volume ---
{
  const storage = new FakeStorage();
  check(loadVolume(storage) === 1 && loadVolume(undefined) === 1, 'volume: full without a stored value');
  saveVolume(storage, 0.4);
  check(loadVolume(storage) === 0.4, 'volume: the stored value');
  for (const bad of ['x', '-0.1', '1.5', 'NaN']) {
    storage.setItem(VOLUME_KEY, bad);
    check(loadVolume(storage) === 1, `volume: ${bad} gives full`);
  }
  const throwing = { getItem() { throw new Error('denied'); }, setItem() { throw new Error('denied'); } };
  saveVolume(throwing, 0.5);
  check(loadVolume(throwing) === 1, 'volume: a storage that throws');

  const TOC = { frames_per_second: 75, tracks: [{ number: 1, type: 'data', start: 0 }, { number: 2, type: 'audio', start: 100, length: 750, file: 'track02.opus' }] };
  const volumeEnv = (toc) => {
    const log = { contexts: [], fetched: [] };
    class AudioContext {
      constructor() { this.currentTime = 2; this.destination = { node: 'destination' }; this.closed = 0; log.contexts.push(this); }
      createGain() { this.gainNode = { gain: { value: 1 }, connect: (to) => { this.gainNode.to = to; } }; return this.gainNode; }
      async decodeAudioData(bytes) { return { bytes: bytes.byteLength }; }
      source(kind) {
        const s = { kind, frequency: { value: 0 }, connect: (to) => { s.to = to; }, start: (at) => { s.started = at; },
          stop: (at) => { s.stopped = at; this.stop = () => s.onended?.(); } };
        this.played = s;
        return s;
      }
      createBufferSource() { return this.source('buffer'); }
      createOscillator() { return this.source('oscillator'); }
      close() { this.closed++; return Promise.resolve(); }
    }
    const fetch = async (url) => {
      log.fetched.push(url);
      if (url === 'cd/toc.json' && toc) return { ok: true, json: async () => toc };
      if (url === 'cd/track02.opus') return { ok: true, arrayBuffer: async () => new ArrayBuffer(12) };
      return { ok: false, status: 404 };
    };
    const env = fakeBrowser({ languages: ['en'] });
    Object.assign(env, { AudioContext, fetch, localStorage: new FakeStorage() });
    return { env, log };
  };
  const tick = async () => { for (let i = 0; i < 10; i++) await new Promise((resolve) => setTimeout(resolve, 0)); };
  {
    const { env, log } = volumeEnv(TOC);
    env.localStorage.setItem(VOLUME_KEY, '0.3');
    const doc = new FakeDocument();
    const v = showVolume(env, doc);
    check(!doc.ids.volume.hidden && v.slider.type === 'range' && v.slider.value === '30' && v.slider.min === '0' && v.slider.max === '100'
      && doc.ids.volume.text().startsWith(`${TEXTS['volume.title']}|?||${text('volume.value', { percent: '30' })}|${TEXTS['volume.test']}`),
    `volume: the slider at the stored value (${doc.ids.volume.text()})`);
    check(doc.ids.volume.children[0].children[1].title === TEXTS['volume.tip'], 'volume: the tooltip at the label');
    v.test.fire('click');
    await tick();
    const ctx = log.contexts[0];
    check(v.test.disabled && ctx.played.kind === 'buffer' && ctx.played.buffer.bytes === 12 && ctx.played.to === ctx.gainNode
      && ctx.gainNode.to === ctx.destination && ctx.gainNode.gain.value === 0.3 && ctx.played.started === 0
      && ctx.played.stopped === 2 + VOLUME_TEST_SECONDS, 'volume: the test plays the first audio track through the gain');
    v.slider.value = '80';
    v.slider.fire('input');
    check(ctx.gainNode.gain.value === 0.8 && env.localStorage.getItem(VOLUME_KEY) === '0.8'
      && doc.ids.volume.text().includes(text('volume.value', { percent: '80' })), 'volume: the slider moves the playing test and is kept');
    ctx.stop();
    await tick();
    check(!v.test.disabled && ctx.closed === 1 && v.status.textContent === '', 'volume: the test ends and closes its context');
    v.slider.value = '10';
    v.slider.fire('input');
    check(ctx.gainNode.gain.value === 0.8, 'volume: an ended test is left alone');
  }
  {
    const { env, log } = volumeEnv(null);
    const v = showVolume(env, new FakeDocument());
    check(v.slider.value === '100', 'volume: full by default');
    v.test.fire('click');
    await tick();
    const ctx = log.contexts[0];
    check(ctx.played.kind === 'oscillator' && ctx.played.frequency.value === 440 && ctx.played.to === ctx.gainNode, 'volume: a tone without CD audio');
    ctx.stop();
    await tick();
  }
  {
    const { env, log } = volumeEnv({ tracks: [{ type: 'audio', file: 'track09.opus' }] });
    const v = showVolume(env, new FakeDocument());
    v.test.fire('click');
    await tick();
    check(!v.test.disabled && log.contexts[0].closed === 1
      && v.status.textContent === text('page.failed', { error: 'cd/track09.opus: HTTP 404' }), 'volume: a failed fetch is named');
  }
}

// --- app.js: the language step ---
{
  const cfg = 'C:\\ES2\\DATA\\LANGUAGE.CFG';
  const installed = [[cfg, { kind: 'bytes', name: 'C:\\ES2\\DATA\\language.cfg', data: Uint8Array.of(0x46) }]];
  check(installedLanguage([]) === null && installedLanguage(installed) === 'F'
    && installedLanguage([[cfg, { kind: 'bytes', name: cfg, data: new Uint8Array(0) }]]) === null
    && installedLanguage([[cfg, { kind: 'dir', name: cfg }]]) === null, 'language: the installation\'s letter from LANGUAGE.CFG');

  let overlay = [];
  const store = { load: async () => overlay };
  const doc = new FakeDocument();
  const step = showLanguage(fakeBrowser({ languages: ['fr-FR', 'de'] }), doc, async () => store);
  const section = doc.ids.language;
  const pressed = () => step.flags.map((f) => f.attributes['aria-pressed']);
  const gated = new FakeDocument();
  const gatedButton = showStart(fakeBrowser({ languages: ['de-DE'] }), gated, () => { throw new Error('no start'); },
    { openStore: () => Promise.resolve(store), languageStep: step });
  check(gatedButton.disabled && gatedButton.title === TEXTS['start.language'] && !step.ready(), 'language: Start waits for a flag');
  check(section.hidden && step.choice() === 'F' && same(step.flags.map((f) => f.value), ['G', 'E', 'F'])
    && same(pressed(), ['false', 'false', 'false']) && same(step.flags.map((f) => f.className), ['flag flag-G', 'flag flag-E', 'flag flag-F'])
    && same(step.flags.map((f) => [f.title, f.attributes['aria-label']]), ['G', 'E', 'F'].map((c) => [TEXTS[`lang.${c}`], TEXTS[`lang.${c}`]]))
    && section.text() === [TEXTS['lang.title'], '?', '', '', ''].join('|') && section.children[0].children[1].title === TEXTS['lang.tip'],
  `language: three flags, none pressed, French as the default choice (${section.text()})`);
  check(await step.refresh() === null && !section.hidden, 'language: shown while the installation has none');
  step.flags[0].fire('click');
  check(step.choice() === 'G' && same(pressed(), ['true', 'false', 'false']) && step.ready(), 'language: the clicked flag counts');
  check(!gatedButton.disabled && gatedButton.title === TEXTS['start.button'], 'language: the click enables Start');
  overlay = installed;
  check(await step.refresh() === 'F' && !section.hidden && step.ready() && !gatedButton.disabled && step.choice() === 'F'
    && same(pressed(), ['false', 'false', 'true']) && step.flags.every((f) => f.disabled),
  "language: the installation's language shown pressed and locked, Start enabled");
  overlay = [];
  check(await step.refresh() === null && !step.ready() && gatedButton.disabled && same(pressed(), ['false', 'false', 'false'])
    && step.flags.every((f) => !f.disabled) && step.choice() === 'G',
  'language: after a reset the flags unlock and Start waits for a new click');
  const brokenDoc = new FakeDocument();
  const broken = showLanguage(fakeBrowser({ languages: ['it'] }), brokenDoc, async () => { throw new Error('no db'); });
  check(broken.choice() === 'E' && await broken.refresh() === null && !brokenDoc.ids.language.hidden
    && broken.flags.every((f) => !f.disabled), 'language: English otherwise; a store error leaves the flags enabled');

  // Start passes the choice and looks at the installation after the game.
  const env = fakeBrowser({ languages: ['de-DE'] });
  env.localStorage = new FakeStorage();
  const calls = [];
  const refreshed = [];
  const languageStep = { choice: () => 'E', refresh: async () => { refreshed.push(calls.length); return 'E'; }, ready: () => true, onChange: () => {} };
  const button = showStart(env, new FakeDocument(), (e, d, options) => {
    calls.push(options);
    return { ended: Promise.resolve({ state: 0 }) };
  }, { openStore: () => Promise.resolve(store), languageStep });
  button.fire('click');
  for (let i = 0; i < 8; i++) await Promise.resolve();
  check(calls.length === 1 && calls[0].language === 'E' && same(refreshed, [1]) && !button.disabled,
    'language: Start passes the choice and refreshes the step after the game');
}

// --- app.js: the game files ---
{
  const doc = new FakeDocument();
  const section = doc.ids.gamefiles;
  let ready = false;
  const listeners = [];
  const languageStep = { ready: () => ready, onChange: (fn) => listeners.push(fn), choice: () => 'G', refresh: async () => 'G' };
  const changed = () => { for (const fn of listeners) fn(); };
  const loads = [];
  const files = { load: (onProgress) => new Promise((resolve, reject) => loads.push({ onProgress, resolve, reject })) };
  const step = showGameFiles(fakeBrowser({ languages: ['en'] }), doc, languageStep, files);
  const ticks = async () => { for (let i = 0; i < 6; i++) await Promise.resolve(); };
  const button = showStart(fakeBrowser({ languages: ['en'] }), new FakeDocument(), () => { throw new Error('no start'); },
    { openStore: () => Promise.resolve({}), languageStep, gameFiles: step });
  changed();
  check(section.hidden && loads.length === 0 && !step.ready() && button.title === TEXTS['start.language'],
    'files: nothing loads before the language');
  ready = true;
  changed();
  check(loads.length === 1 && !section.hidden && step.status.textContent === TEXTS['files.starting']
    && button.disabled && button.title === TEXTS['start.files'], 'files: the language starts the load, Start waits for it');
  changed();
  check(loads.length === 1, 'files: one load at a time');
  loads[0].onProgress(5e6, 20e6);
  check(step.bar.value === 0.25 && step.status.textContent === text('files.loading', { received: '5.0', size: '20.0', percent: 25 }),
    `files: the progress (${step.status.textContent})`);
  loads[0].reject(new Error('HTTP 503'));
  await ticks();
  check(!step.ready() && step.bar.hidden && !step.retry.hidden && step.status.textContent === text('files.failed', { error: 'HTTP 503' })
    && button.disabled, 'files: a failed load names its error and offers a retry');
  step.retry.fire('click');
  check(loads.length === 2 && step.retry.hidden && !step.bar.hidden, 'files: the retry loads again');
  const loaded = { manifest: { files: [] }, gamefiles: new Uint8Array(1500000) };
  loads[1].resolve(loaded);
  await ticks();
  check(step.ready() && step.loaded() === loaded && step.bar.value === 1 && step.status.textContent === text('files.ready', { size: '1.5' })
    && !button.disabled && button.title === TEXTS['start.button'], 'files: loaded, Start enabled');
  changed();
  check(loads.length === 2, 'files: loaded once');
  step.drop();
  check(!step.ready() && section.hidden && button.disabled, 'files: drop forgets the files');
  ready = false;
  changed();
  check(loads.length === 2, 'files: after a reset nothing loads before the language');

  // A load a drop overtook leaves the step alone.
  ready = true;
  changed();
  step.drop();
  loads[2].resolve(loaded);
  await ticks();
  check(!step.ready() && section.hidden, 'files: an overtaken load is ignored');

  // Start passes the loaded files.
  const env = fakeBrowser({ languages: ['en'] });
  env.localStorage = new FakeStorage();
  const calls = [];
  const loadedStep = { ready: () => true, loaded: () => loaded, onChange: () => {} };
  const start = showStart(env, new FakeDocument(), (e, d, options) => { calls.push(options); return { ended: Promise.resolve({ state: 0 }) }; },
    { openStore: () => Promise.resolve({}), gameFiles: loadedStep });
  start.fire('click');
  await ticks();
  check(calls.length === 1 && calls[0].files === loaded, 'files: Start passes them to the game');
}

// --- app.js: the start step ---
{
  const doc = new FakeDocument();
  const env = fakeBrowser({ languages: ['de-DE'] });
  env.navigator.language = 'de-DE';
  const opened = [];
  env.indexedDB = { open: (name) => { opened.push(name); return {}; } };
  env.localStorage = new FakeStorage();
  env.localStorage.setItem(VOLUME_KEY, '0.25');
  const calls = [];
  let finish;
  const start = (e, d, options) => {
    calls.push(options);
    return { ended: new Promise((resolve) => { finish = resolve; }) };
  };
  const button = showStart(env, doc, start);
  check(!doc.ids.start.hidden && button.textContent === '' && button.attributes['aria-label'] === TEXTS['start.button'] && button.className === 'start'
    && doc.ids.start.children.length === 2, 'start: the button and its status');
  button.fire('click');
  await Promise.resolve();
  check(calls.length === 1 && calls[0].language === 'G' && calls[0].keyboard.name === 'de' && opened[0] === 'es2'
    && calls[0].volume === 0.25 && button.disabled, 'start: the click starts the game over the es2 store with the de layout and the volume');
  finish({ state: 0 });
  for (let i = 0; i < 5; i++) await Promise.resolve();
  check(!button.disabled && doc.ids.start.text().endsWith('|'), 'start: the end brings the step back');
  button.fire('click');
  await Promise.resolve();
  finish({ state: null, error: 'trap' });
  for (let i = 0; i < 5; i++) await Promise.resolve();
  check(calls.length === 2 && opened.length === 1 && doc.ids.start.text().endsWith(text('game.failed', { error: 'trap' })),
    'start: a second start reuses the store and names the error');
}

// --- app.js: Start holds the installation's lock ---
{
  check(typeof await holdInstallation(undefined) === 'function'
    && typeof await holdInstallation({ request: () => Promise.reject(new Error('SecurityError')) }) === 'function',
    'lock: granted without a LockManager or when it refuses');
  const locks = new FakeLocks();
  const other = await holdInstallation(locks);
  check(typeof other === 'function' && await holdInstallation(locks) === null, 'lock: null while another tab holds it');
  const env = fakeBrowser({ languages: ['en'] });
  env.navigator.locks = locks;
  env.localStorage = new FakeStorage();
  const doc = new FakeDocument();
  const calls = [];
  let finish;
  const button = showStart(env, doc, (e, d, options) => {
    calls.push(options);
    return { ended: new Promise((resolve) => { finish = resolve; }) };
  }, { openStore: () => Promise.resolve({}) });
  const ticks = async () => { for (let i = 0; i < 10; i++) await Promise.resolve(); };
  button.fire('click');
  await ticks();
  check(calls.length === 0 && !button.disabled && doc.ids.start.text().endsWith(TEXTS['start.busy']),
    'lock: no start while a game in another tab holds it');
  other();
  await ticks();
  button.fire('click');
  await ticks();
  check(calls.length === 1 && locks.held.has(INSTALLATION_LOCK) && await holdInstallation(locks) === null,
    'lock: the game holds it');
  finish({ state: 0 });
  await ticks();
  check(!locks.held.has(INSTALLATION_LOCK) && doc.ids.start.text().endsWith('|'), 'lock: released after the game');
}

// --- app.js: the saves step ---
{
  const SAV = 'C:\\ES2\\SAV\\';
  const b = (...x) => Uint8Array.of(...x);
  const changes = saveChanges([[`${SAV}gamefile.str`, b(1)], [`${SAV}GAME_0.SAV`, b(2)], [`${SAV}GAME_1.SAV`, b(3)]],
    [[`${SAV}GAME_0.SAV`, { kind: 'bytes', name: `${SAV}game_0.sav`, data: b(9) }],
      [`${SAV}GAME_1.SAV`, { kind: 'tombstone', name: `${SAV}game_1.sav` }]]);
  check(same(changes, [
    [`${SAV}GAMEFILE.STR`, { kind: 'bytes', name: `${SAV}GAMEFILE.STR`, data: b(1) }],
    [`${SAV}GAME_0.SAV`, { kind: 'bytes', name: `${SAV}game_0.sav`, data: b(2) }],
    [`${SAV}GAME_1.SAV`, { kind: 'bytes', name: `${SAV}GAME_1.SAV`, data: b(3) }],
  ]), 'saves: an upload keeps the names of the base and the overlay, not of a deleted file');
  const overlay = [
    [`${SAV}GAME_0.SAV`, { kind: 'bytes', name: `${SAV}game_0.sav`, data: b(1) }],
    [`${SAV}GAMEFILE.STR`, { kind: 'bytes', name: `${SAV}GAMEFILE.STR`, data: b(2) }],
    [`${SAV}GAME_1.SAV`, { kind: 'tombstone', name: `${SAV}GAME_1.SAV` }],
    [`${SAV}OLD`, { kind: 'dir', name: `${SAV}old` }],
    [`${SAV}OLD\\GAME_2.SAV`, { kind: 'bytes', name: `${SAV}old\\GAME_2.SAV`, data: b(3) }],
    ['C:\\ES2\\DATA\\SCRIPT.DAT', { kind: 'bytes', name: 'C:\\ES2\\DATA\\script.dat', data: b(4) }],
  ];
  check(same(exportEntries(overlay), [{ name: 'GAMEFILE.STR', bytes: b(2) }, { name: 'game_0.sav', bytes: b(1) }]),
    'export: the overlay\'s files directly in C:\\ES2\\SAV\\');

  // tools.js under a fake: names ending in .SAV are saves; a first byte 0xff is truncated (2).
  const tools = async () => async () => {
    const m = { HEAPU8: new Uint8Array(1 << 16), top: 8 };
    m.HEAPU32 = new Uint32Array(m.HEAPU8.buffer);
    m._malloc = (n) => { const p = m.top; m.top += (n + 7) & ~7; return p; };
    m._free = () => {};
    const str = (p) => { let s = ''; while (m.HEAPU8[p]) s += String.fromCharCode(m.HEAPU8[p++]); return s; };
    m._es2t_kind = (p) => (/\.SAV$/i.test(str(p)) ? 1 : 0);
    m._es2t_check = (p, data, length, offset) => {
      if (!/\.SAV$/i.test(str(p))) return -1;
      if (length && m.HEAPU8[data] === 0xff) { m.HEAPU32[offset >>> 2] = 0; return 2; }
      return 0;
    };
    return m;
  };
  const savesEnv = (stored = []) => {
    const log = { applied: [], cleared: 0, cachesDeleted: [], urls: [], revoked: [] };
    const store = { load: async () => stored, applyAll: async (c) => { log.applied.push(c); }, clear: async () => { log.cleared++; } };
    const env = fakeBrowser({ languages: ['en'] });
    env.caches = { delete: async (name) => { log.cachesDeleted.push(name); return true; }, open: async () => ({}) };
    env.Blob = class { constructor(parts, o) { this.parts = parts; this.type = o.type; } };
    env.URL = { createObjectURL: (blob) => { log.urls.push(blob); return 'blob:1'; }, revokeObjectURL: (u) => log.revoked.push(u) };
    env.setTimeout = (fn) => fn();
    return { env, store, log };
  };
  {
    const doc = new FakeDocument();
    const { env, store, log } = savesEnv([[`${SAV}GAME_0.SAV`, { kind: 'bytes', name: `${SAV}game_0.sav`, data: b(1) }]]);
    const s = showSaves(env, doc, async () => store, { loadTools: tools, onReset: async () => { log.resets = (log.resets ?? 0) + 1; } });
    const actions = doc.ids.saves.children[1].children.slice(0, 3);
    const info = doc.ids.saves.children[0].children[1];
    check(info.title === TEXTS['saves.details'] && doc.ids.saves.children[1].children[3] === s.confirm && s.confirm.className === 'bubble',
      'saves: the details as a tooltip, the confirmation as a bubble at the reset button');
    const zipInput = doc.ids.saves.children[2];
    check(!doc.ids.saves.hidden && same(actions.map((x) => x.textContent), [TEXTS['saves.upload'],
      TEXTS['export.button'], TEXTS['reset.button']]) && zipInput.type === 'file' && !zipInput.multiple
      && zipInput.accept === '.zip,application/zip', 'saves: the step with its ZIP upload, export and reset buttons');
    const upload = (name, entries) => s.upload({ name, bytes: zipStored(entries) });
    await upload('saves.zip', [{ name: 'SAV/GAME_0.SAV', bytes: b(5) }, { name: 'readme.txt', bytes: b(6) }]);
    check(log.applied.length === 1 && same(log.applied[0], [[`${SAV}GAME_0.SAV`, { kind: 'bytes', name: `${SAV}game_0.sav`, data: b(5) }]])
      && s.messages.text() === [text('saves.ignored', { name: 'readme.txt' }), text('saves.written', { count: 1 }),
        TEXTS['saves.language_note']].join('|'), `saves: an upload in one transaction (${s.messages.text()})`);
    await upload('saves.zip', [{ name: 'GAME_1.SAV', bytes: b(0xff) }, { name: 'GAME_2.SAV', bytes: b(7) }]);
    check(log.applied.length === 1 && s.messages.text().endsWith(TEXTS['saves.nothing_written']), 'saves: a failing file writes nothing');
    await s.upload({ name: 'GAME_0.SAV', bytes: b(1) });
    check(log.applied.length === 1 && s.messages.text() === [text('saves.zip_invalid', { name: 'GAME_0.SAV',
      reason: TEXTS['saves.zip.format'] }), TEXTS['saves.nothing_written']].join('|'), `saves: only a ZIP is taken (${s.messages.text()})`);
    const damaged = zipStored([{ name: 'GAME_3.SAV', bytes: b(8) }]);
    damaged[30 + 'GAME_3.SAV'.length] ^= 1;
    await s.upload({ name: 'damaged.zip', bytes: damaged });
    check(log.applied.length === 1 && s.messages.text().startsWith(text('saves.zip_invalid', { name: 'damaged.zip',
      reason: TEXTS['saves.zip.crc'] })), 'saves: a ZIP entry failing its CRC writes nothing');
    await s.download();
    const a = doc.body.children[0];
    const zip = log.urls[0]?.parts[0];
    check(a && a.download === TEXTS['export.filename'] && a.href === 'blob:1' && a.clicked === 1 && a.removed
      && log.urls[0].type === 'application/zip' && zip instanceof Uint8Array && zip[0] === 0x50 && zip[1] === 0x4b
      && same(log.revoked, ['blob:1']), 'export: a ZIP download');
    actions[2].fire('click');
    check(!s.confirm.hidden && log.cleared === 0, 'reset: the button asks first');
    s.confirm.children[2].fire('click');
    check(s.confirm.hidden && log.cleared === 0, 'reset: cancel keeps everything');
    actions[2].fire('click');
    doc.fire('keydown', { key: 'Escape' });
    actions[2].fire('click');
    actions[2].fire('click');
    check(s.confirm.hidden && log.cleared === 0, 'reset: Escape and a second click close the bubble');
    actions[2].fire('click');
    s.confirm.children[1].fire('click');
    for (let i = 0; i < 5; i++) await Promise.resolve();
    check(log.cleared === 1 && same(log.cachesDeleted, ['es2-gamefiles']) && s.messages.text() === TEXTS['reset.done'] && log.resets === 1,
      'reset: the overlay and the part cache, then the language step');
    env.navigator.locks = new FakeLocks();
    const game = await holdInstallation(env.navigator.locks);
    await s.reset();
    check(log.cleared === 1 && log.resets === 1 && s.messages.text() === TEXTS['reset.busy'],
      'reset: nothing while a game in another tab holds the lock');
    game();
    for (let i = 0; i < 5; i++) await Promise.resolve();
    await s.reset();
    check(log.cleared === 2 && log.resets === 2 && !env.navigator.locks.held.has(INSTALLATION_LOCK),
      'reset: after the game, and the lock is free again');
  }
  {
    const doc = new FakeDocument();
    const { env, store } = savesEnv([]);
    const s = showSaves(env, doc, async () => store, { loadTools: tools });
    await s.download();
    check(doc.body.children.length === 0 && s.messages.text() === TEXTS['export.empty'], 'export: nothing to export');
    const broken = showSaves(env, new FakeDocument(), async () => { throw new Error('no db'); }, { loadTools: tools });
    await broken.upload({ name: 'saves.zip', bytes: zipStored([{ name: 'GAME_0.SAV', bytes: b(1) }]) });
    check(broken.messages.text().endsWith(text('page.failed', { error: 'no db' })), 'saves: a store error is named');
  }
  {
    let opens = 0;
    const env = {
      indexedDB: {
        open: () => {
          const r = { result: { objectStoreNames: { contains: () => true } } };
          const fail = opens++ === 0;
          setTimeout(() => (fail ? r.onerror() : r.onsuccess()), 0);
          if (fail) r.error = new Error('busy');
          return r;
        },
      },
    };
    const open = storeOpener(env);
    let first = null;
    try { await open(); } catch (e) { first = e.message; }
    const second = await open();
    check(first === 'busy' && opens === 2 && second && (await open()) === second, 'store: a failed open is tried again, a good one kept');
  }
}

// --- selftest.mjs ---
{
  const doc = new FakeDocument();
  const env = fakeBrowser({ languages: ['de-DE'] });
  const result = await runSelftest(env, doc);
  check(result.state === 'isolated' && result.rows.length === CHECKS.length + 1 && result.rows.every((r) => r.ok), 'selftest: all passed');
  check(doc.ids.results.children.length === CHECKS.length + 1 && doc.ids.results.children.every((r) => r.className === 'passed'), 'selftest: rows');
  check(doc.ids.results.children[0].text() === `${TEXTS['selftest.name.isolation']}|${TEXTS['selftest.passed']}|`, 'selftest: isolation row');
  check(result.keyboard.name === 'de' && doc.ids.layout.text().endsWith(': de'), 'selftest: layout of the language');
  const press = (code, key, keyCode, numlock = false) =>
    doc.fire('keydown', { code, key, keyCode, getModifierState: (m) => m === 'NumLock' && numlock });
  press('KeyZ', 'z', 90);
  press('Numpad7', '7', 103, true);
  press('Numpad7', 'Home', 36, false);
  press('ArrowUp', 'ArrowUp', 38);
  check(same(doc.ids['key-rows'].children.map((r) => r.text()), [
    'ArrowUp|ArrowUp|0x148|0x26', 'Numpad7|Home|0x047|0x24', 'Numpad7|7|0x047|0x67', 'KeyZ|z|0x02c|0x5a']), 'selftest: key rows, newest first');

  const us = new Keyboard('us');
  check(same(keyReadout(us, { code: 'KeyY', key: 'y', keyCode: 89 }), ['KeyY', 'y', '0x015', '0x59']), 'selftest: us readout');
  check(same(keyReadout(us, { code: 'Lang1', key: 'x', keyCode: 0 }), ['Lang1', 'x', '-', '0x00']), 'selftest: an unknown code');
  const chrome = fakeBrowser();
  chrome.navigator.keyboard = { getLayoutMap: async () => new Map([['KeyY', 'z'], ['KeyZ', 'y'], ['Semicolon', 'ö']]) };
  const chosen = await runSelftest(chrome, new FakeDocument());
  check(chosen.keyboard.name === 'de', 'selftest: layout from the layout map');

  const failing = new FakeDocument();
  const bad = fakeBrowser({ isolated: false, serviceWorker: new FakeServiceWorker({ fail: true }) });
  const failed = await runSelftest(bad, failing);
  check(failed.rows[0].ok === false && failed.rows[0].detail === 'failed'
    && failing.ids.results.children[0].className === 'failed', 'selftest: isolation failed');
}

// --- the self-test's game checks ---
{
  const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
  const TOC = { frames_per_second: 75, tracks: [{ number: 1, type: 'data', start: 0, length: 100 },
    { number: 2, type: 'audio', start: 100, length: 750, file: 'track02.opus' }] };
  const audioEnv = ({ toc = TOC, duration = 10.03, addModule = async () => {} } = {}) => {
    const log = { modules: [], nodes: [], decoded: [], closed: 0, rates: [], played: [], waits: [], toneFrames: 0 };
    class AudioContext {
      constructor(o = {}) {
        this.sampleRate = o.sampleRate ?? 48000;
        log.rates.push(this.sampleRate);
        this.destination = {};
        this.audioWorklet = { addModule: async (u) => { log.modules.push(u); await addModule(u); } };
      }
      async decodeAudioData(bytes) { log.decoded.push(bytes.byteLength); return { duration }; }
      createBufferSource() {
        const rate = this.sampleRate;
        const source = { connect: () => {}, start: (when, offset, seconds) => log.played.push([rate, source.buffer.duration, offset, seconds]) };
        return source;
      }
      close() { log.closed++; return Promise.resolve(); }
    }
    class AudioWorkletNode {
      constructor(ctx, name, options) {
        log.nodes.push([ctx.sampleRate, name, options.outputChannelCount, options.processorOptions.sab instanceof SharedArrayBuffer]);
        log.sab = options.processorOptions.sab;
      }
      connect() {}
    }
    const fetch = async (url) => {
      if (url === 'cd/toc.json' && toc) return { ok: true, json: async () => toc };
      if (url === 'cd/track02.opus') return { ok: true, arrayBuffer: async () => new ArrayBuffer(12) };
      return { ok: false, status: 404 };
    };
    const setTimeout = (fn, ms) => { log.waits.push(ms); return globalThis.setTimeout(fn, 0); };
    return { env: { AudioContext, AudioWorkletNode, fetch, setTimeout }, log };
  };
  {
    const { env, log } = audioEnv();
    const r = await audioCheck(env);
    check(r.ok && same(log.modules, ['audio.js']) && same(log.nodes, [[22050, 'es2-audio', [2], true]])
      && same(log.rates, [22050, 48000]) && same(log.decoded, [12]) && log.closed === 2
      && r.detail === '22050 Hz worklet, music 48000 Hz; track02.opus 10.03 s (TOC 10.00 s)', `audio: worklet and track (${r.detail})`);
    const ring = views(log.sab);
    const written = Atomics.load(ring.control, SHARED_C.SOUND_WRITE);
    const peak = ring.shellSound.subarray(0, written * 2).reduce((m, x) => Math.max(m, Math.abs(x)), 0);
    check(written === Math.round(TONE_SECONDS * 22050) && peak > 0.2 && peak <= 0.25 && ring.shellSound[0] === 0,
      `audio: a faded tone in the shell sound ring (${written} frames, peak ${peak})`);
    check(same(log.played, [[48000, 10.03, 0, MUSIC_SECONDS]]) && log.waits.includes(MUSIC_SECONDS * 1000),
      `audio: the track's first seconds play (${JSON.stringify(log.played)})`);
  }
  {
    const { env } = audioEnv({ duration: 10.2 });
    check(!(await audioCheck(env)).ok, 'audio: a track 0.2 s off its TOC length fails');
    const none = audioEnv({ toc: null });
    const r = await audioCheck(none.env);
    check(r.ok && r.detail.endsWith(TEXTS['selftest.noCdAudio']) && none.log.decoded.length === 0, 'audio: without CD music the worklet alone');
    const broken = audioEnv({ addModule: async () => { throw new Error('no worklet'); } });
    let thrown = null;
    try { await audioCheck(broken.env); } catch (e) { thrown = e.message; }
    check(thrown === 'no worklet' && broken.log.closed === 2, 'audio: a failing worklet fails the row, both contexts closed');
  }

  const MEDIA = {
    intr_pt1: { video: 'intr_pt1.webm', audio: 'intr_pt1.audio.webm', width: 320, height: 240, frames: 900 },
    'avf/end1': { video: 'avf/end1.webm', audio: null, width: 320, height: 200, frames: 30 },
    rest: { video: 'rest.webm', audio: null, width: 320, height: 200, frames: 0 },
  };
  check(same(shortestMovie(MEDIA), { name: 'avf/end1', entry: MEDIA['avf/end1'], path: 'D:\\avf\\end1.avi' })
    && shortestMovie({ intr_pt1: MEDIA.intr_pt1 }).path === 'D:\\avi\\intr_pt1.avi' && shortestMovie({}) === null,
  'video: the movie with the fewest frames under its CD path');
  const videoEnv = ({ media = MEDIA, end = 'ended' } = {}) => {
    const log = { fetched: [], appended: [], attached: 0, removed: 0, ended: 0 };
    const listeners = (o) => {
      o.l = {};
      o.addEventListener = (n, fn) => { (o.l[n] ??= []).push(fn); };
      o.removeEventListener = (n, fn) => { o.l[n] = (o.l[n] ?? []).filter((f) => f !== fn); };
      o.fire = (n) => { for (const fn of [...(o.l[n] ?? [])]) fn(); };
      return o;
    };
    class MediaSource {
      constructor() { listeners(this); this.readyState = 'closed'; setTimeout(() => { this.readyState = 'open'; this.fire('sourceopen'); }, 0); }
      addSourceBuffer(type) {
        const b = listeners({ appendBuffer: (bytes) => { log.appended.push([type, bytes.byteLength]); setTimeout(() => b.fire('updateend'), 0); } });
        return b;
      }
      endOfStream() { log.ended++; this.readyState = 'ended'; finish(); }
    }
    // As a real video, it ends only once it plays and its stream has ended.
    let playing = false;
    const finish = () => { if (playing && log.ended) setTimeout(() => video.fire(end), 5); };
    const video = listeners({ style: {}, play: () => { playing = true; finish(); return Promise.resolve(); },
      pause() {}, removeAttribute() {}, load() {}, remove: () => { log.removed++; } });
    let now = 0;
    const env = {
      MediaSource, URL: { createObjectURL: () => 'blob:1', revokeObjectURL() {} },
      fetch: async (url) => {
        log.fetched.push(url);
        if (url === 'media/manifest.json') return media ? { ok: true, json: async () => media } : { ok: false };
        return { ok: true, arrayBuffer: async () => new ArrayBuffer(7) };
      },
      performance: { now: () => (now += 1500) },
      setTimeout, clearTimeout,
    };
    const doc = { createElement: () => video, body: { append: () => { log.attached++; } } };
    return { env, doc, log };
  };
  {
    const { env, doc, log } = videoEnv();
    const r = await videoCheck(env, doc);
    check(r.ok && same(log.fetched, ['media/manifest.json', 'media/avf/end1.webm']) && log.appended.length === 1
      && log.ended === 1 && log.attached === 1 && log.removed === 1 && r.detail === 'avf/end1, 30 frames: 1.5 s', `video: played to its end (${r.detail})`);
    const failing = videoEnv({ end: 'error' });
    const f = await videoCheck(failing.env, failing.doc);
    check(!f.ok && f.detail.endsWith(', MCI 8') && failing.log.removed === 1, 'video: an error is MCI_NOTIFY_FAILURE');
    const stuck = videoEnv({ end: 'none' });
    const t = await videoCheck(stuck.env, stuck.doc, { timeoutMs: 20 });
    check(!t.ok && t.detail.endsWith(', timeout') && stuck.log.removed === 1, 'video: no end within the limit');
    const none = videoEnv({ media: null });
    const n = await videoCheck(none.env, none.doc);
    check(!n.ok && n.detail === TEXTS['selftest.noMedia'], 'video: no movies in this build');
  }

  // The frame-time run and its summary.
  const run = frameTimeRun({ ticks: 120 });
  const env = new TextDecoder('latin1');
  const hostFile = (name) => run.hostFiles.find(([p]) => p === `${HOST_DIR}\\${name}`)?.[1];
  const ini = new TextDecoder('utf-16le').decode(hostFile('selftest.ini').subarray(2));
  check(run.language === 'G' && same(run.environment, { ES2SIM_PERF: PERF_FILE }) && same(run.collect, [PERF_FILE])
    && run.hostFiles.every(([p]) => p.startsWith(`${HOST_DIR}\\`)) && hostFile('selftest.ini')[0] === 0xff && hostFile('selftest.ini')[1] === 0xfe
    && ini.includes('virtual_time_pace=1\r\n') && ini.includes(`journal=${HOST_DIR}\\selftest.jsonl\r\n`)
    && env.decode(hostFile('selftest.jsonl')).split('\n').filter(Boolean).map((l) => JSON.parse(l)).filter((e) => e.vk === 81).map((e) => e.f).join() === '120,122'
    && same(run.shellArgs.map((a) => a.at(-1)), [`${HOST_DIR}\\launch.script`, `${HOST_DIR}\\results.script`])
    && same(run.simArgs, [['--config', `${HOST_DIR}\\selftest.ini`]]), 'frame time: the run (paced, Q at the tick, the host files)');
  const perf = (busy) => [
    JSON.stringify({ perf: 'es2sim', version: 1, mode: 'window', clock_hz: 1000000, setup_us: 5 }),
    ...busy.map((b, i) => JSON.stringify({ f: i + 1, us: 47000, work: b * 1000, wait: 47000 - b * 1000, traps: 0, calls: 9 })),
    JSON.stringify({ end: 'exit', frames: busy.length, us: 1, traps: 0, calls: 9, hot: [] }), ''].join('\n');
  const busy = Array.from({ length: 100 }, (_, i) => (i < 95 ? 10 + i * 0.1 : 45));
  const s = frameTimeSummary(new TextEncoder().encode(perf(busy)));
  check(s.frames === 100 && s.mode === 'window' && s.end === 'exit' && s.median === 14.95 && s.p95 === 19.4 && s.max === 45 && s.ok,
    `frame time: nearest-rank p95 of us - wait (${JSON.stringify(s)})`);
  const slow = frameTimeSummary(perf(busy.map((b) => b + 25)));
  check(!slow.ok && slow.p95 === 44.4 && FRAME_BUDGET_MS === 40, 'frame time: p95 at or over 40 ms fails');
  check(!frameTimeSummary(undefined).ok && frameTimeSummary('').frames === 0, 'frame time: no file, no frames');
  const row = frameTimeRow({ state: 0, files: [[PERF_FILE, new TextEncoder().encode(perf(busy))]] });
  check(row.ok && row.detail === '100 frames (window), busy median 14.95 ms, p95 19.4 ms, max 45 ms; p95 < 40 ms', `frame time: the row (${row.detail})`);
  check(!frameTimeRow({ state: null, error: 'trap\n at x' }).ok && frameTimeRow({ state: null, error: 'trap\n at x' }).detail === 'trap'
    && !frameTimeRow({ state: 0, files: [] }).ok, 'frame time: an error or no recorder file fails');
  const store = new MemoryStore();
  await store.applyAll([['C:\\ES2\\DATA\\A', { kind: 'bytes' }], ['C:\\ES2\\DATA\\B', { kind: 'bytes' }]]);
  await store.applyAll([['C:\\ES2\\DATA\\A', null]]);
  check(same([...store.map.keys()], ['C:\\ES2\\DATA\\B']) && same(await store.load(), []), 'frame time: the store in memory starts empty');

  // The rows: one per check with its button; a click runs the check (here the audio check over
  // the fake AudioContext) and shows its verdict.
  const doc = new FakeDocument();
  const page = fakeBrowser({ languages: ['en-US'] });
  Object.assign(page, audioEnv().env);
  const result = await runSelftest(page, doc);
  const rows = doc.ids['game-results'].children;
  check(same(result.game.map((r) => r.id), ['audio', 'video', 'frameTime']) && rows.length === 3
    && rows.every((r) => r.children[1].children[0].textContent === TEXTS['selftest.run'])
    && doc.ids['game-title'].textContent === TEXTS['selftest.game'], 'game checks: three rows with buttons');
  const button = rows[0].children[1].children[0];
  const clicked = button.listeners.click[0]();
  check(button.textContent === TEXTS['selftest.running'], 'game checks: running while the check runs');
  await clicked;
  await tick();
  check(rows[0].className === 'passed' && button.textContent === TEXTS['selftest.passed']
    && rows[0].children[2].textContent.startsWith('22050 Hz worklet'), 'game checks: the verdict and the detail');

  // The suite replays: the run of an entry, its row, the wasm check, a row per suite.
  const LOG = `${HOST_DIR}\\suite-tick-log.jsonl`;
  const b64 = (bytes) => Buffer.from(bytes).toString('base64');
  const logBytes = (qpc, frame = 1) => new TextEncoder().encode(
    `{"event":"start","pid":${qpc},"tid":2}\n{"event":"tick","sequence":${frame},"qpc":${qpc}}\n{"event":"exit","exit_code":0,"dep_flags":${qpc}}\n`);
  const expected = await tickLogHash(logBytes(1));
  const entry = {
    name: 'c1_01-quit-dialog', ticks: 400, language: 'G',
    game_files: [['C:\\ES2\\DATA\\script.dat', b64([1, 2, 3])]],
    host_files: [[`${HOST_DIR}\\suite.ini`, b64([0xff, 0xfe, 0x41, 0])]],
    args: ['--headless', '--config', `${HOST_DIR}\\suite.ini`, '--tick-log', LOG],
    tick_log: LOG, collect: [LOG], exit_code: 0, sha256: expected.sha256, lines: 3, node_seconds: 4.1,
  };
  const suiteOptions = suiteRun(entry);
  check(suiteOptions.simOnly && suiteOptions.language === 'G' && same(suiteOptions.environment, {})
    && same(suiteOptions.gameFiles.map(([path, b]) => [path, [...b]]), [['C:\\ES2\\DATA\\script.dat', [1, 2, 3]]])
    && same([...suiteOptions.hostFiles[0][1]], [0xff, 0xfe, 0x41, 0]) && same(suiteOptions.simArgs, [entry.args])
    && same(suiteOptions.collect, [LOG]), 'suite: the run of an entry (sim only, the files decoded)');
  const passed = await suiteRow(entry, { state: { sim: 0 }, files: [[LOG, logBytes(987654321987654)]] }, 6.25);
  check(passed.ok && passed.detail === `3 lines, exit code 0, SHA-256 ${expected.sha256.slice(0, 16)}; 6.3 s (Node 4.1 s)`,
    `suite: other host fields pass (${passed.detail})`);
  const other = await suiteRow(entry, { state: { sim: 0 }, files: [[LOG, logBytes(1, 2)]] }, 1);
  check(!other.ok && other.detail.includes(`(Node: 3 lines, exit code 0, SHA-256 ${expected.sha256.slice(0, 16)})`), 'suite: another tick fails');
  check(!(await suiteRow(entry, { state: { sim: 3 }, files: [[LOG, logBytes(1)]] }, 1)).ok, 'suite: another exit code fails');
  check((await suiteRow(entry, { state: null, error: 'trap\n at x' }, 1)).detail === 'trap'
    && (await suiteRow(entry, { state: { sim: 0 }, files: [] }, 1)).detail === `no tick log (${LOG})`, 'suite: an error or no tick log fails');
  const wasm = new Uint8Array([0, 0x61, 0x73, 0x6d]);
  const wasmHash = Buffer.from(await crypto.subtle.digest('SHA-256', wasm)).toString('hex');
  const suites = { format: 1, es2sim_wasm_sha256: wasmHash, suites: [entry, { ...entry, name: 'acceptance-60s' }] };
  const suitePage = (served) => ({
    ...fakeBrowser({ languages: ['de-DE'] }), crypto, performance,
    fetch: async (url) => {
      if (url === SUITES_FILE && served) return { ok: true, json: async () => served };
      if (url === 'es2sim.wasm') return { ok: true, arrayBuffer: async () => wasm.buffer.slice(0) };
      return { ok: false, status: 404 };
    },
  });
  const mismatch = await suiteCheck(suitePage(suites), new FakeDocument(), null, { ...suites, es2sim_wasm_sha256: '00' }, entry);
  check(!mismatch.ok && mismatch.detail === TEXTS['selftest.suiteWasm'], 'suite: another es2sim.wasm fails before the run');
  const suiteDoc = new FakeDocument();
  const withSuites = await runSelftest(suitePage(suites), suiteDoc);
  const suiteRows = suiteDoc.ids['game-results'].children;
  check(same(withSuites.game.map((r) => r.id), ['audio', 'video', 'frameTime', 'suite:c1_01-quit-dialog', 'suite:acceptance-60s'])
    && suiteRows[3].children[0].textContent === text('selftest.name.suite', { name: 'c1_01-quit-dialog' })
    && suiteRows[4].children[0].textContent === 'Suite replay acceptance-60s', 'suite: a row per suite of selftest_suites.json');
  const noSuites = await runSelftest(suitePage(null), new FakeDocument());
  check(noSuites.game.length === 3, 'suite: no selftest_suites.json, no suite rows');
}

console.log(`${checks} checks, ${failures.length} failed`);
if (failures.length) process.exit(1);
