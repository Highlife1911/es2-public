# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share (es2shell.exe is a GUI-subsystem program and opens no window headless)
"""es2shell's frames on the tours' game_r path against VSHELL's.

The screen harness (//shell:screen_test) draws one screen per case; the
tour's WEAPONS and MISSION frames are compositions (the bay's tab bar, the
squad panel and the herc picture of the career, the screen drawn over the
previous frame), so they are compared here, on the whole shell.

The tour's state came from the clone calibration-r111 (its GAME_R career).
This test builds an equivalent state of its own, in a temporary game
directory with the CD's PREFS.CFG, GAMEFILE.STR and VOL\\LANG0.VOL:

1. a new career "Z1" with seed 12 (the shell's random seed of menu.ini;
   with it the three roster groups' selected pilots are DUGGAN, OSAKI and
   BROWN, as in the tour's career), CREW: order 1 = group 0 in bay 1,
   order 2 = group 1 in bay 2; BUILD: a Tomahawk in the sixth bay (the
   Razor of the fifth is a new career's); then MISSION, WEAPONS,
   MISSION and Rock & Roll, with -G: the clone's slot carries a German
   mission text (the preparation 0x417cb0 opens the .ger member when the
   language word 0x48235e is 1) and the squad file of a launch
   (0x40f68c), so this career is prepared in German and launched; the
   shell saves the career as slot 10 (GAME_R.SAV) when it ends (exit 2);
2. a second start, as the tours' game_r run (tools/reference-game/
   input-scripts/vshell/menu-bay.jsonl, arguments dummy -eggplant -X1 -a,
   PREFS.CFG's language 0: English buttons): the main menu, CONTINUE
   GAME, the bay, WEAPONS, MISSION, WEAPONS, MISSION (page 1 after its
   fly-over), OBJECTIVES, INTELLIGENCE, BRIEFING, a `snap` of each but
   the second WEAPONS.

Each snap's index and colour table hashes must equal frames.json's
(game_r: main_menu:start, service_bay:after_continue, weapons:from_bay,
mission:opened, mission:page1, mission:objectives, mission:intelligence,
mission:briefing) of the newest es2-data logs\\vshell-screens-* session, as
the screen harness finds it. The index hash is the snap's; the colour
table hash is the capture tour's form (red, green, blue per entry, as the screen
harness's), taken from a `dump` BMP's colour table in the temporary
directory, which is removed after a passing run.
The second MISSION's presents (vshell_ui.md section 42) are traced with a
16 ms clock and compared, consecutive repeats dropped, with the session's
runs\\game_r\\capture.jsonl present records (not checked without it).
Section 42.6: the tour runs with ES2_SHELL_PRESENT_LOG; the record's
frames, the host's wait record (the first paint's busy-wait ticks, not spent
headless) and, with a version 2 capture, VSHELL's wall time before the full
frame against those ticks are checked.
Skipped without ES2_DATA_ROOT or without a session.

Arguments: es2shell.exe [--node NODE RUN.MJS]
With --node NODE RUN.MJS, es2shell.exe is es2shell_wasm's module, run as
`NODE RUN.MJS MODULE ...` (web/node/run.mjs); variables the run sets go to the
module through --environment, since a wasm process has only that block.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

checks = 0
failures = []


def node_prefix(args):
    """Removes --node NODE RUN.MJS from args; the command prefix, or []."""
    if '--node' not in args:
        return []
    at = args.index('--node')
    prefix = [str(Path(p).resolve()) for p in args[at + 1:at + 3]]
    del args[at:at + 3]
    return prefix


def environment_option(prefix, env, path):
    """--environment FILE for the wasm shell: the variables env sets beyond the process's."""
    if not prefix or env is None:
        return []
    zero = bytes(1)
    block = b''.join(f'{k}={v}'.encode('latin-1') + zero for k, v in env.items() if os.environ.get(k) != v) + zero
    path.write_bytes(block)
    return ['--environment', str(path)]


PREFIX = []


def check(value, label):
    global checks
    checks += 1
    if not value:
        failures.append(label)
        print('FAIL', label, flush=True)


def scratch_base():
    base = Path(os.environ.get('TEST_TMPDIR') or tempfile.gettempdir()).resolve()
    for parent in base.parents:
        if parent.name == 'execroot':
            return parent.parent
    return base


def references(root):
    """frames.json of the newest session (ES2_VSHELL_SCREENS first, as screen_harness.c)."""
    explicit = os.environ.get('ES2_VSHELL_SCREENS')
    if explicit:
        path = Path(explicit)
        return path if path.name == 'frames.json' else path / 'frames.json'
    logs = (root / '..' / '..' / 'logs').resolve()
    sessions = sorted(p for p in logs.glob('vshell-screens-*') if (p / 'frames.json').is_file())
    return sessions[-1] / 'frames.json' if sessions else None


CAREER = ('step 200\nexpect screen=0\nclick main_menu.07\nstep 5\nexpect screen=1\ntype Z1\n'
          'click registration.10\nclick registration.14\nstep 5\nexpect screen=5 game=1\n'
          # CREW (service_bay.17): order rows crew.(12 + o), roster groups
          # crew.(2 + g), bay selectors squad_panel.(17 + 5 k)
          'click service_bay.17\nstep 3\nexpect word=6\n'
          'click crew.13\nclick crew.02\nclick squad_panel.22\n'
          'click crew.14\nclick crew.03\nclick squad_panel.27\nstep 3\n'
          # BUILD (service_bay.13): the sixth bay, the Tomahawk row, Build
          'click service_bay.13\nstep 5\nexpect word=4\nclick squad_panel.42\nstep 3\nexpect bay=5\n'
          'click build.22\nstep 3\nclick build.78\nstep 3\n'
          # MISSION, WEAPONS, MISSION, Rock & Roll (mission.27): the
          # launch writes data\\player.mec (0x40f68c) and the slot's
          # sav\\missn10.str and player10.mec, as in the clone's GAME_R state
          'click service_bay.19\nstep 5\nclick service_bay.09\nstep 5\nclick service_bay.19\nstep 5\n'
          'click mission.27\nstep 5\n')

KEYS = ('main_menu:start', 'service_bay:after_continue', 'weapons:from_bay', 'mission:opened',
        'mission:page1', 'mission:objectives', 'mission:intelligence', 'mission:briefing')


def tour(tmp):
    def snap(i):
        return f'snap {KEYS[i]}\ndump {tmp / f"snap{i}.bmp"}\n'
    return ('step 200\nexpect screen=0\n' + snap(0) +
            'click main_menu.09\nstep 5\nexpect screen=5\n' + snap(1) +
            'click service_bay.09\nstep 5\nexpect word=2\n' + snap(2) +
            'click service_bay.19\nstep 5\nexpect word=7\n' + snap(3) +
            # WEAPONS, MISSION again (page 1 and its
            # first entry's fly-over), then the three text buttons
            'click service_bay.09\nstep 5\nexpect word=2\n'
            # every 16 ms tick visited (0x465d5c), each present traced
            'clock 16\ntrace 1\n'
            'click service_bay.19\nstep 5\nexpect word=7\n'
            'trace 0\nclock 0\n' + snap(4) +
            'click mission.23\nstep 5\n' + snap(5) +
            'click mission.25\nstep 5\n' + snap(6) +
            'click mission.21\nstep 5\n' + snap(7))


def table_hash(path):
    """sha256 of a dump's colour table as red, green, blue per entry."""
    table = path.read_bytes()[54:54 + 1024]
    return hashlib.sha256(bytes(table[4 * k + c] for k in range(256) for c in (2, 1, 0))).hexdigest()


def run(shell, game, menu, name, script, extra=(), code=0, env=None):
    (game.parent / f'{name}.script').write_text(script, encoding='ascii')
    report = game.parent / f'{name}.report'
    own = environment_option(PREFIX, env, game.parent / f'{name}.environment')
    result = subprocess.run([*PREFIX, str(shell), *own, '--menu', str(menu), '--headless', '--script',
                             str(game.parent / f'{name}.script'), '--report', str(report),
                             'dummy', '-eggplant', '-X1', '-a', *extra], cwd=game, capture_output=True, timeout=600,
                            env=env)
    text = report.read_text('latin-1') if report.exists() else ''
    check(result.returncode == code and 'errors=0' in text, f'{name}: es2shell exit {code}, no script error\n{text}')
    return text


def main(shell, cd, frames):
    refs = {}
    for s in json.loads(frames.read_text('utf-8'))['screens']:
        if s['run'] == 'game_r':
            refs[f"{s['screen']}:{s['state']}"] = (s['index_sha256'].lower(), s['palette_sha256'].lower())
    tmp = Path(tempfile.mkdtemp(prefix='gui-tour-', dir=scratch_base()))
    game, menu = tmp / 'game', tmp / 'menu'
    for sub in ('DATA', 'SAV', 'VOL'):
        (game / sub).mkdir(parents=True)
    shutil.copyfile(cd / 'DATA' / 'PREFS.CFG', game / 'DATA' / 'PREFS.CFG')
    shutil.copyfile(cd / 'SAV' / 'GAMEFILE.STR', game / 'SAV' / 'GAMEFILE.STR')
    shutil.copyfile(cd / 'VOL' / 'LANG0.VOL', game / 'VOL' / 'LANG0.VOL')
    menu.mkdir()
    lines = ['[Menu]', f'game_dir = {game}', f'cd_root = {cd}', 'hangar = lowest-free', 'seed = 12', 'chain_dir = ',
             'online_manual = text']
    (menu / 'menu.ini').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    # -G: the German preparation (0x417cb0 picks .ger by 0x48235e); the
    # launch ends the process with exit code 2
    run(shell, game, menu, 'career', CAREER, extra=('-G',), code=2)
    for name in ('GAME_R.SAV', 'missn10.str', 'player10.mec'):
        check((game / 'SAV' / name).is_file(), f'career: slot 10 file {name} written')
    # The tour with the wall-time record of the presents on (headless: no wait is spent)
    record = tmp / 'present.jsonl'
    report = run(shell, game, menu, 'tour', tour(tmp), env=dict(os.environ, ES2_SHELL_PRESENT_LOG=str(record)))
    snaps = {}
    for line in report.splitlines():
        if line.startswith('snap '):
            name, frame = line.split()[1:3]
            snaps[name] = frame[len('frame='):]
    for i, key in enumerate(KEYS):
        want = refs.get(key)
        dump = tmp / f'snap{i}.bmp'
        got = (snaps[key], table_hash(dump)) if key in snaps and dump.is_file() else None
        check(want is not None, f'frames.json has game_r:{key}')
        check(got is not None, f'snap {key} reported')
        if want and got:
            check(got[0] == want[0], f'{key}: index hash {got[0][:12]} = VSHELL {want[0][:12]}')
            check(got[1] == want[1], f'{key}: colour table hash {got[1][:12]} = VSHELL {want[1][:12]}')
        print(f'snap {key} frame={got[0] if got else "-"} reference={want[0] if want else "-"}', flush=True)
    animation(frames.parent / 'runs' / 'game_r' / 'capture.jsonl', report)
    present_record(record, report, frames.parent / 'runs' / 'game_r' / 'capture.jsonl')
    return tmp


def animation(capture, report):
    """The second MISSION's frames, present by present, against the capture tour's records.

    The capture tour (every=0) writes a present record when the frame or colour table
    changed; the records of the journal step that clicks MISSION on WEAPONS
    and of its wait are the screen change and the fly-over, one record per
    distinct frame. es2shell's traced presents (clock 16: each tick
    visited), consecutive repeats dropped, must be the same sequence."""
    if not capture.is_file():
        print(f'animation: no {capture}, not checked')
        return
    records = [json.loads(line) for line in capture.read_text('utf-8').splitlines() if line.strip()]
    click = next((r['step'] for r in records if r['kind'] == 'step' and r['op'] == 'click'
                  and r['screen'] == 'weapons' and r['state'] == 'from_mission'), None)
    check(click is not None, 'capture: the click on MISSION from WEAPONS')
    want = [r['index_sha256'].lower() for r in records
            if r['kind'] == 'present' and click is not None and r['step'] in (click, click + 1)]
    got = []
    for line in report.splitlines():
        if line.startswith('present '):
            frame = line.rsplit('frame=', 1)[1]
            if not got or got[-1] != frame:
                got.append(frame)
    check(len(want) > 100, f'capture: {len(want)} present records of the fly-over')
    first = next((i for i, (a, b) in enumerate(zip(want, got)) if a != b), min(len(want), len(got)))
    check(got == want, f'animation: {len(got)} distinct frames = VSHELL {len(want)} (first difference at {first})')
    print(f'animation frames={len(got)} reference={len(want)} first-difference={first}', flush=True)


# es2shell's present record (ES2_SHELL_PRESENT_LOG) and VSHELL's capture.jsonl (version 2), which
# have the same shape; tour_windowed.py prints the same summary for windowed runs.
MAP_AREA = 'bb8ee118'
FULL_FRAME = '832f906f'
FRAME_KINDS = ('present', 'clear', 'snap')


def read_records(path):
    records = []
    for line in Path(path).read_text('utf-8').splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            continue   # a line the writer did not finish
        if isinstance(record, dict):
            records.append(record)
    return records


def summarise(records, map_area=MAP_AREA, full_frame=FULL_FRAME):
    """The intervals around the first paint (a dict), or None when the record lacks them."""
    start = next((r for r in records if r.get('kind') == 'start'), {})
    frequency = start.get('qpc_frequency')
    frames = [r for r in records if r.get('kind') in FRAME_KINDS and isinstance(r.get('qpc'), int)]
    i = next((n for n, r in enumerate(frames) if (r.get('index_sha256') or '').startswith(map_area)), None)
    if not isinstance(frequency, int) or frequency <= 0 or i is None or i + 2 >= len(frames):
        return None
    full, flight = frames[i + 1], frames[i + 2]
    if not (full.get('index_sha256') or '').startswith(full_frame):
        return None
    ms = lambda a, b: (b['qpc'] - a['qpc']) * 1000.0 / frequency
    out = {'source': start.get('source', 'vshell'), 'map_hold_ms': ms(frames[i], full),
           'full_hold_ms': ms(full, flight), 'map_to_flight_ms': ms(frames[i], flight),
           'full_presents': full.get('presents'), 'flight_presents': flight.get('presents'),
           'flight_index': (flight.get('index_sha256') or '')[:8]}
    wait = next((r for r in records if r.get('kind') == 'wait' and frames[i]['seq'] < r.get('seq', -1) < full['seq']),
                None)
    if wait:
        out['wait_ticks'] = wait.get('ticks')
        out['wait_wall'] = wait.get('wall')
        out['wait_ms'] = (wait['qpc_end'] - wait['qpc']) * 1000.0 / frequency
    return out


def describe(path, summary):
    if summary is None:
        return f'{path}: no map-area record {MAP_AREA} followed by {FULL_FRAME} and a third frame record'
    text = (f"{path}: {summary['source']}: map area {MAP_AREA} held {summary['map_hold_ms']:.1f} ms, "
            f"full frame {FULL_FRAME} held {summary['full_hold_ms']:.1f} ms ({summary['flight_presents']} presents), "
            f"map area to the first flight frame {summary['flight_index']} {summary['map_to_flight_ms']:.1f} ms")
    if 'wait_ticks' in summary:
        text += f"; host wait {summary['wait_ticks']} ticks, wall={summary['wait_wall']}, {summary['wait_ms']:.1f} ms"
    return text


def present_record(record, report, capture):
    """es2shell's present record (ES2_SHELL_PRESENT_LOG) on the tour (vshell_ui.md 42.6).

    The traced presents' frames, consecutive repeats dropped, are a run of the
    record's frame records; the host's single wait record lies between the
    map-area record and the full frame and holds the first paint's busy-wait
    ticks W, not spent (headless). With a version 2 capture, VSHELL's wall
    time from its record before the full frame (step 6's palette upload) to
    the full frame lies within W - 1 ticks (the wait starts inside a tick)
    and W + 1 ticks (the drawing after the wait)."""
    records = read_records(record) if record.is_file() else []
    start = records[0] if records else {}
    check(start.get('kind') == 'start' and start.get('schema') == 2 and start.get('source') == 'es2shell'
          and start.get('headless') == 1 and isinstance(start.get('qpc_frequency'), int),
          'present record: a version 2 start record of es2shell, headless')
    traced = []
    for line in report.splitlines():
        if line.startswith('present '):
            frame = line.rsplit('frame=', 1)[1]
            if not traced or traced[-1] != frame:
                traced.append(frame)
    recorded = [r['index_sha256'] for r in records if r.get('kind') in ('present', 'clear')]
    found = any(recorded[i:i + len(traced)] == traced for i in range(len(recorded) - len(traced) + 1))
    check(len(traced) > 100 and found,
          f'present record: the {len(traced)} traced frames are a run of its {len(recorded)} frame records')
    summary = summarise(records)
    check(summary is not None and summary.get('wait_wall') == 0 and summary.get('wait_ticks', 0) > 20
          and summary['flight_index'] == 'a8ee6a56',
          f'present record: map area, host wait, full frame, first flight frame ({summary})')
    waits = [r for r in records if r.get('kind') == 'wait']
    check(len(waits) == 1, f'present record: one wait record ({len(waits)})')
    print(describe(record.name, summary), flush=True)
    if not summary or not capture.is_file():
        return
    vshell = read_records(capture)
    if not vshell or vshell[0].get('schema') != 2:
        print('present record: the capture has no times (record version 1), not compared')
        return
    click = next((r['step'] for r in vshell if r['kind'] == 'step' and r['op'] == 'click'
                  and r['screen'] == 'weapons' and r['state'] == 'from_mission'), None)
    full = next((n for n, r in enumerate(vshell) if n and r.get('kind') == 'present'
                 and r.get('step') in (click, (click or 0) + 1)
                 and r.get('index_sha256', '').lower().startswith(FULL_FRAME)), None)
    check(full is not None, 'capture: the full frame 832f906f after the click on MISSION')
    if full is None:
        return
    gap = (vshell[full]['qpc'] - vshell[full - 1]['qpc']) * 1000.0 / vshell[0]['qpc_frequency']
    ticks = summary['wait_ticks']
    check((ticks - 1) * 16 <= gap <= (ticks + 1) * 16,
          f'capture: VSHELL wall time {gap:.1f} ms before the full frame within {ticks - 1}..{ticks + 1} ticks')
    print(f'present record: VSHELL {gap:.1f} ms before the full frame, es2shell wait {ticks} ticks', flush=True)
    # VSHELL's presents count the BitBlts at 0x406e52 (the flush 0x41fe49 one
    # per dirty rectangle), es2shell's blits the same per rectangle: the tab
    # bar, the map area and the full frame (three unchanged presents after
    # the map area's change) equal. The fly-over's counts after that depend
    # on the loop's speed per tick and are not compared.
    want = [(r['index_sha256'].lower()[:8], r.get('presents')) for r in vshell[:full + 1]
            if r.get('kind') == 'present' and r.get('step') == vshell[full].get('step')][-3:]
    frames = [r for r in records if r.get('kind') in ('present', 'clear')]
    i = next((n for n, r in enumerate(frames) if r['index_sha256'].startswith(MAP_AREA)), None)
    got = [(r['index_sha256'][:8], r.get('blits')) for r in frames[i - 1:i + 2]] if i else []
    check(len(want) == 3 and got == want, f'present record: blits {got} = VSHELL presents {want}')


if __name__ == '__main__':
    root = os.environ.get('ES2_DATA_ROOT')
    if not root or not (Path(root) / 'VER95' / 'VSHELL.EXE').exists():
        print('SKIP: no ES2_DATA_ROOT')
        raise SystemExit(0)
    frames = references(Path(root))
    if not frames or not frames.is_file():
        print('SKIP: no vshell-screens session (frames.json)')
        raise SystemExit(0)
    print(f'references: {frames}')
    args = sys.argv[1:]
    PREFIX = node_prefix(args)
    tmp = main(Path(args[0]).resolve(), Path(root).resolve(), frames)
    if not failures:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'{checks} checks, {len(failures)} failed')
    raise SystemExit(1 if failures else 0)
