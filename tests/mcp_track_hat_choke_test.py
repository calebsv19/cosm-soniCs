"""Qualify cross-region hat context using the actual SDK, native bridge and production bounce."""
import argparse, asyncio, hashlib, json, os, shutil, subprocess, tempfile, uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import fixture_command
ROOT = Path(__file__).resolve().parents[1]

async def exercise(fixture, output):
    """Create adjacent hat regions on one track, verify guarded edits, and deliver an editable groove."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sonics-track-hats-") as directory:
        root = Path(directory)
        env = dict(os.environ, DAW_CONTROL_SOCKET=str(root / "control.sock"), DAW_PRESET_LIBRARY=str(root / "library"))
        process = subprocess.Popen([fixture, env["DAW_CONTROL_SOCKET"], "empty", str(output)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env, cwd=root)
        try:
            assert process.stdout.readline().strip() == "READY"
            async with stdio_client(StdioServerParameters(command=str(ROOT / "tools/mcp/run.sh"), env=env)) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    async def call(tool, **args):
                        """Reject protocol and domain failures explicitly."""
                        result = await session.call_tool(tool, args)
                        assert not result.isError, result
                        value = result.structuredContent or json.loads(result.content[0].text)
                        assert value["ok"], value
                        return value
                    cap = await call("capabilities"); instance = cap["instance"]
                    async def patch(target, **args):
                        """Bind each mutation to a fresh instrument token."""
                        state = await call("get_instrument", target=target)
                        return await call("set_instrument", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=state["expected_state"], **args)
                    async def create(start, length, target=None):
                        """Use existing structural action guards for one adjacent region."""
                        layout = await call("get_midi_layout")
                        args = dict(instance=instance, request_id=str(uuid.uuid4()), expected_layout=layout["expected_layout"], start_frame=str(start), duration_frames=str(length))
                        if target: args["target"] = target
                        return (await call("create_midi_region", **args))["created_target"]
                    async def notes(target, hits):
                        """Replace notes against the region's current identity and note revision."""
                        state = await call("get_midi_region", target=target)
                        values = [dict(start_frame=str(round(start * 24000)), duration_frames=str(round(duration * 24000)), pitch=pitch, velocity=velocity) for start, duration, pitch, velocity in hits]
                        await call("set_midi_notes", instance=instance, request_id=str(uuid.uuid4()), target=target, expected_state=state["expected_state"], mode="replace", notes=values)
                    async def render(target, label):
                        """Render an isolated region retaining track-context choke fences."""
                        audio = await call("analyze_composition", start_frame="0", end_frame="384000", target=target)
                        preview = await call("preview_composition", instance=instance, request_id=str(uuid.uuid4()), render_id=audio["render_id"])
                        path = output / (label + ".wav"); shutil.copy2(preview["path"], path)
                        return audio["metrics"], hashlib.sha256(path.read_bytes()).hexdigest()
                    hats = []
                    for index in range(8):
                        target = await create(index * 48000, 48000, dict(hats[0], kind="track", clip_id="0") if hats else None)
                        hats.append(target)
                        await patch(target, preset="open_hat" if index % 2 == 0 else "closed_hat", parameters={"hat_choke": 1, "noise_decay_ms": 900 if index % 2 == 0 else 60, "level": .85})
                        await notes(target, [(1.5, .02, 46, .65)] if index % 2 == 0 else [(0, .02, 42, .65), (.5, .02, 42, .35), (1, .02, 42, .55), (1.5, .02, 42, .3)])
                    assert len({target["track_id"] for target in hats}) == 1 and len({target["clip_id"] for target in hats}) == 8
                    before, original_hash = await render(hats[0], "Open_Hat_Track_Choked")
                    changed = await patch(hats[1], parameters={"hat_choke": 0})
                    unchoked, unchoked_hash = await render(hats[0], "Open_Hat_Unchoked")
                    assert original_hash != unchoked_hash and unchoked["rms"] > before["rms"]
                    undone = await call("undo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=hats[1], expected_state=changed["expected_state"])
                    assert await render(hats[0], "Open_Hat_Track_Choked") == (before, original_hash)
                    redone = await call("redo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=hats[1], expected_state=undone["expected_state"])
                    assert await render(hats[0], "Open_Hat_Unchoked") == (unchoked, unchoked_hash)
                    await call("undo_instrument", instance=instance, request_id=str(uuid.uuid4()), target=hats[1], expected_state=redone["expected_state"])
                    await render(dict(hats[0], kind="track", clip_id="0"), "Adjacent_Hat_Regions")
                    for voice in ("kick_drum", "snare_drum", "driven_808"):
                        target = await create(0, 384000)
                        await patch(target, preset=voice, parameters={"mono": 1, "glide_ms": 140, "level": .75} if voice == "driven_808" else {})
                        if voice == "kick_drum": hits = [(b, .05, 36, .8) for b in range(0, 16, 2)]
                        elif voice == "snare_drum": hits = [(b + .015, .05, 50, .75) for b in range(1, 16, 2)]
                        else:
                            hits = []
                            for bar in range(4):
                                base = 36 if bar < 2 else 33
                                hits += [(bar * 4, .9, base, .7), (bar * 4 + .75, .9, base + 7, .65), (bar * 4 + 1.5, .7, base + 12, .6), (bar * 4 + 2.5, .7, base, .7)]
                        await notes(target, hits)
                    mix = json.loads(fixture_command(process, "render composition").removeprefix("DONE "))
                    mix_hash = hashlib.sha256((output / "composition.wav").read_bytes()).hexdigest()
                    assert 0 < mix["rms"] < mix["peak"] < .95
                    fixture_command(process, "reopen")
                    assert json.loads(fixture_command(process, "render composition").removeprefix("DONE ")) == mix
                    assert hashlib.sha256((output / "composition.wav").read_bytes()).hexdigest() == mix_hash
                    (output / "track-hat-choke-mcp.json").write_text(json.dumps(dict(hat_regions=8, hat_track_count=1, choked=before, unchoked=unchoked, exact_undo=True, exact_redo=True, exact_reopen=True, mix=mix, mix_sha256=mix_hash), indent=2) + "\n")
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait(); raise
            assert process.returncode == 0
    print("mcp_track_hat_choke_test: success; adjacent regions, guarded edits and exact project reopen")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--fixture", required=True); parser.add_argument("--output-root", required=True)
    args = parser.parse_args(); asyncio.run(exercise(str(Path(args.fixture).resolve()), Path(args.output_root).resolve()))
