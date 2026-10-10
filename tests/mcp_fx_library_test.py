"""Qualify reusable effect chains through the official SDK, production audio and immutable receipts."""
import argparse, asyncio, json, os, shutil, socket, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

# Assigns a unique identity only to deliberate library or authored changes.
def identity(): return str(uuid.uuid4())

async def check(fixture, output):
    """Exercise portable settings, guarded save/update/apply, complete history and independent project recall."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='daw-fx-library-mcp-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');library=root/'library';env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(library))
        process=subprocess.Popen([fixture,endpoint,'write',str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==45
                    async def call(tool_name,**args):
                        """Keep transport errors distinct from guarded domain refusals."""
                        result=await session.call_tool(tool_name,args);assert not result.isError,result
                        return result.structuredContent or json.loads(result.content[0].text)
                    assert (await call('list_effect_presets'))['presets']==[] and not library.exists()
                    cap=await call('capabilities');instance=cap['instance'];target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='track');target.pop('name')
                    source=await call('get_effect_chain',target=target);empty=source
                    async def edit(action,**kw):
                        """Advance the source chain's exact precondition after explicit edits."""
                        nonlocal source
                        source=await call('edit_effect_chain',instance=instance,request_id=identity(),target=target,expected_state=source['expected_state'],action=action,**kw);assert source['ok'];return source
                    await edit('add',type_id=1);await edit('parameter',effect_id=source['effects'][0]['effect_id'],parameter=0,value=-6.0)
                    await edit('add',type_id=50);await edit('parameter',effect_id=source['effects'][1]['effect_id'],parameter=0,mode='beats',beats=.5)
                    await edit('add',type_id=90);await edit('enable',effect_id=source['effects'][2]['effect_id'],enabled=False)
                    args=dict(instance=instance,request_id=identity(),target=target,expected_state=source['expected_state'],name='Warm Echo')
                    changes=fixture_command(process,'changes');history=fixture_command(process,'end_gesture')
                    saved=await call('save_effect_preset',**args);assert saved['ok'] and saved['durable']
                    assert fixture_command(process,'changes')==changes and fixture_command(process,'end_gesture')==history
                    assert await call('save_effect_preset',**args)==saved and await call('request_status',instance=instance,request_id=args['request_id'])==saved
                    assert (await call('save_effect_preset',**dict(args,name='Changed payload')))['code']=='request_id_conflict'
                    preset=saved['preset'];id=preset['preset_id'];revision=preset['preset_revision'];document=library/'FxChains'/f'{id}.sonics-fx.json'
                    assert len(list((library/'FxChains').glob('*.sonics-fx.json')))==1
                    native=json.loads(document.read_text());assert all('id' not in e for e in native['effects']) and native['effects'][1]['parameters'][0]['mode']==1
                    shutil.copy2(document,output/'warm-echo.sonics-fx.json')
                    read=await call('get_effect_preset',preset_id=id,preset_revision=revision);assert read['effects']==preset['effects'] and all('effect_id' not in e for e in read['effects'])
                    assert (await call('list_effect_presets',search='Warm'))['count']==1
                    assert (await call('save_effect_preset',instance=instance,request_id=identity(),target=target,expected_state=empty['expected_state'],name='Stale'))['code']=='state_conflict'
                    fixture_command(process,'begin_gesture')
                    assert (await call('save_effect_preset',instance=instance,request_id=identity(),target=target,expected_state=source['expected_state'],name='Busy'))['code']=='busy'
                    fixture_command(process,'end_gesture')
                    # Source and master share sound settings but must own different runtime identities.
                    master=await call('get_effect_chain');initial_master=master;applyargs=dict(instance=instance,request_id=identity(),expected_state=master['expected_state'],preset_id=id,preset_revision=revision)
                    master=await call('apply_effect_preset',**applyargs);assert master['ok'] and len(master['effects'])==3
                    assert all(a['effect_id']!=b['effect_id'] for a,b in zip(source['effects'],master['effects']))
                    assert master['effects'][1]['parameters'][0]['value']==250 and not master['effects'][2]['enabled']
                    assert await call('apply_effect_preset',**applyargs)==master
                    noop=await call('apply_effect_preset',**dict(applyargs,request_id=identity(),expected_state=master['expected_state']));assert noop['code']=='unchanged'
                    fixture_command(process,'render edited')
                    undone=await call('undo_effect_chain',instance=instance,request_id=identity(),expected_state=master['expected_state']);assert undone['ok'] and undone['effects']==initial_master['effects']
                    redone=await call('redo_effect_chain',instance=instance,request_id=identity(),expected_state=undone['expected_state']);assert redone['effects']==master['effects']
                    fixture_command(process,'render rounded_keys');assert (output/'edited.wav').read_bytes()==(output/'rounded_keys.wav').read_bytes()
                    source=await call('get_effect_chain',target=target)
                    updateargs=dict(instance=instance,request_id=identity(),target=target,expected_state=source['expected_state'],name='Warm Echo Revised',preset_id=id,preset_revision=revision)
                    updated=await call('save_effect_preset',**updateargs);assert updated['ok'] and updated['preset']['preset_revision']!=revision
                    assert await call('save_effect_preset',**updateargs)==updated and document.with_name(document.name+'.previous').is_file()
                    assert (await call('save_effect_preset',**dict(updateargs,request_id=identity())))['code']=='preset_revision_conflict'
                    assert (await call('apply_effect_preset',**dict(applyargs,request_id=identity(),expected_state=redone['expected_state'])))['code']=='preset_revision_conflict'
                    # A disconnected publication is recovered under its original immutable request identity.
                    lost=dict(op='save_effect_preset',instance=instance,request_id=identity(),target=target,expected_state=source['expected_state'],name='Recovered Chain')
                    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:connection.connect(endpoint);connection.sendall(json.dumps(lost,separators=(',',':')).encode()+b'\n')
                    recovered=None
                    for _ in range(100):
                        recovered=await call('request_status',instance=instance,request_id=lost['request_id'])
                        if recovered['code']!='unknown_request':break
                        await asyncio.sleep(.01)
                    assert recovered['ok'] and recovered['preset']['name']=='Recovered Chain' and len(list((library/'FxChains').glob('*.sonics-fx.json')))==2
                    # Projects retain actual chain settings after every library document is removed.
                    for p in (library/'FxChains').glob('*.sonics-fx.json*'):p.unlink()
                    fixture_command(process,'render baseline');fixture_command(process,'reopen');fixture_command(process,'render motion')
                    assert (output/'baseline.wav').read_bytes()==(output/'motion.wav').read_bytes()
                    assert (await call('get_effect_chain',target=target))['code']=='stale_target'
                    records={'tool_count':35,'saved':saved,'master':master,'updated':updated,'lost_response':recovered,'exact_history_and_project_without_library':True}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=10);assert process.returncode==0
        read_endpoint=str(root/'read.sock');reader=subprocess.Popen([fixture,read_endpoint,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert reader.stdout.readline().strip()=='READY';cap=wire(read_endpoint,dict(op='capabilities'));target=next(t for t in wire(read_endpoint,dict(op='project'))['targets'] if t['kind']=='track');target.pop('name');chain=wire(read_endpoint,dict(op='get_effect_chain',target=target))
            assert wire(read_endpoint,dict(op='list_effect_presets'))['ok']
            assert wire(read_endpoint,dict(op='save_effect_preset',instance=cap['instance'],request_id=identity(),target=target,expected_state=chain['expected_state'],name='Denied'))['code']=='read_only'
            assert wire(read_endpoint,dict(op='apply_effect_preset',instance=cap['instance'],request_id=identity(),target=target,expected_state=chain['expected_state'],preset_id=id,preset_revision=revision))['code']=='read_only'
            assert fixture_command(reader,'changes')=='DONE 0'
        finally:
            if reader.poll() is None:reader.stdin.write('quit\n');reader.stdin.flush()
            reader.wait(timeout=10);assert reader.returncode==0
    (output/'fx-library-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_fx_library_test: success (45 tools, complete portable settings, exact guards, revision updates, independent IDs, atomic history/audio, lost-response recovery, project independence and read-only)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'fx-library'))
