// es2launch.exe's host around web/launcher.mjs under Node: the working directory is the
// installation (error.str, data\language.cfg), the children are child processes and wait for
// their end, messages go to the log only (as ES2_LAUNCH_NO_UI). Environment as es2launch.exe's:
//   ES2_LAUNCH_LOG    one line per decision, start and exit, after an "HH:MM:SS.mmm " time
//   ES2_LAUNCH_SHELL  command prefix of the shell replacement (the VSHELL argv is appended)
//   ES2_LAUNCH_SIM    command prefix of the sim replacement (` --arguments "..."` is appended)
// A command is split as CommandLineToArgvW splits it; argv[0] is started directly.
//
//   node launcher_host.mjs [--memory FILE] LAUNCH_WEB.JS [COMMAND_LINE]
//
// --memory: memory_probe's output (total_phys, total_pagefile, avail_pagefile, allocated) instead
// of the recorded REFERENCE_MEMORY.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { loadLaunch, runLauncher, splitCommandLine, REFERENCE_MEMORY } from '../launcher.mjs';

let args = process.argv.slice(2);
let memory = REFERENCE_MEMORY;
if (args[0] === '--memory') {
  const values = Object.fromEntries(fs.readFileSync(args[1], 'latin1').split(/\r?\n/).filter(Boolean)
    .map((line) => line.split(' ')).map(([k, v]) => [k, Number(v)]));
  memory = { totalPhys: values.total_phys, totalPageFile: values.total_pagefile, availPageFile: values.avail_pagefile,
    allocated: values.allocated };
  args = args.slice(2);
}
const [module, commandLine = ''] = args;

const logPath = process.env.ES2_LAUNCH_LOG;
const two = (n) => String(n).padStart(2, '0');
const log = (line) => {
  if (!logPath) return;
  const t = new Date();
  const stamp = `${two(t.getHours())}:${two(t.getMinutes())}:${two(t.getSeconds())}.${String(t.getMilliseconds()).padStart(3, '0')}`;
  fs.appendFileSync(logPath, Buffer.from(`${stamp} ${line}\n`, 'latin1'));
};
const read = (name) => { try { return fs.readFileSync(name); } catch { return null; } };
const language = read(path.join('data', 'language.cfg'));
const run = (argv) => {
  const child = spawnSync(argv[0], argv.slice(1), { stdio: 'inherit', windowsHide: true });
  return child.error || child.status === null ? -1 : child.status | 0;
};

const factory = (await import(pathToFileURL(path.resolve(module)).href)).default;
const launch = await loadLaunch(factory);
const nonempty = (v) => (v ? v : null);
await runLauncher(launch, {
  commandLine,
  languageByte: language && language.length ? language[0] : -1,
  errorStr: read('error.str'),
  memory,
  shellPrefix: nonempty(process.env.ES2_LAUNCH_SHELL),
  simPrefix: nonempty(process.env.ES2_LAUNCH_SIM),
  runShell: run,
  runSim: run,
  log,
});
process.exit(0);
