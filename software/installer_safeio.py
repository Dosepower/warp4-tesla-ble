"""Linux fd-relative IO: no symlink traversal or inherited descriptors."""
import os
from pathlib import Path
import stat


def directory_fd(path, create=False):
    path = Path(path).absolute()
    if ".." in path.parts:
        raise ValueError("ARTIFACT_PARENT_LINK_FORBIDDEN")
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in path.parts[1:]:
            if create:
                try:
                    os.mkdir(part, 0o700, dir_fd=fd)
                except FileExistsError:
                    pass
            new = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            os.close(fd); fd = new
        return fd
    except OSError as exc:
        os.close(fd)
        raise ValueError("ARTIFACT_PARENT_LINK_FORBIDDEN") from exc
    except BaseException:
        os.close(fd)
        raise


def file_fd(path):
    path = Path(path).absolute()
    parent = directory_fd(path.parent)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=parent)
    except OSError as exc:
        raise ValueError("FILE_LINK_OR_MISSING_FORBIDDEN") from exc
    finally:
        os.close(parent)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
            raise ValueError("FILE_SPECIAL_OR_HARDLINK_FORBIDDEN")
        return fd, st
    except BaseException:
        os.close(fd)
        raise


def read_bytes(path):
    fd, st = file_fd(path)
    with os.fdopen(fd, "rb") as stream:
        data = stream.read()
        end = os.fstat(stream.fileno())
    if (st.st_size, st.st_mtime_ns, st.st_ctime_ns) != (end.st_size, end.st_mtime_ns, end.st_ctime_ns):
        raise ValueError("INPUT_CHANGED_DURING_READ")
    return data
