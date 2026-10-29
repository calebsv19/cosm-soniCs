#!/usr/bin/env python3
"""Run unchanged release gates from verified inputs inside the owner's writable attempt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def git(source, *arguments):
    """Read one selected source repository without inheriting mutable Git controls."""
    return subprocess.check_output(["git", "-C", str(source), *arguments])


def identity(path):
    """Bind a regular input's bytes and executable mode for the before/after check."""
    if path.is_symlink() or not path.is_file():
        raise ValueError("contained producer requires regular input files")
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "mode": path.stat().st_mode & 0o777}


def run(args):
    """Copy frozen inputs, run the original gate, and independently check input preservation."""
    source = Path(__file__).resolve().parents[2]
    root = Path(args.root)
    if not root.is_absolute() or root.resolve() != root or source == root or source in root.parents:
        raise ValueError("contained producer root must be a separate physical absolute directory")
    root.mkdir(parents=True, exist_ok=True)
    work = root / "work" / "daw"
    if work.exists():
        raise ValueError("contained producer working copy must be absent")
    names = sorted({os.fsdecode(p) for p in git(source, "ls-files", "-z", "--cached", "--others",
                                               "--exclude-standard").split(b"\0") if p})
    if any(Path(name).is_absolute() or ".." in Path(name).parts or ".git" in Path(name).parts
           for name in names):
        raise ValueError("unsafe contained input name")
    before = {name: identity(source / name) for name in names}
    available = shutil.disk_usage(root).free
    needed = sum((source / name).stat().st_size for name in names)
    if available < needed * 3 + 512 * 1024 * 1024:
        raise ValueError("insufficient space for owned source, build and package inputs")
    subprocess.run(["git", "clone", "--quiet", "--no-checkout", "--shared", "--", str(source), str(work)],
                   check=True)
    subprocess.run(["git", "-C", str(work), "remote", "remove", "origin"], check=True)
    subprocess.run(["git", "-C", str(work), "read-tree", "HEAD"], check=True)
    for name in names:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, destination)
    if any(identity(work / name) != value for name, value in before.items()):
        raise ValueError("contained producer copy differs from frozen inputs")
    environment = dict(os.environ)
    toolchain = Path(args.compiler).resolve().parent
    for role in ("otool", "install_name_tool"):
        tool = toolchain / role
        if not tool.is_file():
            raise ValueError("selected native bundle tool is unavailable: " + role)
        environment[role.upper() + "_BIN"] = str(tool)
    environment["PATH"] = str(toolchain) + os.pathsep + environment.get("PATH", "")
    if environment.get("CODEWORKCTL_WORKSPACE_ROOT"):
        environment["CODEWORK_DATA_ROOT"] = environment["CODEWORKCTL_WORKSPACE_ROOT"]
    command = [args.make, "-C", str(work), "-j2", "BUILD_DIR=" + str(root / "output"),
               "HOST_CC=" + args.compiler, "MCP_PYTHON=" + sys.executable,
               "RELEASE_PREPARATION_ROOT=", "RELEASE_ROOT=" + args.release_root, args.target]
    if args.target == "release-bundle-audit":
        command.insert(-1, "DIST_DIR=" + args.release_root)
    result = subprocess.run(command, env=environment)
    unchanged = all(identity(work / name) == value and identity(source / name) == value
                    for name, value in before.items())
    (root / "input-preservation.json").write_text(json.dumps({
        "source_head": git(source, "rev-parse", "HEAD").decode().strip(),
        "inputs": len(before), "unchanged": unchanged, "target": args.target,
        "exit_code": result.returncode}, indent=2))
    if not unchanged:
        raise ValueError("contained gate changed a frozen or copied source input")
    return result.returncode


def main():
    """Accept the fixed DAW validation, package and unsigned authentication audit commands."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--target", choices=["run-headless-smoke", "release-package-self-test",
                                             "release-artifact-disposable", "release-bundle-audit"], required=True)
    parser.add_argument("--make", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--release-root", required=True)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
