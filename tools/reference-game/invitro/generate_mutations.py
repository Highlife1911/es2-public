# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Operator-driven mutation runs for the in-vitro differential harness.

usage: generate_mutations.py [--list | --report] [--all] [--files F ...]
                             [--group G ...] [--functions N ...] [--ops OP ...]
                             [--per-function N] [--limit N] [--seed S] ...

Mutants are derived from our own core sources (core/src/*.c), never from a
hand-written list: every function whose definition is preceded by a
`/* Original: DBSIM.EXE 0x... */` marker gets the operators below applied to
its body, one change per mutant.

  const     an integer literal +1 and -1 (image VAs 400000h..6D8000h are
            skipped unless --addresses; array dimensions of declarations too)
  bitflip   a mask literal next to &, |, ^ with its lowest or highest set bit
            flipped; the low bit of a returned integer, of a stored value
            (W8/W16/W32, fx_w*, ->write*, VW/VD) and of an assigned value
  shift     the literal width of <<, >>, <<=, >>= or of a sar/shr/shl call
            +1 and -1
  cmp       < <=, > >=, == != swapped
  drop      a store statement (store call, or an assignment to anything but a
            plain local variable) replaced by `(void)0;`

Mapping: the marker address is matched with the registry's RVAs (VA - 400000h)
to find the hook and its adapter group. A marked helper that is not a hook
gets the groups of its callers in core (reverse call graph, nearest first)
and of the adapter headers (proxy/*_hooks.h) that name it. Each mutant is
built into the real `:differential_harness` target of a scratch copy of the
workspace and run with `--quick --group <g>` for its groups only (direct
hook groups first, at most --max-groups), stopping at the first failure.
--unmarked adds the older modules' unmarked global functions that an adapter
header calls directly (group known, hook not). One run takes at most --limit
mutants (default 100), round-robin over the functions.

Results: killed (a named FAIL, a crash or a timeout), survived (every
planned group passed), build-error (the compiler refused the mutant; not a
witness), equivalent-suspect (the linked harness is byte-identical to the
unmutated one: the compiler folded the change away, nothing is run) and
unmapped (no group found). Survivors are the product; this script exits 0
whenever the runs themselves worked.

Incremental: the state file (default build/mutations/state.json, ignored)
keeps each core file's SHA-256 and the result of every mutant run against
that content. A file whose hash changed loses its old results; mutants that
already have a result for the current content are skipped, so repeated
bounded runs (--limit) work through a file. --all reruns everything.

The working tree is never modified: the workspace copy (default
build/mutations/workspace) gets the mutated file, the original is written
back after each file and on any exit (also Ctrl-C), and its BUILD files are
renamed to BUILD.bazel.inactive on exit so `//...` of the enclosing checkout
cannot discover them. Bazel runs with its own --output-base and is shut down
at the end unless --keep-server.
"""
import argparse
import bisect
import hashlib
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
IMAGE_BASE = 0x400000
IMAGE_END = 0x6D8000          # DBSIM.EXE image size 2D8000h
OPERATORS = ("const", "bitflip", "shift", "cmp", "drop")
TARGET = "//tools/reference-game/invitro:differential_harness"
HARNESS = "tools/reference-game/invitro/differential_harness.exe"
WORKSPACE_FILES = ("MODULE.bazel", "MODULE.bazel.lock", ".bazelrc", ".bazelversion", "BUILD.bazel")
WORKSPACE_DIRS = ("core", "hook", "bazel",
                  "tools/reference-game/proxy", "tools/reference-game/invitro")

# ----------------------------------------------------------------------------
# Lexer: enough C for our own sources (comments, strings, preprocessor lines).

_TOKEN = re.compile(r"""
    (?P<ws>\s+)
  | (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<str>"(?:\\.|[^"\\\n])*")
  | (?P<chr>'(?:\\.|[^'\\\n])*')
  | (?P<num>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<punct>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\#\#
              |[][(){}.,;:?~!<>=+\-*/%&|^\#\\])
""", re.S | re.X)
_PREPROCESSOR = re.compile(r"[ \t]*#(?:\\\r?\n|[^\n])*")
_SIGNIFICANT = ("id", "num", "punct", "str", "chr")


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    start: int
    end: int


def lex(text):
    tokens, pos, line_start = [], 0, True
    while pos < len(text):
        if line_start:
            m = _PREPROCESSOR.match(text, pos)
            if m and "#" in m.group():
                tokens.append(Token("pp", m.group(), m.start(), m.end()))
                pos, line_start = m.end(), False
                continue
        m = _TOKEN.match(text, pos)
        if not m:
            line = text.count("\n", 0, pos) + 1
            raise ValueError(f"cannot tokenize line {line}: {text[pos:pos + 20]!r}")
        tokens.append(Token(m.lastgroup, m.group(), m.start(), m.end()))
        line_start = m.lastgroup == "ws" and "\n" in m.group()
        pos = m.end()
    return tokens


def match_brackets(sig):
    pairs, stack = {}, []
    closers = {")": "(", "]": "[", "}": "{"}
    for i, t in enumerate(sig):
        if t.kind != "punct":
            continue
        if t.text in "([{":
            stack.append(i)
        elif t.text in closers:
            if not stack or sig[stack[-1]].text != closers[t.text]:
                raise ValueError(f"unbalanced {t.text!r} at offset {t.start}")
            j = stack.pop()
            pairs[i], pairs[j] = j, i
    if stack:
        raise ValueError(f"unclosed {sig[stack[-1]].text!r} at offset {sig[stack[-1]].start}")
    return pairs


# ----------------------------------------------------------------------------
# Functions and their markers.

MARKER = re.compile(r"Original:\s*DBSIM\.EXE\s+0x([0-9A-Fa-f]+)")
KEYWORDS = {"if", "else", "for", "while", "do", "switch", "case", "default", "return", "break",
            "continue", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "const",
            "volatile", "extern", "inline", "register", "void", "_Static_assert", "unsigned", "signed",
            "int", "char", "short", "long", "float", "double", "_Bool"}
_INT_TYPE = re.compile(r"^(?:u?int(?:8|16|32|64)_t|u?intptr_t|size_t|unsigned|signed|int|long|short|char|"
                       r"_Bool|bool|BYTE|WORD|DWORD|uint|es2_\w*word\w*)$")
_DECL_START = re.compile(r"^(?:const|static|volatile|register|unsigned|signed|struct|union|enum|void|float|double|"
                         r"int|char|short|long|_Bool|u?int(?:8|16|32|64)_t|u?intptr_t|size_t)$")


@dataclass
class Function:
    file: str
    name: str
    line: int
    open: int          # index of the body's "{" in sig
    close: int         # index of the body's "}" in sig
    address: int = None
    static: bool = False
    returns_int: bool = False
    calls: set = field(default_factory=set)


@dataclass
class Source:
    file: str          # repository-relative path with forward slashes
    text: str
    sig: list
    pairs: dict
    functions: list

    def line_of(self, offset):
        return self.text.count("\n", 0, offset) + 1


def parse_source(file, text):
    tokens = lex(text)
    sig = [t for t in tokens if t.kind in _SIGNIFICANT]
    pairs = match_brackets(sig)
    comments = [t for t in tokens if t.kind == "comment"]
    comment_starts = [t.start for t in comments]
    boundaries = sorted(t.end for t in tokens if t.kind == "pp")
    functions, i = [], 0
    while i < len(sig):
        t = sig[i]
        if t.text == ";":
            bisect.insort(boundaries, t.end)
        if t.text != "{" or t.kind != "punct":
            i += 1
            continue
        j = pairs[i]
        if i > 0 and sig[i - 1].text == ")":
            k = pairs[i - 1]
            name = sig[k - 1] if k > 0 else None
            if name is not None and name.kind == "id" and name.text not in KEYWORDS:
                region = boundaries[bisect.bisect_right(boundaries, name.start) - 1] if boundaries and boundaries[0] <= name.start else 0
                header = [s.text for s in sig if region <= s.start < name.start]
                address = None
                lo = bisect.bisect_left(comment_starts, region)
                hi = bisect.bisect_left(comment_starts, name.start)
                for c in comments[lo:hi]:
                    m = MARKER.search(c.text)
                    if m:
                        address = int(m.group(1), 16)
                types = [h for h in header if h not in ("static", "inline", "extern", "const", "volatile")]
                returns_int = "*" not in types and any(_INT_TYPE.match(h) for h in types)
                f = Function(file, name.text, text.count("\n", 0, name.start) + 1, i, j, address,
                             "static" in header, returns_int)
                f.calls = {s.text for s in sig[i + 1:j] if s.kind == "id"}
                functions.append(f)
                bisect.insort(boundaries, sig[j].end)
        i = j + 1
    return Source(file, text, sig, pairs, functions)


# ----------------------------------------------------------------------------
# Operators.

@dataclass
class Mutant:
    file: str
    function: str
    op: str
    start: int
    end: int
    old: str
    new: str
    line: int = 0
    diff: str = ""
    id: str = ""


_INT_LITERAL = re.compile(r"^(0[xX][0-9A-Fa-f]+|0[0-7]*|[1-9][0-9]*)([uUlL]*)$")
_CMP_SWAP = {"<": "<=", "<=": "<", ">": ">=", ">=": ">", "==": "!=", "!=": "=="}
_SHIFT_OPS = {"<<", ">>", "<<=", ">>="}
_SHIFT_CALL = re.compile(r"(?i)(?:sar|shr|shl|shift)")
_BITWISE = {"&", "|", "^", "&=", "|=", "^="}
_ASSIGN = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
STORE_CALL = re.compile(r"^(?:(?:[A-Z]+_)?W(?:8|16|32)|V[WD]|fx_w(?:8|16|32)|write(?:8|16|32)|put(?:8|16|32))$")
_CONTROL = {"if", "else", "for", "while", "do", "switch", "case", "default", "return", "break", "continue", "goto"}


def literal_value(text):
    m = _INT_LITERAL.match(text)
    if not m:
        return None
    digits, suffix = m.groups()
    if digits[:2] in ("0x", "0X"):
        return int(digits, 16), digits, suffix
    if len(digits) > 1 and digits[0] == "0":
        return int(digits, 8), digits, suffix
    return int(digits), digits, suffix


def format_literal(value, digits, suffix):
    if digits[:2] in ("0x", "0X"):
        body = digits[2:]
        out = format(value, "X" if body != body.lower() else "x").zfill(len(body) if body.startswith("0") else 1)
        return digits[:2] + out + suffix
    if len(digits) > 1 and digits[0] == "0":
        return "0" + format(value, "o") + suffix
    return str(value) + suffix


def _fits(value, original, suffix):
    if value < 0:
        return False
    return value <= 0xFFFFFFFF or original > 0xFFFFFFFF or "ll" in suffix.lower()


class _Body:
    """Statement structure of one function body (indices into sig)."""

    def __init__(self, src, f):
        self.src, self.f, self.sig, self.pairs = src, f, src.sig, src.pairs
        self.statements = []      # (first, semicolon, kind) with kind expr/decl/return/other
        self.declarator_dims = set()
        self._scan(f.open + 1, f.close)

    def _skip(self, i):
        return self.pairs.get(i, i) if self.sig[i].text in "([{" and self.sig[i].kind == "punct" else i

    def _end(self, i, limit):
        while i < limit:
            t = self.sig[i]
            if t.kind == "punct" and t.text == ";":
                return i
            if t.kind == "punct" and t.text == "}":
                return None
            i = self._skip(i) + 1
        return None

    def _scan(self, i, limit):
        sig = self.sig
        expect_start = True
        while i < limit:
            t = sig[i]
            if t.kind == "punct" and t.text == "{":
                self._scan(i + 1, self.pairs[i])
                i, expect_start = self.pairs[i] + 1, True
                continue
            if t.kind == "punct" and t.text in (";", "}"):
                i, expect_start = i + 1, True
                continue
            if t.kind == "id" and t.text in ("if", "while", "for", "switch") and i + 1 < limit and sig[i + 1].text == "(":
                header_close = self.pairs[i + 1]
                if t.text == "for":
                    self._statement_in_for(i + 2, header_close)
                i, expect_start = header_close + 1, True
                continue
            if t.kind == "id" and t.text in ("else", "do"):
                i, expect_start = i + 1, True
                continue
            if t.kind == "id" and t.text in ("case", "default"):
                j = i + 1
                while j < limit and not (sig[j].kind == "punct" and sig[j].text == ":"):
                    j = self._skip(j) + 1
                i, expect_start = j + 1, True
                continue
            if expect_start and t.kind == "id" and i + 1 < limit and sig[i + 1].text == ":" and t.text not in KEYWORDS:
                i += 2          # label
                continue
            if not expect_start:
                i += 1
                continue
            end = self._end(i, limit)
            if end is None:
                i += 1
                continue
            kind = self._kind(i, end)
            self.statements.append((i, end, kind))
            if kind == "decl":
                self._dims(i, end)
            # Nested blocks inside an expression statement are not expected; skip to the ";".
            i, expect_start = end + 1, True

    def _statement_in_for(self, i, limit):
        end = self._end(i, limit)
        if end is not None and self._kind(i, end) == "decl":
            self._dims(i, end)

    def _kind(self, i, end):
        sig = self.sig
        first = sig[i]
        if first.kind == "id" and first.text == "return":
            return "return"
        if first.kind == "id" and first.text in _CONTROL:
            return "other"
        if first.kind == "id" and _DECL_START.match(first.text):
            return "decl"
        if first.kind == "id" and i + 1 < end and sig[i + 1].kind == "id":
            return "decl"
        if (first.kind == "id" and i + 3 <= end and sig[i + 1].text == "*" and sig[i + 2].kind == "id"
                and sig[i + 3].text in ("=", ",", ";", "[")):
            return "decl"
        return "expr"

    def _dims(self, i, end):
        """Literals inside declarator brackets (not initializers) of a declaration."""
        in_init = False
        while i < end:
            t = self.sig[i]
            if t.kind == "punct" and t.text == "=":
                in_init = True
            elif t.kind == "punct" and t.text == ",":
                in_init = False
            elif t.kind == "punct" and t.text == "[" and not in_init:
                for k in range(i + 1, self.pairs[i]):
                    self.declarator_dims.add(k)
            if t.kind == "punct" and t.text in "([{" and in_init:
                i = self.pairs[i]
            i += 1

    def top_level(self, i, end):
        """Indices i..end-1 at bracket depth 0 relative to i."""
        while i < end:
            yield i
            i = self._skip(i) + 1


def _call_context(sig, pairs, f, idx):
    """Name of the innermost call whose argument list directly holds sig[idx], and whether
    sig[idx] alone forms a non-first argument."""
    depth, j = 0, idx - 1
    while j > f.open:
        t = sig[j]
        if t.kind == "punct" and t.text in ")]}":
            j = pairs[j] - 1
            continue
        if t.kind == "punct" and t.text in "[{":
            return None, False
        if t.kind == "punct" and t.text == "(":
            callee = sig[j - 1] if sig[j - 1].kind == "id" and sig[j - 1].text not in KEYWORDS else None
            alone = sig[idx - 1].text == "," and sig[idx + 1].text in (",", ")")
            return (callee.text if callee else None), alone
        j -= 1
    return None, False


def mutants_for(src, f, skip_addresses=True):
    sig, pairs, text = src.sig, src.pairs, src.text
    body = _Body(src, f)
    out = []

    def add(op, a, b, new):
        out.append(Mutant(f.file, f.name, op, a, b, text[a:b], new))

    def flip(i, j):
        """Low-bit flip of the value sig[i..j]; a lone literal is already covered by const."""
        if i == j and sig[i].kind == "num":
            return
        add("bitflip", sig[i].start, sig[j].end, "(" + text[sig[i].start:sig[j].end] + ")^1u")

    for idx in range(f.open + 1, f.close):
        t = sig[idx]
        prev, nxt = sig[idx - 1], sig[idx + 1]
        if t.kind == "punct" and t.text in _CMP_SWAP:
            add("cmp", t.start, t.end, _CMP_SWAP[t.text])
            continue
        if t.kind != "num":
            continue
        parsed = literal_value(t.text)
        if parsed is None:
            continue
        value, digits, suffix = parsed
        callee, alone = _call_context(sig, pairs, f, idx)
        if (prev.kind == "punct" and prev.text in _SHIFT_OPS) or (alone and callee and _SHIFT_CALL.search(callee)):
            for v in (value + 1, value - 1):
                if 0 <= v <= 63 and v != value:
                    add("shift", t.start, t.end, format_literal(v, digits, suffix))
            continue
        if idx in body.declarator_dims:
            continue
        if skip_addresses and IMAGE_BASE <= value < IMAGE_END:
            continue
        for v in (value + 1, value - 1):
            if _fits(v, value, suffix):
                add("const", t.start, t.end, format_literal(v, digits, suffix))
        if prev.text in _BITWISE or nxt.text in ("&", "|", "^"):
            seen = {value, value + 1, value - 1}
            if value:
                for bit in (value & -value, 1 << (value.bit_length() - 1)):
                    v = value ^ bit
                    if v not in seen:
                        seen.add(v)
                        add("bitflip", t.start, t.end, format_literal(v, digits, suffix))

    for first, end, kind in body.statements:
        a, b = sig[first].start, sig[end].end
        if kind == "return" and f.returns_int and end > first + 1:
            flip(first + 1, end - 1)
            continue
        if kind != "expr":
            continue
        top = list(body.top_level(first, end))
        assign = next((k for k in top if sig[k].kind == "punct" and sig[k].text in _ASSIGN), None)
        if assign is not None:
            if assign == first + 1 and sig[first].kind == "id":
                continue                      # plain local variable
            add("drop", a, b, "(void)0;")
            if sig[assign].text == "=" and assign + 1 < end:
                flip(assign + 1, end - 1)
            continue
        # A store call that is the whole statement: NAME(...) or obj->NAME(...)
        k = first
        while k + 2 < end and sig[k].kind == "id" and sig[k + 1].text in ("->", ".") and sig[k + 2].kind == "id":
            k += 2
        if (sig[k].kind == "id" and STORE_CALL.match(sig[k].text) and k + 1 < end and sig[k + 1].text == "("
                and pairs[k + 1] == end - 1):
            add("drop", a, b, "(void)0;")
            args = [j for j in body.top_level(k + 2, end - 1) if sig[j].text == ","]
            if args:
                flip(args[-1] + 1, end - 2)

    out.sort(key=lambda m: (m.start, OPERATORS.index(m.op), m.new))
    for m in out:
        m.line = src.line_of(m.start)
        m.diff = render_diff(text, m)
        key = f"{m.file}|{m.function}|{m.op}|{m.start - sig[f.open].start}|{m.old}|{m.new}"
        m.id = hashlib.sha1(key.encode()).hexdigest()[:12]
    return out


def render_diff(text, m, context=40, longest=80):
    ls = text.rfind("\n", 0, m.start) + 1
    le = text.find("\n", m.end)
    le = len(text) if le < 0 else le

    def squash(s):
        return re.sub(r"\s+", " ", s)
    before, after = squash(text[max(ls, m.start - context):m.start]), squash(text[m.end:min(le, m.end + context)])
    old = squash(m.old)
    if len(old) > longest:
        old = old[:longest - 1] + "..."
    lead = "..." if m.start - context > ls else ""
    tail = "..." if m.end + context < le else ""
    return f"{lead}{before.lstrip() if not lead else before}[{old}=>{squash(m.new)}]{after}{tail}"


def apply(text, m):
    assert text[m.start:m.end] == m.old, "stale mutant"
    return text[:m.start] + m.new + text[m.end:]


# ----------------------------------------------------------------------------
# Function -> hooks -> groups.

_REGISTRY_ROW = re.compile(r"^ES2_HOOK\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(\w+)[^,]*,[^,]*,\s*(\w+)\s*\)", re.M)
_DESCRIPTOR_ARRAY = re.compile(r"\bes2_hook_descriptor\s+(\w+)\s*\[\s*\]\s*=")
_GROUP_TABLE = re.compile(r"HOOK_GROUPS\s*=\s*\{(.*?)\}", re.S)


def registry_hooks(text):
    """[(hook, va, descriptor array, group)] in registry order."""
    rows = []
    for name, rva, array, group in _REGISTRY_ROW.findall(text):
        rva = int(rva, 0)
        rows.append((name, rva + IMAGE_BASE if rva < IMAGE_BASE else rva, array, group))
    if not rows:
        raise ValueError("no ES2_HOOK entries in the registry")
    return rows


def bzl_groups(text):
    m = _GROUP_TABLE.search(text)
    if not m:
        raise ValueError("HOOK_GROUPS table not found")
    return re.findall(r'"(\w+)"\s*:', m.group(1))


def adapter_references(headers, hooks, names):
    """{core function name: [groups]} for adapter headers naming the function."""
    groups_of_array = {}
    for _, _, array, group in hooks:
        groups_of_array.setdefault(array, [])
        if group not in groups_of_array[array]:
            groups_of_array[array].append(group)
    result = {}
    for text in headers:
        code = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
        groups = [g for a in _DESCRIPTOR_ARRAY.findall(code) for g in groups_of_array.get(a, [])]
        if not groups:
            continue
        for ident in set(re.findall(r"\b[A-Za-z_]\w*\b", code)) & names:
            for g in groups:
                result.setdefault(ident, [])
                if g not in result[ident]:
                    result[ident].append(g)
    return result


class Mapping:
    """Hooks and adapter groups of every core function."""

    def __init__(self, sources, hooks, adapter_headers=(), known_groups=None):
        self.hooks_at = {}
        for name, va, _, group in hooks:
            self.hooks_at.setdefault(va, []).append((name, group))
        self.functions = [f for s in sources for f in s.functions]
        globals_ = {}
        for f in self.functions:
            if not f.static:
                globals_.setdefault(f.name, f)
        by_file = {}
        for f in self.functions:
            by_file.setdefault(f.file, {})[f.name] = f
        self.callers = {id(f): [] for f in self.functions}
        for f in self.functions:
            local = by_file[f.file]
            for name in sorted(f.calls):
                callee = local.get(name) or globals_.get(name)
                if callee is not None and callee is not f:
                    self.callers[id(callee)].append(f)
        self.adapters = adapter_references(adapter_headers, hooks, set(globals_))
        self.known = set(known_groups) if known_groups is not None else None

    def direct_hooks(self, f):
        return list(self.hooks_at.get(f.address, [])) if f.address is not None else []

    def groups(self, f):
        """(direct hooks, ordered groups): the function's own hooks' groups first, then
        adapter groups and the groups of hooked callers, breadth first."""
        direct = self.direct_hooks(f)
        order = [g for _, g in direct]
        seen, queue = {id(f)}, [f]
        while queue:
            g = queue.pop(0)
            if g is not f:
                order += [grp for _, grp in self.direct_hooks(g)]
            if not g.static:
                order += self.adapters.get(g.name, [])
            for caller in self.callers[id(g)]:
                if id(caller) not in seen:
                    seen.add(id(caller))
                    queue.append(caller)
        result = []
        for g in order:
            if g not in result and (self.known is None or g in self.known):
                result.append(g)
        return direct, result


# ----------------------------------------------------------------------------
# State, selection and sampling.

def sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def load_state(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        if data.get("version") == 1:
            return data
    except (OSError, ValueError):
        pass
    return {"version": 1, "files": {}}


def save_state(path, state):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as out:
        json.dump(state, out, indent=1, sort_keys=True)
        out.write("\n")
    os.replace(tmp, path)


def file_state(state, file, digest, rerun_all):
    """The state entry of a file for its current content (reset when it changed)."""
    entry = state["files"].get(file)
    if rerun_all or entry is None or entry.get("sha256") != digest:
        entry = {"sha256": digest, "results": {}}
        state["files"][file] = entry
    return entry


def sample(pending, per_function, limit, seed):
    """Deterministic per-function sample, then round-robin across functions up to limit."""
    by_function = {}
    for m in pending:
        by_function.setdefault((m.file, m.function), []).append(m)
    lists = []
    for key in sorted(by_function):
        items = by_function[key]
        if per_function and len(items) > per_function:
            rng = random.Random(f"{seed}|{key[0]}|{key[1]}")
            items = sorted(rng.sample(items, per_function), key=lambda m: m.start)
        lists.append(items)
    out, k = [], 0
    while any(k < len(x) for x in lists):
        out += [x[k] for x in lists if k < len(x)]
        k += 1
    return out[:limit] if limit else out


# ----------------------------------------------------------------------------
# Workspace and Bazel.

class Workspace:
    def __init__(self, repo, path, bazel, output_base, contents_cache, log, extra_flags=()):
        self.repo, self.path, self.bazel = Path(repo), Path(path), bazel
        self.startup = [f"--output_base={Path(output_base).as_posix()}"]
        self.flags = ["--lockfile_mode=error", "--remote_upload_local_results=false"] + list(extra_flags)
        if contents_cache:
            self.flags.append(f"--repo_contents_cache={Path(contents_cache).as_posix()}")
        self.log = log
        self.originals = {}
        self.exe = None

    def create(self):
        if self.path.exists():
            shutil.rmtree(self.path)
        self.path.mkdir(parents=True)
        for name in WORKSPACE_FILES:
            if (self.repo / name).exists():
                shutil.copy2(self.repo / name, self.path / name)
        for folder in WORKSPACE_DIRS:
            shutil.copytree(self.repo / folder, self.path / folder,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))

    def write(self, file, text):
        target = self.path / file
        if file not in self.originals:
            self.originals[file] = target.read_bytes()
        with open(target, "w", encoding="utf-8", newline="") as out:
            out.write(text)

    def restore(self):
        for file, data in self.originals.items():
            (self.path / file).write_bytes(data)
        self.originals.clear()

    def deactivate(self):
        for build in self.path.rglob("BUILD.bazel"):
            os.replace(build, build.with_name("BUILD.bazel.inactive"))

    def _bazel(self, args, log_name, timeout=None):
        cmd = [self.bazel] + self.startup + args
        with open(self.log / log_name, "w", encoding="utf-8", newline="\n") as out:
            result = subprocess.run(cmd, cwd=self.path, stdout=out, stderr=subprocess.STDOUT, timeout=timeout)
        return result.returncode

    def build(self, log_name="build.log"):
        status = self._bazel(["build", TARGET] + self.flags, log_name)
        if status == 0 and self.exe is None:
            out = subprocess.run([self.bazel] + self.startup + ["info", "bazel-bin"] + self.flags,
                                 cwd=self.path, capture_output=True, text=True)
            if out.returncode:
                raise RuntimeError("bazel info bazel-bin failed: " + out.stderr[-500:])
            self.exe = Path(out.stdout.strip().splitlines()[-1]) / HARNESS
        return status

    def digest(self):
        return hashlib.sha256(self.exe.read_bytes()).hexdigest()

    def shutdown(self):
        subprocess.run([self.bazel] + self.startup + ["shutdown"], cwd=self.path,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


_FAIL = re.compile(r"^FAIL: .*$", re.M)


def run_group(exe, group, data_root, timeout, cwd):
    """(passed, detail, seconds) of one quick group run."""
    env = dict(os.environ, ES2_DATA_ROOT=data_root)
    for name in ("TEST_TOTAL_SHARDS", "TEST_SHARD_INDEX", "TEST_SHARD_STATUS_FILE"):
        env.pop(name, None)
    start = time.monotonic()
    try:
        out = subprocess.run([str(exe), "--quick", "--group", group], cwd=cwd, env=env,
                             capture_output=True, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timeout after {timeout:.0f} s", time.monotonic() - start
    seconds = time.monotonic() - start
    text = (out.stdout or "") + (out.stderr or "")
    if out.returncode == 0 and "SKIP:" in text and "PASS:" not in text:
        raise RuntimeError(f"harness skipped the group {group}: {text.strip()[-300:]}")
    if out.returncode == 0:
        return True, "", seconds
    fail = _FAIL.search(text)
    detail = fail.group(0).strip() if fail else f"exit status {out.returncode:#x} without a FAIL line"
    return False, detail, seconds


# ----------------------------------------------------------------------------
# Driver.

def load_sources(repo):
    sources = []
    for path in sorted((repo / "core" / "src").glob("*.c")):
        rel = path.relative_to(repo).as_posix()
        with open(path, encoding="utf-8", newline="") as stream:   # exact bytes, no newline translation
            sources.append(parse_source(rel, stream.read()))
    return sources


def select_files(sources, names):
    if not names:
        return sources
    wanted = {Path(n).name for n in names}
    chosen = [s for s in sources if Path(s.file).name in wanted]
    missing = wanted - {Path(s.file).name for s in chosen}
    if missing:
        raise SystemExit(f"unknown core file(s): {', '.join(sorted(missing))}")
    return chosen


def plan(args, sources, mapping, state):
    """The pending mutants with their group plans, and the per-function summary."""
    pending, catalogue, skipped_files = [], [], []
    for src in select_files(sources, args.files):
        marked = [f for f in src.functions if f.address is not None
                  or (args.unmarked and not f.static and f.name in mapping.adapters)]
        entry = file_state(state, src.file, sha256(src.text), args.all)
        file_pending, generated = [], 0
        for f in marked:
            direct, groups = mapping.groups(f)
            hooks = [h for h, _ in direct]
            if args.functions and f.name not in args.functions and not set(hooks) & set(args.functions):
                continue
            if args.group:
                groups = [g for g in groups if g in args.group]
                if not groups:
                    continue
            groups = groups[:args.max_groups]
            ms = [m for m in mutants_for(src, f, not args.addresses) if m.op in args.ops]
            catalogue.append((src.file, f, hooks, groups, ms))
            generated += len(ms)
            for m in ms:
                m.groups, m.hooks, m.address = groups, hooks, f.address
                if args.ids and m.id not in args.ids:
                    continue
                if m.id not in entry["results"]:
                    file_pending.append(m)
        if generated and not file_pending:
            skipped_files.append(src.file)
        pending += file_pending
    chosen = sample(pending, args.per_function, args.limit, args.seed)
    return chosen, catalogue, skipped_files, len(pending)


def result_record(m, status, detail="", group="", seconds=0.0):
    return {"file": m.file, "function": m.function, "address": f"0x{m.address:x}" if m.address is not None else None, "hooks": m.hooks,
            "op": m.op, "line": m.line, "diff": m.diff, "status": status, "detail": detail,
            "group": group, "groups": m.groups, "seconds": round(seconds, 1)}


def summarize(records, out):
    by_group = {}
    for r in records:
        key = r["groups"][0] if r["groups"] else "(unmapped)"
        row = by_group.setdefault(key, {"mutants": 0, "killed": 0, "survived": 0, "build-error": 0,
                                        "equivalent-suspect": 0, "unmapped": 0, "seconds": 0.0})
        row["mutants"] += 1
        row[r["status"]] += 1
        row["seconds"] += r.get("seconds", 0.0) + r.get("build_seconds", 0.0)
    lines = ["| Group | Mutants | Killed | Survived | Build error | Equivalent-suspect | Unmapped | Time (s) |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for g in sorted(by_group):
        r = by_group[g]
        lines.append(f"| {g} | {r['mutants']} | {r['killed']} | {r['survived']} | {r['build-error']} | "
                     f"{r['equivalent-suspect']} | {r['unmapped']} | {r['seconds']:.0f} |")
    by_function = {}
    for r in records:
        by_function.setdefault((r["file"], r["function"]), []).append(r)
    lines += ["", "| File | Function | Hooks | Killed | Survived | Build error | Equivalent-suspect |", "|---|---|---|---:|---:|---:|---:|"]
    for (file, function), rs in sorted(by_function.items()):
        count = lambda s: sum(1 for r in rs if r["status"] == s)
        lines.append(f"| {Path(file).name} | {function} | {', '.join(rs[0]['hooks']) or '-'} | {count('killed')} | "
                     f"{count('survived')} | {count('build-error')} | {count('equivalent-suspect')} |")
    survivors = [r for r in records if r["status"] in ("survived", "equivalent-suspect")]
    if survivors:
        lines += ["", "Survivors and equivalent suspects:"]
        for r in sorted(survivors, key=lambda r: (r["file"], r["line"])):
            lines.append(f"- {r['status']} {Path(r['file']).name}:{r['line']} {r['function']} {r['op']} "
                         f"[{','.join(r['groups'])}]: {r['diff']}")
    text = "\n".join(lines) + "\n"
    out.write(text)
    return text


def parse_args(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--list", action="store_true", help="list the selected mutants and their groups; build nothing")
    p.add_argument("--report", action="store_true", help="print the stored results and survivors; build nothing")
    p.add_argument("--all", action="store_true", help="ignore stored results: every file and mutant is due")
    p.add_argument("--files", nargs="+", help="core/src file names (default: all)")
    p.add_argument("--group", nargs="+", help="only functions mapped to these groups, run only against them")
    p.add_argument("--functions", nargs="+", help="only these core functions or hook names")
    p.add_argument("--ops", nargs="+", choices=OPERATORS, default=list(OPERATORS))
    p.add_argument("--ids", nargs="+",
                   help="only these mutant ids; @FILE reads the first word of each line (e.g. an earlier run's survivors)")
    p.add_argument("--per-function", type=int, default=0, help="at most N mutants per function (seeded sample)")
    p.add_argument("--limit", type=int, default=100,
                   help="at most N mutants in this run, round-robin over functions (default 100; 0 = no limit)")
    p.add_argument("--seed", default="t-08")
    p.add_argument("--max-groups", type=int, default=2, help="groups tried per mutant (direct hook groups first)")
    p.add_argument("--addresses", action="store_true", help="also mutate literals in the image VA range")
    p.add_argument("--unmarked", action="store_true",
                   help="also mutate unmarked global functions that an adapter header calls (older modules)")
    p.add_argument("--state", default=str(REPO / "build" / "mutations" / "state.json"))
    p.add_argument("--out", default=str(REPO / "build" / "mutations"), help="logs and run reports")
    p.add_argument("--workspace", default=str(REPO / "build" / "mutations" / "workspace"))
    p.add_argument("--output-base", default=str(REPO / "build" / "bazel" / "mutations"))
    p.add_argument("--repo-contents-cache", default=None)
    p.add_argument("--bazel", default="bazel")
    p.add_argument("--bazel-flag", action="append", default=[],
                   help="extra build option for the mutant builds, repeatable (e.g. --bazel-flag=--local_resources=cpu=4)")
    p.add_argument("--data-root", default=os.environ.get("ES2_DATA_ROOT"))
    p.add_argument("--keep-server", action="store_true", help="do not shut the mutant Bazel server down")
    args = p.parse_args(argv)
    if args.functions:
        args.functions = set(args.functions)
    if args.ids:
        ids = set()
        for item in args.ids:
            if item.startswith("@"):
                with open(item[1:], encoding="utf-8") as stream:
                    ids.update(line.split()[0] for line in stream if line.strip() and not line.startswith("#"))
            else:
                ids.add(item)
        args.ids = ids
    return args


def main(argv=None, repo=REPO):
    args = parse_args(argv)
    sources = load_sources(repo)
    registry = registry_hooks((repo / "tools/reference-game/proxy/hook_registry.h").read_text(encoding="utf-8"))
    known = bzl_groups((repo / "tools/reference-game/invitro/groups.bzl").read_text(encoding="utf-8"))
    headers = [p.read_text(encoding="utf-8") for p in sorted((repo / "tools/reference-game/proxy").glob("*_hooks.h"))]
    mapping = Mapping(sources, registry, headers, known)
    state = load_state(args.state)
    out_dir = Path(args.out)

    if args.report:
        records = [r for e in state["files"].values() for r in e["results"].values()]
        summarize(records, sys.stdout)
        return 0

    chosen, catalogue, skipped, pending_total = plan(args, sources, mapping, state)
    if args.list:
        for file, f, hooks, groups, ms in catalogue:
            print(f"{file}:{f.line} {f.name} {f'0x{f.address:x}' if f.address is not None else '(unmarked)'} hooks={','.join(hooks) or '-'} "
                  f"groups={','.join(groups) or '-'} mutants={len(ms)}")
        for m in chosen:
            print(f"  {m.id} {Path(m.file).name}:{m.line} {m.function} {m.op}: {m.diff}")
        print(f"{len(chosen)} of {pending_total} pending mutants selected; "
              f"{len(skipped)} file(s) already checked for their current content")
        return 0
    if not chosen:
        print(f"Nothing to do: {len(skipped)} file(s) unchanged and fully checked (use --all to rerun).")
        return 0
    if not args.data_root:
        raise SystemExit("ES2_DATA_ROOT (or --data-root) is required to run mutants")

    stamp = time.strftime("%Y%m%d-%H%M%S")
    log = out_dir / f"run-{stamp}"
    log.mkdir(parents=True, exist_ok=True)
    ws = Workspace(repo, args.workspace, args.bazel, args.output_base, args.repo_contents_cache, log,
                   args.bazel_flag)
    records, baseline_time, started = [], {}, time.monotonic()
    texts = {s.file: s.text for s in sources}
    try:
        ws.create()
        print(f"Workspace {ws.path}; output base {args.output_base}", flush=True)
        if ws.build("baseline-build.log"):
            raise SystemExit(f"baseline build failed, see {log / 'baseline-build.log'}")
        baseline = ws.digest()
        # Every planned group must pass unmutated first; its time sets the mutant timeout.
        for g in dict.fromkeys(g for m in chosen for g in m.groups):
            ok, detail, seconds = run_group(ws.exe, g, args.data_root, 3600, log)
            if not ok:
                raise SystemExit(f"baseline run of group {g} failed: {detail}")
            baseline_time[g] = round(seconds, 1)
            print(f"baseline {g}: PASS in {seconds:.1f} s", flush=True)
        current_file = None
        for n, m in enumerate(chosen, 1):
            entry = state["files"][m.file]
            if not m.groups:
                entry["results"][m.id] = rec = result_record(m, "unmapped")
                records.append(rec)
                continue
            if current_file not in (None, m.file):
                ws.restore()
            current_file = m.file
            ws.write(m.file, apply(texts[m.file], m))
            t0 = time.monotonic()
            status = ws.build(f"build-{m.id}.log")
            build_seconds = time.monotonic() - t0
            if status:
                rec = result_record(m, "build-error", _build_error(log / f"build-{m.id}.log"))
            elif ws.digest() == baseline:
                os.remove(log / f"build-{m.id}.log")
                rec = result_record(m, "equivalent-suspect", "linked harness identical to the unmutated build")
            else:
                os.remove(log / f"build-{m.id}.log")
                rec, total = None, 0.0
                for g in m.groups:
                    timeout = max(60.0, 5 * baseline_time[g] + 30)
                    ok, detail, seconds = run_group(ws.exe, g, args.data_root, timeout, log)
                    total += seconds
                    if not ok:
                        rec = result_record(m, "killed", detail, g, total)
                        break
                if rec is None:
                    rec = result_record(m, "survived", "", ",".join(m.groups), total)
            rec["build_seconds"] = round(build_seconds, 1)
            entry["results"][m.id] = rec
            records.append(rec)
            save_state(args.state, state)
            print(f"[{n}/{len(chosen)}] {rec['status']:<18} {Path(m.file).name}:{m.line} {m.function} {m.op} "
                  f"{rec['group'] or ''} {rec['detail'][:100]}", flush=True)
    finally:
        try:
            ws.restore()
        finally:
            if ws.path.exists():
                ws.deactivate()
            if not args.keep_server:
                ws.shutdown()
            if records:
                save_state(args.state, state)
    wall = time.monotonic() - started
    with open(log / "summary.md", "w", encoding="utf-8", newline="\n") as out:
        out.write(f"Mutation run {stamp}: {len(records)} mutants, wall time {wall:.0f} s\n\n")
        summarize(records, out)
    with open(log / "results.json", "w", encoding="utf-8", newline="\n") as out:
        json.dump({"wall_seconds": round(wall, 1), "baseline_seconds": baseline_time, "results": records},
                  out, indent=1)
        out.write("\n")
    print(f"\n{len(records)} mutants in {wall:.0f} s; logs {log}\n")
    summarize(records, sys.stdout)
    return 0


def _build_error(path):
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return "build failed"
    m = re.search(r"error: .*", text)
    return m.group(0)[:200] if m else "build failed"


if __name__ == "__main__":
    sys.exit(main())
