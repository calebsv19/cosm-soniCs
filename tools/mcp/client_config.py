#!/usr/bin/env python3
"""Print a reviewable stdio client configuration without modifying client settings."""
import json
import os
from pathlib import Path

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    path = os.environ.get("DAW_CONTROL_SOCKET", f"/tmp/sonics-control-{os.getuid()}/control.sock")
    print(json.dumps({"mcpServers": {"sonics": {"command": str(root / "tools/mcp/run.sh"),
                                               "env": {"DAW_CONTROL_SOCKET": path}}}}, indent=2))
