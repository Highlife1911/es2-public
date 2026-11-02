// The page's side of the game (docs/web_build.md sections 2, 3 and 9 to 12): the game worker
// (worker.js over game_worker.mjs) runs the launcher loop with es2shell and es2sim_window; the
// page answers its requests, shows the frames, plays the sound, the movies and the CD music, and
// feeds the input ring.
//
//   const game = await startGame(env, doc, { language, keyboard, store, files });
//   await game.ended;                    // the launcher loop's end state
//
// GameRequests is the request side without the DOM (web/node/game_test.mjs and the Node game
// host use it with fakes):
//   {kind: 'persist', changes}           store.applyAll (one transaction per Vfs operation)
//   {kind: 'process', seq, program}      the CD music of a sim process, INPUT.START, FOCUS 1
//                                        when the page has the focus, then the answer
//   {kind: 'process_end', program, code} the music and a movie stop without a notification
//   {kind: 'message_box', seq, ...}      the modal dialog's button
//   {kind: 'movie_play', seq, ...}       MoviePlayer.play (1 or 0); {kind: 'movie_stop'}
//   {kind: 'mci', seq, ...}              answerMci over the process's CdMusic
//   {kind: 'cursor', busy | wait}        {kind: 'fullscreen', on}  {kind: 'display', fullscreen}
//   {kind: 'manual'}                     the online manual: MANUAL_URL in a new tab
//   {kind: 'end', state, files}  {kind: 'failed', text}   the worker is done (files: a run's
//                                        collected files, selftest_run.mjs)
// A request that throws ends the game with its error: the worker may be waiting for the answer.
import { C, INPUT, create, pushInput, readFrame, reply } from './shared.mjs';
import { answerMci } from './es2sim_imports.mjs';
import { CdAudio } from './cdaudio.mjs';
import { CdMusic } from './cd_player.mjs';
import { MoviePlayer } from './movie_player.mjs';
import { KeyWiring } from './keys.mjs';
import { guardUnload, requestPersistence } from './store.mjs';
import { text } from './texts.mjs';

export const AUDIO_PROCESSOR = 'es2-audio';   // audio_worklet.mjs PROCESSOR
export const SOUND_RATE = 22050;
export const MANUAL_URL = 'https://github.com/Highlife1911/es2-public';

// MessageBoxA's buttons by the style's low four bits (MB_OK .. MB_RETRYCANCEL) as
// [IDOK 1, IDCANCEL 2, IDABORT 3, IDRETRY 4, IDIGNORE 5, IDYES 6, IDNO 7], the default button
// (MB_DEFBUTTON2 0x100, MB_DEFBUTTON3 0x200) and Esc's answer: IDCANCEL when the box has it,
// IDOK for MB_OK, none otherwise (as Windows ignores Esc in a Yes/No box).
const BUTTONS = [[1], [1, 2], [3, 4, 5], [6, 7, 2], [6, 7], [4, 2]];
export const BUTTON_KEYS = { 1: 'box.ok', 2: 'box.cancel', 3: 'box.abort', 4: 'box.retry', 5: 'box.ignore', 6: 'box.yes', 7: 'box.no' };

export function messageBoxButtons(flags) {
  const buttons = BUTTONS[flags & 0xf] ?? BUTTONS[0];
  const index = Math.min((flags >> 8) & 0xf, buttons.length - 1);
  const escape = buttons.includes(2) ? 2 : buttons.length === 1 ? 1 : null;
  return { buttons, default: buttons[index], escape };
}

export class GameRequests {
  constructor({
    shared, store, dialog, movies = null, newMusic = null, movieBox = () => [{ left: 0, top: 0, width: 640, height: 480 }, 640, 480],
    cursor = () => {}, display = () => {}, manual = () => {}, focused = () => true, onEnd = () => {}, log = () => {},
  }) {
    Object.assign(this, { shared, store, dialog, movies, newMusic, movieBox, cursor, display, manual, focused, onEnd, log });
    this.music = null;
    this.program = null;
    this.writes = Promise.resolve();
    // MCI before a process has its music (none in practice): the headless answers.
    this.headless = new CdAudio({ toc: null });
  }

  push(type, a = 0, b = 0) {
    pushInput(this.shared, type, a, b, 0, 0, Math.floor(performance.now()));
  }

  endProcess() {
    if (this.music) this.music.end();
    this.music = null;
    if (this.movies) this.movies.stop();
  }

  handle(m) {
    try {
      this.answer(m);
    } catch (e) {
      const error = `${m.kind}: ${e && e.message ? e.message : e}`;
      this.log(error);
      this.onEnd({ state: null, error });
    }
  }

  answer(m) {
    switch (m.kind) {
      case 'persist': {
        const write = this.store.applyAll(m.changes);
        this.writes = write.catch((e) => this.log(`overlay: ${e && e.message ? e.message : e}`));
        return;
      }
      case 'process':
        this.endProcess();
        this.program = m.program;
        if (m.program === 'sim' && this.newMusic) {
          this.music = this.newMusic((code, device) => this.push(INPUT.MCI, code, device));
        }
        this.push(INPUT.START, m.seq);
        if (this.focused()) this.push(INPUT.FOCUS, 1);
        reply(this.shared, m.seq, 1);
        return;
      case 'process_end':
        this.endProcess();
        this.program = null;
        this.cursor(false);
        return;
      case 'message_box': {
        const { escape } = messageBoxButtons(m.flags);
        Promise.resolve()
          .then(() => this.dialog(m.text, m.title, m.flags))
          .then((button) => reply(this.shared, m.seq, button), (e) => {
            this.log(`message box: ${e && e.message ? e.message : e}`);
            reply(this.shared, m.seq, escape ?? messageBoxButtons(m.flags).default);
          });
        return;
      }
      case 'movie_play':
        reply(this.shared, m.seq, this.movies ? this.movies.play(m, ...this.movieBox()) : 0);
        return;
      case 'movie_stop':
        if (this.movies) this.movies.stop();
        return;
      case 'mci':
        answerMci(this.shared, this.music ?? this.headless, m);
        return;
      case 'cursor':
        this.cursor(!!(m.busy ?? m.wait));
        return;
      case 'fullscreen':
        this.display(!!m.on);
        return;
      case 'display':
        this.display(!!m.fullscreen);
        return;
      case 'manual':
        this.manual();
        return;
      case 'end':
        this.endProcess();
        this.onEnd({ state: m.state, files: m.files ?? [] });
        return;
      case 'failed':
        this.endProcess();
        this.onEnd({ state: null, error: m.text || text('game.worker') });
        return;
      case 'log':
        this.log(m.text);
        return;
      default:
        this.log(`unknown request ${m.kind}`);
    }
  }
}

// The modal dialog of a message box: the text, the caption as its heading, the buttons; resolves
// to the button's ID.
export function messageDialog(doc, dialog) {
  return (message, title, flags) => new Promise((resolve) => {
    const { buttons, default: initial, escape } = messageBoxButtons(flags);
    const heading = doc.createElement('h2');
    heading.textContent = title ?? '';
    const body = doc.createElement('p');
    body.textContent = message ?? '';
    body.style.whiteSpace = 'pre-wrap';
    const row = doc.createElement('form');
    row.method = 'dialog';
    let first = null;
    const done = (id) => {
      dialog.removeEventListener('cancel', cancel);
      dialog.close();
      resolve(id);
    };
    const cancel = (event) => {
      event.preventDefault();
      if (escape !== null) done(escape);
    };
    for (const id of buttons) {
      const b = doc.createElement('button');
      b.type = 'button';
      b.textContent = text(BUTTON_KEYS[id]);
      b.addEventListener('click', () => done(id));
      if (id === initial) first = b;
      row.append(b);
    }
    dialog.replaceChildren(heading, body, row);
    dialog.addEventListener('cancel', cancel);
    dialog.showModal();
    if (first) first.focus();
  });
}

// The canvas size by the largest integer factor that fits the container (at least 1).
export function scaleFactor(width, height, boxWidth, boxHeight) {
  return Math.max(1, Math.floor(Math.min(boxWidth / width, boxHeight / height)));
}

// A pointer position in guest client pixels, clamped to the client area.
export function clientPoint(event, box, width, height) {
  const x = Math.floor((event.clientX - box.left) * width / box.width);
  const y = Math.floor((event.clientY - box.top) * height / box.height);
  return [Math.min(Math.max(x, 0), width - 1), Math.min(Math.max(y, 0), height - 1)];
}

// WM_MOUSEWHEEL's delta of a wheel event (120 per notch, positive away from the user): pixels as
// Chrome's 100 per notch, lines as 40 pixels, pages as 800.
export function wheelDelta(event) {
  const scale = event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? 800 : 1;
  return Math.round(-event.deltaY * scale * 1.2);
}

const MOUSE_BUTTON = { 0: 0, 2: 1, 1: 2 };

// The game in the page: called from the Start click (the user activation for both
// AudioContexts, the movies and fullscreen).
// `store` is the OverlayStore or a promise of it and `files` store.mjs GameFiles' {manifest,
// gamefiles} or a promise of them, both awaited once the contexts exist. `volume`
// (0 to 1, web/volume.mjs) is a gain after the sound worklet and after the CD music, and the
// movies' volume. `language` is the game's language letter (G, E or F) that a new installation
// gets. `run` is the self-test's run (selftest_run.mjs frameTimeRun) for the worker; its
// run.language replaces `language`.
export async function startGame(env, doc, {
  language, keyboard, store: opening, files: loading, run = null, volume = 1, log = (s) => env.console?.log(s),
}) {
  // The contexts first, while the click's activation holds.
  const sound = new env.AudioContext({ sampleRate: SOUND_RATE });
  const musicContext = new env.AudioContext();
  const gain = (context) => {
    const g = context.createGain();
    g.gain.value = volume;
    g.connect(context.destination);
    return g;
  };
  const soundOutput = gain(sound);
  const musicOutput = gain(musicContext);
  const closeContexts = () => {
    sound.close?.().catch?.(() => {});
    musicContext.close?.().catch?.(() => {});
  };
  let store, overlay, manifest, gamefiles;
  try {
    ({ manifest, gamefiles } = await loading);
    store = await opening;
    overlay = await store.load();
  } catch (e) {
    closeContexts();
    throw e;
  }
  const shared = create();
  const container = doc.getElementById('game');
  const canvas = doc.getElementById('screen');
  const context2d = canvas.getContext('2d');
  const landing = doc.getElementById('landing');
  let running = true;
  // The guard stays until the game's last overlay writes have ended.
  const removeGuard = guardUnload(env, () => true);
  requestPersistence(env.navigator?.storage);

  try {
    await sound.audioWorklet.addModule('audio.js');
    const node = new env.AudioWorkletNode(sound, AUDIO_PROCESSOR, { outputChannelCount: [2], processorOptions: { sab: shared.sab } });
    node.connect(soundOutput);
  } catch (e) {
    log(`sound: ${e.message}`);
  }
  const json = async (url) => {
    try {
      const r = await env.fetch(url);
      return r.ok ? await r.json() : null;
    } catch {
      return null;
    }
  };
  const [mediaManifest, toc] = await Promise.all([json('media/manifest.json'), json('cd/toc.json')]);

  const push = (type, a = 0, b = 0, c = 0, d = 0, time = env.performance.now()) =>
    pushInput(shared, type, a, b, c, d, Math.floor(time));
  const movies = new MoviePlayer({
    manifest: mediaManifest ?? {},
    fetchBytes: async (url, signal) => {
      const r = await env.fetch(url, { signal });
      if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
      return r.arrayBuffer();
    },
    MediaSource: env.MediaSource,
    createObjectURL: (o) => env.URL.createObjectURL(o),
    revokeObjectURL: (u) => env.URL.revokeObjectURL(u),
    createVideo: () => {
      const v = doc.createElement('video');
      v.playsInline = true;
      v.volume = volume;
      return v;
    },
    attach: (v) => container.append(v),
    detach: (v) => v.remove(),
    notify: (code) => push(INPUT.MCI, code, 0),
    log,
  });
  const newMusic = (notify) => new CdMusic({
    toc,
    context: musicContext,
    output: musicOutput,
    load: async (url) => {
      const r = await env.fetch(url);
      if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
      return musicContext.decodeAudioData(await r.arrayBuffer());
    },
    setTimer: (fn, ms) => env.setTimeout(fn, ms),
    clearTimer: (id) => env.clearTimeout(id),
    notify,
    log,
  });

  const fullscreen = (on) => {
    if (on && !doc.fullscreenElement) container.requestFullscreen?.().catch((e) => log(`fullscreen: ${e.message}`));
    else if (!on && doc.fullscreenElement) doc.exitFullscreen?.().catch(() => {});
  };
  const keyboardLock = () => {
    const lock = env.navigator?.keyboard;
    if (doc.fullscreenElement) lock?.lock?.().catch?.(() => {});
    else lock?.unlock?.();
  };

  let endGame;
  const ended = new Promise((resolve) => { endGame = resolve; });
  const requests = new GameRequests({
    shared, store, movies, newMusic,
    dialog: messageDialog(doc, doc.getElementById('message')),
    movieBox: () => [canvas.getBoundingClientRect(), canvas.width, canvas.height],
    cursor: (wait) => { canvas.style.cursor = wait ? 'wait' : 'default'; },
    display: () => {}, // the game's fullscreen requests (prefs byte 6) leave the page as it is; Alt+Enter switches
    // The request follows the button's click by milliseconds, within the click's user activation.
    manual: () => { env.open?.(MANUAL_URL, '_blank', 'noopener'); },
    focused: () => doc.hasFocus(),
    onEnd: (result) => endGame(result),
    log,
  });

  // The newest frame on each animation frame, the canvas at the integer scale.
  let lastSeq = 0, width = 0, height = 0, image = null;
  const layout = () => {
    if (!width) return;
    const k = scaleFactor(width, height, container.clientWidth || env.innerWidth, container.clientHeight || env.innerHeight);
    canvas.style.width = `${width * k}px`;
    canvas.style.height = `${height * k}px`;
  };
  const present = () => {
    if (!running) return;
    const seq = Atomics.load(shared.control, C.FRAME_SEQ);
    if (seq !== lastSeq) {
      const frame = readFrame(shared);
      if (frame) {
        lastSeq = frame.seq;
        if (frame.width !== width || frame.height !== height) {
          width = canvas.width = frame.width;
          height = canvas.height = frame.height;
          image = context2d.createImageData(width, height);
          layout();
        }
        image.data.set(frame.rgba);
        context2d.putImageData(image, 0, 0);
      }
    }
    env.requestAnimationFrame(present);
  };
  env.requestAnimationFrame(present);

  // Input: the keys through KeyWiring (Alt+Enter also switches the page's fullscreen, its keydown
  // being a user activation), the mouse in client pixels, focus and visibility.
  const wiring = new KeyWiring(keyboard, { push, keys: shared.keys });
  const onKey = (down) => (event) => {
    if (!running) return;
    if (down && event.code === 'Enter' && event.altKey && !event.repeat) fullscreen(!doc.fullscreenElement);
    if (wiring.key(event, down)) event.preventDefault();
  };
  const listeners = [
    [doc, 'keydown', onKey(true)],
    [doc, 'keyup', onKey(false)],
    [canvas, 'mousemove', (e) => push(INPUT.MOVE, ...clientPoint(e, canvas.getBoundingClientRect(), width || 640, height || 480))],
    [canvas, 'mousedown', (e) => mouseButton(e, 1)],
    [doc, 'mouseup', (e) => mouseButton(e, 0)],
    [canvas, 'contextmenu', (e) => e.preventDefault()],
    [canvas, 'wheel', (e) => { e.preventDefault(); push(INPUT.WHEEL, wheelDelta(e)); }],
    [env, 'focus', () => push(INPUT.FOCUS, 1)],
    [env, 'blur', () => { wiring.blur(); push(INPUT.FOCUS, 0); }],
    [doc, 'visibilitychange', () => push(INPUT.SIZE, doc.visibilityState === 'hidden' ? 1 : 0)],
    [doc, 'fullscreenchange', keyboardLock],
    [doc, 'fullscreenchange', layout],
    [env, 'resize', layout],
  ];
  function mouseButton(e, down) {
    const button = MOUSE_BUTTON[e.button];
    if (button === undefined) return;
    const [x, y] = clientPoint(e, canvas.getBoundingClientRect(), width || 640, height || 480);
    push(INPUT.BUTTON, x, y, button, down);
    if (down) e.preventDefault();
  }
  for (const [target, name, fn] of listeners) target.addEventListener(name, fn, name === 'wheel' ? { passive: false } : undefined);

  let worker = null;
  const teardown = () => {
    running = false;
    worker?.terminate();
    for (const [target, name, fn] of listeners) target.removeEventListener(name, fn);
    removeGuard();
    fullscreen(false);
    closeContexts();
    container.hidden = true;
    if (landing) landing.hidden = false;
  };

  // The canvas still holds the previous game's last frame; the shell presents nothing before the
  // intro has played, so the container's black shows until then.
  context2d.clearRect(0, 0, canvas.width, canvas.height);
  if (landing) landing.hidden = true;
  container.hidden = false;
  if (sound.state === 'suspended') sound.resume().catch(() => {});
  if (musicContext.state === 'suspended') musicContext.resume().catch(() => {});

  // An error event without a message, or a message that cannot be read, still ends the game with
  // an error. A worker the browser stops without either is not noticed: no event tells the page.
  const failed = (message) => endGame({ state: null, error: message || text('game.worker') });
  try {
    worker = new env.Worker('worker.js', { type: 'module' });
    worker.addEventListener('message', (e) => requests.handle(e.data));
    worker.addEventListener('error', (e) => failed(e.message));
    worker.addEventListener('messageerror', () => failed(''));
    worker.postMessage({
      kind: 'start', sab: shared.sab, overlay, language: run?.language ?? language ?? 'E', layout: keyboard.name, manifest, gamefiles, run,
    });
  } catch (e) {
    teardown();
    throw e;
  }

  const result = ended.then(async (r) => {
    running = false;
    await requests.writes;
    teardown();
    return r;
  });
  return { shared, worker, requests, ended: result };
}
