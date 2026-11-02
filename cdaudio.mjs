// The cdaudio device of the web platform (docs/web_build.md section 10): the MCI commands of
// DBSIM's CD glue 0x473c00..0x473ea8 (docs/reverse-engineering/audio.md, "The MCI contract")
// over the TOC of cd/toc.json and a playback clock. es2_sim_mci_send hands the platform's `send`
// callback the command, its flags and a host block of the MCI_*_PARMS fields; `send` here
// answers as Windows' cdaudio driver for the contract:
//
//   MCI_OPEN 803h, TYPE "cdaudio"       the device id into field 1
//   MCI_SET 80Dh, TIME_FORMAT TMSF       field 1 = 10
//   MCI_STATUS 814h, ITEM LENGTH | TRACK  field 3 the track; field 1 its length, MSF
//   MCI_STATUS 814h, ITEM POSITION       field 1 the position, TMSF
//   MCI_PLAY 806h, NOTIFY | FROM | TO    field 0 the callback, fields 1 and 2 FROM and TO, TMSF
//   MCI_STOP 808h, MCI_CLOSE 804h        no block
//
// and posts MM_MCINOTIFY through `notify(code, device, callback)`: SUCCESSFUL when TO is
// reached, SUPERSEDED when a new command with NOTIFY replaces a pending notify, ABORTED when
// STOP, CLOSE or a PLAY without NOTIFY ends the pending play. Anything outside the contract
// answers MCIERR_UNSUPPORTED_FUNCTION. Without a TOC the device answers as the headless one:
// MCI_OPEN 306, everything else 257.
//
// The audio side plays what `player.play({from, to, segments})` asks for (frames of 1/75 s on
// the disc; per track the file and the seconds to play) until `player.stop()`; `clock()` is its
// time in seconds (AudioContext.currentTime), from which the position is computed. The host
// calls `poll()` when `nextEvent()`'s time has come, and `send` polls first itself.

export const MCI = { OPEN: 0x803, CLOSE: 0x804, PLAY: 0x806, STOP: 0x808, SET: 0x80d, STATUS: 0x814 };
export const MCI_FLAG = {
  NOTIFY: 0x1, WAIT: 0x2, FROM: 0x4, TO: 0x8, TRACK: 0x10, ITEM: 0x100, TIME_FORMAT: 0x400, OPEN_TYPE: 0x2000,
};
export const MCI_STATUS_ITEM = { LENGTH: 1, POSITION: 2 };
export const MCI_FORMAT_TMSF = 10;
export const MCIERR = {
  INVALID_DEVICE_ID: 257, UNSUPPORTED_FUNCTION: 274, OUTOFRANGE: 282, MUST_USE_SHAREABLE: 291,
  DEVICE_NOT_INSTALLED: 306,
};
export const MCI_NOTIFY = { SUCCESSFUL: 1, SUPERSEDED: 2, ABORTED: 4, FAILURE: 8 };
export const FRAMES_PER_SECOND = 75;

export const msf = (frames) =>
  (Math.floor(frames / 4500) | (Math.floor(frames / 75) % 60) << 8 | (frames % 75) << 16) >>> 0;
export const tmsf = (track, frames) => (track | msf(frames) << 8) >>> 0;

export class CdAudio {
  constructor({ toc = null, clock = () => 0, player = null, notify = () => {} } = {}) {
    this.tracks = toc ? toc.tracks.map((t) => ({ number: t.number, start: t.start, length: t.length, file: t.file ?? null })) : null;
    this.clock = clock;
    this.player = player;
    this.notify = notify;
    this.nextId = 1;
    this.device = 0;             // the open device's id, 0 when none
    this.timeFormat = null;
    this.head = this.tracks ? this.tracks[0].start : 0;   // the position while not playing
    this.play = null;            // {from, to, startedAt}
    this.pending = null;         // {callback, ofPlay} the notify waiting to be posted
    this.commands = 0;
  }

  discEnd() { const t = this.tracks[this.tracks.length - 1]; return t.start + t.length; }
  track(number) { return this.tracks.find((t) => t.number === number); }

  position() {
    if (!this.play) return this.head;
    // The epsilon keeps a clock that reads exactly nextEvent() from falling a frame short.
    const run = Math.floor((this.clock() - this.play.startedAt) * FRAMES_PER_SECOND + 1e-6);
    return Math.min(this.play.to, this.play.from + Math.max(0, run));
  }

  // The disc frame of a TMSF value, or -1 out of range.
  frameOf(value) {
    const t = this.track(value & 0xff), m = (value >>> 8) & 0xff, s = (value >>> 16) & 0xff, f = value >>> 24;
    if (!t || s >= 60 || f >= FRAMES_PER_SECOND) return -1;
    const at = t.start + m * 4500 + s * 75 + f;
    return at <= this.discEnd() ? at : -1;
  }

  // TMSF of a disc frame: the track that holds it, the last one at the disc's end.
  tmsfOf(frame) {
    let t = this.tracks[0];
    for (const x of this.tracks) if (x.start <= frame) t = x;
    return tmsf(t.number, frame - t.start);
  }

  // The clock time at which the play reaches TO, or null.
  nextEvent() {
    return this.play ? this.play.startedAt + (this.play.to - this.play.from) / FRAMES_PER_SECOND : null;
  }

  // Ends a play that has reached TO: the head stays there, a notify of the play is SUCCESSFUL.
  poll() {
    if (!this.play || this.position() < this.play.to) return;
    this.halt(MCI_NOTIFY.SUCCESSFUL);
  }

  // Stops the play at the current position; its pending notify gets `code`.
  halt(code) {
    if (this.play) {
      this.head = this.position();
      this.play = null;
      if (this.player) this.player.stop();
    }
    if (this.pending && this.pending.ofPlay) this.post(code);
  }

  post(code) {
    const p = this.pending;
    this.pending = null;
    this.notify(code, this.device, p.callback);
  }

  // A command with NOTIFY replaces the pending notify (SUPERSEDED); a command without it leaves
  // the pending one.
  requestNotify(flags, block, ofPlay) {
    if (!(flags & MCI_FLAG.NOTIFY)) return;
    if (this.pending) this.post(MCI_NOTIFY.SUPERSEDED);
    this.pending = { callback: block ? block[0] >>> 0 : 0, ofPlay };
  }

  // A command other than PLAY with NOTIFY is done at once.
  doneNotify(flags, block) {
    if (!(flags & MCI_FLAG.NOTIFY)) return;
    this.requestNotify(flags, block, false);
    this.post(MCI_NOTIFY.SUCCESSFUL);
  }

  // es2_sim_mci_host's send: `block` the host block's fields (written in place) or null,
  // `readString(pointer)` the string a field points to. The MCIERROR.
  send(device, message, flags, block, readString = () => '') {
    this.commands++;
    if (!this.tracks) return message === MCI.OPEN ? MCIERR.DEVICE_NOT_INSTALLED : MCIERR.INVALID_DEVICE_ID;
    this.poll();
    const extra = (allowed) => (flags & ~(allowed | MCI_FLAG.NOTIFY | MCI_FLAG.WAIT)) !== 0;
    if (message === MCI.OPEN) {
      if (!block || extra(MCI_FLAG.OPEN_TYPE) || !(flags & MCI_FLAG.OPEN_TYPE)) return MCIERR.UNSUPPORTED_FUNCTION;
      if (String(readString(block[2] >>> 0)).toLowerCase() !== 'cdaudio') return MCIERR.DEVICE_NOT_INSTALLED;
      if (this.device) return MCIERR.MUST_USE_SHAREABLE;
      this.device = this.nextId++;
      this.timeFormat = null;
      block[1] = this.device;
      this.doneNotify(flags, block);
      return 0;
    }
    if (!this.device || device !== this.device) {
      return message in MESSAGES ? MCIERR.INVALID_DEVICE_ID : MCIERR.UNSUPPORTED_FUNCTION;
    }
    switch (message) {
      case MCI.SET:
        if (!block || extra(MCI_FLAG.TIME_FORMAT) || !(flags & MCI_FLAG.TIME_FORMAT) || block[1] !== MCI_FORMAT_TMSF) {
          return MCIERR.UNSUPPORTED_FUNCTION;
        }
        this.timeFormat = MCI_FORMAT_TMSF;
        this.doneNotify(flags, block);
        return 0;
      case MCI.STATUS: {
        if (!block || this.timeFormat !== MCI_FORMAT_TMSF || !(flags & MCI_FLAG.ITEM)) return MCIERR.UNSUPPORTED_FUNCTION;
        const item = block[2];
        if (item === MCI_STATUS_ITEM.LENGTH && (flags & MCI_FLAG.TRACK) && !extra(MCI_FLAG.ITEM | MCI_FLAG.TRACK)) {
          const t = this.track(block[3]);
          if (!t) return MCIERR.OUTOFRANGE;
          block[1] = msf(t.length);
        } else if (item === MCI_STATUS_ITEM.POSITION && !extra(MCI_FLAG.ITEM)) {
          block[1] = this.tmsfOf(this.position());
        } else {
          return MCIERR.UNSUPPORTED_FUNCTION;
        }
        this.doneNotify(flags, block);
        return 0;
      }
      case MCI.PLAY: {
        if (!block || this.timeFormat !== MCI_FORMAT_TMSF || (flags & MCI_FLAG.WAIT) || extra(MCI_FLAG.FROM | MCI_FLAG.TO)) {
          return MCIERR.UNSUPPORTED_FUNCTION;
        }
        const from = flags & MCI_FLAG.FROM ? this.frameOf(block[1] >>> 0) : this.position();
        const to = flags & MCI_FLAG.TO ? this.frameOf(block[2] >>> 0) : this.discEnd();
        if (from < 0 || to < 0 || from > to) return MCIERR.OUTOFRANGE;
        if (flags & MCI_FLAG.NOTIFY) {
          if (this.play) { this.head = this.position(); this.play = null; if (this.player) this.player.stop(); }
          this.requestNotify(flags, block, true);
        } else {
          this.halt(MCI_NOTIFY.ABORTED);
        }
        this.play = { from, to, startedAt: this.clock() };
        if (this.player) this.player.play({ from, to, segments: this.segments(from, to) });
        this.poll();
        return 0;
      }
      case MCI.STOP:
        if (block && extra(0)) return MCIERR.UNSUPPORTED_FUNCTION;
        this.halt(MCI_NOTIFY.ABORTED);
        this.doneNotify(flags, block);
        return 0;
      case MCI.CLOSE:
        if (block && extra(0)) return MCIERR.UNSUPPORTED_FUNCTION;
        this.halt(MCI_NOTIFY.ABORTED);
        this.doneNotify(flags, block);
        this.device = 0;
        this.timeFormat = null;
        return 0;
      default:
        return MCIERR.UNSUPPORTED_FUNCTION;
    }
  }

  // The tracks a play crosses: per track its file (null for the data track), the start within
  // it and the length, in seconds.
  segments(from, to) {
    const out = [];
    for (const t of this.tracks) {
      const a = Math.max(from, t.start), b = Math.min(to, t.start + t.length);
      if (a < b) out.push({ track: t.number, file: t.file, offset: (a - t.start) / FRAMES_PER_SECOND, duration: (b - a) / FRAMES_PER_SECOND });
    }
    return out;
  }
}

const MESSAGES = Object.fromEntries(Object.values(MCI).map((m) => [m, true]));
