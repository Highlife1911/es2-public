// A scripted page for es2shell_wasm under Node: the shared buffer of shared.mjs, the module in a
// worker thread (shell_worker.mjs), the input ring filled by the script, the frame slots read
// back, and the worker's requests answered (docs/web_build.md section 3).
//
//   const shell = startShell({module, args, cwd, onRequest});
//   shell.push(INPUT.KEY, scancode, vk, flags) ...    an input entry
//   await shell.frame(afterSeq, ms)                   the newest frame after `afterSeq`
//   await shell.request('movie_play', ms)             the next request of that kind
//   await shell.exited                                the exit code
//
// onRequest(message) answers a message_box or movie_play request with a number; without it,
// or when it returns undefined, a box gets IDOK (1) and a movie 1 (playing). Every request is
// also kept in shell.requests. The worker shares the main thread's current directory, so the
// host changes into `cwd` before the start.
import { Worker } from 'node:worker_threads';
import { create, pushInput, readFrame, reply, C } from '../shared.mjs';

export function startShell({ module, args = [], cwd, onRequest } = {}) {
  if (cwd) process.chdir(cwd);
  const shared = create();
  const worker = new Worker(new URL('./shell_worker.mjs', import.meta.url),
    { workerData: { module, args, sab: shared.sab } });
  const requests = [];
  const waiting = [];
  let resolveExit;
  const exited = new Promise((resolve) => { resolveExit = resolve; });
  worker.on('message', (m) => {
    if (m.kind === 'exit') { resolveExit(m.code); return; }
    requests.push(m);
    if (m.kind === 'message_box' || m.kind === 'movie_play') {
      const answer = onRequest ? onRequest(m) : undefined;
      reply(shared, m.seq, answer ?? 1);
    }
    for (let i = waiting.length - 1; i >= 0; i--) {
      if (waiting[i].kind === m.kind) { waiting[i].resolve(m); waiting.splice(i, 1); }
    }
  });
  worker.on('error', (e) => { console.error(e); resolveExit(1); });

  const push = (type, a = 0, b = 0, c = 0, d = 0) => {
    if (!pushInput(shared, type, a, b, c, d, Math.floor(performance.now()))) throw new Error('input ring full');
  };
  const frame = async (afterSeq = 0, ms = 10000) => {
    const end = performance.now() + ms;
    for (;;) {
      const seq = Atomics.load(shared.control, C.FRAME_SEQ);
      if (seq > afterSeq) return readFrame(shared);
      const left = end - performance.now();
      if (left <= 0) return null;
      await Atomics.waitAsync(shared.control, C.FRAME_SEQ, seq, Math.min(left, 100)).value;
    }
  };
  const request = (kind, ms = 10000) => new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`no ${kind} request within ${ms} ms`)), ms);
    waiting.push({ kind, resolve: (m) => { clearTimeout(timer); resolve(m); } });
  });
  return { shared, worker, requests, exited, push, frame, request };
}
