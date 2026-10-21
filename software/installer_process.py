"""No ambient process configuration is part of the installer contract."""

def clean_environment(extra=None):
    result = {"LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "TZ": "UTC",
              "HOME": "/nonexistent", "PATH": "/nonexistent"}
    if extra:
        result.update(extra)
    return result
