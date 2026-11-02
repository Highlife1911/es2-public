// node host.mjs [SITE OPTIONS] [--keyboard NAME] [--host HOST.mjs]... MODULE.js [ARG...]
// runs an Emscripten module (es2sim's wasm build) as a program (run.mjs) with the "es2os"
// imports of os_host.mjs: over node:fs on the host's paths and working directory, or with
// --site over the site's file system (web/vfs.mjs). es2sim's "es2sim" imports
// (sim/src/os_process_wasm.js) get the keyboard of web/keys.mjs with the layout NAME (de, fr
// or us; de, the reference machine's, by default). Every argument after MODULE.js reaches the
// program unchanged (no ${NAME} expansion).
//
// SITE OPTIONS, all but the first two optional:
//   --site DIR --codec VFS_JS  the built site and the codec's vfs.js
//   --cwd PATH                 the working directory in the VFS (C:\ES2)
//   --poison                   unfetched bytes of a mapping are poisoned (Vfs poison mode)
//   --language CODE            Vfs.installLanguage(CODE) before the start, as the landing page
//   --stage DIR                the files below the host directory DIR go into the working
//                              directory before the start; at the exit the files the run
//                              changed there go back into DIR (deleted ones are deleted)
//   --host-dir DIR             full paths at or below the host directory DIR stay on node:fs
//                              (a test's tick log and journal); repeatable
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { runProgram, takeHosts } from './run.mjs';
import { es2osHost, nodeFiles, routedFiles, vfsFiles } from './os_host.mjs';
import { Keyboard, LAYOUT_NAMES } from '../keys.mjs';
import { keyOf } from '../vfs.mjs';

// os_win32.c's clocks ignore TZ (GetTimeZoneInformation, FileTimeToLocalFileTime) and Node's
// follow it from the process start on (Bazel's test environment sets TZ=UTC), so with TZ set the
// host runs itself again without it.
const tz = Object.keys(process.env).filter((name) => name.toUpperCase() === 'TZ' && process.env[name]);
if (tz.length) {
  const env = { ...process.env };
  for (const name of tz) delete env[name];
  const run = spawnSync(process.execPath, [...process.execArgv, ...process.argv.slice(1)], { stdio: 'inherit', env });
  process.exit(run.status === null ? 1 : run.status | 0);
}

const usage = () => {
  console.error('usage: host.mjs [--site DIR --codec VFS_JS [--cwd PATH] [--poison] [--language CODE] [--stage DIR] '
    + '[--host-dir DIR]...] [--keyboard NAME] [--host HOST.mjs]... MODULE.js [ARG...]');
  process.exit(2);
};

const VALUE_OPTIONS = ['--site', '--codec', '--cwd', '--language', '--stage'];
let args = process.argv.slice(2);
const site = { hostDirs: [] };
let layout = 'de';
for (;;) {
  const [option, value] = args;
  if (VALUE_OPTIONS.includes(option) || option === '--host-dir') {
    if (value === undefined) usage();
    if (option === '--host-dir') site.hostDirs.push(value);
    else site[option.slice(2)] = value;
    args = args.slice(2);
  } else if (option === '--keyboard') {
    if (!LAYOUT_NAMES.includes(value)) usage();
    layout = value;
    args = args.slice(2);
  } else if (option === '--poison') {
    site.poison = true;
    args = args.slice(1);
  } else {
    break;
  }
}
const [hosts, rest] = takeHosts(args);
const siteOnly = ['cwd', 'poison', 'language', 'stage'].some((k) => site[k]) || site.hostDirs.length;
if (!rest.length || !site.site !== !site.codec || (!site.site && siteOnly)) usage();

// The host directory's files as [relative path with backslashes, full path], directories first.
function hostTree(dir, relative = '') {
  const dirs = [], files = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const rel = relative ? `${relative}\\${entry.name}` : entry.name;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      dirs.push(rel);
      const [d, f] = hostTree(full, rel);
      dirs.push(...d);
      files.push(...f);
    } else {
      files.push([rel, full]);
    }
  }
  return [dirs, files];
}

let files;
if (site.site) {
  const { loadCodecFrom, openSite } = await import('./vfs_site.mjs');
  const vfs = openSite(site.site, await loadCodecFrom(site.codec), { poison: !!site.poison });
  const root = site.cwd ?? 'C:\\ES2';
  if (site.language) vfs.installLanguage(site.language);
  let staged = null;
  if (site.stage) {
    const [dirs, stageFiles] = hostTree(site.stage);
    for (const d of dirs) if (!vfs.stat(`${root}\\${d}`)) vfs.mkdir(`${root}\\${d}`);
    for (const [rel, full] of stageFiles) vfs.write(`${root}\\${rel}`, fs.readFileSync(full));
    staged = new Map(vfs.overlay);
  }
  process.on('exit', () => {
    if (staged) {
      const prefix = keyOf(root) + '\\';
      for (const [key, e] of vfs.overlay) {
        if (!key.startsWith(prefix) || staged.get(key) === e) continue;
        const target = path.join(site.stage, ...e.name.slice(prefix.length).split('\\'));
        if (e.kind === 'tombstone') fs.rmSync(target, { force: true });
        else if (e.kind === 'dir') fs.mkdirSync(target, { recursive: true });
        else {
          fs.mkdirSync(path.dirname(target), { recursive: true });
          fs.writeFileSync(target, vfs.readAll(e.name));
        }
      }
    }
  });
  const inVfs = vfsFiles(vfs, { cwd: root });
  files = site.hostDirs.length ? routedFiles(inVfs, nodeFiles(), site.hostDirs) : inVfs;
} else {
  files = nodeFiles();
}
const [modulePath, ...programArgs] = rest;
const keyboard = new Keyboard(layout);
const es2sim = () => ({
  key_scan: (vk) => keyboard.vkToScan(vk),
  key_translate: (vk) => keyboard.translate(vk),
});
await runProgram(modulePath, programArgs, { hosts, options: { es2os: es2osHost(files), es2sim }, expandArgs: false });
