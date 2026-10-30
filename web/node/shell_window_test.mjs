// es2shell_wasm's web platform under the scripted Node host (shell_host.mjs).
//
// 1. The test pattern (no --menu, not headless): a 640 x 480 frame arrives in a slot, mouse and
//    key entries pass, the window's close request ends the process with exit code 0.
// 2. With ES2_DATA_ROOT, the intro movies of -X1 over the MCI player's web host: each one is a
//    movie_play request with the placed rectangle; the first ends on the page's end
//    notification, the second on Space down; each is followed by movie_stop; the main menu's
//    frames follow and the session's end (QUIT) ends the process with exit 0.
//
// Arguments: es2shell_web.js [SCRATCH] (default TEST_TMPDIR)
import fs from 'node:fs';
import path from 'node:path';
import { startShell } from './shell_host.mjs';
import { C, INPUT, MCI_NOTIFY } from '../shared.mjs';

const [module, scratchArg] = process.argv.slice(2);
const scratch = path.resolve(scratchArg || process.env.TEST_TMPDIR || '.');
const modulePath = path.resolve(module);
let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const within = (promise, ms, label) => Promise.race([promise,
  new Promise((_, reject) => setTimeout(() => reject(new Error(`${label}: no answer within ${ms} ms`)), ms))]);

async function pattern() {
  const dir = path.join(scratch, 'pattern');
  fs.mkdirSync(dir, { recursive: true });
  const shell = startShell({ module: modulePath, args: ['dummy'], cwd: dir });
  const colours = (f) => {
    const seen = new Set();
    for (let i = 0; i < f.rgba.length; i += 4 * 97) seen.add(f.rgba[i] | f.rgba[i + 1] << 8 | f.rgba[i + 2] << 16);
    return seen.size;
  };
  // the cleared screen first (WinMain step 8), then the pattern
  let frame = await shell.frame(0, 20000);
  for (let n = 0; frame && colours(frame) < 2 && n < 50; n++) frame = await shell.frame(frame.seq, 200) || frame;
  check(frame && frame.width === 640 && frame.height === 480, `pattern: a 640 x 480 frame (${frame && frame.width} x ${frame && frame.height})`);
  if (frame) {
    check(colours(frame) > 1, `pattern: the pattern's frame has more than one colour (${colours(frame)})`);
    check(frame.rgba.every((v, i) => i % 4 !== 3 || v === 255), 'pattern: every pixel opaque');
  }
  shell.push(INPUT.MOVE, 320, 240);
  shell.push(INPUT.BUTTON, 320, 240, 0, 1);
  shell.push(INPUT.BUTTON, 320, 240, 0, 0);
  shell.push(INPUT.KEY, 0x1e, 0x41, 0);
  shell.push(INPUT.KEY, 0x1e, 0x41, 1);
  shell.push(INPUT.CLOSE);
  const code = await within(shell.exited, 20000, 'pattern exit').catch((e) => { shell.worker.terminate(); return String(e); });
  check(code === 0, `pattern: the close request ends with exit 0 (${code})`);
}

function game(cd) {
  const root = path.join(scratch, 'movies');
  const gameDir = path.join(root, 'game');
  for (const sub of ['DATA', 'SAV', 'VOL']) fs.mkdirSync(path.join(gameDir, sub), { recursive: true });
  fs.writeFileSync(path.join(gameDir, 'DATA', 'language.cfg'), 'G');
  fs.copyFileSync(path.join(cd, 'DATA', 'PREFS.CFG'), path.join(gameDir, 'DATA', 'PREFS.CFG'));
  fs.copyFileSync(path.join(cd, 'SAV', 'GAMEFILE.STR'), path.join(gameDir, 'SAV', 'GAMEFILE.STR'));
  fs.copyFileSync(path.join(cd, 'VOL', 'LANG0.VOL'), path.join(gameDir, 'VOL', 'LANG0.VOL'));
  const menu = path.join(root, 'menu');
  fs.mkdirSync(menu, { recursive: true });
  const lines = ['[Menu]', `game_dir = ${gameDir}`, `cd_root = ${cd}`, 'hangar = lowest-free', 'seed = 12', 'chain_dir = ',
    'online_manual = text'];
  fs.writeFileSync(path.join(menu, 'menu.ini'), lines.join('\r\n') + '\r\n');
  return { gameDir, menu };
}

async function movies(cd) {
  const { gameDir, menu } = game(cd);
  const shell = startShell({ module: modulePath, args: ['--menu', menu, 'dummy', '-eggplant', '-X1', '-G'], cwd: gameDir });
  const inside = (m) => m.x >= 0 && m.y >= 0 && m.width > 0 && m.height > 0 && m.x + m.width <= 640 && m.y + m.height <= 480;
  const first = await shell.request('movie_play', 20000);
  check(/intr_pt1\.avi$/i.test(first.path) && inside(first), `movies: intr_pt1 at a rectangle in the window (${JSON.stringify(first)})`);
  const firstStop = shell.request('movie_stop', 20000);
  shell.push(INPUT.MOVE, 100, 100);
  shell.push(INPUT.MCI, MCI_NOTIFY.ABORTED, 1);
  await new Promise((resolve) => setTimeout(resolve, 500));
  check(!shell.requests.some((m) => m.kind === 'movie_stop'), 'movies: the wait loop holds without an end, a button or a key');
  shell.push(INPUT.MCI, MCI_NOTIFY.SUCCESSFUL, 1);
  await firstStop;
  const second = await shell.request('movie_play', 20000);
  check(/intr_pt2\.avi$/i.test(second.path) && inside(second), `movies: intr_pt2 next (${JSON.stringify(second)})`);
  const secondStop = shell.request('movie_stop', 20000);
  shell.push(INPUT.KEY, 0x39, 0x20, 0);
  shell.push(INPUT.KEY, 0x39, 0x20, 1);
  await secondStop;
  const before = Atomics.load(shell.shared.control, C.FRAME_SEQ);
  const frame = await shell.frame(before, 20000);
  check(frame !== null, 'movies: the main menu presents after the movies');
  shell.push(INPUT.QUIT);
  const code = await within(shell.exited, 20000, 'movies exit').catch((e) => { shell.worker.terminate(); return String(e); });
  check(code === 0, `movies: the session's end ends the process with exit 0 (${code})`);
  const plays = shell.requests.filter((m) => m.kind === 'movie_play').length;
  check(plays === 2, `movies: two movie_play requests (${plays})`);
}

try {
  await pattern();
  const root = process.env.ES2_DATA_ROOT;
  if (root && fs.existsSync(path.join(root, 'VER95', 'VSHELL.EXE'))) await movies(path.resolve(root));
  else console.log('SKIP part 2: no ES2_DATA_ROOT');
} catch (e) {
  check(false, String(e && e.stack || e));
}
console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
