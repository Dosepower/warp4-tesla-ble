"""Pure unsigned WARP4 OTA serializer and local atomic candidate staging.

Never imports merge_firmware_hook or any signing/provisioning helper.
A staged candidate is not a device/runtime installation approval.
"""
import hashlib
import json
import os
from pathlib import Path
from zlib import crc32


def validate_metadata(metadata):
    from installer_sdk_verify import fields, require
    required = ("firmware_info_product_id", "firmware_info_product_name", "version", "build_timestamp")
    optional = {"product_id": str, "project_dir": str, "build_dir": str, "firmware_basename": str,
                "signature_preset": str, "signature_verify_preset": str, "split_esptool_ota": bool,
                "secure_boot": str, "frontend_modules": list, "branding_mod_path": str,
                "options": dict, "installer_public_key_sha256": str}
    fields(metadata, required, optional)
    for key, kind in optional.items():
        require(key not in metadata or type(metadata[key]) is kind, "OTA metadata type: " + key)
    require(type(metadata["firmware_info_product_id"]) is str and type(metadata["firmware_info_product_name"]) is str,
            "OTA metadata identity type")
    require(type(metadata["build_timestamp"]) is int and 0 <= metadata["build_timestamp"] <= 0xffffffff, "OTA timestamp type/range")
    version = metadata["version"]
    require(type(version) is list and len(version) == 4, "OTA version list required")
    for value in version:
        require(type(value) is int or (type(value) is str and value.isascii() and value.isdecimal()), "OTA version exact type")
        require(0 <= int(value) <= 255, "OTA version range")
    return True


def firmware_info(app, metadata):
    validate_metadata(metadata)
    if metadata["firmware_info_product_id"] != "warp4" or metadata["firmware_info_product_name"] != "WARP4 Charger":
        raise ValueError("WARP4 identity required")
    info = bytearray(b"\xff" * 4096)
    info[:8] = bytes.fromhex("7121ce12f0126e04")
    name = metadata["firmware_info_product_name"].encode()
    info[8:69] = name + bytes(61 - len(name))
    version = [int(x) for x in metadata["version"]]
    if len(version) != 4 or any(x < 0 or x > 255 for x in version):
        raise ValueError("firmware version range")
    info[69:72] = bytes(version[:3])
    info[72:76] = metadata["build_timestamp"].to_bytes(4, "little")
    info[76] = version[3]
    info[77:138] = b"warp4" + bytes(56)
    info[138:170] = hashlib.sha256(app + b"\xff" * (-len(app) % 4096)).digest()
    info[4092:] = crc32(info[:4092]).to_bytes(4, "little")
    return bytes(info)


def make_container(app, metadata):
    return b"\xff" * 0xc000 + firmware_info(app, metadata) + b"\xff" * 0x2000 + app


def read_app_fd(path, start_ns):
    from installer_safeio import file_fd
    fd, st = file_fd(path)
    with os.fdopen(fd, "rb") as stream:
        if st.st_mtime_ns < start_ns:
            raise ValueError("STALE_OR_MISSING_FINAL_APP")
        app = stream.read()
        end = os.fstat(stream.fileno())
        if (st.st_size, st.st_mtime_ns) != (end.st_size, end.st_mtime_ns):
            raise ValueError("APP_CHANGED_DURING_READ")
        return app


def stage_ota(app_path, output, metadata, start_ns, provenance, *, app_bytes=None, evidence=None):
    from installer_ota_verify import verify_container
    from installer_safeio import directory_fd
    import ctypes
    import errno
    import uuid
    output = Path(output).absolute()
    # Hold the destination parent before any attacker-triggerable parsing or IO.
    parent = directory_fd(output.parent, create=True)
    temporary = ".ota-staging-" + uuid.uuid4().hex
    staging = None
    names = ('firmware-UNSIGNED_ota.bin','firmware_info.bin','manifest.json')
    from installer_sdk_verify import require
    evidence={} if evidence is None else evidence
    require(type(evidence) is dict and set(evidence)<= {'firmware.elf','firmware.map','metadata.json',
            'host-stage-dag.json','linked.json','build.stdout','build.stderr','posthook.stdout','posthook.stderr','generated.json','generate.stdout','generate.stderr'} and
            all(type(v) is bytes and len(v)<=64*1024*1024 for v in evidence.values()),'OTA_EXPORT_EVIDENCE')
    names += tuple(sorted(evidence))
    published = False
    try:
        try:
            os.stat(output.name, dir_fd=parent, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise ValueError("OUTPUT_ALREADY_EXISTS")
        app = read_app_fd(app_path, start_ns) if app_bytes is None else app_bytes
        data = make_container(app, metadata)
        verified = verify_container(data, app, expected_metadata=metadata)
        manifest = {"schema": "warp4-ota-candidate-v1", "status": "unsigned-offline-candidate-not-device-approved",
                    "provenance": provenance, "verification": verified}
        if 'stage_closure' in provenance:
            manifest['stage_closure']=provenance['stage_closure']
        os.mkdir(temporary, 0o700, dir_fd=parent)
        staging = os.open(temporary, os.O_DIRECTORY | os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        contents = (data, data[0xc000:0xd000], (json.dumps(manifest, sort_keys=True, indent=2, allow_nan=False) + "\n").encode())
        contents += tuple(evidence[name] for name in sorted(evidence))
        for name, content in zip(names, contents):
            fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=staging)
            with os.fdopen(fd, "wb") as stream:
                stream.write(content); stream.flush(); os.fsync(stream.fileno())
        fd = os.open(names[0], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=staging)
        with os.fdopen(fd, "rb") as stream:
            verify_container(stream.read(), app, expected_metadata=metadata)
        os.fsync(staging)
        # Linux renameat2(RENAME_NOREPLACE): no check/rename overwrite race.
        libc = ctypes.CDLL(None, use_errno=True)
        rename = libc.renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(parent, os.fsencode(temporary), parent, os.fsencode(output.name), 1):
            error = ctypes.get_errno()
            if error == errno.EEXIST:
                raise ValueError("OUTPUT_ALREADY_EXISTS")
            raise OSError(error, os.strerror(error))
        published = True
        os.fsync(parent)
    finally:
        if staging is not None:
            if not published:
                for name in names:
                    try:
                        os.unlink(name, dir_fd=staging)
                    except FileNotFoundError:
                        pass
            os.close(staging)
            if not published:
                os.rmdir(temporary, dir_fd=parent)
        os.close(parent)
    return output / names[0]
