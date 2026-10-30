// Link-time names of os_process_wasm.c's "es2sim" imports. The instance gets the real ones
// from os_process_wasm.js (Module.es2sim); these entries only let the link resolve them.
addToLibrary({
  key_scan: () => abort('es2sim.key_scan: no host import'),
  key_translate: () => abort('es2sim.key_translate: no host import'),
});
