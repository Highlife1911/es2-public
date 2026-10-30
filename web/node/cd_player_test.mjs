// web/cd_player.mjs over a fake AudioContext and fake decodes: the segment schedule on the
// context clock (data track as silence, tracks one after the other), a late decode starting at
// the clock's point, stop before and after a decode, the decoded-track cache; CdMusic over
// web/cdaudio.mjs: the MCI commands drive the player, the timer posts SUCCESSFUL at TO, STOP
// posts ABORTED and stops the sound; without a TOC the headless answers.
import { MCI, MCIERR, MCI_NOTIFY, msf, tmsf } from '../cdaudio.mjs';
import { CdMusic, CdPlayer, schedule, startAt } from '../cd_player.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const near = (a, b) => Math.abs(a - b) < 1e-9;
const tick = () => new Promise((resolve) => setImmediate(resolve));

// cd_tracks.py's toc.json shape: a data track and three audio tracks of 10 s, 4.5 s and 61.2 s.
const TOC = {
  frames_per_second: 75,
  tracks: [
    { number: 1, type: 'data', start: 0, length: 1000 },
    { number: 2, type: 'audio', start: 1000, length: 750, file: 'track02.opus' },
    { number: 3, type: 'audio', start: 1750, length: 338, file: 'track03.opus' },
    { number: 4, type: 'audio', start: 2088, length: 4590, file: 'track04.opus' },
  ],
};

function fakeContext() {
  const ctx = { currentTime: 50, destination: { node: 'destination' }, sources: [] };
  ctx.createBufferSource = () => {
    const s = { buffer: null, started: null, stopped: false, connected: null, disconnected: false };
    s.connect = (to) => { s.connected = to; };
    s.disconnect = () => { s.disconnected = true; };
    s.start = (when, offset, duration) => { s.started = [when, offset, duration]; };
    s.stop = () => { s.stopped = true; };
    ctx.sources.push(s);
    return s;
  };
  return ctx;
}

// Decodes that resolve when the test says so.
function fakeLoads() {
  const l = { calls: [], pending: new Map() };
  l.load = (url) => new Promise((resolve, reject) => {
    l.calls.push(url);
    l.pending.set(url, { resolve, reject });
  });
  l.finish = async (url) => { l.pending.get(url).resolve({ decoded: url }); l.pending.delete(url); await tick(); };
  l.fail = async (url) => { l.pending.get(url).reject(new Error('decode failed')); l.pending.delete(url); await tick(); };
  return l;
}

// ---- schedule and startAt
{
  const segs = [
    { track: 1, file: null, offset: 12, duration: 1.5 },
    { track: 2, file: 'track02.opus', offset: 0, duration: 10 },
    { track: 3, file: 'track03.opus', offset: 0, duration: 4.5 },
  ];
  check(same(schedule(segs, 100), [
    { track: 2, file: 'track02.opus', when: 101.5, offset: 0, duration: 10 },
    { track: 3, file: 'track03.opus', when: 111.5, offset: 0, duration: 4.5 },
  ]), 'schedule: the data track is silence; the tracks follow each other');
  const item = { when: 10, offset: 2, duration: 5 };
  check(same(startAt(item, 9), { when: 10, offset: 2, duration: 5 }), 'startAt: ahead, unchanged');
  const late = startAt(item, 11.25);
  check(late && late.when === 11.25 && near(late.offset, 3.25) && near(late.duration, 3.75), 'startAt: late, from the clock\'s point');
  check(startAt(item, 15) === null && startAt(item, 16) === null, 'startAt: past its end, nothing');
}

// ---- CdPlayer: two tracks, decodes in either order, the late one shortened
{
  const ctx = fakeContext(), loads = fakeLoads();
  const player = new CdPlayer({ context: ctx, load: loads.load });
  player.play({ from: 1600, to: 1900, segments: [
    { track: 2, file: 'track02.opus', offset: 8, duration: 2 },
    { track: 3, file: 'track03.opus', offset: 0, duration: 2 },
  ] });
  await tick();
  check(same(loads.calls, ['cd/track02.opus', 'cd/track03.opus']), 'player: both files decoded from cd/');
  await loads.finish('cd/track03.opus');
  check(ctx.sources.length === 1 && same(ctx.sources[0].started, [52, 0, 2]) && ctx.sources[0].buffer.decoded === 'cd/track03.opus',
    'player: the second track waits for its time on the clock');
  check(ctx.sources[0].connected === ctx.destination, 'player: sources go to the destination');
  ctx.currentTime = 50.5;
  await loads.finish('cd/track02.opus');
  const first = ctx.sources[1];
  check(first && first.started[0] === 50.5 && near(first.started[1], 8.5) && near(first.started[2], 1.5),
    'player: a late decode starts at the clock\'s point of the track');
  player.stop();
  check(ctx.sources.every((s) => s.stopped && s.disconnected), 'player: stop stops and disconnects every source');
}

// ---- CdPlayer: stop before the decode, a decode entirely late, the cache
{
  const ctx = fakeContext(), loads = fakeLoads();
  const player = new CdPlayer({ context: ctx, load: loads.load });
  const seg = (file, duration = 3) => ({ segments: [{ track: 2, file, offset: 0, duration }] });
  player.play(seg('track02.opus'));
  await tick();
  player.stop();
  await loads.finish('cd/track02.opus');
  check(ctx.sources.length === 0, 'player: a decode that ends after stop plays nothing');
  player.play(seg('track02.opus'));
  await tick();
  check(ctx.sources.length === 1 && loads.calls.length === 1, 'player: a decoded track is taken from the cache');
  player.play(seg('track03.opus', 1));
  await tick();
  ctx.currentTime = 52;
  await loads.finish('cd/track03.opus');
  check(ctx.sources.length === 1, 'player: a decode later than its segment\'s end plays nothing');
  player.play(seg('track04.opus'));
  await tick();
  await loads.finish('cd/track04.opus');
  player.play(seg('track02.opus'));
  await tick();
  check(loads.calls.at(-1) === 'cd/track02.opus' && loads.calls.length === 4, 'player: the cache holds the two most recent tracks');
  await loads.fail('cd/track02.opus');
  player.play(seg('track02.opus'));
  await tick();
  check(loads.calls.length === 5, 'player: a failed decode is not cached');
}

// ---- CdMusic: the MCI commands over the player and the timer
{
  const ctx = fakeContext(), loads = fakeLoads();
  const timers = new Map();
  let nextTimer = 1;
  const notes = [];
  const output = { node: 'gain' };
  const music = new CdMusic({
    toc: TOC, context: ctx, output, load: loads.load,
    setTimer: (fn, ms) => { timers.set(nextTimer, { fn, ms }); return nextTimer++; },
    clearTimer: (id) => timers.delete(id),
    notify: (code, device, callback) => notes.push([code, device, callback]),
  });
  check(music.player.output === output, 'music: the tracks play into the given output');
  const strings = new Map([[0x4a0e34, 'cdaudio']]);
  const send = (d, message, flags, block) => music.send(d, message, flags, block, (p) => strings.get(p));
  const open = [0, 0, 0x4a0e34, 0, 0];
  check(send(0, MCI.OPEN, 0x2000, open) === 0 && open[1] === 1, 'music: OPEN cdaudio');
  const d = open[1];
  check(send(d, MCI.SET, 0x400, [0, 10, 0]) === 0, 'music: SET TMSF');
  const len = [0, 0, 1, 3];
  check(send(d, MCI.STATUS, 0x112, len) === 0 && len[1] === msf(338), 'music: STATUS LENGTH from the TOC');
  check(timers.size === 0, 'music: no timer without a play');
  // 0x473c48: PLAY NOTIFY FROM track 3 TO track 3 | its length.
  check(send(d, MCI.PLAY, 0xd, [0x10001, 3, (3 | msf(338) << 8) >>> 0]) === 0, 'music: PLAY NOTIFY FROM TO');
  await tick();
  check(same(loads.calls, ['cd/track03.opus']), 'music: the track is decoded');
  await loads.finish('cd/track03.opus');
  check(ctx.sources.length === 1 && same(ctx.sources[0].started, [50, 0, 338 / 75]), 'music: the track plays at once, whole');
  check(timers.size === 1 && [...timers.values()][0].ms === Math.ceil(338 / 75 * 1000) + 1, 'music: a timer for TO');
  ctx.currentTime = 52;
  const pos = [0, 0, 2, 0];
  check(send(d, MCI.STATUS, 0x102, pos) === 0 && pos[1] === tmsf(3, 150), 'music: STATUS POSITION from the context clock');
  check(timers.size === 1, 'music: one timer after a further command');
  // The timer comes early (the context clock lags): it arms again.
  ctx.currentTime = 54.5;
  const [[id1, t1]] = [...timers.entries()];
  timers.delete(id1);
  t1.fn();
  check(!notes.length && timers.size === 1, 'music: an early timer polls and arms again');
  ctx.currentTime = 50 + 338 / 75;
  const [[id2, t2]] = [...timers.entries()];
  timers.delete(id2);
  t2.fn();
  check(same(notes, [[MCI_NOTIFY.SUCCESSFUL, d, 0x10001]]), 'music: SUCCESSFUL at TO through the timer');
  check(ctx.sources[0].stopped && timers.size === 0, 'music: the sound stopped, no timer left');
  // A second play, then STOP: ABORTED, the sound stops, the timer goes.
  check(send(d, MCI.PLAY, 0xd, [0x10001, 2, 3]) === 0, 'music: PLAY track 2 to track 3');
  await tick();
  await loads.finish('cd/track02.opus');
  check(timers.size === 1, 'music: timer armed');
  check(send(d, MCI.STOP, 0, null) === 0, 'music: STOP');
  check(same(notes.at(-1), [MCI_NOTIFY.ABORTED, d, 0x10001]) && ctx.sources.at(-1).stopped && timers.size === 0,
    'music: STOP posts ABORTED, stops the sound and the timer');
  check(send(d, MCI.CLOSE, 0, null) === 0, 'music: CLOSE');
  // The process ends during a play: the sound and the timer stop, no notification follows.
  const open2 = [0, 0, 0x4a0e34, 0, 0];
  check(send(0, MCI.OPEN, 0x2000, open2) === 0 && send(open2[1], MCI.SET, 0x400, [0, 10, 0]) === 0,
    'music: OPEN and SET TMSF again');
  check(send(open2[1], MCI.PLAY, 0xd, [0x10001, 2, 3]) === 0, 'music: PLAY track 2 to track 3 again');
  await tick();
  await tick();
  const before = notes.length;
  music.end();
  check(ctx.sources.at(-1).stopped && timers.size === 0 && notes.length === before,
    'music: end stops the sound and the timer without a notification');
}

// ---- CdMusic without a TOC: the headless answers, no player
{
  const music = new CdMusic({ toc: null, context: fakeContext(), load: () => { throw new Error('no load'); },
    setTimer: () => { throw new Error('no timer'); }, clearTimer: () => {}, notify: () => {} });
  check(music.player === null && music.send(0, MCI.OPEN, 0x2000, [0, 0, 1, 0, 0], () => 'cdaudio') === MCIERR.DEVICE_NOT_INSTALLED &&
    music.send(1, MCI.PLAY, 0xd, [0, 2, 3]) === MCIERR.INVALID_DEVICE_ID, 'no TOC: 306 and 257, no player');
}

console.log(`${checks - failures.length}/${checks} checks passed`);
if (failures.length) process.exit(1);
