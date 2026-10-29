"""Exercise official MCP initialization and tools against the production C bridge and engine."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]


def wire(path: str, payload: dict | bytes) -> dict:
    """Send raw bridge cases that the typed MCP interface deliberately cannot express."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(path)
        client.sendall(payload if isinstance(payload, bytes) else json.dumps(payload).encode() + b"\n")
        response = bytearray()
        while not response.endswith(b"\n"):
            chunk = client.recv(8192)
            assert chunk, "response closed early"
            response.extend(chunk)
        return json.loads(response)


def fixture_command(process, command: str) -> str:
    """Synchronize a simulated UI mutation with the fixture's owning application thread."""
    process.stdin.write(command + "\n")
    process.stdin.flush()
    result = process.stdout.readline().strip()
    assert result.startswith("DONE"), result
    return result


async def check(fixture: str, path: str, write: bool, prior_instance: str | None = None) -> str:
    """Test negotiated MCP schemas and authored-state failure boundaries in one endpoint lifetime."""
    process = subprocess.Popen([fixture, path, "write" if write else "read"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True, bufsize=1)
    try:
        assert process.stdout.readline().strip() == "READY"
        assert os.stat(path).st_mode & 0o777 == 0o600
        refused = subprocess.run([fixture, path, "read"], capture_output=True, timeout=10)
        assert refused.returncode == 3, "existing socket must be refused"
        env = dict(os.environ, DAW_CONTROL_SOCKET=path)
        params = StdioServerParameters(command=str(ROOT / "tools/mcp/run.sh"), env=env)
        async with stdio_client(params) as (read, send):
            async with ClientSession(read, send) as session:
                initialized = await session.initialize()
                tools = await session.list_tools()
                expected_tools = {"capabilities", "inspect_project", "get_instrument", "set_instrument",
                                  "request_status", "undo_instrument", "redo_instrument", "list_presets", "get_preset",
                                  "save_preset", "apply_preset", "preview_preset", "analyze_instrument", "analyze_preset", "get_midi_region", "set_midi_notes",
                                  "create_midi_pattern", "undo_midi", "redo_midi", "get_midi_layout", "create_midi_region",
                                  "extend_midi_region", "undo_midi_region", "redo_midi_region", "list_effects", "get_effect_chain", "edit_effect_chain", "undo_effect_chain", "redo_effect_chain", "analyze_composition", "preview_composition", "list_effect_presets", "get_effect_preset", "save_effect_preset", "apply_effect_preset", "transform_midi", "get_arrangement", "arrange_regions", "undo_arrangement", "redo_arrangement", "get_routing", "create_return", "create_group", "edit_routing", "undo_routing", "redo_routing", "get_automation", "set_automation", "undo_automation", "redo_automation",
                                  "cancel_song_export", "capture_ab", "capture_mix", "edit_eq", "get_ab_comparison", "get_eq", "get_live_meters", "get_mix_capture", "get_mix_snapshot", "get_song_export", "get_track_controls", "listen_ab", "redo_eq", "redo_track_control", "set_monitor_mode", "set_track_control", "start_song_export", "stop_ab", "undo_eq", "undo_track_control"}
                assert {tool.name for tool in tools.tools} == expected_tools
                assert next(tool for tool in tools.tools if tool.name == "set_instrument").inputSchema["properties"]["target"]

                async def call(name: str, **arguments) -> dict:
                    """Require protocol success and decode the server's structured domain result."""
                    result = await session.call_tool(name, arguments)
                    assert not result.isError, result
                    return result.structuredContent or json.loads(result.content[0].text)

                cap = await call("capabilities")
                assert cap["ok"] and cap["writes_enabled"] == write
                if prior_instance:
                    assert cap["instance"] != prior_instance
                    assert (await call("request_status", instance=prior_instance, request_id="old-request"))["code"] == "stale_instance"
                assert len(cap["presets"]) == 39 and len(cap["parameters"]) == 32
                presets = {preset["id"]: preset for preset in cap["presets"]}
                assert set(presets["pure_sine"]["visible_parameters"]) == {"level", "tone", "attack_ms", "decay_ms", "sustain", "release_ms"}
                assert len(presets["synth_lab"]["visible_parameters"]) == 12
                for name in ("filtered_keys", "filtered_pad", "sweep_bass", "filter_pluck", "moving_pad"):
                    assert len(presets[name]["visible_parameters"]) == 16
                    assert "cutoff_hz" in presets[name]["visible_parameters"]
                for name in ("rounded_keys", "rounded_pluck", "rounded_pad"):
                    assert len(presets[name]["visible_parameters"]) == 10
                    assert "vibrato_depth" not in presets[name]["visible_parameters"]
                scales = {parameter["id"]: parameter["control_scale"] for parameter in cap["parameters"]}
                assert scales["attack_ms"] == scales["decay_ms"] == scales["release_ms"] == "log1p_ms"
                assert scales["level"] == "linear"
                assert scales["cutoff_hz"] == "log_hz" and scales["resonance_q"] == "linear"
                project = await call("inspect_project")
                assert project["ok"] and project["next_offset"] == -1
                empty_page = await call("inspect_project", offset=project["total"])
                assert empty_page["targets"] == [] and empty_page["next_offset"] == -1
                target = next(item for item in project["targets"] if item["kind"] == "midi_region")
                target = {key: value for key, value in target.items() if key != "name"}
                before = await call("get_instrument", target=target)
                assert before["ok"] and before["instrument"]["inherits_track"]
                assert fixture_command(process, "changes") == "DONE 0", "reads must not request authored UI refresh"
                arguments = dict(instance=cap["instance"], request_id=str(uuid.uuid4()), target=target,
                                 expected_state=before["expected_state"], preset="synth_lab",
                                 parameters={"level": .37, "attack_ms": 73})
                if write:
                    old_token = before["expected_state"].replace(cap["instance"], "0" * 32)
                    rejected = await call("set_instrument", **dict(arguments, request_id=str(uuid.uuid4()), expected_state=old_token))
                    assert rejected["code"] == "state_conflict", "tokens must bind to endpoint lifetime too"
                edited = await call("set_instrument", **arguments)
                if not write:
                    assert edited["code"] == "read_only"
                    assert (await call("get_instrument", target=target))["expected_state"] == before["expected_state"]
                    print(f"mcp_control_test: read-only passed (MCP {initialized.protocolVersion})")
                    return cap["instance"]
                assert edited["ok"] and edited["code"] == "applied", edited
                assert edited["request_id"] == arguments["request_id"]
                assert abs(edited["instrument"]["parameters"]["level"] - .37) < 1e-6
                assert edited["instrument"]["parameters"]["attack_ms"] == 73
                assert not edited["instrument"]["inherits_track"]
                assert await call("set_instrument", **arguments) == edited
                assert fixture_command(process, "end_gesture") == "DONE 1 0", "replay must not add history"
                assert fixture_command(process, "changes") == "DONE 1", "replay must not refresh authored UI"
                assert await call("request_status", instance=cap["instance"], request_id=arguments["request_id"]) == edited
                collision = dict(arguments, parameters={"level": .5})
                assert (await call("set_instrument", **collision))["code"] == "request_id_conflict"
                stale = dict(arguments, request_id=str(uuid.uuid4()))
                assert (await call("set_instrument", **stale))["code"] == "state_conflict"
                history = dict(instance=cap["instance"], request_id=str(uuid.uuid4()), target=target,
                               expected_state=edited["expected_state"])
                undone = await call("undo_instrument", **history)
                assert undone["ok"] and undone["expected_state"] == before["expected_state"]
                assert fixture_command(process, "end_gesture") == "DONE 0 1"
                redone = await call("redo_instrument", **dict(history, request_id=str(uuid.uuid4()), expected_state=undone["expected_state"]))
                assert redone["ok"] and redone["expected_state"] == edited["expected_state"]
                noop = dict(arguments, request_id=str(uuid.uuid4()), expected_state=edited["expected_state"])
                assert (await call("set_instrument", **noop))["code"] == "unchanged"
                for parameters in ({"unknown": 1}, {"level": 1e9}, {"attack_ms": -1}):
                    invalid = dict(noop, request_id=str(uuid.uuid4()), parameters=parameters)
                    assert (await call("set_instrument", **invalid))["code"] == "invalid_request"
                fixture_command(process, "begin_gesture")
                assert (await call("set_instrument", **dict(noop, request_id=str(uuid.uuid4()), parameters={"level": .4})))["code"] == "busy"
                fixture_command(process, "end_gesture")
                assert fixture_command(process, "changes") == "DONE 3", "reads/no-ops/rejections must not refresh authored UI"
                fixture_command(process, "ui_edit")
                assert (await call("set_instrument", **dict(noop, request_id=str(uuid.uuid4()))))["code"] == "state_conflict"
                # A lost response remains discoverable under the original ID without replaying the action.
                current = await call("get_instrument", target=target)
                lost = dict(arguments, request_id=str(uuid.uuid4()), expected_state=current["expected_state"])
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as disconnected:
                    disconnected.connect(path)
                    disconnected.sendall(json.dumps(dict(lost, op="set_instrument")).encode() + b"\n")
                recovered = await call("request_status", instance=cap["instance"], request_id=lost["request_id"])
                assert recovered["ok"] and recovered["code"] == "applied"
                assert (await call("request_status", instance=cap["instance"], request_id="never-sent"))["code"] == "unknown_request"
                assert (await call("request_status", instance="old-process", request_id=lost["request_id"]))["code"] == "stale_instance"
                wrong_history = dict(history, request_id=str(uuid.uuid4()), expected_state=recovered["expected_state"],
                                     target=dict(target, kind="track", clip_id="0"))
                track_state = await call("get_instrument", target=wrong_history["target"])
                wrong_history["expected_state"] = track_state["expected_state"]
                assert (await call("undo_instrument", **wrong_history))["code"] == "history_conflict"
                bad_typed = await session.call_tool("set_instrument", dict(noop, request_id=str(uuid.uuid4()), parameters={"level": "0.1"}))
                assert bad_typed.isError
                assert wire(path, {"op": "project", "offset": -1})["code"] == "invalid_offset"
                assert wire(path, {"op": "capabilities", "offset": 0})["code"] == "invalid_request"
                assert wire(path, {"op": "get_instrument", "target": dict(target, track_id="18446744073709551616")})["code"] == "invalid_target"
                assert wire(path, b'{"op":"capabilities","op":"project"}\n')["code"] == "invalid_request"
                assert wire(path, b'{"op":"capabilities"}garbage\n')["code"] == "invalid_request"
                assert wire(path, b'x' * 16384 + b'\n')["code"] == "invalid_frame"
                fixture_command(process, "replace")
                assert (await call("get_instrument", target=target))["code"] == "stale_target"
                assert (await call("set_instrument", **dict(noop, request_id=str(uuid.uuid4()))))["code"] == "stale_target"
                # Rejections are retained too; a full ledger refuses new IDs instead of evicting old writes.
                used = (await call("capabilities"))["receipts_used"]
                for _ in range(128 - used):
                    assert (await call("set_instrument", **dict(noop, request_id=str(uuid.uuid4()))))["code"] == "stale_target"
                assert (await call("set_instrument", **dict(noop, request_id=str(uuid.uuid4()))))["code"] == "receipt_capacity_reached"
                assert await call("request_status", instance=cap["instance"], request_id=arguments["request_id"]) == edited
                print(f"mcp_control_test: write/replay/history/conflicts/lifetime/limits passed (MCP {initialized.protocolVersion})")
                return cap["instance"]
    finally:
        # Teardown must also finish while a client has submitted an incomplete frame.
        stalled = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        if process.poll() is None:
            stalled.connect(path)
            stalled.sendall(b'{')
            await asyncio.sleep(.05)
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            raise
        stalled.close()
        assert process.returncode == 0, process.returncode
        assert not os.path.exists(path), "shutdown must remove owned endpoint"


async def main(fixture: str) -> None:
    """Use private ephemeral endpoints and leave installed applications and user projects untouched."""
    with tempfile.TemporaryDirectory(prefix="daw-mcp-", dir="/tmp") as directory:
        os.chmod(directory, 0o700)
        unsafe = Path(directory) / "unsafe"
        unsafe.mkdir(mode=0o755)
        os.chmod(unsafe, 0o755)  # Keep the refusal fixture unsafe even under a private process umask.
        refused = subprocess.run([fixture, str(unsafe / "control.sock"), "write"], capture_output=True, timeout=10)
        assert refused.returncode == 3 and not (unsafe / "control.sock").exists()
        path = str(Path(directory) / "control.sock")
        instance = await check(fixture, path, True)
        await check(fixture, path, False, instance)
    print("mcp_control_test: success")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    asyncio.run(main(str(Path(parser.parse_args().fixture).resolve())))
