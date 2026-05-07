import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from .log import error, ok, step, warn
from .manifest import build_mapping, restore_from_manifest
from .paths import BIN_ROOT
from .store import get_tool_store, hash_file
from .ui import display_path


def packaged_native_binary():
    return Path(__file__).parent / "native" / "uvl"


def ensure_fuse_binary():
    packaged_bin = packaged_native_binary()
    repo_bin = Path(__file__).resolve().parents[2] / "build" / "uvl"
    source_bin = packaged_bin if packaged_bin.exists() else repo_bin
    if not source_bin.exists():
        error(f"Native binary not found at {packaged_bin} or {repo_bin}")
        error("Build the native binary first with `make build`, or install a prebuilt wheel.")
        sys.exit(1)

    c_hash = hash_file(source_bin)[:12]
    fuse_bin = BIN_ROOT / f"uvl_{c_hash}"
    if fuse_bin.exists():
        step(f"Using cached native engine: {fuse_bin}")
        return fuse_bin

    step("Preparing native engine from bundled binary")
    BIN_ROOT.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_bin, fuse_bin)
    try:
        os.chmod(fuse_bin, 0o755)
    except OSError:
        pass
    ok(f"Prepared native engine: {fuse_bin}")
    return fuse_bin


def is_mountpoint(path):
    return subprocess.run(
        ["mountpoint", "-q", str(path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def unmount_target(target_dir, quiet=False):
    target = Path(target_dir).expanduser().resolve()
    result = subprocess.run(
        ["fusermount3", "-u", str(target)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode != 0:
        result = subprocess.run(
            ["fusermount3", "-uz", str(target)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    if quiet:
        return result.returncode == 0
    if result.returncode == 0:
        ok(f"Unmounted {display_path(target)}")
        step("You can now delete or modify anything inside")
    else:
        error(f"Could not unmount {target}")
    return result.returncode == 0


def wait_for_mount(process, target, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if is_mountpoint(target):
            return True
        if process.poll() is not None:
            time.sleep(0.05)
            return is_mountpoint(target)
        time.sleep(0.05)
    return is_mountpoint(target)


def create_virtual_fs(tool, target_dir, remount=False):
    target = Path(target_dir).expanduser().resolve()
    if not target.exists() or not target.is_dir():
        return False
    if is_mountpoint(target):
        return False

    shown_target = display_path(target)
    print()
    if remount:
        step(f"Re-virtualizing '{shown_target}' after command completed")
    else:
        step(f"Taking control of '{shown_target}'")

    fuse_bin = ensure_fuse_binary()
    step(f"Scanning '{shown_target}' and writing binary .uvl manifest")
    manifest_path, file_count, stored_bytes, duplicate_bytes = build_mapping(tool, target)
    step(f"Manifest ready: {manifest_path}")
    step(f"Preparing empty mount point: {shown_target}")
    shutil.rmtree(target)
    target.mkdir(parents=True, exist_ok=True)

    step(f"Mounting virtual filesystem with {file_count} files")
    process = subprocess.Popen(
        [str(fuse_bin), str(manifest_path), str(target)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    if not wait_for_mount(process, target):
        if is_mountpoint(target):
            ok(f"Successfully remounted virtual '{shown_target}'" if remount else f"Successfully mounted virtual '{shown_target}'")
            return True
        warn("Mount failed; restoring normal directory from .uvl manifest")
        restore_from_manifest(manifest_path, target)
        error(f"Failed to mount virtual '{shown_target}'. Restored the normal directory.")
        return False

    saved_mb = duplicate_bytes / (1024 * 1024)
    virtualized_mb = (stored_bytes + duplicate_bytes) / (1024 * 1024)
    ok(f"Successfully remounted virtual '{shown_target}'" if remount else f"Successfully mounted virtual '{shown_target}'")
    step(f"Deduplication saved {saved_mb:.2f} MB of physical disk space")
    step(f"Virtualized {virtualized_mb:.2f} MB into {get_tool_store(tool)}")
    return True
