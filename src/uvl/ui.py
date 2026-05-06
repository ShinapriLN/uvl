from pathlib import Path


def display_path(path):
    try:
        return str(Path(path).expanduser().resolve().relative_to(Path.cwd().resolve()))
    except ValueError:
        return str(path)


def display_home_path(path):
    path = Path(path)
    try:
        return "~/" + str(path.expanduser().resolve().relative_to(Path.home().resolve()))
    except ValueError:
        return str(path)
