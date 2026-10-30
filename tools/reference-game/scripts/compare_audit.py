"""Compare-mode audit: which registered hooks accept `compare` mode.

Reads `tools/reference-game/proxy/hook_registry.h` and every adapter header
`proxy/*_hooks.h`, and reports per hook whether the proxy accepts
`<hook>=compare`. The rule is the one of `hook/hook.c`
(`extension_valid`): a hook whose registry extension is NULL uses the legacy
path and may compare; a hook whose `es2_hook_extension` has
`compare_unsupported` (the eighth initializer) TRUE is refused; FALSE or a
missing eighth initializer (zero) accepts compare.

The adapters define their extensions directly or through function-like
macros (`X_EXT(name)`, `X_EXT(name,refuse)`, `X_BOTH(name)` expanding to
`X_EXT(name) ...`). A small expander handles exactly that subset: comments,
line continuations, `#define`/`#undef` of function-like macros in order,
parameter substitution and `##` pasting, nested invocations. It is not a C
preprocessor; anything it cannot resolve (a flag that is not TRUE/FALSE/0/1
after expansion, a registry extension without a definition) is reported as
`unknown` and makes the run fail, rather than guessing.

    python compare_audit.py                    # Markdown table to stdout
    python compare_audit.py --json             # JSON list
    python compare_audit.py --hooks-doc docs/reference-game/hooks.md
    python compare_audit.py --check <doc>      # any document with a Hook ... Compare table

`--check` compares the Compare column of the audit table in the given
document (rows `| \\`hook\\` | ... | yes/no | ...`, the column headed
"Compare") with the extraction and lists hooks that are missing, extra or
disagree; exit status 1 if any. `--hooks-doc` additionally checks the
Compare column of the registry table in `hooks.md`. Read-only; standard
library only.
"""
import argparse
import json
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
PROXY = TOOLS / 'proxy'
IMAGE_BASE = 0x400000

REGISTRY_LINE = re.compile(
    r'^\s*ES2_HOOK\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*([^,]+?)\s*,\s*(&?\s*\w+)\s*,\s*(\w+)\s*\)')
EXTENSION_DEF = re.compile(r'\bes2_hook_extension\s+(\w+)\s*=\s*\{')
IDENT = re.compile(r'[A-Za-z_]\w*')
FLAG_VALUES = {'TRUE': True, '1': True, 'FALSE': False, '0': False}


class AuditError(Exception):
    pass


# ------------------------------------------------------------ registry

def load_registry(path):
    """[(name, va, extension or None, group)] in registry order."""
    hooks = []
    for n, line in enumerate(Path(path).read_text(encoding='utf-8').splitlines(), 1):
        if not line.lstrip().startswith('ES2_HOOK('):
            continue
        m = REGISTRY_LINE.match(line)
        if not m:
            raise AuditError(f'{path}:{n}: unparsed registry line: {line.strip()}')
        ext = m.group(4).replace('&', '').strip()
        hooks.append((m.group(1), IMAGE_BASE + int(m.group(2), 16), None if ext == 'NULL' else ext, m.group(5)))
    if not hooks:
        raise AuditError(f'{path}: no ES2_HOOK lines')
    return hooks


# ------------------------------------------------------------ mini expander

def strip_comments(text):
    """Remove /* */ and // comments, keep newlines and string/char literals."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j) or ' ')
            i = j
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def logical_lines(text):
    """Yield (first physical line number, logical line) after joining `\\` continuations."""
    buf, start = [], None
    for n, line in enumerate(text.split('\n'), 1):
        if start is None:
            start = n
        if line.endswith('\\'):
            buf.append(line[:-1])
            continue
        buf.append(line)
        yield start, ' '.join(buf)
        buf, start = [], None
    if buf:
        yield start, ' '.join(buf)


def split_args(text, open_index):
    """Arguments of the call whose '(' is at open_index; returns (args, index after ')')."""
    depth, args, cur, i = 0, [], [], open_index
    while i < len(text):
        c = text[i]
        if c in '([{':
            depth += 1
            if depth > 1:
                cur.append(c)
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append(''.join(cur).strip())
                return args, i + 1
            cur.append(c)
        elif c == ',' and depth == 1:
            args.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    raise AuditError('unbalanced macro invocation')


def substitute(body, params, args):
    if len(args) != len(params) and not (params == [] and args == ['']):
        raise AuditError(f'macro expects {len(params)} arguments, got {len(args)}')
    values = dict(zip(params, args))
    out = IDENT.sub(lambda m: values.get(m.group(0), m.group(0)), body)
    return re.sub(r'\s*##\s*', '', out)


def expand(text, macros, depth=0):
    """Expand every invocation of a known function-like macro in text."""
    if depth > 16:
        raise AuditError('macro expansion too deep')
    out, pos = [], 0
    for m in IDENT.finditer(text):
        if m.start() < pos or m.group(0) not in macros:
            continue
        j = m.end()
        while j < len(text) and text[j] in ' \t\n':
            j += 1
        if j >= len(text) or text[j] != '(':
            continue
        params, body = macros[m.group(0)]
        args, end = split_args(text, j)
        out.append(text[pos:m.start()])
        out.append(expand(substitute(body, params, args), macros, depth + 1))
        pos = end
    out.append(text[pos:])
    return ''.join(out)


def extensions_in(text):
    """{name: (flag or None, raw flag text)} for every extension definition in expanded text."""
    found = {}
    for m in EXTENSION_DEF.finditer(text):
        fields, _ = split_args(text, m.end() - 1)
        raw = fields[7] if len(fields) > 7 else '0'
        found[m.group(1)] = (FLAG_VALUES.get(raw.strip()), raw.strip())
    return found


DEFINE = re.compile(r'^\s*#\s*define\s+(\w+)\(([^)]*)\)\s?(.*)$', re.S)
UNDEF = re.compile(r'^\s*#\s*undef\s+(\w+)')


def scan_adapter(text, filename='<text>'):
    """{extension name: dict(flag, raw, file, line)} defined in one adapter header."""
    macros, result = {}, {}
    chunk, chunk_line = [], None

    def flush():
        if not chunk:
            return
        body = '\n'.join(chunk)
        for name, (flag, raw) in extensions_in(expand(body, macros)).items():
            result[name] = dict(flag=flag, raw=raw, file=filename, line=chunk_line)
        chunk.clear()

    for line_no, line in logical_lines(strip_comments(text)):
        stripped = line.lstrip()
        if stripped.startswith('#'):
            flush()
            d = DEFINE.match(line)
            u = UNDEF.match(line)
            if d:
                params = [p.strip() for p in d.group(2).split(',') if p.strip()]
                macros[d.group(1)] = (params, d.group(3))
            elif u:
                macros.pop(u.group(1), None)
            continue
        # One logical line per chunk entry; flush per line keeps line numbers
        # exact unless an initializer spans lines (then the first line).
        if not chunk:
            chunk_line = line_no
        chunk.append(line)
        joined = '\n'.join(chunk)
        if joined.count('{') == joined.count('}') and joined.count('(') == joined.count(')'):
            flush()
    flush()
    return result


def scan_proxy(proxy_dir):
    extensions = {}
    for path in sorted(Path(proxy_dir).glob('*_hooks.h')):
        for name, info in scan_adapter(path.read_text(encoding='utf-8'), path.name).items():
            if name in extensions:
                raise AuditError(f'extension {name} defined in {extensions[name]["file"]} and {path.name}')
            extensions[name] = info
    return extensions


# ------------------------------------------------------------ audit

def audit(registry_path, proxy_dir):
    """[dict(hook, va, group, extension, compare, source)] in registry order."""
    extensions = scan_proxy(proxy_dir)
    rows = []
    for name, va, ext, group in load_registry(registry_path):
        if ext is None:
            rows.append(dict(hook=name, va=va, group=group, extension=None, compare='yes', source='legacy (NULL extension)'))
            continue
        info = extensions.get(ext)
        if info is None:
            rows.append(dict(hook=name, va=va, group=group, extension=ext, compare='unknown', source='definition not found'))
            continue
        compare = 'unknown' if info['flag'] is None else ('no' if info['flag'] else 'yes')
        rows.append(dict(hook=name, va=va, group=group, extension=ext, compare=compare,
                         source=f'{info["file"]}:{info["line"]} compare_unsupported={info["raw"]}'))
    return rows


def doc_compare_column(path, heading_hint=None):
    """{hook: yes/no} from the first Markdown table whose header has a 'Compare' column.

    With heading_hint, only a table whose header line contains that text counts.
    """
    out, column = {}, None
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.startswith('|'):
            if out:
                break
            column = None
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if column is None:
            if cells and cells[0] == 'Hook' and 'Compare' in cells and (heading_hint is None or heading_hint in line):
                column = cells.index('Compare')
            continue
        m = re.fullmatch(r'`(\w+)`', cells[0]) if cells else None
        if m and len(cells) > column:
            value = cells[column].lower()
            out[m.group(1)] = 'yes' if value.startswith('yes') else 'no' if value.startswith('no') else value
    return out


def check(rows, documented):
    problems = []
    extracted = {r['hook']: r['compare'] for r in rows}
    for hook, value in extracted.items():
        if hook not in documented:
            problems.append(f'{hook}: missing in document')
        elif documented[hook] != value:
            problems.append(f'{hook}: document says {documented[hook]}, adapters say {value}')
    for hook in documented:
        if hook not in extracted:
            problems.append(f'{hook}: in document but not in registry')
    return problems


def markdown(rows):
    lines = ['| Hook | VA | Group | Compare | Source |', '| --- | --- | --- | --- | --- |']
    for r in rows:
        lines.append(f'| `{r["hook"]}` | {r["va"]:06x} | {r["group"]} | {r["compare"]} | {r["source"]} |')
    yes = sum(r['compare'] == 'yes' for r in rows)
    no = sum(r['compare'] == 'no' for r in rows)
    lines.append('')
    lines.append(f'{len(rows)} hooks: {yes} accept compare, {no} refuse it, {len(rows) - yes - no} unknown.')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--registry', default=PROXY / 'hook_registry.h')
    parser.add_argument('--proxy', default=PROXY, help='directory with the *_hooks.h adapters')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--check', help='audit document whose table Compare column must match')
    parser.add_argument('--hooks-doc', help='also check the registry table Compare column of hooks.md')
    args = parser.parse_args(argv)
    try:
        rows = audit(args.registry, args.proxy)
    except AuditError as e:
        print(f'compare_audit: {e}', file=sys.stderr)
        return 2
    status = 1 if any(r['compare'] == 'unknown' for r in rows) else 0
    if args.check or args.hooks_doc:
        problems = []
        if args.check:
            problems += [f'{args.check}: {p}' for p in check(rows, doc_compare_column(args.check))]
        if args.hooks_doc:
            problems += [f'{args.hooks_doc}: {p}' for p in check(rows, doc_compare_column(args.hooks_doc, 'c1_01-success calls'))]
        for p in problems:
            print(p)
        print(f'{len(rows)} hooks checked, {len(problems)} problems')
        return 1 if problems or status else 0
    sys.stdout.write(json.dumps(rows, indent=1) + '\n' if args.json else markdown(rows))
    return status


if __name__ == '__main__':
    sys.exit(main())
