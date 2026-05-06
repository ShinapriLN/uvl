try:
    from importlib import metadata

    __version__ = metadata.version("uvl")
except (metadata.PackageNotFoundError, ImportError):
    __version__ = "0.0.1-dev"
