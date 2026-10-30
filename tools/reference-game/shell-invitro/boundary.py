# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Rebuild an address-only preparation inventory from the hash-gated executable.

Disassembly stays in memory. Optional --trace logs resolve observed virtual
calls. Unresolved indirect transfers are reported, never called complete.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from es2shellprep import EXE_HASH

ROOTS = (0x41cd61, 0x408bb5, 0x411e40, 0x412f2a, 0x412781,
         0x41029b, 0x4107dc, 0x410e2e, 0x411fad, 0x4125e8,
         0x410055, 0x41072b, 0x410b11, 0x465cbc, 0x410aaf)

# Static receiver-type evidence, not copied vtable contents. Targets are read
# from the checked executable. Constructors: 0x44ed92, 0x44f5b6, 0x44f8e6,
# 0x403108. Archive opener 0x4027f3 constructs the underlying file stream;
# 0x4033fc..0x403524 selects the plain/codec-7/codec-9 receivers.
VIRTUAL_SLOTS = {
    0x402e46: ((0x47a730, 8),), 0x402e58: ((0x47a730, 24),),
    0x403145: ((0x47a7a8, 0),), 0x403187: ((0x47a800, 0),),
    0x4037b7: ((0x47a730, 8),), 0x4037f2: ((0x47a730, 8),),
    0x40380d: ((0x47a7a8, 8),), 0x403845: ((0x47a730, 8),),
    0x403860: ((0x47a800, 8),), 0x403a7b: ((0x47a730, 4),),
    0x403a8c: ((0x46d14c, 8),), 0x403aba: ((0x47a730, 24),),
    0x403acd: ((0x47a730, 4),),
    0x403ae4: ((0x47a730, 24), (0x47a7a8, 24), (0x47a800, 24)),
    0x403af3: ((0x47a730, 4),), 0x44f674: ((0x47a7a8, 16),),
    0x44fa6c: ((0x47a800, 16),),
    # Codec seek loops call their own read method; raw byte helpers use the
    # underlying file stream. Receiver flow: 0x44f742/762, 0x44fb16/36,
    # 0x44f800/84c/87b, 0x44ff96/fff2; source installation 0x44fa04.
    0x44f6fb: ((0x47a730, 8),), 0x44f8cd: ((0x47a7a8, 24),),
    0x44fad5: ((0x47a730, 8),), 0x44fce2: ((0x47a800, 24),),
    0x450a7c: ((0x47a730, 24),), 0x4540b8: ((0x47a730, 24),),
    0x46283b: ((0x47a730, 24),),
}


def inventory(exe, objdump, traces):
    data = exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXE_HASH:
        raise ValueError("reference SHA-256 mismatch")
    u16 = lambda p: struct.unpack_from("<H", data, p)[0]
    u32 = lambda p: struct.unpack_from("<I", data, p)[0]
    pe = u32(60)
    opt = pe + 24
    sections = [opt + u16(pe + 20) + 40*i for i in range(u16(pe + 6))]

    def offset(va):
        for s in sections:
            if u32(s+12) <= va-0x400000 < u32(s+12)+u32(s+16):
                return u32(s+20)+va-0x400000-u32(s+12)
        raise ValueError(f"unbacked address {va:x}")

    def string(va):
        p = offset(va)
        return data[p:data.index(0, p)].decode("ascii")

    imports = {}
    d = offset(0x400000 + u32(opt+104))
    while u32(d+12):
        dll = string(0x400000+u32(d+12))
        lookup = offset(0x400000+(u32(d) or u32(d+16)))
        iat = 0x400000+u32(d+16)
        while u32(lookup):
            entry = u32(lookup)
            imports[iat] = dll + "!" + (str(entry & 65535) if entry >> 31
                                        else string(0x400000+entry+2))
            lookup += 4
            iat += 4
        d += 20
    instructions = {}

    def disassemble(start=None):
        command = [str(objdump), "-d", "--x86-asm-syntax=intel"]
        if start is not None:
            command += [f"--start-address={start}", f"--stop-address={min(start+8192, 0x46bb00)}"]
        text = subprocess.check_output([*command, str(exe)], text=True)
        for line in text.splitlines():
            match = re.match(r"\s*([0-9a-f]+): ([0-9a-f ]+)\t(.*)", line)
            if match:
                a = int(match[1], 16)
                assembly = re.sub(r" <[^>]+>", "", match[3]).strip()
                instructions[a] = (len(match[2].split()), assembly)

    disassemble()
    observed = {}
    executed = set()
    for trace in traces:
        raw = trace.read_bytes()
        for line in raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8").splitlines():
            if line.startswith("EDGE "):
                _, site, target = line.split()
                observed.setdefault(int(site,16), set()).add(int(target,16))
            elif line.startswith("EXEC "):
                executed.add(int(line.split()[1],16))
    pending = list(ROOTS)
    functions = {}
    while pending:
        root = pending.pop()
        if root in functions or not 0x401000 <= root < 0x46bb00:
            continue
        # This assertion is a fail-stop boundary, not permission to construct UI.
        if root == 0x44e560:
            functions[root] = dict(boundary="assertion trap")
            continue
        work = [root]
        seen = set()
        calls, unresolved, references, used_imports = {}, {}, {}, set()
        missing = set()
        while work:
            pc = work.pop()
            if pc in seen or not 0x401000 <= pc < 0x46bb00:
                continue
            seen.add(pc)
            if pc not in instructions:
                disassemble(pc)
            if pc not in instructions:
                missing.add(pc)
                continue
            size, asm = instructions[pc]
            pieces = asm.split(None,1)
            op = pieces[0]
            operand = pieces[1] if len(pieces)>1 else ""
            # Conservative references: absolute operands and pointer immediates.
            # These are not all reads (some are destinations or addresses).
            refs = sorted({int(x,16) for x in re.findall(r"0x([0-9a-f]+)", operand)
                           if 0x400000 <= int(x,16) < 0x5ea000})
            if (op == "call" or op.startswith("j")) and re.fullmatch(r"0x[0-9a-f]+",operand):
                refs = []
            if refs:
                references[pc] = refs
            transfer = op == "call" or op.startswith("j")
            target_match = re.fullmatch(r"0x([0-9a-f]+)",operand) if transfer else None
            targets = {int(target_match[1],16)} if target_match else observed.get(pc,set())
            if pc in VIRTUAL_SLOTS:
                targets = {u32(offset(table+slot)) for table,slot in VIRTUAL_SLOTS[pc]}
            if root == 0x46b898 and pc in (0x46b913,0x46b91a,0x46b921,0x46b927):
                # Its sole caller in these roots is 0x412fd0. The callback
                # immediate is at 0x412fbd; mode=1 at 0x412fc2 reaches only
                # 0x46b913. Other ABI arms retain the same argument target.
                targets = {u32(offset(0x412fbd)+1)}
            table = re.fullmatch(r"dword ptr \[4\*eax \+ 0x([0-9a-f]+)\]", operand)
            if op == "jmp" and table and pc == 0x418348:
                # Bounds: cmp eax,5 at 0x41833f; ja at 0x418342.
                # Read targets from the hash-checked image, never embed its table.
                start = offset(int(table[1],16))
                targets = {u32(start+4*i) for i in range(6)}
            if transfer and not target_match:
                slots = set(refs) & imports.keys()
                if slots:
                    used_imports.update(imports[x] for x in slots)
                elif not targets:
                    unresolved[pc] = operand
            if op == "call":
                calls[pc] = sorted(targets)
                pending.extend(targets)
            elif op.startswith("j"):
                work.extend(targets)
            if op not in ("ret", "retl", "iret", "int3", "jmp"):
                work.append(pc+size)
        functions[root] = dict(calls={f"{k:08x}":[f"{x:08x}" for x in v] for k,v in sorted(calls.items())},
                               global_references={f"{k:08x}":[f"{x:08x}" for x in v] for k,v in sorted(references.items())},
                               imports=sorted(used_imports),
                               unresolved_indirect={f"{k:08x}":v for k,v in sorted(unresolved.items())},
                               undecoded=[f"{x:08x}" for x in sorted(missing)],
                               instructions=len(seen), executed_in_trace=len(seen & executed))
    return dict(executable_sha256=EXE_HASH, roots=[f"{x:08x}" for x in ROOTS],
                static_virtual_slots={f"{k:08x}":[dict(vtable=f"{t:08x}", slot=s,
                    target=f"{u32(offset(t+s)):08x}") for t,s in v] for k,v in VIRTUAL_SLOTS.items()},
                functions={f"{k:08x}":v for k,v in sorted(functions.items())},
                trace_instructions=len(executed),
                trace_calls={f"{k:08x}":[f"{x:08x}" for x in sorted(v)] for k,v in sorted(observed.items())})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--objdump", type=Path, default=Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe')
    p.add_argument("--trace", type=Path, action="append", default=[])
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    result = inventory(a.exe, a.objdump, a.trace)
    a.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(f"{len(result['functions'])} function roots; {result['trace_instructions']} traced instructions")


if __name__ == "__main__":
    main()
