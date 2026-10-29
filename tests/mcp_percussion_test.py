"""Qualify percussion creation, controls, user presets and rendered persistence through native MCP."""
import argparse, asyncio, hashlib, json, os, shutil, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import fixture_command
ROOT=Path(__file__).resolve().parents[1]
VOICES=("kick_drum","snare_drum","closed_hat","sub_808","driven_808")

async def exercise(fixture, output):
    """Build a drum groove from zero tracks and compare captured audio before and after reopening."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sonics-percussion-mcp-") as directory:
        d=Path(directory); env=dict(os.environ,DAW_CONTROL_SOCKET=str(d/"control.sock"),DAW_PRESET_LIBRARY=str(d/"library"))
        process=subprocess.Popen([fixture,env["DAW_CONTROL_SOCKET"],"empty",str(output)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env,cwd=d)
        try:
            assert process.stdout.readline().strip()=="READY"
            async with stdio_client(StdioServerParameters(command=str(ROOT/"tools/mcp/run.sh"),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize()
                    async def call(tool_name,**args):
                        """Require domain and protocol success for every guarded action."""
                        result=await session.call_tool(tool_name,args); assert not result.isError,result
                        value=result.structuredContent or json.loads(result.content[0].text); assert value["ok"],value
                        return value
                    cap=await call("capabilities"); instance=cap["instance"]; targets=[]; records={}
                    async def patch(target,**args):
                        """Read the current exact instrument token immediately before mutation."""
                        old=await call("get_instrument",target=target)
                        return await call("set_instrument",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=old["expected_state"],**args)
                    for index,name in enumerate(VOICES):
                        factory=next(p for p in cap["presets"] if p["id"]==name)
                        layout=await call("get_midi_layout")
                        created=await call("create_midi_region",instance=instance,request_id=str(uuid.uuid4()),expected_layout=layout["expected_layout"],start_frame="0",duration_frames="384000")
                        target=created["created_target"];targets.append(target)
                        selected=await patch(target,preset=name,parameters={})
                        assert selected["instrument"]["effective_parameters"]==factory["defaults"]
                        if index==0: hits=[(b*24000,36,.85 if b%4==0 else .7,1200) for b in (0,2,4,6,8,10,12,13.5,14)]
                        elif index==1: hits=[(b*24000+360,50,.8 if b%4==1 else .9,1200) for b in (1,3,5,7,9,11,13,15)]
                        elif index==2: hits=[(b*12000+240,84,.4 if b%2 else .65,480) for b in range(32)]
                        else: hits=[(b*24000,36 if b<8 else 33,.55 if index==3 else .25,30000) for b in (0,3,4,7,8,11,12,14)]
                        notes=[dict(start_frame=str(int(t)),duration_frames=str(gate),pitch=pitch,velocity=vel) for t,pitch,vel,gate in hits]
                        midi=await call("get_midi_region",target=target)
                        await call("set_midi_notes",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=midi["expected_state"],mode="replace",notes=notes)
                        before=await call("analyze_composition",start_frame="0",end_frame="384000",target=target)
                        preview=await call("preview_composition",instance=instance,request_id=str(uuid.uuid4()),render_id=before["render_id"])
                        shutil.copy2(preview["path"],output/(name+".wav"))
                        edit_params={"noise_decay_ms":160} if index==2 else {"pitch_env_semitones":20,"body_decay_ms":1000}
                        edited=await patch(target,parameters=edit_params)
                        changed=await call("analyze_composition",start_frame="0",end_frame="384000",target=target)
                        assert changed["metrics"]!=before["metrics"]
                        undone=await call("undo_instrument",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=edited["expected_state"])
                        restored=await call("analyze_composition",start_frame="0",end_frame="384000",target=target)
                        assert restored["metrics"]==before["metrics"]
                        restored_preview=await call("preview_composition",instance=instance,request_id=str(uuid.uuid4()),render_id=restored["render_id"])
                        assert Path(restored_preview["path"]).read_bytes()==(output/(name+".wav")).read_bytes(),"exact undo audio"
                        redone=await call("redo_instrument",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=undone["expected_state"])
                        redo_audio=await call("analyze_composition",start_frame="0",end_frame="384000",target=target)
                        assert redo_audio["metrics"]==changed["metrics"]
                        undone=await call("undo_instrument",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=redone["expected_state"])
                        saved=await call("save_preset",instance=instance,request_id=str(uuid.uuid4()),target=target,expected_state=undone["expected_state"],name="Test "+name)
                        recalled=await call("get_preset",preset_id=saved["preset"]["id"],preset_revision=saved["preset"]["revision"])
                        assert recalled["preset"]["parameters"]==factory["defaults"]
                        records[name]={"factory":factory,"metrics":before["metrics"],"edited_metrics":changed["metrics"]}
                    original=json.loads(fixture_command(process,"render composition").removeprefix("DONE "))
                    original_hash=hashlib.sha256((output/"composition.wav").read_bytes()).hexdigest()
                    assert 0<original["rms"]<original["peak"]<.95,original
                    fixture_command(process,"reopen")
                    layout=await call("get_midi_layout");assert layout["track_count"]==5 and layout["clip_count"]==5
                    reopened=json.loads(fixture_command(process,"render composition").removeprefix("DONE "))
                    assert original==reopened and hashlib.sha256((output/"composition.wav").read_bytes()).hexdigest()==original_hash
                    (output/"percussion-mcp.json").write_text(json.dumps({"voices":records,"mix":original,"reopen_exact_audio":True},indent=2)+"\n")
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait();raise
            assert process.returncode==0
    print("mcp_percussion_test: success; exact undo and project-reopen audio")
if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--fixture",required=True);parser.add_argument("--output-root",required=True)
    args=parser.parse_args();asyncio.run(exercise(str(Path(args.fixture).resolve()),Path(args.output_root).resolve()))
