#!/bin/sh
# Start the stdio adapter with the repository's isolated, pinned SDK environment.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python_bin="$repo_root/build/mcp-venv/bin/python"
if [ ! -x "$python_bin" ]; then
    echo 'Missing MCP environment. Follow tools/mcp/README.md setup first.' >&2
    exit 1
fi
exec "$python_bin" "$repo_root/tools/mcp/server.py"
