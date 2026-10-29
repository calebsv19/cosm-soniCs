"""Qualify guarded native/MCP-shared comparison capture and callback-safe listening on private fixtures."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,fixture_command,wire

def identity():
    """Separate deliberate operation identities from immutable terminal receipt replay."""
    return str(uuid.uuid4())

async def check(fixture,output,mode):
    """Exercise real SDK discovery, exact guards, immutable audio ownership, retry and read-only admission."""
    with tempfile.TemporaryDirectory(prefix='sonics-ab-mcp-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,SDL_AUDIODRIVER='dummy',DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/'library'),DAW_COLLECTIONS_ROOT=str(root/'collections'))
        process=subprocess.Popen([fixture,endpoint,mode,str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70
                    assert tools['get_ab_comparison'].annotations.readOnlyHint
                    for name in ['capture_ab','listen_ab','stop_ab']:assert not tools[name].annotations.readOnlyHint
                    async def call(name,**args):
                        """Keep successful transport and explicit domain refusal separate."""
                        result=await session.call_tool(name,args);assert not result.isError,result;return result.structuredContent or json.loads(result.content[0].text)
                    caps=await call('capabilities');instance=caps['instance'];current=await call('get_ab_comparison');mix=await call('get_mix_snapshot')
                    args=dict(instance=instance,request_id=identity(),expected_comparison=current['comparison_id'],expected_mix=mix['expected_mix'],side='A',start_frame='0',end_frame='24000')
                    if mode=='read':
                        assert (await call('capture_ab',**args))['code']=='read_only'
                        assert (await call('listen_ab',instance=instance,request_id=identity(),expected_comparison=current['comparison_id'],side='A'))['code']=='read_only'
                        assert (await call('stop_ab',instance=instance,request_id=identity(),expected_comparison=current['comparison_id']))['code']=='read_only'
                        assert (await call('get_ab_comparison'))==current;return {'read_only':True,'tool_count':len(tools)}
                    history=fixture_command(process,'end_gesture');baseline_changes=fixture_command(process,'changes')
                    a=await call('capture_ab',**args);assert a['ok'] and not a['ready'] and len(a['captures'])==1
                    assert await call('capture_ab',**args)==a and await call('request_status',instance=instance,request_id=args['request_id'])==a
                    assert (await call('capture_ab',**dict(args,request_id=identity())))['code']=='comparison_conflict'
                    assert fixture_command(process,'end_gesture')==history and fixture_command(process,'changes')==baseline_changes
                    fx=await call('get_effect_chain');add=await call('edit_effect_chain',instance=instance,request_id=identity(),expected_state=fx['expected_state'],action='add',type_id=1);gain=add['effects'][0]['effect_id']
                    changed=await call('edit_effect_chain',instance=instance,request_id=identity(),expected_state=add['expected_state'],action='parameter',effect_id=gain,parameter=0,value=-6.0);assert changed['ok']
                    assert (await call('capture_ab',**dict(args,request_id=identity(),expected_comparison=a['comparison_id'],side='B')))['code']=='state_conflict'
                    mix=await call('get_mix_snapshot');bargs=dict(args,request_id=identity(),expected_mix=mix['expected_mix'],expected_comparison=a['comparison_id'],side='B')
                    b=await call('capture_ab',**bargs);assert b['ok'] and b['ready'] and b['match_available'];assert b['captures'][0]['captured_configuration_fingerprint']!=b['captures'][1]['captured_configuration_fingerprint']
                    assert abs(b['captures'][0]['matched_rms']-b['captures'][1]['matched_rms'])<1e-10
                    assert 0<b['captures'][0]['matched_gain']<1 and b['captures'][1]['matched_gain']==1
                    assert b['captures'][0]['original_rms']==a['captures'][0]['original_rms']
                    assert await call('capture_ab',**args)==a and (await call('get_ab_comparison'))['comparison_id']==b['comparison_id']
                    before=await call('get_ab_comparison');bad=dict(bargs,request_id=identity(),expected_comparison=b['comparison_id'],end_frame='24001');assert (await call('capture_ab',**bad))['code']=='invalid_request';assert await call('get_ab_comparison')==before
                    malformed=dict(bad,request_id=identity(),end_frame='024000');assert (await call('capture_ab',**malformed))['code']=='invalid_request'
                    listen=dict(instance=instance,request_id=identity(),expected_comparison=b['comparison_id'],side='A')
                    unavailable=await call('listen_ab',**listen);assert unavailable['code']=='output_unavailable'
                    fixture_command(process,'ab_open_device');assert await call('listen_ab',**listen)==unavailable
                    listen['request_id']=identity();started=await call('listen_ab',**listen);assert started['ok'] and started['playing'] and started['position_frames']=='0'
                    fixture_command(process,'ab_tick');assert (await call('get_ab_comparison'))['position_frames']=='128'
                    switched=await call('listen_ab',**dict(listen,request_id=identity(),side='B'));assert switched['playing'] and switched['position_frames']=='128'
                    fixture_command(process,'ab_tick');assert (await call('get_ab_comparison'))['position_frames']=='256'
                    original=await call('listen_ab',**dict(listen,request_id=identity(),side='A',matched=False));assert not original['matched'] and original['position_frames']=='256'
                    assert (await call('listen_ab',**dict(listen,request_id=identity(),side='A',restart=True)))['position_frames']=='0'
                    assert (await call('capture_ab',**dict(bargs,request_id=identity(),expected_comparison=b['comparison_id'])))['code']=='busy'
                    stop=await call('stop_ab',instance=instance,request_id=identity(),expected_comparison=b['comparison_id']);assert stop['ok'] and not stop['playing']
                    gesture=fixture_command(process,'end_gesture');changes=fixture_command(process,'changes')
                    assert await call('get_ab_comparison') and fixture_command(process,'end_gesture')==gesture
                    raw=dict(op='listen_ab',**dict(listen,request_id=identity(),matched=1));assert wire(endpoint,raw)['code']=='invalid_request'
                    cleared=await call('stop_ab',instance=instance,request_id=identity(),expected_comparison=b['comparison_id'],clear=True);assert cleared['ok'] and not cleared['ready'] and not cleared['captures']
                    assert fixture_command(process,'changes')==changes
                    return {'tool_count':len(tools),'matched_rms':b['captures'][0]['matched_rms'],'immutable_a_rms':a['captures'][0]['original_rms'],'history_preserved_for_listening':True,'shared_cursor_frames':256,'retry_recovery':True,'capture':b}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            assert process.wait(timeout=30)==0

async def main(args):
    """Persist a compact ordinary-SDK receipt and retire private endpoint fixtures."""
    output=Path(args.output_root)/'ab-comparison';output.mkdir(parents=True,exist_ok=True)
    write=await check(args.fixture,output,'write');read=await check(args.fixture,output,'read');(output/'acceptance.json').write_text(json.dumps({'write':write,'read':read},indent=2)+'\n')
    print('mcp_ab_comparison_test: passed (65 tools, real SDK exact captures/guards, original/matched readback, private dummy listening, aligned switches/restart, history preservation, receipts and read-only refusal)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',required=True);asyncio.run(main(parser.parse_args()))
