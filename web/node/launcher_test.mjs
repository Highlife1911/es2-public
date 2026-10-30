// web/launcher.mjs over launch.wasm: the memory check with the recorded REFERENCE_MEMORY and the
// native cases of launch_logic_test, CommandLineToArgvW's split, and the loop's decisions with
// scripted children (the commands es2launch.exe builds, after_replacement, the counter, the
// messages of a missing error.str and a failed memory check).
//
// Arguments: launch_web.js
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { loadLaunch, runLauncher, splitCommandLine, REFERENCE_MEMORY } from '../launcher.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const factory = (await import(pathToFileURL(path.resolve(process.argv[2])).href)).default;
const launch = await loadLaunch(factory);
const { m } = launch;

const r = REFERENCE_MEMORY;
check(m._es2l_memory_check(r.totalPhys, r.totalPageFile, r.availPageFile, r.allocated) === -1,
  'memory: the recorded reference values pass');
const big = 0xffffffff, n = 20000000;
const cases = [
  [[0x739fff, big, big, n], 0], [[0x73a000, 0, big, n], 1], [[0x73a000, 0x7a11ff, big, n], 2],
  [[0x73a000, 0x7a1200, 0, 12000000], -1], [[0x73a000, 0x7a1200, 0, 11999999], 2], [[0xdabfff, big, 0, 12000000], -1],
  [[0xdac000, big, 0, 12000000], 2], [[0xdac000, big, 0, 18000000], -1], [[0x7fffffff, 0x7fffffff, 0x7fffffff, n], 2],
  [[0x7fffffff, 0x7fffffff, 0x7fffffff - n, n], -1],
];
for (const [a, want] of cases) check(m._es2l_memory_check(...a) === want, `memory: ${a.join(' ')} -> ${want}`);

const splits = [
  ['a b  c', ['a', 'b', 'c']],
  ['"D:\\a b\\x.exe" --menu "D:\\m 1" dummy', ['D:\\a b\\x.exe', '--menu', 'D:\\m 1', 'dummy']],
  ['"a\\\\" b', ['a\\', 'b']], ['a\\\\\\"b', ['a\\"b']], ['a\\b c\\\\d', ['a\\b', 'c\\\\d']], ['"" x', ['', 'x']],
  ['--arguments "-eggplant -X2 -R0 -G"', ['--arguments', '-eggplant -X2 -R0 -G']], ['"a""b"', ['a"b']],
];
for (const [line, want] of splits) check(same(splitCommandLine(line), want), `split: ${line} -> ${JSON.stringify(splitCommandLine(line))}`);

// error.str as the route tests write it: group 0 "m0".."m2", group 1 "c"
const body = [];
const h = (v) => body.push(v & 0xff, (v >> 8) & 0xff);
h(3);
for (let i = 0; i < 3; i++) { h(3); body.push(0x6d, 0x30 + i, 0, 0); }
h(1); h(2); body.push(0x63, 0, 0);
const errorStr = Uint8Array.from([body.length & 0xff, body.length >> 8, 0, 0, ...body]);

async function loop(host, codes) {
  const lines = [], runs = [];
  const run = (argv, command) => { runs.push([argv, command]); return codes.length ? codes.shift() : 0; };
  const state = await runLauncher(launch, { errorStr, runShell: run, runSim: run, log: (l) => lines.push(l), ...host });
  return { state, lines, runs };
}

{
  const { lines, runs } = await loop({ commandLine: '', languageByte: 0x47, shellPrefix: '"D:\\s 1\\sh.exe" --menu M',
    simPrefix: 'sim.exe --headless' }, [2, 3, 5, 0, 64]);
  check(same(runs.map((x) => x[1]), [
    '"D:\\s 1\\sh.exe" --menu M dummy -eggplant -X1 -G',
    'sim.exe --headless --arguments "-eggplant -X2 -R0 -G"',
    '"D:\\s 1\\sh.exe" --menu M dummy -eggplant -X3 -G',
    'sim.exe --headless --arguments "-eggplant -X5 -R1 -D -G"',
  ]), `loop: shell, sim, shell, sim (${JSON.stringify(runs.map((x) => x[1]))})`);
  check(same(runs[0][0], ['D:\\s 1\\sh.exe', '--menu', 'M', 'dummy', '-eggplant', '-X1', '-G']), 'loop: the shell argv');
  check(same(lines.slice(-3), ['start sim-replacement sim.exe --headless --arguments "-eggplant -X5 -R1 -D -G"',
    'exit sim-replacement 0', 'end 0']), `loop: DBSIM's 0 ends (${JSON.stringify(lines)})`);
}
{
  // the counter, VIEW DEMO, es2sim's own code 64 ends the loop
  const { state, lines } = await loop({ commandLine: '-s', languageByte: 0x46, shellPrefix: 'sh', simPrefix: 'sim' }, [5, 3, 2, 64]);
  check(same(lines, ['launcher -s', 'start shell-replacement sh dummy -eggplant -X1 -s -F', 'exit shell-replacement 5',
    'start sim-replacement sim --arguments "-eggplant -X5 -R0 -s -D -F"', 'exit sim-replacement 3',
    'start shell-replacement sh dummy -eggplant -X3 -s -F', 'exit shell-replacement 2',
    'start sim-replacement sim --arguments "-eggplant -X2 -R1 -s -F"', 'exit sim-replacement 64', 'end 64']) && state === 64,
  `loop: demo, counter, code 64 (${JSON.stringify(lines)})`);
}
{
  const { state, lines } = await loop({ languageByte: -1, shellPrefix: 'sh' }, [1]);
  check(same(lines, ['launcher ', 'start shell-replacement sh dummy -eggplant -X1', 'exit shell-replacement 1', 'end 0']) && state === 0,
    `loop: no language, the replacement's 1 ends (${JSON.stringify(lines)})`);
}
{
  const { lines } = await loop({ languageByte: 0x47, shellPrefix: 'sh' }, [2]);
  check(same(lines.slice(2), ['exit shell-replacement 2', 'start dbsim.EXE dummy -eggplant -X2 -R0 -G',
    'start_failed dbsim.EXE 2', 'end -1']),
  `loop: no sim prefix, DBSIM fails to start (${JSON.stringify(lines)})`);
}
{
  const { state, lines, runs } = await loop({ errorStr: null, shellPrefix: 'sh' }, []);
  check(same(lines, ['launcher ', 'message error.str could not be loaded.']) && state === 0 && runs.length === 0,
    `loop: no error.str (${JSON.stringify(lines)})`);
}
{
  const messages = [];
  const { lines, runs } = await loop({ shellPrefix: 'sh', memory: { totalPhys: 0x73a000, totalPageFile: 0, availPageFile: 0, allocated: n },
    message: (text, caption) => messages.push([text, caption]) }, []);
  check(same(messages, [['m1', 'c']]) && same(lines, ['launcher ', 'message m1', 'end 0']) && runs.length === 0,
    `loop: a failed memory check shows group 0 message 1 (${JSON.stringify(messages)} ${JSON.stringify(lines)})`);
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
