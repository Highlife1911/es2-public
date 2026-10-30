#!/usr/bin/env python3
"""The keyboard layout tables of the web build (docs/web_build.md section 12).

The native window platform asks Windows for three things per key (sim/src/platform_sdl.c,
sim/src/main.c): the virtual key of a scan code (`MapVirtualKeyW`, MAPVK_VSC_TO_VK_EX), the scan
code of a virtual key (`MapVirtualKeyA`, MAPVK_VK_TO_VSC) and TranslateMessage's characters
(`ToAscii` with an all-up key state, the thread's dead-key state kept), plus whether the layout
has AltGr (`ToUnicodeEx` with Ctrl and right Alt down). The web build has no such API, so it
carries these answers per layout.

`dump` asks Windows on this machine, changing no setting:
  - a layout already in the session (`GetKeyboardLayoutList`) answers through the API itself;
  - every layout is also read from its DLL's `KbdLayerDescriptor` tables (LoadLibrary of
    kbdgr.dll, kbdfr.dll or kbdus.dll; no layout is loaded into the session) and answered by
    this file's model of the API over those tables.
For a layout with both sources the two must agree, or the dump fails; the dump keeps the API's
answers where it has them. `generate` turns a dump into web/key_layouts.mjs.

usage: keyboard_layouts.py dump --out DUMP.json
       keyboard_layouts.py generate --dump DUMP.json --out key_layouts.mjs
       keyboard_layouts.py check --dump DUMP.json [--mjs key_layouts.mjs]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

# name, KLID, layout DLL
LAYOUTS = [("de", 0x00000407, "kbdgr.dll"), ("fr", 0x0000040C, "kbdfr.dll"), ("us", 0x00000409, "kbdus.dll")]

MAPVK_VK_TO_VSC = 0
MAPVK_VSC_TO_VK_EX = 3
VK_SPACE = 0x20
VK_PACKET = 0xE7
VK_SHIFT, VK_CONTROL, VK_MENU = 0x10, 0x11, 0x12
VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU = 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5
WCH_NONE, WCH_DEAD, WCH_LGTR = 0xF000, 0xF001, 0xF002
KBDEXT, KBDNUMPAD = 0x100, 0x800
KLLF_ALTGR = 0x0001
# The model's shift-state bits (kbd.h): KBDSHIFT 1, KBDCTRL 2, KBDALT 4.
KBDCTRL, KBDALT = 2, 4


# ---- the answers of one layout ---------------------------------------------------------------
#
# A dump entry per layout:
#   vsc_to_vk  [[code, vk], ...]  MAPVK_VSC_TO_VK_EX of code 01..7F and E001..E07F, nonzero only
#   vk_to_vsc  [[vk, scan], ...]  MAPVK_VK_TO_VSC of VK 01..FE, nonzero only
#   chars      [[vk, n, [bytes]], ...]  ToAscii(vk, its scan, all keys up) with no dead key
#              pending; n -1 a dead key (the byte its spacing character), 1 one character
#   dead       [[dead_vk, [[vk, n, [bytes]], ...]], ...]  ToAscii of vk right after dead_vk;
#              vks that answer 0 (the dead key stays pending) are left out
#   altgr      the layout has AltGr (es2_sim_window_layout_altgr's test)

def dead_probe_keys():
    return range(1, 0xFF)


class ApiLayout:
    """The answers of a layout in this session, from the API."""

    def __init__(self, hkl):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.hkl = ctypes.c_void_p(hkl)
        u = ctypes.WinDLL("user32")
        self.map = u.MapVirtualKeyExW
        self.map.argtypes = [wintypes.UINT, wintypes.UINT, ctypes.c_void_p]
        self.map.restype = wintypes.UINT
        self.to_ascii = u.ToAsciiEx
        self.to_ascii.argtypes = [wintypes.UINT, wintypes.UINT, ctypes.c_char_p, ctypes.c_void_p, wintypes.UINT,
                                  ctypes.c_void_p]
        self.to_ascii.restype = ctypes.c_int
        self.to_unicode = u.ToUnicodeEx
        self.to_unicode.argtypes = [wintypes.UINT, wintypes.UINT, ctypes.c_char_p, ctypes.c_wchar_p, ctypes.c_int,
                                    wintypes.UINT, ctypes.c_void_p]
        self.to_unicode.restype = ctypes.c_int

    def vsc_to_vk(self, code):
        return self.map(code, MAPVK_VSC_TO_VK_EX, self.hkl)

    def vk_to_vsc(self, vk):
        return self.map(vk, MAPVK_VK_TO_VSC, self.hkl)

    def key(self, vk):
        """One ToAscii with all keys up: (n, bytes)."""
        state = bytes(256)
        out = (self.ctypes.c_ubyte * 4)()
        n = self.to_ascii(vk, self.vk_to_vsc(vk), state, self.ctypes.addressof(out), 0, self.hkl)
        count = 1 if n < 0 else n
        return n, list(out[:count]) if n else []

    def reset(self):
        # A space ends any pending dead key; from a clean state it changes nothing.
        self.key(VK_SPACE)

    def altgr(self):
        state = bytearray(256)
        for vk in (VK_CONTROL, VK_MENU, VK_LCONTROL, VK_RMENU):
            state[vk] = 0x80
        out = self.ctypes.create_unicode_buffer(4)
        for vk in range(0x30, 0xE3):
            scan = self.vk_to_vsc(vk)
            if not scan:
                continue
            n = self.to_unicode(vk, scan, bytes(state), out, 4, 4, self.hkl)
            if n > 0 and ord(out[0]) >= 0x20:
                return True
        return False


class TableLayout:
    """The answers of a layout DLL's KbdLayerDescriptor tables, by the model of the API below."""

    def __init__(self, dll):
        import ctypes
        self.ctypes = ctypes
        path = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / dll
        self.path = path
        self.sha256 = hashlib.sha256(path.read_bytes()).hexdigest()
        module = ctypes.WinDLL(str(path))
        descriptor = module.KbdLayerDescriptor
        descriptor.restype = ctypes.c_void_p
        descriptor.argtypes = []
        self.read_tables(descriptor())
        self.pending = 0

    # Little-endian reads of the DLL's mapped tables (64-bit layout of kbd.h's KBDTABLES).
    def mem(self, address, size):
        return self.ctypes.string_at(address, size)

    def ptr(self, address):
        return struct.unpack("<Q", self.mem(address, 8))[0]

    def read_tables(self, t):
        if self.ctypes.sizeof(self.ctypes.c_void_p) != 8:
            raise SystemExit("the table reader expects a 64-bit Python")
        modifiers = self.ptr(t + 0)
        wchar_tables = self.ptr(t + 8)
        dead_keys = self.ptr(t + 16)
        vsc_to_vk = self.ptr(t + 48)
        max_vsc = self.mem(t + 56, 1)[0]
        e0 = self.ptr(t + 64)
        self.locale_flags = struct.unpack("<I", self.mem(t + 80, 4))[0]
        max_bits = struct.unpack("<H", self.mem(modifiers + 8, 2))[0]
        self.mod_number = list(self.mem(modifiers + 10, max_bits + 1))
        self.vk_bits = {}
        at = self.ptr(modifiers)
        while True:
            vk, bits = self.mem(at, 2)
            if not vk:
                break
            self.vk_bits[vk] = bits
            at += 2
        self.vsc = list(struct.unpack("<%dH" % max_vsc, self.mem(vsc_to_vk, 2 * max_vsc)))
        self.e0 = []
        at = e0
        while True:
            sc, vk = struct.unpack("<BxH", self.mem(at, 4))
            if not sc:
                break
            self.e0.append((sc, vk))
            at += 4
        # Each row: vk, attributes, the characters per modification; a WCH_DEAD row is followed
        # by a row of VK 0xFF that holds the dead keys' spacing characters.
        self.rows = []
        at = wchar_tables
        while True:
            table = self.ptr(at)
            if not table:
                break
            mods, size = self.mem(at + 8, 2)
            row = table
            while True:
                data = self.mem(row, size)
                if not data[0]:
                    break
                self.rows.append((data[0], data[1], list(struct.unpack("<%dH" % mods, data[2:2 + 2 * mods]))))
                row += size
            at += 16
        self.dead_keys = {}
        at = dead_keys
        while at:
            both, composed, flags = struct.unpack("<IHH", self.mem(at, 8))
            if not both:
                break
            self.dead_keys[both] = (composed, flags)
            at += 8

    def vsc_to_vk(self, code):
        if code & 0xE000 == 0xE000:
            for sc, vk in self.e0:
                if sc == code & 0xFF:
                    return 0 if vk & 0xFF == 0xFF else vk & 0xFF
            return 0
        if code >= len(self.vsc):
            return 0
        vk = self.vsc[code] & 0xFF
        return 0 if vk == 0xFF else vk

    # The keypad's VKs with Num Lock on stand on the cursor-block VKs of the KBDNUMPAD scans.
    NUMPAD = {0x60: 0x2D, 0x61: 0x23, 0x62: 0x28, 0x63: 0x22, 0x64: 0x25, 0x65: 0x0C, 0x66: 0x27, 0x67: 0x24,
              0x68: 0x26, 0x69: 0x21, 0x6E: 0x2E}

    def vk_to_vsc(self, vk):
        vk = {VK_SHIFT: VK_LSHIFT, VK_CONTROL: VK_LCONTROL, VK_MENU: VK_LMENU}.get(vk, vk)
        if vk in self.NUMPAD:
            base = self.NUMPAD[vk]
            for sc, entry in enumerate(self.vsc):
                if entry & KBDNUMPAD and entry & 0xFF == base:
                    return sc
        for sc, entry in enumerate(self.vsc):
            if sc and entry & 0xFF == vk:
                return sc
        for sc, entry in self.e0:
            if entry & 0xFF == vk:
                return sc
        return 0

    def char(self, vk, bits):
        """The row's character for a shift state: (wch, dead) or (None, False)."""
        if bits >= len(self.mod_number):
            return None, False
        mod = self.mod_number[bits]
        for i, (row_vk, _, chars) in enumerate(self.rows):
            if row_vk != vk:
                continue
            if mod >= len(chars) or mod == 0x0F:
                return None, False
            wch = chars[mod]
            if wch == WCH_NONE:
                return None, False
            if wch == WCH_DEAD:
                return self.rows[i + 1][2][mod], True
            if wch == WCH_LGTR:
                return None, False
            return wch, False
        return None, False

    @staticmethod
    def ansi(wch):
        try:
            return list(chr(wch).encode("cp1252"))
        except UnicodeEncodeError:
            return [ord("?")]

    def key(self, vk):
        # VK_PACKET answers one NUL (its character would be in the scan's high word) and leaves a
        # pending dead key pending (measured through the API).
        if vk == VK_PACKET:
            return 1, [0]
        wch, dead = self.char(vk, 0)
        if wch is None:
            return 0, []
        if self.pending:
            first, self.pending = self.pending, 0
            hit = self.dead_keys.get(first << 16 | wch)
            if hit:
                return 1, self.ansi(hit[0])
            return 2, self.ansi(first) + self.ansi(wch)
        if dead:
            self.pending = wch
            return -1, self.ansi(wch)
        return 1, self.ansi(wch)

    def reset(self):
        self.pending = 0

    def altgr(self):
        for vk in range(0x30, 0xE3):
            if not self.vk_to_vsc(vk):
                continue
            wch, _ = self.char(vk, KBDCTRL | KBDALT)
            if wch is not None and wch >= 0x20:
                return True
        return False


def answers(layout):
    vsc = []
    for code in list(range(1, 0x80)) + list(range(0xE001, 0xE080)):
        vk = layout.vsc_to_vk(code)
        if vk:
            vsc.append([code, vk])
    vk_to_vsc = []
    for vk in range(1, 0xFF):
        sc = layout.vk_to_vsc(vk)
        if sc:
            vk_to_vsc.append([vk, sc])
    chars, dead_vks = [], []
    for vk in range(1, 0xFF):
        layout.reset()
        n, data = layout.key(vk)
        if n:
            chars.append([vk, n, data])
        if n < 0:
            dead_vks.append(vk)
    dead = []
    for dead_vk in dead_vks:
        after = []
        for vk in dead_probe_keys():
            layout.reset()
            layout.key(dead_vk)
            n, data = layout.key(vk)
            if n:
                after.append([vk, n, data])
        dead.append([dead_vk, after])
    layout.reset()
    return {"vsc_to_vk": vsc, "vk_to_vsc": vk_to_vsc, "chars": chars, "dead": dead, "altgr": layout.altgr()}


def session_layouts():
    import ctypes
    u = ctypes.WinDLL("user32")
    n = u.GetKeyboardLayoutList(0, None)
    buffer = (ctypes.c_void_p * max(n, 1))()
    n = u.GetKeyboardLayoutList(n, buffer)
    return [h or 0 for h in buffer[:n]]


def differences(a, b):
    out = []
    for field in ("vsc_to_vk", "vk_to_vsc", "chars", "dead", "altgr"):
        if a[field] != b[field]:
            if isinstance(a[field], list):
                sa = {json.dumps(x) for x in a[field]}
                sb = {json.dumps(x) for x in b[field]}
                out.append("%s: only api %s; only tables %s" % (field, sorted(sa - sb)[:8], sorted(sb - sa)[:8]))
            else:
                out.append("%s: api %r, tables %r" % (field, a[field], b[field]))
    return out


def dump(args):
    loaded = session_layouts()
    result = {"layouts": {}}
    failed = False
    for name, klid, dll in LAYOUTS:
        tables = TableLayout(dll)
        from_tables = answers(tables)
        # A session HKL of this language with the default device (low and high word the KLID).
        hkl = next((h for h in loaded if h & 0xFFFFFFFF == (klid << 16 | klid)), None)
        entry = {"dll": dll, "dll_sha256": tables.sha256, "locale_flags": tables.locale_flags}
        if hkl is not None:
            from_api = answers(ApiLayout(hkl))
            diff = differences(from_api, from_tables)
            for line in diff:
                print("%s: %s" % (name, line))
            failed |= bool(diff)
            entry["source"] = "api"
            entry["tables_agree"] = not diff
            entry.update(from_api)
        else:
            entry["source"] = "tables"
            entry.update(from_tables)
        print("%s: %s (%s), %d scan codes, %d VKs, %d characters, %d dead keys, AltGr %s" % (
            name, entry["source"], dll, len(entry["vsc_to_vk"]), len(entry["vk_to_vsc"]), len(entry["chars"]),
            len(entry["dead"]), entry["altgr"]))
        result["layouts"][name] = entry
    Path(args.out).write_bytes(dump_text(result).encode())
    return 1 if failed else 0


def dump_text(result):
    """JSON with one line per field, so a changed table shows as a changed line."""
    compact = lambda v: json.dumps(v, separators=(",", ":"))
    layouts = []
    for name, entry in result["layouts"].items():
        fields = ",\n".join("   %s: %s" % (json.dumps(k), compact(v)) for k, v in entry.items())
        layouts.append("  %s: {\n%s\n  }" % (json.dumps(name), fields))
    return '{\n "layouts": {\n%s\n }\n}\n' % ",\n".join(layouts)


# ---- the generated module --------------------------------------------------------------------

def module_text(dump_data):
    lines = [
        "// Generated by tools/web/keyboard_layouts.py from tools/web/testdata/keyboard_layouts.json; do not",
        "// edit. Per layout (docs/web_build.md section 12): vscToVk [code, vk] (code 0xE0xx for E0 keys,",
        "// MapVirtualKeyW's MAPVK_VSC_TO_VK_EX), vkToVsc [vk, scan] (MAPVK_VK_TO_VSC), chars [vk, n, bytes]",
        "// (ToAscii with all keys up: n -1 a dead key, 1 one character), dead [deadVk, [[vk, n, bytes]]]",
        "// (ToAscii right after the dead key; 2 two characters), altgr.",
        "export const KEY_LAYOUTS = {",
    ]
    for name, entry in dump_data["layouts"].items():
        lines.append("  %s: {" % name)
        for field, key in (("vsc_to_vk", "vscToVk"), ("vk_to_vsc", "vkToVsc"), ("chars", "chars"), ("dead", "dead")):
            lines.append("    %s: %s," % (key, json.dumps(entry[field], separators=(",", ":"))))
        lines.append("    altgr: %s," % ("true" if entry["altgr"] else "false"))
        lines.append("  },")
    lines.append("};")
    return "\n".join(lines) + "\n"


def generate(args):
    data = json.loads(Path(args.dump).read_text(encoding="utf-8"))
    Path(args.out).write_bytes(module_text(data).encode())
    return 0


def check(args):
    """Compares the dump with this machine (tables, and the API for session layouts) and, with
    --mjs, the generated module with the dump."""
    data = json.loads(Path(args.dump).read_text(encoding="utf-8"))
    problems = []
    loaded = session_layouts() if os.name == "nt" else []
    for name, klid, dll in LAYOUTS:
        entry = data["layouts"][name]
        if os.name != "nt":
            print("%s: not Windows, machine check skipped" % name)
            continue
        tables = TableLayout(dll)
        if tables.sha256 != entry["dll_sha256"]:
            print("%s: %s differs from the dump's (%s); tables check skipped" % (name, dll, tables.sha256))
        else:
            for line in differences(entry, answers(tables)):
                problems.append("%s tables: %s" % (name, line))
            print("%s: tables of %s checked" % (name, dll))
        hkl = next((h for h in loaded if h & 0xFFFFFFFF == (klid << 16 | klid)), None)
        if hkl is None:
            print("%s: not in this session, API check skipped" % name)
        else:
            for line in differences(answers(ApiLayout(hkl)), entry):
                problems.append("%s api: %s" % (name, line))
            print("%s: API checked" % name)
    if args.mjs and Path(args.mjs).read_bytes() != module_text(data).encode():
        problems.append("%s is not the module generated from the dump" % args.mjs)
    for p in problems:
        print(p)
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("dump")
    p.add_argument("--out", required=True)
    p = sub.add_parser("generate")
    p.add_argument("--dump", required=True)
    p.add_argument("--out", required=True)
    p = sub.add_parser("check")
    p.add_argument("--dump", required=True)
    p.add_argument("--mjs")
    args = parser.parse_args()
    return {"dump": dump, "generate": generate, "check": check}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
