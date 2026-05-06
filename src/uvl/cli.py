import argparse
import shutil
import sys
from pathlib import Path

from .config import DEFAULT_ENTRIES, get_registration, load_config, register_tool_config
from .fuse import is_mountpoint, unmount_target
from .log import error, ok, step
from .manifest import read_metadata
from .store import get_tool_store
from .tools import execute_tool
from .ui import display_home_path
from .version import __version__


def register_tool(tool, entry):
    exe_path = shutil.which(tool)
    if not exe_path:
        error(f"Cannot mount '{tool}': binary not found in PATH")
        print(f"Install '{tool}' first, or pass the real executable name to `uvl --fuse`.", file=sys.stderr)
        sys.exit(1)

    step(f"Loading config from ~/.uvl/config.json")
    register_tool_config(tool, entry)
    step(f"Ensuring store for '{tool}'")
    store = get_tool_store(tool)

    step(f"Resolved '{tool}' binary: {exe_path}")
    ok(f"{tool} has been mounted with uvl.")
    print(f"👍 now you can use `uvl {tool} ...` with any {tool} arguments.")
    print(f"✨ {entry} will physically store at {display_home_path(store)}")
    print()
    print("💥 Caution: While a directory is mounted by uvl, avoid deleting or modifying it directly.")
    print(f"Unmount it first with `uvl --unmnt {entry}`.")


def print_status():
    manifest_path = Path.cwd() / ".uvl"
    if not manifest_path.exists():
        print("Project status: not initialized")
        print("No .uvl manifest exists in this directory.")
        print("Run a registered package manager through uvl to create one.")
        return

    try:
        entries = read_metadata(manifest_path)
    except Exception as exc:
        print("Project status: invalid")
        print(f"Manifest: {manifest_path}")
        print(f"Error: {exc}")
        return

    print("Project status:")
    print(f"  manifest: {manifest_path}")
    for entry, metadata in sorted(entries.items()):
        target = Path.cwd() / entry
        mounted = target.exists() and is_mountpoint(target)
        print(f"  {metadata['tool']}:")
        print(f"    mount: {entry}")
        print(f"    store: {display_home_path(get_tool_store(metadata['tool']))}")
        print(f"    records: {metadata['records']}")
        print(f"    mounted: {'yes' if mounted else 'no'}")


def print_list():
    config = load_config()
    for tool in sorted(config["registrations"]):
        print(tool)


def print_help():
    print("uvl: virtual dependency directories backed by a shared FUSE store")
    print()
    print("Usage:")
    print("  uvl --fuse <tool> --mnt <dir>     Register a package manager mount directory")
    print("  uvl <tool> [args...]              Run the package manager through uvl")
    print("  uvl --unmnt <dir>                 Unmount a virtualized dependency directory")
    print("  uvl --status                      Show current project mount status")
    print("  uvl --list                        List registered package manager binaries")
    print("  uvl --has <tool>                  Check whether a tool is mounted and installed")
    print("  uvl --version, -v                 Print version")
    print()
    print("Example:")
    print("  uvl --fuse bun --mnt node_modules")
    print("  uvl bun install")


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--help", "-h", action="store_true")
    parser.add_argument("--version", "-v", action="store_true")
    parser.add_argument("--fuse")
    parser.add_argument("--mnt")
    parser.add_argument("--unmnt")
    parser.add_argument("--has")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv[1:])

    if args.help or (not sys.argv[1:]):
        print_help()
        sys.exit(0)

    if args.version:
        print(f"uvl version {__version__}")
        sys.exit(0)

    mount_tool = args.fuse
    if mount_tool:
        entry = args.mnt or DEFAULT_ENTRIES.get(mount_tool)
        if not entry:
            print("Usage: uvl --fuse <tool> --mnt <dependency-dir>", file=sys.stderr)
            sys.exit(1)
        register_tool(mount_tool, entry)
        sys.exit(0)

    if args.unmnt:
        sys.exit(0 if unmount_target(args.unmnt) else 1)

    if args.has:
        registration = get_registration(args.has)
        ok = bool(registration and shutil.which(args.has))
        print("true" if ok else "false")
        sys.exit(0 if ok else 1)

    if args.status:
        print_status()
        sys.exit(0)

    if args.list:
        print_list()
        sys.exit(0)

    if args.command:
        execute_tool(args.command[0], args.command[1:])
        sys.exit(0)

    print_help()
    sys.exit(1)


if __name__ == "__main__":
    main()
