// web/keys.mjs and web/key_layouts.mjs against the dump of the Windows API
// (tools/web/testdata/keyboard_layouts.json, tools/web/keyboard_layouts.py) and against the
// native window platform's own tables (sim/src/platform_sdl.c): per layout the generated tables
// equal the dump; the VK of every key code follows es2_sim_window_vk over the dump's
// MAPVK_VSC_TO_VK_EX; vkToScan is MAPVK_VK_TO_VSC; translate is ToAscii for every key alone and
// right after every dead key; the code to scan table is platform_sdl.c's HID table.
//
// Arguments: keyboard_layouts.json platform_sdl.c
import fs from 'node:fs';
import { KEY_LAYOUTS } from '../key_layouts.mjs';
import {
  CODE_SCAN, EXTENDED, FIXED_VK, KEYPAD_OFF, KEYPAD_ON, Keyboard, LAYOUT_NAMES, TRANSLATE, layoutForLanguage,
} from '../keys.mjs';

let checks = 0;
const failures = [];
const check = (ok, label) => {
  checks++;
  if (!ok) { failures.push(label); console.log('FAIL', label); }
};
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const dump = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')).layouts;
const native = fs.readFileSync(process.argv[3], 'utf8');

// ---- the generated tables are the dump
check(same(LAYOUT_NAMES, ['de', 'fr', 'us']) && same(Object.keys(dump), LAYOUT_NAMES), 'layouts de, fr, us');
for (const name of LAYOUT_NAMES) {
  const t = KEY_LAYOUTS[name], d = dump[name];
  check(same(t.vscToVk, d.vsc_to_vk), `${name}: vscToVk is the dump's MAPVK_VSC_TO_VK_EX`);
  check(same(t.vkToVsc, d.vk_to_vsc), `${name}: vkToVsc is the dump's MAPVK_VK_TO_VSC`);
  check(same(t.chars, d.chars), `${name}: chars is the dump's ToAscii`);
  check(same(t.dead, d.dead), `${name}: dead is the dump's ToAscii after the dead keys`);
  check(t.altgr === d.altgr, `${name}: altgr is the dump's`);
}
check(KEY_LAYOUTS.de.altgr && KEY_LAYOUTS.fr.altgr && !KEY_LAYOUTS.us.altgr, 'AltGr: de and fr, not us');

// ---- the code to scan table is platform_sdl.c's HID table, by the UI Events code names
const HID_CODE = {
  4: 'KeyA', 5: 'KeyB', 6: 'KeyC', 7: 'KeyD', 8: 'KeyE', 9: 'KeyF', 10: 'KeyG', 11: 'KeyH', 12: 'KeyI', 13: 'KeyJ',
  14: 'KeyK', 15: 'KeyL', 16: 'KeyM', 17: 'KeyN', 18: 'KeyO', 19: 'KeyP', 20: 'KeyQ', 21: 'KeyR', 22: 'KeyS',
  23: 'KeyT', 24: 'KeyU', 25: 'KeyV', 26: 'KeyW', 27: 'KeyX', 28: 'KeyY', 29: 'KeyZ',
  30: 'Digit1', 31: 'Digit2', 32: 'Digit3', 33: 'Digit4', 34: 'Digit5', 35: 'Digit6', 36: 'Digit7', 37: 'Digit8',
  38: 'Digit9', 39: 'Digit0', 40: 'Enter', 41: 'Escape', 42: 'Backspace', 43: 'Tab', 44: 'Space', 45: 'Minus',
  46: 'Equal', 47: 'BracketLeft', 48: 'BracketRight', 49: 'Backslash', 50: 'Backslash', 51: 'Semicolon',
  52: 'Quote', 53: 'Backquote', 54: 'Comma', 55: 'Period', 56: 'Slash', 57: 'CapsLock',
  58: 'F1', 59: 'F2', 60: 'F3', 61: 'F4', 62: 'F5', 63: 'F6', 64: 'F7', 65: 'F8', 66: 'F9', 67: 'F10', 68: 'F11',
  69: 'F12', 70: 'PrintScreen', 71: 'ScrollLock', 72: 'Pause', 73: 'Insert', 74: 'Home', 75: 'PageUp',
  76: 'Delete', 77: 'End', 78: 'PageDown', 79: 'ArrowRight', 80: 'ArrowLeft', 81: 'ArrowDown', 82: 'ArrowUp',
  83: 'NumLock', 84: 'NumpadDivide', 85: 'NumpadMultiply', 86: 'NumpadSubtract', 87: 'NumpadAdd',
  88: 'NumpadEnter', 89: 'Numpad1', 90: 'Numpad2', 91: 'Numpad3', 92: 'Numpad4', 93: 'Numpad5', 94: 'Numpad6',
  95: 'Numpad7', 96: 'Numpad8', 97: 'Numpad9', 98: 'Numpad0', 99: 'NumpadDecimal', 100: 'IntlBackslash',
  101: 'ContextMenu', 224: 'ControlLeft', 225: 'ShiftLeft', 226: 'AltLeft', 227: 'MetaLeft', 228: 'ControlRight',
  229: 'ShiftRight', 230: 'AltRight', 231: 'MetaRight',
};
const table = native.match(/scan_table\[\] = \{([\s\S]*?)\};/);
check(table, 'platform_sdl.c: scan_table found');
const rows = [...(table ? table[1] : '').matchAll(/\{(\d+), 0x([0-9a-f]+), ([01])\}/g)].map((m) => [+m[1], parseInt(m[2], 16), +m[3]]);
check(rows.length === 106, `platform_sdl.c: 106 rows (${rows.length})`);
const fromNative = {};
for (const [hid, scan, ext] of rows) {
  const code = HID_CODE[hid];
  check(code !== undefined, `HID ${hid} has a code name`);
  fromNative[code] = scan | (ext ? EXTENDED : 0);
}
check(same(Object.keys(fromNative).sort(), Object.keys(CODE_SCAN).sort()), 'CODE_SCAN has the native table\'s keys only');
for (const [code, scan] of Object.entries(fromNative)) check(CODE_SCAN[code] === scan, `${code}: scan ${scan.toString(16)}`);
const arrayOf = (name) => {
  const m = native.match(new RegExp(`${name}\\[11\\] = \\{([^}]*)\\}`));
  return m ? m[1].split(',').map((x) => parseInt(x, 16)) : null;
};
check(same(arrayOf('keypad_on'), KEYPAD_ON) && same(arrayOf('keypad_off'), KEYPAD_OFF), 'keypad tables are platform_sdl.c\'s');
// The fixed cases of es2_sim_window_vk's switch.
const defines = Object.fromEntries([...native.matchAll(/#define (VK_\w+) 0x([0-9a-f]+)u/g)].map((m) => [m[1], parseInt(m[2], 16)]));
const vkSwitch = native.match(/switch \(sdl_scancode\) \{([\s\S]*?)default:/);
const fixed = {};
for (const m of (vkSwitch ? vkSwitch[1] : '').matchAll(/((?:case \d+: )+)return (0x[0-9a-f]+|VK_\w+)u?;/g)) {
  const vk = m[2].startsWith('0x') ? parseInt(m[2], 16) : defines[m[2]];
  for (const c of m[1].matchAll(/case (\d+):/g)) fixed[HID_CODE[+c[1]]] = vk;
}
check(Object.keys(fixed).length === 14 && same(Object.entries(fixed).sort(), Object.entries(FIXED_VK).sort()),
  `FIXED_VK is es2_sim_window_vk's switch (${JSON.stringify(fixed)})`);

// ---- es2_sim_window_vk over each layout's dump
const packed = (n, bytes) => n < 0 ? (TRANSLATE.DEAD | bytes[0]) >>> 0
  : n === 2 ? (TRANSLATE.TWO | bytes[0] | bytes[1] << 8) >>> 0 : n === 1 ? (TRANSLATE.ONE | bytes[0]) >>> 0 : 0;
for (const name of LAYOUT_NAMES) {
  const d = dump[name];
  const vscToVk = new Map(d.vsc_to_vk);
  const k = new Keyboard(name);
  let bad = 0;
  for (const [code, scan] of Object.entries(CODE_SCAN)) {
    for (const numlock of [0, 1]) {
      const pad = ['Numpad1', 'Numpad2', 'Numpad3', 'Numpad4', 'Numpad5', 'Numpad6', 'Numpad7', 'Numpad8', 'Numpad9',
        'Numpad0', 'NumpadDecimal'].indexOf(code);
      const want = pad >= 0 ? (numlock ? KEYPAD_ON : KEYPAD_OFF)[pad] : code in FIXED_VK ? FIXED_VK[code]
        : (vscToVk.get(scan & EXTENDED ? 0xe000 | (scan & 0xff) : scan) || 0) & 0xff;
      if (k.vk(code, numlock) !== want) { bad++; console.log(`${name} ${code} numlock ${numlock}: ${k.vk(code, numlock)} != ${want}`); }
    }
  }
  check(!bad, `${name}: vk(code) for all ${Object.keys(CODE_SCAN).length} codes`);
  const vkToVsc = new Map(d.vk_to_vsc);
  bad = 0;
  for (let vk = 1; vk < 0xff; vk++) if (k.vkToScan(vk) !== (vkToVsc.get(vk) || 0)) bad++;
  check(!bad, `${name}: vkToScan for VK 01..FE`);
  const chars = new Map(d.chars.map(([vk, n, b]) => [vk, packed(n, b)]));
  bad = 0;
  for (let vk = 1; vk < 0xff; vk++) {
    const fresh = new Keyboard(name);
    if (fresh.translate(vk) !== (chars.get(vk) || 0)) bad++;
  }
  check(!bad, `${name}: translate of every VK alone`);
  bad = 0;
  for (const [deadVk, after] of d.dead) {
    const results = new Map(after.map(([vk, n, b]) => [vk, packed(n, b)]));
    for (let vk = 1; vk < 0xff; vk++) {
      const fresh = new Keyboard(name);
      const first = fresh.translate(deadVk);
      if (!(first & TRANSLATE.DEAD) || fresh.translate(vk) !== (results.get(vk) || 0)) bad++;
    }
  }
  check(!bad, `${name}: translate of every VK after each of ${d.dead.length} dead keys`);
}

// ---- the dead-key state across keys (the dump's single steps chained)
{
  const k = new Keyboard('de');
  const vkCircumflex = KEY_LAYOUTS.de.vscToVk.find(([c]) => c === 0x29)[1];
  check(k.translate(vkCircumflex) === (TRANSLATE.DEAD | 0x5e) >>> 0, 'de: ^ is a dead key');
  check(k.translate(0x10) === 0, 'de: Shift after ^ gives nothing');
  check(k.translate(0x41) === (TRANSLATE.ONE | 0xe2) >>> 0, 'de: ^ Shift a gives â (the dead key stayed pending)');
  check(k.translate(0x41) === (TRANSLATE.ONE | 0x61) >>> 0, 'de: then a alone');
  k.translate(vkCircumflex);
  check(k.translate(0xe7) === (TRANSLATE.ONE | 0) >>> 0 && k.translate(0x45) === (TRANSLATE.ONE | 0xea) >>> 0,
    'de: VK_PACKET leaves the dead key pending');
  k.translate(vkCircumflex);
  check(k.translate(0x42) === (TRANSLATE.TWO | 0x5e | 0x62 << 8) >>> 0, 'de: ^ b gives ^b');
  check(new Keyboard('us').translate(0xde) === (TRANSLATE.ONE | 0x27) >>> 0, 'us: VK_OEM_7 is the apostrophe');
}

// ---- key codes, Num Lock, corrections and the layout choice
{
  const k = new Keyboard('us');
  check(k.vk('Numpad7', 1) === 0x67 && k.vk('Numpad7', 0) === 0x24, 'Numpad7: VK_NUMPAD7 / VK_HOME');
  check(k.vk('NumpadEnter', 0) === 0x0d && CODE_SCAN.NumpadEnter === 0x11c, 'NumpadEnter: VK_RETURN, extended');
  check(k.vk('AltRight', 0) === 0x12 && CODE_SCAN.AltRight === 0x138, 'AltRight: VK_MENU, E0 38');
  check(k.vk('Lang1', 0) === 0 && k.vk('IntlYen', 0) === 0, 'keys outside the table: 0');
  check(k.vk('Semicolon', 0) === 0xba, 'us: Semicolon VK_OEM_1');
  k.correct('Semicolon', 59);
  check(k.vk('Semicolon', 0) === 0xba, 'Firefox 59 is VK_OEM_1');
  k.correct('Equal', 61);
  k.correct('Minus', 173);
  check(k.vk('Equal', 0) === 0xbb && k.vk('Minus', 0) === 0xbd, 'Firefox 61 and 173 are VK_OEM_PLUS and VK_OEM_MINUS');
  const de = new Keyboard('de');
  check(de.vk('KeyY', 0) === 0x5a && de.vk('KeyZ', 0) === 0x59, 'de: Y and Z swapped');
  de.correct('KeyY', 0x59);
  check(de.vk('KeyY', 0) === 0x59, 'keyCode corrects the layout\'s VK');
  de.correct('KeyZ', 0);
  de.correct('KeyZ', 229);
  check(de.vk('KeyZ', 0) === 0x59, 'keyCode 0 and 229 change nothing');
  de.correct('Numpad7', 0x24);
  de.correct('ShiftLeft', 0xa0);
  check(de.vk('Numpad7', 1) === 0x67 && de.vk('ShiftLeft', 0) === 0x10, 'keyCode leaves the keypad and fixed keys');
  check(new Keyboard('fr').vk('KeyQ', 0) === 0x41 && new Keyboard('fr').vk('KeyA', 0) === 0x51, 'fr: A and Q swapped');
  check(layoutForLanguage('de-DE') === 'de' && layoutForLanguage('fr-CA') === 'fr' && layoutForLanguage('en-US') === 'us' &&
    layoutForLanguage('it') === 'us' && layoutForLanguage('') === 'us', 'the browser language: de, fr, otherwise us');
  for (const name of LAYOUT_NAMES) {
    const own = new Keyboard(name);
    const map = new Map(Object.keys(CODE_SCAN).map((code) => [code, own.character(code)]).filter(([, c]) => c));
    check(Keyboard.chooseLayout(map, 'us') === name && Keyboard.chooseLayout(map, 'de') === name,
      `chooseLayout finds ${name} from its own layout map`);
  }
  check(Keyboard.chooseLayout(new Map(), 'fr') === 'fr', 'chooseLayout of an empty map: the fallback');
  let threw = false;
  try { new Keyboard('xx'); } catch { threw = true; }
  check(threw, 'an unknown layout throws');
}

console.log(`${checks} checks, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
