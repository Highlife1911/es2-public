# window-lint: allow-file inherits console: developer/worker CLI run from a console, from check.ps1 or from a Bazel test; the children share its (possibly hidden) console
"""Differential VSHELL matrix; stdlib, read-only inputs, hash-only reports.

Coverage profile: complete missions/languages/modes/source baseline; seed,
difficulty, training and authored-comparison axes about that baseline. Use
--config JSON for an explicit Cartesian product (potentially very large).
Loadout profile: generated SAVs (every Herc as player, every hardpoint-legal
item, every item ID, team sizes 0..3) prepared by both implementations, plus
the original reader/writer round trip of each SAV. default-sav: our CD-default
career SAV against the original writers.
No game process, installation write, stored asset or embedded oracle hash.
"""
import argparse
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import subprocess
import time

FIELDS = ('script.dat', 'MISSION.STR', 'mission.var', 'player.mec', 'rng', 'selectors')


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def packets(data):
    result, at = [], 0
    while at < len(data):
        if data[at:at+4] != b'ES2P':
            raise ValueError('bad original packet magic')
        at += 4
        blocks = []
        def block():
            nonlocal at
            if at + 4 > len(data):
                raise ValueError('truncated original packet')
            size, = struct.unpack_from('<I', data, at)
            at += 4
            if not 0 < size <= 16*1024*1024 or at + size > len(data):
                raise ValueError('bad original packet extent')
            blocks.append(data[at:at+size])
            at += size
        for _ in range(5):
            block()
        meta = json.loads(blocks[0])
        if meta.get('emitted_save'):
            block()
        buffers = dict(zip(FIELDS[:4], blocks[1:5]))
        if meta.get('emitted_save'):
            buffers['sav'] = blocks[5]
        buffers['rng'] = bytes.fromhex(meta['output_rng_state'])
        buffers['selectors'] = bytes.fromhex(meta['output_reset_selectors'])
        result.append(buffers)
    return result


def original_batch(worker, cd, language, requests, emit=False):
    """A fatal original assertion ends one private worker, never the sweep.

    Completed packets precede the failing case; remaining independent cases
    run in a fresh worker. Retained pairs are handled separately.
    """
    out = []
    while len(out) < len(requests):
        r = subprocess.run([str(worker), '--cd-root', str(cd), '--language',
                            dict(ENG='E', FRE='F', GER='G')[language], '--batch',
                            *(['--emit-save'] if emit else [])],
                           input=''.join(requests[len(out):]).encode('ascii'),
                           capture_output=True, timeout=120)
        decoded = packets(r.stdout)
        out.extend(decoded)
        if r.returncode:
            diagnostic = r.stderr.decode('utf-8', errors='replace')
            evidence = [line.strip() for line in diagnostic.splitlines()
                        if line.startswith(('ASSERT ', 'FAULT ', 'FAIL ', 'TRAP '))]
            if len(out) >= len(requests):
                raise ValueError('worker failed after last response')
            out.append({'error': '; '.join(evidence), 'exit': r.returncode})
        elif len(out) != len(requests):
            raise ValueError('worker returned too few packets')
    return out


def field_name(name, data, offset):
    if name == 'mission.var':
        return f'variable[{offset//2}]'
    if name == 'rng':
        return f'word[{offset//2}]' if offset < 112 else ('cursor_a' if offset == 112 else 'cursor_b')
    if name == 'selectors':
        return f'reset_selector[{offset//2}]'
    if name == 'player.mec':
        if offset < 4:
            return 'header' if offset < 2 else 'active_count'
        try:
            count, = struct.unpack_from('<H', data, 2)
            at = 4
            for i in range(count):
                capacity, = struct.unpack_from('<H', data, at+6)
                end = at+76+4*capacity
                if offset < end:
                    r = offset-at
                    if r < 8:
                        return f'actor[{i}].' + ('pilot_variant','pilot_rank','vehicle','capacity')[r//2]
                    if r < 8+2*capacity:
                        return f'actor[{i}].item[{(r-8)//2}]'
                    if r < 8+4*capacity:
                        return f'actor[{i}].secondary[{(r-8-2*capacity)//2}]'
                    return f'actor[{i}].condition_or_zero[{(r-8-4*capacity)//2}]'
                at = end
            return 'availability_count' if offset < at+2 else f'availability[{offset-at-2}]'
        except struct.error:
            return 'malformed_MEC'
    if name == 'script.dat':
        if offset < 20:
            return f'header[{offset//2}]'
        at = 20
        widths = {6:12,7:2,9:6,10:74,11:24,12:134,13:92,14:52,15:14,16:156,17:54,18:2}
        try:
            for family in range(6,19):
                n, = struct.unpack_from('<H',data,at)
                if offset < at+2:
                    return f'list[{family}].count'
                at += 2
                for i in range(n):
                    size = 2+2*struct.unpack_from('<H',data,at)[0] if family==8 else widths[family]
                    if offset < at+size:
                        return f'list[{family}].record[{i}]+{offset-at}'
                    at += size
        except struct.error:
            pass
    return 'size_or_framing' if offset >= len(data) else f'byte[{offset}]'


def compare(cli, ours, reference):
    if 'error' in ours or 'error' in reference:
        # These exact original failure sites delimit the safe preparation API.
        known = ('caller=0041ce61',)
        bounded = 'error' in ours and 'error' in reference and any(x in reference['error'] for x in known)
        return dict(equal=False, outside_contract=bounded,
                    own_error=ours.get('error'), original_error=reference.get('error'))
    files = {}
    for name in FIELDS:
        a,b=ours[name],reference[name]
        row=dict(equal=a==b, size=len(a), reference_size=len(b),
                 sha256=cli.sha(a), reference_sha256=cli.sha(b))
        if a!=b:
            first=next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),min(len(a),len(b)))
            row.update(first_difference=first,field=field_name(name,b,first))
        files[name]=row
    return dict(equal=all(x['equal'] for x in files.values()),files=files)


def signed_word(value):
    return ((value+32768)&65535)-32768


def comparison_banks(core, cd):
    r=subprocess.run([str(core),str(cd),'--comparisons'],capture_output=True,text=True,check=True)
    seen=set()
    for line in r.stdout.splitlines():
        g,p,name,record,index,literal,op,parent=line.split()
        g,p,record,index,literal,op,parent=map(int,(g,p,record,index,literal,op,parent))
        for value in sorted({signed_word(literal-1),literal,signed_word(literal+1),-32768,32767}):
            key=(g,p,index,value)
            if key in seen:continue
            seen.add(key)
            bank=bytearray(2000);struct.pack_into('<h',bank,2*index,value)
            yield (g,p,name),f'var{index}={value}',bytes(bank),dict(record=record,operator=op,parent=parent,literal=literal)


def run(cli, core, worker, cd, cases, *, batch_size=128, progress=None):
    tables=cli.default_tables(cd);rngs={};rows=[]
    for language, group in itertools.groupby(cases,key=lambda c:c['language']):
        pending=[]
        def flush():
            requests=[]
            for c in pending:
                seed=c.get('seed',0)
                if seed not in rngs:rngs[seed]=cli.initial_rng(cd,seed)
                t=c.get('training',(0,0,0,0,0))
                requests.append(cli.request(group=c['group'],position=c['position'],mode=c['mode'],
                    language=language,difficulty=c.get('difficulty',0),rng=rngs[seed],
                    bank=c.get('bank',bytes(2000)),save=c.get('save'),tables=tables,
                    mode0_values=(*t[:3],t[4]),training_vehicle=t[3],
                    preset_index=c.get('preset_index',0),preset_flag=c.get('preset_flag',0)))
            mine=cli.prepare_batch(core,cd,requests)
            original=original_batch(worker,cd,language,requests)
            for c,a,b in zip(pending,mine,original):
                label={k:v for k,v in c.items() if k not in ('save','bank')}
                label.update(compare(cli,a,b));rows.append(label)
            pending.clear()
            if progress:progress(rows)
        for case in group:
            pending.append(case)
            if len(pending)>=batch_size:flush()
        if pending:flush()
    return rows


def matrix(cli,core,cd,saves,profile,config=None):
    missions=cli.campaign(core,cd)
    if profile=='tiny':missions=missions[:1]+[m for m in missions if m[0]==1][:1]
    sources=[('cd-defaults',None),*saves]
    langs=['ENG','FRE','GER'] if profile!='tiny' else ['GER']
    if config:
        missions=[m for m in missions if m[2] in config.get('missions',[x[2] for x in missions])]
        langs=config.get('languages',langs)
        sources=[s for s in sources if s[0] in config.get('state_sources',[s[0] for s in sources])]
    banks=[('zero',bytes(2000))]
    # Save banks are parsed by the same bounded evidence reader used by acceptance.
    acceptance=module(Path(cli.__file__).parents[1]/'tests/real_acceptance.py','acceptance_inputs')
    for name,data in saves:banks.append((name,acceptance.save_inputs(data)['bank']))
    if config:
        banks=[b for b in banks if b[0] in config.get('banks',[b[0] for b in banks])]
        for path in config.get('bank_files',[]):
            payload=Path(path).read_bytes()
            if len(payload)!=2000:raise ValueError('bank file must have 2000 bytes')
            banks.append((str(path),payload))
    base=[]
    for lang,(g,p,name),mode,(source,save) in itertools.product(langs,missions,config.get('modes',[0,1]) if config else (0,1),sources):
        base.append(dict(language=lang,group=g,position=p,mission=name,mode=mode,source=source,save=save,axis='baseline'))
    yield from base
    if profile in ('tiny','baseline'):return
    if config:
        for c,seed,difficulty,training,flag,index,(bankname,bank) in itertools.product(base,config.get('seeds',[0]),config.get('difficulties',[0]),config.get('training_options',[[0]*5]),config.get('preset_flags',[0]),config.get('preset_indices',[0]),banks):
            yield dict(c,axis='cartesian',seed=seed,difficulty=difficulty,training=training,preset_flag=flag,preset_index=index,bank_name=bankname,bank=bank)
        if config.get('authored_comparisons',False):
            selected={(m[0],m[1]) for m in missions}
            for (g,p,name),bankname,bank,evidence in comparison_banks(core,cd):
                if (g,p) not in selected:continue
                for c in base:
                    if (c['group'],c['position'])==(g,p):
                        yield dict(c,axis='comparison',bank_name=bankname,bank=bank,comparison=evidence)
        return
    # Each seed on every mission, mode and source in German; baseline covers
    # all languages. Assertion-only mode-1 training rows need no seed replay.
    seeds=[*range(128),32767,32768,32769,65535]
    for c in base:
        if c['language']!='GER' or (c['group']==0 and c['mode']==1):continue
        for seed in seeds[1:]:yield dict(c,axis='seed',seed=seed)
        for difficulty in (-32768,-1,1,2,3,32767):yield dict(c,axis='difficulty',difficulty=difficulty)
        for bankname,bank in banks[1:]:yield dict(c,axis='bank',bank_name=bankname,bank=bank)
        if c['mode']==0:
            for vehicle in range(9):
                yield dict(c,axis='preset',training=(1,1,2,vehicle,255),preset_index=4)
            for flag,index in ((0,3),(1,4),(255,32767),(0,-32768)):
                yield dict(c,axis='preset-predicate',preset_flag=flag,preset_index=index)
            for training in ((0,0,0,0,0),(255,255,255,8,255),(1,0,2,4,1)):
                yield dict(c,axis='training',training=training)
    for (g,p,name),bankname,bank,evidence in comparison_banks(core,cd):
        for mode,(source,save) in itertools.product((0,1),sources):
            if g==0 and mode==1:continue
            yield dict(language='GER',group=g,position=p,mission=name,mode=mode,
                       source=source,save=save,axis='comparison',bank_name=bankname,bank=bank,comparison=evidence)


# ---- Loadout profile -------------------------------------------------------
SWEEP_MISSIONS = ('C1_01', 'C1_04', 'C1_05', 'C1_06', 'C2_09', 'C3_01', 'C3_02', 'C3_03', 'C3_04',
                  'C3_05', 'C3_06', 'C3_07', 'C3_08', 'C3_09', 'C3_10', 'C4_01', 'C4_08', 'C5_02', 'C5_07')


def loadout_module(cli):
    return module(Path(cli.__file__).with_name('es2loadout.py'), 'sweep_loadout_cli')


def loadout_plans(catalog):
    """Player Herc loadouts. Every Herc is used; rotating variants place every
    ARM-legal armory item on every slot that admits it at least once; sparse
    and empty bays; then every item ID 1..32 on every slot of the largest Herc
    (hangar rules not applied, odd IDs with explicit secondary words)."""
    plans = []
    for position, (vid, capacity) in enumerate(catalog.vehicles):
        allowed = [[item for item in sorted(catalog.armory) if slot in catalog.hardpoints[position].get(item, [])]
                   for slot in range(capacity)]
        for variant in range(max([len(a) for a in allowed] + [1])):
            mounts = [(slot, a[(variant + slot) % len(a)], None) for slot, a in enumerate(allowed) if a]
            plans.append(dict(label=f'herc{vid}-v{variant}', herc=vid, mounts=mounts, ui=True))
        plans.append(dict(label=f'herc{vid}-sparse', herc=vid, ui=True,
                          mounts=[m for m in plans[-1]['mounts'] if m[0] % 2 == 0]))
        plans.append(dict(label=f'herc{vid}-empty', herc=vid, mounts=[], ui=True))
    vid, capacity = max(catalog.vehicles, key=lambda v: v[1])
    for item in range(1, 33):
        plans.append(dict(label=f'item{item}', herc=vid, ui=False,
                          mounts=[(slot, item, (item * 7 + slot) & 0xffff if item % 2 else None)
                                  for slot in range(capacity)]))
    return plans


def loadout_plan(plans, i):
    """Case i: player plan i, team size i % 4 from roster groups (i+k) % 3
    with varying pilot indices and hangar-legal member loadouts."""
    legal = [p for p in plans if p['ui']]
    player = plans[i % len(plans)]
    team = []
    for k in range(i % 4):
        member = legal[(i * 7 + k * 13) % len(legal)]
        team.append(((i + k) % 3, (i * 5 + k) % 12, member['herc'], member['mounts']))
    flags = [1] * 33 if i % 7 == 3 else [0] * 33 if i % 7 == 5 else None
    return player, dict(player=(player['herc'], player['mounts']), team=team, flags=flags,
                        zero_variables=i % 5 == 0)


def loadout_cases(cli, core, cd, saves, tiny=False):
    loadout = loadout_module(cli)
    catalog = loadout.Catalog(core, cd)
    plans = loadout_plans(catalog)
    campaign = {name: (g, p) for g, p, name in cli.campaign(core, cd)}
    missions = [m for m in SWEEP_MISSIONS if m in campaign]
    count = len(plans)
    if tiny:
        missions, count = missions[:2], 6
    for i in range(count):
        player, plan = loadout_plan(plans, i)
        base = dict(plan_index=i, player_plan=player['label'], hangar_rules=player['ui'], plan=plan)
        chosen = missions if not tiny else missions[:1]
        for mission in chosen:
            yield dict(base, language='GER', mission=mission, campaign=campaign[mission], source='cd-defaults',
                       save=None, seed=0, axis='loadout')
        if tiny:
            continue
        for name, data in saves:
            for mission in (missions[i % len(missions)], missions[(i + 7) % len(missions)]):
                yield dict(base, language='GER', mission=mission, campaign=campaign[mission], source=name,
                           save=data, seed=0, axis='loadout-base')
        for language in ('ENG', 'FRE'):
            mission = missions[(i + 3) % len(missions)]
            yield dict(base, language=language, mission=mission, campaign=campaign[mission],
                       source='cd-defaults', save=None, seed=0, axis='loadout-language')
        mission = missions[(i + 11) % len(missions)]
        yield dict(base, language='GER', mission=mission, campaign=campaign[mission], source='cd-defaults',
                   save=None, seed=(i * 4099 + 1) % 65536, axis='loadout-seed')


def loadout_runs(cli, core, worker, cd, saves, tiny=False, batch_size=128, progress=None):
    loadout = loadout_module(cli)
    catalog = loadout.Catalog(core, cd)
    tables = cli.default_tables(cd)
    rows, rngs = [], {}
    cases = sorted(loadout_cases(cli, core, cd, saves, tiny), key=lambda c: c['language'])
    for language, group in itertools.groupby(cases, key=lambda c: c['language']):
        group = list(group)
        built = loadout.build_saves(core, cd, [loadout.save_request(catalog, language, *c['campaign'], c['plan'],
                                                                    c['save']) for c in group])
        for start in range(0, len(group), batch_size):
            chunk, sav_chunk = group[start:start + batch_size], built[start:start + batch_size]
            requests, kept = [], []
            for c, sav in zip(chunk, sav_chunk):
                label = {k: v for k, v in c.items() if k not in ('save', 'plan', 'campaign')}
                label.update(group=c['campaign'][0], position=c['campaign'][1],
                             team=len(c['plan']['team']), zero_variables=c['plan']['zero_variables'],
                             explicit_flags=c['plan']['flags'] is not None,
                             player_items=sorted({m[1] for m in c['plan']['player'][1]}),
                             team_items=sorted({m[1] for t in c['plan']['team'] for m in t[3]}),
                             team_hercs=[t[2] for t in c['plan']['team']], player_herc=c['plan']['player'][0])
                if isinstance(sav, str):
                    rows.append(dict(label, equal=False, build_error=sav))
                    continue
                seed = c['seed']
                if seed not in rngs:
                    rngs[seed] = cli.initial_rng(cd, seed)
                kept.append((label, sav))
                requests.append(cli.request(group=c['campaign'][0], position=c['campaign'][1], mode=1,
                                            language=language, rng=rngs[seed], tables=tables, save=sav))
            ours = cli.prepare_batch(core, cd, requests)
            theirs = original_batch(worker, cd, language, requests, emit=True)
            for (label, sav), a, b in zip(kept, ours, theirs):
                row = dict(label, sav_sha256=cli.sha(sav), **compare(cli, a, b))
                row['sav_roundtrip'] = b.get('sav') == sav
                if 'files' in row:
                    row['effective_team'] = struct.unpack_from('<H', a['player.mec'], 2)[0] - 1
                row['equal'] = row['equal'] and row['sav_roundtrip']
                rows.append(row)
            if progress:
                progress(rows)
    return rows


def default_sav_runs(cli, core, worker, cd, tiny=False):
    """Our CD-default career SAV against the original writers after the
    original new-career loaders, one-shot with the original seed-0 roster."""
    loadout = loadout_module(cli)
    catalog = loadout.Catalog(core, cd)
    rows = []
    names = ('In vitro', 'X', 'Commander', 'A' * 29) if not tiny else ('Commander',)
    ranks = (0, 1, 3, 65535) if not tiny else (2,)
    positions = ((1, 0), (3, 9), (4, 7), (5, 9)) if not tiny else ((3, 8),)
    for language in (('ENG', 'FRE', 'GER') if not tiny else ('GER',)):
        for name, rank, (g, p) in itertools.product(names, ranks, positions):
            ours = loadout.default_save(core, cd, catalog, language, g, p, name, rank)
            r = subprocess.run([str(worker), '--cd-root', str(cd), '--campaign', f'{g}:{p}', '--language', language[0],
                                '--player-name', name, '--player-rank', str(rank), '--emit-save', '--pipe'],
                               capture_output=True, timeout=120)
            if r.returncode:
                rows.append(dict(axis='default-sav', language=language, name=name, rank=rank, group=g,
                                 position=p, equal=False, error=r.stderr.decode(errors='replace')[-400:]))
                continue
            theirs, = packets(r.stdout)
            rows.append(dict(axis='default-sav', language=language, name=name, rank=rank, group=g, position=p,
                             size=len(ours), sha256=cli.sha(ours), equal=theirs.get('sav') == ours))
    return rows


def loadout_summary(rows):
    result = summary(rows)
    loadout_rows = [r for r in rows if r['axis'].startswith('loadout')]
    result.update(
        sav_roundtrip_mismatches=sum(not r.get('sav_roundtrip', True) for r in loadout_rows),
        build_errors=sum('build_error' in r for r in loadout_rows),
        player_hercs=sorted({r['player_herc'] for r in loadout_rows}),
        team_hercs=sorted({h for r in loadout_rows for h in r['team_hercs']}),
        player_items=sorted({i for r in loadout_rows for i in r['player_items']}),
        team_items=sorted({i for r in loadout_rows for i in r['team_items']}),
        requested_team_sizes=sorted({r['team'] for r in loadout_rows}),
        effective_team_sizes=sorted({r['effective_team'] for r in loadout_rows if 'effective_team' in r}),
        missions=sorted({r['mission'] for r in loadout_rows}),
        sources=sorted({r['source'] for r in loadout_rows}),
        languages=sorted({r['language'] for r in loadout_rows}))
    return result


def summary(rows):
    return dict(cases=len(rows),equal=sum(r['equal'] for r in rows),
                outside_contract=sum(r.get('outside_contract',False) for r in rows),
                mismatches=sum(not r['equal'] and not r.get('outside_contract',False) for r in rows))


def retained_runs(cli, core, worker, cd, saves, tiny=False):
    """Prepare A then B with A's complete returned state, including equipment.

    First obtain the exchanged RNG/bank/selectors, then replay each A/B pair
    in one worker session. The original retains its own roster/hangar objects.
    """
    missions=cli.campaign(core,cd)
    if tiny:missions=[m for m in missions if m[0]==1][:2]
    tables=cli.default_tables(cd);rng=cli.initial_rng(cd,127);rows=[]
    selectors=struct.pack('<30h',*([999,50,0,-1,999]*6))
    for language in (['GER'] if tiny else ['ENG','FRE','GER']):
        labels=[];initial=[]
        for i,(g,p,name) in enumerate(missions):
            for mode,(source,save) in itertools.product((0,1),[('cd-defaults',None),*saves]):
                if mode==1 and name in ('TRAIN1','TRAIN2'):continue
                b=missions[(i+1)%len(missions)]
                if mode==1 and b[2] in ('TRAIN1','TRAIN2'):b=missions[2]
                labels.append(dict(language=language,group=g,position=p,mission=name,mode=mode,
                                   source=source,axis='retained',next_mission=b[2],target=b))
                initial.append(cli.request(group=g,position=p,mode=mode,language=language,
                    rng=rng,selectors=selectors,tables=tables,save=save))
        first=cli.prepare_batch(core,cd,initial)
        requests=[];kept=[]
        for label,request,a in zip(labels,initial,first):
            if 'error' in a:
                reference=original_batch(worker,cd,language,[request])[0]
                rows.append(dict(label,step='A',**compare(cli,a,reference)));continue
            g,p,_=label.pop('target');kept.append(label);requests.append(request)
            requests.append(cli.request(group=g,position=p,mode=label['mode'],language=language,
                rng=a['rng'],bank=a['mission.var'],selectors=a['selectors'],tables=tables,retain=True))
        # Keep pairs together, preserving equipment in the worker between A/B.
        for start in range(0,len(requests),64):
            chunk=requests[start:start+64]
            ours=cli.prepare_batch(core,cd,chunk)
            reference=original_batch(worker,cd,language,chunk)
            for j,(a,b) in enumerate(zip(ours,reference)):
                label=kept[(start+j)//2]
                rows.append(dict(label,step='A' if j%2==0 else 'B',**compare(cli,a,b)))
    return rows


def seed_runs(cli, core, worker, cd, save_paths, seeds=None, languages=('ENG','FRE','GER')):
    """Exercise the original initializer itself, without injecting our RNG bytes."""
    if seeds is None:seeds=[*range(128),32767,32768,32769,65535]
    tables=cli.default_tables(cd);rows=[]
    for language,(source,path) in itertools.product(languages,[('cd-defaults',None),*[(s.stem,s) for s in save_paths]]):
        save=path.read_bytes() if path else None
        requests=[cli.request(group=1,position=0,mode=1,language=language,
                    rng=cli.initial_rng(cd,seed),tables=tables,save=save) for seed in seeds]
        ours=cli.prepare_batch(core,cd,requests)
        for seed,a in zip(seeds,ours):
            args=[str(worker),'--cd-root',str(cd),'--campaign','1:0','--mode','1',
                  '--language',dict(ENG='E',FRE='F',GER='G')[language],'--seed',str(seed),'--pipe']
            if path:args+=['--save',str(path)]
            r=subprocess.run(args,capture_output=True,timeout=120)
            if r.returncode:
                b={'error':'; '.join(line for line in r.stderr.decode(errors='replace').splitlines()
                                    if line.startswith(('ASSERT ','FAULT ','FAIL ','TRAP ')))}
            else:
                decoded=packets(r.stdout)
                if len(decoded)!=1:raise ValueError('initializer packet count')
                b=decoded[0]
            rows.append(dict(language=language,group=1,position=0,mission='C1_01',mode=1,
                             source=source,axis='seed-initializer',seed=seed,**compare(cli,a,b)))
        print(json.dumps(summary(rows)),flush=True)
    return rows


def markdown(report):
    lines=['# VSHELL differential sweep','',json.dumps(report['summary']), '',
           '| Mission | Mode | Language | Source | script | STR | var | MEC | RNG | reset |','|---|---:|---|---|---|---|---|---|---|---|']
    for r in report['cases']:
        if r['axis']=='baseline':
            status=['equal' if r['files'][f]['equal'] else 'DIFFERENT' for f in FIELDS] if 'files' in r else ['outside contract' if r.get('outside_contract') else 'ERROR']*6
            lines.append(f"| {r['mission']} | {r['mode']} | {r['language']} | {r['source']} | "+' | '.join(status)+' |')
    lines.extend(['','| Axis | Cases | Equal | Outside contract | Mismatches |','|---|---:|---:|---:|---:|'])
    for axis in sorted({r['axis'] for r in report['cases']}):
        s=summary([r for r in report['cases'] if r['axis']==axis])
        lines.append(f"| {axis} | {s['cases']} | {s['equal']} | {s['outside_contract']} | {s['mismatches']} |")
    for r in report['cases']:
        if not r['equal'] and not r.get('outside_contract'):
            lines.extend(['',json.dumps(r)])
    return '\n'.join(lines)+'\n'


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',required=True,type=Path);p.add_argument('--worker',required=True,type=Path)
    p.add_argument('--cd-root',required=True,type=Path);p.add_argument('--cli',type=Path,default=Path(__file__).parents[3]/'shell/tools/es2prep.py')
    p.add_argument('--save',action='append',type=Path,default=[])
    p.add_argument('--profile',choices=('tiny','baseline','coverage','retained','seed-initializer','loadout'),default='coverage')
    p.add_argument('--config',type=Path);p.add_argument('--report',required=True,type=Path)
    args=p.parse_args(argv);cli=module(args.cli,'portable_cli')
    saves=[(s.stem,s.read_bytes()) for s in args.save]
    config=json.loads(args.config.read_text()) if args.config else None
    started=time.monotonic();last=started
    def progress(rows):
        nonlocal last
        now=time.monotonic()
        if now-last>=10:
            print(json.dumps(summary(rows)),flush=True);last=now
    if args.profile=='seed-initializer':
        rows=seed_runs(cli,args.core,args.worker,args.cd_root,args.save)
    elif args.profile=='loadout':
        rows=loadout_runs(cli,args.core,args.worker,args.cd_root,saves,progress=progress)
        rows+=default_sav_runs(cli,args.core,args.worker,args.cd_root)
    elif args.profile=='retained':
        rows=retained_runs(cli,args.core,args.worker,args.cd_root,saves)
    else:
        rows=run(cli,args.core,args.worker,args.cd_root,matrix(cli,args.core,args.cd_root,saves,args.profile,config),progress=progress)
    report=dict(profile=args.profile,config=config,elapsed_seconds=time.monotonic()-started,
                reference_sha256=cli.REFERENCE_HASH,core_sha256=cli.sha(args.core.read_bytes()),
                worker_sha256=cli.sha(args.worker.read_bytes()),
                saves=[dict(name=n,sha256=cli.sha(b)) for n,b in saves],
                summary=loadout_summary(rows) if args.profile=='loadout' else summary(rows),cases=rows)
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    args.report.with_suffix('.md').write_text(markdown(report),encoding='utf-8')
    print(json.dumps(report['summary']),flush=True)
    return int(report['summary']['mismatches']!=0)


if __name__=='__main__':
    raise SystemExit(main())
