// The JS side of es2sim's process in the web build (os_process_wasm.c, docs/web_build.md
// sections 5 and 6), a --pre-js after platform/src/os_node.js:
//
// - the "es2sim" imports of Module.es2sim(memory): the keyboard, key_scan(vk) (MapVirtualKeyA's
//   scan code) and key_translate(vk, scan) (ToAscii's answer as web/keys.mjs Keyboard.translate
//   packs it), and in the windowed build the web platform's (web/es2sim_imports.mjs); without
//   Module.es2sim the first call of any of them ends the run with an error;
// - the end of a run that left the wasm code by an exception: a RuntimeError resets the
//   stack pointer and calls es2_wasm_trap_exit(kind), a VFS read without bytes
//   ("media-bytes") calls es2_wasm_media_exit. Both end the process themselves.

var es2Imports = es2Imports || {};

if (typeof Module !== 'undefined') {
  es2Imports['es2sim'] = (memory) => {
    if (Module['es2sim']) return Module['es2sim'](memory);
    return new Proxy({}, {
      get: (_, name) => () => { throw new Error('es2sim: no host for ' + String(name) + ' (Module.es2sim)'); },
    });
  };

  // The trap kinds of es2/sim/machine_sim.h (ES2_MACHINE_SIM_TRAP_*) from the engines' messages:
  // V8's, and SpiderMonkey's "integer divide by zero" and "integer overflow" (taken from its
  // message table, not yet seen in a Firefox run; the module has only saturating float-to-int
  // conversions, so only INT_MIN / -1 gives that overflow).
  var es2simTrapKind = (e) => {
    if (/divide by zero|remainder by zero/.test(e.message)) return 1;
    if (/divide result unrepresentable|^integer overflow$/.test(e.message)) return 2;
    return 0;
  };

  // callMain's handleException ends every exception but ExitStatus in quit_(1, e); the run's
  // own exits are installed in front of it before main runs.
  Module['preRun'] = [].concat(Module['preRun'] || [], () => {
    const quit = quit_;
    let ending = false;
    quit_ = (status, toThrow) => {
      if (!ending && toThrow instanceof WebAssembly.RuntimeError) {
        ending = true;
        stackRestore(_emscripten_stack_get_base());
        _es2_wasm_trap_exit(es2simTrapKind(toThrow));
      } else if (!ending && toThrow && toThrow.code === 'media-bytes') {
        ending = true;
        stackRestore(_emscripten_stack_get_base());
        _es2_wasm_media_exit();
      }
      quit(status, toThrow);
    };
  });
}
