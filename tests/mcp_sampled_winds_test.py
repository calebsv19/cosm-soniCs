"""Exercise trumpet/clarinet presets, expression and recall through the official MCP SDK."""
import asyncio,json,os,pathlib,subprocess,tempfile,uuid,sys,hashlib,shutil
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import fixture_command
owner=pathlib.Path(__file__).resolve().parents[1]
async def run(voice,minimum,maximum,layers,piano=False):
 out=pathlib.Path(sys.argv[2])/voice;out.mkdir(parents=True,exist_ok=True)
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
     cap=await call('capabilities');instance=cap['instance'];bank_info=next(p for p in cap['presets'] if p['id']==voice);assert (bank_info['minimum_note'],bank_info['maximum_note'],bank_info['dynamic_layers'])==(minimum,maximum,layers)
     target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');target.pop('name')
     before=await call('get_instrument',target=target)
     args=dict(instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=before['expected_state'],preset=voice,parameters=({'release_ms':330,'level':.6,'attack_ms':5,'sustain_pedal':0,'string_resonance':.4,'release_noise':.5} if piano else {'dynamics':.65,'velocity_depth':.8,'release_ms':330,'level':.6}))
     edited=await call('set_instrument',**args);assert await call('set_instrument',**args)==edited
     at=dict(target,kind='region');lane=2 if piano else 26;auto=await call('get_automation',target=at,lane_target=lane)
     assert {p['lane_target'] for p in auto['available_targets']}==({0,1,2,4,5,28,29,30} if piano else {0,1,2,4,5,26,27})
     automated=await call('set_automation',instance=instance,request_id=str(uuid.uuid4()),target=at,expected_arrangement=auto['expected_arrangement'],lane_target=lane,domain='offset',before='hold',after='hold',points=[dict(frame='0',value=-1.,curve='smooth'),dict(frame='120000',value=1.,curve='linear')])
     if piano:
      for extra in [28,29,30]:
       current=await call('get_automation',target=at,lane_target=extra)
       await call('set_automation',instance=instance,request_id=str(uuid.uuid4()),target=at,expected_arrangement=current['expected_arrangement'],lane_target=extra,domain='offset',before='hold',after='hold',points=[dict(frame='0',value=0.,curve='hold'),dict(frame='120000',value=1.,curve='hold')])
       assert (await call('get_automation',target=at,lane_target=extra))['point_count']==2
     state=await call('get_instrument',target=target)
     preset=await call('save_preset',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=state['expected_state'],name=voice+' Foundation')
     p=preset['preset'];assert p['base_voice']==voice and len(p['parameters'])==32
     previews=[]
     for mode in ['note','chord']:
      q=await call('preview_preset',instance=instance,request_id=str(uuid.uuid4()),preset_id=p['id'],preset_revision=p['revision'],mode=mode);assert not q['normalized'] and 0<q['rms']<q['peak']<1
      dest=out/f'violin-{mode}.wav';shutil.copy2(q['path'],dest);previews.append(dict(mode=mode,peak=q['peak'],sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
     expected=state['instrument'];fixture_command(proc,'reopen')
     target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region');target.pop('name');now=await call('get_instrument',target=target)
     for key in ['effective_preset','parameters']:assert now['instrument'][key]==expected[key]
     restored=await call('get_automation',target=dict(target,kind='region'),lane_target=lane);assert restored['point_count']==2
     shutil.copy2(root/'midi-project.json',out/'sdk-reopened-project.json')
     (out/'sdk-wind.json').write_text(json.dumps(dict(status='passed',voice=bank_info,previews=previews,checks=['SDK discovery','guarded apply','immutable replay','voice-aware automation','dynamics lane','preset save','note/chord WAV','project reopen']),indent=2)+'\n')
     print(voice+': MCP preset, expression, preview and project recall passed')
  finally:
   proc.stdin.close();proc.wait(timeout=10);assert proc.returncode==0,proc.returncode
async def main():
 if len(sys.argv)>3 and sys.argv[3]=='--piano-strings-only':
  await run('sampled_acoustic_piano',21,108,3,True)
  await run('sampled_viola_section',48,86,2)
  await run('sampled_double_bass',28,59,2)
  await run('sampled_violin_section',55,86,2)
 elif len(sys.argv)>3 and sys.argv[3]=='--bassoon-trombone-only':
  await run('sampled_bassoon',34,72,2)
  await run('sampled_tenor_trombone',34,65,2)
 elif len(sys.argv)>3 and sys.argv[3]=='--oboe-horn-only':
  await run('sampled_oboe',58,89,2)
  await run('sampled_french_horn',36,60,2)
 elif len(sys.argv)>3 and sys.argv[3]=='--flute-cello-only':
  await run('sampled_flute',60,93,2)
  await run('sampled_cello_section',36,77,2)
 elif len(sys.argv)>3 and sys.argv[3]=='--saxophone-only':
  await run('sampled_saxophone',44,88,2)
 else:
  await run('sampled_trumpet',53,84,2)
  await run('sampled_clarinet',50,89,3)
asyncio.run(main())
