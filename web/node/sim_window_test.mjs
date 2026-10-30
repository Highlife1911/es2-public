// The worker side of es2sim_window's web platform without the module: the simulation audio ring
// and the MCI reply of shared.mjs, every "es2sim" import of es2sim_imports.mjs over a fake linear
// memory (the input ring, the frame slots, the metrics, the key bitmap, the sink's format and
// fill, the message box and MCI requests with web/cdaudio.mjs answering), and keys.mjs's
// KeyWiring: the entries and the bitmap per key, the fake left Ctrl of AltGr added only when no
// ControlLeft came with the right Alt's timestamp, the toggles and the blur.
import {
  C, INPUT, INPUT_WORDS, MCI_FIELD_COUNT, SIM_AUDIO_BYTES, create, pushInput, readFrame, replyMci, resetSimAudio,
  simAudioQueued, takeSimAudio, writeSimAudio,
} from '../shared.mjs';
import { answerMci, es2simWindowImports } from '../es2sim_imports.mjs';
import { CdAudio, MCI, MCI_FLAG, MCIERR, MCI_NOTIFY } from '../cdaudio.mjs';
import { Keyboard, KeyWiring } from '../keys.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ---- the simulation audio ring
{
  const v = create();
  check(simAudioQueued(v) === 0, 'audio ring: empty at the start');
  check(writeSimAudio(v, Uint8Array.from([1, 2, 3, 4])) && simAudioQueued(v) === 4, 'audio ring: a buffer queued');
  check(same([...takeSimAudio(v, 3)], [1, 2]), 'audio ring: a take is whole stereo frames');
  check(simAudioQueued(v) === 2, 'audio ring: the fill after the take');
  check(!writeSimAudio(v, new Uint8Array(SIM_AUDIO_BYTES - 1)) && simAudioQueued(v) === 2, 'audio ring: a buffer larger than the space is refused whole');
  check(writeSimAudio(v, new Uint8Array(SIM_AUDIO_BYTES - 2)) && simAudioQueued(v) === SIM_AUDIO_BYTES, 'audio ring: the space exactly');
  resetSimAudio(v);
  check(simAudioQueued(v) === 0 && takeSimAudio(v, 16).length === 0, 'audio ring: reset drops what is queued');
  // Across the ring's end and the uint32 wrap of the counters.
  Atomics.store(v.control, C.SIM_AUDIO_WRITE, -3);
  Atomics.store(v.control, C.SIM_AUDIO_READ, -3);
  check(writeSimAudio(v, Uint8Array.from([10, 11, 12, 13, 14, 15])), 'audio ring: a write across the wrap');
  check((Atomics.load(v.control, C.SIM_AUDIO_WRITE) >>> 0) === 3 && simAudioQueued(v) === 6, 'audio ring: the counter wrapped');
  check(same([...takeSimAudio(v, 6)], [10, 11, 12, 13, 14, 15]), 'audio ring: the bytes across the wrap in order');
}

// ---- replyMci
{
  const v = create();
  replyMci(v, 7, 282, [1, 0xffffffff, 3, 4, 5]);
  check(Atomics.load(v.control, C.REPLY_SEQ) === 7 && Atomics.load(v.control, C.REPLY_VALUE) === 282, 'replyMci: the sequence and the error');
  const fields = [];
  for (let i = 0; i < MCI_FIELD_COUNT; i++) fields.push(Atomics.load(v.control, C.MCI_FIELDS + i) >>> 0);
  check(same(fields, [1, 0xffffffff, 3, 4, 5]), 'replyMci: the fields');
}

// ---- the imports over a fake memory
const memoryBuffer = new ArrayBuffer(1 << 20);
const memory = () => memoryBuffer;
const mem8 = new Uint8Array(memoryBuffer);
const putString = (at, s) => { for (let i = 0; i < s.length; i++) mem8[at + i] = s.charCodeAt(i); mem8[at + s.length] = 0; };
{
  const v = create();
  const posted = [];
  const keyboard = new Keyboard('de');
  const toc = { tracks: [{ number: 1, start: 150, length: 10000 }, { number: 2, start: 10150, length: 7500, file: 't2' }] };
  const cd = new CdAudio({ toc, clock: () => 0, notify: (code, device) => pushInput(v, INPUT.MCI, code, device) });
  // The page answers at once (the request is posted before the worker waits).
  const post = (m) => {
    posted.push(m);
    if (m.kind === 'message_box') replyMci(v, m.seq, 2, null);
    if (m.kind === 'mci') answerMci(v, cd, m);
  };
  const metrics = [1920, 1080, 4, 4, 23];
  const im = es2simWindowImports(v, { post, keyboard, metrics })(memory);

  check([0x41, 0x5a, 0x59, 0x0d, 0x1b, 0x70].every((vk) => im.key_scan(vk) === keyboard.vkToScan(vk)), 'key_scan: MapVirtualKeyA of the layout (de: Y and Z swapped)');
  check(im.key_scan(0x5a) === 0x15 && im.key_scan(0x59) === 0x2c, 'key_scan: de Z at 15h, Y at 2Ch');
  check(im.key_translate(0x41, 0x1e) === new Keyboard('de').translate(0x41), 'key_translate: ToAscii of the layout');

  const t = im.window_now();
  check(typeof t === 'number' && t >= 0, 'window_now: milliseconds since the start');
  const s0 = performance.now();
  im.window_sleep(20);
  check(performance.now() - s0 >= 15, 'window_sleep: waits');
  pushInput(v, INPUT.KEY, 0x1e, 0x41, 0, 0, 5);
  const s1 = performance.now();
  im.window_sleep(30);
  check(performance.now() - s1 >= 25, 'window_sleep: input does not end it');

  pushInput(v, INPUT.MOVE, 10, 20, 0, 0, 6);
  const out = 4096;
  check(im.window_take_input(out, 1) === 1, 'take_input: one entry');
  const e = new Int32Array(memoryBuffer, out, INPUT_WORDS);
  check(same([...e], [INPUT.KEY, 0x1e, 0x41, 0, 0, 5, 0, 0]), 'take_input: the entry in the C layout');
  check(im.window_take_input(out, 4) === 1 && e[0] === INPUT.MOVE && e[1] === 10 && e[2] === 20, 'take_input: the next entry');
  check(im.window_take_input(out, 4) === 0, 'take_input: empty');

  const pix = 65536;
  for (let i = 0; i < 4 * 4 * 4; i++) mem8[pix + i] = i;
  im.window_present(pix, 4, 4);
  const f = readFrame(v);
  check(f && f.width === 4 && f.height === 4 && f.rgba.every((b, i) => b === i), 'present: the picture in a frame slot');

  im.window_cursor(1);
  check(same(posted.at(-1), { kind: 'cursor', wait: true }), 'cursor: a request');
  check(im.window_display(1) === 1 && same(posted.at(-1), { kind: 'display', fullscreen: true }), 'display: a request, answered 1');

  im.window_metrics(8192);
  check(same([...new Uint32Array(memoryBuffer, 8192, 5)], metrics), 'metrics: the five values');
  v.keys[0x90] = 1;
  v.keys[0x41] = 0x80;
  im.window_keys(12288);
  check(mem8[12288 + 0x90] === 1 && mem8[12288 + 0x41] === 0x80 && mem8[12288 + 0x42] === 0, 'keys: the bitmap');

  putString(16384, 'Fehler \xe4');
  putString(16512, 'DBSIM');
  check(im.window_message_box(16384, 16512, 0x30) === 2, 'message_box: the page\'s button');
  const box = posted.at(-1);
  check(box.kind === 'message_box' && box.text === 'Fehler \xe4' && box.title === 'DBSIM' && box.flags === 0x30, 'message_box: text, caption and style');

  check(im.audio_write(pix, 4) === 5, 'audio: a write before the open is refused');
  check(im.audio_open(22050, 2, 16) === 32 && im.audio_open(11025, 2, 8) === 32, 'audio: another format is refused (WAVERR_BADFORMAT)');
  check(im.audio_open(22050, 2, 8) === 0 && im.audio_open(22050, 2, 8) === 4, 'audio: the open, a second one refused');
  check(im.audio_write(pix, 0x800) === 0 && im.audio_queued() === 0x800, 'audio: a buffer queued');
  check(same([...takeSimAudio(v, 4)], [0, 1, 2, 3]) && im.audio_queued() === 0x7fc, 'audio: the ring holds the buffer\'s bytes');
  im.audio_reset();
  check(im.audio_queued() === 0, 'audio: reset');
  check(im.audio_write(pix, SIM_AUDIO_BYTES + 2) === 1, 'audio: a buffer beyond the ring is an error');
  im.audio_close();
  check(im.audio_queued() === 0 && im.audio_write(pix, 2) === 5, 'audio: closed');

  // MCI: the contract's commands through the host block.
  const block = 20480;
  const fields = () => new Uint32Array(memoryBuffer, block, MCI_FIELD_COUNT);
  putString(24576, 'cdaudio');
  fields().set([0x1234, 0, 24576, 0, 0]);
  check(im.mci_send(0, MCI.OPEN, MCI_FLAG.OPEN_TYPE, block) === 0 && fields()[1] === 1, 'mci: OPEN cdaudio, the device id into field 1');
  const open = posted.at(-1);
  check(open.kind === 'mci' && open.strings[24576] === 'cdaudio', 'mci: OPEN\'s type travels as text');
  fields().set([0, 10, 0, 0, 0]);
  check(im.mci_send(1, MCI.SET, MCI_FLAG.TIME_FORMAT, block) === 0, 'mci: SET TMSF');
  fields().set([0, 0, 1, 2, 0]);
  check(im.mci_send(1, MCI.STATUS, MCI_FLAG.ITEM | MCI_FLAG.TRACK, block) === 0 && fields()[1] === ((1 | 40 << 8) >>> 0), 'mci: STATUS LENGTH of track 2 (1:40) in field 1');
  check(im.mci_send(1, 0x80a, 0, 0) === MCIERR.UNSUPPORTED_FUNCTION, 'mci: outside the contract, no block');
  fields().set([0x55, 2, 0, 0, 0]);
  check(im.mci_send(1, MCI.PLAY, MCI_FLAG.NOTIFY | MCI_FLAG.FROM, block) === 0, 'mci: PLAY FROM with NOTIFY');
  check(im.mci_send(1, MCI.STOP, 0, 0) === 0, 'mci: STOP');
  check(im.window_take_input(out, 1) === 1 && e[0] === INPUT.MCI && e[1] === MCI_NOTIFY.ABORTED && e[2] === 1, 'mci: the ABORTED notify as an MCI entry (code, device)');

  // Without a TOC: the headless answers.
  const headless = new CdAudio();
  const v2 = create();
  const im2 = es2simWindowImports(v2, { post: (m) => { if (m.kind === 'mci') answerMci(v2, headless, m); }, keyboard })(memory);
  fields().set([0, 0, 24576, 0, 0]);
  check(im2.mci_send(0, MCI.OPEN, MCI_FLAG.OPEN_TYPE, block) === 306 && im2.mci_send(1, MCI.PLAY, 0, block) === 257, 'mci: headless 306 and 257');
}

// ---- KeyWiring
const event = (code, timeStamp, extra = {}) => ({ code, timeStamp, keyCode: 0, repeat: false, ...extra });
function wiring(layout) {
  const entries = [];
  const keys = new Uint8Array(256);
  const w = new KeyWiring(new Keyboard(layout), { push: (...a) => entries.push(a), keys });
  return { w, entries, keys };
}
const keyEntries = (entries) => entries.map(([type, scan, vk, flags]) => [type, scan, vk, flags & 3]);
{
  const { w, entries, keys } = wiring('de');
  check(w.keydown(event('KeyZ', 10)) === true, 'wiring: a game key is the game\'s');
  check(same(entries[0], [INPUT.KEY, 0x2c, 0x59, 0, 0, 10]), 'wiring: de KeyZ is scan 2Ch, VK Y');
  check(keys[0x59] === 0x80, 'wiring: the bitmap holds the key');
  w.keyup(event('KeyZ', 12));
  check(same(entries[1], [INPUT.KEY, 0x2c, 0x59, 1, 0, 12]) && keys[0x59] === 0, 'wiring: the key-up');
  check(w.keydown(event('BrowserBack', 13)) === false && entries.length === 2, 'wiring: a key without a scan code stays the browser\'s');

  w.keydown(event('ShiftRight', 20));
  check(keys[0x10] === 0x80 && keys[0xa1] === 0x80 && keys[0xa0] === 0, 'wiring: the generic and the right Shift');
  check((entries.at(-1)[3] & 4) !== 0, 'wiring: the SHIFT flag after the event');
  w.keydown(event('ShiftLeft', 21));
  w.keyup(event('ShiftRight', 22));
  check(keys[0x10] === 0x80 && keys[0xa0] === 0x80 && keys[0xa1] === 0, 'wiring: Shift stays down while the left one is');
  w.keyup(event('ShiftLeft', 23));
  check(keys[0x10] === 0, 'wiring: Shift up with both');
  w.keydown(event('KeyA', 24, { repeat: true }));
  check((entries.at(-1)[3] & 3) === 2, 'wiring: a repeat has the REPEAT flag');

  // Toggles: flipped by the key-down without getModifierState, taken from it with it.
  w.keydown(event('NumLock', 30));
  check((keys[0x90] & 1) === 1, 'wiring: NumLock toggles on');
  w.keyup(event('NumLock', 31));
  w.keydown(event('Numpad7', 32));
  check(entries.at(-1)[2] === 0x67, 'wiring: Numpad7 with Num Lock is VK_NUMPAD7');
  w.keydown(event('Numpad7', 33, { getModifierState: () => false }));
  check((keys[0x90] & 1) === 0 && entries.at(-1)[2] === 0x24, 'wiring: Num Lock off from getModifierState, Numpad7 is VK_HOME');

  w.keydown(event('KeyQ', 40));
  w.blur();
  check(keys[0x51] === 0 && keys[0x41] === 0, 'wiring: blur lets the held keys go');
}
// AltGr, a browser that does not deliver the fake Ctrl: added before the right Alt and its up before the right Alt's up.
{
  const { w, entries, keys } = wiring('de');
  w.keydown(event('AltRight', 100));
  check(same(keyEntries(entries), [[INPUT.KEY, 0x1d, 0x11, 0], [INPUT.KEY, 0x138, 0x12, 0]]), 'altgr: left Ctrl added before the right Alt');
  check(keys[0x11] === 0x80 && keys[0xa2] === 0x80 && keys[0xa5] === 0x80 && keys[0x12] === 0x80, 'altgr: the bitmap holds Ctrl and Alt');
  w.keydown(event('AltRight', 130, { repeat: true }));
  check(same(keyEntries(entries.slice(2)), [[INPUT.KEY, 0x1d, 0x11, 2], [INPUT.KEY, 0x138, 0x12, 2]]), 'altgr: the repeat gets its Ctrl too');
  w.keyup(event('AltRight', 150));
  check(same(keyEntries(entries.slice(4)), [[INPUT.KEY, 0x1d, 0x11, 1], [INPUT.KEY, 0x138, 0x12, 1]]), 'altgr: Ctrl up before the right Alt up');
  check(keys[0x11] === 0 && keys[0x12] === 0, 'altgr: both up');
  check(w.fakeCtrls === 2, 'altgr: two Ctrls added');
}
// AltGr, a browser on Windows: the delivered ControlLeft with the same timestamp is not doubled.
{
  const { w, entries } = wiring('de');
  w.keydown(event('ControlLeft', 200));
  w.keydown(event('AltRight', 200));
  w.keyup(event('ControlLeft', 220));
  w.keyup(event('AltRight', 220));
  check(same(keyEntries(entries), [[INPUT.KEY, 0x1d, 0x11, 0], [INPUT.KEY, 0x138, 0x12, 0], [INPUT.KEY, 0x1d, 0x11, 1], [INPUT.KEY, 0x138, 0x12, 1]]), 'altgr: the delivered Ctrl only');
  check(w.fakeCtrls === 0, 'altgr: none added');
}
// A left Ctrl held by the user (another timestamp) is not doubled either.
{
  const { w, entries } = wiring('de');
  w.keydown(event('ControlLeft', 300));
  w.keydown(event('AltRight', 340));
  check(entries.length === 2 && w.fakeCtrls === 0, 'altgr: no Ctrl added while Ctrl is down');
}
// A layout without AltGr: the right Alt alone.
{
  const { w, entries } = wiring('us');
  w.keydown(event('AltRight', 400));
  w.keyup(event('AltRight', 410));
  check(same(keyEntries(entries), [[INPUT.KEY, 0x138, 0x12, 0], [INPUT.KEY, 0x138, 0x12, 1]]), 'altgr: us has no fake Ctrl');
}

console.log(`${checks - failures.length}/${checks} checks passed`);
if (failures.length) process.exit(1);
