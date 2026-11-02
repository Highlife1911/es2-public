// The SharedArrayBuffer between the page (or a Node test host) and the game worker
// (docs/web_build.md section 3). One buffer holds every region; the wasm memory itself is
// not shared, so the worker copies frames and samples in and out.
//
//   control     Int32 words: the wake word, the ring indices, the frame slots' state, the
//               reply slot with the MCI block's fields, focus and visibility
//   keys        the key bitmap, one byte per VK (the asynchronous key state)
//   input       the input ring: INPUT_ENTRIES entries of eight int32 (web_imports.h's
//               es2_web_input: type, a, b, c, d, time, two reserved)
//   frames      two RGBA slots of FRAME_MAX_WIDTH x FRAME_MAX_HEIGHT
//   shellSound  the shell sound ring: interleaved float stereo at 22,050 Hz
//   simAudio    the simulation audio ring: 22,050 Hz stereo U8 frames
//
// Indices are free-running uint32 counters; a ring position is the counter modulo its size.

export const CONTROL_WORDS = 64;
export const C = {
  WAKE: 0,            // incremented and notified by the page after each input entry
  INPUT_WRITE: 1,     // entries written (page)
  INPUT_READ: 2,      // entries taken (worker)
  FRAME_LATEST: 3,    // the slot of the newest frame, -1 before the first
  FRAME_SEQ: 4,       // frames written; notified after each
  SLOT_STATE: 5,      // 5, 6: 0 free, 1 the worker writes, 2 the page reads
  SLOT_WIDTH: 7,      // 7, 8
  SLOT_HEIGHT: 9,     // 9, 10
  REPLY_SEQ: 11,      // the sequence number of the last answered request; notified
  REPLY_VALUE: 12,
  SOUND_WRITE: 13,    // shell sound frames written (worker)
  SOUND_READ: 14,     // shell sound frames played (worklet)
  SIM_AUDIO_WRITE: 15,
  SIM_AUDIO_READ: 16,
  FOCUS: 17,
  VISIBLE: 18,
  SLEEP: 19,          // never notified: Sleep's wait
  MCI_FIELDS: 20,     // 20..24: an MCI request's host block, as the page returns it
};

export const INPUT_ENTRIES = 4096;
export const INPUT_WORDS = 8;
export const FRAME_MAX_WIDTH = 1280;
export const FRAME_MAX_HEIGHT = 960;
export const FRAME_BYTES = FRAME_MAX_WIDTH * FRAME_MAX_HEIGHT * 4;
export const SHELL_SOUND_FRAMES = 8192;
export const SIM_AUDIO_BYTES = 65536;

// START is the page's mark of a process start: the worker drops the ring up to it, so it never
// reaches a guest.
export const INPUT = { KEY: 1, MOVE: 2, BUTTON: 3, FOCUS: 4, EXPOSE: 5, CLOSE: 6, QUIT: 7, MCI: 8, SIZE: 9, WHEEL: 10, START: 11 };
export const MCI_FIELD_COUNT = 5;
export const KEY = { UP: 1, REPEAT: 2, SHIFT: 4, ALT: 8, CTRL: 16, NUMLOCK: 32 };
export const MCI_NOTIFY = { SUCCESSFUL: 1, SUPERSEDED: 2, ABORTED: 4, FAILURE: 8 };

const align = (n) => (n + 63) & ~63;

function layout() {
  let at = 0;
  const take = (bytes) => { const start = at; at = align(at + bytes); return start; };
  const control = take(CONTROL_WORDS * 4);
  const keys = take(256);
  const input = take(INPUT_ENTRIES * INPUT_WORDS * 4);
  const frames = [take(FRAME_BYTES), take(FRAME_BYTES)];
  const shellSound = take(SHELL_SOUND_FRAMES * 2 * 4);
  const simAudio = take(SIM_AUDIO_BYTES);
  return { control, keys, input, frames, shellSound, simAudio, size: at };
}

export const LAYOUT = layout();

// The views of one buffer; `create()` makes a fresh one, `views(sab)` wraps a received one.
export function views(sab) {
  const l = LAYOUT;
  return {
    sab,
    control: new Int32Array(sab, l.control, CONTROL_WORDS),
    keys: new Uint8Array(sab, l.keys, 256),
    input: new Int32Array(sab, l.input, INPUT_ENTRIES * INPUT_WORDS),
    frames: l.frames.map((offset) => new Uint8Array(sab, offset, FRAME_BYTES)),
    shellSound: new Float32Array(sab, l.shellSound, SHELL_SOUND_FRAMES * 2),
    simAudio: new Uint8Array(sab, l.simAudio, SIM_AUDIO_BYTES),
  };
}

export function create() {
  const v = views(new SharedArrayBuffer(LAYOUT.size));
  Atomics.store(v.control, C.FRAME_LATEST, -1);
  Atomics.store(v.control, C.FOCUS, 1);
  Atomics.store(v.control, C.VISIBLE, 1);
  return v;
}

// ---- The page's side -------------------------------------------------------------------------

// Appends one input entry ([type, a, b, c, d] and the time) and wakes the worker. Returns false
// when the ring is full (the entry is dropped, as a full message queue drops).
export function pushInput(v, type, a = 0, b = 0, c = 0, d = 0, time = 0) {
  const write = Atomics.load(v.control, C.INPUT_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.INPUT_READ) >>> 0;
  if (((write - read) >>> 0) >= INPUT_ENTRIES) return false;
  const at = (write % INPUT_ENTRIES) * INPUT_WORDS;
  v.input.set([type, a, b, c, d, time | 0, 0, 0], at);
  Atomics.store(v.control, C.INPUT_WRITE, (write + 1) | 0);
  Atomics.add(v.control, C.WAKE, 1);
  Atomics.notify(v.control, C.WAKE);
  return true;
}

// The newest frame as {width, height, rgba (a copy), seq}, or null before the first. seq is read
// before the slot: the worker counts a frame after publishing its slot, so the copy is at least
// as new as seq. Read after it, seq could count a frame newer than the copy, and the page would
// show the older picture until the next one.
export function readFrame(v) {
  for (;;) {
    const seq = Atomics.load(v.control, C.FRAME_SEQ);
    const slot = Atomics.load(v.control, C.FRAME_LATEST);
    if (slot < 0) return null;
    if (Atomics.compareExchange(v.control, C.SLOT_STATE + slot, 0, 2) !== 0) continue;
    const width = v.control[C.SLOT_WIDTH + slot], height = v.control[C.SLOT_HEIGHT + slot];
    const rgba = v.frames[slot].slice(0, width * height * 4);
    Atomics.store(v.control, C.SLOT_STATE + slot, 0);
    return { width, height, rgba, seq };
  }
}

// Answers request `seq` of the worker with `value`.
export function reply(v, seq, value) {
  Atomics.store(v.control, C.REPLY_VALUE, value | 0);
  Atomics.store(v.control, C.REPLY_SEQ, seq | 0);
  Atomics.notify(v.control, C.REPLY_SEQ);
}

// Answers MCI request `seq` with its MCIERROR and the host block's fields (null: none).
export function replyMci(v, seq, error, fields) {
  if (fields) for (let i = 0; i < MCI_FIELD_COUNT; i++) Atomics.store(v.control, C.MCI_FIELDS + i, fields[i] | 0);
  reply(v, seq, error);
}

// Takes up to `frames` shell sound frames (interleaved stereo floats), as the worklet does.
export function takeShellSound(v, frames) {
  const write = Atomics.load(v.control, C.SOUND_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.SOUND_READ) >>> 0;
  const n = Math.min(frames, (write - read) >>> 0);
  const out = new Float32Array(n * 2);
  for (let i = 0; i < n; i++) {
    const at = ((read + i) % SHELL_SOUND_FRAMES) * 2;
    out[2 * i] = v.shellSound[at];
    out[2 * i + 1] = v.shellSound[at + 1];
  }
  Atomics.store(v.control, C.SOUND_READ, (read + n) | 0);
  return out;
}

// Takes up to `bytes` simulation audio bytes (whole stereo frames), as the worklet does; the
// Node hosts play them this way.
export function takeSimAudio(v, bytes) {
  const write = Atomics.load(v.control, C.SIM_AUDIO_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0;
  const n = Math.min(bytes, (write - read) >>> 0) & ~1;
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i++) out[i] = v.simAudio[(read + i) % SIM_AUDIO_BYTES];
  Atomics.store(v.control, C.SIM_AUDIO_READ, (read + n) | 0);
  return out;
}

// ---- The worker's side -----------------------------------------------------------------------

export function inputPending(v) {
  return ((Atomics.load(v.control, C.INPUT_WRITE) - Atomics.load(v.control, C.INPUT_READ)) >>> 0) > 0;
}

// Waits up to `ms` for input; true when the ring is not empty.
export function waitInput(v, ms) {
  if (inputPending(v)) return true;
  const wake = Atomics.load(v.control, C.WAKE);
  if (inputPending(v)) return true;
  if (ms > 0) Atomics.wait(v.control, C.WAKE, wake, ms);
  return inputPending(v);
}

// Copies up to `count` entries into `out` (an Int32Array of count * 8 words); the number taken.
export function takeInput(v, out, count) {
  const write = Atomics.load(v.control, C.INPUT_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.INPUT_READ) >>> 0;
  const n = Math.min(count, (write - read) >>> 0);
  for (let i = 0; i < n; i++) {
    const at = ((read + i) % INPUT_ENTRIES) * INPUT_WORDS;
    out.set(v.input.subarray(at, at + INPUT_WORDS), i * INPUT_WORDS);
  }
  Atomics.store(v.control, C.INPUT_READ, (read + n) | 0);
  return n;
}

// Writes an RGBA picture into a free slot: the older one, or the newest when the page reads the
// older. The page never holds both, so one of them is free.
export function writeFrame(v, rgba, width, height) {
  const latest = Atomics.load(v.control, C.FRAME_LATEST);
  const order = latest < 0 ? [0, 1] : [latest ^ 1, latest];
  for (const slot of order) {
    if (Atomics.compareExchange(v.control, C.SLOT_STATE + slot, 0, 1) !== 0) continue;
    v.frames[slot].set(rgba.subarray(0, width * height * 4));
    v.control[C.SLOT_WIDTH + slot] = width;
    v.control[C.SLOT_HEIGHT + slot] = height;
    Atomics.store(v.control, C.SLOT_STATE + slot, 0);
    Atomics.store(v.control, C.FRAME_LATEST, slot);
    Atomics.add(v.control, C.FRAME_SEQ, 1);
    Atomics.notify(v.control, C.FRAME_SEQ);
    return true;
  }
  return false;
}

// Waits for the page's answer to request `seq`.
export function waitReply(v, seq) {
  for (;;) {
    const got = Atomics.load(v.control, C.REPLY_SEQ);
    if (got === seq) return Atomics.load(v.control, C.REPLY_VALUE);
    Atomics.wait(v.control, C.REPLY_SEQ, got, 1000);
  }
}

// Posts a request with the next sequence number and waits for its answer. The worker waits for
// every answer, so the last answered number is the last one used, by this process or the one
// before it in the same worker: a new process never takes a stale answer for its own.
export function request(v, post, message) {
  const seq = (Atomics.load(v.control, C.REPLY_SEQ) + 1) | 0;
  post({ ...message, seq });
  return waitReply(v, seq);
}

export function shellSoundSpace(v) {
  const write = Atomics.load(v.control, C.SOUND_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.SOUND_READ) >>> 0;
  return SHELL_SOUND_FRAMES - ((write - read) >>> 0);
}

export function writeShellSound(v, samples, frames) {
  const write = Atomics.load(v.control, C.SOUND_WRITE) >>> 0;
  for (let i = 0; i < frames; i++) {
    const at = ((write + i) % SHELL_SOUND_FRAMES) * 2;
    v.shellSound[at] = samples[2 * i];
    v.shellSound[at + 1] = samples[2 * i + 1];
  }
  Atomics.store(v.control, C.SOUND_WRITE, (write + frames) | 0);
}

// The simulation audio ring's fill in bytes (the sink's `queued`).
export function simAudioQueued(v) {
  return (Atomics.load(v.control, C.SIM_AUDIO_WRITE) - Atomics.load(v.control, C.SIM_AUDIO_READ)) >>> 0;
}

// Appends one buffer of the mixer whole; false when the ring lacks the space (nothing written).
export function writeSimAudio(v, bytes) {
  const write = Atomics.load(v.control, C.SIM_AUDIO_WRITE) >>> 0;
  if (bytes.length > SIM_AUDIO_BYTES - simAudioQueued(v)) return false;
  for (let i = 0; i < bytes.length; i++) v.simAudio[(write + i) % SIM_AUDIO_BYTES] = bytes[i];
  Atomics.store(v.control, C.SIM_AUDIO_WRITE, (write + bytes.length) | 0);
  return true;
}

// waveOutReset: drops what is queued by moving the read counter to the write counter (the one
// write of SIM_AUDIO_READ from this side; a take that runs at the same time ends at most one
// quantum later).
export function resetSimAudio(v) {
  Atomics.store(v.control, C.SIM_AUDIO_READ, Atomics.load(v.control, C.SIM_AUDIO_WRITE));
}
