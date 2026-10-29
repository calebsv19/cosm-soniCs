"""Qualify complete creation and extension from zero tracks through official MCP and production audio."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,wire,fixture_command
# Creates one unique immutable identity for each deliberate edit.
def identity():return str(uuid.uuid4())
async def check(fixture,output):
    """Exercise one empty composition, complete mixed history, refusal boundaries and private reopen."""
    output.mkdir(exist_ok=True,parents=True)
    with tempfile.TemporaryDirectory(prefix='daw-region-mcp-') as directory:
        endpoint=str(Path(directory)/'control.sock');env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(Path(directory)/'library'))
        process=subprocess.Popen([fixture,endpoint,'empty',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70
                    assert tools['get_midi_layout'].annotations.readOnlyHint
                    async def call(name,**args):
                        """Decode domain refusals separately from SDK transport success."""
                        result=await session.call_tool(name,args);assert not result.isError,result
                        return result.structuredContent or json.loads(result.content[0].text)
                    cap=await call('capabilities');instance=cap['instance'];layout=await call('get_midi_layout')
                    assert layout['ok'] and layout['track_count']==layout['clip_count']==0 and layout['tracks']==[]
                    assert fixture_command(process,'changes')=='DONE 0'
                    args=dict(instance=instance,request_id=identity(),expected_layout=layout['expected_layout'],start_frame='0',duration_frames='96000')
                    created=await call('create_midi_region',**args);assert created['ok'] and created['track_count']==created['clip_count']==1,created
                    target=created['created_target'];assert await call('create_midi_region',**args)==created
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==created
                    assert fixture_command(process,'end_gesture')=='DONE 1 0'
                    assert (await call('create_midi_region',**dict(args,request_id=identity())))['code']=='state_conflict'
                    assert (await call('create_midi_region',**dict(args,duration_frames='192000')))['code']=='request_id_conflict'
                    track=created['tracks'][0]
                    region=await call('get_midi_region',target=target);assert region['region']['note_count']==0
                    sound=await call('get_instrument',target=target)
                    instrument=await call('set_instrument',instance=instance,request_id=identity(),target=target,expected_state=sound['expected_state'],preset='rounded_keys',parameters={'level':.45})
                    assert instrument['ok']
                    pattern=await call('create_midi_pattern',instance=instance,request_id=identity(),target=target,expected_state=region['expected_state'],mode='replace',pattern='melody',root_note=60,start_beat=0.0,velocity=.55);assert pattern['ok'] and len(pattern['notes'])==8
                    assert (await call('get_midi_layout'))['expected_layout']==created['expected_layout'],'notes/sound are outside layout state'
                    extargs=dict(instance=instance,request_id=identity(),target=target,expected_state=pattern['expected_state'],duration_frames='192000')
                    extended=await call('extend_midi_region',**extargs);assert extended['ok'] and extended['region']['duration_frames']=='192000' and extended['notes']==pattern['notes']
                    assert await call('extend_midi_region',**extargs)==extended
                    final=await call('create_midi_pattern',instance=instance,request_id=identity(),target=target,expected_state=extended['expected_state'],mode='append',pattern='arpeggio',root_note=60,start_beat=4.0,velocity=.5);assert final['ok'] and len(final['notes'])==16
                    render=json.loads(fixture_command(process,'render composition')[5:]);assert render['frames']==192000 and render['notes']==16 and 0<render['rms']<render['peak']<1
                    fixture_command(process,'render edited')
                    current=await call('get_midi_region',target=target)
                    assert (await call('extend_midi_region',**dict(extargs,request_id=identity(),expected_state=current['expected_state'])))['code']=='unchanged'
                    assert fixture_command(process,'end_gesture')=='DONE 5 0'
                    # Undo/redo the mixed action chain fully, including the created track.
                    a=await call('undo_midi',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']);assert a['ok']
                    layout=await call('get_midi_layout');a=await call('undo_midi_region',instance=instance,request_id=identity(),target=target,expected_layout=layout['expected_layout']);assert a['ok']
                    current=await call('get_midi_region',target=target);assert current['region']['duration_frames']=='96000'
                    assert (await call('undo_midi',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']))['ok']
                    sound=await call('get_instrument',target=target);assert (await call('undo_instrument',instance=instance,request_id=identity(),target=target,expected_state=sound['expected_state']))['ok']
                    layout=await call('get_midi_layout');a=await call('undo_midi_region',instance=instance,request_id=identity(),target=target,expected_layout=layout['expected_layout']);assert a['ok'] and a['track_count']==a['clip_count']==0,a
                    assert fixture_command(process,'end_gesture')=='DONE 0 5'
                    a=await call('redo_midi_region',instance=instance,request_id=identity(),target=target,expected_layout=a['expected_layout']);assert a['ok']
                    current=await call('get_midi_region',target=target);assert current['region']['note_count']==0,'same IDs survive redo'
                    sound=await call('get_instrument',target=target);assert (await call('redo_instrument',instance=instance,request_id=identity(),target=target,expected_state=sound['expected_state']))['ok']
                    a=await call('redo_midi',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']);assert a['ok']
                    layout=await call('get_midi_layout');assert (await call('redo_midi_region',instance=instance,request_id=identity(),target=target,expected_layout=layout['expected_layout']))['ok']
                    current=await call('get_midi_region',target=target);a=await call('redo_midi',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']);assert a['ok'] and a['notes']==final['notes']
                    fixture_command(process,'render redo');assert (output/'edited.wav').read_bytes()==(output/'redo.wav').read_bytes()
                    # Current placements guard overlap and are independently bound to complete tempo state.
                    layout=await call('get_midi_layout')
                    base=dict(instance=instance,request_id=identity(),target=track,expected_layout=layout['expected_layout'],start_frame='48000',duration_frames='96000')
                    assert (await call('create_midi_region',**base))['code']=='invalid_request'
                    fixture_command(process,'begin_gesture')
                    assert (await call('create_midi_region',**dict(base,request_id=identity(),start_frame='192000')))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    for start,duration in [('0','0'),('18446744073709551615','1'),('9223372036854775807','2'),('00','10')]:
                        assert wire(endpoint,dict(op='create_midi_region',**dict(base,request_id=identity(),start_frame=start,duration_frames=duration)))['code']=='invalid_request'
                    neighbor=await call('create_midi_region',**dict(base,request_id=identity(),start_frame='192000'));assert neighbor['ok']
                    assert (await call('get_midi_layout',expected_layout=layout['expected_layout']))['code']=='state_conflict'
                    current=await call('get_midi_region',target=target)
                    assert (await call('extend_midi_region',**dict(extargs,request_id=identity(),expected_state=current['expected_state'],duration_frames='192001')))['code']=='invalid_request'
                    assert (await call('extend_midi_region',**dict(extargs,request_id=identity(),expected_state=current['expected_state'],duration_frames='96000')))['code']=='invalid_request'
                    layout=await call('get_midi_layout');assert (await call('undo_midi_region',instance=instance,request_id=identity(),target=neighbor['created_target'],expected_layout=layout['expected_layout']))['ok']
                    fixture_command(process,'reopen');assert (await call('get_midi_region',target=target))['code']=='stale_target'
                    fixture_command(process,'render motion');assert (output/'edited.wav').read_bytes()==(output/'motion.wav').read_bytes()
                    records={'tool_count':35,'created':created,'extended':extended,'final_notes':final['notes'],'composition_render':render,'full_undo_to_zero_and_redo':True,'exact_production_history_and_file_reopen':True}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        # Every structural write shares endpoint admission; readonly reads never prepare project lanes.
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';cap=wire(endpoint,{'op':'capabilities'});layout=wire(endpoint,{'op':'get_midi_layout'})
            target=next(t for t in wire(endpoint,{'op':'project'})['targets'] if t['kind']=='midi_region');target.pop('name');region=wire(endpoint,{'op':'get_midi_region','target':target})
            for op,extra in [('create_midi_region',{'expected_layout':layout['expected_layout'],'start_frame':'0','duration_frames':'96000'}),('extend_midi_region',{'target':target,'expected_state':region['expected_state'],'duration_frames':'192000'}),('undo_midi_region',{'target':target,'expected_layout':layout['expected_layout']}),('redo_midi_region',{'target':target,'expected_layout':layout['expected_layout']})]:
                assert wire(endpoint,dict(op=op,instance=cap['instance'],request_id=identity(),**extra))['code']=='read_only'
            assert fixture_command(reader,'changes')=='DONE 0'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
    (output/'midi-regions-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_midi_regions_test: success (zero-track creation, full mixed undo/redo IDs, extension/second phrase, exact render/file reopen, layout/overlap/busy/overflow conflicts, replay and read-only admission)')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'midi-regions'))
