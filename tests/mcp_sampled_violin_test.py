"""Exercise sampled violin preset, automation and recall through the official MCP SDK."""
import asyncio,json,os,pathlib,subprocess,tempfile,uuid,sys,hashlib,shutil
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import fixture_command
owner=pathlib.Path(__file__).resolve().parents[1]
async def run():
 out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='sonics-violin-sdk-') as d:
  root=pathlib.Path(d);env=dict(os.environ,DAW_PRESET_LIBRARY=str(root/'library'),DAW_CONTROL_SOCKET=str(root/'control.sock'))
  proc=subprocess.Popen([sys.argv[1],env['DAW_CONTROL_SOCKET'],'write',str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=env)
  try:
   assert proc.stdout.readline().strip()=='READY'
   async with stdio_client(StdioServerParameters(command=sys.executable,args=[str(owner/'tools/mcp/server.py')],env=env)) as (r,w):
    async with ClientSession(r,w) as client:
     await client.initialize()
     async def call(tool_name,**kw):
      """Require protocol and domain success for each qualified operation."""
      q=await client.call_tool(tool_name,kw);assert not q.isError,q;j=q.structuredContent or json.loads(q.content[0].text);assert j['ok'],j;return j
     cap=await call('capabilities');instance=cap['instance'];voice=next(p for p in cap['presets'] if p['id']=='sampled_violin');assert (voice['minimum_note'],voice['maximum_note'],voice['dynamic_layers'])==(55,96,2)
     target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');target.pop('name')
     before=await call('get_instrument',target=target)
     args=dict(instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=before['expected_state'],preset='sampled_violin',parameters={'dynamics':.65,'velocity_depth':.8,'release_ms':330,'level':.6})
     edited=await call('set_instrument',**args);assert await call('set_instrument',**args)==edited
     at=dict(target,kind='region');auto=await call('get_automation',target=at,lane_target=26)
     assert {p['lane_target'] for p in auto['available_targets']}=={0,1,2,4,5,26,27}
     automated=await call('set_automation',instance=instance,request_id=str(uuid.uuid4()),target=at,expected_arrangement=auto['expected_arrangement'],lane_target=26,domain='offset',before='hold',after='hold',points=[dict(frame='0',value=-1.,curve='smooth'),dict(frame='120000',value=1.,curve='linear')])
     state=await call('get_instrument',target=target)
     preset=await call('save_preset',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=state['expected_state'],name='Sampled Violin Foundation')
     p=preset['preset'];assert p['base_voice']=='sampled_violin' and len(p['parameters'])==32
     previews=[]
     for mode in ['note','chord']:
      q=await call('preview_preset',instance=instance,request_id=str(uuid.uuid4()),preset_id=p['id'],preset_revision=p['revision'],mode=mode);assert not q['normalized'] and 0<q['rms']<q['peak']<1
      dest=out/f'violin-{mode}.wav';shutil.copy2(q['path'],dest);previews.append(dict(mode=mode,peak=q['peak'],sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
     expected=state['instrument'];fixture_command(proc,'reopen')
     target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');target.pop('name');now=await call('get_instrument',target=target)
     for key in ['effective_preset','parameters']:assert now['instrument'][key]==expected[key]
     restored=await call('get_automation',target=dict(target,kind='region'),lane_target=26);assert restored['point_count']==2
     shutil.copy2(root/'midi-project.json',out/'sdk-reopened-project.json')
     (out/'sdk-violin.json').write_text(json.dumps(dict(status='passed',voice=voice,previews=previews,checks=['SDK discovery','guarded apply','immutable replay','voice-aware automation','dynamics lane','preset save','note/chord WAV','project reopen']),indent=2)+'\n')
     print('Sampled violin MCP preset, expression, preview and project recall: passed')
  finally:
   proc.stdin.close();proc.wait(timeout=10);assert proc.returncode==0,proc.returncode
asyncio.run(run())
