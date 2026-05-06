# uvl 

An (almost) Universal Linker 🚀

**Solve disk space bloat across all your development ecosystems.**

`uvl` is a high-performance, zero-dependency CLI wrapper that enforces "Best-in-Class" storage optimization for over 40+ package managers. It ensures that your `node_modules`, `.venv`, and global caches are centralized and deduplicated, saving you gigabytes of disk space.

---

## Why uvl? 💾

Modern development environments are disk space black holes. 
- **Python:** Every `.venv` often contains full copies of massive libraries like `torch` or `pandas`.
- **Node.js:** `node_modules` is notoriously heavy and duplicated across every project.
- **Rust/Go/JVM:** Global caches are often scattered and unmanaged.

**`uvl` fixes this by:**
1. **Centralizing:** Redirecting all tool caches to a single, managed store at `~/.uvl/store/`.
2. **Linking:** Forcing tools (like `uv`) to use **symbolic links** instead of full copies.
3. **Optimizing:** Automatically injecting the best storage-saving flags (like `--store-dir` for `pnpm`) without you having to remember them.

---

## Installation 📦

Install `uvl` globally using your preferred tool:

```bash
# clone 
git clone https://github.com/ShinapriLN/uvl.git 
cd uvl

# Using uv
uv tool install .

# Using pip
pip install .
```

---

## Usage 🛠️

Simply prefix your usual package manager commands with `uvl`.

### Examples:

```bash
# Optimized Python environment (uses symlinks to global store)
uvl uv add requests

# Optimized JS installation (uses pnpm content-addressable store)
uvl pnpm install

# Centralized Cargo registry
uvl cargo build

# Check if a tool is supported and installed
uvl --has bun
```

### Pro Tip:
Add an alias to your `.zshrc` or `.bashrc` to make it even faster:
```bash
alias uv="uvl uv"
alias pnpm="uvl pnpm"
```

---

## Supported Tools (44+) 🌍

`uvl` provides optimized storage mappings for:

| Ecosystem | Supported Tools |
| :--- | :--- |
| **Python** | `uv`, `pip`, `poetry`, `pdm`, `pipenv`, `conda`, `mamba`, `micromamba`, `pixi` |
| **JS / TS** | `npm`, `pnpm`, `yarn`, `bun`, `deno` |
| **Rust** | `cargo` |
| **Go** | `go` |
| **JVM** | `mvn`, `gradle`, `sbt`, `coursier`, `lein` |
| **.NET** | `dotnet`, `nuget` |
| **PHP** | `composer` |
| **Ruby** | `gem`, `bundle` |
| **C / C++** | `vcpkg`, `conan`, `spack` |
| **Mobile / Others** | `dart`, `pub`, `swift`, `mix`, `rebar3`, `cabal`, `stack`, `opam`, `julia`, `R`, `luarocks`, `zig`, `nimble`, `shards` |

---

## Goal 🎯

Stop wasting disk space on duplicated dependencies. One store to rule them all.
