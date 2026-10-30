// Link-time names of the "es2sim" imports of the windowed web platform (platform_web.c,
// audio_web.c). The instance gets the real ones from Module.es2sim (web/es2sim_imports.mjs);
// these entries only let the link resolve them.
addToLibrary({
  window_now: () => abort('es2sim.window_now: no host import'),
  window_sleep: () => abort('es2sim.window_sleep: no host import'),
  window_take_input: () => abort('es2sim.window_take_input: no host import'),
  window_present: () => abort('es2sim.window_present: no host import'),
  window_cursor: () => abort('es2sim.window_cursor: no host import'),
  window_display: () => abort('es2sim.window_display: no host import'),
  window_metrics: () => abort('es2sim.window_metrics: no host import'),
  window_keys: () => abort('es2sim.window_keys: no host import'),
  window_message_box: () => abort('es2sim.window_message_box: no host import'),
  audio_open: () => abort('es2sim.audio_open: no host import'),
  audio_write: () => abort('es2sim.audio_write: no host import'),
  audio_queued: () => abort('es2sim.audio_queued: no host import'),
  audio_reset: () => abort('es2sim.audio_reset: no host import'),
  audio_close: () => abort('es2sim.audio_close: no host import'),
  mci_send: () => abort('es2sim.mci_send: no host import'),
});
