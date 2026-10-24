#!/usr/bin/env python3
"""Deploy verified Kodi test bundles and collect evidence through dashboard FTP."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from ftplib import FTP, error_perm, error_temp
import hashlib
import io
import json
import os
from pathlib import Path
import re
from queue import Queue
import socket
import threading

ROOT = Path(__file__).resolve().parents[1]
LOGS = ["exit.log", "diagnostics/startup.log", "diagnostics/shutdown.log", "diagnostics/profile/kodi.log",
        "diagnostics/profile/kodi.old.log", "diagnostics/profile/userdata/guisettings.xml"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def remote_digest(ftp, name):
    result = hashlib.sha256()
    ftp.retrbinary("RETR " + name, result.update, 65536)
    return result.hexdigest()


def enter(ftp, path, create=False):
    ftp.cwd("/")
    for component in path.strip("/").split("/"):
        try:
            ftp.cwd(component)
        except error_perm:
            if not create:
                raise
            try:
                ftp.mkd(component)
            except error_perm:
                pass  # A parallel worker may have created the same directory.
            ftp.cwd(component)


def collect(ftp, directory, evidence, prefix=""):
    targets = [(directory + "/" + name, name.replace("/", "-")) for name in LOGS]
    # Ordinary builds and diagnostic builds using the normal profile log here,
    # outside the isolated install directory. Preserve these before any launch.
    targets += [("/E/UDATA/Kodi/" + name, "normal-profile-" + name)
                for name in ("kodi.log", "kodi.old.log")]
    for remote, local in targets:
        try:
            parent, _, basename = remote.rpartition("/")
            enter(ftp, parent)
            output = evidence / (prefix + local)
            with output.open("wb") as stream:
                ftp.retrbinary("RETR " + basename, stream.write)
            print("Collected", remote, flush=True)
        except error_perm:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["status", "deploy", "launch", "logs"])
    parser.add_argument("--host", default="192.168.1.254")
    parser.add_argument("--directory", default="/E/Kodi-NXDK-test")
    parser.add_argument("--package", type=Path, default=ROOT / "build/diagnostic/xiso")
    parser.add_argument("--executable-only", action="store_true",
                        help="Reuse a verified asset deployment when only default.xbe changed")
    args = parser.parse_args()
    if not re.fullmatch(r"/E/Kodi-NXDK-[A-Za-z0-9-]+", args.directory):
        parser.error("Use a dedicated /E/Kodi-NXDK-... test directory")
    evidence = ROOT / "build/hardware" / datetime.now(timezone.utc).strftime("kodi-%Y%m%dT%H%M%S%fZ")
    evidence.mkdir(parents=True)
    with FTP(args.host, timeout=10) as ftp:
        ftp.login(os.getenv("XBOX_FTP_USER", "xbox"), os.getenv("XBOX_FTP_PASSWORD", "xbox"))
        if args.action == "status":
            print(ftp.getwelcome())
            return
        if args.action == "logs":
            collect(ftp, args.directory, evidence)
            print("Evidence:", evidence)
            return
        if args.action == "deploy":
            package = args.package.resolve()
            if not (package / "default.xbe").is_file():
                parser.error("Package must contain default.xbe and its complete assets")
            manifest = {p.relative_to(package).as_posix(): digest(p)
                        for p in sorted(package.rglob("*")) if p.is_file()}
            if len({name.casefold() for name in manifest}) != len(manifest):
                parser.error("Package contains case-insensitive path collisions")
            for name in manifest:
                if any(len(part) > 42 or re.search(r'[\x00-\x1f<>:"\\|?*]', part)
                       for part in name.split("/")):
                    parser.error("Invalid FATX package path: " + name)
            collect(ftp, args.directory, evidence, "prior-")
            enter(ftp, args.directory, create=True)
            transfers = manifest
            baseline = None
            if args.executable_only:
                records = [json.loads(p.read_text()) for p in (ROOT / "build/hardware").glob("*/deploy.json")]
                records = [r for r in records if r.get("host") == args.host and r.get("directory") == args.directory]
                if not records:
                    parser.error("Executable-only update requires a completed asset deployment")
                baseline = max(records, key=lambda r: r["utc"])
                if ({k: v for k, v in manifest.items() if k != "default.xbe"} !=
                        {k: v for k, v in baseline["files"].items() if k != "default.xbe"}):
                    parser.error("Assets changed; use a full deployment")
                if remote_digest(ftp, "codex-manifest.json") != baseline["manifest_sha256"]:
                    raise RuntimeError("Installed asset manifest changed; use a full deployment")
                if remote_digest(ftp, "default.xbe") != baseline["files"]["default.xbe"]:
                    raise RuntimeError("Installed executable changed; use a full deployment")
                transfers = {"default.xbe": manifest["default.xbe"]}
            try:
                with (evidence / "prior-default.xbe").open("wb") as out:
                    ftp.retrbinary("RETR default.xbe", out.write)
            except error_perm:
                pass
            ftp.close()
            worker = threading.local()
            connections = []
            available = Queue()
            for _ in range(4):
                connection = FTP(args.host, timeout=10)
                try:
                    connection.login(os.getenv("XBOX_FTP_USER", "xbox"), os.getenv("XBOX_FTP_PASSWORD", "xbox"))
                except error_temp as error:
                    connection.close()
                    if str(error).startswith("421"):
                        break
                    raise
                connections.append(connection)
                available.put(connection)
            if not connections:
                raise RuntimeError("Dashboard has no available FTP connections")
            print(f"Using {len(connections)} FTP connections", flush=True)

            def transfer(name, expected):
                if not hasattr(worker, "ftp"):
                    worker.ftp = available.get()
                    worker.parent = None
                connection = worker.ftp
                local = package / name
                subdirectory = local.relative_to(package).parent.as_posix()
                target = args.directory + ("/" + subdirectory if subdirectory != "." else "")
                if worker.parent != target:
                    enter(connection, target, create=True)
                    worker.parent = target
                # Verify partial installs too, so an interrupted upload is resumable.
                unchanged = False
                try:
                    unchanged = remote_digest(connection, local.name) == expected
                except error_perm:
                    pass
                if not unchanged:
                    with local.open("rb") as source:
                        connection.storbinary("STOR " + local.name, source, 65536)
                    if remote_digest(connection, local.name) != expected:
                        raise RuntimeError("Readback mismatch: " + name)
                return int(not unchanged)

            uploaded = 0
            try:
                with ThreadPoolExecutor(max_workers=len(connections)) as pool:
                    pending = [pool.submit(transfer, name, expected) for name, expected in transfers.items()]
                    try:
                        for index, result in enumerate(as_completed(pending), 1):
                            uploaded += result.result()
                            if index % 100 == 0:
                                print(f"Verified {index}/{len(transfers)} files", flush=True)
                    except Exception:
                        for result in pending:
                            result.cancel()
                        raise
            finally:
                for connection in connections:
                    connection.close()
            ftp.connect(args.host, 21)
            ftp.login(os.getenv("XBOX_FTP_USER", "xbox"), os.getenv("XBOX_FTP_PASSWORD", "xbox"))
            enter(ftp, args.directory)
            payload = json.dumps(manifest, sort_keys=True).encode()
            ftp.storbinary("STOR codex-manifest.json", io.BytesIO(payload))
            manifest_hash = hashlib.sha256(payload).hexdigest()
            if remote_digest(ftp, "codex-manifest.json") != manifest_hash:
                raise RuntimeError("Manifest readback mismatch")
            record = {"host": args.host, "directory": args.directory,
                      "package": str(package), "files": manifest, "manifest_sha256": manifest_hash,
                      "uploaded_files": uploaded, "utc": datetime.now(timezone.utc).isoformat()}
            record["verification"] = "executable-only" if baseline else "full-readback"
            if baseline:
                record["asset_baseline_utc"] = baseline.get("asset_baseline_utc", baseline["utc"])
            (evidence / "deploy.json").write_text(json.dumps(record, indent=2) + "\n")
            print(f"Verified {len(transfers)} files; uploaded {uploaded}; XBE {manifest['default.xbe']}")
            if baseline:
                print(f"Reused {len(manifest) - 1} verified assets from {record['asset_baseline_utc']}")
        else:
            records = [json.loads(p.read_text()) for p in (ROOT / "build/hardware").glob("*/deploy.json")]
            records = [r for r in records if r.get("host") == args.host and r.get("directory") == args.directory]
            if not records:
                parser.error("No completed verified deployment for this target")
            record = max(records, key=lambda r: r["utc"])
            enter(ftp, args.directory)
            if remote_digest(ftp, "codex-manifest.json") != record["manifest_sha256"]:
                raise RuntimeError("Installed manifest changed")
            if remote_digest(ftp, "default.xbe") != record["files"]["default.xbe"]:
                raise RuntimeError("Installed executable changed")
            collect(ftp, args.directory, evidence, "prior-")
            (evidence / "launch.json").write_text(json.dumps(record, indent=2) + "\n")
            path = "E:\\" + args.directory.split("/", 2)[2] + "\\default.xbe"
            print("Launching", path, "XBE", record["files"]["default.xbe"], flush=True)
            try:
                print(ftp.sendcmd("SITE RunXBE " + path))
            except (EOFError, socket.timeout, ConnectionError) as error:
                print(type(error).__name__, "during launch; inspect KVM and logs")
        print("Evidence:", evidence)


if __name__ == "__main__":
    main()
