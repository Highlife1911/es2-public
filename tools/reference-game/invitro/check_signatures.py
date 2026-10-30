"""Read-only signature audit of every proxy descriptor; no original bytes saved."""
import hashlib
import os
from pathlib import Path
import re
import struct

def expand_descriptor_macros(text):
    """Descriptor rows written through a function-like macro whose body starts
    with {ES2_HOOK_ID(p),EXPR, where EXPR pastes one macro parameter between an
    optional prefix and suffix (actor_virtuals_hooks.h: name##_sig with a
    prefix; stage_flush_hooks.h: name##_sig, the name the only argument;
    weapon_methods_hooks.h: weapon_methods_##sig##_sig, a second argument),
    become the literal rows the parser below expects; the #define itself is
    dropped so its parameter names are not taken for hooks."""
    pattern = re.compile(r"#define (\w+)\(([^)]*)\)[ \t]*\\\n\s*\{ES2_HOOK_ID\((\w+)\),\s*([\w#]+)")
    for macro, params, id_param, expr in pattern.findall(text):
        names = [p.strip() for p in params.split(",")]
        parts = expr.split("##")
        pasted = [i for i, part in enumerate(parts) if part in names]
        if id_param not in names or len(pasted) != 1:
            continue
        k = pasted[0]
        prefix, suffix = "".join(parts[:k]), "".join(parts[k + 1:])
        id_index, sig_index = names.index(id_param), names.index(parts[k])
        text = re.sub(r"#define " + macro + r"\((?:[^\n]*\\\n)*[^\n]*\n", "", text)

        def row(m):
            args = [a.strip() for a in m.group(1).split(",")]
            return "{ES2_HOOK_ID(%s),%s%s%s)" % (args[id_index], prefix, args[sig_index], suffix)
        text = re.sub(r"\b" + macro + r"\(([^()]*)\)", row, text)
    return text

def main():
    value = os.environ.get("ES2_DATA_ROOT")
    if not value:
        print("SKIP: ES2_DATA_ROOT is not set")
        return
    root = Path(value).resolve(strict=True)
    choices = [root / "VER95/DBSIM.EXE", root / "extracted/cd/VER95/DBSIM.EXE"]
    choices = [p.resolve() for p in choices if p.is_file()]
    if len(choices) != 1 or not choices[0].is_relative_to(root):
        raise ValueError("ambiguous or escaping reference path")
    data = choices[0].read_bytes()
    if hashlib.sha256(data).hexdigest() != "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad":
        raise ValueError("reference SHA-256 mismatch")
    pe = struct.unpack_from("<I", data, 60)[0]
    section = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(struct.unpack_from("<H", data, pe + 6)[0]):
        _, rva, size, offset = struct.unpack_from("<IIII", data, section + 40*i + 8)
        sections.append((rva, size, offset))
    proxy = Path(__file__).resolve().parents[1] / "proxy"
    registry = re.findall(r"ES2_HOOK\((\w+),\s*(0x[0-9a-fA-F]+),\s*(\w+)\[(\d+)\],\s*([^,]+),\s*(\w+)\)", (proxy / "hook_registry.h").read_text())
    # Signature arrays may live in an included header (building_weapons_hooks.h)
    # and share one declaration ("static const BYTE a[]={..},b[]={..};").
    arrays = {}
    for header in sorted(proxy.glob("*_hooks.h")):
        for declaration in re.findall(r"static const BYTE ([^;]+);", header.read_text()):
            for key, body in re.findall(r"(\w+)\[\]\s*=\s*\{([^}]+)\}", declaration):
                arrays[key] = bytes(int(word.strip(), 0) for word in body.split(","))
    # The descriptor array names its header; the group name need not match
    # the file name (group effect lives in effect_create_hooks.h).
    owners = {}
    for header in sorted(proxy.glob("*_hooks.h")):
        text = expand_descriptor_macros(header.read_text())
        for name in re.findall(r"es2_hook_descriptor\s+(\w+)\[\]\s*=", text):
            owners[name] = text
    count = 0
    for hook, address, descriptor, slot, extension, group in registry:
        text = owners[descriptor]
        descriptors = re.findall(r"\{ES2_HOOK_ID\((\w+)\),\s*(\w+)", text)
        found, key = descriptors[int(slot)]
        if found != hook:
            raise ValueError(f"registry descriptor order mismatch: {hook}")
        rva, expected = int(address, 16), arrays[key]
        matches = [offset+rva-start for start, size, offset in sections if start <= rva and rva+len(expected) <= start+size]
        if len(matches) != 1 or data[matches[0]:matches[0]+len(expected)] != expected:
            raise ValueError(f"signature mismatch: {hook}")
        count += 1
        print(f"PASS: {hook} VA=0x{rva+0x400000:08x} signature_length={len(expected)}")
    if count != len(registry) or len({row[0] for row in registry}) != count:
        raise ValueError("duplicate or missing registry entries")
    print(f"PASS: {count}/{len(registry)} registry signatures, hash gate verified")

if __name__ == "__main__":
    main()
