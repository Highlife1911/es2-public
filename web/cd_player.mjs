// The audio side of the cdaudio device (docs/web_build.md section 10): web/cdaudio.mjs answers
// the MCI commands and hands `play({from, to, segments})` and `stop()` to this player, which
// plays the tracks' Opus files of cd/ (tools/web/cd_tracks.py, section 15) as AudioBuffers on
// the AudioContext whose clock is the device's clock.
//
//   const music = new CdMusic({ toc, context, load, setTimer, clearTimer, notify });
//   music.send(device, message, flags, block, readString);   // es2_sim_mci_send's answer
//   music.end();                                             // the sim process has ended
//
// `context` is the AudioContext (currentTime, destination, createBufferSource) and `output` the
// node the tracks play into (the context's destination unless given); `load(url)`
// resolves to a decoded AudioBuffer (fetch and decodeAudioData); `setTimer(fn, ms)` and
// `clearTimer(id)` are setTimeout and clearTimeout. The position is the clock's, as CdAudio
// computes it: a track that is decoded late starts at the point the clock has reached, so the
// sound stays at the position STATUS reports and at the TO that ends the play.
import { CdAudio } from './cdaudio.mjs';

// When each segment plays on the context clock: one after the other from `startedAt`. The data
// track (no file) is silence, as an audio CD player plays nothing there.
export function schedule(segments, startedAt) {
  const out = [];
  let when = startedAt;
  for (const s of segments) {
    if (s.file) out.push({ track: s.track, file: s.file, when, offset: s.offset, duration: s.duration });
    when += s.duration;
  }
  return out;
}

// The start(when, offset, duration) of a scheduled segment at clock time `now`: unchanged when
// it lies ahead, else the part from `now` on; null when nothing of it is left.
export function startAt(item, now) {
  if (item.when >= now) return { when: item.when, offset: item.offset, duration: item.duration };
  const late = now - item.when;
  if (late >= item.duration) return null;
  return { when: now, offset: item.offset + late, duration: item.duration - late };
}

export class CdPlayer {
  constructor({ context, output = context.destination, load, base = 'cd/', cacheSize = 2, log = () => {} }) {
    Object.assign(this, { context, output, load, base, cacheSize, log });
    this.cache = new Map();      // file -> Promise<AudioBuffer>, the most recent last
    this.sources = [];
    this.generation = 0;
  }

  buffer(file) {
    let p = this.cache.get(file);
    if (p) {
      this.cache.delete(file);
    } else {
      p = Promise.resolve().then(() => this.load(this.base + file));
      p.catch(() => { if (this.cache.get(file) === p) this.cache.delete(file); });
    }
    this.cache.set(file, p);
    while (this.cache.size > this.cacheSize) this.cache.delete(this.cache.keys().next().value);
    return p;
  }

  play({ segments }) {
    this.stop();
    const generation = this.generation;
    for (const item of schedule(segments, this.context.currentTime)) {
      this.buffer(item.file).then((buffer) => {
        if (generation !== this.generation) return;
        const at = startAt(item, this.context.currentTime);
        if (!at) return;
        const source = this.context.createBufferSource();
        source.buffer = buffer;
        source.connect(this.output);
        source.start(at.when, at.offset, at.duration);
        this.sources.push(source);
      }, (e) => this.log(`cd ${item.file}: ${e.message}`));
    }
  }

  stop() {
    this.generation++;
    for (const source of this.sources) {
      source.stop();
      source.disconnect();
    }
    this.sources = [];
  }
}

// CdAudio over the player and the context clock, with the timer that calls poll() when a play
// reaches TO, so SUCCESSFUL is posted without a further command.
export class CdMusic {
  constructor({ toc, context, output, load, setTimer, clearTimer, notify, base, log }) {
    this.context = context;
    this.player = toc ? new CdPlayer({ context, output, load, base, log }) : null;
    this.cd = new CdAudio({ toc, clock: () => context.currentTime, player: this.player, notify });
    Object.assign(this, { setTimer, clearTimer });
    this.timer = null;
  }

  send(device, message, flags, block, readString) {
    const result = this.cd.send(device, message, flags, block, readString);
    this.arm();
    return result;
  }

  // The process that opened the device has ended: the sound stops and nothing more is posted.
  end() {
    if (this.timer !== null) this.clearTimer(this.timer);
    this.timer = null;
    if (this.player) this.player.stop();
  }

  arm() {
    if (this.timer !== null) this.clearTimer(this.timer);
    this.timer = null;
    const at = this.cd.nextEvent();
    if (at === null) return;
    // The context clock can lag the timer; a timer that comes early polls and arms again.
    const ms = Math.max(0, (at - this.context.currentTime) * 1000);
    this.timer = this.setTimer(() => { this.timer = null; this.cd.poll(); this.arm(); }, Math.ceil(ms) + 1);
  }
}
