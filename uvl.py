import os
import sys
import shutil
import hashlib
import subprocess
from pathlib import Path

# Centralized stores
STORE_ROOT = Path.home() / ".uvl" / "store"
CAS_ROOT = Path.home() / ".uvl" / "cas"

def get_store_path(tool_name):
    path = STORE_ROOT / tool_name
    path.mkdir(parents=True, exist_ok=True)
    return str(path)

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
    This guarantees symlinks and massive space savings.
    """
    target = Path(target_dir)
    if not target.exists() or not target.is_dir():
        return

    cas_dir = get_cas_path()
    linked_count = 0
    saved_bytes = 0

    print(f"\n[uvl] 🪄  Taking control: Optimizing '{target_dir}' with custom symlinks...")

    for root, _, files in os.walk(target):
        for file in files:
            filepath = Path(root) / file
            
            if filepath.is_symlink():
                continue 

            try:
                size = filepath.stat().st_size
                file_hash = hash_file(filepath)
                cas_filepath = cas_dir / file_hash

                if cas_filepath.exists():
                    filepath.unlink()
                    filepath.symlink_to(cas_filepath)
                    saved_bytes += size
                else:
                    shutil.move(str(filepath), str(cas_filepath))
                    filepath.symlink_to(cas_filepath)
                    
                linked_count += 1
            except Exception as e:
                pass

    if linked_count > 0:
        saved_mb = saved_bytes / (1024 * 1024)
        print(f"[uvl] ✅ Successfully enforced {linked_count} symlinks.")
        print(f"[uvl] 💾 Deduplication saved {saved_mb:.2f} MB of disk space.")

# 1. Global Cache Mappings (Keep ~/.uvl/store clean)
ENV_MAPPINGS = {
    # --- Python ---
    "uv": {"UV_CACHE_DIR": "{store}", "UV_LINK_MODE": "symlink"},
    "pip": {"PIP_CACHE_DIR": "{store}"},
    "poetry": {"POETRY_CACHE_DIR": "{store}"},
    "pdm": {"PDM_CACHE_DIR": "{store}"},
    "pipenv": {"PIPENV_CACHE_DIR": "{store}"},
    "conda": {"CONDA_PKGS_DIRS": "{store}"},
    "mamba": {"CONDA_PKGS_DIRS": "{store}"},
    "micromamba": {"CONDA_PKGS_DIRS": "{store}"},
    "pixi": {"PIXI_CACHE_DIR": "{store}"},
    # --- JS/TS ---
    "npm": {"npm_config_cache": "{store}"},
    "yarn": {"YARN_CACHE_FOLDER": "{store}"},
    "bun": {"BUN_INSTALL_CACHE_DIR": "{store}"},
    "deno": {"DENO_DIR": "{store}"},
    # --- Rust ---
    "cargo": {"CARGO_HOME": "{store}"},
    # --- Go ---
    "go": {"GOMODCACHE": "{store}"},
    # --- JVM ---
    "mvn": {"MAVEN_OPTS": "-Dmaven.repo.local={store}"},
    "gradle": {"GRADLE_USER_HOME": "{store}"},
    "sbt": {"COURSIER_CACHE": "{store}"},
    "coursier": {"COURSIER_CACHE": "{store}"},
    "lein": {"LEIN_HOME": "{store}"},
    # --- .NET ---
    "dotnet": {"NUGET_PACKAGES": "{store}"},
    "nuget": {"NUGET_PACKAGES": "{store}"},
    # --- PHP ---
    "composer": {"COMPOSER_CACHE_DIR": "{store}"},
    # --- Ruby ---
    "gem": {"GEM_HOME": "{store}", "GEM_PATH": "{store}"},
    "bundle": {"BUNDLE_USER_CACHE": "{store}"},
    # --- C/C++ ---
    "vcpkg": {"VCPKG_DEFAULT_BINARY_CACHE": "{store}"},
    "conan": {"CONAN_USER_HOME": "{store}"},
    "spack": {"SPACK_USER_CACHE_PATH": "{store}"},
    # --- Swift / Dart ---
    "swift": {"SWIFTPM_CACHE_PATH": "{store}"},
    "pub": {"PUB_CACHE": "{store}"},
    "dart": {"PUB_CACHE": "{store}"},
    # --- Others ---
    "mix": {"MIX_HOME": "{store}/mix", "HEX_HOME": "{store}/hex"},
    "rebar3": {"REBAR_CACHE_DIR": "{store}"},
    "cabal": {"CABAL_DIR": "{store}"},
    "stack": {"STACK_ROOT": "{store}"},
    "opam": {"OPAMROOT": "{store}"},
    "julia": {"JULIA_DEPOT_PATH": "{store}"},
    "R": {"RENV_PATHS_CACHE": "{store}/renv", "R_USER_CACHE_DIR": "{store}"},
    "Rscript": {"RENV_PATHS_CACHE": "{store}/renv", "R_USER_CACHE_DIR": "{store}"},
    "luarocks": {"LUAROCKS_SYSCONFDIR": "{store}"},
    "zig": {"ZIG_GLOBAL_CACHE_DIR": "{store}"},
    "nimble": {"NIMBLE_DIR": "{store}"},
    "shards": {"SHARDS_CACHE_PATH": "{store}"},
}

# 2. Local Project Directories to enforce Symlinking (The Custom Engine)
TOOL_TARGETS = {
    # Python
    "uv": [".venv"],
    "pip": [".venv", "venv", "env"],
    "poetry": [".venv"],
    "pdm": ["__pypackages__", ".venv"],
    "pipenv": [".venv"],
    "pixi": [".pixi"],

    # JS/TS
    "npm": ["node_modules"],
    "yarn": ["node_modules"],
    "pnpm": ["node_modules"],
    "bun": ["node_modules"],
    "deno": ["node_modules", "vendor"],

    # PHP
    "composer": ["vendor"],

    # Ruby
    "bundle": ["vendor/bundle", ".bundle"],

    # C/C++
    "vcpkg": ["vcpkg_installed"],
    "conan": ["build"],

    # Rust
    "cargo": ["target"],

    # Go
    "go": ["vendor"],

    # Elixir
    "mix": ["deps", "_build"],

    # Swift / Dart
    "swift": [".build"],
    "pub": [".dart_tool"],
    "dart": [".dart_tool"],
    
    # Zig
    "zig": ["zig-cache", "zig-out"],
}

def execute_and_link(tool, args):
    exe_path = shutil.which(tool)
    if not exe_path:
        print(f"Error: '{tool}' not found in PATH.", file=sys.stderr)
        sys.exit(1)

    env = os.environ.copy()
    store = get_store_path(tool)

    # Inject global cache environment variables
    if tool in ENV_MAPPINGS:
        for key, value_template in ENV_MAPPINGS[tool].items():
            val = value_template.format(store=store)
            if key == "MAVEN_OPTS" and "MAVEN_OPTS" in env:
                env[key] = f"{env[key]} {val}"
            else:
                env[key] = val

    # Inject pnpm specific store-dir flag
    new_args = [exe_path]
    if tool == "pnpm":
        if args and args[0] in ["install", "add", "update", "i"]:
            new_args.extend(["--store-dir", store])
    new_args.extend(args)

    # 1. Execute native package manager
    try:
        result = subprocess.run(new_args, env=env)
        if result.returncode != 0:
            sys.exit(result.returncode)
    except KeyboardInterrupt:
        sys.exit(130)

    # 2. Post-process local project folders to enforce symlinks
    if tool in TOOL_TARGETS:
        for target in TOOL_TARGETS[tool]:
            if Path(target).exists():
                deduplicate_and_link(target)

def print_help():
    print("uvl: Universal Linker - Custom Symlink Engine")
    print("\nUsage:")
    print("  uvl <tool> [args...]")
    print("  uvl --has <tool>        Check if a tool is supported and installed")
    print("\nHow it works:")
    print("  1. Sets global cache to ~/.uvl/store/<tool> to keep home dir clean.")
    print("  2. Lets the package manager run normally.")
    print("  3. Intercepts local project folders (like node_modules, .venv, target).")
    print("  4. Hashes every file and moves it to a global CAS (~/.uvl/cas).")
    print("  5. Replaces the local files with absolute symlinks.")
    print("  This guarantees extreme space savings for 44+ ecosystems!")

def main():
    supported = list(ENV_MAPPINGS.keys()) + ["pnpm"]

    if len(sys.argv) < 2 or sys.argv[1] in ["--help", "-h"]:
        print_help()
        sys.exit(0)

    if sys.argv[1] == "--has":
        if len(sys.argv) < 3:
            print("Usage: uvl --has <tool>")
            sys.exit(1)
        tool_to_check = sys.argv[2]
        if tool_to_check in supported and shutil.which(tool_to_check):
            print("true")
            sys.exit(0)
        else:
            print("false")
            sys.exit(1)

    tool = sys.argv[1]
    args = sys.argv[2:]

    if tool in supported:
        execute_and_link(tool, args)
    else:
        print(f"Error: Unsupported tool '{tool}'.")
        print("Use 'uvl --help' to see supported tools.")
        sys.exit(1)

if __name__ == "__main__":
    main()
