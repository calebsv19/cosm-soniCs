"""Exercise exact MIDI pages, transactional writes, immutable receipts and production audio via MCP."""
import argparse, asyncio, hashlib, json, os, socket, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Generates one fresh request identity for each deliberate mutation.
def identity():
    return str(uuid.uuid4())

async def check(fixture, output):
    """Qualify the production owner bridge through official SDK schemas and private fixtures."""
    output.mkdir(parents=True, exist_ok=True)
    records = {}
    with tempfile.TemporaryDirectory(prefix='daw-midi-mcp-') as directory:
        endpoint = str(Path(directory)/'control.sock')
        env = dict(os.environ, DAW_CONTROL_SOCKET=endpoint, DAW_PRESET_LIBRARY=str(Path(directory)/'library'))
        process = subprocess.Popen([fixture, endpoint, 'write', str(output)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
        try:
            assert process.stdout.readline().strip() == 'READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'), env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize()
                    tools = {t.name:t for t in (await session.list_tools()).tools}
                    assert len(tools) == 45
                    assert tools['get_midi_region'].annotations.readOnlyHint
                    for name in ('set_midi_notes','create_midi_pattern','transform_midi','undo_midi','redo_midi'):
                        assert not tools[name].annotations.readOnlyHint and tools[name].annotations.idempotentHint
                    async def call(name, **args):
                        """Require transport success then inspect the independently typed domain outcome."""
                        result = await session.call_tool(name,args)
                        assert not result.isError, result
                        return result.structuredContent or json.loads(result.content[0].text)
                    cap = await call('capabilities'); instance = cap['instance']
                    assert cap['midi']['write_note_limit']==64 and cap['midi']['state_token_capacity']==8
                    async def target_now():
                        """Discover the current generation-bound region without reusing array indices."""
                        return {k:v for k,v in next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region').items() if k!='name'}
                    target = await target_now()
                    first = await call('get_midi_region', target=target)
                    assert first['ok'] and first['region']['note_count']==1 and first['notes'][0]['start_frame']=='0'
                    assert first == await call('get_midi_region',target=target)
                    assert fixture_command(process,'changes')=='DONE 0'
                    track=dict(target,kind='track',clip_id='0')
                    assert not (await call('get_midi_region',target=track))['ok']
                    instrument=await call('get_instrument',target=target)
                    assert (await call('set_instrument',instance=instance,request_id=identity(),target=target,expected_state=instrument['expected_state'],preset='rounded_keys',parameters={}))['ok']
                    assert (await call('get_midi_region',target=target))['expected_state']==first['expected_state'], 'instrument edits are outside MIDI state'
                    baseline=json.loads(fixture_command(process,'render baseline')[5:])
                    args=dict(instance=instance,request_id=identity(),target=target,expected_state=first['expected_state'],mode='replace',pattern='chords',root_note=60,start_beat=0.0,velocity=.5)
                    edited=await call('create_midi_pattern',**args)
                    assert edited['ok'] and edited['code']=='applied' and len(edited['notes'])==12, edited
                    assert await call('create_midi_pattern',**args)==edited
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==edited
                    assert fixture_command(process,'end_gesture')=='DONE 2 0', 'one MIDI undo entry after instrument'
                    assert (await call('create_midi_pattern',**dict(args,velocity=.6)))['code']=='request_id_conflict'
                    assert (await call('create_midi_pattern',**dict(args,request_id=identity())))['code']=='state_conflict'
                    render=json.loads(fixture_command(process,'render edited')[5:]);assert render['notes']==12 and 0<render['rms']<render['peak']<1
                    history=dict(instance=instance,target=target,request_id=identity(),expected_state=edited['expected_state'])
                    undone=await call('undo_midi',**history);assert undone['ok'] and undone['notes']==first['notes']
                    fixture_command(process,'render undo')
                    redone=await call('redo_midi',**dict(history,request_id=identity(),expected_state=undone['expected_state']));assert redone['ok'] and redone['notes']==edited['notes']
                    fixture_command(process,'render redo')
                    for a,b in [('baseline','undo'),('edited','redo')]:
                        assert (output/f'{a}.wav').read_bytes()==(output/f'{b}.wav').read_bytes(), 'exact production render history'
                    no_op=await call('create_midi_pattern',**dict(args,request_id=identity(),expected_state=redone['expected_state']));assert no_op['code']=='unchanged'
                    # Expression uses the exact same MIDI state and one-step history owner.
                    transform_args=dict(instance=instance,request_id=identity(),target=target,expected_state=no_op['expected_state'],operation='velocity',velocity_scale=.5)
                    shaped=await call('transform_midi',**transform_args)
                    assert shaped['ok'] and shaped['code']=='applied' and all(n['velocity']==.25 for n in shaped['notes']), shaped
                    assert await call('transform_midi',**transform_args)==shaped
                    assert (await call('transform_midi',**dict(transform_args,request_id=identity())))['code']=='state_conflict'
                    bad=dict(transform_args,request_id=identity(),expected_state=shaped['expected_state'],timing_ms=0)
                    assert (await call('transform_midi',**bad))['code']=='invalid_request'
                    restored=await call('undo_midi',instance=instance,request_id=identity(),target=target,expected_state=shaped['expected_state'])
                    assert restored['ok'] and restored['notes']==no_op['notes']
                    assert (await call('transform_midi',instance=instance,request_id=identity(),target=target,expected_state=restored['expected_state'],operation='quantize',grid_beats=.5,strength=1,swing=0))['code']=='unchanged'
                    fixture_command(process,'begin_gesture')
                    assert (await call('transform_midi',**dict(transform_args,request_id=identity(),expected_state=restored['expected_state'])))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    human_args=dict(instance=instance,request_id=identity(),target=target,expected_state=restored['expected_state'],operation='humanize',timing_ms=0,velocity_amount=.08,seed=123)
                    human=await call('transform_midi',**human_args);assert human['ok'] and human['notes']!=restored['notes']
                    human_undo=await call('undo_midi',instance=instance,request_id=identity(),target=target,expected_state=human['expected_state'])
                    repeated=await call('transform_midi',**dict(human_args,request_id=identity(),expected_state=human_undo['expected_state']))
                    assert repeated['ok'] and repeated['notes']==human['notes']
                    no_op=await call('undo_midi',instance=instance,request_id=identity(),target=target,expected_state=repeated['expected_state'])
                    current=no_op
                    async def mutation(name='set_midi_notes', **changes):
                        """Supply an exact latest MIDI token and a fresh ID for negative or explicit edits."""
                        defaults=dict(instance=instance,request_id=identity(),target=target,expected_state=current['expected_state'])
                        defaults.update(changes);return await call(name,**defaults)
                    fixture_command(process,'begin_gesture')
                    assert (await mutation(mode='replace',notes=[]))['code']=='busy'
                    assert (await mutation('undo_midi'))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    assert (await mutation('create_midi_pattern',mode='append',pattern='melody',root_note=60,start_beat=4.0,velocity=.5))['code']=='invalid_request'
                    assert (await mutation('create_midi_pattern',mode='replace',pattern='chords',root_note=127,start_beat=0.0,velocity=.5))['code']=='invalid_request'
                    base=dict(op='set_midi_notes',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state'],mode='append')
                    for notes in [[{'start_frame':'120000','duration_frames':'1','pitch':60,'velocity':.5}],
                                  [{'start_frame':'18446744073709551615','duration_frames':'1','pitch':60,'velocity':.5}],
                                  [{'start_frame':'0','duration_frames':'0','pitch':60,'velocity':.5}],
                                  [{'start_frame':'0','duration_frames':'1','pitch':60,'velocity':2}],
                                  [{'start_frame':0,'duration_frames':'1','pitch':60,'velocity':.5}],
                                  [{'start_frame':'0','duration_frames':'1','pitch':True,'velocity':.5}],
                                  [{'start_frame':'0','duration_frames':'1','pitch':60,'velocity':.5,'extra':1}],
                                  [{'start_frame':'0','duration_frames':'1','pitch':60,'velocity':.5}]*65,
                                  [{'start_frame':'0','duration_frames':'1','pitch':60,'velocity':.5}]*2]:
                        assert wire(endpoint,dict(base,request_id=identity(),notes=notes))['code']=='invalid_request'
                    raw=json.dumps(dict(base,notes=[])).replace('"notes": []','"notes": [], "notes": []').encode()+b'\n'
                    assert wire(endpoint,raw)['code']=='invalid_request'
                    assert (await call('get_midi_region',target=target))['expected_state']==current['expected_state']
                    malformed=await session.call_tool('set_midi_notes',dict({k:v for k,v in base.items() if k!='op'},notes=[{'start_frame':'0','duration_frames':'1','pitch':True,'velocity':.5}]))
                    assert malformed.isError
                    # A disconnected client can recover the original terminal receipt without replay.
                    lost=dict(base,request_id=identity(),mode='replace',notes=[])
                    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as c:
                        c.connect(endpoint);c.sendall(json.dumps(lost).encode()+b'\n')
                    recovered=await call('request_status',instance=instance,request_id=lost['request_id'])
                    assert recovered['ok'] and recovered['region']['note_count']==0
                    current=recovered
                    note={'start_frame':'0','duration_frames':'1000','pitch':60,'velocity':.5}
                    current=await mutation(mode='append',notes=[note]);assert current['region']['note_count']==1
                    # Top instrument history cannot be consumed by MIDI history.
                    ins=await call('get_instrument',target=target)
                    assert (await call('set_instrument',instance=instance,request_id=identity(),target=target,expected_state=ins['expected_state'],parameters={'level':.4}))['ok']
                    assert (await mutation('undo_midi'))['code']=='history_conflict'
                    # Tokens bind notes and the entire tempo map, not only returned pages.
                    old=current['expected_state'];fixture_command(process,'midi_edit')
                    assert (await mutation(mode='replace',notes=[]))['code']=='state_conflict'
                    assert (await call('get_midi_region',target=target,expected_state=old))['code']=='state_conflict'
                    current=await call('get_midi_region',target=target);fixture_command(process,'tempo_edit')
                    assert (await mutation(mode='replace',notes=[]))['code']=='state_conflict'
                    # The maximum accepted batch and major tonic transposition use the same typed owner.
                    current=await call('get_midi_region',target=target)
                    batch=[dict(note,start_frame=str(i*1000),pitch=60+i%12) for i in range(64)]
                    current=await mutation(mode='replace',notes=batch);assert current['ok'] and len(current['notes'])==64
                    unchanged=await mutation(mode='append',notes=[]);assert unchanged['code']=='unchanged'
                    current=await mutation('create_midi_pattern',mode='replace',pattern='melody',root_note=62,start_beat=0.0,velocity=.55)
                    assert current['ok'] and len(current['notes'])==8 and current['notes'][0]['pitch']==62
                    assert {n['pitch']%12 for n in current['notes']} <= {2,4,6,7,9,11,1}
                    # Full-capacity snapshots paginate within the response limit without consuming receipts.
                    fixture_command(process,'pages');current=await call('get_midi_region',target=target)
                    notes=[];tempos=[];offset=tempo_offset=0
                    while True:
                        part=await call('get_midi_region',target=target,offset=offset,tempo_offset=tempo_offset,expected_state=current['expected_state'])
                        assert part['ok'] and len(part['notes'])<=128 and len(part['tempo_events'])<=64 and len(json.dumps(part))<131072
                        notes.extend(part['notes']);tempos.extend(part['tempo_events'])
                        next_note=part['next_offset'];next_tempo=part['next_tempo_offset']
                        if next_note==-1 and next_tempo==-1:break
                        offset=next_note if next_note!=-1 else 4096
                        tempo_offset=next_tempo if next_tempo!=-1 else 1024
                    assert len(notes)==4096 and len(tempos)==1024
                    assert (await mutation(mode='append',notes=[note]))['code']=='invalid_request'
                    # More than eight distinct states expire old tokens rather than weakening exact checks.
                    expired=current['expected_state']
                    for pitch in range(60,70):
                        current=await mutation(mode='replace',notes=[dict(note,pitch=pitch)])
                        assert current['ok']
                    assert (await call('get_midi_region',target=target,expected_state=expired))['code']=='state_conflict'
                    # File reopen retains sound and notes while revoking old generation handles.
                    fixture_command(process,'render rounded_keys');fixture_command(process,'reopen')
                    assert (await call('get_midi_region',target=target))['code']=='stale_target'
                    target=await target_now();current=await call('get_midi_region',target=target)
                    assert current['region']['note_count']==1 and current['notes'][0]['pitch']==69
                    fixture_command(process,'render motion');assert (output/'rounded_keys.wav').read_bytes()==(output/'motion.wav').read_bytes()
                    # MIDI rejections share the fixed immutable ledger and never evict the first write.
                    used=(await call('capabilities'))['receipts_used']
                    for _ in range(128-used):
                        assert (await mutation(expected_state='expired',mode='replace',notes=[]))['code']=='state_conflict'
                    assert (await mutation(mode='replace',notes=[]))['code']=='receipt_capacity_reached'
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==edited
                    records={'tool_count':len(tools),'baseline':baseline,'pattern_render':render,'full_note_page_count':len(notes),'full_tempo_page_count':len(tempos),'exact_history_and_reopen_audio':True,'first_write_receipt':edited}
        finally:
            if process.poll() is None:
                process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        # Every new MIDI write operation is refused by read-only admission.
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY'
            cap=wire(endpoint,{'op':'capabilities'});target=next(t for t in wire(endpoint,{'op':'project'})['targets'] if t['kind']=='midi_region');target.pop('name')
            read=wire(endpoint,{'op':'get_midi_region','target':target});assert read['ok']
            assert wire(endpoint,{'op':'request_status','instance':instance,'request_id':args['request_id']})['code']=='stale_instance'
            for op,extra in [('set_midi_notes',{'mode':'replace','notes':[]}),('create_midi_pattern',{'mode':'replace','pattern':'melody','root_note':60,'start_beat':0,'velocity':.5}),('transform_midi',{'operation':'velocity','velocity_scale':.5}),('undo_midi',{}),('redo_midi',{})]:
                assert wire(endpoint,dict(op=op,instance=cap['instance'],request_id=identity(),target=target,expected_state=read['expected_state'],**extra))['code']=='read_only'
            assert wire(endpoint,{'op':'get_midi_region','target':target})==read
            assert fixture_command(reader,'changes')=='DONE 0'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
    (output/'midi-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_midi_test: success (official SDK, exact paged state, writes/patterns/history, conflicts/busy/limits, lost-response receipts, token expiry, read-only/restart, production undo/redo and file reopen)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'midi-mcp'))
