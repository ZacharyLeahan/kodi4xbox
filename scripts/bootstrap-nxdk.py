#!/usr/bin/env python3
"""Prepare pinned upstream NXDK with the Kodi runtime compatibility patches."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = "119ab2f7b04a92a8ac4f4f35e61e0901224656a1"
PATCHES = [(".", "runtime.patch"), ("lib/libcxx", "libcxx.patch"),
           ("lib/pdclib", "pdclib.patch")]


def git(directory, *args):
    subprocess.run(["git", "-C", str(directory), *args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / ".local/nxdk")
    args = parser.parse_args()
    sdk = args.directory.resolve()
    patch_dir = ROOT / "tools/nxdk/patches"
    identity = {"upstream": REVISION, "patches": {
        name: hashlib.sha256((patch_dir / name).read_bytes()).hexdigest()
        for _, name in PATCHES}}
    marker = sdk / ".kodi-toolchain.json"
    if sdk.exists():
        if not marker.exists() or json.loads(marker.read_text()) != identity:
            parser.error(f"{sdk} already exists with a different or incomplete setup; "
                         "choose a fresh --directory (existing SDK is preserved)")
        head = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
        if head != REVISION:
            parser.error(f"{sdk} HEAD differs from the pinned revision")
        modules = subprocess.check_output(
            ["git", "-C", str(sdk), "submodule", "status", "--recursive"], text=True)
        if any(line[0] != " " for line in modules.splitlines()):
            parser.error(f"{sdk} submodules differ from the pinned revisions")
        for directory, name in PATCHES:
            git(sdk / directory, "apply", "--reverse", "--check", str(patch_dir / name))
        print(f"SDK already prepared: {sdk}")
        return
    sdk.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "clone", "https://github.com/XboxDev/nxdk.git", str(sdk)], check=True)
    git(sdk, "checkout", "--detach", REVISION)
    git(sdk, "submodule", "update", "--init", "--recursive", "--jobs", "8")
    for directory, name in PATCHES:
        git(sdk / directory, "apply", "--check", str(patch_dir / name))
        git(sdk / directory, "apply", str(patch_dir / name))
    marker.write_text(json.dumps(identity, indent=2) + "\n")
    print(f"SDK prepared: {sdk}")


if __name__ == "__main__":
    main()
