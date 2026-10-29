"""Qualify guarded routing creation/edit/history through the official SDK and production bridge."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,wire,fixture_command

def identity():
    """Allocate a deliberate request identity without replaying uncertain writes."""
    return str(uuid.uuid4())

async def check(fixture,output):
    """Exercise empty-project routing, signal changes, guards, receipts and exact saved audio."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='daw-routing-mcp-') as directory:
        endpoint=str(Path(directory)/'control.sock');env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(Path(directory)/'library'))
        process=subprocess.Popen([fixture,endpoint,'empty',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70
                    assert tools['get_routing'].annotations.readOnlyHint and not tools['create_return'].annotations.readOnlyHint
                    async def call(tool_name,**args):
                        """Inspect domain outcomes separately from transport success."""
                        response=await session.call_tool(tool_name,args);assert not response.isError,response
                        return response.structuredContent or json.loads(response.content[0].text)
                    instance=(await call('capabilities'))['instance'];empty=await call('get_routing');assert empty['track_count']==0
                    args=dict(instance=instance,request_id=identity(),expected_routing=empty['expected_routing'])
                    created=await call('create_return',**args);assert created['ok'] and created['tracks'][0]['is_return'],created
                    assert await call('create_return',**args)==created
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==created
                    undone=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=created['expected_routing']);assert undone['ok'] and not undone['tracks']
                    redone=await call('redo_routing',instance=instance,request_id=identity(),expected_routing=undone['expected_routing']);assert redone['ok'] and redone['tracks']==created['tracks']
                    empty=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=redone['expected_routing']);assert empty['ok']
                    layout=await call('get_midi_layout');region=await call('create_midi_region',instance=instance,request_id=identity(),expected_layout=layout['expected_layout'],start_frame='0',duration_frames='48000');assert region['ok'],region
                    target=region['created_target'];notes=await call('get_midi_region',target=target)
                    written=await call('set_midi_notes',instance=instance,request_id=identity(),target=target,expected_state=notes['expected_state'],mode='replace',notes=[dict(start_frame='0',duration_frames='24000',pitch=60,velocity=.4)]);assert written['ok'],written
                    routing=await call('get_routing');created=await call('create_return',instance=instance,request_id=identity(),expected_routing=routing['expected_routing']);assert created['ok'],created
                    return_target=created['created_target'];source=created['tracks'][0]['target']
                    dry=await call('analyze_composition',start_frame='0',end_frame='48000');assert dry['ok'] and dry['metrics']['rms']>0
                    edit=dict(operation='send',target=source,slot=3,return_target=return_target,gain=.5)
                    args=dict(instance=instance,request_id=identity(),expected_routing=created['expected_routing'],edits=[edit])
                    sent=await call('edit_routing',**args);assert sent['ok'] and sent['tracks'][0]['sends'][3]['gain']==.5,sent
                    assert await call('edit_routing',**args)==sent
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==sent
                    wet=await call('analyze_composition',start_frame='0',end_frame='48000');assert wet['ok'] and abs(wet['metrics']['rms']/dry['metrics']['rms']-1.5)<1e-6
                    undone=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=sent['expected_routing']);assert undone['ok']
                    restored=await call('analyze_composition',start_frame='0',end_frame='48000');assert restored['metrics']==dry['metrics'] and restored['plots']==dry['plots']
                    sent=await call('redo_routing',instance=instance,request_id=identity(),expected_routing=undone['expected_routing']);assert sent['ok']
                    assert (await call('create_return',instance=instance,request_id=identity(),expected_routing=created['expected_routing']))['code']=='state_conflict'
                    assert (await call('edit_routing',instance=instance,request_id=identity(),expected_routing=sent['expected_routing'],edits=[edit]))['code']=='unchanged'
                    for changes in [[dict(edit,target=return_target,return_target=return_target)], [edit,edit], [dict(operation='role',target=source,is_return=True)]]:
                        refused=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=sent['expected_routing'],edits=changes);assert refused['code']=='invalid_request',refused
                    fixture_command(process,'begin_gesture')
                    assert (await call('create_return',instance=instance,request_id=identity(),expected_routing=sent['expected_routing']))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    rendered=fixture_command(process,'render composition');audio=(output/'composition.wav').read_bytes();assert json.loads(rendered[5:])['peak']>0
                    fixture_command(process,'reopen');assert (await call('undo_routing',instance=instance,request_id=identity(),expected_routing=sent['expected_routing']))['code']=='state_conflict'
                    reopened=await call('get_routing');assert reopened['ok'] and reopened['tracks'][1]['is_return'] and reopened['tracks'][0]['sends'][3]['gain']==.5
                    fixture_command(process,'render composition');assert audio==(output/'composition.wav').read_bytes()
                    raw=dict(op='edit_routing',instance=instance,request_id=identity(),expected_routing=reopened['expected_routing'],edits=[dict(edit,unknown=True)])
                    assert wire(endpoint,raw)['code']=='invalid_request'
                    for value in [.1,.2,.3,.4,.6]:
                        fresh=await call('get_routing')
                        changed=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=fresh['expected_routing'],edits=[dict(edit,target=reopened['tracks'][0]['target'],return_target=reopened['tracks'][1]['target'],gain=value)])
                        assert changed['ok'],changed
                    expired=await call('create_return',instance=instance,request_id=identity(),expected_routing=reopened['expected_routing']);assert expired['code']=='state_conflict'
                    fresh=await call('get_routing')
                    reset=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=fresh['expected_routing'],edits=[dict(edit,target=reopened['tracks'][0]['target'],return_target=reopened['tracks'][1]['target'])]);assert reset['ok']

                    root=await call('create_group',instance=instance,request_id=identity(),expected_routing=reset['expected_routing'],name='Strings');assert root['ok'],root
                    strings=root['created_target'];sub=await call('create_group',instance=instance,request_id=identity(),expected_routing=root['expected_routing'],name='Major Theme');assert sub['ok'],sub
                    theme=sub['created_target'];source=sub['tracks'][0]['target']
                    nested=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=sub['expected_routing'],edits=[dict(operation='parent',target=theme,parent_target=strings),dict(operation='parent',target=source,parent_target=theme)]);assert nested['ok'],nested
                    group_fx=await call('get_effect_chain',target=strings);assert group_fx['ok'],group_fx
                    effect=await call('edit_effect_chain',instance=instance,request_id=identity(),target=strings,expected_state=group_fx['expected_state'],action='add',type_id=1);assert effect['ok'],effect
                    effect=await call('edit_effect_chain',instance=instance,request_id=identity(),target=strings,expected_state=effect['expected_state'],action='parameter',effect_id=effect['effects'][0]['effect_id'],parameter=0,value=-6.020599913);assert effect['ok'],effect
                    expected_audio=await call('analyze_composition',start_frame='0',end_frame='48000');assert expected_audio['ok'],expected_audio
                    routing=await call('get_routing');automation=dict(before=1,after=1,points=[dict(frame='0',value=0.5,curve=0),dict(frame='48000',value=0.5,curve=2)])
                    curve_edit=dict(operation='volume_automation',target=strings,automation=automation)
                    request=dict(instance=instance,request_id=identity(),expected_routing=routing['expected_routing'],edits=[curve_edit])
                    curves=await call('edit_routing',**request);assert curves['ok'] and curves['tracks'][-2]['volume_automation']==automation
                    assert await call('edit_routing',**request)==curves and await call('request_status',instance=instance,request_id=request['request_id'])==curves
                    quieter=await call('analyze_composition',start_frame='0',end_frame='48000');assert abs(quieter['metrics']['rms']/expected_audio['metrics']['rms']-.75)<1e-4, (quieter['metrics'],expected_audio['metrics'])
                    # .75 includes the independent child send that bypasses the group fader.
                    undone_curve=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=curves['expected_routing']);assert undone_curve['ok']
                    assert (await call('get_routing'))['tracks']==routing['tracks']
                    redone_curve=await call('redo_routing',instance=instance,request_id=identity(),expected_routing=undone_curve['expected_routing']);assert redone_curve['ok']
                    undone_curve=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=redone_curve['expected_routing']);assert undone_curve['ok']
                    send_curve=dict(automation,points=[dict(frame='0',value=.25,curve=0),dict(frame='48000',value=.25,curve=1)])
                    send_edit=dict(operation='send_automation',target=source,slot=3,automation=send_curve)
                    send_auto=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=undone_curve['expected_routing'],edits=[send_edit]);assert send_auto['ok'] and send_auto['tracks'][0]['sends'][3]['automation']==send_curve
                    send_audio=await call('analyze_composition',start_frame='0',end_frame='48000');assert abs(send_audio['metrics']['rms']/expected_audio['metrics']['rms']-.75)<1e-4
                    base_send=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=send_auto['expected_routing'],edits=[dict(edit,target=source,return_target=send_auto['tracks'][0]['sends'][3]['return_target'],gain=.7)]);assert base_send['ok'] and base_send['tracks'][0]['sends'][3]['automation']==send_curve
                    assert (await call('analyze_composition',start_frame='0',end_frame='48000'))['metrics']==send_audio['metrics']
                    clear_base=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=base_send['expected_routing']);assert clear_base['ok']
                    undone_curve=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=clear_base['expected_routing']);assert undone_curve['ok']
                    nested=await call('get_routing')
                    bad=dict(op='edit_routing',instance=instance,request_id=identity(),expected_routing=undone_curve['expected_routing'],edits=[dict(curve_edit,automation=dict(automation,points=[dict(frame='00',value=.5,curve=0)]))]);assert wire(endpoint,bad)['code']=='invalid_request'

                    fixture_command(process,'play_view')
                    current=await call('get_routing');fold_args=dict(instance=instance,request_id=identity(),expected_routing=current['expected_routing'],edits=[dict(operation='fold',target=strings,folded=True)])
                    folded=await call('edit_routing',**fold_args);assert folded['ok'],folded
                    assert await call('edit_routing',**fold_args)==folded
                    assert folded['tracks'][0]['visible_row']==-1 and folded['tracks'][3]['visible_row']==-1
                    unfolded=await call('undo_routing',instance=instance,request_id=identity(),expected_routing=folded['expected_routing']);assert unfolded['ok'],unfolded
                    redo=await call('redo_routing',instance=instance,request_id=identity(),expected_routing=unfolded['expected_routing']);assert redo['ok'] and redo['tracks']==folded['tracks']
                    fixture_command(process,'stop_view')
                    folded_audio=await call('analyze_composition',start_frame='0',end_frame='48000');assert expected_audio['metrics']==folded_audio['metrics'] and expected_audio['plots']==folded_audio['plots']
                    refused=await call('edit_routing',instance=instance,request_id=identity(),expected_routing=redo['expected_routing'],edits=[dict(operation='parent',target=strings,parent_target=theme)]);assert refused['code']=='invalid_request',refused
                    ids=[row['persistent_id'] for row in redo['tracks']];fixture_command(process,'render composition');nested_wav=(output/'composition.wav').read_bytes()
                    fixture_command(process,'reopen');after=await call('get_routing');assert [row['persistent_id'] for row in after['tracks']]==ids
                    assert after['tracks'][2]['is_group'] and after['tracks'][2]['folded'] and after['tracks'][0]['parent_target']==after['tracks'][3]['target']
                    fixture_command(process,'render composition');assert nested_wav==(output/'composition.wav').read_bytes()
                    assert (await call('create_group',instance=instance,request_id=identity(),expected_routing=redo['expected_routing'],name='Brass'))['code']=='state_conflict'

                    (output/'routing-acceptance.json').write_text(json.dumps(dict(tool_count=len(tools),created=created,sent=sent,reopened=reopened,exact_audio_reopen=True),indent=2)+'\n')
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';instance=wire(endpoint,dict(op='capabilities'))['instance'];routing=wire(endpoint,dict(op='get_routing'))
            for operation in ['create_group','create_return','edit_routing','undo_routing','redo_routing']:
                payload=dict(op=operation,instance=instance,request_id=identity(),expected_routing=routing['expected_routing'])
                if operation=='create_group':payload['name']='Brass'
                if operation=='edit_routing':payload['edits']=[]
                assert wire(endpoint,payload)['code']=='read_only'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
    print('mcp_routing_test: success (61 tools, nested groups, empty creation, stable history, exact signal/audio reopening, guards, receipts and read-only)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'mcp-routing'))
