"""The own process's gap list: original functions that own code still reaches without a row or hook.

The own DBSIM (`sim/`) never runs original code: every call to an original
address goes through the dispatch (`sim/include/es2/sim/dispatch.h`), which
answers it with a hook (the registry's adapter) or a service row
(`sim/dispatch/services/*.h`) and refuses anything else, which stops the run.
This tool lists what is still refused, from four committed sources and the
user's DBSIM.EXE:

1. the **service rows** (`ES2_SIM_SERVICE(...)` lines of the fragments);
2. the **hook registry** (`hook_registry.h`; RVA plus the image base);
3. the **guest-call sites** of `sim/src`: every VA that own code passes to
   `es2_sim_call` or to a wrapper of it (a function or macro whose parameter
   reaches the VA slot of a wrapper, found by a fixpoint over all files:
   `call`, `CALL`, `HCALL`, `su_call`, ...), constants resolved through
   `#define`, `enum` and `const` (with `+`, `-`, `|`, shifts and casts);
   a call whose VA is not a constant (an object's slot, a variable) is a
   *dynamic* site and counted per file. A file's function-like macros are
   expanded where they are invoked outside braces, so bodies written as
   `ROW(name) { ... }` or `ROW(name, body)` are functions of their own
   (`RD10_TRY(callr(l, 0x47a1ac, ...))` in fill.c's `RD10_SERVICE` rows).
   Limit: a slot call (`HVCALL(c, x, 0x64, ...)`, `vcallr`, `r32(r32(o) + off)`)
   stays dynamic: the object is read from guest memory and the scanner has no
   type for it, so its table is unknown even when the class table is a constant
   of the image (`0x40e40c`, slot +0x64 of the ENERGY table `0x498a84`, called
   only by `weapon_gauges` through `HVCALL(x, 0x64)`). Such a target enters the
   list only as a slot of a table that sim/src names (*virtual*, below);
4. the **bridged calls** of the hook adapters (`"call_<va>"` counters of
   `tools/reference-game/proxy/*.h`: a hook's replacement calls the original
   function through the trap, which the dispatch answers like a call from own
   code); a key that is no function start and whose bytes are an indirect call
   (`FF /2`) is the call site of a call through a pointer (e.g. `420a8d`,
   `420acb`), named in the sources line but no root;
5. the **static call graph** of DBSIM.EXE from `dbsim_map.py` (the
   synchronized descent of `probe_instruction_inventory.py`: instruction
   counts per function start, direct calls, tail calls and jump tables; calls
   through pointers and vtables are not followed and are counted per function).

A *gap root* is a VA that own code calls (or a hook adapter bridges) that has
neither a row nor a hook. The *gap* is every original function behind a root
that has neither a row nor a hook either (breadth-first over the call graph,
stopping at rows and hooks): what own code would have to replace, or the
dispatch refuses. *Referenced only* VAs are code addresses that sim/src names
but never passes to a call (tables, comparisons, variables): possible calls,
listed apart. *Virtual* targets are the slots of the function-pointer tables
whose VA sim/src names, with their own behind.

A *verdict* (`sim_gap_verdicts.json`: VAs, kind `dead`, `caller-replaced`,
`behind` or `served`, the document section as `path#anchor`, a short reason)
marks a gap VA whose refusal a document explains; the document links it in
every table and counts the *open* VAs (no row, no hook, no verdict) apart.

Counts are facts of the sources; the roles of the functions are not claimed.
Standard library only (dbsim_map.py and the pinned llvm-objdump are needed for
the call graph). The output holds addresses, names and counts, nothing that
originates from the CD beyond VAs and instruction counts.

    python sim_gap.py --executable DBSIM.EXE --sim sim \
        --registry tools/reference-game/proxy/hook_registry.h \
        --proxy tools/reference-game/proxy --map .../map.json \
        --verdicts tools/reference-game/scripts/sim_gap_verdicts.json \
        --output docs/reference-game/sim_gap.md --json build/function-docs/sim_gap.json
    python sim_gap.py --check ...      # same inputs; exit 1 when the document is stale

`--check` compares the regenerated document with the committed one, names the
roots that appeared and disappeared since, and fails on a stale document, on a
row VA that occurs twice, on a source construct it could not read and on a stale
or unfounded verdict (the VA has a row or hook or is no longer in the gap, the
document or heading is missing, an open root still reaches a `behind` VA).
"""
import argparse
import bisect
import hashlib
import json
import os
import posixpath
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import areas  # noqa: E402

IMAGE_BASE = 0x400000
CODE_FIRST, CODE_END = 0x401000, 0x497000   # default code window when no image is given
BASE_WRAPPER = {'es2_sim_call': 1}          # the dispatch entry: (d, va, slots, ...)

# ---------------------------------------------------------------- C text

HEX = r'0[xX][0-9a-fA-F]+[uUlL]*'
NUMBER = re.compile(r'0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*')
IDENT = re.compile(r'[A-Za-z_]\w*')
TOKEN = re.compile(r'0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*|<<|>>|[-+*/%|&^~()]')
CAST = re.compile(r'\(\s*(?:const\s+)?(?:u?int(?:8|16|32|64)_t|unsigned(?:\s+(?:int|long|short|char))?|'
                  r'int|long|short|char|size_t|uintptr_t)\s*\)')
KEYWORDS = frozenset(('if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined', 'do', 'else', 'case'))
FRAGMENT_ROW = re.compile(
    r'\bES2_SIM_SERVICE\(\s*(\w+)\s*,\s*(0[xX][0-9a-fA-F]+)[uU]?\s*,\s*(CDECL|STDCALL)\s*,\s*(\w+)\s*,'
    r'\s*(\d+)\s*,\s*(\w+)\s*\)')
REGISTRY_HOOK = re.compile(r'ES2_HOOK\(\s*(\w+)\s*,\s*(0[xX][0-9a-fA-F]+)\s*,')
BRIDGE = re.compile(r'"call_([0-9a-fA-F]{6})"')


def strip_c(text):
    """The text without comments, string contents and character literals; the newlines stay."""
    out, i, n = [], 0, len(text)
    while i < n:
        c, d = text[i], text[i:i + 2]
        if d == '//':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif d == '/*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            seg = text[i:j + 1]
            out.append('""' if c == '"' else '0')
            out.append(re.sub(r'[^\n]', '', seg))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def split_directives(text):
    """(text without preprocessor lines, [(line, directive text)]); continuation lines are joined."""
    lines, out, directives, i = text.split('\n'), [], [], 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith('#'):
            start, joined = i, line
            while joined.rstrip().endswith('\\') and i + 1 < len(lines):
                joined = joined.rstrip()[:-1] + ' ' + lines[i + 1]
                i += 1
                out.append('')
            directives.append((start + 1, joined.strip()))
            out.append('')
        else:
            out.append(line)
        i += 1
    return '\n'.join(out), directives


def balanced(text, start, opener='('):
    """Index just past the group that opens at text[start]; None when it does not close."""
    pairs = {'(': ')', '[': ']', '{': '}'}
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c in pairs:
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return None


def split_args(text):
    """Top-level comma separated pieces of an argument list (text between the parentheses)."""
    out, depth, cur = [], 0, []
    for c in text:
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
    tail = ''.join(cur).strip()
    if tail or out:
        out.append(tail)
    return out


def expand_macro(macro, args):
    """The body of a function-like macro with its parameters replaced (`#p` as a string, `##` pasted)."""
    params, body = macro['params'], macro['body']
    values = dict(zip(params, args))
    if params and params[-1] == '...':
        values['__VA_ARGS__'] = ', '.join(args[len(params) - 1:])
    names = [p for p in values if p != '...']
    if not names:
        return re.sub(r'\s*##\s*', '', body)
    pattern = re.compile(r'(##\s*)?(#\s*)?\b(' + '|'.join(map(re.escape, names)) + r')\b')

    def substitute(m):
        if m.group(2) and not m.group(1):
            return '""'
        return (m.group(1) or '') + values[m.group(3)]
    return re.sub(r'\s*##\s*', '', pattern.sub(substitute, body))


def expand_top_level(text, macros):
    """The text with every invocation of a function-like macro of `macros` outside braces expanded once.

    Service rows are often written as `ROW(name) { ... }` or as a whole `ROW(name, body)` line; without the
    expansion such bodies are no functions of their own (all `ROW` heads would share one name). The
    expansion stays on the invocation's line and the newlines of the invocation follow it, so line
    numbers do not move."""
    names = [n for n, m in macros.items() if m['params'] is not None]
    if not names:
        return text
    head = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(names, key=len, reverse=True))) + r')\s*\(')
    out, i, depth, n = [], 0, 0, len(text)
    while i < n:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif depth == 0 and (c.isalpha() or c == '_') and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = head.match(text, i)
            end = balanced(text, m.end() - 1) if m else None
            if end is not None:
                expansion = expand_macro(macros[m.group(1)], split_args(text[m.end():end - 1]))
                out.append(expansion.replace('\n', ' ') + '\n' * text.count('\n', i, end))
                i = end
                continue
        out.append(c)
        i += 1
    return ''.join(out)


def unparen(expr):
    expr = expr.strip()
    while expr.startswith('(') and expr.endswith(')') and balanced(expr, 0) == len(expr):
        expr = expr[1:-1].strip()
    return expr


def evaluate(expr, resolve):
    """Value of a constant integer expression (32-bit), or None; `resolve(name)` gives identifiers."""
    tokens = TOKEN.findall(CAST.sub(' ', expr))
    if not tokens or ''.join(tokens) != re.sub(r'\s+', '', CAST.sub(' ', expr)):
        return None
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def take():
        pos[0] += 1
        return tokens[pos[0] - 1]

    def atom():
        t = peek()
        if t is None:
            raise ValueError
        take()
        if t == '(':
            v = level(0)
            if take() != ')':
                raise ValueError
            return v
        if t == '-':
            return -atom()
        if t == '+':
            return atom()
        if t == '~':
            return ~atom()
        if NUMBER.fullmatch(t):
            return int(t.rstrip('uUlL'), 0)
        if IDENT.fullmatch(t):
            v = resolve(t)
            if v is None:
                raise ValueError
            return v
        raise ValueError

    ladder = (('|',), ('^',), ('&',), ('<<', '>>'), ('+', '-'), ('*', '/', '%'))

    def level(k):
        if k == len(ladder):
            return atom()
        v = level(k + 1)
        while peek() in ladder[k]:
            op = take()
            w = level(k + 1)
            if op == '|': v |= w
            elif op == '^': v ^= w
            elif op == '&': v &= w
            elif op == '<<': v <<= w
            elif op == '>>': v >>= w
            elif op == '+': v += w
            elif op == '-': v -= w
            elif op == '*': v *= w
            elif w == 0: raise ValueError
            elif op == '/': v = int(v / w)
            else: v = v - w * int(v / w)
        return v

    try:
        value = level(0)
        if pos[0] != len(tokens):
            return None
    except (ValueError, RecursionError):
        return None
    return value & 0xffffffff


# ---------------------------------------------------------------- one source file

class Source:
    """Definitions, constants and calls of one C file (heuristic parse of the stripped text)."""

    def __init__(self, path, name, text):
        self.path, self.name = path, name
        clean = strip_c(text)
        self.clean = clean
        self.body, directives = split_directives(clean)
        self.includes, self.consts, self.macros, self.defs = [], {}, {}, {}
        self.includes = [Path(m).name for m in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', text, re.M)]
        for line, d in directives:
            m = re.match(r'#\s*define\s+(\w+)(\(([^)]*)\))?\s*(.*)$', d)
            if m:
                if m.group(2) is None:
                    self.consts[m.group(1)] = m.group(4)
                else:
                    params = [p.strip() for p in m.group(3).split(',') if p.strip()]
                    self.macros[m.group(1)] = dict(params=params, body=m.group(4), line=line)
        self.body = expand_top_level(self.body, self.macros)
        self.lines = [0] + [m.end() for m in re.finditer('\n', self.body)]
        for m in re.finditer(r'\benum\b[^{;]*\{([^}]*)\}', self.body):
            previous = None
            for item in m.group(1).split(','):
                item = item.strip()
                if not item:
                    continue
                key, _, val = item.partition('=')
                key = key.strip()
                if not IDENT.fullmatch(key):
                    continue
                val = val.strip()
                self.consts[key] = val if val else (f'({previous}) + 1' if previous else '0')
                previous = key
        for m in re.finditer(r'\bconst\s+(?:u?int(?:8|16|32)_t|unsigned(?:\s+int)?|int)\s+(\w+)\s*=\s*([^;{]+);',
                             self.body):
            self.consts.setdefault(m.group(1), m.group(2).strip())
        self.functions = self._functions()

    def line_of(self, pos):
        return bisect.bisect_right(self.lines, pos)

    def _functions(self):
        text, n, out = self.body, len(self.body), {}
        i = head = depth = 0
        while i < n:
            c = text[i]
            if c == ';' and depth == 0:
                head = i + 1
            elif c == '{':
                end = balanced(text, i, '{')
                if end is None:
                    break
                if depth == 0:
                    prefix = text[head:i].rstrip()
                    fn = self._head(prefix)
                    if fn:
                        name, params, static = fn
                        if name in out:   # a second body of the name (#if branches, a head macro of a header)
                            name = f'{name}@{self.line_of(i)}'
                        out[name] = dict(params=params, static=static, body=text[i + 1:end - 1], at=i + 1,
                                         line=self.line_of(i))
                    head = end
                i = end
                continue
            i += 1
        return out

    @staticmethod
    def _head(prefix):
        if not prefix.endswith(')') or prefix.rstrip().endswith('='):
            return None
        start = None
        depth = 0
        for k in range(len(prefix) - 1, -1, -1):
            if prefix[k] == ')':
                depth += 1
            elif prefix[k] == '(':
                depth -= 1
                if depth == 0:
                    start = k
                    break
        if start is None:
            return None
        m = re.search(r'([A-Za-z_]\w*)\s*$', prefix[:start])
        if not m or m.group(1) in KEYWORDS:
            return None
        params = []
        for p in split_args(prefix[start + 1:-1]):
            if p in ('', 'void', '...'):
                continue
            p = re.sub(r'\[[^\]]*\]', '', p)
            ids = IDENT.findall(p)
            if ids:
                params.append(ids[-1])
        return m.group(1), params, bool(re.search(r'\bstatic\b', prefix[:start]))

    def calls(self, text, base=0):
        """[(callee, [argument texts], position)] for every `name(` in the text, nested calls included."""
        out = []
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', text):
            name = m.group(1)
            if name in KEYWORDS:
                continue
            end = balanced(text, m.end() - 1)
            if end is None:
                continue
            out.append((name, split_args(text[m.end():end - 1]), base + m.start()))
        return out


# ---------------------------------------------------------------- the scan

def read_sources(sim_dir):
    """Every C file whose calls reach the dispatch: sim/src and the headers it includes."""
    sim = Path(sim_dir)
    paths = sorted(sim.glob('src/*.c')) + sorted(sim.glob('src/*.h')) + sorted(sim.glob('include/es2/sim/*.h'))
    return {p.name: Source(p, p.name, p.read_text(encoding='utf-8', errors='replace')) for p in paths}


def read_rows(sim_dir):
    """Service rows: dicts name, va, abi, slots, function, fragment, line (fragments in file-name order)."""
    rows = []
    for path in sorted(Path(sim_dir, 'dispatch', 'services').glob('*.h')):
        text = strip_c(path.read_text(encoding='utf-8', errors='replace'))
        for m in FRAGMENT_ROW.finditer(text):
            rows.append(dict(name=m.group(1), va=int(m.group(2), 16), abi=m.group(3), slots=int(m.group(5)),
                             function=m.group(6), fragment=path.name,
                             line=text.count('\n', 0, m.start()) + 1))
    return rows


def read_hooks(registry):
    return {IMAGE_BASE + int(m.group(2), 16): m.group(1)
            for m in REGISTRY_HOOK.finditer(Path(registry).read_text(encoding='utf-8-sig'))}


def read_bridges(proxy_dir):
    """VAs that hook adapters call through the trap: the `"call_<va>"` counters of proxy/*.h."""
    vas = {}
    for path in sorted(Path(proxy_dir).glob('*.h')):
        for m in BRIDGE.finditer(path.read_text(encoding='utf-8', errors='replace')):
            vas.setdefault(int(m.group(1), 16), set()).add(path.name)
    return vas


def indirect_call_site(image, va):
    """Whether the code bytes at `va` are an indirect near call (`FF /2`, `call r/m32`).

    A `"call_<va>"` counter normally names the callee; for a call through a
    pointer (an object's slot, an event port) the adapter keys it by the address
    of the call instruction instead (sim_leaves_hooks.h: `call_420a8d`,
    `call_420acb`). Such a key is no function: the dispatch sees the pointer's
    target, not the key, so it is no root. Only keys that are no function start
    of the call graph are tested (see `split_bridges`).
    """
    if image is None:
        return False
    i = va - image.code_va
    if i < 0 or i + 2 > len(image.code):
        return False
    return image.code[i] == 0xFF and (image.code[i + 1] >> 3) & 7 == 2


def split_bridges(bridges, starts, image):
    """(callee bridges, call-site bridges): a key that is no function start and whose bytes are `FF /2` is the
    call site of an indirect call (`indirect_call_site`), not a callee."""
    callees, sites = {}, {}
    for va, files in bridges.items():
        (sites if va not in starts and indirect_call_site(image, va) else callees)[va] = files
    return callees, sites


class Scan:
    """Wrappers, call sites and code-address mentions of the sources."""

    def __init__(self, sources, in_code, in_image=None):
        self.sources, self.in_code = sources, in_code
        self.in_image = in_image or (lambda v: False)
        self.closure = {n: self._closure(n) for n in sources}
        self._memo = {}
        self.wrappers = {}    # (file, name) -> slot index; file None = global
        self.global_wrappers = dict(BASE_WRAPPER)
        self.local = {n: set(s.functions) | set(s.macros) for n, s in sources.items()}
        self.definitions = self._definitions()
        self._fixpoint()
        self.sites, self.dynamic, self.dynamic_sites, self.warnings = [], {}, [], []
        self._collect()
        self.mentions, self.data_mentions = self._mentions()

    def _closure(self, name):
        seen, stack = [], [name]
        while stack:
            n = stack.pop()
            if n in seen or n not in self.sources:
                continue
            seen.append(n)
            stack.extend(self.sources[n].includes)
        return seen   # the file first, then the headers it includes (transitively)

    def resolve(self, file, name):
        key = (file, name)
        if key in self._memo:
            return self._memo[key]
        self._memo[key] = None   # cycle guard
        value = None
        for n in self.closure[file]:
            src = self.sources[n]
            if name in src.consts:
                value = evaluate(src.consts[name], lambda x, f=file: self.resolve(f, x))
                break
        self._memo[key] = value
        return value

    def _definitions(self):
        """(file, name, kind, params, calls, line) of every function and macro body."""
        out = []
        for file, src in self.sources.items():
            for name, fn in src.functions.items():
                out.append(dict(file=file, name=name, kind='function', params=fn['params'], static=fn['static'],
                                calls=src.calls(fn['body'], fn['at']), at=fn['at']))
            for name, mac in src.macros.items():
                out.append(dict(file=file, name=name, kind='macro', params=mac['params'], static=True,
                                calls=src.calls(mac['body']), at=None, line=mac['line']))
        return out

    def wrapper_slot(self, file, name):
        """VA slot of a wrapper as seen from `file`, else None. Local definitions shadow global ones."""
        for n in self.closure[file]:
            if name in self.local[n]:
                return self.wrappers.get((n, name))
        return self.global_wrappers.get(name)

    def _fixpoint(self):
        changed = True
        while changed:
            changed = False
            for d in self.definitions:
                key = (d['file'], d['name'])
                if key in self.wrappers:
                    continue
                for callee, args, _ in d['calls']:
                    slot = self.wrapper_slot(d['file'], callee)
                    if slot is None or slot >= len(args):
                        continue
                    arg = unparen(args[slot])
                    if arg in d['params']:
                        self.wrappers[key] = d['params'].index(arg)
                        if d['kind'] == 'function' and not d['static']:
                            self.global_wrappers.setdefault(d['name'], d['params'].index(arg))
                        changed = True
                        break

    def _collect(self):
        for d in self.definitions:
            file, src = d['file'], self.sources[d['file']]
            if d['kind'] != 'function':
                continue
            for callee, args, at in d['calls']:
                slot = self.wrapper_slot(file, callee)
                if slot is None:
                    continue
                if slot >= len(args):
                    self.warnings.append(f'{file}:{src.line_of(at)}: {callee} has no argument {slot}')
                    continue
                value = evaluate(unparen(args[slot]), lambda x, f=file: self.resolve(f, x))
                if value is None:
                    expr = unparen(args[slot])
                    if expr in d['params'] and (file, d['name']) in self.wrappers:
                        continue   # a wrapper passing its own parameter on
                    self.dynamic[file] = self.dynamic.get(file, 0) + 1
                    self.dynamic_sites.append((file, src.line_of(at), callee, expr))
                    # A code address inside the expression (`down ? A : B`) is a possible target.
                    for t in TOKEN.findall(expr):
                        v = int(t.rstrip('uUlL'), 0) if NUMBER.fullmatch(t) else (
                            self.resolve(file, t) if IDENT.fullmatch(t) and t not in KEYWORDS else None)
                        if v is not None and self.in_code(v):
                            self.sites.append(dict(file=file, line=src.line_of(at), caller=d['name'],
                                                   wrapper=callee, va=v, conditional=True))
                    continue
                self.sites.append(dict(file=file, line=src.line_of(at), caller=d['name'], wrapper=callee, va=value))

    def _mentions(self):
        """(code, data): VA -> files for the addresses that a literal or a resolvable constant names,
        inside the code section and elsewhere in the image."""
        code, data = {}, {}
        for file, src in self.sources.items():
            for m in TOKEN.finditer(src.clean):
                t = m.group(0)
                if NUMBER.fullmatch(t):
                    v = int(t.rstrip('uUlL'), 0)
                elif IDENT.fullmatch(t) and t not in KEYWORDS:
                    v = self.resolve(file, t)
                else:
                    continue
                if v is None:
                    continue
                if self.in_code(v):
                    code.setdefault(v, set()).add(file)
                elif self.in_image(v):
                    data.setdefault(v, set()).add(file)
        return code, data


# ---------------------------------------------------------------- the gap

def graph(cfg):
    """VA -> (instructions, callees (calls and tail calls), indirect calls) of a dbsim_map.summarize result."""
    out = {}
    for va, v in cfg.items():
        out[va] = (v['instructions'], sorted({int(c, 16) for c in v['calls'] + v['tail_calls']}),
                   v.get('indirect_calls', 0))
    return out


def read_tables(image, vas, starts):
    """{table VA: [slot targets]} for the image addresses `vas` that begin a run of base-relocated dwords
    pointing at function starts (a vtable, a table of callbacks, a global holding one callback)."""
    out = {}
    for va in sorted(vas):
        run, at = [], va
        while at in image.relocations:
            target = image.dword(at)
            if target is None or target not in starts:
                break
            run.append(target)
            at += 4
        if run:
            out[va] = run
    return out


def reach_of(g, covered, root):
    """Functions reachable from `root` through the call graph, stopping at rows and hooks (root included)."""
    seen, stack = set(), [root]
    while stack:
        v = stack.pop()
        if v in seen or (v in covered and v != root):
            continue
        seen.add(v)
        stack.extend(g.get(v, (0, (), 0))[1])
    return seen


def compute(scan, rows, hooks, bridges, cfg, tables=None, call_sites=None):
    """The gap model (plain values) from a scan, the rows, the hooks, the bridged VAs, the call graph and
    the function-pointer tables that sim/src names. `call_sites` are bridge keys that are call sites of
    indirect calls (`split_bridges`): listed, never roots."""
    g = graph(cfg)
    covered = {r['va'] for r in rows} | set(hooks)
    site_vas = {}
    for s in scan.sites:
        e = site_vas.setdefault(s['va'], dict(sites=0, files={}))
        e['sites'] += 1
        e['files'][s['file']] = e['files'].get(s['file'], 0) + 1
    roots = {va: dict(via='call', sites=e['sites'], files=e['files'])
             for va, e in site_vas.items() if va not in covered}
    call_sites = call_sites or {}
    for va, files in bridges.items():
        if va in covered or va in call_sites:
            continue
        if va in roots:
            roots[va]['via'] = 'call+bridge'
        else:
            roots[va] = dict(via='bridge', sites=0, files={})
        roots[va]['bridge'] = sorted(files)
    tables = tables or {}
    table_info = {tva: dict(targets=run, files=sorted(scan.data_mentions.get(tva, ())))
                  for tva, run in tables.items()}
    virtual = {}
    for tva, t in table_info.items():
        for slot, target in enumerate(t['targets']):
            if target in covered or target in roots:
                continue
            e = virtual.setdefault(target, dict(tables=[], files=set()))
            e['tables'].append([tva, slot])
            e['files'].update(t['files'])
    virtual = {va: dict(tables=e['tables'], files=sorted(e['files'])) for va, e in virtual.items()}

    nodes = {}

    def expand(start_set, kinds):
        queue = []
        for va in sorted(start_set):
            if va not in nodes:
                nodes[va] = dict(kind=kinds[0], depth=0, root=va)
                queue.append(va)
        k = 0
        while k < len(queue):
            va = queue[k]
            k += 1
            for c in g.get(va, (0, (), 0))[1]:
                if c in covered or c in nodes:
                    continue
                nodes[c] = dict(kind=kinds[1], depth=nodes[va]['depth'] + 1, root=nodes[va]['root'])
                queue.append(c)

    expand(set(roots), ('called', 'behind'))
    expand(set(virtual), ('virtual', 'vbehind'))
    for va, n in nodes.items():
        n['instructions'] = g.get(va, (0, (), 0))[0]
        n['indirect'] = g.get(va, (0, (), 0))[2]
        n['known'] = va in g
    reach = {r: sorted(reach_of(g, covered, r)) for r in list(roots) + list(virtual)}
    referenced = {va: sorted(files) for va, files in scan.mentions.items()
                  if va not in covered and va not in nodes}
    return dict(roots=roots, virtual=virtual, tables=table_info, nodes=nodes, reach=reach, referenced=referenced,
                instructions={va: g[va][0] for va in g}, hooks=sorted(hooks), starts=sorted(g),
                call_sites={va: sorted(files) for va, files in call_sites.items() if va not in covered})


# ---------------------------------------------------------------- verdicts

VERDICT_KINDS = ('dead', 'caller-replaced', 'behind', 'served')
DOCS_DIR = 'docs/reference-game'   # where sim_gap.md lives: the verdict links are relative to it


def read_verdicts(path):
    """({VA: dict(kind, doc, reason)}, [problems]) of the verdict list (`sim_gap_verdicts.json`)."""
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    out, problems = {}, []
    for k, entry in enumerate(data.get('verdicts', ())):
        kind, doc = entry.get('kind'), entry.get('doc', '')
        if kind not in VERDICT_KINDS:
            problems.append(f'verdict {k}: kind {kind!r} is none of {", ".join(VERDICT_KINDS)}')
        if '#' not in doc:
            problems.append(f'verdict {k}: doc {doc!r} has no anchor')
        for text in entry.get('vas', ()):
            va = int(text, 16)
            if va in out:
                problems.append(f'verdict VA {va:x} twice')
            out[va] = dict(kind=kind, doc=doc, reason=entry.get('reason', ''))
    return out, problems


def slugs(text):
    """The anchors of the Markdown headings of `text` (GitHub's rule, `-1`, `-2` for repeats)."""
    out, seen = set(), {}
    for m in re.finditer(r'^#{1,6}\s+(.*?)\s*#*\s*$', text, re.M):
        slug = re.sub(r'[^\w\- ]', '', m.group(1).lower()).replace(' ', '-')
        n = seen.get(slug, 0)
        seen[slug] = n + 1
        out.add(slug if n == 0 else f'{slug}-{n}')
    return out


def verdict_problems(verdicts, model, rows, hooks, root='.'):
    """Stale or unfounded verdicts: a VA with a row or hook, a VA no longer in the gap, a missing document or
    anchor, a `behind` verdict that an open root (one without verdict) still reaches."""
    problems, covered = [], {r['va'] for r in rows} | set(hooks)
    nodes, docs = model['nodes'], {}
    for va, v in sorted(verdicts.items()):
        if va in covered:
            problems.append(f'stale verdict {va:x}: it has a {"row" if va not in hooks else "hook"} now')
        elif va not in nodes:
            problems.append(f'stale verdict {va:x}: no longer in the gap')
        path, _, anchor = v['doc'].partition('#')
        if path not in docs:
            p = Path(root, path)
            docs[path] = slugs(p.read_text(encoding='utf-8')) if p.is_file() else None
        if docs[path] is None:
            problems.append(f'verdict {va:x}: {path} missing')
        elif anchor not in docs[path]:
            problems.append(f'verdict {va:x}: {path} has no heading #{anchor}')
    starts = list(model['roots']) + list(model['virtual'])
    for va, v in sorted(verdicts.items()):
        if v['kind'] != 'behind' or va not in nodes:
            continue
        open_roots = sorted(r for r in starts if r != va and r not in verdicts and va in model['reach'][r])
        if open_roots:
            problems.append(f'verdict {va:x} (behind): reached by the open root(s) '
                            + ', '.join(f'{r:x}' for r in open_roots))
    return problems


def verdict_link(v):
    path, _, anchor = v['doc'].partition('#')
    rel = posixpath.relpath(path, DOCS_DIR)
    return f'[{v["kind"]}]({rel}#{anchor})'


# ---------------------------------------------------------------- document

AREA_NAMES = list(areas.AREAS) + ['(none)']


def area_label(va):
    return areas.area_of(va) or '(none)'


def area_key(va):
    return (AREA_NAMES.index(area_label(va)), va)


def read_live(path):
    """Live information of a map.json: suites with a nonzero entry count per live unreplaced VA, unprobed VAs."""
    data = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    rows = {int(r['va'], 16): r.get('suites', 0) for r in data.get('rows', ())}
    unprobed = {int(v, 16) for v in data.get('unprobed', {})}
    total = len(data.get('summary', {}).get('suites', ()))
    return dict(rows=rows, unprobed=unprobed, total=total, name=f'{Path(path).parent.name}/{Path(path).name}')


def live_text(va, live):
    if live is None:
        return ''
    if va in live['rows']:
        return f"{live['rows'][va]}/{live['total']}"
    return 'unprobed' if va in live['unprobed'] else '0'


def md(text):
    return str(text).replace('|', '\\|')


def call_sites_text(sites):
    """The Sources line's tail for the bridge keys that are call sites of indirect calls."""
    if not sites:
        return '.'
    return (f'; {len(sites)} of them are no function but the address of an indirect call instruction '
            '(`FF /2`) that an adapter uses as the key of a call through a pointer, whose target the dispatch '
            'sees instead, so they are no roots: '
            + ', '.join(f'`{va:06x}` ({md(", ".join(files))})' for va, files in sorted(sites.items())) + '.')


def render(model, scan, rows, hooks, info, live):
    """The Markdown document, deterministic."""
    roots, virtual, nodes, instr = model['roots'], model['virtual'], model['nodes'], model['instructions']
    verdicts = info.get('verdicts') or {}

    def verdict(va):
        return verdict_link(verdicts[va]) if va in verdicts else 'open'

    def total(vas):
        return sum(instr.get(v, 0) for v in vas)

    def n_live(vas):
        return sum(1 for v in vas if v in live['rows']) if live else ''

    kinds = {k: [v for v, n in nodes.items() if n['kind'] == k] for k in ('called', 'behind', 'virtual', 'vbehind')}
    per_area = {a: dict(called=[], behind=[], virtual=[], referenced=[]) for a in AREA_NAMES}
    for v in kinds['called']:
        per_area[area_label(v)]['called'].append(v)
    for v in kinds['behind']:
        per_area[area_label(v)]['behind'].append(v)
    for v in kinds['virtual'] + kinds['vbehind']:
        per_area[area_label(v)]['virtual'].append(v)
    for v in model['referenced']:
        per_area[area_label(v)]['referenced'].append(v)

    out = ["# The own process's gap list", '',
           'Generated by `tools/reference-game/scripts/sim_gap.py`; do not edit '
           '(regeneration: [docs/functions/README.md](../functions/README.md#the-sim-gap-list)). '
           'The original functions that the own DBSIM (`sim/`) still reaches without a service row or a hook: '
           'the dispatch refuses them and the run stops there. Counts are facts of the sources named below; '
           'the roles of the functions are not claimed here (see [the function index](../functions/index.md)).',
           '', '## Sources and method', '',
           f'- Executable: `VER95/DBSIM.EXE`. Call graph: {len(model["starts"])} '
           'function starts of the synchronized descent (`dbsim_map.py` over `probe_instruction_inventory.py`): '
           'direct calls, tail calls and jump tables are followed, calls through pointers are not; an '
           "instruction count is the descent's per function (a tail-called start is not included).",
           f'- Service rows: {len(rows)} in {len({r["fragment"] for r in rows})} fragments of '
           f'`sim/dispatch/services/`; hooks: {len(hooks)} (`hook_registry.h`, RVA + image base).',
           f'- Guest-call sites of `sim/src`: {len(scan.sites)} with a constant VA '
           f'({len({s["va"] for s in scan.sites})} distinct VAs; `A ? B : C` arguments count each constant) '
           f'in {len({s["file"] for s in scan.sites})} files, {sum(scan.dynamic.values())} dynamic (the VA is '
           f'read from an object or a variable). {len(scan.wrappers) + len(BASE_WRAPPER)} wrappers found: '
           'functions and macros that pass a parameter to the VA slot of `es2_sim_call` or of another wrapper '
           '(fixpoint over all files).',
           f'- Bridged calls of the hook adapters: {len(info["bridges"])} distinct VAs '
           '(`"call_<va>"` counters of `tools/reference-game/proxy/*.h`)'
           + call_sites_text(model.get('call_sites', {})),
           f'- Function-pointer tables: {len(model["tables"])} addresses that `sim/src` names which begin a run '
           'of base-relocated code pointers in the image (vtables, callback slots).']
    if live:
        out.append(f'- Live column: `{live["name"]}` (number of the {live["total"]} suites with a nonzero entry '
                   'count; the map lists only live, unreplaced functions; `0` = probed and never entered).')
    out += ['',
            '**Called**: a VA that own code passes to the dispatch (or a hook adapter bridges) with neither a '
            'row nor a hook; when that path runs the run stops. **Behind**: original functions that the called '
            'ones reach in the call graph, again without row or hook (breadth-first, stopping at rows and '
            'hooks): what an own replacement of the called function may still have to call, an upper bound. '
            '**Virtual**: targets of the function-pointer tables that `sim/src` names (own code installs the '
            'table and calls its slots through an object, which no call site shows), with their own behind; '
            'a target only matters when a slot is called, which a reading of the use decides. '
            '**Referenced only**: code addresses that `sim/src` names but never passes to a call (tables, '
            'comparisons, variables, return-address constants).', '',
            '## Per area', '',
            '| Area | Called | Instr. | Live | Behind | Instr. | Live | Virtual | Instr. | Live | Referenced only |',
            '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    sums = [0] * 10
    for a in AREA_NAMES:
        r = per_area[a]
        if not any(r.values()):
            continue
        cells = [len(r['called']), total(r['called']), n_live(r['called']), len(r['behind']), total(r['behind']),
                 n_live(r['behind']), len(r['virtual']), total(r['virtual']), n_live(r['virtual']),
                 len(r['referenced'])]
        sums = [x + (y or 0) for x, y in zip(sums, cells)]
        out.append(f'| {a} | ' + ' | '.join(str(c) for c in cells) + ' |')
    out.append('| **all** | ' + ' | '.join(str(c) if live or i not in (2, 5, 8) else '' for i, c in enumerate(sums))
               + ' |')
    out += ['', 'An area is the module-map range of `areas.py` that contains the address (the table of '
            "[dbsim_gap.md](dbsim_gap.md#1-method)); it names the source module, not the function's role.", '']

    out += ['## Verdicts', '',
            'A VA of the gap keeps no row when a document gives the reason; the verdict list '
            '`tools/reference-game/scripts/sim_gap_verdicts.json` names the VA, the kind and the section. '
            '**dead**: no own path makes the call (the slot is never called on such an object, the branch '
            'is never taken); **caller-replaced**: the callers are own code that does not make the call; '
            '**behind**: reached only through other refused VAs; **served**: own code serves the effect '
            'elsewhere (another row, the process record). A verdict is a reading of the sources, not a '
            'run; the dispatch still refuses the VA. **Open**: neither row, hook nor verdict.', '',
            '| Kind | Gap | ' + ' | '.join(VERDICT_KINDS) + ' | Open |',
            '|---|---:|' + '---:|' * (len(VERDICT_KINDS) + 1)]
    open_vas = {}
    for k, label in (('called', 'called'), ('behind', 'behind'), ('virtual', 'virtual'),
                     ('vbehind', 'behind virtual')):
        vas = kinds[k]
        per = [sum(1 for v in vas if v in verdicts and verdicts[v]['kind'] == x) for x in VERDICT_KINDS]
        open_vas[label] = sorted(v for v in vas if v not in verdicts)
        out.append(f'| {label} | {len(vas)} | ' + ' | '.join(map(str, per)) + f' | {len(open_vas[label])} |')
    out += ['', 'Open: ' + ('; '.join(f'{label} ' + ', '.join(f'`{v:06x}`' for v in vas)
                                      for label, vas in open_vas.items() if vas) or 'none') + '.', '']

    out += ['## Called by own code (the roots)', '',
            'Sorted by area, then address. **Via**: `call` = a guest-call site of `sim/src`, `bridge` = a hook '
            'adapter calls it through the trap. **Sites**: call sites with a constant VA (a conditional '
            'argument counts each constant). **Files**: the `sim/src` files with their site counts (and the '
            'adapter headers). **Reach**: functions / instructions of the original code reachable from it '
            'without crossing a row or hook (itself included). **Indirect**: calls through pointers in the '
            'function, not followed.', '',
            '<!-- gap-roots:begin -->',
            '| VA | Area | Via | Instr. | Sites | Files | Reach | Indirect | Live | Verdict |',
            '|---|---|---|---:|---:|---|---|---:|---|---|']
    for va in sorted(roots, key=area_key):
        r, n = roots[va], nodes[va]
        files = ', '.join(f'{f} ({c})' for f, c in sorted(r['files'].items()))
        if 'bridge' in r:
            files = (files + ', ' if files else '') + ', '.join(r['bridge'])
        reach = model['reach'][va]
        out.append(f'| {va:06x} | {area_label(va)} | {r["via"]} | {instr[va] if n["known"] else "?"} | '
                   f'{r["sites"]} | {md(files)} | {len(reach)} / {total(reach)} | '
                   f'{n["indirect"] if n["known"] else ""} | {live_text(va, live)} | {verdict(va)} |')
    out += ['<!-- gap-roots:end -->', '']

    def behind_section(title, intro, kind, key):
        out.extend([title, '', intro, ''])
        any_rows = False
        for a in AREA_NAMES:
            vas = sorted(v for v in per_area[a][key] if nodes[v]['kind'] == kind)
            if not vas:
                continue
            any_rows = True
            out.extend([f'### {a}', '', '| VA | Instr. | Root (depth) | Live | Verdict |', '|---|---:|---|---|---|'])
            for va in vas:
                n = nodes[va]
                out.append(f'| {va:06x} | {n["instructions"] if n["known"] else "?"} | '
                           f'{n["root"]:06x} ({n["depth"]}) | {live_text(va, live)} | {verdict(va)} |')
            out.append('')
        if not any_rows:
            out.extend(['None.', ''])

    behind_section('## Behind the called roots, per area',
                   'Original functions without row or hook that a called root reaches. Each appears once; '
                   '**Root (depth)** is the called root that reaches it by the shortest chain and the length of '
                   'that chain.', 'behind', 'behind')

    out += ['## Virtual: targets of the function-pointer tables that sim/src names', '',
            'Tables first (**Slots** = the run of code pointers at the address, **Open** = slots whose target '
            'has neither row nor hook, **Files** = where `sim/src` names the address), then the open targets '
            'per area (**Table (slot)**: the first table and slot that lists it).', '']
    open_tables = {t: i for t, i in model['tables'].items()
                   if any(x in virtual or x in roots for x in i['targets'])}
    if open_tables:
        out += ['| Table | Slots | Open | Files |', '|---|---:|---:|---|']
        for tva in sorted(open_tables):
            t = open_tables[tva]
            n_open = sum(1 for x in t['targets'] if x in virtual or x in roots)
            out.append(f'| {tva:06x} | {len(t["targets"])} | {n_open} | {md(", ".join(t["files"]))} |')
        out.append('')
    else:
        out += ['None.', '']
    started = False
    for a in AREA_NAMES:
        vas = sorted(v for v in per_area[a]['virtual'] if nodes[v]['kind'] == 'virtual')
        if not vas:
            continue
        started = True
        out += [f'### {a}', '', '| VA | Instr. | Table (slot) | Reach | Live | Verdict |', '|---|---:|---|---|---|---|']
        for va in vas:
            n, v = nodes[va], virtual[va]
            tva, slot = v['tables'][0]
            more = f' +{len(v["tables"]) - 1}' if len(v['tables']) > 1 else ''
            reach = model['reach'][va]
            out.append(f'| {va:06x} | {n["instructions"] if n["known"] else "?"} | {tva:06x} ({slot}){more} | '
                       f'{len(reach)} / {total(reach)} | {live_text(va, live)} | {verdict(va)} |')
        out.append('')
    if not started and open_tables:
        out += ['Every open slot target is also behind a called root.', '']
    behind_section('### Behind the virtual targets',
                   'Original functions without row or hook that a virtual target reaches and that no called '
                   'root reaches.', 'vbehind', 'virtual')

    ref = sorted(model['referenced'])
    starts = [v for v in ref if v in instr]
    others = [v for v in ref if v not in instr]
    out += ['## Referenced only', '',
            'Code addresses that `sim/src` names (a literal or a constant that resolves into the code '
            'section) without passing them to a call, with neither a row nor a hook. Function starts first; '
            'the others are no start of the call graph (data in the code section such as vtables and tables, '
            'mid-function addresses, return-address constants).', '']
    if starts:
        out += ['| VA | Area | Instr. | Files | Live |', '|---|---|---:|---|---|']
        for va in sorted(starts, key=area_key):
            out.append(f'| {va:06x} | {area_label(va)} | {instr[va]} | '
                       f'{md(", ".join(model["referenced"][va]))} | {live_text(va, live)} |')
        out.append('')
    by_file = {}
    for va in others:
        by_file.setdefault(model['referenced'][va][0], []).append(va)
    out.append(f'Not function starts: {len(others)}'
               + (' (by the first file that names them): ' + '; '.join(
                   f'{f}: ' + ' '.join(f'{v:x}' for v in vas) for f, vas in sorted(by_file.items())) if others else '')
               + '.')
    out.append('')

    called_vas = {s['va'] for s in scan.sites}
    odd = sorted(v for v in called_vas if v not in instr and v not in model['hooks'])
    notstart = sorted(r['va'] for r in rows if r['va'] not in instr)
    dyn = sorted(scan.dynamic.items())
    out += ['## Anomalies', '',
            f'- Call-site VAs that are no function start of the call graph (and no hook): {len(odd)}'
            + (': ' + ', '.join(f'`{v:x}`' for v in odd) if odd else '') + '.',
            f'- Service-row VAs that are no function start of the call graph: {len(notstart)}'
            + (': ' + ', '.join(f'`{v:x}`' for v in notstart) if notstart else '') + '.',
            '- Dynamic guest-call sites (the VA is not a constant) per file: '
            + (', '.join(f'`{f}` {n}' for f, n in dyn) if dyn else 'none') + '.',
            f'- Source constructs the scanner could not read: {len(scan.warnings)}'
            + (': ' + '; '.join(scan.warnings) if scan.warnings else '') + '.', '']
    return '\n'.join(out)


# ---------------------------------------------------------------- driver

def build(args, cfg=None, image=None):
    """(model, scan, rows, hooks, document text, live) for the arguments."""
    rows = read_rows(args.sim)
    hooks = read_hooks(args.registry)
    bridges = read_bridges(args.proxy)
    sources = read_sources(args.sim)
    if cfg is None:
        import dbsim_map
        starts, image, code, tables = dbsim_map.starts_and_code(Path(args.executable), Path(args.objdump))
        cfg = dbsim_map.summarize(image, code, tables, starts)
    if image is None:
        lo, hi, top = CODE_FIRST, CODE_END, CODE_END
    else:
        lo = image.code_va
        hi = lo + len(image.code)
        top = max(start + size for start, size, _, _, _ in image.sections)
    scan = Scan(sources, lambda v: lo <= v < hi, lambda v: IMAGE_BASE <= v < top)
    tables = read_tables(image, scan.data_mentions, set(cfg)) if image is not None else {}
    _, call_sites = split_bridges(bridges, set(cfg), image)
    model = compute(scan, rows, hooks, bridges, cfg, tables, call_sites)
    verdicts, problems = read_verdicts(args.verdicts) if getattr(args, 'verdicts', None) else ({}, [])
    model['verdicts'] = verdicts
    model['verdict_problems'] = problems + verdict_problems(verdicts, model, rows, hooks,
                                                            getattr(args, 'root', None) or '.')
    live = read_live(args.map) if getattr(args, 'map', None) else None
    sha = hashlib.sha256(Path(args.executable).read_bytes()).hexdigest() if args.executable else ''
    text = render(model, scan, rows, hooks, dict(sha=sha, bridges=bridges, verdicts=verdicts), live)
    return model, scan, rows, hooks, text, live


def sim_info(model, scan, rows):
    """What function_index.py reads: per VA the row and the gap kind."""
    functions = {}
    for file, src in scan.sources.items():
        if file.endswith('.c'):
            for name in src.functions:
                functions.setdefault(name, file)
    return dict(
        rows={f'{r["va"]:x}': dict(name=r['name'], fragment=r['fragment'], source=functions.get(r['function'], ''))
              for r in rows},
        gap={f'{v:x}': dict(kind=n['kind'], root=f'{n["root"]:x}', depth=n['depth'],
                            **({'verdict': model['verdicts'][v]['kind']} if v in model.get('verdicts', {}) else {}))
             for v, n in model['nodes'].items()},
        referenced=[f'{v:x}' for v in sorted(model['referenced'])])


def committed_roots(path):
    """VAs of the roots table of a committed document (between its markers)."""
    text = Path(path).read_text(encoding='utf-8')
    m = re.search(r'<!-- gap-roots:begin -->(.*?)<!-- gap-roots:end -->', text, re.S)
    return {int(x, 16) for x in re.findall(r'^\| ([0-9a-f]{6}) \|', m.group(1), re.M)} if m else set()


def check(model, scan, rows, text, output):
    """Problems (list of strings) of the sources and of the committed document."""
    problems = list(scan.warnings) + list(model.get('verdict_problems', ()))
    path = Path(output)
    now = set(model['roots'])
    if not path.is_file():
        problems.append(f'{output}: missing')
    elif path.read_bytes() != text.encode('utf-8'):
        was = committed_roots(path)
        new, closed = sorted(now - was), sorted(was - now)
        problems.append(f'{output} is stale (called roots now {len(now)}, in the document {len(was)}; '
                        f'new: {", ".join(f"{v:x}" for v in new) or "none"}; '
                        f'closed: {", ".join(f"{v:x}" for v in closed) or "none"}); '
                        'regenerate with tools/reference-game/scripts/regenerate_function_docs.ps1')
    seen = {}
    for r in rows:
        seen.setdefault(r['va'], []).append(r['name'])
    problems += [f'service-row VA {va:#x} twice: {", ".join(n)}' for va, n in sorted(seen.items()) if len(n) > 1]
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--executable', required=True, help='DBSIM.EXE (reference file)')
    ap.add_argument('--objdump', default=str(Path(os.environ.get('ES2_DEPENDENCIES') or Path(sys.executable).resolve().parents[1]) / 'llvm-mingw/bin/llvm-objdump.exe'))
    ap.add_argument('--sim', default='sim', help='the sim/ directory')
    ap.add_argument('--registry', default='tools/reference-game/proxy/hook_registry.h')
    ap.add_argument('--proxy', default='tools/reference-game/proxy', help='directory of the adapter headers')
    ap.add_argument('--map', help='map.json of a DBSIM map run (adds the Live column)')
    ap.add_argument('--verdicts', default=str(HERE / 'sim_gap_verdicts.json'),
                    help='the verdict list (VA, kind, document section); "" for none')
    ap.add_argument('--root', default='.', help='repository root: the verdicts name their documents relative to it')
    ap.add_argument('--output', default='docs/reference-game/sim_gap.md')
    ap.add_argument('--json', help='also write the per-VA status that function_index.py reads')
    ap.add_argument('--check', action='store_true', help='write nothing; exit 1 when the document is stale')
    args = ap.parse_args(argv)
    model, scan, rows, hooks, text, live = build(args)
    kinds, opened = {}, {}
    for va, n in model['nodes'].items():
        kinds[n['kind']] = kinds.get(n['kind'], 0) + 1
        if va not in model['verdicts']:
            opened[n['kind']] = opened.get(n['kind'], 0) + 1
    summary = (f'{len(rows)} rows, {len(hooks)} hooks, {len(scan.sites)} call sites '
               f'({sum(scan.dynamic.values())} dynamic): ' + ', '.join(
                   f'{label} {kinds.get(k, 0)} (open {opened.get(k, 0)})' for k, label in (
                       ('called', 'called'), ('behind', 'behind'), ('virtual', 'virtual'),
                       ('vbehind', 'behind virtual')))
               + f', referenced only {len(model["referenced"])}')
    if args.check:
        problems = check(model, scan, rows, text, args.output)
        print(summary)
        for p in problems:
            print('FAIL:', p)
        if not problems:
            print(f'PASS: {args.output} is current')
        return 1 if problems else 0
    Path(args.output).write_bytes(text.encode('utf-8'))
    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_bytes((json.dumps(sim_info(model, scan, rows), indent=0, sort_keys=True)
                                     + '\n').encode('utf-8'))
    print(summary)
    return 0


if __name__ == '__main__':
    sys.exit(main())
