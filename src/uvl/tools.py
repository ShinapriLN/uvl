import shutil
import subprocess
import sys
from pathlib import Path

from .config import get_registration
from .fuse import create_virtual_fs, is_mountpoint
from .log import ok, step
from .manifest import restore_from_manifest


def execute_tool(tool, args):
    registration = get_registration(tool)
    if not registration:
        print(f"Error: '{tool}' is not mounted with uvl.", file=sys.stderr)
        print(f"Register it first with `uvl --fuse {tool} --mnt <dependency-dir>`.", file=sys.stderr)
        sys.exit(1)

    exe_path = shutil.which(tool)
    if not exe_path:
        print(f"Error: '{tool}' not found in PATH.", file=sys.stderr)
        sys.exit(1)

    step(f"Resolved '{tool}' binary: {exe_path}")
    target = Path.cwd() / registration["entry"]
    manifest = Path.cwd() / ".uvl"
    was_mounted = target.exists() and is_mountpoint(target)
    if was_mounted:
        step(f"Restoring mounted entry '{registration['entry']}' before running {tool}")
        if not manifest.exists():
            print(f"Error: '{registration['entry']}' is mounted but .uvl manifest is missing.", file=sys.stderr)
            sys.exit(1)
        restore_from_manifest(manifest, target)

    step(f"Running command: {tool} {' '.join(args)}".rstrip())
    try:
        result = subprocess.run([exe_path, *args])
    except KeyboardInterrupt:
        sys.exit(130)

    if result.returncode != 0:
        sys.exit(result.returncode)
    ok(f"{tool} completed successfully")

    if not target.exists():
        step(f"Registered entry '{registration['entry']}' does not exist; nothing to mount")
        return
    if is_mountpoint(target):
        step(f"Registered entry '{registration['entry']}' is already mounted")
        return
    create_virtual_fs(tool, target, remount=was_mounted)
