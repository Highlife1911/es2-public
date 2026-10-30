// web/audio_worklet.mjs over a shared buffer of shared.mjs: U8 and float conversion, the mix and
// its clamp, partial quanta (underrun as silence), an empty ring, counters across the ring's end
// and across the uint32 wrap, the overfull guard, and the processor class with its stats.
import { C, SHELL_SOUND_FRAMES, SIM_AUDIO_BYTES, create, writeShellSound } from '../shared.mjs';
import { defineProcessor, render, takeShellAudio, takeSimAudio } from '../audio_worklet.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const near = (a, b) => Math.abs(a - b) < 1e-6;
const Q = 128;

// The producer side as the worker will write it: bytes at the write counter, then the counter.
function writeSim(v, bytes) {
  const write = Atomics.load(v.control, C.SIM_AUDIO_WRITE) >>> 0;
  for (let i = 0; i < bytes.length; i++) v.simAudio[((write + i) >>> 0) % SIM_AUDIO_BYTES] = bytes[i];
  Atomics.store(v.control, C.SIM_AUDIO_WRITE, (write + bytes.length) | 0);
}
const counters = (v, write, read, at) => { Atomics.store(v.control, write, at | 0); Atomics.store(v.control, read, at | 0); };
const quantum = () => [new Float32Array(Q), new Float32Array(Q)];

// U8 to float: 0 -> -1, 128 -> 0, 255 -> 127/128; left and right interleaved.
{
  const v = create();
  writeSim(v, [0, 255, 128, 64, 192, 128]);
  const [l, r] = quantum();
  const got = render(v, l, r);
  check(got.sim === 3 && got.shell === 0, `u8: three frames taken (${JSON.stringify(got)})`);
  check(near(l[0], -1) && near(r[0], 127 / 128) && near(l[1], 0) && near(r[1], -0.5) && near(l[2], 0.5) && near(r[2], 0), 'u8: (b - 128) / 128 per channel');
  check(l.subarray(3).every((x) => x === 0) && r.subarray(3).every((x) => x === 0), 'u8: the rest of the quantum is silence (underrun)');
  check((Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0) === 6, 'u8: the read counter advanced by six bytes');
  const [l2, r2] = quantum();
  check(render(v, l2, r2).sim === 0 && l2.every((x) => x === 0) && r2.every((x) => x === 0), 'empty ring: silence, nothing taken');
}

// A half frame is left in the ring until its second byte arrives.
{
  const v = create();
  writeSim(v, [200, 100, 50]);
  const [l, r] = quantum();
  check(takeSimAudio(v, l, r, Q) === 1, 'odd fill: one whole frame taken');
  check((Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0) === 2, 'odd fill: the odd byte stays');
  writeSim(v, [150]);
  const [l2, r2] = quantum();
  check(takeSimAudio(v, l2, r2, Q) === 1 && near(l2[0], (50 - 128) / 128) && near(r2[0], (150 - 128) / 128), 'odd fill: the frame completes with the next write');
}

// A full quantum is taken from a fuller ring, the rest stays.
{
  const v = create();
  writeSim(v, Array.from({ length: 2 * 300 }, (_, i) => i & 0xff));
  const [l, r] = quantum();
  check(render(v, l, r).sim === Q, 'fuller ring: one quantum taken');
  check((Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0) === 2 * Q, 'fuller ring: 256 bytes read');
  check(near(l[1], (2 - 128) / 128) && near(r[1], (3 - 128) / 128), 'fuller ring: frames in order');
}

// Across the ring's end and across the uint32 wrap of the counters.
for (const [label, start] of [['ring end', SIM_AUDIO_BYTES - 6], ['uint32 wrap', 2 ** 32 - 6]]) {
  const v = create();
  counters(v, C.SIM_AUDIO_WRITE, C.SIM_AUDIO_READ, start);
  const bytes = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
  writeSim(v, bytes);
  check(v.simAudio[SIM_AUDIO_BYTES - 6] === 10 && v.simAudio[3] === 100, `${label}: the producer wrote across the end`);
  const [l, r] = quantum();
  const n = takeSimAudio(v, l, r, Q);
  const want = bytes.map((b) => (b - 128) / 128);
  check(n === 5 && [0, 1, 2, 3, 4].every((i) => near(l[i], want[2 * i]) && near(r[i], want[2 * i + 1])), `${label}: five frames in order`);
  check((Atomics.load(v.control, C.SIM_AUDIO_READ) >>> 0) === ((start + 10) >>> 0), `${label}: read counter = write counter`);
}

// The shell ring: floats unchanged, across its end and the uint32 wrap.
for (const [label, start] of [['shell', 0], ['shell ring end', SHELL_SOUND_FRAMES - 2], ['shell uint32 wrap', 2 ** 32 - 2]]) {
  const v = create();
  counters(v, C.SOUND_WRITE, C.SOUND_READ, start);
  writeShellSound(v, new Float32Array([0.25, -0.25, 0.5, -0.5, 0.75, -0.75, 1, -1]), 4);
  const [l, r] = quantum();
  const got = render(v, l, r);
  check(got.shell === 4 && got.sim === 0, `${label}: four frames`);
  check(near(l[0], 0.25) && near(r[1], -0.5) && near(l[3], 1) && near(r[3], -1) && l[4] === 0, `${label}: samples in order, then silence`);
  check((Atomics.load(v.control, C.SOUND_READ) >>> 0) === ((start + 4) >>> 0), `${label}: read counter advanced`);
}

// Mix and clamp: sim + shell summed, clamped to [-1, 1].
{
  const v = create();
  writeSim(v, [255, 0, 192, 64]);
  writeShellSound(v, new Float32Array([0.5, -0.5, 0.25, 0.25]), 2);
  const [l, r] = quantum();
  render(v, l, r);
  check(l[0] === 1 && r[0] === -1, 'mix: the sum is clamped');
  check(near(l[1], 0.75) && near(r[1], -0.25), 'mix: the sum within range');
}

// The overfull guard: a fill above the ring's size counts as the ring's size.
{
  const v = create();
  Atomics.store(v.control, C.SIM_AUDIO_WRITE, SIM_AUDIO_BYTES * 3);
  const l = new Float32Array(SIM_AUDIO_BYTES), r = new Float32Array(SIM_AUDIO_BYTES);
  check(takeSimAudio(v, l, r, SIM_AUDIO_BYTES) === SIM_AUDIO_BYTES / 2, 'overfull: at most the ring');
}

// The processor: stereo and mono outputs, underruns counted after data, stats over the port.
{
  const v = create();
  class Base { constructor() { this.port = { posted: [], postMessage(m) { this.posted.push(m); } }; } }
  const Processor = defineProcessor(Base);
  const p = new Processor({ processorOptions: { sab: v.sab } });
  const out = () => [[new Float32Array(Q), new Float32Array(Q)]];
  writeSim(v, new Uint8Array(2 * Q * 2).fill(160));
  check(p.process([], out()) === true, 'processor: process keeps the node alive');
  p.process([], out());
  const third = out();
  p.process([], third);
  check(third[0][0].every((x) => x === 0), 'processor: silence once the ring is empty');
  writeSim(v, new Uint8Array(2 * 10).fill(160));
  p.process([], out());
  p.process([], out());
  const mono = [[new Float32Array(Q)]];
  writeSim(v, [192, 64]);
  p.process([], mono);
  check(near(mono[0][0][0], 0), 'processor: a mono output gets the mean of both channels');
  p.port.onmessage({ data: 'stats' });
  const s = p.port.posted[0];
  check(s && s.quanta === 6 && s.simFrames === 2 * Q + 10 + 1, `processor: stats count quanta and frames (${JSON.stringify(s)})`);
  check(s && s.simUnderruns === 2 && s.shellUnderruns === 0, 'processor: underruns after data (the third and the fifth quantum)');
}

console.log(`${checks - failures.length}/${checks} checks passed`);
if (failures.length) process.exit(1);
