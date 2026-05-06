import hashlib
from pathlib import Path

from .paths import STORE_ROOT


def get_tool_store(tool):
    path = STORE_ROOT / tool
    path.mkdir(parents=True, exist_ok=True)
    (path / "objects").mkdir(exist_ok=True)
    return path


def hash_file(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def manifest_path_for(tool, target):
    return Path(target).resolve().parent / ".uvl"
