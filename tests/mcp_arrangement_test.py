"""Qualify guarded arrangement batches through the official SDK and production bridge."""
import argparse
import asyncio
import json
import os
import subprocess
import tempfile
import uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, wire, fixture_command


def identity():
    """Allocate one deliberate operation's immutable receipt identity."""
    return str(uuid.uuid4())


async def check(fixture, output):
    """Exercise accepted edits, exact history, strict refusals and project reopening."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="daw-arrangement-mcp-") as directory:
        endpoint = str(Path(directory) / "control.sock")
        env = dict(os.environ, DAW_CONTROL_SOCKET=endpoint, DAW_PRESET_LIBRARY=str(Path(directory) / "library"))
        process = subprocess.Popen([fixture, endpoint, "empty", str(output)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
        try:
            assert process.stdout.readline().strip() == "READY"
            async with stdio_client(StdioServerParameters(command=str(ROOT / "tools/mcp/run.sh"), env=env)) as (r, w):
                async with ClientSession(r, w) as session:
                    await session.initialize()
                    tools = {t.name: t for t in (await session.list_tools()).tools}
                    assert len(tools) == 45
                    assert tools["get_arrangement"].annotations.readOnlyHint

                    async def call(name, **args):
                        """Keep domain outcomes distinct from successful MCP transport."""
                        result = await session.call_tool(name, args)
                        assert not result.isError, result
                        return result.structuredContent or json.loads(result.content[0].text)

                    instance = (await call("capabilities"))["instance"]
                    empty = await call("get_arrangement")
                    assert empty["ok"] and empty["clip_count"] == empty["track_count"] == 0
                    targets = []
                    for start in ["0", "96000"]:
                        layout = await call("get_midi_layout")
                        created = await call("create_midi_region", instance=instance, request_id=identity(),
                                             expected_layout=layout["expected_layout"], start_frame=start, duration_frames="48000",
                                             **({"target": track} if targets else {}))
                        assert created["ok"], created
                        targets.append(created["created_target"])
                        track = dict(targets[0], clip_id="0", kind="track")
                    region = await call("get_midi_region", target=targets[0])
                    notes = [dict(start_frame="0", duration_frames="12000", pitch=60, velocity=0.6)]
                    seeded = await call("set_midi_notes", instance=instance, request_id=identity(), target=targets[0],
                                        expected_state=region["expected_state"], mode="replace", notes=notes)
                    assert seeded["ok"], seeded
                    original = await call("get_arrangement")
                    args = dict(instance=instance, request_id=identity(), expected_arrangement=original["expected_arrangement"],
                                edits=[dict(operation="duplicate", target=targets[0], start_frame="192000")])
                    copied = await call("arrange_regions", **args)
                    assert copied["ok"] and copied["clip_count"] == 3, copied
                    copy = copied["output_targets"][0]
                    assert copy["clip_id"] != targets[0]["clip_id"]
                    assert await call("arrange_regions", **args) == copied
                    assert await call("request_status", instance=instance, request_id=args["request_id"]) == copied
                    assert (await call("get_arrangement", expected_arrangement=original["expected_arrangement"]))["code"] == "state_conflict"
                    assert (await call("get_midi_region", target=copy))["notes"] == seeded["notes"]
                    undone = await call("undo_arrangement", instance=instance, request_id=identity(), expected_arrangement=copied["expected_arrangement"])
                    assert undone["ok"] and undone["placements"] == original["placements"], undone
                    redone = await call("redo_arrangement", instance=instance, request_id=identity(), expected_arrangement=undone["expected_arrangement"])
                    assert redone["ok"] and redone["placements"] == copied["placements"], redone
                    swap = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=redone["expected_arrangement"],
                                      edits=[dict(operation="move", target=targets[0], start_frame="96000"),
                                             dict(operation="move", target=targets[1], start_frame="0")])
                    assert swap["ok"], swap
                    trimmed = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=swap["expected_arrangement"],
                                         edits=[dict(operation="trim", target=copy, duration_frames="24000")])
                    assert trimmed["ok"], trimmed
                    bad = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=trimmed["expected_arrangement"],
                                     edits=[dict(operation="trim", target=copy, duration_frames="100")])
                    assert bad["code"] == "invalid_request", bad
                    for edits in [[dict(operation="move", target=copy, start_frame="0")],
                                  [dict(operation="delete", target=copy, start_frame="0")],
                                  [dict(operation="move", target=copy, destination=copy, start_frame="300000")],
                                  [dict(operation="delete", target=copy), dict(operation="delete", target=copy)]]:
                        refused = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=trimmed["expected_arrangement"], edits=edits)
                        assert refused["code"] == "invalid_request", refused
                        assert (await call("get_arrangement"))["placements"] == trimmed["placements"]
                    fixture_command(process, "begin_gesture")
                    busy = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=trimmed["expected_arrangement"],
                                      edits=[dict(operation="delete", target=copy)])
                    assert busy["code"] == "busy", busy
                    fixture_command(process, "end_gesture")
                    deleted = await call("arrange_regions", instance=instance, request_id=identity(), expected_arrangement=trimmed["expected_arrangement"],
                                         edits=[dict(operation="delete", target=copy)])
                    assert deleted["ok"] and deleted["output_targets"] == [None] and deleted["clip_count"] == 2, deleted
                    restored = await call("undo_arrangement", instance=instance, request_id=identity(), expected_arrangement=deleted["expected_arrangement"])
                    assert restored["ok"] and restored["placements"] == trimmed["placements"], restored
                    rendered = fixture_command(process, "render composition")
                    assert json.loads(rendered[5:])["peak"] > 0
                    original_audio = (output / "composition.wav").read_bytes()
                    fixture_command(process, "reopen")
                    assert (await call("get_arrangement", expected_arrangement=restored["expected_arrangement"]))["code"] == "state_conflict"
                    reopened = await call("get_arrangement")
                    assert reopened["ok"] and reopened["clip_count"] == 3, reopened
                    fixture_command(process, "render composition")
                    assert original_audio == (output / "composition.wav").read_bytes()
                    raw = dict(op="arrange_regions", instance=instance, request_id=identity(), expected_arrangement=reopened["expected_arrangement"],
                               edits=[dict(operation="delete", target=copy, unknown=True)])
                    assert wire(endpoint, raw)["code"] == "invalid_request"
                    (output / "arrangement-acceptance.json").write_text(json.dumps(dict(tool_count=45, copied=copied, swapped=swap,
                                                                                      restored=restored, reopened=reopened,
                                                                                      exact_project_audio_reopen=True), indent=2))
        finally:
            if process.poll() is None:
                process.stdin.write("quit\n"); process.stdin.flush()
            process.wait(timeout=10)
            assert process.returncode == 0
        reader = subprocess.Popen([fixture, endpoint, "read"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
        try:
            assert reader.stdout.readline().strip() == "READY"
            instance = wire(endpoint, {"op": "capabilities"})["instance"]
            arrangement = wire(endpoint, {"op": "get_arrangement"})
            for op in ["arrange_regions", "undo_arrangement", "redo_arrangement"]:
                payload = dict(op=op, instance=instance, request_id=identity(), expected_arrangement=arrangement["expected_arrangement"])
                if op == "arrange_regions": payload["edits"] = []
                assert wire(endpoint, payload)["code"] == "read_only"
        finally:
            reader.stdin.write("quit\n"); reader.stdin.flush(); reader.wait(timeout=10)
            assert reader.returncode == 0
    print("mcp_arrangement_test: success (45 tools, atomic batches, guards, receipts, history, native engine audio/reopen and read-only admission)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--fixture", required=True); parser.add_argument("--output-root", required=True)
    arguments = parser.parse_args()
    asyncio.run(check(arguments.fixture, Path(arguments.output_root) / "mcp-arrangement"))
