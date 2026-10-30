"""Rank H4 candidates from the DBSIM map against the current hook registry.

Reads the entry-probe map (`map.json` of a dbsim-map run) and
`tools/reference-game/proxy/hook_registry.h`, and writes a Markdown table of
live original functions that have no, or exactly one, live original callee
left (callees that are hooks count as replaced). With `--classified`, the
verdict table of `docs/reference-game/function_verdicts.md` is read as well:
candidates with a verdict other than H4 (H3, platform, RTL, dead,
in-progress) leave the table and are listed in a short section at the end,
so the table shows only open targets. Standard library only.

    python hook_candidates.py --map <es2-data>/logs/dbsim-map-20261010/map.json \
        --registry tools/reference-game/proxy/hook_registry.h \
        --classified docs/reference-game/function_verdicts.md \
        --output docs/reference-game/hook_candidates.md

With `--frontier` the script writes the next frontier instead
(`docs/reference-game/hook_frontier.md`): every live original function with two
or more live original callees left, with the original-only subtree that would
have to be replaced before it is ready, ranked as the generated header
explains. A hand-written section that starts with `## Proposed packages` at
the end of an existing output file is kept when the file is regenerated.

With `--bridge-table` the script writes the bridged-call inventory
instead: every callee that an adapter counts as `call_<va>` or `slot_<off>`
(`--bridge-calls`, the proxy headers), joined with the live counters of a
regression (`hook-counters.json` of `run-oracle-parallel.ps1`), whether the
callee is a hook itself, its verdict from `--classified`, and name/kind
notes from the `### Bridge notes` table of `--bridge-notes`. When the output
file holds the markers `<!-- bridge-table:begin -->` and
`<!-- bridge-table:end -->`, only the text between them is replaced. With
`--image` a `call_<va>` key that is no hook and no map row and whose
bytes are an indirect near call (`FF /2`) is listed as an indirect call site,
not as a callee (the adapters key a call through a pointer by its call
instruction, e.g. `call_420a8d`):

    python hook_candidates.py --bridge-table <es2-data>/logs/<run>/hook-counters.json \
        --bridge-calls tools/reference-game/proxy --map .../map.json \
        --registry tools/reference-game/proxy/hook_registry.h \
        --classified docs/reference-game/function_verdicts.md \
        --bridge-notes docs/reference-game/hook_frontier.md --image .../VER95/DBSIM.EXE \
        --output docs/reference-game/hook_frontier.md

With `--check-verdicts` the script only checks the verdict file and exits
with 1 when a table row outside the `## Verdicts` table (the per-package
sections append such rows) classifies a function the table lacks: those rows
are not read by the modes above, so the function would stay a candidate.
A function that is a hook (`--registry`) needs no row; a second cell that does
not start with one of the six verdict words (`open`, `pure`, a branch-site
table's edge list) is not a function verdict and does not count:

    python hook_candidates.py --check-verdicts --registry tools/reference-game/proxy/hook_registry.h \
        --classified docs/reference-game/function_verdicts.md

The single command that regenerates everything derived from the map and the
registry is `tools/reference-game/scripts/regenerate_function_docs.ps1`.
"""
import argparse
import bisect
import json
import re
import sys
from collections import deque
from pathlib import Path

IMAGE_BASE = 0x400000
SUITES = ['acceptance-60s', 'c1_01-success', 'c1_01-collision',
          'c3_09-loadout-b', 'c4_08-razor-plasma', 'c3_10-loadout-hercs']
ROOTS = {'R': 0x45fca8, 'P': 0x45fba4, 'C': 0x4011a0}
# Runtime helpers that are never H4 targets (allocator and RTL wrappers).
NEVER = {0x49630c, 0x496406, 0x4953c4}
# Verdicts of function_verdicts.md, in the order the left-out section lists them.
VERDICTS = ['H4', 'in-progress', 'H3', 'platform', 'RTL', 'dead']
VERDICT_HEADING = '## Verdicts'
# The logic frame was not probed; its direct children are the map rows that
# list it as a caller (reach tag L of the frontier mode).
LOGIC_FRAME = 0x45f570
# Hand-written tail of the frontier file that regeneration keeps.
FRONTIER_TAIL = '## Proposed packages'
# Frontier ranking classes, in order (see the generated header).
FRONTIER_CLASSES = {
    'A': 'replaced code still calls it (an adapter counts `call_<va>`; without '
         '`--bridge-calls`: a hook is a static caller)',
    'B': 'calls a hook (replaced code sits below it)',
    'C': 'reached from the logic frame or only through vtables/callbacks, no hook link',
    'D': 'reached only from the rendering, presentation or camera roots',
    'S': 'superseded: every static caller is a hook and no adapter counts a call to it, '
         'so it no longer runs in replaced mode (only with `--bridge-calls`)',
}


def read_hooks(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    hooks = {}
    for m in re.finditer(r'ES2_HOOK\((\w+),\s*(0x[0-9a-fA-F]+)', text):
        hooks[IMAGE_BASE + int(m.group(2), 16)] = m.group(1)
    return hooks


def read_classified(path):
    """VA -> (verdict, package or document, reason) from the verdict table.

    Only the table rows between the heading `## Verdicts` and the next
    level-2 heading count; a row is `| `0x...` | verdict | package | reason |`.
    An unknown verdict, a VA listed twice or a missing section is an error.
    """
    out = {}
    inside = found = False
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()
    for n, line in enumerate(lines, 1):
        if line.startswith('## '):
            inside = line.rstrip() == VERDICT_HEADING
            found = found or inside
            continue
        if not inside or not line.startswith('|'):
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        m = re.fullmatch(r'`?(0x[0-9a-fA-F]+)`?', cells[0])
        if not m:
            continue  # header or separator row
        if len(cells) < 4:
            raise ValueError(f'{path}:{n}: expected VA, verdict, package, reason')
        va = int(m.group(1), 16)
        verdict = cells[1].strip('*').strip()
        if verdict not in VERDICTS:
            raise ValueError(f'{path}:{n}: unknown verdict {cells[1]!r} '
                             f'(one of {", ".join(VERDICTS)})')
        if va in out:
            raise ValueError(f'{path}:{n}: {va:#x} is classified twice')
        out[va] = (verdict, cells[2], '|'.join(cells[3:]).strip())
    if not found:
        raise ValueError(f'{path}: no "{VERDICT_HEADING}" section')
    return out


# A verdict cell of a per-package section starts with one of the six words
# ("H4, hooked", "H3 (stays original)", "platform (stay original)", ...);
# "runtime" is the older spelling of RTL.
LEADING_VERDICT = re.compile(r'\**(H4|H3|platform|RTL|runtime|dead|in-progress)\b')


def read_outside_rows(path):
    """{VA: [(line number, section heading, verdict word, package cell)]} of the
    table rows outside the `## Verdicts` section whose first cell holds
    backticked VAs and whose second cell starts with a verdict word, in file
    order (later sections are newer)."""
    out = {}
    section, inside = '', False
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()
    for n, line in enumerate(lines, 1):
        if line.startswith('## '):
            section = line.strip()[3:].split(':')[0].split(' (')[0]
            inside = line.rstrip() == VERDICT_HEADING
            continue
        if inside or not line.startswith('|'):
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if len(cells) < 4:
            continue
        m = LEADING_VERDICT.match(cells[1])
        if not m:
            continue
        word = 'RTL' if m.group(1) == 'runtime' else m.group(1)
        for va in re.findall(r'`0x([0-9a-fA-F]+)`', cells[0]):
            out.setdefault(int(va, 16), []).append((n, section, word, cells[2]))
    return out


def check_verdicts(path, hooks):
    """(missing, differing): the VAs that a row outside the table classifies, that
    are no hook and have no row in the table, as {VA: newest outside row}; and the
    VAs of the table whose newest outside row (not a hook) names another verdict word
    as {VA: (table verdict, outside row)}."""
    table = read_classified(path)
    missing, differing = {}, {}
    for va, found in read_outside_rows(path).items():
        if va in hooks:
            continue
        if va not in table:
            missing[va] = found[-1]
        elif found[-1][2] != table[va][0]:
            differing[va] = (table[va][0], found[-1])
    return missing, differing


def reachable(callees, root):
    seen, queue = set(), deque([root])
    while queue:
        va = queue.popleft()
        if va in seen:
            continue
        seen.add(va)
        queue.extend(c for c in callees.get(va, ()) if c not in seen)
    return seen


def run_check_verdicts(args):
    """--check-verdicts: prints the findings, returns 1 when a row is missing."""
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    missing, differing = check_verdicts(args.classified, read_hooks(args.registry))
    for va, (n, section, word, package) in sorted(missing.items()):
        print(f'{args.classified}:{n}: {va:#x} is classified {word} in section "{section}" '
              f'({package}) but has no row in the "{VERDICT_HEADING}" table')
    for va, (table_word, (n, section, word, _)) in sorted(differing.items()):
        print(f'note: {va:#x} is {table_word} in the table but {word} at line {n} '
              f'(section "{section}"): the table row decides; replace it if the later one supersedes it')
    if missing:
        print(f'FAIL: {len(missing)} functions classified outside the "{VERDICT_HEADING}" '
              'table are missing from it')
        return 1
    print('PASS: every non-hook function classified outside the table has a table row'
          f' ({len(differing)} differing verdict words noted)')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', help='map.json of a DBSIM map run (all modes but --check-verdicts)')
    ap.add_argument('--registry', required=True)
    ap.add_argument('--output', help='output file (all modes but --check-verdicts)')
    ap.add_argument('--check-verdicts', action='store_true',
                    help='only check that the Verdicts table holds every non-hook function that '
                         'a row of a per-package section classifies; exit 1 otherwise')
    ap.add_argument('--classified',
                    help='function_verdicts.md; candidates whose verdict is not H4 leave the table')
    ap.add_argument('--min-ready', type=int, default=60)
    ap.add_argument('--min-one', type=int, default=100)
    ap.add_argument('--frontier', action='store_true',
                    help='write the frontier (two or more live original callees) instead')
    ap.add_argument('--bridge-calls',
                    help='frontier: directory of the proxy adapter headers; their callee '
                         'counters "call_<va>" name the originals that replaced code calls')
    ap.add_argument('--image',
                    help='frontier: DBSIM.EXE; the first bytes of the unprobed callees are '
                         'read from it (one-byte PUSH r32 = probeable once the probe inventory '
                         'knows the entry); frontier and bridge table: a "call_<va>" key whose '
                         'bytes are an indirect call (FF /2) is a call site, not a callee')
    ap.add_argument('--bridge-table',
                    help='hook-counters.json of a regression: write the bridged-call '
                         'inventory (needs --bridge-calls)')
    ap.add_argument('--bridge-notes',
                    help='bridge table: Markdown file with a "### Bridge notes" table '
                         '(VA, name, kind guest/core, note)')
    args = ap.parse_args(argv)
    if args.check_verdicts:
        if not args.classified:
            ap.error('--check-verdicts needs --classified')
        return run_check_verdicts(args)
    if not args.map or not args.output:
        ap.error('--map and --output are required (except with --check-verdicts)')
    if args.bridge_table and not args.bridge_calls:
        ap.error('--bridge-table needs --bridge-calls')

    hooks = read_hooks(args.registry)
    hook_vas = sorted(hooks)
    classified = read_classified(args.classified) if args.classified else None
    with open(args.map, encoding='utf-8') as f:
        data = json.load(f)
    rows = {int(r['va'], 16): r for r in data['rows']}
    callees = {va: [int(c, 16) for c in r['callees_live']] for va, r in rows.items()}
    reach = {tag: reachable(callees, root) for tag, root in ROOTS.items()}
    if args.bridge_table:
        write_bridge_table(args, hooks, classified, rows)
        return
    if args.frontier:
        write_frontier(args, hooks, classified, rows, callees, reach)
        return

    def tags(va):
        t = ''.join(k for k in ROOTS if va in reach[k])
        return t or 'V'

    def live_nonhook(va):
        return [c for c in callees.get(va, ()) if c not in hooks]

    def near(va):
        i = bisect.bisect_left(hook_vas, va)
        return ', '.join(f'{hooks[hook_vas[j]]} {hook_vas[j]:#x}'
                         for j in (i - 1, i) if 0 <= j < len(hook_vas))

    cands, left_out = [], []
    for va, r in rows.items():
        if va in hooks or va in NEVER or r['max'] == 0:
            continue
        ln = live_nonhook(va)
        if not ((not ln and r['instructions'] >= args.min_ready)
                or (len(ln) == 1 and r['instructions'] >= args.min_one)):
            continue
        if classified is not None and va in classified and classified[va][0] != 'H4':
            left_out.append((va, classified[va][0]))
            continue
        cands.append((va, r, ln))
    cands.sort(key=lambda t: (len(t[2]), -t[1]['instructions']))

    out = []
    out.append('# H4 candidates from the DBSIM map\n')
    out.append(f'Generated by `tools/reference-game/scripts/hook_candidates.py` from '
               f'`{args.map}` against {len(hooks)} registered hooks. '
               'Live original functions whose live callees are all hooks ("ready", '
               f'at least {args.min_ready} instructions) or that have exactly one live '
               f'original callee left (at least {args.min_one} instructions). '
               'Counts are the saturated entry probes per suite (1000 = at least 1000). '
               '"Reach" is static direct-call reachability from the main-loop roots: '
               'R = rendering `0x45fca8`, P = presentation `0x45fba4`, C = camera '
               '`0x4011a0`, V = only through vtables or callbacks (the logic frame '
               '`0x45f570` itself was not probed, so its direct children show as V). '
               'An R tag means the function is also used by rendering; a pure helper '
               'can still be an H4 target, a function that only draws is not. Every '
               'role is a hypothesis until the contract is read from the bytes. '
               '"Nearest hooks" are the registered hooks nearest by address, not '
               'callers or callees.\n')
    if classified is not None:
        out.append(f'Verdicts from `{args.classified}`: candidates classified H3, platform, '
                   'RTL, dead or in-progress are left out of the table and listed at the '
                   'end; "Verdict" is H4 for a recorded H4 verdict and "open" for a '
                   'candidate without one.\n')
    header = ('| VA | Instr. | Reach | Calls per suite (a60/c1s/c1c/c3_09/c4_08/c3_10) '
              '| Replaced callees | Live original callee | Callers | Nearest hooks |')
    rule = '|---|---:|---|---|---:|---|---|---|'
    if classified is not None:
        header += ' Verdict |'
        rule += '---|'
    out.append(header)
    out.append(rule)
    for va, r, ln in cands:
        callers = ', '.join(r['callers'][:3]) + ('…' if len(r['callers']) > 3 else '')
        lc = ', '.join(
            f"{c:#x} ({rows[c]['instructions']} instr., {len(live_nonhook(c))} live)" if c in rows
            else f'{c:#x} (unprobed)' for c in ln) or '—'
        counts = '/'.join(str(r['counts'][s]) for s in SUITES)
        line = (f"| `{va:#x}` | {r['instructions']} | {tags(va)} | {counts} | "
                f"{r['callees_replaced']} | {lc} | {callers or 'vtable/callback'} | {near(va)} |")
        if classified is not None:
            line += ' H4 |' if va in classified else ' open |'
        out.append(line)
    ready = sum(1 for _, _, ln in cands if not ln)
    out.append(f'\n{ready} ready and {len(cands) - ready} one-callee candidates. '
               f'Never H4 (runtime helpers): {", ".join(f"{v:#x}" for v in sorted(NEVER))}.\n')
    if classified is not None:
        out.append('## Left out (classified)\n')
        out.append(f'{len(left_out)} further functions meet the same thresholds but have a '
                   f'verdict other than H4 in `{args.classified}`:\n')
        for verdict in VERDICTS[1:]:
            vas = sorted(va for va, v in left_out if v == verdict)
            if vas:
                out.append(f'- {verdict} ({len(vas)}): ' + ', '.join(f'`{va:#x}`' for va in vas))
        out.append('')
    with open(args.output, 'wb') as f:
        f.write('\n'.join(out).encode('utf-8'))
    print(f'{len(cands)} candidates written to {args.output}'
          + (f' ({len(left_out)} classified ones left out)' if classified is not None else ''))
    if classified is not None:
        stale = sorted(va for va in classified if va in hooks)
        if stale:
            print_stale(stale, hooks)


STALE_SHOWN = 12


def print_stale(stale, hooks):
    """The classified VAs that are hooks now (their table rows can be pruned): the first
    STALE_SHOWN by name, then the count."""
    shown = ', '.join(f'{va:#x} {hooks[va]}' for va in stale[:STALE_SHOWN])
    more = f' ... ({len(stale) - STALE_SHOWN} more, {len(stale)} in all)' if len(stale) > STALE_SHOWN else ''
    print('classified functions that are hooks now: ' + shown + more)


def heights(graph):
    """Longest path in edges below every node of `graph` (node -> successors).

    Strongly connected components (recursion) are condensed first, so a cycle
    adds no length; each step between two components counts 1.
    """
    index, low, comp, stack, on_stack, count = {}, {}, {}, [], set(), [0]

    def connect(v):  # Tarjan; the graphs here are a few hundred nodes deep at most
        index[v] = low[v] = count[0]
        count[0] += 1
        stack.append(v)
        on_stack.add(v)
        for w in graph.get(v, ()):
            if w not in index:
                connect(w)
                low[v] = min(low[v], low[w])
            elif w in on_stack:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            while True:
                w = stack.pop()
                on_stack.discard(w)
                comp[w] = v
                if w == v:
                    break

    for v in sorted(graph):
        if v not in index:
            connect(v)
    succ = {}
    for v, ws in graph.items():
        succ.setdefault(comp[v], set()).update(comp[w] for w in ws if comp[w] != comp[v])
    memo = {}

    def depth(c):
        if c not in memo:
            memo[c] = max((1 + depth(d) for d in succ.get(c, ())), default=0)
        return memo[c]

    return {v: depth(comp[v]) for v in graph}


def read_image(path):
    """A function `bytes_at(va, n)` over the sections of a PE32 image (bounds-checked)."""
    with open(path, 'rb') as f:
        data = f.read()

    def u16(off):
        if off + 2 > len(data):
            raise ValueError(f'{path}: truncated at {off:#x}')
        return int.from_bytes(data[off:off + 2], 'little')

    def u32(off):
        if off + 4 > len(data):
            raise ValueError(f'{path}: truncated at {off:#x}')
        return int.from_bytes(data[off:off + 4], 'little')

    if data[:2] != b'MZ':
        raise ValueError(f'{path}: no MZ header')
    pe = u32(0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'{path}: no PE signature')
    sections, opt_size = u16(pe + 6), u16(pe + 20)
    opt = pe + 24
    if u16(opt) != 0x10b:
        raise ValueError(f'{path}: not a PE32 image')
    base = u32(opt + 28)
    table = []
    for i in range(sections):
        sec = opt + opt_size + 40 * i
        vsize, rva, raw_size, raw = u32(sec + 8), u32(sec + 12), u32(sec + 16), u32(sec + 20)
        table.append((base + rva, max(vsize, raw_size), raw, raw_size))

    def bytes_at(va, n):
        for start, size, raw, raw_size in table:
            if start <= va and va + n <= start + size:
                off = va - start
                if off + n > raw_size or raw + off + n > len(data):
                    return None  # uninitialised data
                return data[raw + off:raw + off + n]
        return None

    return bytes_at


def read_bridge_calls(directory):
    """VAs that replaced code still calls: the callee counters `"call_<va>"` of the adapters."""
    vas = set()
    for path in sorted(Path(directory).glob('*.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        vas.update(int(m.group(1), 16) for m in re.finditer(r'"call_([0-9a-fA-F]{6})"', text))
    return vas


def indirect_call_site(bytes_at, va, hooks, rows):
    """Whether a `call_<va>` key is the call site of an indirect call, not a callee.

    For a call through a pointer (an object's slot, an event port) an adapter
    keys its counter by the address of the call instruction (sim_leaves_hooks.h:
    `call_420a8d`, `call_420acb`). Such a key is no hook and no map row, and its
    bytes are an indirect near call `FF /2` (`call r/m32`). Without an image
    (`bytes_at` None) every key counts as a callee.
    """
    if bytes_at is None or va in hooks or va in rows:
        return False
    b = bytes_at(va, 2)
    return bool(b) and b[0] == 0xFF and (b[1] >> 3) & 7 == 2


def frontier_rows(hooks, classified, rows, callees, reach, bridged=None):
    """Ranked frontier entries, the left-out (classified) ones, and the verdict lookup.

    A live callee is "replaced" when it is a hook or classified in-progress,
    "kept" when it is a runtime helper (NEVER) or classified H3, platform,
    RTL or dead (it stays original and is called as a guest), and "to do"
    otherwise. The subtree of a function is the function plus every to-do
    function reachable through to-do callees.
    """
    classified = classified or {}
    logic = set()
    for va, r in rows.items():
        if f'{LOGIC_FRAME:#x}' in r['callers']:
            logic |= reachable(callees, va)

    def verdict(va):
        return classified[va][0] if va in classified else None

    def replaced(va):
        return va in hooks or verdict(va) == 'in-progress'

    def kept(va):
        return va in NEVER or verdict(va) in ('H3', 'platform', 'RTL', 'dead')

    todo = {va: [c for c in callees.get(va, ()) if c in rows and not replaced(c) and not kept(c)]
            for va in rows}
    height = heights(todo)

    def subtree(va):
        seen, kept_seen, stack = set(), set(), [va]
        while stack:
            f = stack.pop()
            if f in seen:
                continue
            seen.add(f)
            kept_seen.update(c for c in callees.get(f, ()) if kept(c) and not replaced(c))
            stack.extend(c for c in todo[f] if c not in seen)
        return seen, kept_seen

    def tags(va):
        t = ('L' if va in logic else '') + ''.join(k for k in ROOTS if va in reach[k])
        return t or 'V'

    out, left_out = [], []
    for va, r in rows.items():
        if va in hooks or va in NEVER or r['max'] == 0:
            continue
        live = [c for c in callees.get(va, ()) if c not in hooks]
        if len(live) < 2:
            continue
        if verdict(va) not in (None, 'H4'):
            left_out.append((va, verdict(va)))
            continue
        members, kept_all = subtree(va)
        t = tags(va)
        hook_callers = sum(1 for c in r['callers'] if int(c, 16) in hooks)
        hook_callees = r['callees_replaced'] + sum(1 for c in callees.get(va, ()) if c in hooks)
        if (bridged is not None and r['callers'] and hook_callers == len(r['callers'])
                and va not in bridged):
            cls = 'S'
        elif 'L' not in t and t != 'V':
            cls = 'D'
        elif va in bridged if bridged is not None else hook_callers:
            cls = 'A'
        elif hook_callees:
            cls = 'B'
        else:
            cls = 'C'
        direct = todo[va]
        out.append(dict(
            va=va, row=r, cls=cls, tags=t, live=live,
            ready=sum(1 for c in direct if not todo[c]),
            one=sum(1 for c in direct if len(todo[c]) == 1),
            members=members, instr=sum(rows[m]['instructions'] for m in members),
            height=height[va], kept=sorted(kept_all),
            hook_callers=hook_callers, hook_callees=hook_callees, verdict=verdict(va),
            unprobed=[int(c, 16) for c in r.get('callees_unprobed', ()) if int(c, 16) not in hooks]))
    order = list(FRONTIER_CLASSES)
    out.sort(key=lambda e: (order.index(e['cls']), len(e['members']), e['instr'],
                            -e['row']['max'], e['va']))
    return out, left_out, verdict


def write_frontier(args, hooks, classified, rows, callees, reach):
    bytes_at = read_image(args.image) if args.image else None
    bridged = read_bridge_calls(args.bridge_calls) if args.bridge_calls else None
    if bridged is not None:
        bridged = {va for va in bridged if not indirect_call_site(bytes_at, va, hooks, rows)}
    entries, left_out, verdict = frontier_rows(hooks, classified, rows, callees, reach, bridged)
    tail = ''
    try:
        with open(args.output, encoding='utf-8') as f:
            old = f.read()
        i = old.find('\n' + FRONTIER_TAIL)
        if i >= 0:
            tail = old[i + 1:]
    except FileNotFoundError:
        pass

    # Callees without a probe row: their liveness is unknown, so they are neither
    # counted as live original callees nor part of a subtree; listed separately.
    unprobed = {}
    for va, r in rows.items():
        for c in r.get('callees_unprobed', ()):
            if int(c, 16) not in hooks:  # hooks are replaced whatever the probe said
                unprobed.setdefault(int(c, 16), set()).add(va)

    def first_bytes(va):
        b = bytes_at(va, 4) if bytes_at else None
        return b.hex() if b else '?'

    def push_entry(va):
        b = bytes_at(va, 1) if bytes_at else None
        return bool(b) and 0x50 <= b[0] <= 0x57

    def callee_text(e):
        parts = []
        for c in e['live']:
            v = verdict(c)
            if c in NEVER:
                parts.append(f'{c:#x} (runtime)')
            elif v not in (None, 'H4'):
                parts.append(f'{c:#x} ({v})')
            else:
                parts.append(f"{c:#x} ({rows[c]['instructions']})" if c in rows else f'{c:#x}')
        if len(parts) > 6:
            parts = parts[:6] + [f'… +{len(parts) - 6}']
        return ', '.join(parts)

    def subtree_text(e):
        members = sorted(e['members'] - {e['va']})
        text = f"{len(e['members'])} / {e['instr']}"
        if 0 < len(members) <= 4:
            text += ' (' + ', '.join(f'{m:#x}' for m in members) + ')'
        return text

    out = []
    out.append('# H4 frontier from the DBSIM map\n')
    out.append(
        f'Generated by `tools/reference-game/scripts/hook_candidates.py --frontier` from '
        f'`{args.map}` against {len(hooks)} registered hooks. The frontier is every live '
        'original function (entered in at least one suite, not a hook, not a runtime helper) '
        'with **two or more** live original callees left: the functions that the candidate '
        'list `hook_candidates.md` cannot admit. Counts are the saturated entry probes per '
        'suite (1000 = at least 1000).\n')
    out.append(
        'A live callee is *replaced* when it is a hook or classified in-progress, *kept* '
        'when it is a runtime helper or classified H3, platform, RTL or dead in the verdict '
        'file (it stays original and is called as a guest), and *to do* otherwise. '
        '**To replace** is the function plus every to-do function reachable through to-do '
        'callees (functions / instructions, the members listed when there are at most '
        'four): what one package would have to replace bottom-up before the function '
        'itself is ready. **Height** is the longest chain of to-do callees below it '
        '(0 = every live callee is replaced or kept; recursion adds nothing). '
        '**Ready/one** counts the direct to-do callees that are ready themselves (no to-do '
        'callee) or have exactly one. **Kept** lists the guest calls that stay original in '
        'the subtree. "Live original callees" gives each callee\'s instruction count, or its '
        'verdict when it is kept. "Unprobed" lists callees without a probe row (liveness '
        'unknown, not counted; see the section of that name). "Hook links" counts hook '
        'callers / replaced callees.\n')
    out.append(
        '**Ranking.** By class first, then by the size of the to-replace subtree '
        '(functions, then instructions; small first), then by the highest entry count '
        '(hot first). Classes: '
        + '; '.join(f'**{k}** {v}' for k, v in FRONTIER_CLASSES.items())
        + '. A and B are neighbours of replaced simulation code and the likeliest H4 '
        'targets; C holds the rest of the logic frame and the vtable/callback roots (HUD '
        'gadgets, cockpit, shell and simulation alike); D is rendering or presentation '
        'reach and normally H3; S comes last because the map counts were taken with every '
        'hook original and S functions are entered only from code that is replaced now '
        '(a pointer reference elsewhere in the image would still reach them, so check '
        'before calling one dead). Reach tags: L = logic frame `0x45f570` (through the map rows '
        'that list it as a caller), R = rendering `0x45fca8`, P = presentation `0x45fba4`, '
        'C = camera `0x4011a0`, V = none of them (vtable or callback only). Reach is static '
        'direct-call reach through map rows; functions that were already hooks when the map '
        'was taken have no row, so reach through them is missing. The class is a sorting '
        'aid, not a verdict: every role is a hypothesis until the contract is read from the '
        'bytes.\n')
    if bridged is not None:
        out.append(f'Bridge calls from `{args.bridge_calls}`: {len(bridged)} distinct '
                   'original or hooked callees are counted as `call_<va>` by the adapters.\n')
    if classified is not None:
        out.append(f'Verdicts from `{args.classified}`: frontier functions classified H3, '
                   'platform, RTL, dead or in-progress are left out of the table and listed '
                   'at the end; "Verdict" is H4 for a recorded H4 verdict and "open" for a '
                   'function without one.\n')
    header = ('| # | VA | Class | Instr. | Reach | Calls per suite (a60/c1s/c1c/c3_09/c4_08/c3_10) '
              '| Live original callees | Ready/one | To replace | Height | Kept | Unprobed '
              '| Hook links | Callers |')
    rule = '|---:|---|---|---:|---|---|---|---|---|---:|---|---|---|---|'
    if classified is not None:
        header += ' Verdict |'
        rule += '---|'
    out.append(header)
    out.append(rule)
    for n, e in enumerate(entries, 1):
        r = e['row']
        callers = ', '.join(r['callers'][:3]) + ('…' if len(r['callers']) > 3 else '')
        counts = '/'.join(str(r['counts'][s]) for s in SUITES)
        kept = ', '.join(f'{k:#x}' for k in e['kept'][:4]) + ('…' if len(e['kept']) > 4 else '')
        unp = ', '.join(f'{u:#x}' for u in e['unprobed'][:3]) + (
            '…' if len(e['unprobed']) > 3 else '')
        line = (f"| {n} | `{e['va']:#x}` | {e['cls']} | {r['instructions']} | {e['tags']} | "
                f"{counts} | {callee_text(e)} | {e['ready']}/{e['one']} | {subtree_text(e)} | "
                f"{e['height']} | {kept or '—'} | {unp or '—'} | "
                f"{e['hook_callers']}/{e['hook_callees']} | {callers or 'vtable/callback'} |")
        if classified is not None:
            line += ' H4 |' if e['verdict'] == 'H4' else ' open |'
        out.append(line)
    per_class = ', '.join(f"{k} {sum(1 for e in entries if e['cls'] == k)}"
                          for k in FRONTIER_CLASSES)
    out.append(f'\n{len(entries)} frontier functions ({per_class}). '
               f'Never H4 (runtime helpers): {", ".join(f"{v:#x}" for v in sorted(NEVER))}.\n')
    frontier_vas = {e['va'] for e in entries}
    out.append('## Unprobed callees\n')
    out.append(
        f'{len(unprobed)} distinct callees of live map rows have no probe row ("callees_unprobed" '
        'of the map) and are not hooks: their liveness is unknown, so the table neither counts them as live '
        'original callees nor puts them into a subtree; the column "Unprobed" shows them. The '
        'entry probe needs a one-byte `PUSH r32` (`50`..`57`) at the entry; the others are '
        'import thunks (`ff 25`), `enter` prologues (`c8`), short getters and other first '
        'bytes.\n')
    if bytes_at:
        pushes = sorted(va for va in unprobed if push_entry(va))
        out.append(
            f'First bytes read from `{args.image}`: **{len(pushes)}** of them begin with a '
            'one-byte push and could be probed once the probe inventory knows the entry '
            '(input for a tooling follow-up: inventory synced at known function starts, then a '
            'map re-run for these entries). "Live callers" counts the live map rows that call '
            'it, "Frontier callers" those of them in the table above.\n')
        out.append('| VA | First bytes | Live callers | Frontier callers | Callers |')
        out.append('|---|---|---:|---:|---|')
        for va in pushes:
            cs = sorted(unprobed[va])
            out.append(f'| `{va:#x}` | `{first_bytes(va)}` | {len(cs)} | '
                       f'{sum(1 for c in cs if c in frontier_vas)} | '
                       + ', '.join(f'{c:#x}' for c in cs[:4]) + ('…' if len(cs) > 4 else '')
                       + ' |')
        out.append('')
    else:
        out.append('Pass `--image` to classify their first bytes.\n')
    if classified is not None:
        out.append('## Left out (classified)\n')
        out.append(f'{len(left_out)} further functions have two or more live original callees '
                   f'but a verdict other than H4 in `{args.classified}`:\n')
        for v in VERDICTS[1:]:
            vas = sorted(va for va, x in left_out if x == v)
            if vas:
                out.append(f'- {v} ({len(vas)}): ' + ', '.join(f'`{va:#x}`' for va in vas))
        out.append('')
    text = '\n'.join(out)
    if tail:
        text += '\n' + tail
    with open(args.output, 'wb') as f:
        f.write(text.encode('utf-8'))
    print(f'{len(entries)} frontier functions written to {args.output}'
          + (f' ({len(left_out)} classified ones left out)' if classified is not None else '')
          + (' (proposed-packages section kept)' if tail else ''))
    if classified is not None:
        stale = sorted(va for va in classified if va in hooks)
        if stale:
            print_stale(stale, hooks)


BRIDGE_BEGIN = '<!-- bridge-table:begin -->'
BRIDGE_END = '<!-- bridge-table:end -->'
NOTES_HEADING = '### Bridge notes'
NOTE_KINDS = ('guest', 'core')


def read_counter_headers(directory):
    """Header name -> set of ('call', va) / ('slot', offset) counters it declares."""
    out = {}
    for path in sorted(Path(directory).glob('*.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        keys = {('call', int(m.group(1), 16))
                for m in re.finditer(r'"call_([0-9a-fA-F]{6})"', text)}
        keys |= {('slot', int(m.group(1), 16))
                 for m in re.finditer(r'"slot_([0-9a-fA-F]{2})"', text)}
        if keys:
            out[path.name] = keys
    return out


def read_live_counters(path, hook_names):
    """(kind, key) -> {hook: [count per suite]}, the record used per suite, the hooks seen.

    Per suite the `exit_counters` record is used when present (the counters
    at process exit), otherwise `last_record` (the last tick record).
    Counter names are `hk_<hook>_call_<va>` and `hk_<hook>_slot_<off>`; a
    counter whose hook is not registered is ignored.
    """
    with open(path, encoding='utf-8') as f:
        suites = json.load(f)['suites']
    rel, used, seen = {}, {}, set()
    for i, suite in enumerate(SUITES):
        entry = suites.get(suite)
        if not entry:
            used[suite] = None
            continue
        record = entry.get('exit_counters') or entry.get('last_record') or {}
        used[suite] = 'exit_counters' if entry.get('exit_counters') else 'last_record'
        seen.update(h for h in hook_names if f'hk_{h}_calls' in record)
        for name, count in record.items():
            m = re.fullmatch(r'hk_(\w+)_(call|slot)_([0-9a-f]{2,6})', name)
            if not m or m.group(1) not in hook_names:
                continue
            per_hook = rel.setdefault((m.group(2), int(m.group(3), 16)), {})
            per_hook.setdefault(m.group(1), [0] * len(SUITES))[i] = int(count)
    return rel, used, seen


def read_bridge_notes(path):
    """VA -> (name, kind, note) from the `### Bridge notes` table of a Markdown file."""
    out = {}
    inside = False
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()
    for n, line in enumerate(lines, 1):
        if line.startswith('#'):
            inside = line.rstrip() == NOTES_HEADING
            continue
        if not inside or not line.startswith('|'):
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        m = re.fullmatch(r'`?(0x[0-9a-fA-F]+)`?', cells[0])
        if not m:
            continue
        if len(cells) < 4 or cells[2] not in NOTE_KINDS:
            raise ValueError(f'{path}:{n}: expected VA, name, kind ({"/".join(NOTE_KINDS)}), note')
        out[int(m.group(1), 16)] = (cells[1], cells[2], '|'.join(cells[3:]).strip())
    return out


def write_bridge_table(args, hooks, classified, rows):
    names = set(hooks.values())
    headers = read_counter_headers(args.bridge_calls)
    rel, used, seen = read_live_counters(args.bridge_table, names)
    notes = read_bridge_notes(args.bridge_notes) if args.bridge_notes else {}
    classified = classified or {}
    # Counters declared in a header but absent from the record (a hook newer than the record).
    static_only = {}
    for header, keys in headers.items():
        for key in keys:
            if key not in rel:
                static_only.setdefault(key, set()).add(header)
    totals = {k: [sum(c[i] for c in v.values()) for i in range(len(SUITES))]
              for k, v in rel.items()}

    def per_suite(k):
        return '/'.join(str(x) for x in totals[k]) if k in totals else '—'

    def callers(k, limit=6):
        if k not in rel:
            return ', '.join(sorted(static_only[k])) + ' (no live counts)'
        hs = sorted(rel[k], key=lambda h: (-sum(rel[k][h]), h))
        text = ', '.join(f'`{h}`' for h in hs[:limit])
        return text + (f' … +{len(hs) - limit}' if len(hs) > limit else '')

    def total(k):
        return sum(totals.get(k, ()))

    calls = set(k for k in rel if k[0] == 'call') | set(k for k in static_only if k[0] == 'call')
    bytes_at = read_image(args.image) if args.image else None
    sites = sorted(k for k in calls if indirect_call_site(bytes_at, k[1], hooks, rows))
    calls -= set(sites)
    original = sorted((k for k in calls if k[1] not in hooks), key=lambda k: (-total(k), k[1]))
    nested = sorted((k for k in calls if k[1] in hooks), key=lambda k: (-total(k), k[1]))
    slots = sorted(set(k for k in rel if k[0] == 'slot') | set(k for k in static_only if k[0] == 'slot'),
                   key=lambda k: k[1])
    core = [k for k in original if notes.get(k[1], ('', 'guest'))[1] == 'core']
    live = [k for k in original if total(k) and k not in core]
    missing = sorted(h for h in names if h not in seen)

    def verdict(va):
        if va in classified:
            return classified[va][0], classified[va][1]
        if notes.get(va, ('', 'guest'))[1] == 'core':
            return 'own code', 'computed in core by its callers (counter only)'
        if va in NEVER:
            return 'runtime', '`NEVER` in `hook_candidates.py`'
        return 'open', '—'

    record_text = '; '.join(f'{s}: {used[s] or "missing"}' for s in SUITES)
    out = [BRIDGE_BEGIN, '']
    out.append(
        f'Generated by `tools/reference-game/scripts/hook_candidates.py --bridge-table` from '
        f'`{args.bridge_table}` ({record_text}) and the adapter headers in '
        f'`{args.bridge_calls}`, against {len(hooks)} registered hooks. '
        f'{len(calls)} distinct callees are counted as `call_<va>`: {len(nested)} are hooks '
        f'themselves (the bridge call enters the nested hook, which keeps its own mode) and '
        f'{len(original)} are original code; {len(core)} of these are computed in core and '
        f'only counted (kind "core" in the bridge notes), {len(live)} of the rest were called '
        f'in at least one suite. '
        + (f'{len(sites)} further `call_<va>` keys are the call sites of indirect calls, '
           f'no callees (see "Indirect call sites"). ' if sites else '')
        + f'{len(slots)} slot offsets are counted as `slot_<off>`. '
        f'Live calls per suite are summed over the calling hooks '
        f'(a60/c1s/c1c/c3_09/c4_08/c3_10).\n')
    if missing:
        out.append('Registered hooks absent from the record (no `hk_<hook>_calls`: added after '
                   'the run, so neither their calls nor their callee counters are live yet): '
                   + ', '.join(f'`{h}`' for h in missing) + '.\n')
    out.append('#### Original callees\n')
    out.append('| VA | Name | Instr. | Kind | Callers (hooks) | Live calls per suite | Total '
               '| Verdict | Package or document |')
    out.append('|---|---|---:|---|---|---|---:|---|---|')
    for k in original:
        va = k[1]
        name, kind, _ = notes.get(va, ('', 'guest', ''))
        if not name and va in classified:
            # first clause of the verdict reason
            name = re.split(r'[;:]| \(', classified[va][2], maxsplit=1)[0].strip()
            name = name if len(name) <= 60 else name[:59].rstrip() + '…'
        instr = rows[va]['instructions'] if va in rows else 'n/p'
        v, pkg = verdict(va)
        out.append(f'| `{va:#x}` | {name or "—"} | {instr} | {kind} | {callers(k)} | '
                   f'{per_suite(k)} | {total(k)} | {v} | {pkg} |')
    out.append('')
    out.append('#### Callees that are hooks\n')
    out.append('| VA | Hook | Callers | Live calls per suite | Total |')
    out.append('|---|---|---|---|---:|')
    for k in nested:
        out.append(f'| `{k[1]:#x}` | `{hooks[k[1]]}` | {callers(k, 4)} | {per_suite(k)} | {total(k)} |')
    out.append('')
    out.append('#### Slot calls\n')
    out.append('A `slot_<off>` counter counts the virtual calls through vtable offset `<off>` '
               'whatever the receiver class; which function runs depends on the receiver '
               '(the slot matrix of [vtables.md](../reverse-engineering/vtables.md#21-slot-matrix) '
               'for the actor classes).\n')
    out.append('| Slot | Callers (hooks) | Live calls per suite | Total |')
    out.append('|---|---|---|---:|')
    for k in slots:
        out.append(f'| `+{k[1]:#04x}` | {callers(k, 8)} | {per_suite(k)} | {total(k)} |')
    out.append('')
    if sites:
        out.append('#### Indirect call sites\n')
        out.append('A `call_<va>` key that is neither a hook nor a map row and whose bytes in '
                   f'`{args.image}` are an indirect near call (`FF /2`) is the address of a call '
                   'through a pointer (an object\'s slot, an event port) inside the calling hook\'s '
                   'original body: the counter counts those calls whatever the target, like a slot '
                   'counter, and the key is no function.\n')
        out.append('| Site | Callers (hooks) | Live calls per suite | Total |')
        out.append('|---|---|---|---:|')
        for k in sites:
            out.append(f'| `{k[1]:#x}` | {callers(k, 8)} | {per_suite(k)} | {total(k)} |')
        out.append('')
    out.append(BRIDGE_END)
    fragment = '\n'.join(out)
    text = None
    try:
        with open(args.output, encoding='utf-8') as f:
            old = f.read()
        i, j = old.find(BRIDGE_BEGIN), old.find(BRIDGE_END)
        if 0 <= i < j:
            text = old[:i] + fragment + old[j + len(BRIDGE_END):]
    except FileNotFoundError:
        pass
    replaced = text is not None
    if text is None:
        text = fragment + '\n'
    with open(args.output, 'wb') as f:
        f.write(text.encode('utf-8'))
    print(f'{len(calls)} bridged callees ({len(nested)} hooks, {len(original)} original, '
          f'{len(core)} core, {len(live)} live), {len(slots)} slots, {len(sites)} indirect call sites '
          f'written to {args.output}'
          + (' (between the markers)' if replaced else ''))
    unnamed = [k[1] for k in original if k[1] not in notes and k[1] not in classified]
    if unnamed:
        print('original callees without a note or verdict: '
              + ', '.join(f'{va:#x}' for va in unnamed))


if __name__ == '__main__':
    sys.exit(main() or 0)
