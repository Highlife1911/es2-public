// The worker's side of es2sim_window's "es2sim" imports in the web build (sim/src/platform_web.c,
// sim/src/audio_web.c, sim/src/os_process_wasm.c) over the shared regions of shared.mjs
// (docs/web_build.md sections 3, 9, 10 and 12). `post(message)` sends a request to the page;
// the requests are
//   {kind: 'cursor', wait}                 SetCursor's shape: true the wait cursor
//   {kind: 'display', fullscreen}          the page's full screen or window
//   {kind: 'message_box', seq, text, title, flags}   answered with reply(seq, button)
//   {kind: 'mci', seq, device, message, flags, fields, strings}   answered by answerMci
// `keyboard` is web/keys.mjs's Keyboard (MapVirtualKeyA and ToAscii); `metrics` the five
// GetSystemMetrics values SM_CXSCREEN, SM_CYSCREEN, SM_CXFRAME, SM_CYFRAME, SM_CYCAPTION.
// Use: Module.es2sim = es2simWindowImports(views(sab), {post, keyboard, metrics}).
//
// The simulation audio ring takes the mixer's buffers as the SDL sink takes them: one format
// (22,050 Hz, stereo, 8 bits), `queued` the bytes not yet played.
import {
  C, INPUT_WORDS, MCI_FIELD_COUNT, resetSimAudio, replyMci, simAudioQueued, takeInput, request as ask,
  writeFrame, writeSimAudio,
} from './shared.mjs';

export const SIM_AUDIO_FORMAT = { rate: 22050, channels: 2, bits: 8 };
export const DEFAULT_METRICS = [640, 480, 4, 4, 18];

const MMSYSERR = { ERROR: 1, ALLOCATED: 4, INVALHANDLE: 5 };
const WAVERR_BADFORMAT = 32;
const MCI_OPEN = 0x803;
const MCI_STRING = 128;   // es2_sim_mci_host's ES2_SIM_MCI_STRING

export function es2simWindowImports(shared, { post = () => {}, keyboard, metrics = DEFAULT_METRICS } = {}) {
  return (memory) => {
    const start = performance.now();
    let audioOpen = false;
    const u8 = () => new Uint8Array(memory());
    // ANSI strings: Windows-1252 bytes, shown as Latin-1; at most `limit` bytes.
    const ansi = (ptr, limit = Infinity) => {
      const bytes = u8();
      let end = ptr;
      while (end < bytes.length && end - ptr < limit && bytes[end]) end++;
      let s = '';
      for (let at = ptr; at < end; at += 4096) s += String.fromCharCode(...bytes.subarray(at, Math.min(end, at + 4096)));
      return s;
    };
    const request = (message) => ask(shared, post, message);
    return {
      key_scan: (vk) => keyboard.vkToScan(vk >>> 0),
      key_translate: (vk) => keyboard.translate(vk >>> 0),

      window_now: () => performance.now() - start,
      window_sleep: (ms) => { Atomics.wait(shared.control, C.SLEEP, Atomics.load(shared.control, C.SLEEP), ms >>> 0); },
      window_take_input: (out, count) => takeInput(shared, new Int32Array(memory(), out >>> 0, count * INPUT_WORDS), count),
      window_present: (ptr, width, height) => {
        writeFrame(shared, u8().subarray(ptr >>> 0, (ptr >>> 0) + width * height * 4), width, height);
      },
      window_cursor: (wait) => post({ kind: 'cursor', wait: wait !== 0 }),
      window_display: (fullscreen) => { post({ kind: 'display', fullscreen: fullscreen !== 0 }); return 1; },
      window_metrics: (out) => { new Uint32Array(memory(), out >>> 0, 5).set(metrics); },
      window_keys: (out) => { u8().set(shared.keys, out >>> 0); },
      window_message_box: (text, caption, style) =>
        request({ kind: 'message_box', text: ansi(text >>> 0), title: ansi(caption >>> 0), flags: style >>> 0 }),

      audio_open: (rate, channels, bits) => {
        if (audioOpen) return MMSYSERR.ALLOCATED;
        if (rate !== SIM_AUDIO_FORMAT.rate || channels !== SIM_AUDIO_FORMAT.channels || bits !== SIM_AUDIO_FORMAT.bits) {
          return WAVERR_BADFORMAT;
        }
        audioOpen = true;
        return 0;
      },
      audio_write: (ptr, bytes) => {
        if (!audioOpen) return MMSYSERR.INVALHANDLE;
        return writeSimAudio(shared, u8().subarray(ptr >>> 0, (ptr >>> 0) + (bytes >>> 0))) ? 0 : MMSYSERR.ERROR;
      },
      audio_queued: () => (audioOpen ? simAudioQueued(shared) : 0),
      audio_reset: () => { if (audioOpen) resetSimAudio(shared); },
      audio_close: () => {
        if (!audioOpen) return;
        resetSimAudio(shared);
        audioOpen = false;
      },

      // mciSendCommandA over the host block of es2_sim_mci_send (five 32-bit fields or none):
      // the page's cdaudio answers and returns the fields; MCI_OPEN's strings (fields 2..4)
      // travel as text.
      mci_send: (device, message, flags, params) => {
        const at = params >>> 0;
        const block = at ? new Uint32Array(memory(), at, MCI_FIELD_COUNT) : null;
        const strings = {};
        if (block && (message >>> 0) === MCI_OPEN) {
          for (let i = 2; i < MCI_FIELD_COUNT; i++) if (block[i]) strings[block[i]] = ansi(block[i], MCI_STRING);
        }
        const error = request({
          kind: 'mci', device: device >>> 0, message: message >>> 0, flags: flags >>> 0,
          fields: block ? Array.from(block) : null, strings,
        });
        if (block) {
          const fields = new Uint32Array(memory(), at, MCI_FIELD_COUNT);
          for (let i = 0; i < MCI_FIELD_COUNT; i++) fields[i] = Atomics.load(shared.control, C.MCI_FIELDS + i);
        }
        return error;
      },
    };
  };
}

// The page's answer to an 'mci' request over web/cdaudio.mjs's CdAudio: the command on the
// device, the fields written back with the MCIERROR.
export function answerMci(shared, cd, m) {
  const block = m.fields ? [...m.fields] : null;
  const error = cd.send(m.device, m.message, m.flags, block, (ptr) => m.strings?.[ptr >>> 0] ?? '');
  replyMci(shared, m.seq, error, block);
  return error;
}
