# uvl

`uvl` lets tools keep using their normal dependency directories while
a FUSE filesystem redirects the physical files into a shared global store.

Your project still sees `node_modules`, `.venv`, `vendor`, or any other entry
directory the tool expects. The bytes live under:

```bash
~/.uvl/store/<manager>
```

That means existing tools, editors, and runtime resolution keep working, but
duplicated dependency files can be stored once and mounted back into each project
as a read-only virtual directory.

## Install

From PyPI:

```bash
pipx install uvl-fuse
# or
uv tool install uvl-fuse
```

From this repository:

```bash
git clone https://github.com/ShinapriLN/uvl.git
cd uvl
make build
uv tool install .
```

Prebuilt wheels include the native engine, so installation does not compile
anything. On Linux, runtime mounting still needs FUSE support and permission to
mount filesystems.

If you build from source instead of using a prebuilt wheel, install:

- `cmake`
- a C compiler such as `gcc` or `clang`
- `pkg-config`
- FUSE 3 development headers
- OpenSSL development headers
- Python 3.8+ and `uv` if you want to build the wheel locally

## Platform Support

Current published wheels:

- Linux x86_64

Runtime requirements:

- Linux kernel FUSE support
- `/dev/fuse` access
- `fusermount3` or an equivalent FUSE unmount helper

Source builds are still possible from this repository, but the published PyPI
package is intended for Linux x86_64 users who want a prebuilt wheel.

## Build From Source

The repository currently has two runnable paths:

- `build/uvl`: a single native binary built by CMake. It handles the CLI,
  tool wrapping, object storage, and FUSE mount mode in one
  executable.
- Python package entrypoint: kept for packaging compatibility while the native
  binary matures.

Use the Makefile wrapper for day-to-day commands:

```bash
make          # configure CMake and build the native binary
make build    # same as make
make check    # build C code and run Python import/CLI checks
make wheel    # build a wheel with the bundled native binary
make clean    # remove build outputs and Python bytecode
```

The native build output is created at:

```bash
build/uvl
```

The native binary starts its own internal FUSE mode by re-executing itself as
`uvl __mount ...`, so there is no separate `uvl_fuse` binary in the CMake build.
The Python wheel bundles this binary at `uvl/native/uvl` so users do not need to
compile it during install.

## Mount A Tool

Register the tool binary and the dependency directory it owns:

```bash
uvl --fuse bun --mnt node_modules
```

```text
🚀 bun has been mounted with uvl.
👍 now you can use `uvl bun ...` with any bun arguments.
✨ node_modules will physically store at ~/.uvl/store/bun

💥 Caution: While a directory is mounted by uvl, avoid deleting or modifying it directly.
Unmount it first with `uvl --unmnt node_modules`.
```

Then run the tool through `uvl` when dependencies may change:

```bash
uvl bun install
uvl bun add vite
uvl bun remove vite
```

After the tool finishes, `uvl` scans the dependency directory, moves
physical files into `~/.uvl/store/bun/objects`, updates the project `.uvl`
binary manifest, clears the local directory, and mounts the
virtual view back at `node_modules`.

The directory still works as normal:

```bash
bun run index.ts
uvl bun run index.ts
```

But disk usage in the project directory is near zero:

```bash
du -sh node_modules
# 0 node_modules
```

## Python Example

Register `uv` with its virtual environment directory:

```bash
uvl --fuse uv --mnt .venv
```

Create or sync a project through `uvl`:

```bash
uvl uv init app
cd app
uvl uv sync
```

`uvl` mounts `.venv` after `uv sync`, while the stored files live under:

```bash
~/.uvl/store/uv
```

Running through either command still works:

```bash
uvl uv run main.py
uv run main.py
```

## Unmount

Mounted dependency directories are intentionally read-only. If you want to
delete, inspect, or rebuild the directory directly, unmount it first:

```bash
uvl --unmnt .venv
```

```text
✅ Unmounted .venv
⚡ You can now delete or modify anything inside
```

After unmounting, the mount point is just a normal directory again.

## Commands

```bash
uvl --fuse <tool> --mnt <dir>    Register a tool mount directory
uvl --fiss <tool>                Remove a tool from ~/.uvl/config.json
uvl <tool> [args...]              Run a mounted tool
uvl --unmnt <dir>                 Unmount a virtualized directory
uvl --status                      Show current project mount status
uvl --list                        List registered tools
uvl --has <tool>                  Check if a mounted tool is installed
uvl --version                     Print the version
```

`uvl` has defaults for common tools, so this is also valid:

```bash
uvl --fuse bun
uvl --fuse uv
uvl --fiss bun
```

## How It Works

1. `uvl --fuse <tool> --mnt <dir>` saves a registration in
   `~/.uvl/config.json`.
2. `uvl --fiss <tool>` removes the tool from `~/.uvl/config.json`.
3. `uvl <tool> ...` runs the real tool binary with the same
   arguments.
4. If the registered entry directory exists and is not already mounted, `uvl`
   scans the result.
5. Files are content-addressed by SHA-256 and stored under
   `~/.uvl/store/<tool>/objects`.
6. A binary project `.uvl` manifest stores every mounted tool, mount directory,
   and virtual path map for fast loading. One project can track entries such as
   `.venv` and `node_modules` at the same time.
7. A C FUSE daemon mounts a read-only virtual filesystem at the original
   dependency directory.

The goal is simple: tools keep their normal layout; projects stop
holding duplicate physical dependency trees.
