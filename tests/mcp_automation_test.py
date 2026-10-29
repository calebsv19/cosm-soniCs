"""Qualify region automation through the official MCP SDK and production engine."""
import argparse
import asyncio
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire


async def qualify(fixture, output, write=True):
    """Check guards, replay, history, both region kinds, pagination and sound persistence."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="daw-automation-mcp-", dir="/tmp") as directory:
        endpoint = str(Path(directory) / "control.sock")
        env = dict(os.environ, DAW_CONTROL_SOCKET=endpoint)
        process = subprocess.Popen([fixture, endpoint, "write" if write else "read", str(output)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
        try:
            assert process.stdout.readline().strip() == "READY"
            async with stdio_client(StdioServerParameters(command=sys.executable,
                args=[str(ROOT / "tools/mcp/server.py")], env=env)) as (reader, writer):
                async with ClientSession(reader, writer) as session:
                    await session.initialize()
                    tools = {tool.name: tool for tool in (await session.list_tools()).tools}
                    assert len(tools) == 70 and tools["get_automation"].annotations.readOnlyHint

                    async def call(name, **args):
                        """Decode domain outcomes separately from successful MCP transport."""
                        result = await session.call_tool(name, args)
                        assert not result.isError, result
                        return result.structuredContent or json.loads(result.content[0].text)

                    def command(value):
                        """Issue one fixture-owner action and consume its terminal response."""
                        process.stdin.write(value + "\n")
                        process.stdin.flush()
                        response = process.stdout.readline().strip()
                        assert response.startswith("DONE"), response
                        return response

                    def render(label):
                        """Use the real engine bounce path and fingerprint float output."""
                        command("render " + label)
                        return hashlib.sha256((output / (label + ".wav")).read_bytes()).hexdigest()

                    cap = await call("capabilities")
                    assert cap["automation"]["shared_history"]
                    instance = cap["instance"]
                    arrangement = await call("get_arrangement")
                    placement = arrangement["placements"][0]
                    target = dict(generation=arrangement["generation"], track_id=placement["track_id"],
                        clip_id=placement["clip_id"], kind="region")
                    original = await call("get_automation", target=target)
                    assert original["ok"] and original["domain"] == "offset" and original["point_count"] == 0
                    advertised = {row["lane_target"] for row in original["available_targets"]}
                    assert {0, 1, 2}.issubset(advertised)
                    assert 25 not in advertised  # The fixture's melodic voice has no percussion click control.
                    points = [dict(frame="0", value=-12.0, curve="smooth"),
                              dict(frame="24000", value=-3.0, curve="hold"),
                              dict(frame="48000", value=0.0, curve="linear")]
                    args = dict(instance=instance, request_id=str(uuid.uuid4()), target=target,
                        expected_arrangement=original["expected_arrangement"], lane_target=0,
                        domain="physical", before="hold", after="hold", points=points)
                    if not write:
                        assert (await call("set_automation", **args))["code"] == "read_only"
                        assert (await call("get_automation", target=target))["point_count"] == 0
                        return
                    baseline_hash = render("baseline")
                    applied = await call("set_automation", **args)
                    assert applied["ok"] and applied["unit"] == "dB" and applied["points"] == points
                    assert await call("set_automation", **args) == applied
                    assert await call("request_status", instance=instance, request_id=args["request_id"]) == applied
                    assert (await call("set_automation", **dict(args, request_id=str(uuid.uuid4()))))["code"] == "state_conflict"
                    wet_hash = render("automated")
                    assert wet_hash != baseline_hash
                    undo = await call("undo_automation", instance=instance, request_id=str(uuid.uuid4()), target=target,
                        expected_arrangement=applied["expected_arrangement"])
                    assert undo["ok"] and undo["point_count"] == 0 and render("undone") == baseline_hash
                    redo = await call("redo_automation", instance=instance, request_id=str(uuid.uuid4()), target=target,
                        expected_arrangement=undo["expected_arrangement"])
                    assert redo["ok"] and render("redone") == wet_hash
                    for invalid in (
                        [dict(frame="0", value=-3.0, curve="linear"), dict(frame="0", value=0.0, curve="linear")],
                        [dict(frame="120001", value=-3.0, curve="linear")],
                        [dict(frame="0", value=25.0, curve="linear")]):
                        rejected = await call("set_automation", **dict(args, request_id=str(uuid.uuid4()),
                            expected_arrangement=redo["expected_arrangement"], points=invalid))
                        assert rejected["code"] == "invalid_lane"
                    command("begin_gesture")
                    assert (await call("set_automation", **dict(args, request_id=str(uuid.uuid4()),
                        expected_arrangement=redo["expected_arrangement"])))["code"] == "busy"
                    command("end_gesture")
                    command("reopen")
                    assert render("reopened") == wet_hash
                    refreshed = await call("get_arrangement")
                    item = refreshed["placements"][0]
                    target = dict(generation=refreshed["generation"], track_id=item["track_id"], clip_id=item["clip_id"], kind="region")
                    assert (await call("get_automation", target=target))["points"] == points
                    command("automation_dense")
                    first = await call("get_automation", target=target)
                    assert first["point_count"] == 160 and len(first["points"]) == 128 and first["next_offset"] == 128
                    second = await call("get_automation", target=target, offset=128, expected_arrangement=first["expected_arrangement"])
                    assert len(second["points"]) == 32 and second["next_offset"] == -1
                    command("ui_edit")
                    assert (await call("get_automation", target=target, offset=128,
                        expected_arrangement=first["expected_arrangement"]))["code"] == "state_conflict"
                    command("audio_region")
                    audio_arrangement = await call("get_arrangement")
                    item = audio_arrangement["placements"][-1]
                    audio_target = dict(generation=audio_arrangement["generation"], track_id=item["track_id"], clip_id=item["clip_id"], kind="region")
                    audio = await call("get_automation", target=audio_target, lane_target=1)
                    assert len(audio["available_targets"]) == 2
                    pan = await call("set_automation", instance=instance, request_id=str(uuid.uuid4()), target=audio_target,
                        expected_arrangement=audio["expected_arrangement"], lane_target=1,
                        domain="physical", before="hold", after="hold", points=[dict(frame="0", value=-.75, curve="linear")])
                    assert pan["ok"] and pan["unit"] == "balance"
                    bad = wire(endpoint, dict(args, op="set_automation", request_id=str(uuid.uuid4()), points=[], typo=True))
                    assert bad["code"] == "invalid_request"
                    (output / "result.json").write_text(json.dumps(dict(official_sdk=True, tool_count=len(tools),
                        audio_and_midi_regions=True, replay_exact=True, stale_guards=True, history_sound_exact=True,
                        save_reopen_sound_exact=True, bounded_pages=True, baseline_hash=baseline_hash,
                        automated_hash=wet_hash), indent=2) + "\n")
        finally:
            process.terminate()
            process.wait(timeout=10)
            assert process.returncode == 0, process.returncode


async def main(fixture, output):
    """Exercise write-enabled and read-only endpoints without touching the installed app."""
    await qualify(fixture, output)
    await qualify(fixture, output / "readonly", False)
    print("mcp_automation_test: success")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--output-root", required=True)
    args = parser.parse_args()
    asyncio.run(main(str(Path(args.fixture).resolve()), Path(args.output_root).resolve()))
