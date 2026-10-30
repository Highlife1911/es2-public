"""DBSIM map: batch plan for an entry-probe run and map.json from its reports.

Both inputs come from probe_instruction_inventory.py: the known function starts with their probe
kinds (step 4 of its rule) and the reached instructions of its descent.
Standard library plus the pinned llvm-objdump; no original bytes are written.

    python dbsim_map.py plan <DBSIM.EXE> <out-dir> [--batch-size 512]
            [--exclude-registry hook_registry.h]
        Writes plan.json and batch-<n>.txt (comma-separated VAs for
        run-live-coverage.ps1 -ProbeEntries). Batch `t` holds the starts that
        could run on another thread (import thunks, operand-pointer callbacks)
        so that a hit there invalidates only that batch; the others hold the
        remaining probeable starts in address order. With
        --exclude-registry (a run with hooks replaced) the starts that
        a patched hook prologue covers are left out: the hook's own start and
        any start in the HOOK_SPAN bytes after it (the proxy refuses a probe
        inside a patched prologue; every relocated prologue is shorter).

    python dbsim_map.py build <DBSIM.EXE> <map.json> --registry hook_registry.h \
        --batches <dir> [<dir> ...] [--suites a,b,...]
        Reads <dir>/<suite>/probes.json of every valid batch (valid:
        probes-summary.md present and not "Probes invalid", every report
        complete, error 0, no other thread) and writes map.json (rows = live
        original functions, as hook_candidates.py and function_index.py read
        them) plus `hooks` (the registered hooks' own counts when the run kept
        every hook original) and `unprobed`. Where a suite also has probes-load.json (the
        counts at the first tick) every row gets `load` (per suite: the
        calls before the first frame) and the summary `load_only` (rows that
        ran in no frame of any suite, exactly known); with --baseline
        <map.json> of a run with every hook original, `superseded` lists the
        baseline's rows that this run probed and that no longer run.

Static summary per start (`instructions`, `calls`, `tail_calls`,
`indirect_calls`, `jump_tables`, `x87`, `globals`): a walk over the reached
instructions from the start, following fall-through, both branch targets,
direct jumps (a jump to another start is a tail call) and jump tables;
`call rel32` targets are callees, indirect calls are counted; ret, int3,
hlt and other indirect jumps end a path. `globals` counts distinct
base-relocated targets outside CODE that the walked instructions hold.
"""
import argparse
import bisect
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import probe_instruction_inventory as inventory  # noqa: E402

# The pinned objdump: ES2_DEPENDENCIES, else the dependency directory of the running pinned Python.
OBJDUMP = Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'
MAX_BATCH = 512  # EP_MAX_ENTRIES of proxy/entry_probe_engine.h
HOOK = re.compile(r'ES2_HOOK\((\w+), (0x[0-9a-f]+),')
IMAGE_BASE = 0x400000
HOOK_SPAN = 16  # bytes after a hook start left out of a replaced-hooks plan (prologues relocate at most 9)


def starts_and_code(executable, objdump):
    """Known starts (VA -> (kind, sources)) and the step-4 reach with its decode and jump tables."""
    state = {}
    _, _, starts = inventory.full_inventory(executable, objdump, state=state)
    image, reached = state['image'], state['reached']
    decoded = inventory.decode(image, set(reached), objdump)
    code = {}
    for va, size in reached.items():
        if decoded.get(va, (None,))[0] != size:
            raise ValueError(f'Decode of {va:#x} differs from the descent')
        code[va] = decoded[va]
    return starts, image, code, state['tables']


def plan(starts, size=MAX_BATCH, hooks=()):
    """Batches of probeable starts: ('t', thread candidates) first, then address order.

    Starts inside a hook's span (`hooks`: the hook starts of a replaced-hooks run) are left out."""
    if not 1 <= size <= MAX_BATCH:
        raise ValueError(f'Batch size must be 1..{MAX_BATCH}')
    hooks = sorted(hooks)

    def covered(va):
        i = bisect.bisect_right(hooks, va)
        return i > 0 and va - hooks[i - 1] < HOOK_SPAN
    probeable = sorted(va for va, (kind, _) in starts.items() if not kind.startswith('unprobed') and not covered(va))
    threads = [va for va in probeable if starts[va][0] == 'EP_JMP_ABS' or 'operand_pointers' in starts[va][1]]
    rest = [va for va in probeable if va not in set(threads)]
    batches = [('t', threads)] if threads else []
    for i in range(0, len(rest), size):
        batches.append((f'{i // size:02d}', rest[i:i + size]))
    if any(len(vas) > MAX_BATCH for _, vas in batches):
        raise ValueError('Thread batch exceeds the engine limit')
    return batches


def summarize(image, code, tables, starts):
    """Static summary per start (see the module docstring)."""
    order = sorted(code)

    def holder(va):
        i = bisect.bisect_right(order, va) - 1
        return order[i] if i >= 0 and order[i] <= va < order[i] + code[order[i]][0] else None

    held = {}
    for location in image.relocations:
        source = holder(location)
        target = image.dword(location)
        if source is not None and target is not None and not image.in_code(target):
            held.setdefault(source, set()).add(target)
    out = {}
    for start in sorted(starts):
        seen, stack = set(), [start]
        calls, tails, globals_ = set(), set(), set()
        indirect = jump_tables = x87 = 0
        while stack:
            va = stack.pop()
            if va in seen or va not in code:
                continue
            seen.add(va)
            size, mnemonic, operands = code[va]
            following = va + size
            target = inventory.TARGET.match(operands)
            globals_ |= held.get(va, set())
            if mnemonic.startswith('f'):
                x87 += 1
            if mnemonic in inventory.ENDS:
                continue
            if mnemonic == 'calll':
                if target:
                    calls.add(int(target[1], 16))
                else:
                    indirect += 1
                stack.append(following)
            elif mnemonic == 'jmp' and target:
                destination = int(target[1], 16)
                if destination in starts and destination != start:
                    tails.add(destination)
                else:
                    stack.append(destination)
            elif mnemonic == 'jmpl':
                if va in tables:
                    jump_tables += 1
                    table, count = tables[va]
                    stack.extend(image.dword(table + 4 * i) for i in range(count))
            elif mnemonic in inventory.CONDITIONAL:
                stack += [following, int(target[1], 16)]
            elif following in starts and following != start:
                tails.add(following)  # falls into the next known start
            else:
                stack.append(following)
        out[start] = dict(instructions=len(seen), x87=x87, indirect_calls=indirect, jump_tables=jump_tables,
                          globals=len(globals_), calls=sorted(f'{c:#x}' for c in calls),
                          tail_calls=sorted(f'{c:#x}' for c in tails))
    return out


def read_batches(directories, suites=None):
    """Counts, saturation and first-tick counts per VA and suite from the valid batches.

    Returns (counts, saturated, load, ok, bad, suites); `load` has a suite only where the
    batch wrote probes-load.json for it."""
    counts, saturated, load, ok, bad = {}, {}, {}, [], []
    directories = sorted(Path(d) for d in directories)
    if suites is None:  # every suite that any batch reports on
        suites = sorted({p.parent.name for d in directories for p in d.glob('*/probes.json')})
    for directory in directories:
        marker = directory / 'probes-summary.md'
        if not marker.is_file() or marker.read_text(encoding='utf-8-sig').startswith('Probes invalid'):
            bad.append(directory.name)
            continue
        reports, valid = {}, True
        for suite in suites:
            path = directory / suite / 'probes.json'
            if not path.is_file():
                valid = False
                break
            report = json.loads(path.read_text(encoding='utf-8-sig'))
            if not report.get('complete') or report.get('error') or report.get('other_thread'):
                valid = False
                break
            first = directory / suite / 'probes-load.json'
            if first.is_file():
                first = json.loads(first.read_text(encoding='utf-8-sig'))
                if first.get('reason') != 'first_tick' or first.get('error') or first.get('other_thread'):
                    valid = False
                    break
                report = dict(report, first_tick={int(e['va'], 16): e['count'] for e in first['entries']})
            reports[suite] = report
        if not valid:
            bad.append(directory.name)
            continue
        for suite, report in reports.items():
            for entry in report['entries']:
                va = int(entry['va'], 16)
                counts.setdefault(va, {})[suite] = entry['count']
                saturated.setdefault(va, {})[suite] = entry['saturated']
                if 'first_tick' in report:
                    load.setdefault(va, {})[suite] = report['first_tick'][va]
        ok.append(directory.name)
    return counts, saturated, load, ok, bad, list(suites)


def build(starts, cfg, hooks, counts, saturated, ok, bad, suites, load=None, baseline=None):
    """map.json rows plus `hooks`, `unprobed`, `load` and `superseded`."""
    load = load or {}
    probed = {va for va, c in counts.items() if len(c) == len(suites)}
    live = {va for va in probed if any(counts[va].values())}
    hot = {va for va in live if any(saturated[va].values())}
    callers = {}
    for f, v in cfg.items():
        for c in v['calls'] + v['tail_calls']:
            callers.setdefault(int(c, 16), set()).add(f)

    def status(f):
        if f in hooks:
            return 'replaced'
        if f in live:
            return 'live'
        if f in probed:
            return 'dead'
        return 'unprobed'

    def subtree(f):
        seen, stack = set(), [f]
        while stack:
            g = stack.pop()
            if g in seen or g not in cfg:
                continue
            seen.add(g)
            stack += [int(c, 16) for c in cfg[g]['calls'] + cfg[g]['tail_calls']
                      if status(int(c, 16)) in ('live', 'unprobed')]
        return seen

    rows = []
    for f in sorted(live - set(hooks)):
        v = cfg.get(f, {})
        callees = [int(c, 16) for c in v.get('calls', []) + v.get('tail_calls', [])]
        st = {c: status(c) for c in callees}
        sub = subtree(f)
        rows.append(dict(
            va=f'{f:#x}', kind=starts[f][0] if f in starts else None,
            sources=starts[f][1] if f in starts else [], max=max(counts[f].values()), hot=f in hot,
            suites=sum(1 for x in counts[f].values() if x), counts=counts[f],
            instructions=v.get('instructions'), x87=v.get('x87'), indirect=v.get('indirect_calls'),
            jump_tables=v.get('jump_tables'), globals=v.get('globals'),
            callees_replaced=sum(1 for x in st.values() if x == 'replaced'),
            callees_live=[f'{c:#x}' for c, x in st.items() if x == 'live'],
            callees_unprobed=[f'{c:#x}' for c, x in st.items() if x == 'unprobed'],
            callers=sorted(f'{c:#x}' for c in callers.get(f, ())),
            subtree_functions=len(sub), subtree_instructions=sum(cfg[g]['instructions'] for g in sub if g in cfg),
            subtree_replaced=sum(1 for g in sub for c in cfg[g]['calls'] if int(c, 16) in hooks)))
        if f in load:
            rows[-1]['load'] = load[f]
    hook_rows = [dict(va=f'{va:#x}', name=hooks[va], counts=counts[va], live=va in live)
                 for va in sorted(set(hooks) & probed)]
    unprobed = {f'{va:#x}': kind for va, (kind, _) in sorted(starts.items()) if va not in probed}
    summary = dict(batches_ok=ok, bad=bad, suites=suites, starts=len(starts), probed=len(probed),
                   live=len(live), hot=len(hot), replaced=len(hooks), replaced_live=len(set(hooks) & live),
                   dead=len(probed - live), unprobed=len(unprobed))
    result = dict(summary=summary, rows=rows, hooks=hook_rows, unprobed=unprobed)
    split = [r for r in rows if len(r.get('load', ())) == len(suites)]
    if split:
        # Exactly known only where the final count is below saturation.
        summary.update(load_split=len(split), load_only=sum(
            1 for r in split if all(r['counts'][s] == r['load'][s] and not saturated[int(r['va'], 16)][s] for s in suites)))
    if baseline is not None:
        result['superseded'] = [dict(va=r['va'], baseline_max=r['max'], baseline_suites=r['suites'])
                                for r in baseline['rows'] if int(r['va'], 16) in probed - live - set(hooks)]
        summary['superseded'] = len(result['superseded'])
    return result


def registry(path):
    return {IMAGE_BASE + int(m[2], 16): m[1] for m in HOOK.finditer(Path(path).read_text(encoding='utf-8-sig'))}


def write_json(path, value):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, indent=0)
        f.write('\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--objdump', type=Path, default=OBJDUMP)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan')
    p.add_argument('executable', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--batch-size', type=int, default=MAX_BATCH)
    p.add_argument('--exclude-registry', type=Path, help='hook_registry.h: plan for a run with every hook replaced')
    b = sub.add_parser('build')
    b.add_argument('executable', type=Path)
    b.add_argument('output', type=Path)
    b.add_argument('--registry', type=Path, required=True)
    b.add_argument('--batches', nargs='+', required=True)
    b.add_argument('--suites', help='comma-separated; default: every suite any batch reports on')
    b.add_argument('--baseline', type=Path, help='map.json of a run with every hook original (superseded rows)')
    args = parser.parse_args(argv)
    if args.command == 'plan':
        state = {}
        _, _, starts = inventory.full_inventory(args.executable, args.objdump, state=state)
        batches = plan(starts, args.batch_size, registry(args.exclude_registry) if args.exclude_registry else ())
        os.makedirs(args.output, exist_ok=True)
        for name, vas in batches:
            (args.output / f'batch-{name}.txt').write_bytes(','.join(f'{va:x}' for va in vas).encode('ascii'))
        write_json(args.output / 'plan.json', dict(
            batch_size=args.batch_size, batches={name: [f'{va:#x}' for va in vas] for name, vas in batches},
            unprobed={f'{va:#x}': kind for va, (kind, _) in sorted(starts.items()) if kind.startswith('unprobed')}))
        print(json.dumps({name: len(vas) for name, vas in batches}))
        return 0
    starts, image, code, tables = starts_and_code(args.executable, args.objdump)
    cfg = summarize(image, code, tables, starts)
    counts, saturated, load, ok, bad, suites = read_batches(args.batches, args.suites.split(',') if args.suites else None)
    baseline = json.loads(args.baseline.read_text(encoding='utf-8-sig')) if args.baseline else None
    result = build(starts, cfg, registry(args.registry), counts, saturated, ok, bad, suites, load, baseline)
    write_json(args.output, result)
    print(json.dumps(result['summary'], indent=0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
