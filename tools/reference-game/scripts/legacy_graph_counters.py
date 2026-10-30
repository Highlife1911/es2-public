"""Generate the legacy graph-counter dispatch of proxy/callee_counters.h from the registry.

Two regions of callee_counters.h are generated:

* the `#define <group>_hook_counters legacy_graph_counters` line of every
  registry group that has no counter function of its own, and
* the `GRAPH_COUNTERS(<hook>)` lines inside `legacy_graph_counters`.

Both follow from the hook registry (hook_registry.h) and from the sources:

* A group is legacy when no `*_hooks.h` defines `<group>_hook_counters`
  (a newer group defines it next to its own counter arrays). The lines are
  written in order of the group's first registry entry.
* A hook is listed when its group is legacy, callee_counters.h defines
  `static callee_counter <hook>_counters[]` and the hook is not one of the
  DEDICATED observers (their arrays are read by the `*_observe` functions
  of the damage and ground groups, not through the hook index). The lines
  are written in registry order.

The generated regions sit between BEGIN/END marker comments; everything
else in the file is untouched. `--check` compares byte for byte. Nothing
here reads the original game; `counter_keys` only parses the arrays so a
test can compare their call targets with the instruction starts of the
reference listing (see tests/test_legacy_graph_counters.py).
"""
import argparse
import re
from pathlib import Path

REGISTRY = re.compile(r'^ES2_HOOK\((\w+),\s*(0x[0-9a-fA-F]+),\s*[^,]+,\s*[^,]+,\s*(\w+)\)\s*$', re.M)
COUNTER_FUNCTION = re.compile(r'callee_counter\s*\*\s*(\w+)_hook_counters\s*\(\s*unsigned\b')
ARRAY = re.compile(r'^static callee_counter (\w+)_counters\[\]\s*=\s*\{(.*?)^\};', re.M | re.S)
ENTRY = re.compile(r'\{"(\w+)",\s*ES2_COLLIDE_(\w+),\s*(0x[0-9a-fA-F]+|0),\s*0\}')

# Hooks of a legacy group whose counter arrays are observed directly by the
# damage and ground-effect observers, so the hook index never maps to them.
DEDICATED = ('ground_update', 'ground_collide', 'ground_effects', 'vehicle_hit', 'building_hit', 'radius_hit')

BEGIN_DEFINES = '/* BEGIN generated: legacy group definitions (scripts/legacy_graph_counters.py) */\n'
END_DEFINES = '/* END generated: legacy group definitions */\n'
BEGIN_LIST = '/* BEGIN generated: legacy graph counters (scripts/legacy_graph_counters.py) */\n'
END_LIST = '/* END generated: legacy graph counters */\n'


def registry(path):
    """[(hook, group)] in registry order."""
    rows = [(name, group) for name, _, group in REGISTRY.findall(Path(path).read_text(encoding='utf-8'))]
    if not rows:
        raise ValueError('no ES2_HOOK entries in the registry')
    return rows


def own_counter_groups(headers):
    """Groups that define their own `<group>_hook_counters` function."""
    found = {}
    for header in headers:
        for group in COUNTER_FUNCTION.findall(Path(header).read_text(encoding='utf-8')):
            if group in found and found[group] != header:
                raise ValueError(f'{group}_hook_counters defined in {found[group]} and {header}')
            found[group] = header
    return set(found)


def counter_arrays(text):
    """Names of the `static callee_counter <name>_counters[]` arrays, in file order."""
    return [m[1] for m in ARRAY.finditer(text)]


def counter_keys(text):
    """{array: [(name, kind, key)]} of every counter array of callee_counters.h."""
    return {m[1]: [(n, kind, int(key, 16)) for n, kind, key in ENTRY.findall(m[2])]
            for m in ARRAY.finditer(text)}


def plan(rows, own, arrays):
    """(legacy groups in registry order, listed hooks in registry order)."""
    groups = list(dict.fromkeys(group for _, group in rows))
    legacy = [group for group in groups if group not in own]
    present = set(arrays)
    listed = [hook for hook, group in rows if group in legacy and hook in present and hook not in DEDICATED]
    return legacy, listed


def region(text, begin, end):
    start = text.find(begin)
    stop = text.find(end)
    if start < 0 or stop < start or text.count(begin) != 1 or text.count(end) != 1:
        raise ValueError(f'markers missing or duplicated: {begin.strip()}')
    return start + len(begin), stop


def render(text, legacy, listed):
    """`text` with both generated regions replaced."""
    defines = ''.join(f'#define {group}_hook_counters legacy_graph_counters\n' for group in legacy)
    lines = ''.join(f'    GRAPH_COUNTERS({hook})\n' for hook in listed)
    for begin, end, body in ((BEGIN_LIST, END_LIST, lines), (BEGIN_DEFINES, END_DEFINES, defines)):
        start, stop = region(text, begin, end)
        text = text[:start] + body + text[stop:]
    return text


def generate(counters, registry_path, headers):
    text = Path(counters).read_bytes().decode('utf-8')
    if '\r' in text:
        raise ValueError('callee_counters.h must use LF line ends')
    rows = registry(registry_path)
    legacy, listed = plan(rows, own_counter_groups(headers), counter_arrays(text))
    return render(text, legacy, listed)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('proxy', type=Path, help='tools/reference-game/proxy')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = args.proxy / 'callee_counters.h'
    headers = sorted(args.proxy.glob('*_hooks.h'))
    result = generate(target, args.proxy / 'hook_registry.h', headers).encode('utf-8')
    if args.check:
        if target.read_bytes() != result:
            raise SystemExit('callee_counters.h differs: run scripts/legacy_graph_counters.py tools/reference-game/proxy')
    else:
        target.write_bytes(result)
