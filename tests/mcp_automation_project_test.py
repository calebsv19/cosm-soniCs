"""Prove automation editing, original-level previews and save behavior on a preserved song copy."""
import argparse
import asyncio
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT


async def check(fixture, project, output):
    """Apply musical callback automation in an isolated clone, never in the user's saved song."""
    output.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(project.read_bytes()).hexdigest()
    shutil.copy2(project, output / "baseline-input.json")
    with tempfile.TemporaryDirectory(prefix="daw-auto-project-", dir="/tmp") as directory:
        endpoint = str(Path(directory) / "control.sock")
        env = dict(os.environ, DAW_CONTROL_SOCKET=endpoint, DAW_PRESET_LIBRARY=str(output / "library"))
        process = subprocess.Popen([fixture, endpoint, "write", str(output)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, bufsize=1, env=env)
        try:
            assert process.stdout.readline().strip() == "READY"
            def command(value):
                """Run one owner-thread fixture operation and wait for its terminal result."""
                process.stdin.write(value + "\n"); process.stdin.flush()
                response = process.stdout.readline().strip()
                assert response.startswith("DONE"), response
                return response
            command("load_project")
            async with stdio_client(StdioServerParameters(command=sys.executable,
                args=[str(ROOT / "tools/mcp/server.py")], env=env)) as (reader, writer):
                async with ClientSession(reader, writer) as session:
                    await session.initialize()
                    async def call(name, **args):
                        """Require both protocol success and a successful application outcome."""
                        response = await session.call_tool(name, args)
                        assert not response.isError, response
                        result = response.structuredContent or json.loads(response.content[0].text)
                        assert result["ok"], result
                        return result
                    instance = (await call("capabilities"))["instance"]
                    layout = await call("get_arrangement")
                    assert layout["track_count"] == 7 and layout["clip_count"] == 40
                    baseline_layout = [(p["track_id"], p["start_frame"], p["duration_frames"]) for p in layout["placements"]]
                    records = []
                    async def preview(label):
                        """Render and publish a bounded ten-second callback preview at original level."""
                        rendered = await call("analyze_composition", start_frame="2560000", end_frame="3040000", tail_frames="48000")
                        published = await call("preview_composition", instance=instance, request_id=str(uuid.uuid4()), render_id=rendered["render_id"])
                        path = Path(published["path"])
                        destination = output / (label + ".wav")
                        shutil.copy2(path, destination)
                        return dict(metrics=rendered["metrics"], sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), file=destination.name)
                    before = await preview("callback-before")
                    melodic = layout["tracks"][4]["track_id"]
                    pad = layout["tracks"][5]["track_id"]
                    for track_id, balance in [(melodic, -.7), (pad, .7)]:
                        region = next(p for p in layout["placements"] if p["track_id"] == track_id and p["start_frame"] == "2560000")
                        target = dict(generation=layout["generation"], track_id=track_id, clip_id=region["clip_id"], kind="region")
                        duration = int(region["duration_frames"])
                        for lane_target, values in [(0, [-12.0, -3.0, 0.0]), (1, [0.0, balance, balance])]:
                            read = await call("get_automation", target=target, lane_target=lane_target)
                            points = [dict(frame=str(frame), value=value, curve="smooth")
                                for frame, value in zip([0, duration // 4, duration], values)]
                            edited = await call("set_automation", instance=instance, request_id=str(uuid.uuid4()), target=target,
                                expected_arrangement=read["expected_arrangement"], lane_target=lane_target,
                                domain="physical", before="hold", after="hold", points=points)
                            records.append(dict(target=target, lane_target=lane_target, points=points, guard=edited["expected_arrangement"]))
                    after = await preview("callback-after")
                    assert after["sha256"] != before["sha256"] and after["metrics"]["rms"] != before["metrics"]["rms"]
                    final_layout = await call("get_arrangement")
                    assert [(p["track_id"], p["start_frame"], p["duration_frames"]) for p in final_layout["placements"]] == baseline_layout
                    command("reopen")
                    reopened = await preview("callback-reopened")
                    assert reopened["sha256"] == after["sha256"]
                    new_layout = await call("get_arrangement")
                    assert new_layout["track_count"] == 7 and new_layout["clip_count"] == 40
                    for t in [4, 5]:
                        track_id = new_layout["tracks"][t]["track_id"]
                        region = next(p for p in new_layout["placements"] if p["track_id"] == track_id and p["start_frame"] == "2560000")
                        target = dict(generation=new_layout["generation"], track_id=track_id, clip_id=region["clip_id"], kind="region")
                        for lane_target in [0, 1]:
                            lane = await call("get_automation", target=target, lane_target=lane_target)
                            assert lane["domain"] == "physical" and lane["before"] == lane["after"] == "hold"
                            assert all(p["curve"] == "smooth" for p in lane["points"])
                    assert hashlib.sha256(project.read_bytes()).hexdigest() == source_hash
                    shutil.copy2(output / "midi-project.json", output / "Night_Pocket_Automation_Preview.json")
                    (output / "result.json").write_text(json.dumps(dict(saved_project_sha256=source_hash,
                        original_unchanged=True, tracks=7, regions=40, timing_unchanged=True,
                        description="Callback pluck/pad rise from -12 dB and split to opposite 70% balances over smooth curves",
                        range_seconds=[2560000/48000,3040000/48000], before=before, after=after,
                        reopen_matches_after=True, automation_edits=records), indent=2) + "\n")
        finally:
            process.terminate(); process.wait(timeout=10)
            assert process.returncode == 0, process.returncode
    print("mcp_automation_project_test: success")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--output-root", required=True)
    args = parser.parse_args()
    asyncio.run(check(str(Path(args.fixture).resolve()), Path(args.project).resolve(), Path(args.output_root).resolve()))
