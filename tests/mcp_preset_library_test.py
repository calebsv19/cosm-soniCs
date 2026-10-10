"""Qualify user-preset SDK operations against the production bridge using a private library."""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
import wave
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command

async def check(fixture, output):
    """Verify guarded writes, receipts, library revisions, project isolation and deterministic audio."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="daw-preset-mcp-") as directory:
        d=Path(directory); root=d/'library'; socket_path=str(d/'control.sock')
        env=dict(os.environ,DAW_PRESET_LIBRARY=str(root),DAW_CONTROL_SOCKET=socket_path)
        process=subprocess.Popen([fixture,socket_path,'write'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        records={}
        try:
            assert process.stdout.readline().strip()=='READY'
            params=StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)
            async with stdio_client(params) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize()
                    async def call(tool_name,**args):
                        """Decode domain results and reject SDK transport failures."""
                        response=await session.call_tool(tool_name,args); assert not response.isError,response
                        return response.structuredContent or json.loads(response.content[0].text)
                    cap=await call('capabilities'); instance=cap['instance']
                    assert (await call('list_presets'))['presets']==[] and not root.exists()
                    target=next(t for t in (await call('inspect_project'))['targets'] if t['kind']=='midi_region'); target.pop('name')
                    before=await call('get_instrument',target=target)
                    edit=await call('set_instrument',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=before['expected_state'],preset='filtered_pad',parameters={'cutoff_hz':1450,'resonance_q':.8,'release_ms':450,'level':.6,'filter_env_octaves':1.2,'filter_decay_ms':180,'filter_lfo_hz':.7,'filter_lfo_octaves':.4})
                    assert edit['ok'],edit
                    args=dict(instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edit['expected_state'],name='Evening Glass')
                    saved=await call('save_preset',**args); assert saved['ok'] and saved['project_unchanged'],saved
                    assert await call('save_preset',**args)==saved
                    assert await call('request_status',instance=instance,request_id=args['request_id'])==saved
                    assert fixture_command(process,'end_gesture')=='DONE 1 0','saving must not add history'
                    assert (await call('get_instrument',target=target))['instrument']==edit['instrument']
                    p=saved['preset']; assert len(p['parameters'])==18
                    listing=await call('list_presets',search='evening'); assert listing['presets']==[p]
                    assert (await call('get_preset',preset_id=p['id'],preset_revision=p['revision']))['preset']==p
                    assert wire(socket_path,{'op':'get_preset','preset_id':p['id'],'preset_revision':3})['code']=='invalid_request'
                    assert wire(socket_path,{'op':'list_presets','search':'','unknown':True})['code']=='invalid_request'
                    previews={}
                    for mode in ('note','chord'):
                        preview_args=dict(instance=instance,request_id=str(uuid.uuid4()),preset_id=p['id'],preset_revision=p['revision'],mode=mode)
                        preview=await call('preview_preset',**preview_args); assert preview['ok'] and not preview['normalized'],preview
                        assert await call('preview_preset',**preview_args)==preview
                        path=Path(preview['path']); first=path.read_bytes()
                        assert .0001<preview['peak']<1 and 0<preview['rms']<preview['peak']
                        with wave.open(str(path)) as wav:
                            assert (wav.getframerate(),wav.getnchannels(),wav.getsampwidth(),wav.getnframes())==(48000,2,2,144000)
                        repeat=await call('preview_preset',**dict(preview_args,request_id=str(uuid.uuid4())))
                        assert repeat['ok'] and path.read_bytes()==first
                        dest=output/f'evening-glass-{mode}.wav'; shutil.copyfile(path,dest)
                        preview['path']=str(dest); preview['sha256']=hashlib.sha256(first).hexdigest(); previews[mode]=preview
                    assert previews['note']['sha256']!=previews['chord']['sha256']
                    assert fixture_command(process,'end_gesture')=='DONE 1 0'
                    edited=await call('set_instrument',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edit['expected_state'],parameters={'cutoff_hz':2100})
                    update_args=dict(instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edited['expected_state'],name='Evening Glass II',preset_id=p['id'],preset_revision=p['revision'])
                    update=await call('save_preset',**update_args); assert update['ok'],update
                    p2=update['preset']; assert p2['id']==p['id'] and p2['revision']!=p['revision']
                    assert (root/f"{p['id']}.sonics-preset.json.previous").exists()
                    assert (await call('save_preset',**dict(update_args,request_id=str(uuid.uuid4()))))['code']=='preset_conflict'
                    assert (await call('get_preset',preset_id=p['id'],preset_revision=p['revision']))['code']=='preset_conflict'
                    assert (await call('get_instrument',target=target))['instrument']==edited['instrument']
                    undo=await call('undo_instrument',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edited['expected_state']); assert undo['ok']
                    assert undo['instrument']==edit['instrument'],'library update must not change project history copy'
                    applied=await call('apply_preset',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=undo['expected_state'],preset_id=p2['id'],preset_revision=p2['revision'])
                    assert applied['ok'] and applied['instrument']['parameters']==p2['parameters'],applied
                    undone=await call('undo_instrument',instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=applied['expected_state'])
                    assert undone['ok'] and undone['instrument']==edit['instrument']
                    (root/f"{p['id']}.sonics-preset.json").unlink()
                    assert (await call('get_instrument',target=target))['instrument']==edit['instrument'],'deleted library must not affect project'
                    records={'saved':saved,'updated':update,'applied':applied,'undo':undone,'previews':previews,'scope':'official SDK, production bridge/engine, isolated library; no physical listening claim'}
        finally:
            if process.poll() is None: process.stdin.write('quit\n'); process.stdin.flush()
            process.wait(timeout=10); assert process.returncode==0
        # Read-only admission must reject every file-creating operation without creating the library.
        readonly_root=d/'read-only-library'; readonly_socket=str(d/'read-only.sock')
        process=subprocess.Popen([fixture,readonly_socket,'read'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=dict(env,DAW_PRESET_LIBRARY=str(readonly_root)))
        try:
            assert process.stdout.readline().strip()=='READY'
            cap=wire(readonly_socket,{'op':'capabilities'})
            for op in ('save_preset','apply_preset','preview_preset'):
                assert wire(readonly_socket,{'op':op,'instance':cap['instance'],'request_id':str(uuid.uuid4())})['code']=='read_only'
            assert wire(readonly_socket,{'op':'list_presets'})['presets']==[] and not readonly_root.exists()
        finally:
            process.stdin.write('quit\n'); process.stdin.flush(); process.wait(timeout=10)
        (output/'preset-mcp.json').write_text(json.dumps(records,indent=2)+'\n')
    print('mcp_preset_library_test: success (SDK, strict reads, guarded library writes, revision conflicts, immutable retries, independent undo and reproducible WAVs)')

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--fixture',required=True); parser.add_argument('--output-root',required=True); args=parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()/'preset-library'))
