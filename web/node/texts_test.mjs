// web/texts.mjs: every text is plain English (printable ASCII), a text for every check of
// web/checks.mjs, every failure of web/isolation.mjs and every reason of web/saves.mjs, the game
// language from the browser's languages, the substitution and the fallback.
import { CHECKS } from '../checks.mjs';
import { FAILURES } from '../isolation.mjs';
import { REASONS } from '../saves.mjs';
import { GAME_LANGUAGES, TEXTS, gameLanguage, text } from '../texts.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};

const keys = Object.keys(TEXTS).sort();
for (const key of keys) {
  const s = TEXTS[key];
  check(typeof s === 'string' && s.trim().length > 0, `${key} not empty`);
  check(/^[\x20-\x7e]*$/.test(s), `${key} is printable ASCII: ${s}`);
}
for (const reason of [...REASONS.slice(1), 'name', 'invalid']) check(Object.hasOwn(TEXTS, `saves.reason.${reason}`), `reason ${reason} has a text`);
for (const c of CHECKS) check(Object.hasOwn(TEXTS, c.text) && Object.hasOwn(TEXTS, `selftest.name.${c.id}`), `check ${c.id} has texts`);
for (const key of Object.values(FAILURES)) check(Object.hasOwn(TEXTS, key), `isolation failure ${key} has a text`);
for (const prefix of ['isolation.', 'check.', 'checks.', 'selftest.', 'lang.', 'saves.', 'start.', 'export.', 'reset.']) {
  check(keys.some((k) => k.startsWith(prefix)), `texts for ${prefix}*`);
}
for (const key of ['reset.confirm', 'saves.language_note', 'isolation.reloading', 'saves.details', 'start.language', 'lang.tip', 'volume.tip']) check(keys.includes(key), key);
check(GAME_LANGUAGES.join() === 'G,E,F' && GAME_LANGUAGES.every((code) => Object.hasOwn(TEXTS, `lang.${code}`)),
  'installation languages G, E, F with their names');

const cases = [
  [['de-DE', 'en'], 'G'], [['de'], 'G'], [['de-AT'], 'G'], [['DE-ch'], 'G'],
  [['fr-FR', 'de'], 'F'], [['fr-CA'], 'F'], [['en-US', 'de'], 'E'], [['es-ES', 'de'], 'E'],
  [[], 'E'], [undefined, 'E'], ['fr', 'F'], ['de_DE', 'G'], [['frr'], 'E'],
];
for (const [input, want] of cases) check(gameLanguage(input) === want, `gameLanguage(${JSON.stringify(input)}) = ${want}`);

check(text('saves.written', { count: 3 }) === '3 files taken over.', 'a parameter');
check(text('saves.failed', { name: 'GAME_0.SAV', reason: 'saves.reason.truncated', offset: 12 }) ===
  'GAME_0.SAV is not a valid save file (the file ends too early, at byte 12).', 'a key as parameter');
check(text('saves.ignored', { name: 'constructor' }).startsWith('constructor '), 'a parameter that is no key');
check(text('no.such.key') === 'no.such.key', 'fallback');
check(text('constructor') === 'constructor' && text('toString') === 'toString', 'no inherited members');
check(text('checks.failed') === TEXTS['checks.failed'], 'a text without parameters');

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
