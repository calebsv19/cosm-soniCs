"""Verify immutable production composition analysis and receipt-backed captured WAV previews."""
import argparse, asyncio, json, os, shutil, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Assigns a fresh identity only to deliberate artifact publications and authored edits.
def identity(): return str(uuid.uuid4())

async def check(fixture, output):
    """Compose a private phrase, layer effects and compare captured previews with production WAV bytes."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='daw-composition-mcp-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');library=root/'library'
        env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(library))
        process=subprocess.Popen([fixture,endpoint,'empty',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==45
                    assert tools['analyze_composition'].annotations.readOnlyHint and not tools['preview_composition'].annotations.readOnlyHint
                    async def call(name,**args):
                        """Keep transport errors distinct from explicit domain refusals."""
                        result=await session.call_tool(name,args);assert not result.isError,result
                        return result.structuredContent or json.loads(result.content[0].text)
                    cap=await call('capabilities');instance=cap['instance'];layout=await call('get_midi_layout')
                    created=await call('create_midi_region',instance=instance,request_id=identity(),expected_layout=layout['expected_layout'],start_frame='0',duration_frames='96000');assert created['ok'];target=created['created_target']
                    track=dict(target,clip_id='0',kind='track');sound=await call('get_instrument',target=target)
                    assert (await call('set_instrument',instance=instance,request_id=identity(),target=target,expected_state=sound['expected_state'],preset='rounded_keys',parameters={'level':.35}))['ok']
                    notes=await call('get_midi_region',target=target)
                    assert (await call('create_midi_pattern',instance=instance,request_id=identity(),target=target,expected_state=notes['expected_state'],mode='replace',pattern='melody',root_note=60,start_beat=0.0,velocity=.55))['ok']
                    fixture_command(process,'render baseline')
                    history=fixture_command(process,'end_gesture');receipts=(await call('capabilities'))['receipts_used']
                    dry=await call('analyze_composition',target=target,start_frame='0',end_frame='120000',dry=True)
                    assert dry['ok'] and dry['frames']=='120000' and not dry['normalized'] and dry['scope']=='midi_region'
                    assert len(dry['plots']['waveform'])==len(dry['plots']['envelope_rms'])==256 and len(dry['plots']['spectrum_db'])==128
                    assert 0<dry['metrics']['rms']<dry['metrics']['peak']<1
                    assert not library.exists() and fixture_command(process,'end_gesture')==history and (await call('capabilities'))['receipts_used']==receipts
                    args=dict(instance=instance,request_id=identity(),render_id=dry['render_id'])
                    published=await call('preview_composition',**args);assert published['ok'] and published['durable']
                    assert Path(published['path']).read_bytes()==(output/'baseline-listen.wav').read_bytes()
                    shutil.copy2(published['path'],output/'dry-listen.wav')
                    chain=await call('get_effect_chain',target=track)
                    async def effect(action,**kw):
                        """Apply explicit chain controls while advancing their exact precondition."""
                        nonlocal chain
                        chain=await call('edit_effect_chain',instance=instance,request_id=identity(),target=track,expected_state=chain['expected_state'],action=action,**kw);assert chain['ok'];return chain
                    await effect('add',type_id=1);gain_id=chain['effects'][0]['effect_id']
                    await effect('parameter',effect_id=gain_id,parameter=0,value=-3.0)
                    await effect('add',type_id=50);delay_id=chain['effects'][-1]['effect_id']
                    await effect('parameter',effect_id=delay_id,parameter=0,mode='beats',beats=.5)
                    await effect('add',type_id=90)
                    fixture_command(process,'render edited')
                    wet=await call('analyze_composition',target=target,start_frame='0',end_frame='120000');assert wet['ok'] and len(wet['captured_inserts']['track']['effects'])==3
                    assert wet['metrics']!=dry['metrics'] and 0<wet['metrics']['peak']<1
                    # Publication consumes the analyzed samples, even after authored gain changes.
                    await effect('parameter',effect_id=gain_id,parameter=0,value=-12.0)
                    wetargs=dict(instance=instance,request_id=identity(),render_id=wet['render_id'])
                    wetwav=await call('preview_composition',**wetargs);assert wetwav['ok']
                    assert Path(wetwav['path']).read_bytes()==(output/'edited-listen.wav').read_bytes()
                    assert Path(wetwav['path']).read_bytes()!=Path(published['path']).read_bytes();shutil.copy2(wetwav['path'],output/'processed-listen.wav')
                    assert await call('preview_composition',**wetargs)==wetwav
                    assert await call('request_status',instance=instance,request_id=wetargs['request_id'])==wetwav
                    assert (await call('preview_composition',**dict(wetargs,render_id=dry['render_id'])))['code']=='request_id_conflict'
                    dry2=await call('analyze_composition',target=target,start_frame='0',end_frame='120000',dry=True);assert dry2['metrics']==dry['metrics'] and dry2['plots']==dry['plots']
                    occupied=library/'CompositionPreviews'/f"{instance}-{dry2['render_id'].split(':')[-1]}-{(await call('capabilities'))['receipts_used']+1}.wav"
                    occupied.write_bytes(b'user owned sentinel')
                    assert (await call('preview_composition',instance=instance,request_id=identity(),render_id=dry2['render_id']))['code']=='destination_occupied'
                    assert occupied.read_bytes()==b'user owned sentinel';occupied.unlink()
                    tail=await call('analyze_composition',target=track,start_frame='0',end_frame='96000',tail_frames='48000',window_frame='96000');assert tail['ok'] and tail['frames']=='144000' and tail['scope']=='track'
                    project=await call('analyze_composition',start_frame='0',end_frame='120000');assert project['ok'] and project['scope']=='project'
                    # Count-bounded immutable audio expires tokens without changing accepted receipts.
                    for _ in range(4):assert (await call('analyze_composition',target=target,start_frame='0',end_frame='120000'))['ok']
                    assert (await call('preview_composition',instance=instance,request_id=identity(),render_id=wet['render_id']))['code']=='expired_render'
                    assert await call('preview_composition',**wetargs)==wetwav
                    for extra in ({'end_frame':'480001'},{'tail_frames':'240001'},{'window_frame':'120000'},{'start_frame':'00'},{'dry':1},{'unknown':0}):
                        raw=dict(op='analyze_composition',target=target,start_frame='0',end_frame='120000',**{k:v for k,v in extra.items() if k not in ('start_frame','end_frame')});raw.update(extra)
                        assert wire(endpoint,raw)['code']=='invalid_request'
                    fixture_command(process,'begin_gesture');assert (await call('analyze_composition',target=target,start_frame='0',end_frame='120000'))['code']=='busy';fixture_command(process,'end_gesture')
                    # Original-level analysis reports overload; PCM publication refuses silent clipping.
                    sound=await call('get_instrument',target=target)
                    assert (await call('set_instrument',instance=instance,request_id=identity(),target=target,expected_state=sound['expected_state'],preset='pure_sine',parameters={'level':1.0}))['ok']
                    for e in list(chain['effects']):
                        if e['effect_id']!=gain_id:await effect('enable',effect_id=e['effect_id'],enabled=False)
                    await effect('parameter',effect_id=gain_id,parameter=0,value=24.0)
                    clipped=await call('analyze_composition',target=target,start_frame='0',end_frame='120000');assert clipped['ok'] and clipped['metrics']['peak']>1
                    files_before=sorted(p.name for p in (library/'CompositionPreviews').iterdir())
                    assert (await call('preview_composition',instance=instance,request_id=identity(),render_id=clipped['render_id']))['code']=='clipping_requires_gain_edit'
                    assert sorted(p.name for p in (library/'CompositionPreviews').iterdir())==files_before
                    records={'tool_count':35,'dry':dry,'processed':wet,'processed_preview':wetwav,'tail':tail,'clipped':clipped,'exact_production_pcm_bytes':True,'immutable_across_authored_changes_and_receipt_replay':True}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';cap=wire(endpoint,{'op':'capabilities'})
            rendered=wire(endpoint,dict(op='analyze_composition',start_frame='0',end_frame='120000'));assert rendered['ok']
            assert wire(endpoint,dict(op='preview_composition',instance=cap['instance'],request_id=identity(),render_id=rendered['render_id']))['code']=='read_only'
            assert fixture_command(reader,'changes')=='DONE 0'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
        # The audio-byte budget can evict before the four-render count limit at high sample rates.
        large=subprocess.Popen([fixture,endpoint,'high-rate'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert large.stdout.readline().strip()=='READY'
            for i in range(3):
                result=wire(endpoint,dict(op='analyze_composition',start_frame='0',end_frame='1920000',tail_frames='960000'));assert result['ok']
                stats=wire(endpoint,dict(op='capabilities'))['composition']
                assert stats['current_audio_bytes']<=67108864
                assert stats['current_render_count']==min(i+1,2)
            records['byte_budget_eviction_192kHz']=stats
        finally:
            if large.poll() is None:large.stdin.write('quit\n');large.stdin.flush()
            large.wait(timeout=10);assert large.returncode==0
    (output/'composition-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_composition_test: success (production dry/processed/tail analysis, exact captured PCM, immutable replay/expiry, read-only and no-op authoring, overload and bounds)')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--output-root',required=True);a=p.parse_args()
    asyncio.run(check(str(Path(a.fixture).resolve()),Path(a.output_root).resolve()/'composition'))
