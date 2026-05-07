import os
import shutil
import struct
from pathlib import Path

from .log import step
from .store import get_tool_store, hash_file, manifest_path_for


MAGIC = b"UVL3"
COUNT = struct.Struct("<I")
ENTRY = struct.Struct("<III")
RECORD = struct.Struct("<BIII")
KIND_DIR = 1
KIND_FILE = 2
KIND_SYMLINK = 3


def mode_to_octal(mode):
    return mode & 0o7777


def read_manifest(manifest_path):
    entries = {}
    manifest_path = Path(manifest_path)
    if not manifest_path.exists():
        return entries

    with open(manifest_path, "rb") as handle:
        if handle.read(len(MAGIC)) != MAGIC:
            raise ValueError(f"{manifest_path} is not a uvl project manifest")
        header = handle.read(COUNT.size)
        if len(header) != COUNT.size:
            raise ValueError(f"{manifest_path} has a truncated header")
        (entry_count,) = COUNT.unpack(header)

        for _ in range(entry_count):
            header = handle.read(ENTRY.size)
            if len(header) != ENTRY.size:
                raise ValueError(f"{manifest_path} has a truncated entry")
            tool_len, entry_len, record_count = ENTRY.unpack(header)
            tool = handle.read(tool_len).decode()
            entry = handle.read(entry_len).decode()
            records = []
            for _ in range(record_count):
                record_header = handle.read(RECORD.size)
                if len(record_header) != RECORD.size:
                    raise ValueError(f"{manifest_path} has a truncated record")
                kind, mode, virtual_len, physical_len = RECORD.unpack(record_header)
                virtual_path = handle.read(virtual_len).decode()
                physical_path = handle.read(physical_len).decode()
                records.append((kind, virtual_path, physical_path, mode))
            entries[entry] = {"tool": tool, "entry": entry, "records": records}
    return entries


def write_manifest(manifest_path, entries):
    manifest_path = Path(manifest_path)
    with open(manifest_path, "wb") as handle:
        handle.write(MAGIC)
        handle.write(COUNT.pack(len(entries)))
        for entry in sorted(entries):
            data = entries[entry]
            tool_bytes = data["tool"].encode()
            entry_bytes = data["entry"].encode()
            records = data["records"]
            handle.write(ENTRY.pack(len(tool_bytes), len(entry_bytes), len(records)))
            handle.write(tool_bytes)
            handle.write(entry_bytes)
            for kind, virtual_path, physical_path, mode in records:
                virtual_bytes = virtual_path.encode()
                physical_bytes = str(physical_path).encode()
                handle.write(RECORD.pack(kind, mode_to_octal(mode), len(virtual_bytes), len(physical_bytes)))
                handle.write(virtual_bytes)
                handle.write(physical_bytes)


def read_metadata(manifest_path):
    return {
        entry: {"tool": data["tool"], "entry": data["entry"], "records": len(data["records"])}
        for entry, data in read_manifest(manifest_path).items()
    }


def read_records(manifest_path, entry=None):
    entries = read_manifest(manifest_path)
    if entry is None:
        for data in entries.values():
            yield from data["records"]
        return
    if entry not in entries:
        raise ValueError(f"{manifest_path} has no entry named {entry}")
    yield from entries[entry]["records"]


def scan_records(tool, target):
    store = get_tool_store(tool)
    objects_dir = store / "objects"
    step(f"Object store: {objects_dir}")

    records = [(KIND_DIR, "/", "", target.stat().st_mode)]
    file_count = 0
    stored_bytes = 0
    duplicate_bytes = 0

    for root_dir, dirnames, filenames in os.walk(target, topdown=True, followlinks=False):
        root = Path(root_dir)

        for dirname in list(dirnames):
            path = root / dirname
            rel_path = "/" + str(path.relative_to(target))
            try:
                stat_result = path.lstat()
            except OSError:
                continue
            if path.is_symlink():
                records.append((KIND_SYMLINK, rel_path, os.readlink(path), stat_result.st_mode))
                dirnames.remove(dirname)
                file_count += 1
            else:
                records.append((KIND_DIR, rel_path, "", stat_result.st_mode))

        for filename in filenames:
            path = root / filename
            rel_path = "/" + str(path.relative_to(target))
            try:
                stat_result = path.lstat()
            except OSError:
                continue

            if path.is_symlink():
                records.append((KIND_SYMLINK, rel_path, os.readlink(path), stat_result.st_mode))
                file_count += 1
                continue

            if not path.is_file():
                continue

            size = stat_result.st_size
            digest = hash_file(path)
            object_path = objects_dir / digest
            if object_path.exists():
                path.unlink()
                duplicate_bytes += size
            else:
                shutil.move(str(path), str(object_path))
                os.chmod(object_path, stat_result.st_mode & 0o777)
                stored_bytes += size
            records.append((KIND_FILE, rel_path, str(object_path), stat_result.st_mode))
            file_count += 1

    return records, file_count, stored_bytes, duplicate_bytes


def build_mapping(tool, target):
    target = target.resolve()
    entry = target.name
    manifest_path = manifest_path_for(tool, target)
    entries = read_manifest(manifest_path)
    records, file_count, stored_bytes, duplicate_bytes = scan_records(tool, target)
    entries[entry] = {"tool": tool, "entry": entry, "records": records}
    write_manifest(manifest_path, entries)
    return manifest_path, file_count, stored_bytes, duplicate_bytes


def restore_from_manifest(manifest_path, target):
    from .fuse import is_mountpoint, unmount_target

    target = Path(target).expanduser()
    entry = target.name
    if is_mountpoint(target) and not unmount_target(target, quiet=True):
        raise RuntimeError(f"cannot restore {target}: still mounted")

    entries = read_manifest(manifest_path)
    if entry not in entries:
        raise ValueError(f"{manifest_path} has no entry named {entry}")

    if target.exists() or target.is_symlink():
        if target.is_dir() and not target.is_symlink():
            shutil.rmtree(target, ignore_errors=True)
        else:
            try:
                target.unlink()
            except FileNotFoundError:
                pass
    try:
        target.mkdir(parents=True, exist_ok=True)
    except FileExistsError:
        if target.is_dir() and not target.is_symlink():
            pass
        else:
            try:
                if target.is_symlink() or target.is_file():
                    target.unlink()
                else:
                    shutil.rmtree(target, ignore_errors=True)
            except FileNotFoundError:
                pass
            target.mkdir(parents=True, exist_ok=True)

    for kind, virtual_path, physical_path, mode in entries[entry]["records"]:
        rel = virtual_path.lstrip("/")
        path = target / rel if rel else target

        if kind == KIND_DIR:
            path.mkdir(parents=True, exist_ok=True)
            os.chmod(path, mode & 0o777)
        elif kind == KIND_SYMLINK:
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.exists() or path.is_symlink():
                path.unlink()
            path.symlink_to(physical_path)
        elif kind == KIND_FILE:
            path.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(physical_path, path)
            except OSError:
                shutil.copy2(physical_path, path)
            os.chmod(path, mode & 0o777)
