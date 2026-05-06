import os
import sys
import shutil
import hashlib
import subprocess
from pathlib import Path

# Centralized CAS store for uvl
CAS_ROOT = Path.home() / ".uvl" / "cas"

def get_cas_path():
    CAS_ROOT.mkdir(parents=True, exist_ok=True)
    return CAS_ROOT

def hash_file(filepath):
    """Calculate SHA-256 hash of a file."""
    h = hashlib.sha256()
    with open(filepath, 'rb') as f:
        while chunk := f.read(8192):
            h.update(chunk)
    return h.hexdigest()

def deduplicate_and_link(target_dir):
    """
    Scans target_dir, moves new files to CAS, and replaces them with symlinks.
    This is uvl taking ACTIVE control over the file system.
    """
    target = Path(target_dir)
    if not target.exists() or not target.is_dir():
        return

    cas_dir = get_cas_path()
    linked_count = 0
    saved_bytes = 0

    print(f"\n[uvl] 🪄  Taking control: Optimizing {target_dir} with custom symlinks...")

    for root, _, files in os.walk(target):
        for file in files:
            filepath = Path(root) / file
            
            # Skip if it's already a symlink
            if filepath.is_symlink():
                continue 

            try:
                # Get file size for stats
                size = filepath.stat().st_size
                
                # Hash the file to find its unique identity
                file_hash = hash_file(filepath)
                cas_filepath = cas_dir / file_hash

                if cas_filepath.exists():
                    # It exists in the global store! Delete local and symlink.
                    filepath.unlink()
                    filepath.symlink_to(cas_filepath)
                    saved_bytes += size
                else:
                    # New unique file! Move to global store and symlink.
                    shutil.move(str(filepath), str(cas_filepath))
                    filepath.symlink_to(cas_filepath)
                    
                linked_count += 1
            except Exception as e:
                # Ignore files we can't read/move (e.g., weird permissions)
                pass

    if linked_count > 0:
        saved_mb = saved_bytes / (1024 * 1024)
        print(f"[uvl] ✅ Successfully enforced {linked_count} symlinks.")
        print(f"[uvl] 💾 Deduplication saved {saved_mb:.2f} MB of disk space.")

# Target directories to take control of based on the tool
TOOL_TARGETS = {
    "uv": [".venv"],
    "pip": [".venv", "venv", "env"],
    "poetry": [".venv"],
    "npm": ["node_modules"],
    "yarn": ["node_modules"],
    "pnpm": ["node_modules"],
    "bun": ["node_modules"],
}

def execute_and_link(tool, args):
    exe_path = shutil.which(tool)
    if not exe_path:
        print(f"Error: '{tool}' not found in PATH.", file=sys.stderr)
        sys.exit(1)

    # 1. Execute the underlying package manager normally
    # We use subprocess.run so we wait for it to finish and regain control.
    try:
        result = subprocess.run([exe_path] + args)
        if result.returncode != 0:
            sys.exit(result.returncode)
    except KeyboardInterrupt:
        sys.exit(130)

    # 2. Post-process the output directory!
    # This is where uvl does the real work, bypassing native limitations.
    if tool in TOOL_TARGETS:
        for target in TOOL_TARGETS[tool]:
            if Path(target).exists():
                deduplicate_and_link(target)

def print_help():
    print("uvl: Universal Linker - Custom Symlink Engine")
    print("\nUsage:")
    print("  uvl <tool> [args...]")
    print("\nHow it works:")
    print("  1. uvl lets the package manager run normally.")
    print("  2. It intercepts the output (like node_modules or .venv).")
    print("  3. It hashes every file and moves it to a global CAS (~/.uvl/cas).")
    print("  4. It replaces the local files with absolute symlinks.")
    print("  This guarantees symlink usage and extreme space savings,")
    print("  regardless of the underlying tool's native limitations.")

def main():
    if len(sys.argv) < 2 or sys.argv[1] in ["--help", "-h"]:
        print_help()
        sys.exit(0)

    tool = sys.argv[1]
    args = sys.argv[2:]

    execute_and_link(tool, args)

if __name__ == "__main__":
    main()
