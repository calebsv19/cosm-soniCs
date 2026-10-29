"""Qualify dedicated EQ, captured channel calibration and escaped Unicode through private SDK endpoints."""
import argparse, asyncio, json, os, subprocess, tempfile, uuid, copy, math, shutil
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Exercise real sampled sources and common native EQ events without contacting the normal runtime.
async def check(fixture,output):
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sonics-eq-sdk-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/'library'))
        process=subprocess.Popen([fixture,endpoint,'write',str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'build/mcp-venv/bin/python'),args=[str(ROOT/'tools/mcp/server.py')],env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70 and tools['get_eq'].annotations.readOnlyHint
                    async def call(op,**kw):
                        """Require transport success while preserving explicit domain refusals."""
                        result=await session.call_tool(op,kw);assert not result.isError,result;return result.structuredContent or json.loads(result.content[0].text)
                    cap=await call('capabilities');instance=cap['instance'];assert cap['dedicated_eq']['bands']==4
                    def req():
                        """Allocate immutable identities only for deliberate new writes."""
                        return dict(instance=instance,request_id=str(uuid.uuid4()))
                    async def edit(target,curve,state=None):
                        """Apply one complete curve through its exact current token."""
                        kw={} if target is None else dict(target=target);state=state or await call('get_eq',**kw)
                        args=dict(**req(),**kw,expected_state=state['expected_state'],curve=curve)
                        out=await call('edit_eq',**args);assert out['ok'],out;return out,args
                    project=await call('inspect_project');region=next(t for t in project['targets'] if t['kind']=='midi_region');region.pop('name');track=dict(region,kind='track',clip_id='0')
                    notes=(await call('get_midi_region',target=region))['notes']
                    sound=await call('get_instrument',target=track);chosen=await call('set_instrument',**req(),target=track,expected_state=sound['expected_state'],preset='sampled_violin',parameters={'level':.2});assert chosen['ok'],chosen
                    before=await call('get_eq',target=track);curve=copy.deepcopy(before['curve']);curve['bands'][1].update(enabled=True,frequency_hz=1000.,gain_db=4.,width_octaves=1.)
                    baseline=await call('analyze_composition',target=track,start_frame='0',end_frame='120000');assert baseline['ok']
                    changed,args=await edit(track,curve,before);assert await call('edit_eq',**args)==changed
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==changed
                    assert (await call('edit_eq',**dict(args,curve=before['curve'])))['code']=='request_id_conflict'
                    got=await call('analyze_composition',target=track,start_frame='0',end_frame='120000');assert got['metrics']!=baseline['metrics']
                    dry=await call('analyze_composition',target=track,start_frame='0',end_frame='120000',dry=True);assert dry['metrics']==baseline['metrics']
                    channels=got['metrics']['channels'];assert len(channels)==2 and [c['label'] for c in channels]==['left','right']
                    for c in channels:
                        assert abs(c['sample_peak_dbfs']-20*math.log10(c['sample_peak']))<1e-9
                        assert abs(c['rms_dbfs']-20*math.log10(c['rms']))<1e-9 and int(c['overload_samples'])==0
                    mixer=await call('get_track_controls',target=track)
                    pan=await call('set_track_control',**req(),target=track,expected_state=mixer['expected_state'],control='pan',value=.5);assert pan['ok']
                    assert (await call('undo_eq',**req(),target=track,expected_state=changed['expected_state']))['code']=='history_unavailable'
                    panundo=await call('undo_track_control',**req(),target=track,expected_state=pan['expected_state'],control='pan');assert panundo['ok']
                    undone=await call('undo_eq',**req(),target=track,expected_state=changed['expected_state']);assert undone['ok'] and undone['curve']==before['curve']
                    redone=await call('redo_eq',**req(),target=track,expected_state=undone['expected_state']);assert redone['ok']
                    fixture_command(process,'native_eq_reset');native=await call('get_eq',target=track);native_defaults=copy.deepcopy(before['curve']);[b.update(enabled=True) for b in native_defaults['bands']];assert native['curve']==native_defaults,native
                    assert (await call('edit_eq',**req(),target=track,expected_state=redone['expected_state'],curve=curve))['code']=='state_conflict'
                    native_undo=await call('undo_eq',**req(),target=track,expected_state=native['expected_state']);assert native_undo['ok'] and native_undo['curve']==curve
                    native_redo=await call('redo_eq',**req(),target=track,expected_state=native_undo['expected_state']);assert native_redo['ok']
                    fixture_command(process,'native_eq_fine');fine=await call('get_eq',target=track);assert 0<fine['curve']['bands'][1]['gain_db']<.001
                    fine_undo=await call('undo_eq',**req(),target=track,expected_state=fine['expected_state']);assert fine_undo['ok'] and fine_undo['curve']==native_defaults
                    fixture_command(process,'native_eq_change');current=await call('get_eq',target=track)
                    assert (await call('undo_eq',**req(),target=track,expected_state=current['expected_state']))['code']=='state_conflict'
                    chosen,args=await edit(track,curve)
                    fixture_command(process,'begin_gesture');assert (await call('edit_eq',**req(),target=track,expected_state=chosen['expected_state'],curve=before['curve']))['code']=='busy';fixture_command(process,'end_gesture')
                    fixture_command(process,'play_view');assert (await call('edit_eq',**req(),target=track,expected_state=chosen['expected_state'],curve=before['curve']))['code']=='busy';fixture_command(process,'stop_view')
                    # Native EQ is independent of existing insert/mixer/instrument and note ownership.
                    routing=await call('get_routing');group=await call('create_group',**req(),expected_routing=routing['expected_routing'],name='Strings — Café 🎻');assert group['ok'];bus=group['created_target']
                    nested=await call('edit_routing',**req(),expected_routing=group['expected_routing'],edits=[dict(operation='parent',target=track,parent_target=bus)]);assert nested['ok']
                    busbefore=await call('get_eq',target=bus);buscurve=copy.deepcopy(busbefore['curve']);buscurve['low_cut'].update(enabled=True,frequency_hz=75.);busset,_=await edit(bus,buscurve)
                    master=await call('get_eq');mastercurve=copy.deepcopy(master['curve']);mastercurve['bands'][2].update(enabled=True,gain_db=-1.);masterset,_=await edit(None,mastercurve)
                    routing=await call('get_routing');returned=await call('create_return',**req(),expected_routing=routing['expected_routing']);assert returned['ok'];ret=returned['created_target'];returneq=await call('get_eq',target=ret);returncurve=copy.deepcopy(returneq['curve']);returncurve['high_cut'].update(enabled=True,frequency_hz=9000.);await edit(ret,returncurve)
                    # Existing sampled wind voice is independently EQ-editable and saved exactly.
                    sound=await call('get_instrument',target=track);chosen=await call('set_instrument',**req(),target=track,expected_state=sound['expected_state'],preset='sampled_clarinet',parameters={'level':.2});assert chosen['ok']
                    fixture_command(process,'render composition');audio=(root/'composition.wav').read_bytes()
                    eqsaved=[await call('get_eq',target=t) for t in [track,bus,ret]];mastersaved=await call('get_eq')
                    fixture_command(process,'reopen');saved=json.loads((root/'midi-project.json').read_text());assert saved['tracks'][1]['name']=='Strings — Café 🎻'
                    # Reopen the same native-saved document with all Unicode represented as JSON escapes.
                    (root/'baseline-input.json').write_text(json.dumps(saved,ensure_ascii=True));fixture_command(process,'load_project')
                    routing=await call('get_routing');handles=[t['target'] for t in routing['tracks']];assert routing['tracks'][1]['name']=='Strings — Café 🎻'
                    inspect=await call('inspect_project');assert any('Strings — Café 🎻'==t['name'] for t in inspect['targets'])
                    for t,expected in zip(handles,eqsaved):assert (await call('get_eq',target=t))['curve']==expected['curve']
                    assert (await call('get_eq'))['curve']==mastersaved['curve'];fixture_command(process,'render composition');assert audio==(root/'composition.wav').read_bytes()
                    newregion=dict(handles[0],kind='midi_region',clip_id=next(t['clip_id'] for t in inspect['targets'] if t['kind']=='midi_region'));assert (await call('get_midi_region',target=newregion))['notes']==notes
                    assert (await call('edit_eq',**req(),target=track,expected_state=changed['expected_state'],curve=curve))['code']=='stale_target'
                    newtrack=handles[0];fresh=await call('get_eq',target=newtrack)
                    bad=copy.deepcopy(fresh['curve']);bad['bands'][0]['extra']=True
                    assert wire(endpoint,dict(op='edit_eq',**req(),target=newtrack,expected_state=fresh['expected_state'],curve=bad))['code']=='invalid_request'
                    oldest=fresh
                    for value in range(10):
                        c=copy.deepcopy(fresh['curve']);c['bands'][0]['gain_db']=float(value);fresh,_=await edit(newtrack,c)
                    assert (await call('edit_eq',**req(),target=newtrack,expected_state=oldest['expected_state'],curve=oldest['curve']))['code']=='state_conflict'
                    # Metering is captured per channel after pan; reads never consume mutation receipts.
                    mixer=await call('get_track_controls',target=newtrack);setpan=await call('set_track_control',**req(),target=newtrack,expected_state=mixer['expected_state'],control='pan',value=.5);assert setpan['ok']
                    receiptcount=(await call('capabilities'))['receipts_used'];metered=await call('analyze_composition',target=newtrack,start_frame='0',end_frame='120000',dry=True);assert metered['ok']
                    l,r=metered['metrics']['channels'];assert abs(l['rms']/r['rms']-.5)<1e-7 and abs(l['sample_peak']/r['sample_peak']-.5)<1e-7
                    assert (await call('capabilities'))['receipts_used']==receiptcount
                    fixture_command(process,'native_mute_solo');silent=await call('analyze_composition',start_frame='0',end_frame='120000');assert silent['ok'] and all(c['silent'] and c['sample_peak_dbfs'] is None and c['rms_dbfs'] is None for c in silent['metrics']['channels'])
                    shutil.copy2(root/'midi-project.json',output/'isolated-native-saved.json');shutil.copy2(root/'baseline-input.json',output/'isolated-escaped-input.json')
                    (output/'acceptance.json').write_text(json.dumps(dict(status='passed',tool_count=len(tools),calibration=got['metrics'],pan_measurement=metered['metrics'],silence=silent['metrics'],curves=eqsaved,exact_audio_reopen=True,checks=['sampled violin and clarinet','dedicated child/group/return/master','independent insert/mixer/note ownership','native double-click reset with MCP history','stale curve/lifetime/token/busy guards','immutable receipts/recovery','raw and escaped UTF8 session parity','per-channel amplitude/dBFS/pan/silence','read-only analysis receipts']),indent=2)+'\n')
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';instance=wire(endpoint,dict(op='capabilities'))['instance'];eq=wire(endpoint,dict(op='get_eq'))
            for op in ['edit_eq','undo_eq','redo_eq']:
                args=dict(op=op,instance=instance,request_id=str(uuid.uuid4()),expected_state=eq['expected_state'])
                if op=='edit_eq':args['curve']=eq['curve']
                assert wire(endpoint,args)['code']=='read_only'
        finally:
            reader.stdin.write('quit\n');reader.stdin.flush();reader.wait(timeout=10);assert reader.returncode==0
    print('mcp_eq_test: passed (61 tools, dedicated EQ native/MCP guards/history/receipts, sampled DSP/save-reopen, Unicode and per-channel captured metering)')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--output-root',required=True);a=p.parse_args();asyncio.run(check(str(Path(a.fixture).resolve()),Path(a.output_root).resolve()/'dedicated-eq'))
