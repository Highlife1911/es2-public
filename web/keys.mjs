// The keyboard of the web platform (docs/web_build.md section 12): what the native window
// platform gets from SDL and Windows per key (sim/src/platform_sdl.c es2_sim_window_vk,
// sim/src/main.c translate_key and vk_scan), from KeyboardEvent.code and the layout tables of
// key_layouts.mjs (MapVirtualKeyW, ToAscii and the AltGr test of the de, fr and us layouts,
// dumped by tools/web/keyboard_layouts.py).
//
// Scan codes are the set-1 code with 0x100 for the E0 prefix, as in the input ring
// (shell/src/web/web_imports.h).
import { KEY_LAYOUTS } from './key_layouts.mjs';

export const EXTENDED = 0x100;

// KeyboardEvent.code to the scan code: platform_sdl.c's table from the USB HID usage, by the
// code names of the UI Events KeyboardEvent code values. Keys that table lacks are absent.
export const CODE_SCAN = {
  KeyA: 0x1e, KeyB: 0x30, KeyC: 0x2e, KeyD: 0x20, KeyE: 0x12, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23, KeyI: 0x17,
  KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26, KeyM: 0x32, KeyN: 0x31, KeyO: 0x18, KeyP: 0x19, KeyQ: 0x10, KeyR: 0x13,
  KeyS: 0x1f, KeyT: 0x14, KeyU: 0x16, KeyV: 0x2f, KeyW: 0x11, KeyX: 0x2d, KeyY: 0x15, KeyZ: 0x2c,
  Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07, Digit7: 0x08, Digit8: 0x09,
  Digit9: 0x0a, Digit0: 0x0b,
  Enter: 0x1c, Escape: 0x01, Backspace: 0x0e, Tab: 0x0f, Space: 0x39, Minus: 0x0c, Equal: 0x0d,
  BracketLeft: 0x1a, BracketRight: 0x1b, Backslash: 0x2b, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
  Comma: 0x33, Period: 0x34, Slash: 0x35, CapsLock: 0x3a,
  F1: 0x3b, F2: 0x3c, F3: 0x3d, F4: 0x3e, F5: 0x3f, F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
  F11: 0x57, F12: 0x58,
  PrintScreen: 0x137, ScrollLock: 0x46, Pause: 0x45,
  Insert: 0x152, Home: 0x147, PageUp: 0x149, Delete: 0x153, End: 0x14f, PageDown: 0x151,
  ArrowRight: 0x14d, ArrowLeft: 0x14b, ArrowDown: 0x150, ArrowUp: 0x148,
  NumLock: 0x145, NumpadDivide: 0x135, NumpadMultiply: 0x37, NumpadSubtract: 0x4a, NumpadAdd: 0x4e,
  NumpadEnter: 0x11c,
  Numpad1: 0x4f, Numpad2: 0x50, Numpad3: 0x51, Numpad4: 0x4b, Numpad5: 0x4c, Numpad6: 0x4d, Numpad7: 0x47,
  Numpad8: 0x48, Numpad9: 0x49, Numpad0: 0x52, NumpadDecimal: 0x53,
  IntlBackslash: 0x56, ContextMenu: 0x15d,
  ControlLeft: 0x1d, ShiftLeft: 0x2a, AltLeft: 0x38, MetaLeft: 0x15b,
  ControlRight: 0x11d, ShiftRight: 0x36, AltRight: 0x138, MetaRight: 0x15c,
};

// The keypad's digits and decimal point (Numpad1..9, Numpad0, NumpadDecimal): with Num Lock
// the VK_NUMPAD codes, without it the cursor block's, as the keyboard driver translates them.
const KEYPAD = ['Numpad1', 'Numpad2', 'Numpad3', 'Numpad4', 'Numpad5', 'Numpad6', 'Numpad7', 'Numpad8', 'Numpad9',
  'Numpad0', 'NumpadDecimal'];
export const KEYPAD_ON = [0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x60, 0x6e];
export const KEYPAD_OFF = [0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21, 0x2d, 0x2e];

// The keys whose VK es2_sim_window_vk fixes instead of asking the layout.
export const FIXED_VK = {
  Pause: 0x13, NumLock: 0x90, NumpadDivide: 0x6f, NumpadMultiply: 0x6a, NumpadSubtract: 0x6d, NumpadAdd: 0x6b,
  NumpadEnter: 0x0d, PrintScreen: 0x2c, ControlLeft: 0x11, ControlRight: 0x11, ShiftLeft: 0x10, ShiftRight: 0x10,
  AltLeft: 0x12, AltRight: 0x12,
};

// Firefox's own keyCodes for the OEM keys ';', '=' and '-' to the Windows codes.
const FIREFOX_KEYCODE = { 59: 0xba, 61: 0xbb, 173: 0xbd };

const VK_PACKET = 0xe7;
export const TRANSLATE = { ONE: 0x01000000, TWO: 0x02000000, DEAD: 0x80000000 };

export const LAYOUT_NAMES = Object.keys(KEY_LAYOUTS);

// The built-in layout of a browser language: German, French, otherwise US.
export function layoutForLanguage(language) {
  const primary = String(language || '').toLowerCase().split(/[-_]/)[0];
  return primary === 'de' ? 'de' : primary === 'fr' ? 'fr' : 'us';
}

function charsOf(entry) {
  const m = new Map();
  for (const [vk, n, bytes] of entry) m.set(vk, { n, bytes });
  return m;
}

const cp1252 = new TextDecoder('windows-1252');

export class Keyboard {
  constructor(name = 'us') {
    const t = KEY_LAYOUTS[name];
    if (!t) throw new Error(`no key layout ${name}`);
    this.name = name;
    this.altgr = t.altgr;
    this.vscToVk = new Map(t.vscToVk.map(([code, vk]) => [code >= 0xe000 ? (code & 0xff) | EXTENDED : code, vk]));
    this.vkToVscTable = new Map(t.vkToVsc);
    this.chars = charsOf(t.chars);
    this.dead = new Map(t.dead.map(([vk, after]) => [vk, charsOf(after)]));
    this.corrected = new Map();   // scan code -> VK from the events' keyCode
    this.pending = 0;             // the dead key's VK until the next key with a character
  }

  // The layout whose characters match navigator.keyboard.getLayoutMap()'s best (a Map or an
  // iterable of [code, key]); a tie goes to `fallback`.
  static chooseLayout(layoutMap, fallback = 'us') {
    let best = fallback, score = -1;
    for (const name of [fallback, ...LAYOUT_NAMES.filter((n) => n !== fallback)]) {
      const k = new Keyboard(name);
      let s = 0;
      for (const [code, key] of layoutMap) if (k.character(code) === key) s++;
      if (s > score) { best = name; score = s; }
    }
    return best;
  }

  // The unshifted character of a key code as a string ('' when none), for the layout choice.
  character(code) {
    const scan = CODE_SCAN[code];
    if (scan === undefined) return '';
    const c = this.chars.get(this.vscToVk.get(scan) || 0);
    return c && c.n ? cp1252.decode(Uint8Array.from(c.bytes.slice(0, 1))) : '';
  }

  // es2_sim_window_vk: the VK of a key, 0 for a key the native platform ignores.
  vk(code, numlock) {
    const scan = CODE_SCAN[code];
    if (scan === undefined) return 0;
    const pad = KEYPAD.indexOf(code);
    if (pad >= 0) return (numlock ? KEYPAD_ON : KEYPAD_OFF)[pad];
    if (code in FIXED_VK) return FIXED_VK[code];
    return this.corrected.get(scan) ?? (this.vscToVk.get(scan) || 0);
  }

  // An event's keyCode corrects the layout's VK of its key (only keys the layout decides;
  // keyCode 0 and 229, the IME's, say nothing).
  correct(code, keyCode) {
    const scan = CODE_SCAN[code];
    if (scan === undefined || KEYPAD.includes(code) || code in FIXED_VK || !keyCode || keyCode === 229) return;
    const vk = FIREFOX_KEYCODE[keyCode] ?? keyCode;
    if (vk > 0 && vk < 0xff) this.corrected.set(scan, vk);
  }

  // MapVirtualKeyA(vk, MAPVK_VK_TO_VSC): the scan code without the E0 prefix, 0 when none.
  vkToScan(vk) { return this.vkToVscTable.get(vk) || 0; }

  // TranslateMessage's characters for a key-down (main.c translate_key: ToAscii with all keys
  // up and the dead-key state kept), as the platform's translate answer: TRANSLATE.ONE | c,
  // TRANSLATE.TWO | c1 | c2 << 8, TRANSLATE.DEAD | the dead key's character, or 0.
  translate(vk) {
    let r;
    if (this.pending) {
      r = this.dead.get(this.pending).get(vk);
      if (r && vk !== VK_PACKET) this.pending = 0;
    } else {
      r = this.chars.get(vk);
    }
    if (!r) return 0;
    if (r.n < 0) {
      this.pending = vk;
      return (TRANSLATE.DEAD | r.bytes[0]) >>> 0;
    }
    if (r.n === 2) return (TRANSLATE.TWO | r.bytes[0] | r.bytes[1] << 8) >>> 0;
    return (TRANSLATE.ONE | r.bytes[0]) >>> 0;
  }
}

// The page's key events into the input ring and the key bitmap (docs/web_build.md section 12).
// `push(type, a, b, c, d, time)` appends an input entry (shared.mjs pushInput), `keys` is the
// shared bitmap (per VK 0x80 down, 0x01 toggled). An entry is KEY with the scan code, the VK
// and the flags UP, REPEAT and the modifiers after the event; a key the native platform ignores
// (VK 0) adds nothing. The bitmap holds both the left/right VKs (A0h..A5h) and the generic one.
//
// With a layout that has AltGr, Windows puts a left Ctrl before every right-Alt key-down and
// its key-up before the right Alt's; a browser on Windows delivers it, with the right Alt's
// timestamp. When no ControlLeft key-down came with that timestamp and no Ctrl is down, the
// wiring adds it (and then its key-up before the right Alt's). The toggles come from the
// event's getModifierState where it has one, else a toggle key's down flips them.
const KEY_FLAG = { UP: 1, REPEAT: 2, SHIFT: 4, ALT: 8, CTRL: 16, NUMLOCK: 32 };
const INPUT_KEY = 1;
const SIDE_VK = {
  ShiftLeft: [0x10, 0xa0], ShiftRight: [0x10, 0xa1], ControlLeft: [0x11, 0xa2], ControlRight: [0x11, 0xa3],
  AltLeft: [0x12, 0xa4], AltRight: [0x12, 0xa5],
};
const TOGGLES = { CapsLock: 0x14, NumLock: 0x90, ScrollLock: 0x91 };
const TOGGLE_VK = new Set(Object.values(TOGGLES));

export class KeyWiring {
  constructor(keyboard, { push, keys }) {
    this.keyboard = keyboard;
    this.push = push;
    this.keys = keys;
    this.held = new Set();            // codes down
    this.ctrlLeftTime = null;         // the timestamp of the last delivered ControlLeft key-down
    this.fakeCtrl = false;            // a left Ctrl added for the held right Alt
    this.fakeCtrls = 0;
  }

  // The toggles as the event reports them (getModifierState); false when it cannot.
  syncToggles(event) {
    if (typeof event.getModifierState !== 'function') return false;
    for (const [name, vk] of Object.entries(TOGGLES)) {
      if (event.getModifierState(name)) this.keys[vk] |= 1; else this.keys[vk] &= ~1;
    }
    return true;
  }

  flags(up, repeat) {
    const k = this.keys;
    return (up ? KEY_FLAG.UP : 0) | (repeat ? KEY_FLAG.REPEAT : 0) | (k[0x10] & 0x80 ? KEY_FLAG.SHIFT : 0)
      | (k[0x12] & 0x80 ? KEY_FLAG.ALT : 0) | (k[0x11] & 0x80 ? KEY_FLAG.CTRL : 0) | (k[0x90] & 1 ? KEY_FLAG.NUMLOCK : 0);
  }

  setDown(code, vk, down) {
    const side = SIDE_VK[code];
    if (down) this.held.add(code); else this.held.delete(code);
    if (side) {
      const [generic, own] = side;
      if (down) this.keys[own] |= 0x80; else this.keys[own] &= ~0x80;
      const any = Object.entries(SIDE_VK).some(([c, [g]]) => g === generic && this.held.has(c));
      if (any) this.keys[generic] |= 0x80; else this.keys[generic] &= ~0x80;
      return;
    }
    if (down) this.keys[vk] |= 0x80; else this.keys[vk] &= ~0x80;
  }

  emit(code, scan, vk, up, repeat, time, flip = false) {
    if (flip && !up && !repeat && TOGGLE_VK.has(vk)) this.keys[vk] ^= 1;
    this.setDown(code, vk, !up);
    this.push(INPUT_KEY, scan, vk, this.flags(up, repeat), 0, time);
  }

  // A keydown (down true) or keyup; true when the key is one the game reads (the page then
  // calls preventDefault).
  key(event, down) {
    const code = event.code;
    const scan = CODE_SCAN[code];
    if (scan === undefined) return false;
    this.keyboard.correct(code, event.keyCode);
    const synced = this.syncToggles(event);
    const time = Math.floor(event.timeStamp || 0);
    const repeat = down && !!event.repeat;
    if (code === 'ControlLeft' && down) this.ctrlLeftTime = event.timeStamp;
    if (code === 'AltRight' && this.keyboard.altgr) {
      if (down && (this.fakeCtrl || (this.ctrlLeftTime !== event.timeStamp && !(this.keys[0x11] & 0x80)))) {
        this.emit('ControlLeft', 0x1d, 0x11, false, this.fakeCtrl, time);
        this.fakeCtrl = true;
        this.fakeCtrls++;
      } else if (!down && this.fakeCtrl) {
        this.emit('ControlLeft', 0x1d, 0x11, true, false, time);
        this.fakeCtrl = false;
      }
    }
    const vk = this.keyboard.vk(code, (this.keys[0x90] & 1) !== 0);
    if (vk) this.emit(code, scan, vk, !down, repeat, time, !synced);
    return true;
  }

  keydown(event) { return this.key(event, true); }
  keyup(event) { return this.key(event, false); }

  // The focus left the page: no key-up will come for the keys held, so the bitmap lets them go
  // (the toggles stay).
  blur() {
    for (let vk = 0; vk < 256; vk++) this.keys[vk] &= ~0x80;
    this.held.clear();
    this.fakeCtrl = false;
  }
}
