import sys


def step(message):
    print(f"[uvl] {message}")


def ok(message):
    print(f"[uvl] ✅ {message}")


def warn(message):
    print(f"[uvl] ⚠️  {message}")


def error(message):
    print(f"[uvl] ❌ {message}", file=sys.stderr)
