# window-lint: allow-file inherits console: runs as a Bazel test, whose windowless console the children share
"""Exercise the actual PowerShell mock path and unchanged oracle comparator."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    helper, runner, oracle_path = map(Path, sys.argv[1:])
    spec = importlib.util.spec_from_file_location('coverage_report', helper)
    coverage = importlib.util.module_from_spec(spec); spec.loader.exec_module(coverage)
    spec = importlib.util.spec_from_file_location('oracle', oracle_path)
    oracle = importlib.util.module_from_spec(spec); spec.loader.exec_module(oracle)
    powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as tmp:
        root = Path(tmp); source = root/'inputs'; source.mkdir()
        def write(path, value): path.write_text(json.dumps(value), encoding='utf-8')
        hooks={'rng_next':0,'rng_bounded':0,'track_count':0}
        assert {'jbe','js','jp','jnp','jo','jno'} <= coverage.CONDITIONS and 'jmp' not in coverage.CONDITIONS
        sites=[dict(hook='rng_next',va='0x00401000',condition='je'),dict(hook='rng_next',va='0x00401010',condition='jne'),dict(hook='rng_bounded',va='0x00401020',condition='jbe')]
        entries=[]
        golden={'format':1,'frame_limit':2,'tick_fields':['frame','clock'],'ticks':[[0,0],[1,47]],'watches':[],'outputs':{},'last_frame':1}
        for index,name in enumerate(('acceptance-60s','c1_01-collision')):
            g=source/(name+'-golden.json');n=source/(name+'-normalized.json');r=source/(name+'-coverage.json')
            write(g,golden);write(n,golden)
            records=[dict(s,fall=0,taken=0,restored=True) for s in sites]
            records[0]['fall' if index==0 else 'taken']=2**32+index
            records[1]['taken']=5
            report=dict(schema=1,module_sha256=coverage.MODULE_HASH,mode='saturate',pid=index+100,install_thread=9,first_thread=9,other_thread=0,tick_thread=9,error=0,complete=True,reason='frame_limit',has_tick=True,last_frame=1,frame_limit=2,hooks=[dict(name=k,mode=v) for k,v in hooks.items()],sites=records)
            write(r,report);entries.append(dict(suite=name,golden=str(g),normalized=str(n),coverage=str(r),pid=index+100,proxy_sha256='synthetic-proxy'))
        manifest=dict(suites=entries,expected_hooks=hooks,expected_sites=sites)
        write(source/'coverage-manifest.json',manifest)
        result=coverage.merge(manifest,oracle)
        assert result['valid'] and result['hooks'][0]['both_ways']==1 and result['hooks'][0]['taken_only']==1 and result['hooks'][1]['never']==1
        assert result['hooks'][2]['sites']==0
        assert result['sites'][0]['first_fall_suite']=='acceptance-60s' and result['sites'][0]['first_taken_suite']=='c1_01-collision'
        command=[str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(runner),'-MockDirectory',str(source),'-OutputDirectory',str(root/'output'),'-Python',sys.executable]
        launch = subprocess.run(command,check=True,capture_output=True,text=True,errors='replace')
        assert (root/'output/coverage-summary.json').exists(), (sys.executable, command, launch.stdout, launch.stderr)
        assert coverage.read(root/'output/coverage-summary.json')['valid']
        # A changed simulation field invalidates coverage and the script fails.
        changed=copy.deepcopy(golden);changed['ticks'][1][1]=48;write(Path(entries[0]['normalized']),changed)
        failed=subprocess.run(command[:-3]+[str(root/'bad-output'),'-Python',sys.executable],capture_output=True,text=True,errors='replace')
        assert failed.returncode != 0, failed.stdout
        assert coverage.read(root/'bad-output/coverage-summary.json')['valid'] is False
        write(Path(entries[0]['normalized']),golden)
        report_path=Path(entries[0]['coverage']);original=coverage.read(report_path)
        mutations=[('module_sha256','wrong'),('schema',2),('complete',False),('error',1),('other_thread',99),('tick_thread',10),('install_thread',10),('pid',999),('reason','snapshot'),('last_frame',0),('frame_limit',3),('has_tick',False)]
        refused=0
        for key,value in mutations:
            report=copy.deepcopy(original);report[key]=value;write(report_path,report)
            try: coverage.merge(manifest,oracle)
            except ValueError: refused+=1
            else: raise AssertionError(key)
        for mutation in ('missing','duplicate','negative','unrestored','condition'):
            report=copy.deepcopy(original)
            if mutation=='missing':report['sites'].pop()
            if mutation=='duplicate':report['sites'].append(report['sites'][0])
            if mutation=='negative':report['sites'][0]['fall']=-1
            if mutation=='unrestored':report['sites'][0]['restored']=False
            if mutation=='condition':report['sites'][0]['condition']='jmp'
            write(report_path,report)
            try:coverage.merge(manifest,oracle)
            except ValueError:refused+=1
            else:raise AssertionError(mutation)
        write(report_path,original)
        full=copy.deepcopy(golden);full['frame_limit']=None
        write(Path(entries[0]['golden']),full);write(Path(entries[0]['normalized']),full)
        full_report=copy.deepcopy(original);full_report.update(reason='exit',frame_limit=0);write(report_path,full_report)
        assert coverage.merge(manifest,oracle)['valid']
        for field,value in [('reason','detach'),('frame_limit',2),('last_frame',2)]:
            invalid=copy.deepcopy(full_report);invalid[field]=value;write(report_path,invalid)
            try:coverage.merge(manifest,oracle)
            except ValueError:refused+=1
            else:raise AssertionError(('full-mission',field))
        write(Path(entries[0]['golden']),golden);write(Path(entries[0]['normalized']),golden);write(report_path,original)
        # A held suite has no golden frame_limit and no exit: the window is its hold_frame (frame_limit = hold, last frame hold-1).
        held_golden=copy.deepcopy(golden);held_golden['frame_limit']=None
        write(Path(entries[0]['golden']),held_golden);write(Path(entries[0]['normalized']),held_golden)
        held_manifest=copy.deepcopy(manifest);held_manifest['suites'][0]['hold_frame']=2
        assert coverage.merge(held_manifest,oracle)['valid']
        for label,change in [('exit',dict(reason='exit',frame_limit=0)),('other window',dict(frame_limit=3,last_frame=2))]:
            write(report_path,dict(original,**change))
            try:coverage.merge(held_manifest,oracle)
            except ValueError:refused+=1
            else:raise AssertionError(('hold',label))
        write(report_path,original)
        for label,hold in [('hold 3',3),('hold 0',0),('hold text','2')]:
            held_manifest['suites'][0]['hold_frame']=hold
            try:coverage.merge(held_manifest,oracle)
            except ValueError:refused+=1
            else:raise AssertionError(('hold',label))
        try:coverage.merge(manifest,oracle)  # the golden has no frame_limit and the launch no hold: the report must be an exit report
        except ValueError:refused+=1
        else:raise AssertionError('held report accepted without hold_frame')
        write(Path(entries[0]['golden']),golden);write(Path(entries[0]['normalized']),golden)
        saved_reports=[coverage.read(e['coverage']) for e in entries]
        for index,entry in enumerate(entries):
            timed=copy.deepcopy(saved_reports[index]);timed['first_hit_ticks']=True
            for s in timed['sites']:
                for outcome in ('fall','taken'):s[f'first_{outcome}_tick']=index-1 if s[outcome] else None
            write(Path(entry['coverage']),timed)
        timed_manifest=dict(manifest,expected_first_hit_ticks=True)
        timed_result=coverage.merge(timed_manifest,oracle)
        assert timed_result['sites'][0]['first_fall_tick']==-1 and timed_result['sites'][0]['first_taken_tick']==0
        invalid=coverage.read(entries[0]['coverage']);invalid['sites'][0]['first_fall_tick']=2;write(Path(entries[0]['coverage']),invalid)
        try:coverage.merge(timed_manifest,oracle)
        except ValueError:refused+=1
        else:raise AssertionError('first-hit tick outside measured interval')
        for entry,saved in zip(entries,saved_reports):write(Path(entry['coverage']),saved)
        env=dict(os.environ,ES2_LIVE_COVERAGE_CONFIG='sentinel')
        dry=subprocess.run([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(runner),'-DryRun','-CoverageHooks','rng_next','-Suite','acceptance-60s','-FirstHitTicks'],capture_output=True,text=True,errors='replace',env=env)
        assert dry.returncode==0, dry.stdout+dry.stderr
        plan=json.loads(dry.stdout[dry.stdout.index('{'):]);assert plan['dry_run'] and plan['first_hit_ticks'] and plan['suites'][0]['suite']=='acceptance-60s'
        for arguments in (['-InstallName','native-r111'],['-InstallName','../native-r111'],['-CoverageHooks','rng_next,rng_next'],['-CoverageHooks','unknown'],['-Hooks','rng_next=bad']):
            p=subprocess.run([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(runner),'-DryRun']+arguments,capture_output=True,text=True,errors='replace')
            assert p.returncode != 0 and any(message in p.stderr for message in ('measurement clone','CoverageHooks must','Invalid hook mode')), (arguments,p.stderr)
        print(f'PASS: PowerShell mock merge + perturbed-golden failure; {refused} invalid reports rejected; clone/selection guards; no game launched')


if __name__=='__main__':main()
