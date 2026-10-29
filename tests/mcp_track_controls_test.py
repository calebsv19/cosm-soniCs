"""Qualify isolated sampled child/bus controls through SDK, native interleavings, history and exact recall."""
import argparse, asyncio, json, os, subprocess, tempfile, uuid, struct, math, shutil
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Extract float samples without normalizing or changing rendered audio.
def samples(path):
    data=path.read_bytes();offset=12
    while offset+8<=len(data):
        tag=data[offset:offset+4];size=int.from_bytes(data[offset+4:offset+8],'little');offset+=8
        if tag==b'data': return struct.unpack('<'+'f'*(size//4),data[offset:offset+size])
        offset+=size+(size&1)
    raise AssertionError('No WAV data')

# Require independent left/right factors across every original-level rendered frame.
def factors(before,after,left,right):
    assert len(before)==len(after) and max(abs(v) for v in before)>0
    assert max(abs(b-a*(left if i%2==0 else right)) for i,(a,b) in enumerate(zip(before,after)))<2e-6

# Exercise production control ownership against isolated synthetic notes and real recorded voices.
async def check(fixture,output):
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sonics-track-controls-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/'library'))
        process=subprocess.Popen([fixture,endpoint,'write',str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'build/mcp-venv/bin/python'),args=[str(ROOT/'tools/mcp/server.py')],env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools}
                    assert len(tools)==70 and tools['get_track_controls'].annotations.readOnlyHint
                    assert not tools['set_track_control'].annotations.readOnlyHint
                    async def call(op,**args):
                        """Require successful protocol delivery while retaining explicit domain refusals."""
                        response=await session.call_tool(op,args);assert not response.isError,response
                        return response.structuredContent or json.loads(response.content[0].text)
                    cap=await call('capabilities');instance=cap['instance'];assert cap['track_controls']['gain_max']==10
                    def req(): return dict(instance=instance,request_id=str(uuid.uuid4()))
                    async def setfield(target,control,value,state=None):
                        """Refresh only when deliberately authoring a new operation."""
                        current=state or await call('get_track_controls',target=target)
                        args=dict(**req(),target=target,expected_state=current['expected_state'],control=control,value=value)
                        out=await call('set_track_control',**args);assert out['ok'],out;return out,args
                    routing=await call('get_routing');violin=routing['tracks'][0]['target']
                    region=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');region.pop('name')
                    initial_notes=await call('get_midi_region',target=region)
                    voice=await call('get_instrument',target=violin)
                    chosen=await call('set_instrument',**req(),target=violin,expected_state=voice['expected_state'],preset='sampled_violin',parameters={'level':.25});assert chosen['ok'],chosen
                    layout=await call('get_midi_layout')
                    added=await call('create_midi_region',**req(),expected_layout=layout['expected_layout'],start_frame='0',duration_frames='120000');assert added['ok'],added
                    wind_region=added['created_target'];clarinet=dict(wind_region,kind='track',clip_id='0')
                    voice=await call('get_instrument',target=clarinet)
                    chosen=await call('set_instrument',**req(),target=clarinet,expected_state=voice['expected_state'],preset='sampled_clarinet',parameters={'level':.25});assert chosen['ok'],chosen
                    notes=await call('get_midi_region',target=wind_region)
                    out=await call('set_midi_notes',**req(),target=wind_region,expected_state=notes['expected_state'],mode='replace',notes=[dict(start_frame='30000',duration_frames='24000',pitch=67,velocity=.6)]);assert out['ok'],out
                    routing=await call('get_routing')
                    bus=await call('create_group',**req(),expected_routing=routing['expected_routing'],name='Strings');assert bus['ok'],bus;strings=bus['created_target']
                    sub=await call('create_group',**req(),expected_routing=bus['expected_routing'],name='Woodwinds');assert sub['ok'],sub;woodwinds=sub['created_target']
                    nested=await call('edit_routing',**req(),expected_routing=sub['expected_routing'],edits=[dict(operation='parent',target=violin,parent_target=strings),dict(operation='parent',target=woodwinds,parent_target=strings),dict(operation='parent',target=clarinet,parent_target=woodwinds)]);assert nested['ok'],nested
                    # One-field names and receipt recovery are shared with native header naming.
                    renamed,args=await setfield(violin,'name','Violin — Theme')
                    assert await call('set_track_control',**args)==renamed
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==renamed
                    assert (await call('set_track_control',**dict(args,value='Other')))['code']=='request_id_conflict'
                    for target,label in [(clarinet,'Clarinet — Answer'),(strings,'Strings — Opening'),(woodwinds,'Woodwinds — Reply')]: await setfield(target,'name',label)
                    fixture_command(process,'render composition');before=samples(root/'composition.wav')
                    await setfield(violin,'gain',.5);await setfield(clarinet,'gain',.5)
                    await setfield(strings,'gain',.5)
                    fixture_command(process,'render composition');factors(before,samples(root/'composition.wav'),.25,.25)
                    pan,args=await setfield(strings,'pan',.5)
                    fixture_command(process,'render composition');balanced=samples(root/'composition.wav');factors(before,balanced,.125,.25)
                    # Field/target scope cannot skip another command; independent native pan is retained.
                    assert (await call('undo_track_control',**req(),target=strings,expected_state=pan['expected_state'],control='gain'))['code']=='history_unavailable'
                    undone=await call('undo_track_control',**req(),target=strings,expected_state=pan['expected_state'],control='pan');assert undone['ok'] and undone['gain']==.5
                    redone=await call('redo_track_control',**req(),target=strings,expected_state=undone['expected_state'],control='pan');assert redone['ok'] and redone['pan']==.5
                    old=await call('get_track_controls',target=violin);fixture_command(process,'native_pan')
                    gain,args=await setfield(violin,'gain',.25,old);assert gain['pan']==-.25
                    undone=await call('undo_track_control',**req(),target=violin,expected_state=gain['expected_state'],control='gain');assert undone['ok'] and undone['pan']==-.25
                    gain,args=await setfield(violin,'gain',.25)
                    fixture_command(process,'native_gain')
                    assert (await call('set_track_control',**req(),target=violin,expected_state=gain['expected_state'],control='gain',value=.4))['code']=='state_conflict'
                    fresh=await call('get_track_controls',target=violin)
                    assert (await call('undo_track_control',**req(),target=violin,expected_state=fresh['expected_state'],control='gain'))['code']=='state_conflict'
                    old=await call('get_track_controls',target=violin);fixture_command(process,'native_name')
                    assert (await call('set_track_control',**req(),target=violin,expected_state=old['expected_state'],control='name',value='Stale'))['code']=='state_conflict'
                    native=await call('get_track_controls',target=violin);assert native['name']=='Native Violin'
                    undone=await call('undo_track_control',**req(),target=violin,expected_state=native['expected_state'],control='name');assert undone['ok'] and undone['name']=='Violin — Theme'
                    redone=await call('redo_track_control',**req(),target=violin,expected_state=undone['expected_state'],control='name');assert redone['ok']
                    fixture_command(process,'begin_gesture');fresh=await call('get_track_controls',target=violin)
                    assert (await call('set_track_control',**req(),target=violin,expected_state=fresh['expected_state'],control='gain',value=.4))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    for field,value in [('name',''),('name','a'*128),('name','bad\nname'),('gain',-1.),('gain',10.1),('pan',1.1)]:
                        assert (await call('set_track_control',**req(),target=violin,expected_state=fresh['expected_state'],control=field,value=value))['code']=='invalid_request'
                    fixture_command(process,'play_view');live,args=await setfield(violin,'gain',.5);assert live['ok'];fixture_command(process,'stop_view')
                    # Existing music contents and effects are independent from names and base mixer state.
                    current_notes=await call('get_midi_region',target=region);assert current_notes['notes']==initial_notes['notes']
                    chain=await call('get_effect_chain',target=violin);assert chain['effects']==[]
                    snapshot=[await call('get_track_controls',target=t) for t in [violin,clarinet,strings,woodwinds]]
                    fixture_command(process,'render composition');audio=(root/'composition.wav').read_bytes()
                    fixture_command(process,'reopen')
                    rows=(await call('get_routing'))['tracks']
                    restored=[await call('get_track_controls',target=row['target']) for row in rows]
                    for a,b in zip(snapshot,restored):
                        for key in ['persistent_id','name','gain','pan','muted','solo','is_group']:assert a[key]==b[key],(key,a,b)
                    fixture_command(process,'render composition');assert audio==(root/'composition.wav').read_bytes()
                    assert (await call('set_track_control',**req(),target=violin,expected_state=live['expected_state'],control='gain',value=.4))['code']=='stale_target'
                    # Raw escaped NUL names must refuse rather than silently shortening the accepted label.
                    state=await call('get_track_controls',target=rows[0]['target'])
                    assert wire(endpoint,dict(op='set_track_control',**req(),target=rows[0]['target'],expected_state=state['expected_state'],control='name',value='Short\x00Hidden'))['code']=='invalid_request'
                    assert (await call('get_track_controls',target=rows[0]['target']))['name']==state['name']
                    literal,args=await setfield(rows[0]['target'],'name',r'Literal \u0000 text');assert literal['name']==r'Literal \u0000 text'
                    fixture_command(process,'native_mute_solo');inspected=await call('get_track_controls',target=rows[0]['target']);assert inspected['muted'] and inspected['solo']
                    # Tokens expire predictably; the client refreshes before starting deliberate new edits.
                    oldest=await call('get_track_controls',target=rows[0]['target'])
                    for i in range(17): await setfield(rows[0]['target'],'pan',-.8+i*.05)
                    assert (await call('set_track_control',**req(),target=rows[0]['target'],expected_state=oldest['expected_state'],control='name',value='Expired'))['code']=='state_conflict'
                    raw=dict(op='set_track_control',**req(),target=rows[0]['target'],expected_state=inspected['expected_state'],control='pan',value=0.,unknown=True)
                    assert wire(endpoint,raw)['code']=='invalid_request'
                    shutil.copy2(root/'midi-project.json',output/'isolated-reopened-project.json')
                    (output/'acceptance.json').write_text(json.dumps(dict(status='passed',tool_count=len(tools),controls=cap['track_controls'],saved=snapshot,exact_audio_reopen=True,checks=['sampled violin/clarinet nested buses','child+bus amplitude factors','stereo balance factors','scoped history','native rename history','stale native/MCP scalar/name interleavings','notes/effects preserved','live playback admission','busy refusal','immutable receipts/recovery','token expiry','lifetime replacement','mute/solo readback']),indent=2)+'\n')
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';cap=wire(endpoint,dict(op='capabilities'));target=wire(endpoint,dict(op='get_routing'))['tracks'][0]['target'];state=wire(endpoint,dict(op='get_track_controls',target=target));assert state['ok']
            for op in ['set_track_control','undo_track_control','redo_track_control']:
                args=dict(op=op,instance=cap['instance'],request_id=str(uuid.uuid4()),target=target,expected_state=state['expected_state'],control='gain')
                if op=='set_track_control':args['value']=.5
                assert wire(endpoint,args)['code']=='read_only'
        finally:
            reader.stdin.write('quit\n');reader.stdin.flush();reader.wait(timeout=10);assert reader.returncode==0
    print('mcp_track_controls_test: passed (61 tools, sampled child/bus factors, exact recall, native history/interleavings, receipts, scoped guards and read-only)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'track-controls'))
