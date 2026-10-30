// The "es2shell" imports of es2shell.wasm (web_imports.h) come from the host as Module.es2shell,
// a function (memory) => imports (web/es2shell_imports.mjs); os_node.js's instantiateWasm adds
// them. Without one (the headless runs under Node, which never open the window) every import
// aborts.
var es2Imports = es2Imports || {};
es2Imports['es2shell'] = (memory) => {
  if (Module['es2shell']) return Module['es2shell'](memory);
  const names = ['now', 'wait_input', 'take_input', 'present', 'cursor', 'fullscreen', 'message_box', 'movie_play',
    'movie_stop', 'manual', 'sound_space', 'sound_write'];
  return Object.fromEntries(names.map((name) => [name, () => abort('es2shell.' + name + ': no host')]));
};
