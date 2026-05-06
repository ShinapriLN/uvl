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


def fuse_source_path():
    return Path(__file__).parent / "native" / "fuse.c"


def ensure_fuse_binary():
    c_src_path = fuse_source_path()
    if not c_src_path.exists():
        error(f"FUSE source not found at {c_src_path}")
        sys.exit(1)

    c_hash = hash_file(c_src_path)[:12]
    fuse_bin = BIN_ROOT / f"uvl_fuse_{c_hash}"
    if fuse_bin.exists():
        step(f"Using cached C FUSE engine: {fuse_bin}")
        return fuse_bin

    step("Compiling C FUSE engine")
    BIN_ROOT.mkdir(parents=True, exist_ok=True)
    try:
        pkg_out = subprocess.check_output(["pkg-config", "fuse3", "--cflags", "--libs"], text=True).strip()
        compile_cmd = ["gcc", "-Wall", "-Wextra", "-O2", str(c_src_path), "-o", str(fuse_bin)]
        compile_cmd.extend(pkg_out.split())
        subprocess.run(compile_cmd, check=True)
    except Exception as exc:
        error(f"Failed to compile FUSE engine: {exc}")
        error("Install gcc, pkg-config, and libfuse3 development headers.")
        sys.exit(1)
    ok(f"Compiled C FUSE engine: {fuse_bin}")
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
