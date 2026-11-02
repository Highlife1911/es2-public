// The page's movie player (docs/web_build.md section 11): the worker's movie_play request
// (es2shell_imports.mjs) names the AVI the shell would open; the page looks it up in
// media/manifest.json (written by tools/web/encode_media.py, section 15), appends the movie's
// video file to an AV1 SourceBuffer and its sound file to an Opus one of one MediaSource, and
// shows the <video> over the canvas at the placed rectangle. The end comes back through
// `notify(code)` as an MCI notification for the input ring.
//
//   const movies = new MoviePlayer({ manifest, fetchBytes, MediaSource, createObjectURL,
//                                    revokeObjectURL, createVideo, attach, detach, notify });
//   reply(seq, movies.play(request, canvasBox, canvasWidth, canvasHeight));   // 1 or 0
//   movies.stop();                                                        // on movie_stop
//
// The browser objects are passed in; the player uses of them only what the fakes of
// web/node/movie_player_test.mjs give: MediaSource (readyState, addSourceBuffer, endOfStream,
// the 'sourceopen' event), SourceBuffer (appendBuffer, 'updateend' and 'error'), the video
// element (src, style, play, pause, removeAttribute, load, 'ended' and 'error').

export const MCI_NOTIFY = { SUCCESSFUL: 1, SUPERSEDED: 2, ABORTED: 4, FAILURE: 8 };
export const OPUS_TYPE = 'audio/webm; codecs="opus"';

// AV1 levels (AV1 specification, Annex A.3): seq_level_idx, MaxPicSize, MaxHSize, MaxVSize.
const AV1_LEVELS = [
  [0, 147456, 2048, 1152], [1, 278784, 2816, 1584], [4, 665856, 4352, 2448], [5, 1065024, 5504, 3096],
  [8, 2359296, 6144, 3456], [12, 8912896, 8192, 4352],
];

// The codecs parameter of an encoded picture size: Main profile, Main tier, 8 bits, the smallest
// level whose picture limits hold it (the movies run at 15 frames per second or less, far below
// every level's display rate).
export function av1Codec(width, height) {
  const level = AV1_LEVELS.find(([, pic, w, h]) => width * height <= pic && width <= w && height <= h) ?? AV1_LEVELS.at(-1);
  return `av01.0.${String(level[0]).padStart(2, '0')}M.08`;
}

export function videoType(entry) {
  const k = entry.supersample ?? 1;
  return `video/webm; codecs="${av1Codec(entry.width * k, entry.height * k)}"`;
}

// The manifest name of the path the shell opens (<CD root>avi\intr_pt1.avi, avf\..., avg\...):
// the AVI folder's movies by their lower-case stem, the AVF and AVG ones under their folder.
export function movieName(path) {
  const parts = String(path).split(/[\\/]+/).filter((p) => p !== '');
  if (parts.length < 2) return null;
  const file = parts[parts.length - 1].toLowerCase(), folder = parts[parts.length - 2].toLowerCase();
  const m = /^(.+)\.avi$/.exec(file);
  if (!m || !['avi', 'avf', 'avg'].includes(folder)) return null;
  return folder === 'avi' ? m[1] : `${folder}/${m[1]}`;
}

// The manifest's entries by name, also under the names of their `also` list (files with the
// same bytes, converted once).
export class MovieIndex {
  constructor(manifest) {
    this.byName = new Map();
    for (const [name, entry] of Object.entries(manifest)) this.byName.set(name, { name, ...entry });
    for (const [name, entry] of Object.entries(manifest)) {
      for (const alias of entry.also ?? []) if (!this.byName.has(alias)) this.byName.set(alias, { name, ...entry });
    }
  }

  // {name, video, audio, width, height, supersample, ...} or null.
  lookup(path) {
    const name = movieName(path);
    return name === null ? null : this.byName.get(name) ?? null;
  }
}

// The video element's CSS box: the rectangle in canvas pixels mapped through the canvas's box
// (getBoundingClientRect), so the movie scales with the canvas.
export function placement(rect, box, canvasWidth, canvasHeight) {
  const sx = box.width / canvasWidth, sy = box.height / canvasHeight;
  return { left: box.left + rect.x * sx, top: box.top + rect.y * sy, width: rect.width * sx, height: rect.height * sy };
}

// What the page appends, in order: the video file to the AV1 buffer, the sound file (if any) to
// the Opus buffer. Both buffers are added before the first append (a MediaSource takes no new
// buffer once it has the first initialization segment).
export function sourcePlan(entry, base = 'media/') {
  const plan = [{ type: videoType(entry), url: base + entry.video }];
  if (entry.audio) plan.push({ type: OPUS_TYPE, url: base + entry.audio });
  return plan;
}

const once = (target, name) => new Promise((resolve) => target.addEventListener(name, resolve, { once: true }));

function append(buffer, bytes) {
  return new Promise((resolve, reject) => {
    const done = () => { buffer.removeEventListener('error', failed); resolve(); };
    const failed = () => { buffer.removeEventListener('updateend', done); reject(new Error('SourceBuffer append failed')); };
    buffer.addEventListener('updateend', done, { once: true });
    buffer.addEventListener('error', failed, { once: true });
    buffer.appendBuffer(bytes);
  });
}

export class MoviePlayer {
  constructor({
    manifest, base = 'media/', fetchBytes, MediaSource, createObjectURL, revokeObjectURL,
    createVideo, attach, detach, notify, log = () => {},
  }) {
    this.index = new MovieIndex(manifest);
    Object.assign(this, { base, fetchBytes, MediaSource, createObjectURL, revokeObjectURL, createVideo, attach, detach, notify, log });
    this.current = null;
  }

  // Starts the movie of a movie_play request ({path, x, y, width, height}); 1 when the manifest
  // has it, 0 otherwise (mci_player_web.c then fails the movie as a failed MCI open does).
  play(request, box, canvasWidth, canvasHeight) {
    this.stop();
    const entry = this.index.lookup(request.path);
    if (!entry) { this.log(`no movie for ${request.path}`); return 0; }
    const movie = { entry, ended: false, abort: new AbortController() };
    this.current = movie;
    const plan = sourcePlan(entry, this.base);
    // The fetches start at once; the appends wait for 'sourceopen'.
    const files = plan.map((p) => this.fetchBytes(p.url, movie.abort.signal));
    files.forEach((f) => f.catch(() => {}));
    const source = new this.MediaSource();
    const video = this.createVideo();
    movie.video = video;
    movie.url = this.createObjectURL(source);
    video.addEventListener('ended', () => this.finish(movie, MCI_NOTIFY.SUCCESSFUL));
    video.addEventListener('error', () => this.finish(movie, MCI_NOTIFY.FAILURE));
    Object.assign(video.style, { position: 'fixed', objectFit: 'fill', imageRendering: 'pixelated' },
      Object.fromEntries(Object.entries(placement(request, box, canvasWidth, canvasHeight)).map(([k, v]) => [k, `${v}px`])));
    movie.loaded = this.load(movie, source, plan, files).catch((e) => {
      this.log(`movie ${entry.name}: ${e.message}`);
      this.finish(movie, MCI_NOTIFY.FAILURE);
    });
    video.src = movie.url;
    this.attach(video);
    Promise.resolve(video.play()).catch((e) => {
      if (this.current === movie) { this.log(`movie ${entry.name}: ${e.message}`); this.finish(movie, MCI_NOTIFY.FAILURE); }
    });
    return 1;
  }

  async load(movie, source, plan, files) {
    if (source.readyState !== 'open') await once(source, 'sourceopen');
    if (this.current !== movie) return;
    const buffers = plan.map((p) => source.addSourceBuffer(p.type));
    await Promise.all(buffers.map(async (buffer, i) => {
      const bytes = await files[i];
      if (this.current === movie) await append(buffer, bytes);
    }));
    if (this.current === movie && source.readyState === 'open') source.endOfStream();
  }

  // The movie's one notification: SUCCESSFUL at its end, FAILURE when it cannot play (the shell's
  // wait loop ends only on SUCCESSFUL, a button or a key, as with a failing MCI driver).
  finish(movie, code) {
    if (this.current !== movie || movie.ended) return;
    movie.ended = true;
    this.notify(code);
  }

  // movie_stop: the element goes and the canvas shows the shell again; nothing is posted after.
  stop() {
    const movie = this.current;
    if (!movie) return;
    this.current = null;
    movie.abort.abort();
    movie.video.pause();
    movie.video.removeAttribute('src');
    movie.video.load();
    this.detach(movie.video);
    this.revokeObjectURL(movie.url);
  }
}
