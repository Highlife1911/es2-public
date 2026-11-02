// The web build's launcher loop: es2launch.c's WinMain over launch.wasm (//launcher:launch_wasm,
// launcher/src/launch_logic.h). Every decision is a call into the module; this file only moves
// strings and runs the children through the host's callbacks (docs/web_build.md section 2).
//
//   const launch = await loadLaunch(factory);        // factory: launch_web.js's default export
//   const state = await runLauncher(launch, {
//     commandLine, languageByte, errorStr, memory,   // ES.EXE's inputs
//     shellPrefix, simPrefix,                        // ES2_LAUNCH_SHELL / ES2_LAUNCH_SIM
//     runShell(argv, command), runSim(argv, command),// a child's exit code, or -1
//     message(text, caption), log(line),
//   });
//
// The children are es2shell (the shell replacement: its exit code goes through
// es2l_after_replacement) and es2sim (the sim replacement); there is no VSHELL.EXE or DBSIM.EXE,
// so a launch without a prefix is a failed start (-1), as a missing program is for ES.EXE.

// GlobalMemoryStatus's totals in es2launch.exe's process on the reference machine, recorded by
// //launcher:memory_probe (not large-address-aware, as ES.EXE): the 2 GB clamp of the physical
// total and the 4 GB clamp of the page file. es2l_memory_check passes them as it does natively
// (available pagefile + the 20,000,000 bytes of the GlobalAlloc loop wraps to 19,999,999).
export const REFERENCE_MEMORY = {
  totalPhys: 2147483647,
  totalPageFile: 4294967295,
  availPageFile: 4294967295,
  allocated: 20000000,
};

const PROGRAM = { QUIT: 0, SHELL: 1, SIM: 2 };
const LAUNCH_BYTES = 512;    // es2l_launch: 396 bytes in wasm32
const OPTIONS_BYTES = 64;    // es2l_options: 27 bytes
const STRINGS_BYTES = 64;    // es2l_strings: nine pointers
const COMMAND_BYTES = 4096;  // es2launch.c's command buffer

export async function loadLaunch(factory) {
  const m = await factory();
  const latin1 = (ptr) => {
    const bytes = m.HEAPU8;
    let end = ptr;
    while (bytes[end]) end++;
    let s = '';
    for (let at = ptr; at < end; at += 4096) s += String.fromCharCode(...bytes.subarray(at, Math.min(end, at + 4096)));
    return s;
  };
  // A NUL-terminated copy of a string (Latin-1) or bytes in the module's heap; free with m._free.
  const copy = (value) => {
    const bytes = typeof value === 'string' ? Uint8Array.from(value, (c) => c.charCodeAt(0) & 0xff) : value;
    const ptr = m._malloc(bytes.length + 1);
    m.HEAPU8.set(bytes, ptr);
    m.HEAPU8[ptr + bytes.length] = 0;
    return ptr;
  };
  return { m, latin1, copy };
}

// CommandLineToArgvW's split of a command line (the argv a C main gets from CreateProcess's line).
export function splitCommandLine(line) {
  const argv = [];
  let at = 0;
  while (at < line.length) {
    while (line[at] === ' ' || line[at] === '\t') at++;
    if (at >= line.length) break;
    let arg = '';
    let quoted = false;
    for (; at < line.length; at++) {
      const c = line[at];
      if (!quoted && (c === ' ' || c === '\t')) break;
      if (c === '\\') {
        let n = 0;
        while (line[at] === '\\') { n++; at++; }
        if (line[at] === '"') {
          arg += '\\'.repeat(n >> 1);
          if (n & 1) arg += '"';
          else quoted = !quoted;
        } else {
          arg += '\\'.repeat(n);
          at--;
        }
        continue;
      }
      if (c === '"') {
        if (quoted && line[at + 1] === '"') { arg += '"'; at++; } else quoted = !quoted;
        continue;
      }
      arg += c;
    }
    argv.push(arg);
  }
  return argv;
}

export async function runLauncher(launch, host) {
  const { m, latin1, copy } = launch;
  const log = host.log || (() => {});
  const message = (text, caption) => {
    log(`message ${text ?? ''}`);
    if (host.message) host.message(text, caption);
  };
  const commandLine = host.commandLine ?? '';
  log(`launcher ${commandLine}`);

  const options = m._malloc(OPTIONS_BYTES);
  const line = copy(commandLine);
  m._es2l_parse(line, options);
  const languageByte = host.languageByte ?? -1;
  const language = m._es2l_language(languageByte);

  const strings = m._malloc(STRINGS_BYTES);
  const errorStr = host.errorStr ? copy(host.errorStr) : 0;
  const free = () => { for (const p of [options, line, strings, errorStr]) if (p) m._free(p); };
  if (!errorStr || !m._es2l_parse_strings(errorStr, host.errorStr.length, strings)) {
    message('error.str could not be loaded.', 'Error');
    free();
    return 0;
  }
  const group0 = (i) => { const p = m.HEAP32[(strings >> 2) + i]; return p ? latin1(p) : null; };
  const group1 = (i) => { const p = m.HEAP32[(strings >> 2) + 6 + i]; return p ? latin1(p) : null; };

  const memory = host.memory || REFERENCE_MEMORY;
  const index = m._es2l_memory_check(memory.totalPhys >>> 0, memory.totalPageFile >>> 0, memory.availPageFile >>> 0,
    memory.allocated >>> 0);
  let state = 1;
  if (index >= 0) {
    message(group0(index), group1(0));
    state = 0;
  }

  const launchPtr = m._malloc(LAUNCH_BYTES);
  const counter = m._malloc(4);
  const command = m._malloc(COMMAND_BYTES);
  const shellPrefix = host.shellPrefix ? copy(host.shellPrefix) : 0;
  const simPrefix = host.simPrefix ? copy(host.simPrefix) : 0;
  m.HEAP32[counter >> 2] = 0;
  while (state !== 0) {
    m._es2l_next(launchPtr, state, options, language, counter);   // sret: the es2l_launch result
    const program = m.HEAP32[launchPtr >> 2];
    if (program === PROGRAM.QUIT) break;
    const shell = program === PROGRAM.SHELL;
    const prefix = shell ? shellPrefix : simPrefix;
    const name = shell ? 'shell-replacement' : 'sim-replacement';
    if (!prefix) {
      // ES.EXE's VSHELL.EXE / DBSIM.EXE: no such program in the web build
      const programName = `${latin1(m.HEAP32[(launchPtr >> 2) + 1])}.EXE`;
      if (m._es2l_command_line(launchPtr, command, COMMAND_BYTES)) {
        log(`start ${programName} ${latin1(command)}`);
        log(`start_failed ${programName} 2`);
      }
      state = -1;
      continue;
    }
    const ok = shell ? m._es2l_replacement_command(prefix, launchPtr, command, COMMAND_BYTES)
      : m._es2l_sim_command(prefix, launchPtr, command, COMMAND_BYTES);
    if (!ok) {
      if (!shell) log(`start_failed ${name} command`);
      state = -1;
    } else {
      const text = latin1(command);
      log(`start ${name} ${text}`);
      const run = shell ? host.runShell : host.runSim;
      const code = run ? await run(splitCommandLine(text), text) : -1;
      if (code === -1 || code === undefined) {
        log(`start_failed ${name} 2`);
        state = -1;
      } else {
        state = code | 0;
        log(`exit ${name} ${state}`);
      }
    }
    if (shell) state = m._es2l_after_replacement(state);
  }
  log(`end ${state}`);
  for (const p of [launchPtr, counter, command, shellPrefix, simPrefix]) if (p) m._free(p);
  free();
  return state;
}
