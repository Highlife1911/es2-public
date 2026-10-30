// web/cdaudio.mjs: each command of DBSIM's MCI contract (docs/reverse-engineering/audio.md, "The
// MCI contract") with the flags and blocks the CD glue sends, and each notification
// (SUCCESSFUL, SUPERSEDED, ABORTED), over a synthetic TOC of cd_tracks.py's shape and a manual
// clock; the headless answers without a TOC; the refusals outside the contract.
import {
  CdAudio, FRAMES_PER_SECOND, MCI, MCIERR, MCI_NOTIFY, msf, tmsf,
} from '../cdaudio.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// A data track and three audio tracks of 10 s, 4.5 s and 61.2 s (frames of 1/75 s).
const TOC = {
  frames_per_second: 75,
  tracks: [
    { number: 1, type: 'data', start: 0, length: 1000 },
    { number: 2, type: 'audio', start: 1000, length: 750, file: 'track02.opus' },
    { number: 3, type: 'audio', start: 1750, length: 338, file: 'track03.opus' },
    { number: 4, type: 'audio', start: 2088, length: 4590, file: 'track04.opus' },
  ],
};
const WINDOW = 0x00010001;   // the callback es2_sim_mci_send hands on (the host's notify window)

function rig(toc = TOC) {
  const r = { time: 100, notes: [], plays: [], stops: 0 };
  r.cd = new CdAudio({
    toc,
    clock: () => r.time,
    player: { play: (p) => r.plays.push(p), stop: () => r.stops++ },
    notify: (code, device, callback) => r.notes.push([code, device, callback]),
  });
  r.strings = new Map([[0x4a0e34, 'cdaudio'], [0x4a0e3c, 'cdaudio'], [0x500, 'avivideo'], [0x600, 'CDAudio']]);
  r.send = (device, message, flags, block) => r.cd.send(device, message, flags, block, (p) => r.strings.get(p));
  // The CD glue's commands (sim/src/audio.c cd_play, cd_status, cd_stop).
  r.open = (type = 0x4a0e34) => { const b = [0, 0, type, 0, 0]; return [r.send(0, MCI.OPEN, 0x2000, b), b[1]]; };
  r.set = (d, format = 10) => r.send(d, MCI.SET, 0x400, [0, format, 0]);
  r.length = (d, track) => { const b = [0, 0, 1, track]; return [r.send(d, MCI.STATUS, 0x112, b), b[1]]; };
  r.position = (d) => { const b = [0, 0, 2, 0]; return [r.send(d, MCI.STATUS, 0x102, b), b[1]]; };
  r.play = (d, from, to, flags = 0xd) => r.send(d, MCI.PLAY, flags, [WINDOW, from, to]);
  r.stop = (d) => r.send(d, MCI.STOP, 0, null);
  r.close = (d) => r.send(d, MCI.CLOSE, 0, null);
  // 0x473c48: open, TMSF, the track's length, PLAY NOTIFY FROM track TO track | length << 8.
  r.start = (track) => {
    const [e, d] = r.open();
    if (e || r.set(d)) return null;
    const [, len] = r.length(d, track);
    return { d, len, e: r.play(d, track, (track | (len & 0xffffff) << 8) >>> 0) };
  };
  return r;
}

// ---- the encodings
check(msf(0) === 0 && msf(750) === (0 | 10 << 8 | 0 << 16) && msf(4590) === (1 | 1 << 8 | 15 << 16), 'MSF: m | s << 8 | f << 16');
check(tmsf(4, 4590) === (4 | 1 << 8 | 1 << 16 | 15 << 24) >>> 0, 'TMSF: t | m << 8 | s << 16 | f << 24');

// ---- without a TOC: the headless answers
{
  const r = rig(null);
  check(same(r.open(), [306, 0]), 'no TOC: MCI_OPEN 306');
  check(r.set(1) === 257 && r.length(1, 2)[0] === 257 && r.position(1)[0] === 257 && r.play(1, 2, 3) === 257 &&
    r.stop(1) === 257 && r.close(1) === 257, 'no TOC: everything else 257');
  check(r.cd.commands === 7 && !r.notes.length, 'no TOC: counted, no notify');
}

// ---- MCI_OPEN, MCI_SET, MCI_CLOSE
{
  const r = rig();
  check(same(r.open(0x500), [306, 0]), 'OPEN of another device type: 306');
  const [e, d] = r.open();
  check(e === 0 && d === 1, 'OPEN "cdaudio": 0, the device id into field 1');
  check(r.open()[0] === MCIERR.MUST_USE_SHAREABLE, 'a second OPEN while open: 291');
  check(r.length(d, 2)[0] === MCIERR.UNSUPPORTED_FUNCTION, 'STATUS before SET TMSF: refused');
  check(r.set(d, 0) === MCIERR.UNSUPPORTED_FUNCTION, 'SET to another time format: refused');
  check(r.set(d + 1) === MCIERR.INVALID_DEVICE_ID && r.set(0) === MCIERR.INVALID_DEVICE_ID, 'SET on another id: 257');
  check(r.set(d) === 0, 'SET TIME_FORMAT TMSF: 0');
  check(r.close(d) === 0 && r.set(d) === MCIERR.INVALID_DEVICE_ID, 'CLOSE: 0, the id is gone');
  const [e2, d2] = r.open(0x600);
  check(e2 === 0 && d2 === 2, 'OPEN after CLOSE: a new id; the type is case-insensitive');
  check(r.length(d2, 2)[0] === MCIERR.UNSUPPORTED_FUNCTION, 'a new open needs SET TMSF again');
  check(r.send(0, MCI.OPEN, 0x1000 | 0x2000, [0, 0, 0x205, 0, 0]) === MCIERR.UNSUPPORTED_FUNCTION &&
    r.send(0, MCI.OPEN, 0x200, [0, 0, 0, 0, 0]) === MCIERR.UNSUPPORTED_FUNCTION, 'OPEN by type id or element: refused');
  check(!r.notes.length && !r.plays.length, 'no notify, no play');
}

// ---- MCI_STATUS LENGTH per track, POSITION
{
  const r = rig();
  const [, d] = r.open();
  r.set(d);
  for (const t of TOC.tracks) check(same(r.length(d, t.number), [0, msf(t.length)]), `STATUS LENGTH track ${t.number}: MSF of ${t.length}`);
  check(r.length(d, 0)[0] === MCIERR.OUTOFRANGE && r.length(d, 5)[0] === MCIERR.OUTOFRANGE, 'STATUS LENGTH of a track outside the TOC: 282');
  check(r.send(d, MCI.STATUS, 0x102, [0, 0, 1, 0]) === MCIERR.UNSUPPORTED_FUNCTION, 'STATUS LENGTH without TRACK: refused');
  check(r.send(d, MCI.STATUS, 0x102, [0, 0, 4, 0]) === MCIERR.UNSUPPORTED_FUNCTION, 'STATUS MODE: refused');
  check(same(r.position(d), [0, tmsf(1, 0)]), 'STATUS POSITION before a play: track 1 at 0');
}

// ---- 0x473c48: the track plays to its end, MM_MCINOTIFY SUCCESSFUL
{
  const r = rig();
  const s = r.start(2);
  check(s && s.e === 0 && s.len === msf(750), 'the CD start: OPEN, SET, STATUS, PLAY answer 0');
  check(r.plays.length === 1 && r.plays[0].from === 1000 && r.plays[0].to === 1750 &&
    same(r.plays[0].segments, [{ track: 2, file: 'track02.opus', offset: 0, duration: 10 }]), 'the player plays track 2 whole');
  check(r.cd.nextEvent() === 110, 'nextEvent: the clock at TO');
  r.time = 104.5;
  check(same(r.position(s.d), [0, tmsf(2, 337)]), 'STATUS POSITION while playing: from the clock');
  r.cd.poll();
  check(!r.notes.length, 'no notify before TO');
  r.time = 110;
  r.cd.poll();
  check(same(r.notes, [[MCI_NOTIFY.SUCCESSFUL, s.d, WINDOW]]), 'TO reached: SUCCESSFUL to the callback with the device id');
  check(r.stops === 1 && r.cd.nextEvent() === null, 'the player is stopped; nothing pending');
  r.time = 200;
  check(same(r.position(s.d), [0, tmsf(3, 0)]), 'the position stays at TO (the next track\'s start)');
  r.cd.poll();
  check(r.notes.length === 1, 'one notify only');
  // The window handler's replay (0x464874 then 0x464858): stop, close, start again.
  check(r.stop(s.d) === 0 && r.close(s.d) === 0 && r.notes.length === 1, 'stop and close after the end: no notify');
  const again = r.start(2);
  check(again.e === 0 && again.d === 2 && r.plays.length === 2, 'the replay opens a new device and plays again');
  r.time = 210;
  check(same(r.position(again.d), [0, tmsf(3, 0)]) && same(r.notes[1], [MCI_NOTIFY.SUCCESSFUL, 2, WINDOW]),
    'a command after TO polls first: the SUCCESSFUL is posted');
}

// ---- STOP: ABORTED; 0x473d44 and 0x473dcc: the pause's position and the resume
{
  const r = rig();
  const s = r.start(4);
  r.time = 100 + 20;
  const [, pos] = r.position(s.d);
  check(pos === tmsf(4, 1500), 'the pause reads the position: track 4 at 20 s');
  check(r.stop(s.d) === 0 && same(r.notes, [[MCI_NOTIFY.ABORTED, s.d, WINDOW]]), 'STOP: ABORTED');
  check(r.close(s.d) === 0 && r.notes.length === 1, 'CLOSE after STOP: no second notify');
  r.time = 500;
  // The resume: OPEN, SET, PLAY FROM the position TO its track | the stored length.
  const [, d] = r.open(0x4a0e3c);
  r.set(d);
  check(r.play(d, pos, ((pos & 0xff) | (s.len & 0xffffff) << 8) >>> 0) === 0, 'the resume plays from the position');
  check(r.plays[1].from === 2088 + 1500 && r.plays[1].to === 2088 + 4590 &&
    same(r.plays[1].segments, [{ track: 4, file: 'track04.opus', offset: 20, duration: (4590 - 1500) / FRAMES_PER_SECOND }]),
  'the player starts track 4 at 20 s');
  r.time = 500 + (4590 - 1500) / FRAMES_PER_SECOND;
  r.cd.poll();
  check(same(r.notes[1], [MCI_NOTIFY.SUCCESSFUL, d, WINDOW]), 'the resumed play ends with SUCCESSFUL');
}

// ---- SUPERSEDED, a PLAY without NOTIFY, CLOSE while playing
{
  const r = rig();
  const s = r.start(2);
  r.time = 102;
  check(r.play(s.d, 3, (3 | msf(338) << 8) >>> 0) === 0, 'a second PLAY NOTIFY: 0');
  check(same(r.notes, [[MCI_NOTIFY.SUPERSEDED, s.d, WINDOW]]), 'the pending notify: SUPERSEDED');
  check(r.plays.length === 2 && r.plays[1].from === 1750 && r.stops === 1, 'the player stops and plays the new range');
  r.time = 102 + 338 / FRAMES_PER_SECOND;
  r.cd.poll();
  check(same(r.notes[1], [MCI_NOTIFY.SUCCESSFUL, s.d, WINDOW]) && r.notes.length === 2, 'the new play ends with SUCCESSFUL');
  r.play(s.d, 2, 3);
  r.time += 1;
  check(r.play(s.d, 4, 5 | 0, 0x4 | 0x8) === MCIERR.OUTOFRANGE, 'PLAY TO beyond the disc: 282');
  check(r.play(s.d, 4, 4, 0x4 | 0x8) === 0 && same(r.notes[2], [MCI_NOTIFY.ABORTED, s.d, WINDOW]),
    'a PLAY without NOTIFY ends the pending play: ABORTED');
  r.play(s.d, 2, 3);
  check(r.close(s.d) === 0 && same(r.notes[3], [MCI_NOTIFY.ABORTED, s.d, WINDOW]), 'CLOSE while playing: ABORTED');
}

// ---- PLAY's ranges and refusals
{
  const r = rig();
  const [, d] = r.open();
  r.set(d);
  check(r.play(d, 3, 2) === MCIERR.OUTOFRANGE, 'FROM after TO: 282');
  check(r.play(d, (2 | 60 << 16) >>> 0, 3) === MCIERR.OUTOFRANGE && r.play(d, (2 | 75 << 24) >>> 0, 3) === MCIERR.OUTOFRANGE,
    'seconds 60 or frames 75: 282');
  check(r.play(d, 9, 2) === MCIERR.OUTOFRANGE, 'a track outside the TOC: 282');
  check(r.play(d, 2, 3, 0xd | 0x2) === MCIERR.UNSUPPORTED_FUNCTION, 'PLAY with WAIT: refused');
  check(!r.notes.length && !r.plays.length, 'refused plays start nothing');
  check(r.play(d, 3, 3) === 0 && same(r.notes, [[MCI_NOTIFY.SUCCESSFUL, d, WINDOW]]), 'FROM = TO: SUCCESSFUL at once');
  check(r.play(d, 0, 0, 0x1 | 0x4) === MCIERR.OUTOFRANGE, 'FROM of track 0: 282');
  check(r.play(d, 4, 0, 0x1 | 0x4) === 0 && r.plays[1].to === 2088 + 4590, 'PLAY without TO: to the disc\'s end');
  check(r.play(d, 2, (3 | 2 << 16) >>> 0) === 0 &&
    same(r.plays[2].segments.map((x) => [x.track, x.offset, x.duration]), [[2, 0, 10], [3, 0, 2]]),
  'a range across a track boundary: both tracks');
  check(r.send(d, 0x809, 0, null) === MCIERR.UNSUPPORTED_FUNCTION && r.send(d, 0x80b, 0, [0, 0, 0]) === MCIERR.UNSUPPORTED_FUNCTION,
    'MCI_PAUSE and other commands: refused');
  check(r.send(0, 0x809, 0, null) === MCIERR.UNSUPPORTED_FUNCTION && r.stop(7) === MCIERR.INVALID_DEVICE_ID,
    'an unknown command on no device: 274; a known one: 257');
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
