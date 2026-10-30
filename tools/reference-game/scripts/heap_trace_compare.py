"""Compares two heap traces (own_process.md "the heap trace"): the
original's (the proxy's [rtl] heap_trace, proxy/rtl_heap_shadow.h) and
es2sim's ([trace] heap_trace, sim/src/host.c). Both write the same lines:
one attach line (the heap's variables at DBSIM's entry, the sizes of the
environment strings), one block line per block that exists then, one line
per heap operation, one end line.

    python heap_trace_compare.py ORIGINAL ES2SIM              # text report
    python heap_trace_compare.py ORIGINAL ES2SIM --json
    python heap_trace_compare.py ORIGINAL ES2SIM --find original:02bdd95c

Report:

- attach: frame, block count and sizes, regions, environment strings;
- operation counts per function and origin (`import`: DBSIM's imports,
  `internal`: CW3220's own calls, `external`: any other module);
- the first operation where the two call sequences differ: function, origin,
  request size (calloc: count times size) and, for imports, the caller;
- along the common prefix: whether each free and realloc names the partner of
  the block the other side frees (a block freed again on both sides, the
  partner of the other side's freed one, is a double free, listed, not a
  difference: the game frees some blocks twice, and CW3220's _free leaves
  a block without its in-use flag), and the low 16 bits of each result (the
  4 MiB reservations are 64K aligned, so the low word is the block's offset
  in its reservation) with their difference, grouped into runs of equal
  difference, and the first operation whose low words differ;
- `--find SIDE:ADDRESS` (hex): the operation that made the block holding that
  address on that side (the latest one at or below the address that was not
  freed before the end of the common prefix) and its partner on the other.

Exit status 0 when the common prefix covers both traces and every low word
matches, 1 otherwise, 2 on unreadable input. Read-only, standard library only.
"""
import argparse
import json
import sys

FUNCTIONS = ('malloc', 'calloc', 'free', 'realloc')


class TraceError(ValueError):
    pass


def read_trace(path):
    """{'attach', 'blocks', 'ops', 'end'} of one trace file."""
    attach, end, blocks, ops = None, None, [], []
    try:
        with open(path, encoding='ascii') as f:
            for number, line in enumerate(f, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except ValueError as e:
                    raise TraceError('%s:%d: %s' % (path, number, e))
                kind = record.get('kind')
                if kind == 'attach':
                    attach = record
                elif kind == 'block':
                    blocks.append(record)
                elif kind == 'end':
                    end = record
                elif 'op' in record:
                    if record.get('fn') not in FUNCTIONS:
                        raise TraceError('%s:%d: unknown function %r' % (path, number, record.get('fn')))
                    ops.append(record)
                else:
                    raise TraceError('%s:%d: unknown line' % (path, number))
    except OSError as e:
        raise TraceError(str(e))
    if attach is None:
        raise TraceError('%s: no attach line' % path)
    return {'attach': attach, 'blocks': blocks, 'ops': ops, 'end': end}


def hexval(record, field):
    return int(record[field], 16)


def request(op):
    """The request size of an allocating operation (None for free)."""
    fn = op['fn']
    if fn == 'malloc':
        return hexval(op, 'a0')
    if fn == 'calloc':
        return hexval(op, 'a0') * hexval(op, 'a1')
    if fn == 'realloc':
        return hexval(op, 'a1')
    return None


def call_key(op):
    """What both sides must agree on: function, origin and request size."""
    return (op['fn'], op['from'], request(op))


def same_call(o, s):
    """Equal keys and, for imports, equal callers where both sides name one:
    CW3220's internal callers are its own addresses, and es2sim names 0 for
    a call from its own C code (no return address)."""
    if call_key(o) != call_key(s):
        return False
    if o['from'] != 'import' or int(o['caller'], 16) == 0 or int(s['caller'], 16) == 0:
        return True
    return o['caller'] == s['caller']


def low_delta(original, simulated):
    """(simulated - original) of the low words as a signed 16-bit value."""
    d = ((simulated & 0xffff) - (original & 0xffff)) & 0xffff
    return d - 0x10000 if d >= 0x8000 else d


def counts(ops):
    result = {}
    for op in ops:
        key = '%s/%s' % (op['fn'], op['from'])
        result[key] = result.get(key, 0) + 1
    return dict(sorted(result.items()))


def attach_summary(trace):
    a = trace['attach']
    sizes = {}
    for b in trace['blocks']:
        if b.get('used'):
            sizes[b['size']] = sizes.get(b['size'], 0) + 1
    return {
        'frame': a.get('frame'), 'blocks': a.get('blocks'), 'rover': a.get('rover'),
        'sentinel': a.get('sentinel'), 'regions': a.get('regions', []),
        'used_sizes': dict(sorted(sizes.items())), 'env_strings': len(a.get('env_lengths', [])),
        'env_bytes': sum(a.get('env_lengths', [])) + len(a.get('env_lengths', [])),
    }


def compare(original, simulated):
    """The comparison of two read traces as one dict."""
    o_ops, s_ops = original['ops'], simulated['ops']
    common, divergence = 0, None
    pointer_map = {}          # original block -> es2sim block along the prefix
    freed = {}                # original block -> es2sim block, freed and not handed out again
    double_frees = []
    pairing = None            # first free/realloc that names a different partner
    runs, first_low = [], None
    for i in range(min(len(o_ops), len(s_ops))):
        o, s = o_ops[i], s_ops[i]
        if not same_call(o, s):
            divergence = {'op': i, 'original': describe(o), 'es2sim': describe(s)}
            break
        if o['fn'] in ('free', 'realloc'):
            o_ptr, s_ptr = hexval(o, 'a0'), hexval(s, 'a0')
            if o['fn'] == 'free' and o_ptr and o_ptr not in pointer_map and freed.get(o_ptr) == s_ptr:
                double_frees.append({'op': i, 'frame': o['frame'], 'original': o['a0'], 'es2sim': s['a0']})
            elif o_ptr and pointer_map.get(o_ptr) != s_ptr and pairing is None:
                pairing = {'op': i, 'original': o['a0'], 'es2sim': s['a0'],
                           'expected': '%08x' % pointer_map[o_ptr] if o_ptr in pointer_map else None}
            if o_ptr in pointer_map:
                freed[o_ptr] = pointer_map.pop(o_ptr)
        if o['fn'] != 'free':
            o_res, s_res = hexval(o, 'result'), hexval(s, 'result')
            freed.pop(o_res, None)
            if o_res and s_res:
                pointer_map[o_res] = s_res
                d = low_delta(o_res, s_res)
                if runs and runs[-1]['delta'] == d:
                    runs[-1]['last'] = i
                    runs[-1]['ops'] += 1
                else:
                    runs.append({'delta': d, 'first': i, 'last': i, 'ops': 1, 'frame': o['frame']})
                if d and first_low is None:
                    first_low = {'op': i, 'frame': o['frame'], 'fn': o['fn'], 'request': request(o),
                                 'original': o['result'], 'es2sim': s['result'], 'delta': d}
            elif bool(o_res) != bool(s_res) and pairing is None:
                pairing = {'op': i, 'original': o['result'], 'es2sim': s['result'], 'expected': None}
        common = i + 1
    return {
        'attach': {'original': attach_summary(original), 'es2sim': attach_summary(simulated)},
        'counts': {'original': counts(o_ops), 'es2sim': counts(s_ops)},
        'ops': {'original': len(o_ops), 'es2sim': len(s_ops), 'common': common},
        'end': {'original': original['end'], 'es2sim': simulated['end']},
        'divergence': divergence,
        'pairing': pairing,
        'double_frees': double_frees,
        'first_low_difference': first_low,
        'delta_runs': runs,
    }


def describe(op):
    return {'frame': op['frame'], 'fn': op['fn'], 'from': op['from'], 'request': request(op),
            'caller': op['caller'], 'result': op.get('result')}


def find(original, simulated, side, address, common):
    """The operation that made the block holding `address` on `side`, among
    the first `common` operations, and its partner (same index) on the other."""
    ops = (original if side == 'original' else simulated)['ops'][:common]
    live = {}
    for i, op in enumerate(ops):
        if op['fn'] in ('free', 'realloc'):
            live.pop(hexval(op, 'a0'), None)
        if op['fn'] != 'free' and hexval(op, 'result'):
            live[hexval(op, 'result')] = i
    below = [p for p in live if p <= address]
    if not below:
        return None
    p = max(below)
    i = live[p]
    other = (simulated if side == 'original' else original)['ops'][i]
    return {'side': side, 'address': '%08x' % address, 'block': '%08x' % p, 'offset': address - p, 'op': i,
            'this': describe(ops[i]), 'partner': describe(other),
            'partner_address': '%08x' % (hexval(other, 'result') + address - p)}


def text_report(result):
    lines = []
    for side in ('original', 'es2sim'):
        a = result['attach'][side]
        lines.append('%-8s attach frame %s, %s blocks, rover %s, sentinel %s, %d regions, environment %d strings %d bytes'
                     % (side, a['frame'], a['blocks'], a['rover'], a['sentinel'], len(a['regions']), a['env_strings'],
                        a['env_bytes']))
        if a['used_sizes']:
            lines.append('         used block sizes: %s' % ', '.join('%sx%d' % (k, v) for k, v in a['used_sizes'].items()))
        lines.append('         ops %d: %s' % (result['ops'][side],
                                               ', '.join('%s %d' % kv for kv in result['counts'][side].items())))
    lines.append('common prefix: %d operations' % result['ops']['common'])
    if result['divergence']:
        d = result['divergence']
        lines.append('first different call at op %d: original %s, es2sim %s' % (d['op'], d['original'], d['es2sim']))
    if result['pairing']:
        lines.append('first different pairing: %s' % result['pairing'])
    for d in result['double_frees'][:10]:
        lines.append('double free on both sides: op %d (frame %s): original %s, es2sim %s'
                     % (d['op'], d['frame'], d['original'], d['es2sim']))
    if len(result['double_frees']) > 10:
        lines.append('  ... %d more double frees' % (len(result['double_frees']) - 10))
    if result['first_low_difference']:
        lines.append('first different low word: %s' % result['first_low_difference'])
    lines.append('low-word difference runs (es2sim - original):')
    for r in result['delta_runs'][:40]:
        lines.append('  ops %6d-%-6d (%5d results, from frame %s): %+d' % (r['first'], r['last'], r['ops'], r['frame'],
                                                                           r['delta']))
    if len(result['delta_runs']) > 40:
        lines.append('  ... %d more runs' % (len(result['delta_runs']) - 40))
    if result.get('find'):
        lines.append('find: %s' % result['find'])
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('original')
    parser.add_argument('es2sim')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--find', help='SIDE:ADDRESS, SIDE original or es2sim, ADDRESS hex')
    args = parser.parse_args(argv)
    try:
        original, simulated = read_trace(args.original), read_trace(args.es2sim)
    except TraceError as e:
        print('heap_trace_compare: %s' % e, file=sys.stderr)
        return 2
    result = compare(original, simulated)
    if args.find:
        side, _, address = args.find.partition(':')
        if side not in ('original', 'es2sim') or not address:
            parser.error('--find SIDE:ADDRESS')
        result['find'] = find(original, simulated, side, int(address, 16), result['ops']['common'])
    print(json.dumps(result, indent=1) if args.json else text_report(result))
    complete = result['ops']['common'] == result['ops']['original'] == result['ops']['es2sim']
    return 0 if complete and not result['first_low_difference'] and not result['pairing'] else 1


if __name__ == '__main__':
    sys.exit(main())
