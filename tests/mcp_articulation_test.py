"""Qualify editable mono/glide and hat-choke patterns through the actual native MCP bridge."""
import argparse, asyncio, hashlib, json, os, shutil, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import fixture_command
ROOT = Path(__file__).resolve().parents[1]

async def exercise(fixture, output):
    """Author four tracks, compare articulation states and verify exact undo and project reopen audio."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sonics-articulation-") as directory:
        root = Path(directory)
        env = dict(os.environ, DAW_CONTROL_SOCKET=str(root / "control.sock"), DAW_PRESET_LIBRARY=str(root / "library"))
        process = subprocess.Popen([fixture, env["DAW_CONTROL_SOCKET"], "empty", str(output)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env, cwd=root)
        try:
            assert process.stdout.readline().strip() == "READY"
            async with stdio_client(StdioServerParameters(command=str(ROOT / "tools/mcp/run.sh"), env=env)) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    async def call(tool, **args):
                        """Require successful protocol and domain responses."""
                        result = await session.call_tool(tool, args)
                        assert not result.isError, result
                        value = result.structuredContent or json.loads(result.content[0].text)
                        assert value["ok"], value
                        return value
                    cap = await call("capabilities")
                    assert len(cap["presets"]) == 25 and len(cap["parameters"]) == 27
                    instance = cap["instance"]
                    async def patch(target, **args):
                        """Use a fresh instrument token for each guarded mutation."""
                        current = await call("get_instrument", target=target)
                        return await call("set_instrument", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=current["expected_state"], **args)
                    async def render(target, label):
                        """Publish a current isolated production render without normalization."""
                        audio = await call("analyze_composition", start_frame="0", end_frame="384000", target=target)
                        preview = await call("preview_composition", instance=instance, request_id=str(uuid.uuid4()), render_id=audio["render_id"])
                        path = output / (label + ".wav")
                        shutil.copy2(preview["path"], path)
                        return audio["metrics"], hashlib.sha256(path.read_bytes()).hexdigest()
                    targets = {}
                    for name in ("kick_drum", "snare_drum", "hat_kit", "driven_808"):
                        layout = await call("get_midi_layout")
                        region = await call("create_midi_region", instance=instance, request_id=str(uuid.uuid4()), expected_layout=layout["expected_layout"], start_frame="0", duration_frames="384000")
                        target = region["created_target"]; targets[name] = target
                        settings = {"mono": 1, "glide_ms": 140, "level": .8} if name == "driven_808" else {"noise_decay_ms": 900} if name == "hat_kit" else {}
                        await patch(target, preset=name, parameters=settings)
                        if name == "kick_drum": hits = [(b, .05, 36, .8) for b in range(0, 16, 2)]
                        elif name == "snare_drum": hits = [(b + .015, .05, 50, .8) for b in range(1, 16, 2)]
                        elif name == "hat_kit": hits = [(b / 2, .02, 46 if b % 8 == 3 else 42, .75 if b % 8 == 3 else .35 if b % 2 else .6) for b in range(32)]
                        else:
                            hits = []
                            for bar in range(4):
                                root_note = 36 if bar < 2 else 33
                                hits += [(bar * 4, .9, root_note, .7), (bar * 4 + .75, .9, root_note + 7, .65), (bar * 4 + 1.5, .7, root_note + 12, .6), (bar * 4 + 2.5, .7, root_note, .7)]
                        notes = [dict(start_frame=str(round(start * 24000)), duration_frames=str(round(duration * 24000)), pitch=pitch, velocity=velocity) for start, duration, pitch, velocity in hits]
                        current = await call("get_midi_region", target=target)
                        await call("set_midi_notes", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=current["expected_state"], mode="replace", notes=notes)
                    records = {}
                    for name, parameter, value, label in (("driven_808", "glide_ms", 0, "808_No_Glide"), ("driven_808", "mono", 0, "808_Polyphonic"), ("hat_kit", "hat_choke", 0, "Hats_Unchoked")):
                        target = targets[name]
                        before, audio_hash = await render(target, "808_Glide" if name == "driven_808" else "Hats_Choked")
                        changed = await patch(target, parameters={parameter: value})
                        after, _ = await render(target, label)
                        assert before != after, parameter
                        undone = await call("undo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=changed["expected_state"])
                        restored, restored_hash = await render(target, "Restored_" + parameter)
                        assert restored == before and restored_hash == audio_hash
                        redone = await call("redo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=undone["expected_state"])
                        redo, _ = await render(target, "Redo_" + parameter)
                        assert redo == after
                        await call("undo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=redone["expected_state"])
                        records[parameter] = dict(before=before, changed=after, exact_undo=True, exact_redo=True)
                    for name in ("driven_808", "hat_kit"):
                        current = await call("get_instrument", target=targets[name])
                        saved = await call("save_preset", instance=instance, request_id=str(uuid.uuid4()), target=targets[name], expected_state=current["expected_state"], name="Articulation " + name)
                        recalled = await call("get_preset", preset_id=saved["preset"]["id"], preset_revision=saved["preset"]["revision"])
                        assert recalled["preset"]["parameters"] == current["instrument"]["effective_parameters"]
                    original = json.loads(fixture_command(process, "render composition").removeprefix("DONE "))
                    original_hash = hashlib.sha256((output / "composition.wav").read_bytes()).hexdigest()
                    assert 0 < original["rms"] < original["peak"] < .95
                    fixture_command(process, "reopen")
                    reopened = json.loads(fixture_command(process, "render composition").removeprefix("DONE "))
                    assert reopened == original and hashlib.sha256((output / "composition.wav").read_bytes()).hexdigest() == original_hash
                    (output / "articulation-mcp.json").write_text(json.dumps(dict(controls=records, mix=original, exact_reopen=True), indent=2) + "\n")
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            assert process.returncode == 0
    print("mcp_articulation_test: success; guarded mono/glide/choke and exact rendered undo/redo/reopen")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True); parser.add_argument("--output-root", required=True)
    args = parser.parse_args()
    asyncio.run(exercise(str(Path(args.fixture).resolve()), Path(args.output_root).resolve()))
