import os
import sys
import shutil
from pathlib import Path

# Centralized store for all package managers
STORE_ROOT = Path.home() / ".uvl" / "store"

def get_store_path(tool_name):
    path = STORE_ROOT / tool_name
    path.mkdir(parents=True, exist_ok=True)
    return str(path)

# A table-driven configuration mapping tools to their cache/store environment variables.
# '{store}' is dynamically replaced with the tool's isolated storage directory.
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
    "bun": {"BUN_INSTALL_CACHE": "{store}"},
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

    # --- Swift ---
    "swift": {"SWIFTPM_CACHE_PATH": "{store}"},

    # --- Dart ---
    "pub": {"PUB_CACHE": "{store}"},
    "dart": {"PUB_CACHE": "{store}"},

    # --- Elixir / Erlang ---
    "mix": {"MIX_HOME": "{store}/mix", "HEX_HOME": "{store}/hex"},
    "rebar3": {"REBAR_CACHE_DIR": "{store}"},

    # --- Haskell ---
    "cabal": {"CABAL_DIR": "{store}"},
    "stack": {"STACK_ROOT": "{store}"},

    # --- OCaml ---
    "opam": {"OPAMROOT": "{store}"},

    # --- Julia ---
    "julia": {"JULIA_DEPOT_PATH": "{store}"},

    # --- R ---
    "R": {"RENV_PATHS_CACHE": "{store}/renv", "R_USER_CACHE_DIR": "{store}"},
    "Rscript": {"RENV_PATHS_CACHE": "{store}/renv", "R_USER_CACHE_DIR": "{store}"},

    # --- Lua ---
    "luarocks": {"LUAROCKS_SYSCONFDIR": "{store}"},

    # --- Zig ---
    "zig": {"ZIG_GLOBAL_CACHE_DIR": "{store}"},

    # --- Nim ---
    "nimble": {"NIMBLE_DIR": "{store}"},

    # --- Crystal ---
    "shards": {"SHARDS_CACHE_PATH": "{store}"},
}

def wrap_pnpm(args):
    """pnpm requires explicit flags for its content-addressable store."""
    pnpm_path = shutil.which("pnpm")
    if not pnpm_path:
        print("Error: 'pnpm' not found.", file=sys.stderr)
        sys.exit(1)
        
    env = os.environ.copy()
    store_dir = get_store_path("pnpm")
    new_args = [pnpm_path]
    
    if args and args[0] in ["install", "add", "update", "i"]:
        new_args.extend(["--store-dir", store_dir])
    new_args.extend(args)
    
    os.execvpe(pnpm_path, new_args, env)

def generic_wrap(tool, args):
    """Applies environment variables and executes the underlying tool."""
    if tool == "pnpm":
        wrap_pnpm(args)
        return

    exe_path = shutil.which(tool)
    if not exe_path:
        print(f"Error: '{tool}' not found in PATH.", file=sys.stderr)
        sys.exit(1)

    env = os.environ.copy()
    store = get_store_path(tool)

    if tool in ENV_MAPPINGS:
        for key, value_template in ENV_MAPPINGS[tool].items():
            val = value_template.format(store=store)
            # Append if MAVEN_OPTS already exists to preserve user flags
            if key == "MAVEN_OPTS" and "MAVEN_OPTS" in env:
                env[key] = f"{env[key]} {val}"
            else:
                env[key] = val

    os.execvpe(exe_path, [exe_path] + args, env)

def print_help():
    print("uvl: Universal Linker - Solve disk space bloat across all ecosystems.")
    print("\nUsage:")
    print("  uvl <tool> [args...]    Execute a tool with optimized storage")
    print("  uvl --has <tool>        Check if a tool is supported and installed")
    
    tools = sorted(list(ENV_MAPPINGS.keys()) + ["pnpm"])
    print(f"\nSupported Tools ({len(tools)}):")
    
    # Print tools in a neat column format
    col_width = 15
    for i in range(0, len(tools), 4):
        chunk = tools[i:i+4]
        line = "  " + "".join(word.ljust(col_width) for word in chunk)
        print(line)

    print("\nGoal:")
    print("  Avoid duplicated dependencies and scattered caches.")
    print("  All tools cache data in: ~/.uvl/store/<tool>")

def main():
    supported = list(ENV_MAPPINGS.keys()) + ["pnpm"]

    if len(sys.argv) < 2 or sys.argv[1] in ["--help", "-h"]:
        print_help()
        sys.exit(0)

    # Check for --has flag
    if sys.argv[1] == "--has":
        if len(sys.argv) < 3:
            print("Usage: uvl --has <tool>")
            sys.exit(1)
        tool_to_check = sys.argv[2]
        is_supported = tool_to_check in supported
        is_installed = shutil.which(tool_to_check) is not None
        
        if is_supported and is_installed:
            print("true")
            sys.exit(0)
        else:
            print("false")
            sys.exit(1)

    tool = sys.argv[1]
    args = sys.argv[2:]

    if tool in supported:
        if os.environ.get("UVL_DEBUG") == "1":
            store = get_store_path(tool)
            print(f"DEBUG: tool={tool} store={store}")
            if tool in ENV_MAPPINGS:
                for key, value_template in ENV_MAPPINGS[tool].items():
                    print(f"DEBUG: env {key}={value_template.format(store=store)}")
            sys.exit(0)

        try:
            generic_wrap(tool, args)
        except OSError as e:
            print(f"Error executing {tool}: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        print(f"Error: Unsupported tool '{tool}'.")
        print("Use 'uvl --help' to see supported tools.")
        sys.exit(1)

if __name__ == "__main__":
    main()
