// The browser feature checks of the landing page (docs/web_build.md section 13, step 1): each
// needs no game. A failed check has a text key in texts.mjs; the page names every failed
// check and does not start the game.
//
//   const results = await runChecks(globalThis);   // [{ id, ok, detail }] in CHECKS order
//
// `env` is the page's global object or a fake with the same members (web/node/checks_test.mjs).
import { OPUS_TYPE, av1Codec } from './movie_player.mjs';
import { FRAME_MAX_HEIGHT, FRAME_MAX_WIDTH } from './shared.mjs';

// The AV1 type of the largest picture a movie can have (a frame slot's maximum; the
// supersampled movies stay inside it) at the movies' 15 frames per second.
export const AV1_TYPE = `video/webm; codecs="${av1Codec(FRAME_MAX_WIDTH, FRAME_MAX_HEIGHT)}"`;
export const AV1_CONFIGURATION = {
  type: 'media-source',
  video: { contentType: AV1_TYPE, width: FRAME_MAX_WIDTH, height: FRAME_MAX_HEIGHT, bitrate: 2_000_000, framerate: 15 },
};

// The smallest valid module: the magic and version 1.
const WASM_HEADER = Uint8Array.of(0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00);
const PROBE_DB = 'es2-probe';

function sharedArrayBuffer(env) {
  if (typeof env.SharedArrayBuffer !== 'function') return 'no SharedArrayBuffer';
  if (env.crossOriginIsolated !== true) return 'not cross-origin isolated';
  try {
    new env.SharedArrayBuffer(8);
  } catch (e) {
    return String(e?.message ?? e);
  }
  return '';
}

function audioWorklet(env) {
  if (typeof env.AudioWorkletNode !== 'function') return 'no AudioWorkletNode';
  const context = env.AudioContext ?? env.webkitAudioContext;
  if (typeof context !== 'function' || !('audioWorklet' in context.prototype)) return 'no AudioContext.audioWorklet';
  return '';
}

function webAssembly(env) {
  const wasm = env.WebAssembly;
  if (typeof wasm !== 'object' || wasm === null) return 'no WebAssembly';
  try {
    return wasm.validate(WASM_HEADER) ? '' : 'WebAssembly.validate refuses a module';
  } catch (e) {
    return String(e?.message ?? e);
  }
}

// Opens and deletes a probe database: a browser can offer the object and still refuse open().
function indexedDB(env) {
  const idb = env.indexedDB;
  if (!idb || typeof idb.open !== 'function') return Promise.resolve('no indexedDB');
  return new Promise((resolve) => {
    let request;
    try {
      request = idb.open(PROBE_DB, 1);
    } catch (e) {
      resolve(String(e?.message ?? e));
      return;
    }
    request.onsuccess = () => {
      request.result.close();
      try { idb.deleteDatabase(PROBE_DB); } catch { /* the probe stays; it holds nothing */ }
      resolve('');
    };
    request.onerror = () => resolve(String(request.error?.message ?? request.error ?? 'open failed'));
    request.onblocked = () => resolve('open blocked');
  });
}

async function av1(env) {
  const capabilities = env.navigator?.mediaCapabilities;
  if (!capabilities || typeof capabilities.decodingInfo !== 'function') return 'no navigator.mediaCapabilities';
  try {
    const info = await capabilities.decodingInfo(AV1_CONFIGURATION);
    return info?.supported ? '' : `${AV1_TYPE} not supported`;
  } catch (e) {
    return String(e?.message ?? e);
  }
}

function mediaSourceType(type) {
  return (env) => {
    const ms = env.MediaSource ?? env.ManagedMediaSource;
    if (!ms || typeof ms.isTypeSupported !== 'function') return 'no MediaSource';
    return ms.isTypeSupported(type) ? '' : `${type} not supported`;
  };
}

// Each check gives '' when it passes, else what is missing.
export const CHECKS = [
  { id: 'sharedArrayBuffer', text: 'check.sharedArrayBuffer', run: sharedArrayBuffer },
  { id: 'audioWorklet', text: 'check.audioWorklet', run: audioWorklet },
  { id: 'webAssembly', text: 'check.webAssembly', run: webAssembly },
  { id: 'indexedDB', text: 'check.indexedDB', run: indexedDB },
  { id: 'av1', text: 'check.av1', run: av1 },
  { id: 'mseAv1', text: 'check.mseAv1', run: mediaSourceType(AV1_TYPE) },
  { id: 'mseOpus', text: 'check.mseOpus', run: mediaSourceType(OPUS_TYPE) },
];

export async function runChecks(env) {
  const results = [];
  for (const c of CHECKS) {
    let detail;
    try {
      detail = await c.run(env);
    } catch (e) {
      detail = String(e?.message ?? e);
    }
    results.push({ id: c.id, text: c.text, ok: detail === '', detail });
  }
  return results;
}

// The text keys of the failed checks, in CHECKS order.
export function failedTexts(results) {
  return results.filter((r) => !r.ok).map((r) => r.text);
}
