# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Exclusive edges and redirect traps of the live branch sites (edge mode).

An edge of a branch site is its fall-through (address + length) or its
taken target. Edge mode of the proxy (live_coverage_engine.h) observes each
edge once instead of trapping the Jcc on every execution:

- a one-shot INT3 on the edge's destination, sound only when the destination
  has no other way in (then reaching it proves that this edge ran);
- for the taken edge, a redirect: the Jcc's displacement points at a byte
  that faults by itself (INT3, HLT, IN/OUT/INS/OUTS: TRAP_BYTES), the handler
  records the edge, restores the displacement and continues at the target.
  A rel32 Jcc gets a stub in the proxy's own page; a rel8 Jcc needs such a
  byte within reach in the original image. That byte is never written; it
  must not start a reached instruction (nothing else may arrive there) nor
  lie in a site's own bytes (the proxy patches those).

This script decides both for every BRANCH line of invitro/branch_sites.h and
writes proxy/live_coverage_edges.h, one EDGE(va, fall, taken, trap) line per
site, sorted by address (fall/taken 1 = exclusive destination; trap = the
redirect byte of a rel8 site, 0 for none or for rel32 sites). Redirect bytes
also avoid the first PROLOGUE_WINDOW bytes of every hooked function
(proxy/hook_registry.h): the proxy overwrites its prologue there. Regenerate
whenever branch_sites.h or the hook registry grows; the proxy joins the table
by address, so a stale table only leaves new sites without edge help.

A destination is exclusive when its only predecessor is the site's Jcc,
counting conservatively every way in that any of these sees:

- the synchronized descent of probe_instruction_inventory.py: the
  fall-through, conditional and direct jump targets, direct call targets,
  return points after calls and jump-table entries of every reached
  instruction;
- one linear llvm-objdump sweep over the whole PE: its direct branch and call
  targets and its fall-throughs (a desynchronized sweep only adds
  predecessors, never removes one);
- every base-relocated dword that points at the destination (vtables,
  callbacks, jump tables, pushed addresses), and the entry point and exports.

A site whose Jcc is not reached or not decoded as the BRANCH line says, or
whose two edges meet, gets 0 for both and no trap. Only addresses and bits
are emitted; no original bytes. The proxy re-checks every byte it relies on
at install time and falls back to the Jcc trap where a check fails.
"""
import argparse
import hashlib
import importlib.util
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRANCH = re.compile(r'BRANCH\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
# Spellings of branch_sites.h (branch_condition.h aliases) -> llvm-objdump AT&T mnemonics.
MNEMONIC = {'je': 'je', 'jz': 'je', 'jne': 'jne', 'jnz': 'jne', 'jl': 'jl', 'jle': 'jle', 'jg': 'jg',
            'jge': 'jge', 'jb': 'jb', 'jc': 'jb', 'ja': 'ja', 'jns': 'jns', 'jae': 'jae', 'jnc': 'jae',
            'jbe': 'jbe', 'js': 'js', 'jp': 'jp', 'jnp': 'jnp', 'jo': 'jo', 'jno': 'jno'}
# Instructions after which execution never continues at the next address.
NO_FALL = frozenset(('jmp', 'jmpl', 'ljmp', 'ljmpl', 'retl', 'ret', 'lretl', 'lret', 'iretl', 'iret',
                     'int3', 'hlt', 'ud2'))
# First bytes that fault in user mode before anything executes: INT3, HLT,
# IN/OUT (imm8 and DX forms), INS/OUTS. Same set as LC_TRAP_BYTES in
# live_coverage_engine.h; the engine fixture executes each one.
TRAP_BYTES = frozenset((0xcc, 0xf4, 0xe4, 0xe5, 0xe6, 0xe7, 0xec, 0xed, 0xee, 0xef, 0x6c, 0x6d, 0x6e, 0x6f))
# Bytes after a hook entry that a redirect byte avoids; covers every relocated
# prologue of the registry (the proxy refuses the rest at install time anyway).
PROLOGUE_WINDOW = 16
HOOK = re.compile(r'^\s*ES2_HOOK\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,', re.M)
IMAGE_BASE = 0x400000


def hook_entries(path):
    """Virtual addresses of the hooked functions in proxy/hook_registry.h."""
    entries = [IMAGE_BASE + int(m[2], 16) for m in HOOK.finditer(Path(path).read_text(encoding='utf-8-sig'))]
    if not entries:
        raise ValueError(f'{path}: no ES2_HOOK lines')
    return entries


def load_inventory():
    spec = importlib.util.spec_from_file_location('es2_probe_inventory', HERE / 'probe_instruction_inventory.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def branch_sites(path):
    """BRANCH lines outside comments, in file order: (hook, va, length, condition, target)."""
    text = COMMENT.sub(' ', Path(path).read_text(encoding='utf-8-sig'))
    sites, seen = [], set()
    for m in BRANCH.finditer(text):
        site = (m[1], int(m[2], 16), int(m[3]), m[4], int(m[5], 16))
        if site[1] in seen or site[3] not in MNEMONIC or site[2] not in (2, 6):
            raise ValueError(f'{path}: duplicate or invalid BRANCH line {m[0]}')
        seen.add(site[1])
        sites.append(site)
    if not sites:
        raise ValueError(f'{path}: no BRANCH lines')
    return sites


def linear_edges(data, objdump, inventory):
    """(source, destination) pairs of one linear sweep: direct targets and fall-throughs."""
    text = subprocess.run([str(objdump), '-d', '-z', '-'], input=data,
                          capture_output=True, check=True).stdout.decode('utf-8')
    pairs = set()
    for line in text.splitlines():
        match = inventory.DECODED.match(line)
        if not match:
            continue
        va, size = int(match[1], 16), len(bytes.fromhex(match[2]))
        fields = [f for f in match[3].split('\t') if f]
        while len(fields) > 1 and fields[0] in inventory.PREFIXES:
            fields = fields[1:]
        mnemonic = fields[0] if fields else ''
        operands = inventory.COMMENT.sub('', fields[1]).strip() if len(fields) > 1 else ''
        target = inventory.TARGET.match(operands)
        if target and (mnemonic in inventory.CONDITIONAL or mnemonic in ('jmp', 'calll')):
            pairs.add((va, int(target[1], 16)))
        if mnemonic not in NO_FALL:
            pairs.add((va, va + size))
    return pairs


def predecessors(data, objdump, inventory=None):
    """Destination -> set of predecessor sources, the reached decode (va -> size, mnemonic, operands), the image."""
    inventory = inventory or load_inventory()
    image = inventory.PeImage(data)
    _, _, reached = inventory.classify(data, objdump)
    decoded = inventory.decode(image, set(reached), objdump)
    preds = {}
    tables = {}

    def add(source, destination):
        preds.setdefault(destination, set()).add(source)

    for va in reached:
        size, mnemonic, operands = decoded[va]
        if size != reached[va]:
            raise ValueError(f'Decode of {va:#x} differs from the descent')
        for destination in inventory.successors(image, va, size, mnemonic, operands, tables):
            add(va, destination)
    for source, destination in linear_edges(data, objdump, inventory):
        add(source, destination)
    for location in image.relocations:
        target = image.dword(location)
        if target is not None:
            add(('reloc', location), target)
    for root in {image.entry} | image.exports:
        add(('root', root), root)
    return preds, {va: decoded[va] for va in reached}, image


def classify_edges(sites, preds, decoded, image, entries=()):
    """(hook, va, fall, taken, trap) per site: exclusivity bits and the rel8 redirect byte (0 = none)."""
    rows, verified = [], []
    for hook, va, length, condition, target in sites:
        instruction = decoded.get(va)
        ok = (instruction is not None and instruction[0] == length and instruction[1] == MNEMONIC[condition]
              and instruction[2] == f'0x{target:x}' and target != va + length)
        verified.append(ok)
        if not ok:
            rows.append([hook, va, 0, 0, 0])
            continue
        fall = preds.get(va + length, set()) == {va}
        taken = preds.get(target, set()) == {va}
        rows.append([hook, va, int(fall), int(taken), 0])
    # Bytes the proxy may patch (every site's Jcc) and instruction starts
    # control flow may reach are never redirect bytes; each byte serves one
    # site (the handler maps it back), so the bytes are a maximum matching.
    busy = {va + i for _, va, length, _, _ in sites for i in range(length)}
    busy |= {entry + i for entry in entries for i in range(PROLOGUE_WINDOW)}
    candidates = {}
    for index, ((_, va, length, _, _), ok) in enumerate(zip(sites, verified)):
        if ok and length == 2:
            origin = va + 2
            candidates[index] = [origin + d for d in sorted(range(-128, 128), key=lambda d: (abs(d), d))
                                 if image.in_code(origin + d) and origin + d not in busy and origin + d not in decoded
                                 and image.code[origin + d - image.code_va] in TRAP_BYTES]
    for index, trap in matching(candidates).items():
        rows[index][4] = trap
    return [tuple(row) for row in rows]


def matching(candidates):
    """Maximum bipartite matching site -> byte (augmenting paths, deterministic order)."""
    owner = {}
    for site in sorted(candidates):
        if not candidates[site]:
            continue
        # Iterative depth-first search for an augmenting path from `site`.
        parent, visited, stack, found = {}, set(), [(site, iter(candidates[site]))], None
        while stack and found is None:
            node, options = stack[-1]
            for trap in options:
                if trap in visited:
                    continue
                visited.add(trap)
                parent[trap] = node
                if trap not in owner:
                    found = trap
                else:
                    stack.append((owner[trap], iter(candidates[owner[trap]])))
                break
            else:
                stack.pop()
        while found is not None:
            node = parent[found]
            previous = next((t for t, s in owner.items() if s == node), None)
            owner[found] = node
            found = previous
    return {site: trap for trap, site in owner.items()}


def render(rows):
    exclusive = sum(f + t for _, _, f, t, _ in rows)
    traps = sum(1 for row in rows if row[4])
    return ('/* Edges of the BRANCH sites, sorted by address: EDGE(va, fall, taken, trap).\n'
            ' * fall/taken 1 = the edge destination has no other predecessor; trap = a byte\n'
            ' * within rel8 reach that faults by itself (the taken redirect), 0 = none.\n'
            ' * Hash-verified DBSIM; generated by scripts/branch_edges.py (live coverage edge mode).\n'
            f' * {exclusive} of {2 * len(rows)} edge destinations exclusive, {traps} rel8 redirect bytes. */\n'
            + ''.join(f'EDGE(0x{va:08x},{f},{t},0x{trap:08x}) /* {hook} */\n'
                      for hook, va, f, t, trap in sorted(rows, key=lambda row: row[1])))


def generate(executable, sites_path, registry_path, objdump):
    data = Path(executable).read_bytes()
    if hashlib.sha256(data).hexdigest() != load_inventory().MODULE_HASH:
        raise ValueError('DBSIM SHA-256 mismatch')
    preds, decoded, image = predecessors(data, objdump)
    return render(classify_edges(branch_sites(sites_path), preds, decoded, image, hook_entries(registry_path)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('executable', type=Path, help='the reference VER95/DBSIM.EXE')
    parser.add_argument('output', type=Path, help='proxy/live_coverage_edges.h')
    parser.add_argument('--sites', type=Path, default=HERE.parent / 'invitro' / 'branch_sites.h')
    parser.add_argument('--registry', type=Path, default=HERE.parent / 'proxy' / 'hook_registry.h')
    parser.add_argument('--objdump', type=Path, default=Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe')
    parser.add_argument('--check', action='store_true', help='compare with the output file instead of writing it')
    args = parser.parse_args()
    result = generate(args.executable, args.sites, args.registry, args.objdump)
    if args.check:
        if args.output.read_text(encoding='ascii') != result:
            raise SystemExit(f'{args.output} differs from the generated edges')
        print(f'{args.output}: up to date')
    else:
        with open(args.output, 'w', encoding='ascii', newline='\n') as f:
            f.write(result)
        print(result.splitlines()[4])


if __name__ == '__main__':
    main()
