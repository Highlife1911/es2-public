// The AudioWorklet of the game's sound (docs/web_build.md section 10), in an
// AudioContext({sampleRate: 22050}): each render quantum takes what the two rings of the shared
// buffer (shared.mjs) hold and mixes it to the stereo output.
//
//   simulation audio ring  SIM_AUDIO_BYTES bytes of interleaved U8 stereo (format 0x29); the
//                          counters SIM_AUDIO_WRITE and SIM_AUDIO_READ count bytes, the unit of
//                          the SDL sink's `queued`
//   shell sound ring       SHELL_SOUND_FRAMES frames of interleaved float stereo; SOUND_WRITE and
//                          SOUND_READ count frames
//
// The counters are free-running uint32 (stored as int32); the fill is (write - read) mod 2^32
// and a position the counter modulo the ring's size (both sizes are powers of two, so the
// position runs on across the counter's wrap). The worklet takes whole frames, at most a
// quantum's worth and never more than the ring holds; what is missing (an underrun) is silence,
// and nothing is waited for. U8 becomes (b - 128) / 128 as SDL3 converts it; the two streams
// are summed and clamped to [-1, 1], as SDL3 mixes bound streams.
//
//   await context.audioWorklet.addModule('audio.js');   // its name on the site
//   new AudioWorkletNode(context, PROCESSOR, { outputChannelCount: [2], processorOptions: { sab } });
//
// The node's port answers 'stats' with {quanta, simFrames, shellFrames, simUnderruns,
// shellUnderruns}: an underrun is a quantum that got less than it needed from a ring that held
// data in the quantum before.
import { C, SHELL_SOUND_FRAMES, SIM_AUDIO_BYTES, views } from './shared.mjs';

export const PROCESSOR = 'es2-audio';

// Adds up to `frames` frames of the simulation ring to `left` and `right`; the frames taken.
export function takeSimAudio(v, left, right, frames) {
  const write = Atomics.load(v.control, C.SIM_AUDIO_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0;
  const fill = Math.min((write - read) >>> 0, SIM_AUDIO_BYTES);
  const n = Math.min(frames, Math.floor(fill / 2));
  for (let i = 0; i < n; i++) {
    const at = (read + 2 * i) >>> 0;
    left[i] += (v.simAudio[at % SIM_AUDIO_BYTES] - 128) / 128;
    right[i] += (v.simAudio[(at + 1) % SIM_AUDIO_BYTES] - 128) / 128;
  }
  Atomics.store(v.control, C.SIM_AUDIO_READ, (read + 2 * n) | 0);
  return n;
}

// Adds up to `frames` frames of the shell sound ring to `left` and `right`; the frames taken.
export function takeShellAudio(v, left, right, frames) {
  const write = Atomics.load(v.control, C.SOUND_WRITE) >>> 0;
  const read = Atomics.load(v.control, C.SOUND_READ) >>> 0;
  const n = Math.min(frames, (write - read) >>> 0, SHELL_SOUND_FRAMES);
  for (let i = 0; i < n; i++) {
    const at = (((read + i) >>> 0) % SHELL_SOUND_FRAMES) * 2;
    left[i] += v.shellSound[at];
    right[i] += v.shellSound[at + 1];
  }
  Atomics.store(v.control, C.SOUND_READ, (read + n) | 0);
  return n;
}

const clamp = (x) => (x > 1 ? 1 : x < -1 ? -1 : x);

// One quantum: silence, both rings added, clamped. {sim, shell}: the frames taken from each.
export function render(v, left, right) {
  left.fill(0);
  right.fill(0);
  const sim = takeSimAudio(v, left, right, left.length);
  const shell = takeShellAudio(v, left, right, left.length);
  for (let i = 0; i < left.length; i++) { left[i] = clamp(left[i]); right[i] = clamp(right[i]); }
  return { sim, shell };
}

// The processor class over the worklet's AudioWorkletProcessor (a plain class in the tests).
export function defineProcessor(Base) {
  return class extends Base {
    constructor(options) {
      super(options);
      this.shared = views(options.processorOptions.sab);
      this.stats = { quanta: 0, simFrames: 0, shellFrames: 0, simUnderruns: 0, shellUnderruns: 0 };
      this.had = { sim: false, shell: false };
      this.port.onmessage = (e) => { if (e.data === 'stats') this.port.postMessage({ ...this.stats }); };
    }

    process(inputs, outputs) {
      const out = outputs[0];
      const left = out[0], right = out[1] ?? new Float32Array(left.length);
      const got = render(this.shared, left, right);
      if (out.length === 1) for (let i = 0; i < left.length; i++) left[i] = (left[i] + right[i]) / 2;
      const s = this.stats;
      s.quanta++;
      s.simFrames += got.sim;
      s.shellFrames += got.shell;
      if (this.had.sim && got.sim < left.length) s.simUnderruns++;
      if (this.had.shell && got.shell < left.length) s.shellUnderruns++;
      this.had = { sim: got.sim > 0, shell: got.shell > 0 };
      return true;
    }
  };
}

if (typeof registerProcessor === 'function' && typeof AudioWorkletProcessor === 'function') {
  registerProcessor(PROCESSOR, defineProcessor(AudioWorkletProcessor));
}
