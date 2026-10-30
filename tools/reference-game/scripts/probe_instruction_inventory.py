# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Read-only boundary/signature inventory, using the project's installed LLVM.

Only metadata is emitted: VA and a one-byte push-register signature. No
disassembly or original binary is saved. Function identity is supplied by
the investigator; an instruction boundary alone does not prove it.

Rule (docs/reference-game/dbsim_map.md, section "probe inventory"):

1. Linear sweep: one `llvm-objdump -d` pass over the whole PE.
   A linear sweep loses the instruction boundaries behind zero padding and
   data in CODE (jump tables, RTTI records) until it happens to resynchronize.
2. Synchronized descent: llvm-objdump decodes the CODE bytes again, wrapped
   in an ELF image whose symbols are the known code starts, so its sweep
   restarts at every start. Control flow is followed from the roots
   - the PE entry point and the export table targets in CODE,
   - every base-relocated dword whose target in CODE begins with
     `55 8b ec` (push ebp; mov ebp, esp): vtable slots, callbacks,
   - every direct `call rel32` target in CODE of the linear sweep,
   and from every reached instruction to its fall-through, its conditional
   or direct jump target, its direct call target and, for
   `jmp *T(,reg,4)`, every entry of the table (the consecutive
   base-relocated dwords at T that point into CODE). ret, int3, hlt and the
   other indirect jumps end a path. A target that is not a decoded start yet
   becomes a symbol of the next pass; passes repeat until none is missing.
   Two reached instructions that overlap, a reached byte the decoder
   rejects, an unknown control transfer, a path that leaves CODE, a final
   pass that decodes a reached instruction differently or a jump table over
   reached code abort the run.
3. Inventory = linear one-byte PUSH r32 entries (not ESP) + reached one-byte
   PUSH r32 instructions - linear entries proven not to be boundaries
   (inside a reached instruction or inside a jump table). Linear entries in
   bytes the descent never reaches (unreferenced code, data in CODE) are
   kept unconfirmed.

Entry kinds (dbsim_map.md, section "entry kinds"):

4. A second descent adds the operand pointers as roots: targets in CODE,
   outside reached code, of base-relocated dwords inside reached
   `push imm32`, `mov r32, imm32` and `mov r/m32, imm32` instructions
   (callbacks such as the stage and pool callbacks). It must reproduce every
   instruction of the first descent, and no jump table may overlap its code.
   Known function starts = the roots it reached, the targets of every
   reached `call rel32`, and every reached instruction start that a
   base-relocated dword outside the jump tables points to. A start whose
   first instruction
   is a one-byte PUSH r32 is probed through the inventory above. Any other
   start whose first instruction, as decoded by the descent, is one of
   ENTRY_KINDS gets a row of the second table `probe_entry_kinds`: the probe
   engine emulates exactly that instruction (entry_probe_engine.h); so does
   a PUSH r32 start that only the second descent reaches (EP_PUSH). The
   remaining starts stay unprobed; `--starts` lists all of them with the
   reason. The first table is unchanged by this step.

Modules (docs/reference-game/vshell_map.md): `--module dbsim` (the
default, proxy/probe_entry_inventory.h) or `--module vshell`
(proxy/vshell_probe_inventory.h from the reference VER95\\VSHELL.EXE, same rule
and tables under the vshell_probe_ prefix, plus a third table with every
probed known start, the default site list of proxy/vshell_probes.h).
"""
import argparse
import bisect
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

MODULE_HASH = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
# The module parameter. Each module is bound to one reference file
# (SHA-256) and renders its own header names, so both headers can be
# included in one translation unit (media_proxy.c). The DBSIM output does
# not change with the parameter.
MODULES = {
    'dbsim': dict(hash=MODULE_HASH, title='DBSIM', guard='ES2_PROBE_ENTRY_INVENTORY_H', prefix='probe'),
    'vshell': dict(hash='1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e', title='VSHELL',
                   guard='ES2_VSHELL_PROBE_INVENTORY_H', prefix='vshell_probe'),
}
PUSH = frozenset((0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57))
FRAME = b'\x55\x8b\xec'
LINEAR_PUSH = re.compile(r'\s*([0-9a-f]+):\s+(5[0-35-7])\s+pushl?\s')
LINEAR_CALL = re.compile(r'\s*[0-9a-f]+:\s[0-9a-f ]+\tcalll\t0x([0-9a-f]+)\b')
DECODED = re.compile(r'\s*([0-9a-f]+):\s([0-9a-f ]+)\t(.*)$')
COMMENT = re.compile(r'\s*(?:<[^>]*>|#.*)')
TARGET = re.compile(r'0x([0-9a-f]+)$')
TABLE = re.compile(r'\*(?:%cs:)?0x([0-9a-f]+)\(,%e(?:ax|bx|cx|dx|si|di|bp),4\)')
PREFIXES = frozenset(('rep', 'repne', 'lock', 'data16', 'addr16', 'cs', 'ds', 'es', 'fs', 'gs', 'ss'))
CONDITIONAL = frozenset(('jo', 'jno', 'jb', 'jae', 'je', 'jne', 'jbe', 'ja', 'js', 'jns', 'jp', 'jnp',
                         'jl', 'jge', 'jle', 'jg', 'jcxz', 'jecxz', 'loop', 'loope', 'loopne'))
ENDS = frozenset(('retl', 'int3', 'hlt', 'ud2'))
TRANSFER = re.compile(r'(?:j|call|ret|lret|iret|ljmp|lcall|loop|int|sys|ud)')


def _abs32_modrm(code, opcode_length):
    """ModR/M byte after the opcode selects [disp32] (mod 00, r/m 101) and a register other than ESP."""
    if len(code) <= opcode_length:
        return False
    modrm = code[opcode_length]
    return modrm & 0xc7 == 0x05 and (modrm >> 3) & 7 != 4


# Emulated first instructions of a function start: (C enumerator, size, byte
# test). The engine checks the same bytes again before it patches (ep_kind_size).
ENTRY_KINDS = (
    ('EP_ENTER', 4, lambda c: c[0] == 0xc8 and c[3] == 0),                 # enter imm16, 0
    ('EP_CALL_REL32', 5, lambda c: c[0] == 0xe8),                         # call rel32
    ('EP_JMP_REL32', 5, lambda c: c[0] == 0xe9),                          # jmp rel32
    ('EP_JMP_ABS', 6, lambda c: c[:2] == b'\xff\x25'),                    # jmp [abs32] (import thunk)
    ('EP_MOV_EAX_ABS', 5, lambda c: c[0] == 0xa1),                        # mov eax, [abs32]
    ('EP_MOV_AL_ABS', 5, lambda c: c[0] == 0xa0),                         # mov al, [abs32]
    ('EP_MOV_AX_ABS', 6, lambda c: c[:2] == b'\x66\xa1'),                 # mov ax, [abs32]
    ('EP_MOV_R32_ABS', 6, lambda c: c[0] == 0x8b and _abs32_modrm(c, 1)),  # mov r32, [abs32]
    ('EP_MOV_R32_IMM', 5, lambda c: 0xb8 <= c[0] <= 0xbf and c[0] != 0xbc),  # mov r32, imm32
    ('EP_MOV_R16_IMM', 4, lambda c: c[0] == 0x66 and 0xb8 <= c[1] <= 0xbf and c[1] != 0xbc),  # mov r16, imm16
    ('EP_MOV_M8_IMM', 7, lambda c: c[:2] == b'\xc6\x05'),                 # mov byte [abs32], imm8
    ('EP_MOVSX_R32_M8', 7, lambda c: c[:2] == b'\x0f\xbe' and _abs32_modrm(c, 2)),  # movsx r32, byte [abs32]
    ('EP_PUSH_IMM8', 2, lambda c: c[0] == 0x6a),                          # push imm8
    ('EP_PUSH_IMM32', 5, lambda c: c[0] == 0x68),                         # push imm32
    ('EP_RET', 1, lambda c: c[0] == 0xc3),                                # ret
    ('EP_CMP_M32_IMM8', 7, lambda c: c[:2] == b'\x83\x3d'),               # cmp dword [abs32], imm8
    ('EP_CMP_M8_IMM8', 7, lambda c: c[:2] == b'\x80\x3d'),                # cmp byte [abs32], imm8
    ('EP_ADD_ESP_IMM8', 3, lambda c: c[:2] == b'\x83\xc4'),               # add esp, imm8
    ('EP_MOVSX_R32_M16', 7, lambda c: c[:2] == b'\x0f\xbf' and _abs32_modrm(c, 2)),  # movsx r32, word [abs32]
    ('EP_CMP_M16_IMM8', 8, lambda c: c[:3] == b'\x66\x83\x3d'),           # cmp word [abs32], imm8
    ('EP_INC_M32', 6, lambda c: c[:2] == b'\xff\x05'),                    # inc dword [abs32]
    ('EP_DEC_M32', 6, lambda c: c[:2] == b'\xff\x0d'),                    # dec dword [abs32]
    ('EP_MOV_R32_R32', 2, lambda c: c[0] == 0x8b and c[1] >= 0xc0 and (c[1] >> 3) & 7 != 4),  # mov r32, r32
    ('EP_XOR_R32_SELF', 2, lambda c: c[0] == 0x33 and c[1] >= 0xc0 and (c[1] >> 3) & 7 == c[1] & 7
     and c[1] & 7 != 4),                                                  # xor r32, r32 (same register)
    ('EP_ADD_M8_IMM8', 7, lambda c: c[:2] == b'\x80\x05'),                # add byte [abs32], imm8
    ('EP_MOV_M16_IMM', 9, lambda c: c[:3] == b'\x66\xc7\x05'),           # mov word [abs32], imm16
)


def entry_kind(code, size):
    """The ENTRY_KINDS name of an instruction (its bytes from the start, its decoded size), else None."""
    for name, length, test in ENTRY_KINDS:
        if size == length and len(code) >= length and test(code):
            return name
    return None


class PeImage:
    """Sections, entry point, exports and base relocations of a PE32 image."""

    def __init__(self, data):
        self.data = data
        pe = self.u32(0x3c)
        if data[pe:pe + 4] != b'PE\0\0' or self.u16(pe + 24) != 0x10b:
            raise ValueError('Not a PE32 image')
        count, optional, header = self.u16(pe + 6), self.u16(pe + 20), pe + 24
        self.base = self.u32(header + 28)
        self.entry = self.base + self.u32(header + 16)
        directories = [(self.u32(header + 96 + 8 * i), self.u32(header + 100 + 8 * i))
                       for i in range(min(16, self.u32(header + 92)))] + [(0, 0)] * 16
        self.sections = []
        for i in range(count):
            at = header + optional + 40 * i
            virtual, address, raw, pointer = struct.unpack_from('<IIII', data, at + 8)
            self.sections.append((self.base + address, max(virtual, raw), pointer, raw, self.u32(at + 36)))
        code = [s for s in self.sections if s[4] & 0x20000000]
        if len(code) != 1:
            raise ValueError('Expected exactly one executable section')
        self.code_va, _, pointer, raw, _ = code[0]
        self.code = data[pointer:pointer + raw]
        self.relocations = set()
        address, size = directories[5]
        at = self.offset(self.base + address) if size else 0
        end = at + size
        while at < end:
            page, block = self.u32(at), self.u32(at + 4)
            if block < 8:
                raise ValueError('Bad relocation block')
            for i in range((block - 8) // 2):
                item = self.u16(at + 8 + 2 * i)
                if item >> 12 == 3:
                    self.relocations.add(self.base + page + (item & 0xfff))
                elif item >> 12:
                    raise ValueError('Unexpected relocation type')
            at += block
        self.exports = set()
        address, size = directories[0]
        if size:
            at = self.offset(self.base + address)
            functions = self.offset(self.base + self.u32(at + 28))
            for i in range(self.u32(at + 20)):
                if self.u32(functions + 4 * i):
                    self.exports.add(self.base + self.u32(functions + 4 * i))

    def u16(self, at):
        return struct.unpack_from('<H', self.data, at)[0]

    def u32(self, at):
        return struct.unpack_from('<I', self.data, at)[0]

    def offset(self, va):
        """File offset of a VA backed by file bytes, else None."""
        for start, size, pointer, raw, _ in self.sections:
            if start <= va < start + size:
                return pointer + va - start if va - start < raw else None
        return None

    def dword(self, va):
        at = self.offset(va)
        return None if at is None or self.offset(va + 3) != at + 3 else self.u32(at)

    def in_code(self, va):
        return self.code_va <= va < self.code_va + len(self.code)

    def code_bytes(self, va, size):
        return self.code[va - self.code_va:va - self.code_va + size]


def linear_sweep(data, objdump):
    """The previous inventory rule (one linear sweep over the whole PE) and its call targets."""
    text = subprocess.run([str(objdump), '-d', '-'], input=data,
                          capture_output=True, check=True).stdout.decode('utf-8')
    pushes, calls = {}, set()
    for line in text.splitlines():
        match = LINEAR_PUSH.match(line)
        if match:
            pushes[int(match[1], 16)] = int(match[2], 16)
        match = LINEAR_CALL.match(line)
        if match:
            calls.add(int(match[1], 16))
    return pushes, calls


def elf_image(va, code, starts):
    """ELF32 i386 image holding only `code` at `va`, one function symbol per start."""
    names, symbols = bytearray(b'\0'), bytearray(16)
    for start in sorted(starts):
        symbols += struct.pack('<IIIBBH', len(names), start, 0, 0x12, 0, 1)
        names += b's%08x\0' % start
    titles = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    text = 64
    table = (text + len(code) + 3) & ~3
    strings = table + len(symbols)
    shstrings = strings + len(names)
    headers = (shstrings + len(titles) + 3) & ~3
    out = bytearray(headers + 5 * 40)
    out[:52] = b'\x7fELF\1\1\1' + bytes(9) + struct.pack('<HHIIIIIHHHHHH', 2, 3, 1, va, 0, headers,
                                                       0, 52, 32, 0, 40, 5, 4)
    for at, blob in ((text, code), (table, symbols), (strings, names), (shstrings, titles)):
        out[at:at + len(blob)] = blob
    sections = ((0,) * 10,
                (1, 1, 6, va, text, len(code), 0, 0, 16, 0),
                (7, 2, 0, 0, table, len(symbols), 3, 1, 4, 16),
                (15, 3, 0, 0, strings, len(names), 0, 0, 1, 0),
                (23, 3, 0, 0, shstrings, len(titles), 0, 0, 1, 0))
    for i, fields in enumerate(sections):
        struct.pack_into('<10I', out, headers + 40 * i, *fields)
    return bytes(out)


def decode(image, starts, objdump):
    """VA -> (size, mnemonic, operands) of an llvm-objdump sweep restarted at every start."""
    blob = elf_image(image.code_va, image.code, starts)
    text = subprocess.run([str(objdump), '-d', '-z', '-'], input=blob,
                          capture_output=True, check=True).stdout.decode('utf-8')
    out = {}
    for line in text.splitlines():
        match = DECODED.match(line)
        if not match:
            continue
        va, raw = int(match[1], 16), bytes.fromhex(match[2])
        if image.code_bytes(va, len(raw)) != raw:
            raise ValueError(f'Decoder bytes differ at {va:#x}')
        fields = [f for f in match[3].split('\t') if f]
        while len(fields) > 1 and fields[0] in PREFIXES:
            fields = fields[1:]
        operands = COMMENT.sub('', fields[1]).strip() if len(fields) > 1 else ''
        out[va] = (len(raw), fields[0] if fields else '', operands)
    return out


def successors(image, va, size, mnemonic, operands, tables):
    following = va + size
    if mnemonic in ENDS:
        return ()
    if mnemonic in CONDITIONAL:
        return (following, int(TARGET.match(operands)[1], 16))
    if mnemonic == 'jmp' and TARGET.match(operands):
        return (int(TARGET.match(operands)[1], 16),)
    if mnemonic == 'calll' and TARGET.match(operands):
        return (int(TARGET.match(operands)[1], 16), following)
    if mnemonic == 'calll' and operands.startswith('*'):
        return (following,)
    if mnemonic == 'jmpl' and operands.startswith('*'):
        match = TABLE.fullmatch(operands)
        if not match:
            return ()
        table, targets = int(match[1], 16), []
        while table + 4 * len(targets) in image.relocations:
            target = image.dword(table + 4 * len(targets))
            if target is None or not image.in_code(target):
                break
            targets.append(target)
        tables[va] = (table, len(targets))
        return tuple(targets)
    if mnemonic == '<unknown>' or TRANSFER.match(mnemonic):
        raise ValueError(f'Unsupported reached instruction {mnemonic} {operands} at {va:#x}')
    return (following,)


def descend(image, roots, objdump):
    """Reached instructions (VA -> size), table jumps (VA -> (table, entries)) and decoder passes."""
    starts = set(roots)
    decoded = decode(image, starts, objdump)
    instructions, tables, passes = {}, {}, 1
    pending = sorted(starts, reverse=True)
    while True:
        missing = set()
        while pending:
            va = pending.pop()
            if va in instructions:
                continue
            if va not in decoded:
                missing.add(va)
                continue
            instructions[va] = decoded[va]
            for target in successors(image, va, *decoded[va], tables):
                if not image.in_code(target):
                    raise ValueError(f'Control flow leaves CODE at {va:#x}')
                if target not in instructions:
                    pending.append(target)
        if not missing:
            break
        if missing <= starts:
            raise ValueError(f'Undecodable start {min(missing):#x}')
        starts |= missing
        decoded = decode(image, starts, objdump)
        passes += 1
        pending = sorted(missing, reverse=True)
    for va, instruction in instructions.items():
        if decoded.get(va) != instruction:
            raise ValueError(f'Final pass decodes {va:#x} differently')
    reached = {va: instruction[0] for va, instruction in instructions.items()}
    order = sorted(reached)
    for left, right in zip(order, order[1:]):
        if left + reached[left] > right:
            raise ValueError(f'Overlapping reached instructions {left:#x} and {right:#x}')
    return reached, tables, passes


def classify(data, objdump, details=None):
    """Inventory entries, the evidence for the report and the reached instructions of a PE32 image.

    `details`, if given, receives the image, the roots and the jump tables (for entry_kinds)."""
    image = PeImage(data)
    linear, calls = linear_sweep(data, objdump)
    roots = dict(entry={image.entry} if image.in_code(image.entry) else set(),
                 exports={va for va in image.exports if image.in_code(va)},
                 frame_pointers=set(), linear_calls={va for va in calls if image.in_code(va)})
    for location in image.relocations:
        target = image.dword(location)
        if target is not None and image.in_code(target) and image.code_bytes(target, 3) == FRAME:
            roots['frame_pointers'].add(target)
    reached, tables, passes = descend(image, set().union(*roots.values()), objdump)
    order = sorted(reached)

    def starts_in(start, end):
        return order[bisect.bisect_left(order, start):bisect.bisect_left(order, end)]

    def inside(va):
        i = bisect.bisect_right(order, va) - 1
        return order[i] if i >= 0 and order[i] < va < order[i] + reached[order[i]] else None

    data_ranges = []
    for jump, (table, count) in sorted(tables.items()):
        end = table + 4 * count
        if starts_in(table, end) or inside(table):
            raise ValueError(f'Jump table of {jump:#x} overlaps reached code')
        data_ranges.append((table, end, f'jump table of {jump:#x}'))
    synchronized = {va: image.code[va - image.code_va] for va, size in reached.items()
                    if size == 1 and image.code[va - image.code_va] in PUSH}
    removed = {}
    for va in linear:
        if va in reached:
            continue
        holder = inside(va)
        if holder is not None:
            removed[va] = f'inside instruction {holder:#x}'
            continue
        for start, end, reason in data_ranges:
            if start <= va < end:
                removed[va] = reason
    entries = sorted((va, op) for va, op in {**linear, **synchronized}.items() if va not in removed)
    report = dict(passes=passes, reached_instructions=len(reached), jump_tables=len(tables),
                  roots={kind: len(vas) for kind, vas in roots.items()}, linear_entries=len(linear),
                  synchronized_entries=len(synchronized), entries=len(entries),
                  added=[f'{va:#010x}' for va in sorted(set(synchronized) - set(linear))],
                  removed={f'{va:#010x}': reason for va, reason in sorted(removed.items())},
                  unconfirmed=[f'{va:#010x}' for va in sorted(set(linear) - set(synchronized) - set(removed))])
    if details is not None:
        details.update(image=image, roots=roots, tables=tables)
    return entries, report, reached


OPERAND_POINTER_OPCODES = frozenset((0x68, 0xc7, *range(0xb8, 0xc0)))  # push imm32, mov r/m32, imm32, mov r32, imm32


def operand_pointers(image, reached):
    """Code addresses taken as immediates (callbacks): targets of base-relocated dwords inside reached
    `push imm32` / `mov r32, imm32` / `mov r/m32, imm32` instructions that lie in CODE outside reached code."""
    order = sorted(reached)

    def holder(va):
        i = bisect.bisect_right(order, va) - 1
        return order[i] if i >= 0 and order[i] <= va < order[i] + reached[order[i]] else None

    out = set()
    for location in image.relocations:
        source = holder(location)
        if source is None or image.code[source - image.code_va] not in OPERAND_POINTER_OPCODES:
            continue
        target = image.dword(location)
        if target is not None and image.in_code(target) and holder(target) is None:
            out.add(target)
    return out


def function_starts(image, reached, roots, tables):
    """Known function starts (step 4): VA -> sorted sources ('entry', 'exports', ..., 'call', 'pointer')."""
    starts = {}
    for kind, vas in roots.items():
        for va in vas:
            if va in reached:
                starts.setdefault(va, set()).add(kind)
    for va, size in reached.items():
        if size == 5 and image.code[va - image.code_va] == 0xe8:
            target = (va + 5 + struct.unpack_from('<i', image.code, va - image.code_va + 1)[0]) & 0xffffffff
            if target in reached:
                starts.setdefault(target, set()).add('call')
    in_table = [(table, table + 4 * count) for table, count in tables.values()]
    for location in image.relocations:
        if any(start <= location < end for start, end in in_table):
            continue
        target = image.dword(location)
        if target is not None and target in reached:
            starts.setdefault(target, set()).add('pointer')
    return {va: sorted(sources) for va, sources in starts.items()}


def extend(image, reached, tables, roots, objdump):
    """Step 4's descent: the roots plus the operand pointers (added to `roots` in place).

    It must reproduce every instruction of the first descent unchanged and keep
    its jump tables clear of reached code; it never changes the first table."""
    roots['operand_pointers'] = operand_pointers(image, reached)
    if not roots['operand_pointers']:
        return reached, tables
    wider, wider_tables, _ = descend(image, set().union(*roots.values()), objdump)
    if any(wider.get(va) != size for va, size in reached.items()):
        raise ValueError('Operand-pointer descent changes the first descent')
    order = sorted(wider)
    for jump, (table, count) in {**tables, **wider_tables}.items():
        i = bisect.bisect_right(order, table + 4 * count - 1) - 1
        if i >= 0 and order[i] + wider[order[i]] > table:
            raise ValueError(f'Jump table of {jump:#x} overlaps reached code')
    return wider, {**tables, **wider_tables}


def entry_kinds(image, reached, starts, entries):
    """Probe kind per known start: 'push' (first table), an ENTRY_KINDS name, or 'unprobed: <reason>'."""
    pushes = {va for va, _ in entries}
    out = {}
    for va in sorted(starts):
        size = reached[va]
        code = image.code_bytes(va, 12)
        if va in pushes:
            out[va] = 'push'
        elif size == 1 and code[0] in PUSH:
            out[va] = 'EP_PUSH'  # reached only by the step-4 descent: a row of the second table
        elif entry_kind(code, size):
            out[va] = entry_kind(code, size)
        elif code[0] == 0xcc:
            out[va] = 'unprobed: int3 (the original byte is the breakpoint itself)'
        else:
            out[va] = f'unprobed: first instruction {code[:size].hex()} not emulated'
    return out


def full_inventory(executable, objdump, report=None, state=None, module='dbsim'):
    """Hash-checked inventory entries, the kind table ((va, kind) of ENTRY_KINDS) and the kind per known start.

    `state`, if given, receives the image, the step-4 reach and its jump tables (dbsim_map.py)."""
    data = Path(executable).read_bytes()
    if hashlib.sha256(data).hexdigest() != MODULES[module]['hash']:
        raise ValueError(f"{MODULES[module]['title']} SHA-256 mismatch")
    details = {}
    entries, summary, reached = classify(data, objdump, details)
    if len(entries) < 100:
        raise ValueError('Unexpected decoder output/inventory')
    image, roots = details['image'], dict(details['roots'])
    reached, tables = extend(image, reached, details['tables'], roots, objdump)
    starts = function_starts(image, reached, roots, tables)
    kinds = entry_kinds(image, reached, starts, entries)
    table = [(va, kind) for va, kind in sorted(kinds.items()) if kind.startswith('EP_')]
    if state is not None:
        state.update(image=image, reached=reached, tables=tables)
    if report is not None:
        report.update(summary)
        counts = {}
        for kind in kinds.values():
            key = kind if not kind.startswith('unprobed: first') else 'unprobed: other first instruction'
            counts[key] = counts.get(key, 0) + 1
        report.update(operand_pointers=len(roots['operand_pointers']), function_starts=len(starts),
                      start_kinds=dict(sorted(counts.items())))
    return entries, table, {va: (kind, starts[va]) for va, kind in kinds.items()}


def inventory(executable, objdump, report=None, module='dbsim'):
    return full_inventory(executable, objdump, report, module=module)[0]


def render(entries, kinds=(), module='dbsim', starts=()):
    m = MODULES[module]
    p = m['prefix']
    text = ('/* Instruction boundaries and one-byte signatures, not function identities.\n'
            f" * Hash-verified {m['title']}; generated by probe_instruction_inventory.py\n" +
            (' * (linear sweep plus synchronized descent; dbsim_map.md, "probe\n'
             ' * inventory"). */\n' if module == 'dbsim' else
             f' * --module {module} (linear sweep plus synchronized descent; dbsim_map.md,\n'
             ' * "probe inventory"; vshell_map.md, "The probes"). */\n') +
            f"#ifndef {m['guard']}\n#define {m['guard']}\n"
            f'typedef struct {p}_instruction {{ uint32_t va; BYTE opcode; }} {p}_instruction;\n'
            f'static const {p}_instruction {p}_instructions[] = {{\n' +
            ''.join(f'{{0x{va:08x}u,0x{op:02x}}},'+('\n' if i % 6 == 5 else ' ') for i, (va, op) in enumerate(entries))+'\n' +
            '};\n')
    if kinds:
        text += ('/* Known function starts whose first instruction is not a one-byte PUSH r32,\n'
                 ' * with the emulated instruction kind of entry_probe_engine.h (step 4). */\n'
                 f'typedef struct {p}_entry_kind {{ uint32_t va; BYTE kind; }} {p}_entry_kind;\n'
                 f'static const {p}_entry_kind {p}_entry_kinds[] = {{\n' +
                 ''.join(f'{{0x{va:08x}u,{kind}}},' + ('\n' if i % 4 == 3 else ' ')
                         for i, (va, kind) in enumerate(kinds)) + '\n' +
                 '};\n')
    if module != 'dbsim':
        # Every probed known start (a push row or a row of the second
        # table), the default site list of the module's function map.
        text += ('/* The known function starts with a probe (push or emulated kind), sorted. */\n'
                 f'static const uint32_t {p}_function_starts[] = {{\n' +
                 ''.join(f'0x{va:08x}u,' + ('\n' if i % 8 == 7 else ' ') for i, va in enumerate(starts)) + '\n' +
                 '};\n')
    return text + '#endif\n'


def probed_starts(starts):
    """Sorted VAs of the known starts that have a probe (`starts` as returned by full_inventory)."""
    return sorted(va for va, (kind, _) in starts.items() if not kind.startswith('unprobed'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--objdump', type=Path, default=Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--module', choices=sorted(MODULES), default='dbsim',
                        help='reference module, SHA-256 checked (vshell: proxy/vshell_probe_inventory.h)')
    parser.add_argument('--report', type=Path, help='classification as JSON (keep it outside the repository)')
    parser.add_argument('--starts', type=Path,
                        help='known function starts with probe kind and sources as JSON (keep it outside the repository)')
    args = parser.parse_args()
    details = {}
    entries, table, starts = full_inventory(args.executable, args.objdump, details, module=args.module)
    result = render(entries, table, args.module, probed_starts(starts))
    if args.report:
        args.report.write_text(json.dumps(details, indent=1) + '\n', encoding='utf-8')
    if args.starts:
        with open(args.starts, 'w', encoding='utf-8', newline='\n') as f:
            json.dump({f'{va:#x}': dict(kind=kind, sources=sources) for va, (kind, sources) in starts.items()},
                      f, indent=0)
            f.write('\n')
    if args.check:
        if args.output.read_bytes() != result.encode('ascii'):
            raise SystemExit('Inventory differs')
    else:
        with open(args.output, 'w', encoding='ascii', newline='\n') as f:
            f.write(result)
