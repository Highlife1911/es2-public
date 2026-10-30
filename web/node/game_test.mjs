// web/game.mjs's request side without the DOM, and web/game_worker.mjs's pure parts: the persist
// messages as one store transaction each (a rename's two keys together), the process start
// (INPUT.START, FOCUS 1 when the page has the focus, the answer, a new CdMusic for a sim), the process end
// (music and movie stopped without a notification), MCI over the process's CdMusic and the
// headless answers before one, the CD's MM_MCINOTIFY as an INPUT.MCI entry, movie_play and
// movie_stop to the MoviePlayer, the online manual, a throwing request or an empty failure text as an error end, the message box's round trip with its default and Esc buttons;
// messageBoxButtons, scaleFactor, clientPoint, wheelDelta; the worker's persist filter (its own
// C:\ES2HOST files never reach the page) and the reference environment block.
import { C, INPUT, INPUT_WORDS, create, takeInput } from '../shared.mjs';
import { MCI, MCIERR, MCI_NOTIFY } from '../cdaudio.mjs';
import { CdMusic } from '../cd_player.mjs';
import { GameRequests, clientPoint, messageBoxButtons, scaleFactor, wheelDelta } from '../game.mjs';
import { HOST_DIR, persistTo, referenceEnvironment } from '../game_worker.mjs';
import { text } from '../texts.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const tick = () => new Promise((resolve) => setImmediate(resolve));

const TOC = {
  frames_per_second: 75,
  tracks: [
    { number: 1, type: 'data', start: 0, length: 1000 },
    { number: 2, type: 'audio', start: 1000, length: 750, file: 'track02.opus' },
    { number: 3, type: 'audio', start: 1750, length: 750, file: 'track03.opus' },
  ],
};

function inputs(shared) {
  const out = new Int32Array(64 * INPUT_WORDS);
  const n = takeInput(shared, out, 64);
  return [...Array(n)].map((_, i) => [...out.subarray(i * INPUT_WORDS, i * INPUT_WORDS + 3)]);
}
const answer = (shared) => [Atomics.load(shared.control, C.REPLY_SEQ), Atomics.load(shared.control, C.REPLY_VALUE)];

function setup({ focused = true, dialog = () => 1 } = {}) {
  const shared = create();
  const store = { transactions: [], applyAll(changes) { this.transactions.push(changes); return Promise.resolve(); } };
  const movies = { plays: [], stops: 0, play(m, box, w, h) { this.plays.push([m.path, box, w, h]); return 1; }, stop() { this.stops++; } };
  const ctx = { currentTime: 0, destination: {}, sources: [] };
  ctx.createBufferSource = () => {
    const s = { connect() {}, disconnect() {}, start() { s.started = true; }, stop() { s.stopped = true; } };
    ctx.sources.push(s);
    return s;
  };
  const timers = new Map();
  let next = 1;
  const musics = [];
  const cursor = [], display = [], ends = [], manuals = [];
  const requests = new GameRequests({
    shared, store, dialog, movies,
    newMusic: (notify) => {
      const m = new CdMusic({
        toc: TOC, context: ctx, load: async () => ({}),
        setTimer: (fn, ms) => { timers.set(next, fn); return next++; }, clearTimer: (id) => timers.delete(id), notify,
      });
      musics.push(m);
      return m;
    },
    movieBox: () => [{ left: 1, top: 2, width: 1280, height: 960 }, 640, 480],
    cursor: (w) => cursor.push(w), display: (on) => display.push(on), manual: () => manuals.push(1), focused: () => focused,
    onEnd: (r) => ends.push(r),
  });
  return { shared, store, movies, ctx, timers, musics, cursor, display, manuals, ends, requests };
}

// ---- persist: one transaction per message, a rename's two keys in it
{
  const t = setup();
  const data = { kind: 'bytes', name: 'C:\\ES2\\SAV\\GAME_1.SAV', data: Uint8Array.of(1, 2) };
  t.requests.handle({ kind: 'persist', changes: [['C:\\ES2\\SAV\\GAME_1.SAV', data], ['C:\\ES2\\SAV\\GAME_1.TMP', null]] });
  t.requests.handle({ kind: 'persist', changes: [['C:\\ES2\\DATA\\PREFS.CFG', { kind: 'tombstone' }]] });
  await t.requests.writes;
  check(t.store.transactions.length === 2 && t.store.transactions[0].length === 2 && t.store.transactions[0][1][1] === null,
    'persist: a rename is one transaction with both keys');

  const posted = [];
  const persist = persistTo((m) => posted.push(m));
  persist([[`${HOST_DIR}\\MENU\\menu.ini`, { kind: 'bytes', data: Uint8Array.of(1) }]]);
  persist([[`${HOST_DIR}\\ENVIRON.BIN`, null], ['C:\\ES2\\SAV\\GAME_0.SAV', { kind: 'bytes', name: 'C:\\ES2\\SAV\\GAME_0.SAV', data: Uint8Array.of(7) }]]);
  check(posted.length === 1 && posted[0].kind === 'persist' && same(posted[0].changes.map(([k]) => k), ['C:\\ES2\\SAV\\GAME_0.SAV']),
    'persist: the worker\'s own files stay in the worker');
}

// ---- process start and end, MCI over the process's CdMusic
{
  const t = setup();
  const strings = { 0x4a0e34: 'cdaudio' };
  const mci = (seq, device, message, flags, fields) => {
    t.requests.handle({ kind: 'mci', seq, device, message, flags, fields, strings });
    return answer(t.shared);
  };
  check(same(mci(1, 0, MCI.OPEN, 0x2000, [0, 0, 0x4a0e34, 0, 0]), [1, MCIERR.DEVICE_NOT_INSTALLED]),
    'mci: before a sim process the headless answers');
  t.requests.handle({ kind: 'process', seq: 2, program: 'shell' });
  check(same(answer(t.shared), [2, 1]) && t.musics.length === 0 && same(inputs(t.shared), [[INPUT.START, 2, 0], [INPUT.FOCUS, 1, 0]]),
    'process: the shell gets START, FOCUS 1 and the answer, no music');
  t.requests.handle({ kind: 'process_end', program: 'shell', code: 2 });
  t.requests.handle({ kind: 'process', seq: 3, program: 'sim' });
  check(same(answer(t.shared), [3, 1]) && t.musics.length === 1 && t.movies.stops === 3, 'process: a sim gets its CdMusic');
  inputs(t.shared);
  const open = [0, 0, 0x4a0e34, 0, 0];
  t.requests.handle({ kind: 'mci', seq: 4, device: 0, message: MCI.OPEN, flags: 0x2000, fields: open, strings });
  const device = Atomics.load(t.shared.control, C.MCI_FIELDS + 1);
  check(same(answer(t.shared), [4, 0]) && device === 1, 'mci: OPEN cdaudio over the TOC, the device id in the fields');
  check(same(mci(5, device, MCI.SET, 0x400, [0, 10, 0]), [5, 0]) && same(mci(6, device, MCI.PLAY, 0xd, [0x10001, 2, 3]), [6, 0]),
    'mci: SET TMSF and PLAY NOTIFY FROM TO');
  await tick();
  await tick();
  check(t.timers.size === 1, 'mci: the play arms its timer');
  t.ctx.currentTime = 10;
  const [[id, fire]] = [...t.timers.entries()];
  t.timers.delete(id);
  fire();
  check(same(inputs(t.shared), [[INPUT.MCI, MCI_NOTIFY.SUCCESSFUL, device]]), 'mci: SUCCESSFUL at TO as an INPUT.MCI entry');
  check(same(mci(7, device, MCI.PLAY, 0xd, [0x10001, 2, 3]), [7, 0]), 'mci: a second play');
  await tick();
  await tick();
  t.requests.handle({ kind: 'process_end', program: 'sim', code: 3 });
  check(t.timers.size === 0 && t.ctx.sources.at(-1).stopped && inputs(t.shared).length === 0 && t.requests.music === null
    && same(t.cursor, [false, false]), 'process_end: the music stops without a notification');
  const unfocused = setup({ focused: false });
  unfocused.requests.handle({ kind: 'process', seq: 1, program: 'shell' });
  check(same(inputs(unfocused.shared), [[INPUT.START, 1, 0]]) && same(answer(unfocused.shared), [1, 1]),
'process: no FOCUS without the page\'s focus');
}

// ---- movies, cursor, display, the end
{
  const t = setup();
  t.requests.handle({ kind: 'movie_play', seq: 1, path: 'D:\\AVF\\INTRO.AVF', x: 0, y: 0, width: 640, height: 480 });
  check(same(answer(t.shared), [1, 1]) && same(t.movies.plays, [['D:\\AVF\\INTRO.AVF', { left: 1, top: 2, width: 1280, height: 960 }, 640, 480]]),
    'movie_play: the player with the canvas box, its answer');
  t.requests.handle({ kind: 'movie_stop' });
  check(t.movies.stops === 1, 'movie_stop: the player stops');
  const none = new GameRequests({ shared: create(), store: null, dialog: () => 1 });
  none.handle({ kind: 'movie_play', seq: 1, path: 'x' });
  check(same(answer(none.shared), [1, 0]), 'movie_play: 0 without a player (the shell skips the movie)');
  t.requests.handle({ kind: 'cursor', busy: 1 });
  t.requests.handle({ kind: 'cursor', wait: 0 });
  t.requests.handle({ kind: 'fullscreen', on: 1 });
  t.requests.handle({ kind: 'display', fullscreen: 0 });
  check(same(t.cursor, [true, false]) && same(t.display, [true, false]), 'cursor, fullscreen and display');
  t.requests.handle({ kind: 'manual' });
  check(t.manuals.length === 1, 'manual: the page opens the online manual');
  t.requests.handle({ kind: 'end', state: 0, files: [[`${HOST_DIR}\\perf.ndjson`, new Uint8Array([1])]] });
  t.requests.handle({ kind: 'failed', text: 'boom' });
  check(same(t.ends, [{ state: 0, files: [[`${HOST_DIR}\\perf.ndjson`, { 0: 1 }]] }, { state: null, error: 'boom' }]),
    'end with the collected files, failed');

  const ends = [];
  const throwing = new GameRequests({
    shared: create(), store: null, dialog: () => 1, onEnd: (r) => ends.push(r),
    movies: { play() { throw new Error('no video'); }, stop() {} },
  });
  throwing.handle({ kind: 'movie_play', seq: 1, path: 'x' });
  throwing.handle({ kind: 'failed', text: '' });
  check(same(ends, [{ state: null, error: 'movie_play: no video' }, { state: null, error: text('game.worker') }]),
    'a throwing request ends the game with its error; an empty failure text still reads as an error');
}

// ---- message boxes
{
  check(same(messageBoxButtons(0x30), { buttons: [1], default: 1, escape: 1 }), 'MB_OK | MB_ICONWARNING: OK, Esc is OK');
  check(same(messageBoxButtons(0x4), { buttons: [6, 7], default: 6, escape: null }), 'MB_YESNO: no Esc');
  check(same(messageBoxButtons(0x103), { buttons: [6, 7, 2], default: 7, escape: 2 }), 'MB_YESNOCANCEL | MB_DEFBUTTON2');
  check(same(messageBoxButtons(0x205), { buttons: [4, 2], default: 2, escape: 2 }), 'MB_RETRYCANCEL, DEFBUTTON3 clamped');
  const boxes = [];
  let answerBox;
  const t = setup({ dialog: (text, title, flags) => { boxes.push([text, title, flags]); return new Promise((r) => { answerBox = r; }); } });
  t.requests.handle({ kind: 'message_box', seq: 9, text: 'Fehler', title: 'EarthSiege 2', flags: 0x4 });
  await tick();
  check(same(boxes, [['Fehler', 'EarthSiege 2', 0x4]]) && answer(t.shared)[0] !== 9, 'message_box: the dialog, no answer yet');
  answerBox(7);
  await tick();
  check(same(answer(t.shared), [9, 7]), 'message_box: the button is the answer');
  const broken = setup({ dialog: () => { throw new Error('no dialog'); } });
  broken.requests.handle({ kind: 'message_box', seq: 4, text: 'x', title: 'y', flags: 0x1 });
  await tick();
  await tick();
  check(same(answer(broken.shared), [4, 2]), 'message_box: a failed dialog answers Esc\'s button');
}

// ---- presentation and input helpers
{
  check(scaleFactor(640, 480, 1920, 1080) === 2 && scaleFactor(640, 480, 600, 400) === 1 && scaleFactor(320, 200, 1280, 1000) === 4,
    'scaleFactor: the largest integer factor, at least 1');
  const box = { left: 100, top: 50, width: 1280, height: 960 };
  check(same(clientPoint({ clientX: 100, clientY: 50 }, box, 640, 480), [0, 0])
    && same(clientPoint({ clientX: 1379, clientY: 1009 }, box, 640, 480), [639, 479])
    && same(clientPoint({ clientX: 2000, clientY: 0 }, box, 640, 480), [639, 0]), 'clientPoint: scaled and clamped');
  check(wheelDelta({ deltaY: -100, deltaMode: 0 }) === 120 && wheelDelta({ deltaY: 3, deltaMode: 1 }) === -144
    && wheelDelta({ deltaY: 1, deltaMode: 2 }) === -960, 'wheelDelta: one notch is 120 away from the user');
}

// ---- the reference environment block
{
  const block = referenceEnvironment();
  const strings = new TextDecoder('latin1').decode(block).split('\0');
  const named = strings.filter(Boolean);
  // CW3220's heap block of a string with its NUL: its length + 9, rounded up to 4 (the pads
  // are sized so that no rounding happens).
  const heap = named.reduce((sum, s) => sum + s.length + 1 + 8, 0);
  check(block.at(-1) === 0 && block.at(-2) === 0 && named.length === 102 && heap === 5648
    && named.every((s) => (s.length + 9) % 4 === 0) && same(named, [...named].sort()),
    `environment: 102 sorted strings in 5648 heap bytes (${block.length} bytes, ${heap})`);
  const perf = 'ES2SIM_PERF=C:\\ES2HOST\\perf.ndjson';
  const withPerf = new TextDecoder('latin1').decode(referenceEnvironment({ ES2SIM_PERF: 'C:\\ES2HOST\\perf.ndjson' }))
    .split('\0').filter(Boolean);
  const heapOf = (s) => Math.max(16, ((s.length + 1 + 3) & ~3) + 8);
  check(withPerf.length === 102 && withPerf[0] === perf && withPerf.reduce((sum, s) => sum + heapOf(s), 0) === 5648
    && withPerf.at(-1).startsWith('Z100='), 'environment: a variable takes the place of one pad, 5648 heap bytes kept');
  let refused = false;
  try { referenceEnvironment({ A: 'x'.repeat(6000) }); } catch { refused = true; }
  check(refused, 'environment: variables that do not fit are refused');
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
