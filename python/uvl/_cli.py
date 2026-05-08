import os
import sys
from pathlib import Path


def main() -> None:
    exe = Path(__file__).resolve().parent / "bin" / "uvl"
    os.execv(str(exe), [str(exe), *sys.argv[1:]])