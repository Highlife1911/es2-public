// The landing page's module (index.html; docs/web_build.md section 13). Step 1: isolation
// through the service worker, then the feature checks; a failure names what is missing and the
// page goes no further. startPage resolves to { state, ready, results }; with ready
// the language step (showLanguage), the saves step (showSaves: upload, export, reset), the volume
// (showVolume) and the start step follow (showStart): its click starts the game (game.mjs) over
// the overlay store in IndexedDB, and the end of the game brings the landing page back.
//
// `env` is the page's global object and `doc` its document; web/node/page_test.mjs passes fakes.
import { failedTexts, runChecks } from './checks.mjs';
import { FAILURES, ensureIsolation } from './isolation.mjs';
import { GAME_LANGUAGES, gameLanguage, text } from './texts.mjs';
import { PartCache, holdInstallation, openOverlayStore, resetInstallation } from './store.mjs';
import { SAV_DIR, importSaves, loadChecker } from './saves.mjs';
import { ZipError, inflateStream, readZip } from './unzip.mjs';
import { LANGUAGE_FILE, keyOf, normalize } from './vfs.mjs';
import { zipStored } from './zip.mjs';
import { loadVolume, saveVolume } from './volume.mjs';
import { Keyboard, layoutForLanguage } from './keys.mjs';

// A setting's label: its name and a '?' whose title (a tooltip) is the text of `tip`.
function settingLabel(doc, key, tip) {
  const heading = doc.createElement('h2');
  const name = doc.createElement('span');
  name.textContent = text(key);
  const info = doc.createElement('span');
  info.className = 'info';
  info.textContent = '?';
  info.tabIndex = 0;
  info.title = text(tip);
  info.setAttribute('aria-label', text(tip));
  heading.append(name, info);
  return heading;
}

export function say(doc, keys) {
  const box = doc.getElementById('checks');
  box.replaceChildren(...keys.map((key) => {
    const p = doc.createElement('p');
    p.textContent = text(key);
    return p;
  }));
}

export async function startPage(env, doc) {
  doc.title = text('page.title');
  doc.getElementById('title').textContent = text('page.title');
  const state = await ensureIsolation(env);
  if (state === 'reloading') {
    say(doc, ['isolation.reloading']);
    return { state, ready: false, results: [] };
  }
  const results = await runChecks(env);
  // Without isolation the SharedArrayBuffer check fails as well; the isolation text says why.
  const failed = failedTexts(results.filter((r) => state === 'isolated' || r.id !== 'sharedArrayBuffer'));
  if (state !== 'isolated') failed.unshift(FAILURES[state]);
  if (failed.length) {
    say(doc, [...failed, 'checks.failed']);
    return { state, ready: false, results };
  }
  say(doc, []);
  doc.getElementById('steps').hidden = false;
  return { state, ready: true, results };
}

// The overlay store, opened on first use; an open that failed is tried again on the next use.
export function storeOpener(env) {
  let store = null;
  return () => (store ??= openOverlayStore(env.indexedDB).catch((e) => { store = null; throw e; }));
}

const browserLanguages = (env) => (env.navigator?.languages?.length ? env.navigator.languages : env.navigator?.language);

// The installation's language letter: the first byte of the overlay's LANGUAGE.CFG, which the
// game worker writes before the first process (game_worker.mjs prepareInstallation); null before.
export function installedLanguage(overlay) {
  const key = keyOf(LANGUAGE_FILE);
  const e = overlay.find(([k]) => k === key)?.[1];
  return e?.kind === 'bytes' && e.data.length ? String.fromCharCode(e.data[0]) : null;
}

// Step 2: the game's language, a flag button per letter (index.html draws the flags), shown after
// the first refresh(). While the installation has none (the first visit and after a reset), Start
// waits for a flag's click: ready() and the onChange listeners. Once it has one, its flag stays
// pressed and the flags are disabled. Start passes choice() to the worker; without a click it is
// the browser's language. refresh() reads the overlay again; a store that does not open leaves
// the flags enabled, and Start names the error.
export function showLanguage(env, doc, openStore) {
  const section = doc.getElementById('language');
  let chosen = gameLanguage(browserLanguages(env));
  let picked = false;
  let installed = null;
  const listeners = [];
  const changed = () => { for (const fn of listeners) fn(); };
  const heading = settingLabel(doc, 'lang.title', 'lang.tip');
  const choices = doc.createElement('div');
  choices.className = 'flags';
  choices.setAttribute('role', 'group');
  choices.setAttribute('aria-label', text('lang.title'));
  const flags = GAME_LANGUAGES.map((code) => {
    const flag = doc.createElement('button');
    flag.type = 'button';
    flag.className = `flag flag-${code}`;
    flag.value = code;
    flag.title = text(`lang.${code}`);
    flag.setAttribute('aria-label', text(`lang.${code}`));
    flag.addEventListener('click', () => { chosen = code; picked = true; mark(); changed(); });
    choices.append(flag);
    return flag;
  });
  const mark = () => {
    const pressed = installed ?? (picked ? chosen : null);
    for (const flag of flags) {
      flag.setAttribute('aria-pressed', flag.value === pressed);
      flag.disabled = installed !== null;
    }
  };
  mark();
  section.replaceChildren(heading, choices);
  section.hidden = true;
  const refresh = async () => {
    try {
      installed = installedLanguage(await (await openStore()).load());
    } catch {
      installed = null;
    }
    // After a reset a new click is needed.
    if (installed !== null) picked = false;
    mark();
    section.hidden = false;
    changed();
    return installed;
  };
  return {
    choice: () => installed ?? chosen, refresh, flags, ready: () => picked || installed !== null, onChange: (fn) => { listeners.push(fn); },
  };
}

const SAV_PREFIX = `${SAV_DIR}\\`;
const BASE_SAVES = [`${SAV_DIR}\\GAMEFILE.STR`]; // the manifest's files in C:\ES2\SAV\

// The overlay changes of an accepted upload ([path, bytes] in order), one transaction's worth: a
// file the overlay or the base already has keeps its name, as an overwrite on Windows does.
export function saveChanges(writes, overlay) {
  const names = new Map(BASE_SAVES.map((name) => [keyOf(name), name]));
  for (const [key, e] of overlay) if (e.kind === 'bytes' || e.kind === 'parts') names.set(key, e.name);
  return writes.map(([path, bytes]) => {
    const key = keyOf(path);
    return [key, { kind: 'bytes', name: names.get(key) ?? normalize(path), data: bytes }];
  });
}

// The export's entries: the overlay's files directly in C:\ES2\SAV\, by name. What the game or an
// upload wrote is a bytes entry; a file the overlay does not hold is still the CD's.
export function exportEntries(overlay) {
  return overlay
    .filter(([key, e]) => key.startsWith(SAV_PREFIX) && !key.slice(SAV_PREFIX.length).includes('\\') && e.kind === 'bytes')
    .map(([, e]) => ({ name: e.name.slice(e.name.lastIndexOf('\\') + 1), bytes: e.data }))
    .sort((a, b) => (a.name.toUpperCase() < b.name.toUpperCase() ? -1 : 1));
}

const errorText = (e) => (e && e.message ? e.message : String(e));

// Step 3 and the extras: the save upload (one ZIP file, its entries checked by tools.wasm and
// written in one transaction), the export as a ZIP download and the reset after its confirmation
// in a bubble over the button row. The label's tooltip names the accepted files.
// `loadTools` resolves to tools.js's default export (page_test.mjs passes a fake); `onReset` runs
// after a reset (the language step shows again).
export function showSaves(env, doc, openStore, {
  loadTools = async () => (await import('./tools.js')).default, onReset = async () => {},
} = {}) {
  const section = doc.getElementById('saves');
  const say = (el, key, params) => { el.textContent = text(key, params); };
  const heading = settingLabel(doc, 'saves.title', 'saves.details');
  const button = (key, onClick) => {
    const b = doc.createElement('button');
    b.type = 'button';
    say(b, key);
    b.addEventListener('click', onClick);
    return b;
  };
  const messages = doc.createElement('div');
  const show = (items) => messages.replaceChildren(...items.map(([key, params]) => {
    const p = doc.createElement('p');
    say(p, key, params);
    return p;
  }));
  let checker = null;
  const inflateRaw = inflateStream(env);
  // file: a File or { name, bytes }; the entries' folders do not count (saves.mjs baseName).
  const upload = async (file) => {
    try {
      const bytes = file.bytes ?? new Uint8Array(await file.arrayBuffer());
      let entries;
      try {
        entries = await readZip(bytes, { inflateRaw });
      } catch (e) {
        if (!(e instanceof ZipError)) throw e;
        return show([['saves.zip_invalid', { name: file.name, reason: text(`saves.zip.${e.reason}`) }], ['saves.nothing_written']]);
      }
      checker ??= loadTools().then(loadChecker).catch((e) => { checker = null; throw e; });
      const store = await openStore();
      const writes = [];
      const result = await importSaves(entries, await checker, (path, data) => { writes.push([path, data]); });
      if (writes.length) await store.applyAll(saveChanges(writes, await store.load()));
      show(result.messages.map((m) => [m.key, m.params]));
    } catch (e) {
      show([['saves.nothing_written'], ['page.failed', { error: errorText(e) }]]);
    }
  };
  const zipInput = doc.createElement('input');
  zipInput.type = 'file';
  zipInput.accept = '.zip,application/zip';
  zipInput.hidden = true;
  zipInput.addEventListener('change', async () => {
    const file = zipInput.files?.[0];
    zipInput.value = '';
    if (file) await upload(file);
  });
  const download = async () => {
    try {
      const entries = exportEntries(await (await openStore()).load());
      if (!entries.length) return show([['export.empty']]);
      const zip = zipStored(entries, { date: new Date() });
      const url = env.URL.createObjectURL(new env.Blob([zip], { type: 'application/zip' }));
      const a = doc.createElement('a');
      a.href = url;
      a.download = text('export.filename');
      doc.body.append(a);
      a.click();
      a.remove();
      env.setTimeout(() => env.URL.revokeObjectURL(url), 1000);
      show([]);
    } catch (e) {
      show([['page.failed', { error: errorText(e) }]]);
    }
  };
  const confirm = doc.createElement('div');
  confirm.className = 'bubble';
  confirm.setAttribute('role', 'dialog');
  confirm.hidden = true;
  // A game in another tab keeps its own view of the overlay and its open part cache; the reset
  // waits until it has ended.
  const reset = async () => {
    confirm.hidden = true;
    const release = await holdInstallation(env.navigator?.locks);
    if (!release) {
      show([['reset.busy']]);
      return;
    }
    try {
      await resetInstallation(await openStore(), new PartCache({ caches: env.caches, fetch: (url) => env.fetch(url) }));
      release();
      show([['reset.done']]);
      await onReset();
    } catch (e) {
      release();
      show([['page.failed', { error: errorText(e) }]]);
    }
  };
  const question = doc.createElement('p');
  say(question, 'reset.confirm');
  const cancel = button('reset.no', () => { confirm.hidden = true; });
  const yes = button('reset.yes', reset);
  yes.className = 'danger';
  confirm.append(question, yes, cancel);
  doc.addEventListener('keydown', (e) => { if (e.key === 'Escape') confirm.hidden = true; });
  const resetButton = button('reset.button', () => {
    show([]);
    confirm.hidden = !confirm.hidden;
    if (!confirm.hidden) cancel.focus?.();
  });
  const actions = doc.createElement('div');
  actions.className = 'actions';
  actions.append(button('saves.upload', () => zipInput.click()), button('export.button', download), resetButton, confirm);
  section.replaceChildren(heading, actions, zipInput, messages);
  section.hidden = false;
  return { upload, download, reset, confirm, messages };
}

export const VOLUME_TEST_SECONDS = 4;

// The volume (web/volume.mjs) as a slider from 0 to 100 % with its test: the start of the CD's
// first audio track, or a 440 Hz tone when the CD has none, through a gain that follows the
// slider while it plays. Each test click makes its own AudioContext (the user activation).
export function showVolume(env, doc) {
  const section = doc.getElementById('volume');
  const heading = settingLabel(doc, 'volume.title', 'volume.tip');
  const slider = doc.createElement('input');
  slider.type = 'range';
  slider.min = '0';
  slider.max = '100';
  slider.step = '1';
  slider.value = String(Math.round(loadVolume(env.localStorage) * 100));
  slider.setAttribute('aria-label', text('volume.title'));
  const value = doc.createElement('span');
  const volume = () => Number(slider.value) / 100;
  const label = () => { value.textContent = text('volume.value', { percent: slider.value }); };
  label();
  let playing = null;
  slider.addEventListener('input', () => {
    label();
    saveVolume(env.localStorage, volume());
    if (playing) playing.gain.value = volume();
  });
  const test = doc.createElement('button');
  test.type = 'button';
  test.textContent = text('volume.test');
  const status = doc.createElement('p');
  const play = async () => {
    const context = new env.AudioContext();
    try {
      const gain = context.createGain();
      gain.gain.value = volume();
      gain.connect(context.destination);
      playing = gain;
      let source;
      const toc = await fetchJson(env, 'cd/toc.json');
      const track = toc?.tracks?.find((t) => t.type === 'audio' && t.file);
      if (track) {
        const r = await env.fetch(`cd/${track.file}`);
        if (!r.ok) throw new Error(`cd/${track.file}: HTTP ${r.status}`);
        source = context.createBufferSource();
        source.buffer = await context.decodeAudioData(await r.arrayBuffer());
      } else {
        source = context.createOscillator();
        source.frequency.value = 440;
      }
      const ended = new Promise((resolve) => { source.onended = resolve; });
      source.connect(gain);
      source.start(0);
      source.stop(context.currentTime + VOLUME_TEST_SECONDS);
      await ended;
    } finally {
      playing = null;
      context.close?.()?.catch?.(() => {});
    }
  };
  test.addEventListener('click', async () => {
    test.disabled = true;
    status.textContent = '';
    try {
      await play();
    } catch (e) {
      status.textContent = text('page.failed', { error: errorText(e) });
    }
    test.disabled = false;
  });
  const row = doc.createElement('div');
  row.className = 'actions';
  row.append(slider, value, test);
  section.replaceChildren(heading, row, status);
  section.hidden = false;
  return { slider, test, status };
}

async function fetchJson(env, url) {
  try {
    const r = await env.fetch(url);
    return r.ok ? await r.json() : null;
  } catch {
    return null;
  }
}

// Step 4: the Start button, a triangle only (index.html draws it), named by its aria-label.
// `start` is game.mjs's startGame (page_test.mjs passes a fake); the keyboard layout follows the
// browser's language, the volume the slider's, and the game's language the language step's
// choice (without one, the browser's language). The game holds the installation's lock until it
// has ended (a game in another tab refuses the start). After the game the language step looks at
// the installation again. While the language step waits for a click, the button stays disabled
// and its title says why.
export function showStart(env, doc, start, { openStore = storeOpener(env), languageStep = null } = {}) {
  const section = doc.getElementById('start');
  const button = doc.createElement('button');
  button.type = 'button';
  button.className = 'start';
  button.setAttribute('aria-label', text('start.button'));
  const status = doc.createElement('p');
  section.replaceChildren(button, status);
  section.hidden = false;
  let running = false;
  const update = () => {
    const waiting = languageStep ? !languageStep.ready() : false;
    button.disabled = running || waiting;
    button.title = text(waiting ? 'start.language' : 'start.button');
  };
  languageStep?.onChange(update);
  update();
  button.addEventListener('click', async () => {
    running = true;
    update();
    status.textContent = '';
    const release = await holdInstallation(env.navigator?.locks);
    if (!release) {
      status.textContent = text('start.busy');
      running = false;
      update();
      return;
    }
    const store = openStore();
    const keyboard = new Keyboard(layoutForLanguage(env.navigator?.language));
    let result;
    try {
      const language = languageStep ? languageStep.choice() : gameLanguage(browserLanguages(env));
      const game = await start(env, doc, { language, keyboard, store, volume: loadVolume(env.localStorage) });
      result = await game.ended;
    } catch (e) {
      result = { state: null, error: e && e.message ? e.message : String(e) };
    }
    release();
    status.textContent = result.error ? text('game.failed', { error: result.error }) : '';
    await languageStep?.refresh();
    running = false;
    update();
  });
  return button;
}

if (typeof document !== 'undefined') {
  startPage(globalThis, document).then(async (page) => {
    if (!page.ready) return;
    const { startGame } = await import('./game.mjs');
    const openStore = storeOpener(globalThis);
    const languageStep = showLanguage(globalThis, document, openStore);
    languageStep.refresh();
    showSaves(globalThis, document, openStore, { onReset: languageStep.refresh });
    showVolume(globalThis, document);
    showStart(globalThis, document, startGame, { openStore, languageStep });
  });
}
