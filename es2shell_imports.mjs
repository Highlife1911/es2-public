// The worker's side of es2shell.wasm's "es2shell" imports (shell/src/web/web_imports.h) over the
// shared regions of shared.mjs. `post(message)` sends a request to the page (postMessage); the
// requests are
//   {kind: 'cursor', busy}            {kind: 'fullscreen', on}
//   {kind: 'message_box', seq, text, title, flags}   answered with reply(seq, button)
//   {kind: 'movie_play', seq, path, x, y, width, height}  answered with reply(seq, 1 or 0); the
//                                     movie's end comes back as an MCI input entry
//   {kind: 'movie_stop'}              {kind: 'manual'}   the online manual
// Use: Module.es2shell = es2shellImports(views(sab), post).
import {
  shellSoundSpace, takeInput, waitInput, request as ask, writeFrame, writeShellSound, INPUT_WORDS,
} from './shared.mjs';

export function es2shellImports(shared, post) {
  return (memory) => {
    const start = performance.now();
    const u8 = () => new Uint8Array(memory());
    // ANSI strings: Windows-1252 bytes, shown as Latin-1.
    const ansi = (ptr) => {
      const bytes = u8();
      let end = ptr;
      while (bytes[end]) end++;
      let s = '';
      for (let at = ptr; at < end; at += 4096) s += String.fromCharCode(...bytes.subarray(at, Math.min(end, at + 4096)));
      return s;
    };
    const request = (message) => ask(shared, post, message);
    return {
      now: () => Math.floor(performance.now() - start) >>> 0,
      wait_input: (ms) => (waitInput(shared, ms >>> 0) ? 1 : 0),
      take_input: (out, count) => {
        const words = new Int32Array(memory(), out, count * INPUT_WORDS);
        return takeInput(shared, words, count);
      },
      present: (ptr, width, height) => { writeFrame(shared, u8().subarray(ptr, ptr + width * height * 4), width, height); },
      cursor: (busy) => post({ kind: 'cursor', busy: busy !== 0 }),
      fullscreen: (on) => post({ kind: 'fullscreen', on: on !== 0 }),
      message_box: (text, title, flags) => request({ kind: 'message_box', text: ansi(text), title: ansi(title), flags: flags >>> 0 }),
      movie_play: (path, x, y, width, height) => request({ kind: 'movie_play', path: ansi(path), x, y, width, height }),
      movie_stop: () => post({ kind: 'movie_stop' }),
      manual: () => post({ kind: 'manual' }),
      sound_space: () => shellSoundSpace(shared),
      sound_write: (ptr, frames) => { writeShellSound(shared, new Float32Array(memory(), ptr, frames * 2), frames); },
    };
  };
}
