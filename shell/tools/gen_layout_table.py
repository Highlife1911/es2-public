"""Generate shell/include/es2/shell/gui/layout_table.h from the layout table.

The header holds numbers only: classes, rectangles, constructor arguments,
the fields and methods the screen constructors apply, ESTEXT IDs, addresses
of executable strings and globals (never their contents), and indices into a
list of resource file names. Formulas (run-time values, vshell_widgets.md
section 4.4)
become indices into the JSON's own order of distinct formulas; the screen
that needs one reads it from shell/data/vshell_layout.json.

Usage: gen_layout_table.py [--check] <vshell_layout.json> <layout_table.h>
--check compares instead of writing (exit 1 when they differ).
"""
import json
import re
import sys

CLASSES = {
    'ESWindow': 'ES2_UI_ESWINDOW', 'ESRect': 'ES2_UI_RECT', 'ESButtonFont': 'ES2_UI_BUTTON_FONT',
    'ESButtonBitmap': 'ES2_UI_BUTTON_BITMAP', 'ESRadioButton': 'ES2_UI_RADIO',
    'ESRegionFill': 'ES2_UI_REGION_FILL', 'ESTitle': 'ES2_UI_TITLE', 'ESAlert': 'ES2_UI_ALERT',
    'ESMessage': 'ES2_UI_MESSAGE', 'ESBitmap': 'ES2_UI_BITMAP', 'ESGrid': 'ES2_UI_GRID',
    'ESDialog': 'ES2_UI_DIALOG', 'ESArm': 'ES2_UI_ARM', 'ESAnim2': 'ES2_UI_ANIM2', 'ESPal': 'ES2_UI_PAL',
    'ESQuad': 'ES2_UI_QUAD',
}
AFTER = {'hidden': 0, 'pending': 1, 'shown': 2}
METHODS = {'quad_columns': 1, 'quad_set_colour': 2, 'anim_add_frame': 3}
# The value kinds and edge kinds as numbers, in the order of es2/shell/gui/layout.h.
REFS = ['ES2_UI_REF_NONE', 'ES2_UI_REF_NUMBER', 'ES2_UI_REF_GLOBAL', 'ES2_UI_REF_ESTEXT', 'ES2_UI_REF_EXE_STRING',
        'ES2_UI_REF_DBA', 'ES2_UI_REF_DBM', 'ES2_UI_REF_CODE', 'ES2_UI_REF_FORMULA', 'ES2_UI_REF_SPRINTF',
        'ES2_UI_REF_FILE', 'ES2_UI_REF_ESTEXT_FORMULA', 'ES2_UI_REF_DBA_RECORD', 'ES2_UI_REF_SPRINTF_FORMULA']
EDGES = ['ES2_UI_EDGE_NUMBER', 'ES2_UI_EDGE_DISPLAY_RIGHT', 'ES2_UI_EDGE_DISPLAY_BOTTOM', 'ES2_UI_EDGE_FORMULA',
         'ES2_UI_EDGE_FILE']


class Gen:
    def __init__(self, layout):
        self.layout = layout
        self.files, self.formulas = [], []
        self.index = {}
        n = 0
        for s in layout['screens']:
            for c in s['controls']:
                self.index[c['id']] = n
                n += 1

    def file(self, name):
        if name not in self.files:
            self.files.append(name)
        return self.files.index(name)

    def formula(self, text):
        if text not in self.formulas:
            self.formulas.append(text)
        return self.formulas.index(text)

    @staticmethod
    def num(v):
        if isinstance(v, bool) or not isinstance(v, (int, str)):
            raise ValueError(f'not a number: {v!r}')
        return int(v, 16) if isinstance(v, str) else v

    def ref(self, v):
        """{kind, a, b} of an argument or field value."""
        if v is None:
            return ('ES2_UI_REF_NONE', 0, 0)
        if isinstance(v, int):
            return ('ES2_UI_REF_NUMBER', v, 0)
        if isinstance(v, str) and v.startswith('0x'):
            return ('ES2_UI_REF_CODE', int(v, 16), 0)
        if isinstance(v, dict):
            keys = sorted(v)
            if keys == ['global']:
                return ('ES2_UI_REF_GLOBAL', int(v['global'], 16), 0)
            if keys == ['estext'] and isinstance(v['estext'], dict):  # run-time ID (vshell_widgets.md 4.4 item 4)
                return ('ES2_UI_REF_ESTEXT_FORMULA', self.formula(v['estext']['formula']), 0)
            if keys == ['estext']:
                return ('ES2_UI_REF_ESTEXT', v['estext'], 0)
            if keys == ['exe_string', 'length']:
                return ('ES2_UI_REF_EXE_STRING', int(v['exe_string'], 16), v['length'])
            if keys == ['dba', 'frame'] and isinstance(v['frame'], dict):  # frame from a record (vshell_widgets.md 4.4 item 1)
                f = v['frame']
                if sorted(f) != ['file', 'record', 'size']:
                    raise ValueError(f'frame {f!r}')
                return ('ES2_UI_REF_DBA_RECORD', self.file(v['dba']),
                        f['record'] | (f['size'] << 8) | (self.file(f['file']) << 16))
            if keys == ['dba', 'frame']:
                return ('ES2_UI_REF_DBA', self.file(v['dba']), v['frame'])
            if keys == ['dbm']:
                return ('ES2_UI_REF_DBM', self.file(v['dbm']), 0)
            if keys == ['code']:
                return ('ES2_UI_REF_CODE', int(v['code'], 16), 0)
            if keys == ['formula']:
                return ('ES2_UI_REF_FORMULA', self.formula(v['formula']), 0)
            if keys == ['local_buffer']:
                lb = v['local_buffer']
                if sorted(lb) != ['sprintf', 'values'] or len(lb['values']) != 1:
                    raise ValueError(f'local buffer {lb!r}')
                value = lb['values'][0]
                if isinstance(value, str):  # a run-time value: the format and the formula
                    return ('ES2_UI_REF_SPRINTF_FORMULA', int(lb['sprintf'], 16), self.formula(value))
                return ('ES2_UI_REF_SPRINTF', int(lb['sprintf'], 16), value)
            if keys == ['file', 'record', 'size']:
                return ('ES2_UI_REF_FILE', v['record'], v['size'] | (self.file(v['file']) << 8))
        raise ValueError(f'unknown value {v!r}')

    def edge(self, v):
        if isinstance(v, int):
            return v, 'ES2_UI_EDGE_NUMBER'
        if isinstance(v, dict) and sorted(v) == ['formula']:
            if v['formula'] == 'display_right':
                return 639, 'ES2_UI_EDGE_DISPLAY_RIGHT'
            if v['formula'] == 'display_bottom':
                return 479, 'ES2_UI_EDGE_DISPLAY_BOTTOM'
            return self.formula(v['formula']), 'ES2_UI_EDGE_FORMULA'
        if isinstance(v, dict) and sorted(v) == ['file', 'record', 'size']:
            return v['record'] | (v['size'] << 8) | (self.file(v['file']) << 16), 'ES2_UI_EDGE_FILE'
        raise ValueError(f'rect edge {v!r}')

    def parent(self, c):
        p = c['parent']['control']
        if p == 'root':
            return 'ES2_UI_LAYOUT_ROOT'
        if p == 'display':
            return 'ES2_UI_LAYOUT_DISPLAY'
        return str(self.index[p])

    @staticmethod
    def fmt_ref(r):
        return '{%d,%d,%d}' % (REFS.index(r[0]), r[1], r[2])

    def run(self):
        controls, fields, methods, showhide, screens = [], [], [], [], []
        for si, s in enumerate(self.layout['screens']):
            first = len(controls)
            for c in s['controls']:
                if c['class'] not in CLASSES:
                    raise ValueError(f"{c['id']}: class {c['class']}")
                args = dict(c['args'])
                handler = self.num(c['handler'])
                rect = [self.edge(e) for e in c['rect']]
                sr = c['screen_rect']
                text = args.pop('label', None) if 'label' in args else args.pop('text', None)
                refs = [self.ref(args.pop(k, None)) for k in
                        ('font', 'bitmap_normal', 'bitmap_active', 'bitmap_extra', 'bitmap')]
                nums = []
                for k in ('colour', 'align', 'opaque', 'style', 'title_height', 'fill', 'arg8'):
                    v = args.pop(k, 0)
                    nums.append(self.num(v))
                args.pop('handler', None)
                if args:
                    raise ValueError(f"{c['id']}: arguments left {sorted(args)}")
                if c.get('text') is not None:
                    text = c['text']  # set by the screen after construction
                f0 = len(fields)
                for off, f in c.get('fields', {}).items():
                    r = self.ref(f['value'])
                    fields.append('{0x%x,%d,%s}' % (int(off, 16), f['size'], self.fmt_ref(r)))
                m0 = len(methods)
                for m in c.get('methods', []):
                    a = m['args']
                    mr = [('ES2_UI_REF_NONE', 0, 0)] * 5
                    mn = [0] * 8
                    if m['name'] == 'quad_columns':
                        mr = [self.ref(a['font'])] + [self.ref(a[f'text{i}']) for i in range(4)]
                        mn = [a['align0'], a['align1'], a['align2'], a['align3'], a['x1'], a['x2'], a['x3'],
                              a['unused']]
                    elif m['name'] == 'quad_set_colour':
                        mn = [a['column'], a['colour'], 0, 0, 0, 0, 0, 0]
                    elif m['name'] == 'anim_add_frame':
                        mr = [self.ref(a['bitmap'])] + mr[1:]
                    else:
                        raise ValueError(m['name'])
                    methods.append('{%d,0x%x,{%s},{%s}}' % (
                        METHODS[m['name']], int(m['site'], 16), ','.join(self.fmt_ref(r) for r in mr),
                        ','.join(str(int(v)) for v in mn)))
                h0 = len(showhide)
                for e in c.get('screen_show_hide', []):
                    what, site = e.split('@')
                    if what not in ('show', 'hide'):
                        raise ValueError(e)
                    showhide.append('{%d,0x%x}' % (what == 'show', int(site, 16)))
                glob = c['globals'][0] if c['globals'] and isinstance(c['globals'][0], str) and \
                    re.fullmatch(r'0x[0-9a-f]+', c['globals'][0]) else '0'
                per = c.get('per_record')
                per_n = 0 if not per else int(per.rsplit(' ', 1)[1])
                controls.append(
                    '    {%d,%s,%d,%s,%s,0x%x,0x%x,%s,0x%x,{%s},{%s},{%s},%d,%d,%s,{%s},{%s},%d,%d,%d,%d,%d,%d}, /* %s */'
                    % (si, CLASSES[c['class']], AFTER[c['after_construction']],
                       str(self.index[c['owner']]) if 'owner' in c else '-1', self.parent(c),
                       int(c['site'], 16), int(c['ctor'], 16), glob, handler,
                       ','.join(str(v) for v, _ in rect), ','.join(str(EDGES.index(k)) for _, k in rect),
                       ','.join(str(v) for v in (sr or [0, 0, 0, 0])), 1 if sr else 0, per_n,
                       self.fmt_ref(self.ref(text)), ','.join(self.fmt_ref(r) for r in refs),
                       ','.join(str(v) for v in nums), f0, len(fields) - f0, m0, len(methods) - m0, h0,
                       len(showhide) - h0, c['id']))
            screens.append('    {0x%x, %d, %d}, /* %s */' % (int(s['constructor'], 16), first,
                                                             len(controls) - first, s['name']))
        names = [s['name'] for s in self.layout['screens']]
        src = self.layout['source']
        out = []
        out.append('/* Generated by shell/tools/gen_layout_table.py from shell/data/vshell_layout.json')
        out.append(' * (VSHELL.EXE SHA-256 %s). Do not edit: regenerate.' % src['sha256'])
        out.append(' * Numbers only. es2/shell/gui/layout.h includes it for the screen enum and the')
        out.append(' * counts; shell/src/gui/screens.c defines ES2_UI_LAYOUT_DATA for the tables.')
        out.append(' * Value and edge kinds are the numbers of the ES2_UI_REF_* and ES2_UI_EDGE_* enums. */')
        out.append('#ifndef ES2_SHELL_GUI_LAYOUT_TABLE_H')
        out.append('#define ES2_SHELL_GUI_LAYOUT_TABLE_H')
        out.append('')
        out.append('@ENUM@')
        out.append('#define ES2_UI_LAYOUT_CONTROL_COUNT %d' % len(controls))
        out.append('#define ES2_UI_LAYOUT_FIELD_COUNT %d' % len(fields))
        out.append('#define ES2_UI_LAYOUT_METHOD_COUNT %d' % len(methods))
        out.append('#define ES2_UI_LAYOUT_SHOW_HIDE_COUNT %d' % len(showhide))
        out.append('#define ES2_UI_LAYOUT_FILE_COUNT %d' % len(self.files))
        out.append('#define ES2_UI_LAYOUT_FORMULA_COUNT %d' % len(self.formulas))
        out.append('')
        out.append('#ifdef ES2_UI_LAYOUT_DATA')
        out.append('static const es2_ui_layout_screen layout_screens[ES2_UI_LAYOUT_SCREEN_COUNT] = {')
        out.extend(screens)
        out.append('};')
        out.append('')
        out.append('static const es2_ui_layout_control layout_controls[ES2_UI_LAYOUT_CONTROL_COUNT] = {')
        out.extend(controls)
        out.append('};')
        out.append('')
        out.append('static const es2_ui_layout_field layout_fields[ES2_UI_LAYOUT_FIELD_COUNT] = {')
        out.extend('    %s,' % f for f in fields)
        out.append('};')
        out.append('')
        out.append('static const es2_ui_layout_method layout_methods[ES2_UI_LAYOUT_METHOD_COUNT] = {')
        out.extend('    %s,' % m for m in methods)
        out.append('};')
        out.append('')
        out.append('static const es2_ui_layout_show_hide layout_show_hide[ES2_UI_LAYOUT_SHOW_HIDE_COUNT] = {')
        out.extend('    %s,' % h for h in showhide)
        out.append('};')
        out.append('')
        out.append('/* Resource files named by the constructors (ES2_UI_REF_DBA/DBM/FILE). */')
        out.append('static const char *const layout_files[ES2_UI_LAYOUT_FILE_COUNT] = {')
        out.extend('    "%s",' % f.replace('\\', '\\\\') for f in self.files)
        out.append('};')
        out.append('#endif /* ES2_UI_LAYOUT_DATA */')
        out.append('')
        out.append('#endif')
        enum = ['/* Screens in construction order (generated with the table). */', 'enum {']
        enum.extend('    ES2_UI_SCREEN_%s = %d,' % (n.upper(), i) for i, n in enumerate(names))
        enum.append('    ES2_UI_LAYOUT_SCREEN_COUNT = %d' % len(names))
        enum.append('};')
        return '\n'.join(out) + '\n', '\n'.join(enum) + '\n'


def main(argv):
    check = argv[:1] == ['--check']
    if check:
        argv = argv[1:]
    if len(argv) != 2:
        print(__doc__)
        return 2
    with open(argv[0], encoding='utf-8') as f:
        layout = json.load(f)
    table, enum = Gen(layout).run()
    text = table.replace('@ENUM@\n', enum, 1)
    data = text.encode('utf-8')
    if check:
        with open(argv[1], 'rb') as f:
            same = f.read() == data
        print('layout table up to date' if same else 'layout table differs: regenerate')
        return 0 if same else 1
    with open(argv[1], 'wb') as f:
        f.write(data)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
