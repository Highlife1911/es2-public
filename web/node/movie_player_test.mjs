// web/movie_player.mjs over fake MediaSource, SourceBuffer and video objects: the manifest
// lookup of the shell's paths (AVI, AVF and AVG, `also` names, the intros' shared video), the
// AV1 codecs parameter, the placement over the scaled canvas, the buffer order (both buffers
// before the first append, each file to its buffer, endOfStream after both), the end and
// failure notifications, and stop.
import { MCI_NOTIFY, MovieIndex, MoviePlayer, OPUS_TYPE, av1Codec, movieName, placement, sourcePlan, videoType } from '../movie_player.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const tick = () => new Promise((resolve) => setImmediate(resolve));

// encode_media.py's manifest.json shape: the AVI intro, its AVG version with the same video and
// its own sound, a movie without sound at 4x supersampling, one with identical copies.
const MANIFEST = {
  'alpha': { also: [], audio: null, bytes: 107346, frames: 19, height: 200, source: 'AVI/ALPHA.AVI', supersample: 4, video: 'alpha.webm', width: 292 },
  'avg/intr_pt1': { also: [], audio: 'avg/intr_pt1.audio.webm', bytes: 521203, frames: 604, height: 180, source: 'AVG/INTR_PT1.AVI', supersample: 1, video: 'intr_pt1.webm', width: 288 },
  'intr_pt1': { also: [], audio: 'intr_pt1.audio.webm', bytes: 6656365, frames: 604, height: 180, source: 'AVI/INTR_PT1.AVI', supersample: 1, video: 'intr_pt1.webm', width: 288 },
  'pt1': { also: ['avf/pt1', 'avg/pt1'], audio: 'pt1.audio.webm', bytes: 1000, frames: 10, height: 180, source: 'AVI/PT1.AVI', supersample: 1, video: 'pt1.webm', width: 240 },
};

class Events {
  constructor() { this.listeners = {}; }
  addEventListener(name, fn, opts) { (this.listeners[name] ??= []).push({ fn, once: opts && opts.once }); }
  removeEventListener(name, fn) { this.listeners[name] = (this.listeners[name] ?? []).filter((l) => l.fn !== fn); }
  fire(name) {
    for (const l of [...(this.listeners[name] ?? [])]) {
      if (l.once) this.removeEventListener(name, l.fn);
      l.fn({ type: name });
    }
  }
}

// ---- the lookup
check(movieName('D:\\CD\\avi\\INTR_PT1.AVI') === 'intr_pt1', 'name: AVI folder, lower case');
check(movieName('C:\\ES2CD\\AVG\\intr_pt1.avi') === 'avg/intr_pt1' && movieName('/cd/avf/PT1.AVI') === 'avf/pt1', 'name: AVF and AVG under their folder');
check(movieName('D:\\avi\\x.txt') === null && movieName('D:\\data\\x.avi') === null && movieName('x.avi') === null, 'name: other files, other folders: none');
const index = new MovieIndex(MANIFEST);
check(index.lookup('D:\\AVI\\INTR_PT1.AVI').audio === 'intr_pt1.audio.webm', 'lookup: the AVI intro with its sound');
const avg = index.lookup('D:\\AVG\\INTR_PT1.AVI');
check(avg.video === 'intr_pt1.webm' && avg.audio === 'avg/intr_pt1.audio.webm', 'lookup: the AVG intro shares the video, has its own sound');
check(index.lookup('D:\\AVF\\INTR_PT1.AVI') === null, 'lookup: a movie the manifest lacks: none');
const copy = index.lookup('D:\\AVF\\PT1.AVI');
check(copy && copy.name === 'pt1' && copy.video === 'pt1.webm', 'lookup: an `also` name gives its entry');

// ---- codecs and the source plan
check(av1Codec(240, 180) === 'av01.0.00M.08' && av1Codec(640, 480) === 'av01.0.04M.08' && av1Codec(1168, 800) === 'av01.0.05M.08' &&
  av1Codec(2400, 1800) === 'av01.0.12M.08' && av1Codec(2100, 100) === 'av01.0.01M.08', 'codec: the smallest level holding the picture');
check(videoType(MANIFEST.alpha) === 'video/webm; codecs="av01.0.05M.08"', 'codec: of the supersampled size (292 x 200 at 4x)');
check(same(sourcePlan(avg), [{ type: 'video/webm; codecs="av01.0.00M.08"', url: 'media/intr_pt1.webm' },
  { type: OPUS_TYPE, url: 'media/avg/intr_pt1.audio.webm' }]), 'plan: the video buffer first, then the sound');
check(sourcePlan(MANIFEST.alpha).length === 1, 'plan: a movie without sound has the video buffer only');

// ---- placement: a 640 x 480 canvas shown at 2x, at (100, 20)
check(same(placement({ x: 160, y: 120, width: 320, height: 240 }, { left: 100, top: 20, width: 1280, height: 960 }, 640, 480),
  { left: 420, top: 260, width: 640, height: 480 }), 'placement: the rectangle scaled and moved with the canvas');

// ---- the player
function rig({ playRejects = false } = {}) {
  const r = { fetches: [], log: [], notes: [], attached: [], detached: [], revoked: [], sources: [], videos: [] };
  class FakeSourceBuffer extends Events {
    constructor(type) { super(); this.type = type; this.appended = null; }
    appendBuffer(bytes) { this.appended = bytes; r.log.push(`append ${this.type === OPUS_TYPE ? 'audio' : 'video'} ${bytes.name}`); }
    finish() { r.log.push(`updateend ${this.type === OPUS_TYPE ? 'audio' : 'video'}`); this.fire('updateend'); }
  }
  class FakeMediaSource extends Events {
    constructor() { super(); this.readyState = 'closed'; this.buffers = []; r.sources.push(this); }
    open() { this.readyState = 'open'; this.fire('sourceopen'); }
    addSourceBuffer(type) { r.log.push(`add ${type === OPUS_TYPE ? 'audio' : 'video'}`); const b = new FakeSourceBuffer(type); this.buffers.push(b); return b; }
    endOfStream() { r.log.push('endOfStream'); this.readyState = 'ended'; }
  }
  r.player = new MoviePlayer({
    manifest: MANIFEST,
    fetchBytes: (url, signal) => new Promise((resolve, reject) => {
      r.fetches.push({ url, signal, resolve: () => resolve({ name: url }), reject: () => reject(new Error(`fetch ${url} failed`)) });
    }),
    MediaSource: FakeMediaSource,
    createObjectURL: (ms) => `blob:${r.sources.indexOf(ms)}`,
    revokeObjectURL: (url) => r.revoked.push(url),
    createVideo: () => {
      const v = new Events();
      Object.assign(v, { style: {}, src: '', paused: true, loads: 0 });
      v.play = () => { v.paused = false; return playRejects ? Promise.reject(new Error('NotAllowedError')) : Promise.resolve(); };
      v.pause = () => { v.paused = true; };
      v.removeAttribute = (name) => { if (name === 'src') v.src = ''; };
      v.load = () => { v.loads++; };
      r.videos.push(v);
      return v;
    },
    attach: (v) => r.attached.push(v),
    detach: (v) => r.detached.push(v),
    notify: (code) => r.notes.push(code),
  });
  r.play = (path) => r.player.play({ path, x: 0, y: 0, width: 288, height: 180 }, { left: 0, top: 0, width: 640, height: 480 }, 640, 480);
  r.fetch = (url) => r.fetches.find((f) => f.url === url);
  return r;
}

// Buffer order: the sound file arrives first, still both buffers precede any append.
{
  const r = rig();
  check(r.play('D:\\AVG\\INTR_PT1.AVI') === 1, 'play: 1 for a movie of the manifest');
  check(same(r.fetches.map((f) => f.url), ['media/intr_pt1.webm', 'media/avg/intr_pt1.audio.webm']), 'play: both files fetched at once');
  const video = r.videos[0], ms = r.sources[0];
  check(video.src === 'blob:0' && r.attached[0] === video && !video.paused, 'play: the element over the MediaSource, attached, playing');
  check(same(video.style, { position: 'fixed', objectFit: 'fill', imageRendering: 'pixelated', left: '0px', top: '0px', width: '288px', height: '180px' }),
    'play: the element at the placed rectangle');
  r.fetch('media/avg/intr_pt1.audio.webm').resolve();
  await tick();
  check(r.log.length === 0, 'play: nothing appended before sourceopen');
  ms.open();
  await tick();
  check(same(r.log, ['add video', 'add audio', 'append audio media/avg/intr_pt1.audio.webm']), 'order: both buffers added, then the first file that arrived');
  r.fetch('media/intr_pt1.webm').resolve();
  await tick();
  check(r.log.at(-1) === 'append video media/intr_pt1.webm', 'order: the video file to the AV1 buffer');
  ms.buffers[1].finish();
  await tick();
  check(!r.log.includes('endOfStream'), 'order: no endOfStream while a buffer updates');
  ms.buffers[0].finish();
  await tick();
  check(r.log.at(-1) === 'endOfStream', 'order: endOfStream after both appends');
  check(ms.buffers[0].type.startsWith('video/webm; codecs="av01') && ms.buffers[1].type === OPUS_TYPE, 'order: AV1 and Opus buffers');
  video.fire('ended');
  video.fire('ended');
  check(same(r.notes, [MCI_NOTIFY.SUCCESSFUL]), 'end: one SUCCESSFUL');
  r.player.stop();
  check(video.paused && video.src === '' && video.loads === 1 && r.detached[0] === video && same(r.revoked, ['blob:0']), 'stop: element emptied, removed, URL revoked');
  check(r.fetches.every((f) => f.signal.aborted), 'stop: the fetches are aborted');
  r.player.stop();
  check(r.detached.length === 1, 'stop: a second stop does nothing');
}

// A movie without sound: one buffer; stop before the end posts nothing.
{
  const r = rig();
  check(r.play('D:\\AVI\\ALPHA.AVI') === 1 && r.fetches.length === 1, 'no sound: one file');
  r.sources[0].open();
  r.fetches[0].resolve();
  await tick();
  r.sources[0].buffers[0].finish();
  await tick();
  check(same(r.log, ['add video', 'append video media/alpha.webm', 'updateend video', 'endOfStream']), 'no sound: one buffer, then endOfStream');
  r.player.stop();
  r.videos[0].fire('ended');
  r.videos[0].fire('error');
  check(r.notes.length === 0, 'stop: no notification after stop');
}

// Not in the manifest: 0 and nothing created.
{
  const r = rig();
  check(r.play('D:\\AVF\\INTR_PT1.AVI') === 0 && !r.videos.length && !r.fetches.length && !r.sources.length, 'missing: 0, nothing created');
}

// Failures: a fetch, an element error, a refused play() give one FAILURE; the next play stops the first.
{
  const r = rig();
  r.play('D:\\AVI\\PT1.AVI');
  r.sources[0].open();
  r.fetches[1].reject();
  await tick();
  check(same(r.notes, [MCI_NOTIFY.FAILURE]), 'failure: a failed fetch posts FAILURE');
  r.videos[0].fire('error');
  r.videos[0].fire('ended');
  check(r.notes.length === 1, 'failure: one notification per movie');
  r.play('D:\\AVI\\INTR_PT1.AVI');
  check(r.detached[0] === r.videos[0] && r.fetches[0].signal.aborted && r.attached.length === 2, 'replay: a new play stops the movie before');
  r.videos[1].fire('error');
  check(same(r.notes, [MCI_NOTIFY.FAILURE, MCI_NOTIFY.FAILURE]), 'failure: an element error posts FAILURE');
  const q = rig({ playRejects: true });
  q.play('D:\\AVI\\INTR_PT1.AVI');
  await tick();
  check(same(q.notes, [MCI_NOTIFY.FAILURE]), 'failure: a refused play() posts FAILURE');
}

console.log(`${checks - failures.length}/${checks} checks passed`);
if (failures.length) process.exit(1);
