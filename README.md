# uvl

`uvl` lets package managers keep using their normal dependency directories while
a FUSE filesystem redirects the physical files into a shared global store.

The project still sees directories like `node_modules` or `.venv`. The files
live under:

```bash
~/.uvl/store/<manager>
```

That keeps normal tool behavior intact while duplicate dependency trees can be
stored once and mounted back into each project as a read-only virtual view.

## Install

From PyPI:

```bash
pipx install uvl-fuse
# or
uv tool install uvl-fuse
```

From this repository:

```bash
❯ git clone https://github.com/ShinapriLN/uvl.git
  ...
❯ cd uvl
❯ make
  ...
❯ ./build/uvl --help
uvl: virtual dependency directories backed by a shared FUSE store

Usage:
  uvl --fuse <tool> --mnt <dir>   Register/fustion a tool mount directory
  uvl --fiss <tool>               Remove/fission a tool out of uvl
  uvl <tool> [args...]            Run the tool through uvl
  uvl --unmnt <dir>               Unmount a virtualized dependency directory
  uvl --stat                      Show current project mount status
  uvl --list, -l                  List fused tools
  uvl --has <tool>                Check whether a tool is fused with uvl
  uvl --version, -v               Print version
```

Published wheels include the native engine, so installation does not need to
compile the C code. Runtime mounting still needs Linux FUSE support and
permission to mount filesystems.

If you build from source, install:

- `cmake`
- a C compiler such as `gcc` or `clang`
- `pkg-config`
- FUSE 3 development headers
- OpenSSL development headers
- Python 3.8+ and `uv` if you want to build the wheel locally

## Platform Support

Published wheels:

- Linux x86_64

Runtime requirements:

- Linux kernel FUSE support
- `/dev/fuse` access
- `fusermount3` or an equivalent FUSE unmount helper

Source builds work on the same Linux environment, but the runtime FUSE
requirements still apply.

## Build From Source

The repository has one native CLI binary built by CMake:

- `build/uvl`

The binary handles the CLI, tool wrapping, object storage, and FUSE mount mode
in a single executable. It re-executes itself internally for mount mode, so
there is no separate mount binary to manage.

Use the Makefile wrapper for day-to-day commands:

```bash
make          # configure CMake and build the native binary
make build    # same as make
make check    # build the native binary and run a CLI smoke check
make wheel    # build a wheel with the bundled native binary
make clean    # remove build outputs and Python bytecode
```

## Register A Tool

Register the tool binary and the dependency directory it owns:

```bash
uvl --fuse bun --mnt node_modules
```

```text
[UVL] [ INFO ] Fused `bun` with uvl.
[UVL] [ INFO ] Mount directory: `node_modules`
[UVL] [ INFO ] Store: ~/.uvl/store/bun
[UVL] [ INFO ] Run `uvl bun ...` to use it through uvl.
```

The same pattern works for `uv`:

```bash
uvl --fuse uv --mnt .venv
```

Useful management flags:

```bash
uvl --list     # list registered tools from ~/.uvl/config.json
uvl --has uv   # check whether a tool binary exists in PATH
uvl --fiss uv  # remove a tool from ~/.uvl/config.json
uvl --stat    # show current project mount status
uvl --unmnt .venv
```

## Usage

After a tool is registered, run it through `uvl` whenever the dependency tree
may change.

### Bun

```bash
❯ uvl bun install
bun install v1.2.10 (db2e7d7f)

+ typescript@5.9.3
+ @types/bun@1.3.13

5 packages installed [11.00ms]
[UVL] [ INFO ] Taking control of '<project>/node_modules'.
[UVL] [ INFO ] Mounting virtual filesystem with 635 files...
[UVL] [ INFO ] Successfully mounted virtual '<project>/node_modules'.
[UVL] [ INFO ] Deduplication saved 27.50 MB of physical disk space.
[UVL] [ INFO ] Virtualized 27.50 MB into ~/.uvl/store/bun.

❯ uvl bun add vite
[uvl] Restoring mounted entry 'node_modules' before running bun
bun add v1.2.10 (db2e7d7f)

installed vite@8.0.11 with binaries:
 - vite

17 packages installed [1.88s]
[UVL] [ INFO ] Re-virtualizing '<project>/node_modules' after command completed.
[UVL] [ INFO ] Mounting virtual filesystem with 846 files...
[UVL] [ INFO ] Successfully remounted virtual '<project>/node_modules'.
[UVL] [ INFO ] Deduplication saved 26.39 MB of physical disk space.
[UVL] [ INFO ] Virtualized 93.64 MB into ~/.uvl/store/bun.
```

The directory still works as normal:

```bash
❯ bun run index.ts
Hello via Bun!

❯ uvl bun run index.ts
Hello via Bun!
```

Disk usage in the project directory stays near zero:

```bash
❯ du -sh node_modules
0	node_modules
```

### Uv

```bash
❯ uv init app
Initialized project `app` at `<project-root>/app`

❯ cd app

❯ uvl uv sync
Using CPython 3.12.12
Creating virtual environment at: .venv
Resolved 1 package in 2ms
Audited in 0.00ms
[UVL] [ INFO ] Taking control of '<project>/.venv'.
[UVL] [ INFO ] Mounting virtual filesystem with 19 files...
[UVL] [ INFO ] Successfully mounted virtual '<project>/.venv'.
[UVL] [ INFO ] Deduplication saved 0.01 MB of physical disk space.
[UVL] [ INFO ] Virtualized 0.03 MB into ~/.uvl/store/uv.
```

Running through either command still works:

```bash
❯ uvl uv run main.py
Hello from app!

❯ uv run main.py
Hello from app!
```

## Project Status

`--stat` shows every mounted tool in the current project. A project can track
multiple entries at once:

```bash
❯ uvl --stat
Project status:
  manifest: <project>/.uvl
  uv:
    mount: .venv
    store: ~/.uvl/store/uv
    records: 46469
    mounted: yes
  bun:
    mount: node_modules
    store: ~/.uvl/store/bun
    records: 22364
    mounted: yes
```

## Unmount

Mounted dependency directories are intentionally read-only. If you want to
delete, inspect, or rebuild the directory directly, unmount it first:

```bash
uvl --unmnt .venv
```

After unmounting, the mount point is just a normal directory again.

## How It Works

1. `uvl --fuse <tool> --mnt <dir>` saves a registration in
   `~/.uvl/config.json`.
2. `uvl --fiss <tool>` removes the tool from `~/.uvl/config.json`.
3. `uvl <tool> ...` resolves the real tool binary from `PATH` and runs it with
   the same arguments.
4. If the registered entry directory exists, `uvl` restores the mounted view
   before the tool changes files.
5. Files are content-addressed by SHA-256 and stored under
   `~/.uvl/store/<tool>/objects`.
6. A binary project `.uvl` manifest stores every mounted tool and mount
   directory for fast loading. One project can track multiple entries such as
   `.venv` and `node_modules` at the same time.
7. A C FUSE daemon mounts a read-only virtual filesystem at the original
   dependency directory.

The goal is simple: tools keep their normal layout while projects stop holding
duplicate physical dependency trees.

---
*Shinapri*