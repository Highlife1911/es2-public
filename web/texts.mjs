// The web pages' texts, in English: one table for the landing page, its checks, the self-test
// page and the save upload (docs/web_build.md section 13).
//
//   text('checks.failed');
//   text('saves.failed', { name: 'GAME_0.SAV', reason: 'saves.reason.truncated', offset: 12 });
//
// A parameter {x} is replaced by params.x; a parameter whose value is itself a key of the
// table (saves.failed's reason) is replaced by that key's text. An unknown key gives the key
// itself.
//
// Keys:
//   isolation.* step 1: the isolation through the service worker (web/isolation.mjs FAILURES)
//   check.*    step 1: one per failed feature check (web/checks.mjs CHECKS)
//   checks.*   step 1: the verdict after the checks
//   selftest.* the self-test page (web/selftest.mjs): its title, results and check names
//   lang.*     step 2: the game's language (lang.G/E/F are the choices, by GAME_LANGUAGES' code)
//   files.*    step 3: the load of the game files after the language (web/store.mjs GameFiles)
//   saves.*    step 4: the upload and the results web/saves.mjs returns (saves.reason.* are
//              the parsers' es2_status names, saves.reason.name for a refused name)
//   start.*    step 5
//   export.*   the export of C:\ES2\SAV\ as a ZIP (web/zip.mjs)
//   reset.*    the reset of the installation, with its confirmation
//   box.*      the buttons of the game's message boxes (web/game.mjs BUTTON_KEYS)
//   game.*     the landing page after the game stopped with an error

// The game's installation languages: prepare-native.ps1 -Language's G, E, F.
export const GAME_LANGUAGES = ['G', 'E', 'F'];

// The game language preselected in step 2 for the browser's preferred languages (BCP 47 tags,
// the first one decides): German, French, otherwise English.
export function gameLanguage(languages) {
  const first = String((Array.isArray(languages) ? languages[0] : languages) ?? '').toLowerCase();
  const primary = first.split(/[-_]/)[0];
  return primary === 'de' ? 'G' : primary === 'fr' ? 'F' : 'E';
}

export const TEXTS = {
  'page.title': 'EarthSiege 2',

  'isolation.reloading': 'Setting up the page. It reloads once.',
  'isolation.insecure': 'The game needs a secure connection (https or localhost).',
  'isolation.no-service-worker': 'This window runs no service worker, so the game cannot run here. Open the page in a normal window, not a private one.',
  'isolation.failed': 'The page could not make itself cross-origin isolated, which the game needs for SharedArrayBuffer.',
  'check.sharedArrayBuffer': 'This browser offers no SharedArrayBuffer to this page.',
  'check.audioWorklet': 'This browser has no AudioWorklet, which plays the game\'s sound.',
  'check.webAssembly': 'This browser cannot run WebAssembly.',
  'check.indexedDB': 'This browser offers no IndexedDB, where the game keeps its installation and saves.',
  'check.av1': 'This browser cannot decode AV1 video, the format of the game\'s movies.',
  'check.mseAv1': 'This browser cannot play AV1 video in WebM through Media Source Extensions.',
  'check.mseOpus': 'This browser cannot play Opus sound in WebM through Media Source Extensions.',
  'checks.failed': 'The game cannot start in this browser.',

  'selftest.title': 'EarthSiege 2 self-test',
  'selftest.passed': 'passed',
  'selftest.failed': 'failed',
  'selftest.name.isolation': 'Cross-origin isolation',
  'selftest.name.sharedArrayBuffer': 'SharedArrayBuffer',
  'selftest.name.audioWorklet': 'AudioWorklet',
  'selftest.name.webAssembly': 'WebAssembly',
  'selftest.name.indexedDB': 'IndexedDB',
  'selftest.name.av1': 'AV1 decoding (MediaCapabilities)',
  'selftest.name.mseAv1': 'MediaSource: WebM with AV1',
  'selftest.name.mseOpus': 'MediaSource: WebM with Opus',
  'selftest.keys': 'Press keys: each shows its code, the scan code and the VK the game gets.',
  'selftest.layout': 'Key layout',
  'selftest.game': 'Checks with the game: each runs when its button is pressed.',
  'selftest.run': 'Run',
  'selftest.running': 'running',
  'selftest.name.audio': 'Sound (AudioWorklet) and CD music',
  'selftest.name.video': 'Movie to its end',
  'selftest.name.frameTime': 'Frame times of a mission',
  'selftest.noCdAudio': 'this build has no CD music',
  'selftest.noMedia': 'this build has no movies',
  'selftest.name.suite': 'Suite replay {name}',
  'selftest.suiteWasm': 'the expected hashes belong to another es2sim.wasm (tools/web/selftest_suites.py)',

  'lang.title': 'Language',
  'lang.tip': 'Can only be changed after a reset.',
  'lang.G': 'German',
  'lang.E': 'English',
  'lang.F': 'French',

  'saves.title': 'Savegames',
  'saves.details': 'The ZIP contains the files of the SAV folder.',
  'saves.upload': 'Import',
  'saves.zip_invalid': '{name} cannot be read as a ZIP file: {reason}.',
  'saves.zip.format': 'it is damaged or not a ZIP file',
  'saves.zip.unsupported': 'it uses ZIP64, encryption or a compression other than Deflate',
  'saves.zip.crc': 'a checksum does not match',
  'saves.zip.size': 'its content is too large',
  'saves.ignored': '{name} is not a save file and was skipped.',
  'saves.failed': '{name} is not a valid save file ({reason}, at byte {offset}).',
  'saves.duplicate': '{name} was chosen more than once.',
  'saves.nothing_written': 'No file was taken over.',
  'saves.none': 'The ZIP file contains no save file.',
  'saves.written': '{count} files taken over.',
  'saves.language_note': 'Saved games from another language version are taken over unchanged; their mission texts stay in the language they were written in.',
  'saves.reason.name': 'name not accepted',
  'saves.reason.argument': 'invalid input',
  'saves.reason.truncated': 'the file ends too early',
  'saves.reason.magic': 'wrong file type',
  'saves.reason.version': 'unknown version',
  'saves.reason.count': 'a count is out of range',
  'saves.reason.offset': 'an offset is out of range',
  'saves.reason.length': 'a length is out of range',
  'saves.reason.overlap': 'overlapping data',
  'saves.reason.string': 'a text is not terminated',
  'saves.reason.unsupported': 'unsupported content',
  'saves.reason.capacity': 'too large',
  'saves.reason.depth': 'nested too deeply',
  'saves.reason.callback': 'check aborted',
  'saves.reason.not_found': 'an entry is missing',
  'saves.reason.invalid': 'invalid content',

  'files.title': 'Game files',
  'files.tip': 'Loaded once after the language is chosen and kept in this browser until a reset.',
  'files.starting': 'Loading ...',
  'files.loading': 'Loading: {received} of {size} MB ({percent} %)',
  'files.ready': 'Loaded ({size} MB).',
  'files.failed': 'Loading failed: {error}',
  'files.retry': 'Retry',

  'volume.title': 'Volume',
  'volume.tip': 'Can only be changed before the start.',
  'volume.test': 'Test',
  'volume.value': '{percent} %',

  'start.button': 'Start',
  'start.language': 'Choose a language first.',
  'start.files': 'Wait until the game files are loaded.',
  'start.busy': 'The game is already running in another tab of this site.',

  'export.button': 'Export',
  'export.filename': 'earthsiege2-saves.zip',
  'export.empty': 'There are no saved games to export.',

  'reset.button': 'Reset',
  'reset.confirm': 'Reset the installation? All saved games and settings in this browser are deleted. Export your saved games first if you want to keep them.',
  'reset.yes': 'Reset',
  'reset.no': 'Cancel',
  'reset.done': 'The installation was reset.',
  'reset.busy': 'The game is running in another tab of this site. End it there, then reset.',
  'page.failed': 'An error occurred: {error}',

  'box.ok': 'OK',
  'box.cancel': 'Cancel',
  'box.abort': 'Abort',
  'box.retry': 'Retry',
  'box.ignore': 'Ignore',
  'box.yes': 'Yes',
  'box.no': 'No',
  'game.failed': 'The game stopped with an error: {error}',
  'game.worker': 'the game worker failed without naming an error',
};

export function text(key, params = {}) {
  const template = Object.hasOwn(TEXTS, key) ? TEXTS[key] : key;
  return template.replace(/\{([a-z_]+)\}/g, (whole, name) => {
    if (!Object.hasOwn(params, name)) return whole;
    const value = String(params[name]);
    return Object.hasOwn(TEXTS, value) ? TEXTS[value] : value;
  });
}
