#!/usr/bin/env python3
"""Verify DAW's exact shared Vulkan source and display-backed rollout proof."""

import argparse
import hashlib
import os
import re
import struct
import subprocess
from pathlib import Path

EXPECTED_SHARED_COMMIT = "09ff89a0a32d80981010b15a7d45a2cdf8e5fd9f"


def command_output(command: list[str], cwd: Path) -> str:
    result = subprocess.run(command, cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout.strip()


def read_version(path: Path) -> tuple[int, int, int]:
    value = path.read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    if not match:
        raise SystemExit(f"invalid version: {path}: {value!r}")
    return tuple(int(part) for part in match.groups())


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_shared_source(root: Path, canonical: Path) -> None:
    # Inspect immutable Git objects, never require the unrelated live shared HEAD
    # or dirty worktree to match this application's deliberately pinned adoption.
    commit = command_output(["git", "rev-parse", EXPECTED_SHARED_COMMIT + "^{commit}"], canonical)
    tracked = command_output(["git", "ls-tree", "-r", "--name-only", commit,
                              "--", "vk_runtime", "vk_renderer"], canonical).splitlines()
    if not tracked:
        raise SystemExit("accepted shared snapshot has no Vulkan sources")
    mismatches = []
    for relative in tracked:
        expected = subprocess.run(["git", "show", commit + ":" + relative],
                                  cwd=canonical, check=True, stdout=subprocess.PIPE).stdout
        adopted_path = root / relative
        if not adopted_path.is_file():
            mismatches.append("missing " + relative)
        elif digest(adopted_path) != hashlib.sha256(expected).hexdigest():
            mismatches.append("digest " + relative)
    if mismatches:
        raise SystemExit("adopted Vulkan source differs from accepted snapshot: " +
                         ", ".join(mismatches[:12]))


def bmp_evidence(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise SystemExit(f"invalid BMP capture: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    if width <= 0 or height == 0 or bits_per_pixel not in (24, 32):
        raise SystemExit(
            f"invalid BMP geometry: {path}: {width}x{height} bpp={bits_per_pixel}")
    stride = ((width * bits_per_pixel + 31) // 32) * 4
    bytes_per_pixel = bits_per_pixel // 8
    colors = set()
    for row in range(0, abs(height), max(1, abs(height) // 64)):
        row_start = pixel_offset + row * stride
        for column in range(0, width, max(1, width // 64)):
            start = row_start + column * bytes_per_pixel
            colors.add(data[start:start + bytes_per_pixel])
    if len(colors) < 3:
        raise SystemExit(f"capture lacks nontrivial rendered content: {path}")
    return width, abs(height), len(colors)


def validation_environment() -> dict[str, str]:
    env = os.environ.copy()
    candidates = [
        Path("/opt/homebrew/opt/vulkan-validationlayers"),
        Path("/usr/local/opt/vulkan-validationlayers"),
    ]
    prefix = next((candidate for candidate in candidates
                   if (candidate / "lib/libVkLayer_khronos_validation.dylib").is_file()),
                  None)
    if prefix is None:
        raise SystemExit("Khronos validation layer library is unavailable")
    library_dir = str(prefix / "lib")
    layer_dir = str(prefix / "share/vulkan/explicit_layer.d")
    prior_library_path = env.get("DYLD_LIBRARY_PATH", "")
    env["DYLD_LIBRARY_PATH"] = (library_dir + ":" + prior_library_path
                                if prior_library_path else library_dir)
    env["VK_LAYER_PATH"] = layer_dir
    return env


def run_app(app: Path, initial: Path, resized: Path, log: Path,
            minimum_scale: float) -> None:
    for capture in (initial, resized):
        capture.unlink(missing_ok=True)
    env = validation_environment()
    env["DAW_VULKAN_ROLLOUT_INITIAL_CAPTURE"] = str(initial.resolve())
    env["DAW_VULKAN_ROLLOUT_RESIZED_CAPTURE"] = str(resized.resolve())
    env["DAW_VULKAN_ROLLOUT_MIN_SCALE"] = str(minimum_scale)
    result = subprocess.run([str(app.resolve()), "--vulkan-rollout-self-test"],
                            env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout or ""
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(output, encoding="utf-8")
    print(output, end="")
    if result.returncode != 0:
        raise SystemExit(f"DAW Vulkan self-test exited {result.returncode}")
    if "[vk_runtime validation]" in output:
        raise SystemExit("Vulkan validation emitted warning/error diagnostics")
    receipts = re.findall(
        r"DAW_VULKAN_RUNTIME schema=1 stage=(startup|resized|restart) "
        r"runtime=0\.6\.0 .* validation_requested=1 validation_enabled=1 "
        r"warnings=0 errors=0", output)
    if receipts != ["startup", "resized", "restart"]:
        raise SystemExit(f"incomplete runtime receipts: {receipts}")
    if not re.search(r"DAW_VULKAN_ROLLOUT schema=1 status=pass\b", output):
        raise SystemExit("missing successful rollout receipt")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shared-root", type=Path, required=True)
    parser.add_argument("--canonical-shared-root", type=Path, required=True)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--initial-capture", type=Path)
    parser.add_argument("--resized-capture", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--minimum-scale", type=float, default=1.0)
    args = parser.parse_args()

    root = args.shared_root.resolve()
    canonical = args.canonical_shared_root.resolve()
    verify_shared_source(root, canonical)
    runtime = read_version(root / "vk_runtime/VERSION")
    renderer = read_version(root / "vk_renderer/VERSION")
    if runtime != (0, 6, 0):
        raise SystemExit(f"vk_runtime 0.6.0 required, found {runtime}")
    if renderer != (1, 7, 0):
        raise SystemExit(f"vk_renderer 1.7.0 required, found {renderer}")

    if args.app:
        if not args.initial_capture or not args.resized_capture or not args.log:
            raise SystemExit("app execution requires both captures and a log")
        run_app(args.app, args.initial_capture, args.resized_capture, args.log,
                args.minimum_scale)

    if args.initial_capture or args.resized_capture:
        if not args.initial_capture or not args.resized_capture:
            raise SystemExit("both capture paths are required")
        initial = bmp_evidence(args.initial_capture)
        resized = bmp_evidence(args.resized_capture)
        if initial[:2] == resized[:2]:
            raise SystemExit(f"capture dimensions did not change: {initial[:2]}")
        print("DAW Vulkan captures: "
              f"initial={initial[0]}x{initial[1]} colors={initial[2]} "
              f"resized={resized[0]}x{resized[1]} colors={resized[2]}")

    print("DAW Vulkan rollout contract: "
          f"shared_root={root} canonical_commit={EXPECTED_SHARED_COMMIT} "
          f"vk_runtime={'.'.join(map(str, runtime))} "
          f"vk_renderer={'.'.join(map(str, renderer))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
