"""Qualify coherent live readback and guarded output-only monitoring through the ordinary SDK."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,fixture_command

async def run(fixture,output,mode):
    """Use a private live graph/dummy endpoint; never attach to a musical user session."""
    with tempfile.TemporaryDirectory(prefix='sonics-live-mcp-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,SDL_AUDIODRIVER='dummy',DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/'library'),DAW_COLLECTIONS_ROOT=str(root/'collections'))
        process=subprocess.Popen([fixture,endpoint,mode,str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70 and tools['get_live_meters'].annotations.readOnlyHint
                    async def call(name,**args):
                        """Separate successful transport from explicit domain admission/refusal."""
                        result=await session.call_tool(name,args);assert not result.isError,result;return result.structuredContent or json.loads(result.content[0].text)
                    caps=await call('capabilities');instance=caps['instance'];initial=await call('get_live_meters');assert initial['ok'] and not initial['available'] and initial['monitor_mode']=='stereo'
                    request=dict(instance=instance,request_id=str(uuid.uuid4()),expected_monitor=initial['expected_monitor'],mode='mono')
                    if mode=='read':assert (await call('set_monitor_mode',**request))['code']=='read_only';return {'read_only':True}
                    baseline=fixture_command(process,'changes');history=fixture_command(process,'end_gesture')
                    unavailable=await call('set_monitor_mode',**request);assert unavailable['code']=='output_unavailable'
                    fixture_command(process,'ab_open_device');assert await call('set_monitor_mode',**request)==unavailable
                    fixture_command(process,'live_tick');live=await call('get_live_meters');assert live['available'] and live['valid'] and live['fresh'] and len(live['channels'])==2 and live['channels'][0]['rms']>0 and live['window_end_frame']=='19200'
                    target=(await call('get_routing'))['tracks'][0]['target'];track=await call('get_live_meters',target=target);assert track['track_id']==target['track_id'] and track['available'] and track['valid']
                    args=dict(request,request_id=str(uuid.uuid4()),expected_monitor=live['expected_monitor']);changed=await call('set_monitor_mode',**args);assert changed['ok'] and changed['monitor_mode']=='mono'
                    assert await call('set_monitor_mode',**args)==changed and await call('request_status',instance=instance,request_id=args['request_id'])==changed
                    assert (await call('set_monitor_mode',**dict(args,request_id=str(uuid.uuid4()),mode='stereo')))['code']=='monitor_conflict'
                    after=await call('get_live_meters');assert after['channels']==live['channels'] and after['mono_loss_db']==live['mono_loss_db']
                    restored=await call('set_monitor_mode',**dict(args,request_id=str(uuid.uuid4()),expected_monitor=after['expected_monitor'],mode='stereo'));assert restored['ok'] and restored['monitor_mode']=='stereo'
                    assert fixture_command(process,'changes')==baseline and fixture_command(process,'end_gesture')==history
                    await asyncio.sleep(1.05);stale=await call('get_live_meters');assert stale['available'] and stale['valid'] and not stale['fresh'] and stale['age_ms']>=1000
                    fixture_command(process,'reopen');replacement=await call('get_live_meters');assert not replacement['available'] and replacement['monitor_mode']=='stereo'
                    assert (await call('set_monitor_mode',**dict(args,request_id=str(uuid.uuid4()))))['code']=='monitor_conflict'
                    return {'tools':len(tools),'master':live,'track':track,'stale':stale,'monitor_guards':True,'receipt_replay':True,'authored_history_preserved':True,'project_replacement':replacement}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            assert process.wait(timeout=20)==0

def main():
    """Retain compact readback/calibration receipts separately from rerunnable private PCM fixtures."""
    p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--output-root',required=True);a=p.parse_args();out=Path(a.output_root)/'live-stereo';out.mkdir(parents=True,exist_ok=True);result={mode:asyncio.run(run(a.fixture,out,mode)) for mode in ['write','read']};(out/'acceptance.json').write_text(json.dumps(result,indent=2));print('mcp_live_stereo_test: passed (70 tools, real live graph, coherent target windows, freshness, dummy output-only monitor, guards/receipts, project lifetime and read-only refusal)')
if __name__=='__main__':main()
