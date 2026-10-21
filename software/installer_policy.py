"""Installer-only offline policy. Empty target tuple is the entire allowlist."""
from installer_sdk_verify import digest

ENVS = ("warp4_ble_installer", "warp4_multi_tesla_installer")

def validate(name, targets, options, locked_configs):
    if name not in ENVS or list(targets) != []:
        raise ValueError("OFFLINE_DEFAULT_ONLY: no explicit targets, aliases or device chains")
    if name not in locked_configs or digest(options) != digest(locked_configs[name]):
        raise ValueError("INSTALLER_CONFIG_MISMATCH: resolved options/injections differ from reviewed lock")
    return True

def reject_private_inputs(project):
    from pathlib import Path
    root = Path(project)
    paths = list(root.glob("default_wifi.json*")) + list(root.glob("*_wifi.json*"))
    paths += [root / "data" / name for name in ("spiffs.json", "wifi_sta_config.json", "wifi_soft_ap_config.json")]
    if any(p.exists() or p.is_symlink() for p in paths):
        raise ValueError("INSTALLER_PRIVATE_INPUT_FORBIDDEN")


_UNSET = object()

def validate_effective(env, not_for_distribution=False, *, approved=_UNSET):
    if type(not_for_distribution) is not bool or not_for_distribution:
        raise ValueError("INSTALLER_PRIVATE_NOT_FOR_DISTRIBUTION")
    # These inputs have no approved public-default role. Reject them in every
    # effective SCons flag carrier, including late CPPDEFINES/CCFLAGS mutations.
    from collections import UserList
    def leaves(value):
        if isinstance(value, (tuple, list, UserList)):
            for item in value:
                yield from leaves(item)
        else:
            yield str(value)
    for key in ("BUILD_FLAGS", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "LINKFLAGS"):
        for value in leaves(env.get(key, [])):
            if any(token in value.upper() for token in ("DEBUG_FS_ENABLE", "DEFAULT_WIFI_", "DEFAULT_MQTT_", "UNREVIEWED_DEFINE", "NOT_FOR_DISTRIBUTION")):
                raise ValueError("INSTALLER_PRIVATE_EFFECTIVE_FLAG: " + key)
    if approved is not _UNSET:
        keys = ("BUILD_FLAGS", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "LINKFLAGS")
        if type(approved) is not dict or set(approved) != set(keys) or any(type(v) is not list for v in approved.values()):
            raise ValueError("INSTALLER_EFFECTIVE_FLAG_CLOSURE_REQUIRED")
        effective = effective_flags(env)
        if digest(effective) != digest(approved):
            raise ValueError("INSTALLER_EFFECTIVE_FLAG_CLOSURE_MISMATCH")
    return True


def effective_flags(env):
    from collections import UserList
    def plain(value):
        if isinstance(value,(list,tuple,UserList)): return [plain(v) for v in value]
        if type(value) in (str,int,bool) or value is None: return value
        raise ValueError('INSTALLER_EFFECTIVE_FLAG_TYPE')
    return {key:plain(env.get(key,[])) for key in
            ('BUILD_FLAGS','CPPDEFINES','CCFLAGS','CFLAGS','CXXFLAGS','LINKFLAGS')}
