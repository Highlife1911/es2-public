# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Generate ABI-transparent x86 DirectDraw forwarding stubs from the installed OS DLL."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--readobj', type=Path, required=True)
p.add_argument('--dll', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
text = subprocess.check_output([str(a.readobj), '--coff-exports', str(a.dll)], text=True)
exports = []
for block in re.findall(r'Export \{(.*?)\}', text, re.S):
    ordinal = int(re.search(r'Ordinal: (\d+)', block).group(1))
    match = re.search(r'Name: ([^\r\n]*)', block)
    name = match.group(1).strip() if match else ''
    if name and not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
        raise SystemExit(f'Unsupported export name: {name!r}')
    exports.append((ordinal, name))
if not exports or len({n for n, _ in exports}) != len(exports):
    raise SystemExit('Missing or duplicate DirectDraw export ordinals')
a.out.mkdir(parents=True, exist_ok=True)
header = ['// Generated from this machine\'s 32-bit system DirectDraw; never hand-edit.',
          f'#define EXPORT_COUNT {len(exports)}',
          'static const WORD export_ordinals[EXPORT_COUNT] = {' + ','.join(str(n) for n, _ in exports) + '};',
          'static const char *const export_names[EXPORT_COUNT] = {' + ','.join('"' + name + '"' for _, name in exports) + '};']
assembly = ['.text']
definition = ['LIBRARY ddraw', 'EXPORTS']
for i, (ordinal, name) in enumerate(exports):
    assembly += [f'.globl _proxy_stub_{i}', f'_proxy_stub_{i}:', '  pushfl', '  pushal',
                 f'  pushl ${i}', '  call _resolve_export', '  addl $4, %esp', '  popal',
                 '  popfl', f'  jmp *_targets+{i * 4}']
    definition += [f'  {name or "ordinal_" + str(ordinal)}=proxy_stub_{i} @{ordinal}' + ('' if name else ' NONAME')]
for filename, lines in [('exports.h', header), ('forward.S', assembly), ('ddraw.def', definition)]:
    (a.out / filename).write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'Generated {len(exports)} system DirectDraw exports')