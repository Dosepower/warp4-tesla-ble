"""Read-only, externally anchored source-derived SDK verifier.

Never calls or promotes the review verifier. The manifest must be approved
outside the mutable SDK; this tool cannot establish trust by creating a lock.
Inventories are canonical JSON: sorted keys, compact separators, UTF-8.
"""
import hashlib
import json
from pathlib import Path
import re
import stat
from installer_safeio import read_bytes

PINS = {
    "builder": ["https://github.com/espressif/esp32-arduino-lib-builder.git", "ce4c80a2f6cbf8c55ba246ffa2000fcdb33d9e6c"],
    "esp-idf-secure": ["https://github.com/espressif/esp-idf.git", "3ea97e7591b6a83f5d6f8e0cc7f1b16f27b2b855"],
    "builder/components/arduino": ["https://github.com/Tinkerforge/arduino-esp32.git", "0ca24d74b6ec582e2780a9fdc9aadead8e272739"],
}
# Official WARP 2.13.6 reference: Tinkerforge/esp32-arduino-libs tf-esp-secure-20261022
# d80bc8e05ab1012010f5acd87ca974206a64f4ee. Its versions.txt revisions are local git-am
# commits that do not exist publicly; they remain unresolved gaps, never pins.
GAPS = [{"component": k, "revision": v} for k, v in
        (("builder", "b63070e"), ("esp-idf", "a3afb692749"), ("arduino", "b280564c6"))]
# Unresolved gaps of the superseded tf-esp-secure-20260922 reference stay recorded.
PREVIOUS_GAPS = [{"component": k, "revision": v} for k, v in
                 (("builder", "d5a56a1"), ("esp-idf", "d2a6bf5f2b4"), ("arduino", "fa903c6b9"))]
# sha256 of esp32/sdkconfig in the official tf-esp-secure-20261022 package.
OFFICIAL_SDKCONFIG_SHA256 = "680cb3155cb96c8c88ba866d274fbc115cba7903d2fb2068cd18f32f7e8c68ed"
# These are full inventories, NOT version strings or git HEAD-only assertions.
# sources includes recursive gitlinks/blobs and patched end trees; tools includes
# compiler/distribution bytes; recipe includes builder/export/transform scripts.
MATERIALS = ("sources", "managed", "tools", "recipe", "configs", "link_closure")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(read_bytes(path)).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     ensure_ascii=False).encode()).hexdigest()


def loads(text):
    def pairs(items):
        result = {}
        for k, v in items:
            require(k not in result, "duplicate JSON key: " + k)
            result[k] = v
        return result
    def constant(value):
        raise ValueError("nonfinite JSON constant: " + value)
    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant)


def hexpin(value, length=64):
    return isinstance(value, str) and re.fullmatch("[0-9a-f]{%d}" % length, value) is not None


def fields(value, required, optional=()):
    require(type(value) is dict and set(required) <= set(value) <= set(required) | set(optional),
            "unknown or missing schema fields")


def validate_inventory(value):
    require(type(value) is dict and bool(value), "nonempty file inventory required")
    for relative, record in value.items():
        require(type(relative) is str and relative and not Path(relative).is_absolute() and
                ".." not in Path(relative).parts and Path(relative).as_posix() == relative,
                "invalid inventory path")
        fields(record, ("size", "mode", "sha256"))
        require(type(record["size"]) is int and record["size"] >= 0 and
                type(record["mode"]) is int and 0 <= record["mode"] <= 0o777 and
                hexpin(record["sha256"]), "invalid inventory value or type")
    return True


def inventory(root):
    root = Path(root)
    require(root.is_dir() and not any(p.is_symlink() for p in (root, *root.absolute().parents)) and
            ".." not in root.parts, "inventory root missing or parent link")
    result = {}
    for p in sorted(root.rglob("*")):
        s = p.lstat()
        require(not stat.S_ISLNK(s.st_mode), "inventory symlink forbidden")
        if stat.S_ISDIR(s.st_mode):
            continue
        require(stat.S_ISREG(s.st_mode) and s.st_nlink == 1, "inventory special file or hardlink forbidden")
        result[p.relative_to(root).as_posix()] = {
            "size": s.st_size, "mode": stat.S_IMODE(s.st_mode), "sha256": sha(p)}
    require(bool(result), "empty inventory")
    return result


def recipe_inputs(project):
    # Same closed set as lib-builder/tesla_sdk.py inputs(): every policy defconfig
    # and the singleton NimBLE source policy bound to its sdk.patch.
    project = Path(project)
    paths = [project / "lib-builder/tesla_sdk.py",
             project / "lib-builder/defconfig.esp32secure",
             project / "lib-builder/defconfig.warp4-tesla-ble",
             project / "lib-builder/defconfig.warp4-tesla-psram-first",
             project / "lib-builder/defconfig.warp4-protected-resolved",
             project / "lib-builder/defconfig.warp4-tesla-single-link",
             project / "lib-builder/defconfig.warp4-tesla-nimble",
             project / "lib-builder/cacert-2026-10-20.pem"]
    nimble = project / "lib-builder/nimble-init-v2"
    entries = sorted(nimble.iterdir())
    require([e.name for e in entries] == ["sdk.patch", "source-policy.json"] and
            all(e.is_file() and not e.is_symlink() for e in entries),
            "NimBLE source policy must be exactly sdk.patch + source-policy.json")
    policy = loads(read_bytes(nimble / "source-policy.json"))
    require(policy.get("policy") == "tesla-psram-first-nimble-single-link-init-v2" and
            policy.get("sdk_patch_sha256") == sha(nimble / "sdk.patch"),
            "NimBLE source policy inventory not bound to sdk.patch")
    paths += entries
    paths += sorted((project / "patches/lib-builder").rglob("*.patch"))
    paths += sorted((project / "patches/lib-builder").rglob("*.rawpatch"))
    paths += sorted((project / "patches/lib-builder-pre").rglob("*.patch"))
    return {p.relative_to(project).as_posix(): sha(p) for p in paths}


def verify(package, manifest_path, lock, materials, project):
    fields(lock, ("schema", "manifest_sha256", "sdk_inventory_sha256", "pins", "recipe_inputs"),
           ("baseline_source_gaps", "previous_baseline_source_gaps", "status"))
    require(lock.get("schema") == "warp4-installer-sdk-lock-v1", "unknown SDK lock schema")
    require(hexpin(lock.get("manifest_sha256")), "EXTERNAL_SDK_MANIFEST_PIN_MISSING")
    require(hexpin(lock.get("sdk_inventory_sha256")), "SDK inventory pin missing")
    require(lock.get("pins") == PINS, "full source pins differ")
    for url, commit in lock["pins"].values():
        require(url.startswith("https://") and hexpin(commit, 40), "full source pin required")
    require(lock.get("recipe_inputs") == recipe_inputs(project), "recipe/patch/config inventory differs")
    mp = Path(manifest_path)
    require(mp.is_file() and not mp.is_symlink(), "external SDK manifest unavailable")
    manifest_bytes = read_bytes(mp)
    require(hashlib.sha256(manifest_bytes).hexdigest() == lock["manifest_sha256"], "external SDK manifest digest mismatch")
    m = loads(manifest_bytes)
    fields(m, ("schema", "kind", "exact_upstream_package", "baseline_source_gaps", "previous_baseline_source_gaps", "pins", "recipe_inputs", "materials", "outputs", "config_deltas", "commands", "secure_boot_sig_block_size"))
    require(m.get("schema") == "warp4-installer-sdk-v1" and
            m.get("kind") == "source-derived-warp4-secure-ble", "not an installer SDK provenance manifest")
    require(m.get("exact_upstream_package") is False and m.get("baseline_source_gaps") == GAPS and
            m.get("previous_baseline_source_gaps") == PREVIOUS_GAPS,
            "source-derived SDK must preserve unresolved baseline revisions")
    require("installable" not in m, "self-asserted installable flag forbidden")
    require(m.get("pins") == PINS and m.get("recipe_inputs") == lock["recipe_inputs"], "source/recipe pins differ")
    require(type(m.get("secure_boot_sig_block_size")) is int and
            m["secure_boot_sig_block_size"] == 1216, "SDK secure boot struct size not qualified")
    require(type(m.get("commands")) is list and bool(m["commands"]) and all(type(v) is str and v for v in m["commands"]) and
            isinstance(m.get("config_deltas"), list), "missing command/config-delta inventory")
    require(set(materials) == set(MATERIALS) and set(m.get("materials", {})) == set(MATERIALS),
            "complete provenance materials required")
    validate_inventory(m["outputs"])
    for key in MATERIALS:
        validate_inventory(m["materials"][key])
        require(digest(inventory(materials[key])) == digest(m["materials"][key]), "provenance materials inventory differs: " + key)
    actual = inventory(package)
    require(digest(actual) == digest(m.get("outputs")) and digest(actual) == lock["sdk_inventory_sha256"], "SDK output inventory differs")
    return {"_manifest": m, "sdk_manifest_sha256": lock["manifest_sha256"],
            "sdk_inventory_sha256": digest(actual), "exact_upstream_package": False}


REQUIRED_BLE = {
    "CONFIG_BT_ENABLED": "y", "CONFIG_BT_BLUEDROID_ENABLED": "y",
    "CONFIG_BT_NIMBLE_ENABLED": "n", "CONFIG_BT_CONTROLLER_ENABLED": "y",
    "CONFIG_BTDM_CTRL_MODE_BLE_ONLY": "y", "CONFIG_BTDM_CTRL_MODE_BTDM": "n",
    "CONFIG_BT_CLASSIC_ENABLED": "n", "CONFIG_BT_BLE_ENABLED": "y",
    "CONFIG_BT_GATTC_ENABLE": "y", "CONFIG_BT_GATTS_ENABLE": "n",
    "CONFIG_BTDM_BLE_MESH_SCAN_DUPL_EN": "n",
    "CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY": "y", "CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST": "n",
    "CONFIG_BT_BLE_SMP_BOND_NVS_FLASH": "n",
    "CONFIG_BT_BTC_TASK_STACK_SIZE": "8192", "CONFIG_BT_BTU_TASK_STACK_SIZE": "8192",
}


def config_map(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"(CONFIG_\w+)=(.*)", line)
        off = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
        if match or off:
            key, value = (match[1], match[2]) if match else (off[1], "n")
            require(key not in result, "duplicate sdkconfig symbol: " + key)
            result[key] = value
    return result


def verify_config_maps(baseline, resolved, approved_deltas, policy='stock'):
    require(policy in ('stock', 'tesla-psram-first-v1'), 'unknown allocation policy')
    threshold = 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'
    candidate = policy == 'tesla-psram-first-v1'
    if candidate:
        require(baseline.get(threshold) == '2048' and resolved.get(threshold) == '0',
                'Tesla allocation policy requires exactly 2048 -> 0')
    protected = ("CONFIG_SECURE_", "CONFIG_SPIRAM", "CONFIG_BOOTLOADER_APP_",
                 "CONFIG_PARTITION_TABLE", "CONFIG_ESPTOOLPY_FLASH", "CONFIG_ESP32_REV_MIN",
                 "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM")
    actual = []
    for key in sorted(baseline.keys() | resolved.keys()):
        before, after = baseline.get(key), resolved.get(key)
        if before != after:
            require(not key.startswith(protected) or (candidate and key == threshold), "baseline security/layout/PSRAM changed: " + key)
            actual.append((key, before, after))
    require(isinstance(approved_deltas, list), "explicit config delta list required")
    approved = []
    for delta in approved_deltas:
        require(set(delta) == {"key", "before", "after", "reason"} and
                isinstance(delta["reason"], str) and bool(delta["reason"].strip()), "unexplained config delta")
        approved.append((delta["key"], delta["before"], delta["after"]))
    require(approved == actual, "full semantic config deltas differ")
    required = REQUIRED_BLE | {"CONFIG_SECURE_BOOT": "y", "CONFIG_SPIRAM": "y",
                "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL": "0" if candidate else "2048",
                "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y", "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK": "n",
                "CONFIG_PARTITION_TABLE_OFFSET": "0xe000", "CONFIG_ESPTOOLPY_FLASHSIZE": "\"16MB\""}
    for key, value in required.items():
        require(resolved.get(key) == value, "required Secure+BLE configuration: " + key)


def verify_archive(data):
    require(data.startswith(b"!<arch>\n"), "ordinary ar archive required")
    pos, objects = 8, 0
    while pos < len(data):
        require(pos + 60 <= len(data) and data[pos + 58:pos + 60] == b"`\n", "invalid ar member header")
        name = data[pos:pos + 16].decode("ascii").strip()
        try:
            size = int(data[pos + 48:pos + 58])
        except ValueError:
            raise ValueError("invalid ar member length") from None
        pos += 60
        require(size >= 0 and pos + size <= len(data), "truncated ar member")
        member = data[pos:pos + size]
        if name not in ("/", "//", "/SYM64/"):
            require(len(member) >= 52 and member[:7] == b"\x7fELF\x01\x01\x01" and
                    member[16:20] == b"\x01\x00\x5e\x00", "non-Xtensa ELF32 relocatable archive member")
            objects += 1
        pos += size
        if size % 2:
            require(pos < len(data) and data[pos:pos+1] == b"\n", "ar alignment byte")
            pos += 1
    require(objects > 0 and pos == len(data), "empty archive or trailing bytes")


def verify_config_header(cfg, header):
    defines = {}
    for line in header.splitlines():
        if not re.search(r"\bCONFIG_\w+", line):
            continue
        if line.lstrip().startswith(("//", "/*", "*")):
            continue
        match = re.fullmatch(r"\s*#define (CONFIG_\w+) (.+?)\s*", line)
        require(match is not None, "ambiguous sdkconfig header directive")
        key, value = match.groups()
        require(key not in defines, "duplicate sdkconfig header symbol: " + key)
        defines[key] = value
    expected = {k: "1" if v == "y" else v for k, v in cfg.items() if v != "n"}
    def resolve(key, active=()):
        require(key in defines and key not in active, "undefined/cyclic sdkconfig header alias")
        value = defines[key]
        if re.fullmatch(r"CONFIG_\w+", value):
            return resolve(value, active + (key,))
        return value
    resolved = {key: resolve(key) for key in defines}
    # The pinned builder's copy-libs.sh appends these provenance macros, not
    # Kconfig values. Accept only the detached pinned IDF identity, never an
    # arbitrary added CONFIG_* define or a source-config override.
    metadata = {"CONFIG_ARDUINO_IDF_BRANCH": '""',
                "CONFIG_ARDUINO_IDF_COMMIT": '"' + PINS["esp-idf-secure"][1][:10] + '"'}
    present = set(metadata) & set(resolved)
    if present:
        require(present == set(metadata) and not set(metadata) & set(cfg) and
                all(resolved[key] == value for key, value in metadata.items()),
                "sdkconfig header builder identity mismatch")
        for key in metadata:
            del resolved[key]
    require(resolved == expected, "complete sdkconfig/header mismatch")


def qualify_contents(package, config_material, deltas, policy='stock'):
    """Additional semantic gate after external inventory verification."""
    package, config_material = Path(package), Path(config_material)
    baseline = config_material / "official-sdkconfig"
    require(sha(baseline) == OFFICIAL_SDKCONFIG_SHA256,
            "official baseline sdkconfig digest mismatch")
    cfg = config_map((package / "esp32/sdkconfig").read_text())
    verify_config_maps(config_map(baseline.read_text()), cfg, deltas, policy=policy)
    header = (package / "esp32/qio_qspi/include/sdkconfig.h").read_text()
    verify_config_header(cfg, header)
    archives = sorted((package / "esp32/lib").glob("*.a"))
    require(any(p.name == "libbt.a" for p in archives), "missing BLE archive")
    for archive in archives:
        verify_archive(archive.read_bytes())


if __name__ == "__main__":
    import argparse
    import sys
    project = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        lock = loads((project / "installer_sdk.lock.json").read_text())
        base = project / ".pio/installer-provenance"
        package = project / ".pio/installer-sdk/sha256" / str(lock.get("sdk_inventory_sha256"))
        result = verify(package, base / "manifest.json", lock, {k:base/k for k in MATERIALS}, project)
        m = loads((base / "manifest.json").read_text())
        qualify_contents(package, base / "configs", m["config_deltas"])
        print(json.dumps(result, sort_keys=True))
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print("SDK VERIFICATION BLOCKED: " + str(exc), file=sys.stderr)
        raise SystemExit(1)
