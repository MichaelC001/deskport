"""Filters shared by the source and static-relink material exporters."""
from pathlib import PurePosixPath


def material_filter():
    """Return a fresh member filter for one archive, including recursive adds.

    Tracked files and component-level exports intentionally overlap. Keep the
    first identical member metadata and reject conflicting archive entries.
    """
    seen = {}

    def include(info):
        path = PurePosixPath(info.name)
        if any(part in ('.git', '__pycache__') for part in path.parts) or info.name.endswith('.pyc'):
            return None
        name = str(path)
        identity = (info.type, info.size, info.mode, info.linkname, info.mtime)
        if name in seen:
            if seen[name] != identity:
                raise ValueError('Conflicting material archive member: ' + name)
            return None
        seen[name] = identity
        return info

    return include
