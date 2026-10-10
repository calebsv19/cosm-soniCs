"""Qualify read-only sound analysis, original-level previews and before/after SDK sound edits."""
import argparse,asyncio,hashlib,json,os,shutil,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,wire,fixture_command
async def check(fixture,output):
    """Exercise calibrated analysis and three listening comparisons using only private runtime data."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='daw-analysis-') as directory:
        d=Path(directory);library=d/'library';endpoint=str(d/'control.sock');env=dict(os.environ,DAW_PRESET_LIBRARY=str(library),DAW_CONTROL_SOCKET=endpoint)
        records={};process=subprocess.Popen([fixture,endpoint,'write'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize()
                    tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==45
                    assert tools['analyze_instrument'].annotations.readOnlyHint and tools['analyze_preset'].annotations.readOnlyHint
                    async def call(tool,**args):
                        """Decode a structured domain result independently of MCP transport success."""
                        response=await session.call_tool(tool,args);assert not response.isError,response
                        return response.structuredContent or json.loads(response.content[0].text)
                    cap=await call('capabilities');instance=cap['instance'];target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');target.pop('name')
                    first=await call('get_instrument',target=target);a=await call('analyze_instrument',target=target)
                    assert a['ok'] and a['instrument']==first['instrument'] and not library.exists()
                    assert fixture_command(process,'changes')=='DONE 0'
                    assert a==await call('analyze_instrument',target=target),'repeat analysis must match'
                    assert wire(endpoint,{'op':'analyze_instrument','target':target,'mode':'note','window':'bogus'})['code']=='invalid_request'
                    for label,preset,before_params,after_params in [
                        ('filter','filtered_pad',{'cutoff_hz':650,'resonance_q':.8,'level':.6},{'cutoff_hz':6500}),
                        ('attack','rounded_keys',{'attack_ms':0,'level':.6},{'attack_ms':180}),
                        ('drive','rounded_keys',{'drive':0,'level':.6},{'drive':.85})]:
                        examples=[]
                        for phase,parameters in [('before',before_params),('after',after_params)]:
                            current=await call('get_instrument',target=target)
                            edit=await call('set_instrument',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=current['expected_state'],preset=preset if phase=='before' else None,parameters=parameters);assert edit['ok'],edit
                            analysis=await call('analyze_instrument',target=target,mode='chord',window='attack' if label=='attack' else 'sustain');assert analysis['ok'],analysis
                            report=analysis['analysis'];assert len(report['waveform'])==256 and len(report['envelope_rms'])==256 and len(report['spectrum_db'])==128
                            assert report['clipped_samples']==0 and 0<report['rms']<report['peak']<1
                            saved=await call('save_preset',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edit['expected_state'],name=f'{label} {phase}');assert saved['ok'],saved
                            p=saved['preset'];read=await call('analyze_preset',preset_id=p['id'],preset_revision=p['revision'],mode='chord',window=report['window']);assert read['ok'] and read['analysis']==report
                            preview=await call('preview_preset',instance=instance,request_id=str(uuid.uuid4()),preset_id=p['id'],preset_revision=p['revision'],mode='chord');assert preview['ok'] and preview['peak']==report['peak'] and preview['rms']==report['rms']
                            destination=output/f'{label}-{phase}.wav';shutil.copyfile(preview['path'],destination)
                            examples.append({'analysis':report,'wav':str(destination),'sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
                            # Attack/sustain/release change plot windows without changing whole-render metrics.
                            for window in ('attack','sustain','release'):
                                sample=await call('analyze_preset',preset_id=p['id'],preset_revision=p['revision'],mode='chord',window=window)
                                assert sample['ok'] and sample['analysis']['peak']==report['peak'] and sample['analysis']['rms']==report['rms']
                            assert (await call('get_instrument',target=target))['expected_state']==edit['expected_state']
                        assert examples[0]['sha256']!=examples[1]['sha256'];records[label]=examples
                    # Read-only endpoints can analyze saved sounds and current state without file publication.
                    readonly=str(d/'read.sock');reader=subprocess.Popen([fixture,readonly,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
                    try:
                        assert reader.stdout.readline().strip()=='READY'
                        t=next(t for t in wire(readonly,{'op':'project'})['targets'] if t['kind']=='midi_region');t.pop('name')
                        result=wire(readonly,{'op':'analyze_instrument','target':t,'mode':'note','window':'sustain'});assert result['ok']
                        assert wire(readonly,{'op':'analyze_preset','preset_id':p['id'],'preset_revision':p['revision'],'mode':'note','window':'release'})['ok']
                        assert wire(readonly,{'op':'analyze_preset','preset_id':p['id'],'preset_revision':'0'*32,'mode':'note','window':'sustain'})['code']=='preset_conflict'
                    finally:reader.stdin.write('quit\n');reader.stdin.flush();reader.wait(timeout=10)
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0
        (output/'sound-analysis.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_sound_analysis_test: success (read-only SDK, repeatable captured state, preset parity, original-level WAV parity and three musical comparisons)')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'sound-analysis'))
