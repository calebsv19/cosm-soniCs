"""Exercise discovered physical effect controls through the official SDK and production render."""
import argparse, asyncio, json, os, socket, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Generates a unique identity for each deliberate authored operation.
def identity(): return str(uuid.uuid4())

async def check(fixture, output):
    """Verify ordered chains, exact history, retry ownership and endpoint admission."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='daw-fx-mcp-') as directory:
        endpoint=str(Path(directory)/'control.sock')
        env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(Path(directory)/'library'))
        process=subprocess.Popen([fixture,endpoint,'write',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==45
                    async def call(name,**args):
                        """Decode accepted transport outcomes without hiding domain refusals."""
                        result=await session.call_tool(name,args);assert not result.isError,result
                        return result.structuredContent or json.loads(result.content[0].text)
                    effects=[];offset=0
                    while True:
                        page=await call('list_effects',offset=offset);assert page['ok'] and len(page['effects'])<=8
                        effects+=page['effects'];offset=page['next_offset']
                        if offset==-1:break
                    assert len(effects)==54 and len({e['type_id'] for e in effects})==54
                    gain=next(e for e in effects if e['type_id']==1);assert gain['parameters'][0]['unit']=='dB'
                    cap=await call('capabilities');instance=cap['instance']
                    target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='track');target.pop('name')
                    current=await call('get_effect_chain',target=target);assert current['effects']==[]
                    async def edit(action,**kwargs):
                        """Advance exact chain state only after a successful deliberate edit."""
                        nonlocal current
                        args=dict(instance=instance,request_id=identity(),expected_state=current['expected_state'],target=target,action=action,**kwargs)
                        response=await call('edit_effect_chain',**args)
                        if response['ok']:current=response
                        return response,args
                    fixture_command(process,'render baseline')
                    first,args=await edit('add',type_id=1);assert first['ok'];gain_id=first['effects'][0]['effect_id']
                    assert await call('edit_effect_chain',**args)==first
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==first
                    assert (await call('edit_effect_chain',**dict(args,request_id=identity())))['code']=='state_conflict'
                    assert (await call('edit_effect_chain',**dict(args,type_id=90)))['code']=='request_id_conflict'
                    changed,_=await edit('parameter',effect_id=gain_id,parameter=0,value=-6.0);assert changed['ok']
                    fixture_command(process,'render rounded_keys');assert (output/'rounded_keys.wav').read_bytes()!=(output/'baseline.wav').read_bytes()
                    count=fixture_command(process,'end_gesture')
                    unchanged,_=await edit('parameter',effect_id=gain_id,parameter=0,value=-6.0);assert unchanged['code']=='unchanged' and fixture_command(process,'end_gesture')==count
                    reverbed,_=await edit('add',type_id=90);assert reverbed['ok'];reverb_id=reverbed['effects'][1]['effect_id']
                    response,_=await edit('reorder',effect_id=reverb_id,position=0);assert response['effects'][0]['effect_id']==reverb_id
                    assert (await edit('enable',effect_id=reverb_id,enabled=False))[0]['ok']
                    assert (await edit('remove',effect_id=gain_id))[0]['ok']
                    final=current;fixture_command(process,'render edited')
                    for _ in range(6):
                        current=await call('undo_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']);assert current['ok']
                    assert current['effects']==[]
                    for _ in range(6):
                        current=await call('redo_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']);assert current['ok']
                    assert current['effects']==final['effects'];fixture_command(process,'render redo')
                    assert (output/'edited.wav').read_bytes()==(output/'redo.wav').read_bytes()
                    # Non-effect commands at the shared cursor cannot be consumed by effect history.
                    region=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');region.pop('name')
                    sound=await call('get_instrument',target=region)
                    ins=await call('set_instrument',instance=instance,request_id=identity(),target=region,expected_state=sound['expected_state'],parameters={'level':.4});assert ins['ok']
                    assert (await call('undo_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state']))['code']=='history_conflict'
                    assert (await call('undo_instrument',instance=instance,request_id=identity(),target=region,expected_state=ins['expected_state']))['ok']
                    fixture_command(process,'begin_gesture');assert (await edit('enable',effect_id=reverb_id,enabled=True))[0]['code']=='busy';fixture_command(process,'end_gesture')
                    assert (await edit('parameter',effect_id=reverb_id,parameter=999,value=0.0))[0]['code']=='invalid_request'
                    assert (await edit('parameter',effect_id=reverb_id,parameter=0,value=1e20))[0]['code']=='invalid_request'
                    assert (await edit('add',type_id=1,value=.5))[0]['code']=='invalid_request'
                    malformed=await session.call_tool('edit_effect_chain',dict(instance=instance,request_id=identity(),target=target,expected_state=current['expected_state'],action='add',type_id=True));assert malformed.isError
                    # A disconnected sender can recover exactly one immutable terminal receipt.
                    lost=dict(op='edit_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=current['expected_state'],action='enable',effect_id=reverb_id,enabled=True)
                    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
                        connection.connect(endpoint);connection.sendall(json.dumps(lost).encode()+b'\n')
                    current=await call('request_status',instance=instance,request_id=lost['request_id']);assert current['ok'];recovered=current
                    # Beat sync resolves to physical units through the common tempo conversion.
                    delay,_=await edit('add',type_id=50);assert delay['ok'];delay_id=current['effects'][-1]['effect_id']
                    delay_type=next(e for e in effects if e['type_id']==50)
                    p=next(p for p in delay_type['parameters'] if p['tempo_sync'] and p['unit'] in ('ms','seconds'))
                    synced,_=await edit('parameter',effect_id=delay_id,parameter=p['index'],mode='beats',beats=.5);assert synced['ok']
                    parameter=synced['effects'][-1]['parameters'][p['index']];assert parameter['mode']=='beats' and parameter['beats']==.5 and parameter['value']==250
                    expired=current['expected_state']
                    for i in range(10):
                        response,_=await edit('parameter',effect_id=delay_id,parameter=p['index'],mode='beats',beats=.25+i*.025);assert response['ok']
                    assert (await call('edit_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=expired,action='add',type_id=1))['code']=='state_conflict'
                    master=await call('get_effect_chain');assert master['effects']==[]
                    master=await call('edit_effect_chain',instance=instance,request_id=identity(),expected_state=master['expected_state'],action='add',type_id=21);assert master['ok']
                    assert (await call('get_effect_chain',target=target))['effects']==current['effects']
                    fixture_command(process,'render rounded_keys');fixture_command(process,'reopen');fixture_command(process,'render motion')
                    assert (output/'rounded_keys.wav').read_bytes()==(output/'motion.wav').read_bytes()
                    assert (await call('get_effect_chain',target=target))['code']=='stale_target'
                    records={'tool_count':35,'catalog_count':len(effects),'full_chain_history_same_IDs_and_audio':True,'file_reopen_audio_exact':True,'lost_response_receipt':recovered,'master_chain':master}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0,process.returncode
        reader=subprocess.Popen([fixture,endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';cap=wire(endpoint,{'op':'capabilities'});chain=wire(endpoint,{'op':'get_effect_chain'})
            for op in ('edit_effect_chain','undo_effect_chain','redo_effect_chain'):
                args=dict(op=op,instance=cap['instance'],request_id=identity(),expected_state=chain['expected_state'])
                if op=='edit_effect_chain':args.update(action='add',type_id=1)
                assert wire(endpoint,args)['code']=='read_only'
            assert fixture_command(reader,'changes')=='DONE 0'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
    (output/'fx-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_fx_test: success (45 tools, 54 types, physical/beat params, ordering/bypass/removal, exact history/render/reopen, conflicts/replay/disconnect/read-only)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'effects'))
