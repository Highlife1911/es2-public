// Link-time names of the "es2shell" imports (web_imports.h). The instance gets the real
// functions from Module.es2shell (es2shell_pre.js); these entries only let the link resolve the
// imports and never run.
addToLibrary({
  now: () => abort('es2shell.now: no host import'),
  wait_input: () => abort('es2shell.wait_input: no host import'),
  take_input: () => abort('es2shell.take_input: no host import'),
  present: () => abort('es2shell.present: no host import'),
  cursor: () => abort('es2shell.cursor: no host import'),
  fullscreen: () => abort('es2shell.fullscreen: no host import'),
  message_box: () => abort('es2shell.message_box: no host import'),
  movie_play: () => abort('es2shell.movie_play: no host import'),
  movie_stop: () => abort('es2shell.movie_stop: no host import'),
  manual: () => abort('es2shell.manual: no host import'),
  sound_space: () => abort('es2shell.sound_space: no host import'),
  sound_write: () => abort('es2shell.sound_write: no host import'),
});
