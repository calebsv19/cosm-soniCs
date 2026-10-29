"""Qualify dynamics controls and non-consuming feedback through the ordinary MCP SDK."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,fixture_command
async def run(fixture,output,mode):
    """Use private MIDI production, authored history and persistence without a user endpoint."""
    with tempfile.TemporaryDirectory(prefix='sonics-dynamics-mcp-') as directory:
        root=Path(directory);env=dict(os.environ,DAW_CONTROL_SOCKET=str(root/'control.sock'),DAW_PRESET_LIBRARY=str(root/'library'),DAW_COLLECTIONS_ROOT=str(root/'collections'),SDL_AUDIODRIVER='dummy')
        process=subprocess.Popen([fixture,env['DAW_CONTROL_SOCKET'],mode,str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70 and tools['get_effect_chain'].annotations.readOnlyHint
                    async def call(name,**args):
                        """Check transport errors separately from domain refusal."""
                        result=await session.call_tool(name,args);assert not result.isError,result;return result.structuredContent or json.loads(result.content[0].text)
                    caps=await call('capabilities');instance=caps['instance'];target=(await call('get_routing'))['tracks'][0]['target'];initial=await call('get_effect_chain',target=target);assert initial['dynamics_feedback']==[]
                    async def edit(target,action,**values):
                        """Resolve the current exact chain token before each deliberate authored edit."""
                        current=await call('get_effect_chain',**({'target':target} if target else {}));args=dict(instance=instance,request_id=str(uuid.uuid4()),expected_state=current['expected_state'],action=action,**values,**({'target':target} if target else {}));result=await call('edit_effect_chain',**args);return result,args
                    if mode=='read':
                        refused,_=await edit(target,'add',type_id=20);assert refused['code']=='read_only';return {'read_only':True,'empty_feedback':initial}
                    added,args=await edit(target,'add',type_id=20);assert added['ok'];assert await call('edit_effect_chain',**args)==added;comp=added['effects'][0]['effect_id']
                    for parameter,value in [(0,-36.),(1,4.),(2,.1),(3,80.),(4,24.),(5,6.),(6,0.)]:assert (await edit(target,'parameter',effect_id=comp,parameter=parameter,value=value))[0]['ok']
                    added,_=await edit(None,'add',type_id=21);assert added['ok'];limiter=added['effects'][0]['effect_id']
                    for parameter,value in [(0,-24.),(1,3.),(2,50.)]:assert (await edit(None,'parameter',effect_id=limiter,parameter=parameter,value=value))[0]['ok']
                    controls=await call('get_effect_chain',target=target);assert controls['dynamics_feedback'][0]['available'] is False
                    fixture_command(process,'live_tick');child=await call('get_effect_chain',target=target);master=await call('get_effect_chain')
                    for chain in [child,master]:
                        v=chain['dynamics_feedback'][0];assert v['available'] and v['fresh'] and v['gain_db']<=0 and v['reduction_db']==-v['gain_db'];assert 'last process call' in v['measurement']
                    assert child['dynamics_feedback'][0]['reduction_db']>1
                    again=await call('get_effect_chain',target=target);assert again['expected_state']==child['expected_state'] and again['dynamics_feedback'][0]['sequence']==child['dynamics_feedback'][0]['sequence']
                    await asyncio.sleep(1.05);stale=await call('get_effect_chain',target=target);assert stale['dynamics_feedback'][0]['available'] and not stale['dynamics_feedback'][0]['fresh']
                    bypass,_=await edit(target,'enable',effect_id=comp,enabled=False);assert bypass['ok'];bypassed=await call('get_effect_chain',target=target);assert not bypassed['dynamics_feedback'][0]['available'] and not bypassed['dynamics_feedback'][0]['enabled']
                    undone=await call('undo_effect_chain',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=bypassed['expected_state']);assert undone['ok'] and undone['effects'][0]['enabled']
                    redone=await call('redo_effect_chain',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=undone['expected_state']);assert redone['ok'] and not redone['effects'][0]['enabled']
                    saved=await call('get_effect_chain',target=target);fixture_command(process,'reopen');fresh_target=(await call('get_routing'))['tracks'][0]['target'];reopened=await call('get_effect_chain',target=fresh_target);assert [{k:v for k,v in effect.items() if k!='effect_id'} for effect in reopened['effects']]==[{k:v for k,v in effect.items() if k!='effect_id'} for effect in saved['effects']];assert not reopened['dynamics_feedback'][0]['available']
                    assert (await call('get_effect_chain',target=target))['code']=='stale_target'
                    return {'tools':70,'child':child,'master':master,'stale':stale,'saved':saved,'reopened':reopened,'control_receipt':added}
        finally:
            process.terminate();process.wait(timeout=10)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--output-root',required=True);a=p.parse_args();out=Path(a.output_root)/'dynamics-feedback';out.mkdir(parents=True,exist_ok=True);results={mode:asyncio.run(run(a.fixture,out,mode)) for mode in ['write','read']};(out/'acceptance.json').write_text(json.dumps(results,indent=2));print('mcp_dynamics_feedback_test: passed (70 tools, actual production feedback, physical controls, non-consuming reads/token stability, immutable receipt, bypass/history/save/reopen and readonly admission)')
