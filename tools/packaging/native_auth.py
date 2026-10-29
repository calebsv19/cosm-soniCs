#!/usr/bin/env python3
"""Write exact soniCs authentication sidecars for the existing Registry owner."""
import argparse
import hashlib
import json
from pathlib import Path
import uuid


def accepted(root):
    """Require the retained Accepted Apple submission identity before final packaging."""
    record = json.loads((root / "notary_submit.json").read_text())
    if record.get("status") != "Accepted" or str(uuid.UUID(record["id"])) != record["id"]:
        raise ValueError("retained Accepted submission identity required")
    return record


def manifest(root, version):
    """Bind final archive bytes, public identity and retained notarization in sidecars."""
    record = accepted(root)
    archive = root / f"soniCs-{version}-macOS-arm64-stable.zip"
    with archive.open("rb") as stream:
        sha = hashlib.file_digest(stream, "sha256").hexdigest()
    Path(str(archive) + ".sha256").write_text(sha + "  " + archive.name + "\n")
    fields = dict(product="soniCs", program="daw", version=version, bundle_id="com.cosm.sonics",
                  platform="macOS", arch="arm64", format="zip", channel="stable",
                  artifact=archive.name, zip=archive.name, sha256=sha, signed=1,
                  release_signed=1, notarized=1, notary_submission_id=record["id"])
    archive.with_suffix(".manifest.txt").write_text("".join(f"{key}={value}\n" for key, value in fields.items()))


def main():
    """Dispatch the two non-credential sidecar operations under the bound release root."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["accepted", "manifest"])
    parser.add_argument("--root", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    root = Path(args.root).resolve()
    if args.phase == "accepted":
        accepted(root)
    else:
        manifest(root, args.version)


if __name__ == "__main__":
    main()
