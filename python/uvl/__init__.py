from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("uvl")
except PackageNotFoundError:
    __version__ = "0.0.0"