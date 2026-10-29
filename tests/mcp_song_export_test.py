"""Verify ordinary guarded SDK export jobs and independently decoded publication."""
import argparse,asyncio,json,os,subprocess,tempfile,uuid,struct,wave
from pathlib import Path
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT,fixture_command

async def run(fixture,output,mode):
    """Use a private endpoint and actual SDK discovery, receipts, progress and cancellation."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sonics-export-mcp-') as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/'library'),DAW_COLLECTIONS_ROOT=str(root/'collections'))
        process=subprocess.Popen([fixture,endpoint,mode,str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools};assert len(tools)==70
                    assert tools['get_song_export'].annotations.readOnlyHint
                    async def call(name,**args):
                        """Separate successful transport from typed domain refusal."""
                        result=await session.call_tool(name,args);assert not result.isError,result;return result.structuredContent or json.loads(result.content[0].text)
                    async def finish(job):
                        """Check monotonic readback until a retained terminal identity is available."""
                        previous=0
                        for n in range(600):
                            value=await call('get_song_export',job_id=job);done=int(value['completed_work_frames']);assert done>=previous;previous=done
                            if value['state'] in ['completed','cancelled','failed','published_unsynced','published_unverified']:return value
                            await asyncio.sleep(.02)
                        raise AssertionError('export timeout')
                    caps=await call('capabilities');mix=await call('get_mix_snapshot');instance=caps['instance'];assert (await call('get_song_export'))['code']=='idle'
                    args=dict(instance=instance,request_id=str(uuid.uuid4()),expected_mix=mix['expected_mix'],format='float32',tail_frames='2400',normalize=False)
                    if mode=='read':
                        assert (await call('start_song_export',**args))['code']=='read_only'
                        assert (await call('cancel_song_export',instance=instance,request_id=str(uuid.uuid4()),job_id='missing'))['code']=='read_only';return {'read_only':True}
                    baseline=fixture_command(process,'changes');history=fixture_command(process,'end_gesture')
                    first=await call('start_song_export',**args);assert first['ok'] and first['job_id'] and not first['published']
                    assert await call('start_song_export',**args)==first and await call('request_status',instance=instance,request_id=args['request_id'])==first
                    done=await finish(first['job_id']);assert done['state']=='completed' and done['verified'] and done['directory_synced']
                    data=Path(done['output_path']).read_bytes();assert data[:4]==b'RIFF' and struct.unpack_from('<H',data,20)[0]==3 and struct.unpack_from('<H',data,34)[0]==32
                    assert int(done['output_frames'])==122400 and len(data)==44+122400*8 and int(done['file_bytes'])==len(data)
                    h=1469598103934665603
                    for byte in data:h=((h^byte)*1099511628211)&((1<<64)-1)
                    assert done['content_id']==f'{h:016x}'
                    assert fixture_command(process,'changes')==baseline and fixture_command(process,'end_gesture')==history
                    bad=await call('start_song_export',**dict(args,request_id=str(uuid.uuid4()),tail_frames='02400'));assert bad['code']=='invalid_request'
                    bad=await call('start_song_export',**dict(args,request_id=str(uuid.uuid4()),tail_frames='2880001'));assert bad['code']=='invalid_request'
                    fx=await call('get_effect_chain');changed=await call('edit_effect_chain',instance=instance,request_id=str(uuid.uuid4()),expected_state=fx['expected_state'],action='add',type_id=1);assert changed['ok']
                    assert (await call('start_song_export',**dict(args,request_id=str(uuid.uuid4()))))['code']=='state_conflict'
                    mix=await call('get_mix_snapshot');args.update(expected_mix=mix['expected_mix'],request_id=str(uuid.uuid4()),format='pcm16',tail_frames='0')
                    second=await call('start_song_export',**args);pcm=await finish(second['job_id']);assert pcm['verified']
                    with wave.open(pcm['output_path']) as w:assert w.getsampwidth()==2 and w.getnchannels()==2 and w.getnframes()==120000
                    assert (await call('get_song_export',job_id=first['job_id']))==done
                    args.update(request_id=str(uuid.uuid4()),tail_frames='2880000',format='float32');long=await call('start_song_export',**args);assert long['ok']
                    cancelled=await call('cancel_song_export',instance=instance,request_id=str(uuid.uuid4()),job_id=long['job_id']);assert cancelled['code'] in ['cancel_requested','cancelling']
                    cancel_done=await finish(long['job_id']);assert cancel_done['state']=='cancelled' and not cancel_done['published'] and not Path(cancel_done['output_path']).exists()
                    assert (await call('cancel_song_export',instance=instance,request_id=str(uuid.uuid4()),job_id=done['job_id']))['code']=='already_finished'
                    assert (await call('get_song_export',job_id='unknown'))['code']=='unknown_export'
                    return {'tools':len(tools),'float32':done,'pcm16':pcm,'cancelled':cancel_done,'receipt_replay':True,'authoring_preserved':True}
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            assert process.wait(timeout=20)==0

def main():
    """Retain compact ordinary SDK acceptance separately from rerunnable private export payloads."""
    p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--output-root',required=True);a=p.parse_args();out=Path(a.output_root)/'song-export';out.mkdir(parents=True,exist_ok=True)
    result={mode:asyncio.run(run(a.fixture,out/mode,mode)) for mode in ['write','read']};(out/'acceptance.json').write_text(json.dumps(result,indent=2));print('mcp_song_export_test: passed (68 tools, guarded real SDK jobs, exact encodings/tails, progress, verified identity, receipts, cancellation and read-only)')
if __name__=='__main__':main()
