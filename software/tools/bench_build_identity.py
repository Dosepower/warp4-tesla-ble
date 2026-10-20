"""Checksum-bound bench build inputs. Never certifies a post-hoc SDK export."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import bench_confinement as confinement

VARIANTS = {"warp4_tesla_ble_iram_bench": "ordinary",
            "warp4_tesla_ble_iram_bench_minimal": "minimal"}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def load(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError) as exc:
        raise ValueError(f"required build identity artifact {path}: {exc}") from exc


def snapshot(roots, store=None):
    files = {}
    if store is not None:
        confinement.blob_output_plan(store).revalidate()
    for root in roots:
        root = Path(root).resolve()
        if not root.exists():
            raise ValueError(f"missing build input: {root}")
        paths = [root] if root.is_file() else (
            Path(directory) / name
            for directory, dirs, names in os.walk(root, followlinks=True)
            if not dirs.__setitem__(slice(None), sorted(d for d in dirs if d not in (".git", "__pycache__")))
            for name in sorted(names) if name != ".git" and not name.endswith(".pyc"))
        for path in paths:
            if not path.is_file():
                raise ValueError(f"unresolved input: {path}")
            checksum = sha(path)
            files[str(path)] = checksum
            if store is not None:
                target = Path(store) / checksum
                confinement.preflight(files=(target,)).revalidate()
                if not target.exists():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
                if sha(target) != checksum:
                    raise ValueError("input changed during snapshot")
    if not files:
        raise ValueError("empty build input snapshot")
    return files


def verify_files(files, store=None):
    if not isinstance(files, dict) or not files:
        raise ValueError("empty/untracked build identity inputs")
    for path, checksum in files.items():
        target = Path(store) / checksum if store is not None else Path(path)
        if not re.fullmatch(r"[0-9a-f]{64}", checksum) or not target.is_file() or sha(target) != checksum:
            raise ValueError(f"stale build identity input: {path}")


def verify_live_inputs(record):
    if snapshot(record["roots"]) != record["files"]:
        raise ValueError("pre/post compilation input set changed")


def sdk_sources(builder):
    builder = Path(builder)
    roots = [builder / name for name in ("main", "components", "managed_components", "tools", "patches", "configs",
                                         "CMakeLists.txt", "partitions.csv", "dependencies.lock", "sdkconfig", "esp-idf")]
    commands = load(builder / "build/compile_commands.json")
    compilers = {row["command"].split()[0] for row in commands}
    roots += [Path(p).resolve().parents[1] for p in sorted(compilers)]
    roots += [builder / "build/compile_commands.json", builder / "build/CMakeCache.txt"]
    return snapshot(roots)


def export_header_suffix(builder):
    idf = str(Path(builder) / "esp-idf")
    commit = subprocess.check_output(["git", "-C", idf, "rev-parse", "--short", "HEAD"], text=True).strip()
    branch = subprocess.run(["git", "-C", idf, "symbolic-ref", "--short", "HEAD"], text=True, capture_output=True)
    ref = branch.stdout.strip() if branch.returncode == 0 else subprocess.check_output(
        ["git", "-C", idf, "tag", "--points-at", "HEAD"], text=True).strip()
    return f'#define CONFIG_ARDUINO_IDF_COMMIT "{commit}"\n#define CONFIG_ARDUINO_IDF_BRANCH "{ref}"\n'


def verify_export_header(path, start):
    data = Path(path).read_bytes()
    suffix = start["export_header_suffix"].encode()
    if not suffix or not data.endswith(suffix) or hashlib.sha256(data[:-len(suffix)]).hexdigest() != start["generated_config_sha256"]:
        raise ValueError("exported SDK header differs from pre-compilation config/exact metadata suffix")


def begin_sdk(builder):
    builder = confinement.lexical(builder)
    plan = confinement.sdk_output_plan(builder)
    sdk = builder / "out/tools/esp32-arduino-libs"
    if sdk.exists() or (builder / "build/bootloader/bootloader.bin").exists():
        raise ValueError("SDK identity requires clean build/bootloader and out directories")
    start = {"schema": 2, "started_ns": time.time_ns(), "sources": sdk_sources(builder),
             "generated_config_sha256": sha(builder / "build/config/sdkconfig.h"),
             "export_header_suffix": export_header_suffix(builder)}
    plan.revalidate()
    (builder / "bench_sdk_start.unchecked.json").write_text(json.dumps(start, sort_keys=True) + "\n")


def seal_sdk(builder, minimal):
    builder = confinement.lexical(builder)
    plan = confinement.sdk_output_plan(builder)
    start = load(builder / "bench_sdk_start.unchecked.json")
    if start["sources"] != sdk_sources(builder):
        raise ValueError("SDK sources/config/toolchain changed during build")
    sdk = builder / "out/tools/esp32-arduino-libs"
    verify_export_header(sdk / "esp32/qio_qspi/include/sdkconfig.h", start)
    for name in ("bootloader.bin", "bootloader.elf"):
        if (builder / "build/bootloader" / name).stat().st_mtime_ns < start["started_ns"]:
            raise ValueError("stale pre-build bootloader output")
    outputs = snapshot([sdk])
    outputs = {str(Path(p).relative_to(sdk)): h for p, h in outputs.items()}
    evidence = snapshot([builder / "build" / p for p in (
        "bootloader/bootloader.bin", "bootloader/bootloader.elf", "bootloader/bootloader.map",
        "bootloader/config/sdkconfig.h", "compile_commands.json", "CMakeCache.txt")])
    proof = {"schema": 2, "variant": "minimal" if minimal else "ordinary", "builder": str(builder),
             "source_identity": digest(start), "inputs": start, "export_sha256": outputs, "builder_outputs": evidence}
    plan.revalidate()
    (sdk / "bench_sdk_provenance.json").write_text(json.dumps(proof, sort_keys=True, indent=2) + "\n")
    return proof


def verify_sdk(sdk, builder):
    sdk, builder = Path(sdk).resolve(), Path(builder).resolve()
    proof = load(sdk / "bench_sdk_provenance.json")
    if proof.get("schema") != 2 or proof.get("builder") != str(builder):
        raise ValueError("SDK identity missing fresh schema/builder association")
    if proof["source_identity"] != digest(proof["inputs"]):
        raise ValueError("SDK source identity checksum mismatch")
    verify_files(proof["inputs"]["sources"])
    if sdk_sources(builder) != proof["inputs"]["sources"]:
        raise ValueError("SDK source input set changed")
    verify_files(proof["builder_outputs"])
    expected = {str(sdk / p): h for p, h in proof["export_sha256"].items()}
    # PIO's component manager adds exactly this backup before executing the
    # build script. It is permitted only as a checksum-identical copy of the
    # already-bound original, never as a wildcard ignored/untracked file.
    original = str(sdk / "esp32/pioarduino-build.py")
    backup = sdk / "esp32/pioarduino-build.py.esp32"
    if backup.exists() and original in expected:
        expected[str(backup)] = expected[original]
    actual = snapshot([sdk])
    actual.pop(str(sdk / "bench_sdk_provenance.json"), None)
    if expected != actual:
        raise ValueError("stale/untracked SDK export identity")
    return proof


def capture_firmware(env, build, sdk, builder, project):
    build, project = confinement.lexical(build), confinement.lexical(project)
    # Validate identity AND content-store outputs before snapshot can copy.
    output = build / "bench_build_inputs.unchecked.json"
    confinement.blob_output_plan(project / ".pio/bench-input-blobs").revalidate()
    plan = confinement.output_plan(roots=(build, project / ".pio/bench-input-blobs"), files=(output,))
    proof = verify_sdk(sdk, builder)
    environment = env["PIOENV"]
    variant = VARIANTS[environment]
    if proof["variant"] != variant:
        raise ValueError("cross-variant SDK identity substitution")
    roots = [project / name for name in ("src", "lib", "boards", "tools", "platformio.ini", "warp4_tesla_ble.ini",
                                         "pio_hooks.py", "bench_guard_hook.py", "bench_image_hook.py", "r13_vfs_guard.py", "boot_app0.bin")]
    roots += [Path(env.GetProjectOption("board_build.partitions")), build / "metadata.json"]
    roots += [Path(env.PioPlatform().get_package_dir(p)) for p in (
        "framework-arduinoespressif32", "toolchain-xtensa-esp-elf")]
    roots += [Path(env.PioPlatform().get_dir()), project / ".pio/libdeps" / environment]
    roots += list(project.glob("*.ini"))
    store = project / ".pio/bench-input-blobs"
    plan.revalidate()  # all identity/store outputs before the first blob copy
    files = snapshot(roots, store)
    identity = {"schema": 2, "variant": variant, "environment": environment,
                "sdk_provenance_sha256": sha(Path(sdk) / "bench_sdk_provenance.json"),
                "metadata_sha256": sha(build / "metadata.json"), "files": files, "store": str(store),
                "roots": [str(Path(p).resolve()) for p in roots],
                "commands": {k: env.subst("$" + k) for k in (
                    "CC", "CXX", "CCFLAGS", "CXXFLAGS", "CPPFLAGS", "CPPPATH", "LINK", "LINKFLAGS", "LIBPATH", "LIBS")},
                "project_options": {k: str(v) for k, v in env.GetProjectOptions()}}
    confinement.preflight(files=(output,)).revalidate()
    output.write_text(json.dumps(identity, sort_keys=True, indent=2) + "\n")
    return identity


def verify_firmware(build, sdk, builder, elf):
    build = Path(build)
    identity = load(build / "bench_build_inputs.unchecked.json")
    proof = verify_sdk(sdk, builder)
    if identity.get("schema") != 2 or VARIANTS.get(identity.get("environment")) != identity.get("variant") or identity["variant"] != proof["variant"]:
        raise ValueError("cross-variant build identity substitution")
    if identity["metadata_sha256"] != sha(build / "metadata.json"):
        raise ValueError("stale/unbound metadata identity")
    if identity["sdk_provenance_sha256"] != sha(Path(sdk) / "bench_sdk_provenance.json"):
        raise ValueError("app/SDK identity substitution")
    markers = [s for s in elf.symbols if s.startswith("bench_inputs_")]
    if markers != ["bench_inputs_" + digest(identity)]:
        raise ValueError("ELF does not bind exact build input identity")
    verify_files(identity["files"], identity["store"])
    options = identity["project_options"]
    for key, value in {"custom_bench_only": "true", "custom_provisioning": "false", "custom_secure_boot": "", "custom_signature_preset": ""}.items():
        if options.get(key) != value:
            raise ValueError(f"unsafe/untracked build identity option {key}")
    metadata = load(build / "metadata.json")
    if metadata.get("secure_boot") != "" or metadata.get("signature_preset") != "" or metadata.get("split_esptool_ota") is not True:
        raise ValueError("unsafe metadata identity")
    for key in ("factory_data_enable_write_api", "esp32_common_enable_secure_boot_api", "product_id_is_warp4_provisioning"):
        if metadata.get("options", {}).get(key) != 0:
            raise ValueError(f"unsafe/untracked metadata option {key}")
    return {"build_identity": digest(identity), "variant": identity["variant"],
            "sdk_manifest_sha256": identity["sdk_provenance_sha256"], "inputs": identity,
            "sdk_provenance": proof}


def direct_logged_alias(path, project, known):
    """Only fold a parent step AFTER proving the crossed directory is real.

    Preserve unknown/dangling/intermediate alias chains without inspecting
    referent bytes. This retains ../build/file from a real build_latest dir.
    """
    link = path.readlink()
    if link.is_absolute():
        if not link.is_relative_to(project):
            return False
        current, parts = project, link.relative_to(project).parts
    else:
        current, parts = path.parent, link.parts
    if not parts or parts[-1] in ('.', '..'):
        return False
    for part in parts[:-1]:
        if part == '..':
            if current == project:
                return False
            current = current.parent
        else:
            current = current / part
        try:
            if confinement.stamp(current, True) is None:
                return False
        except (ValueError, OSError):
            return False
    return current / parts[-1] in known


def quarantine_legacy(project, logs):
    """Quarantine ONLY destinations explicitly logged by the old bench hook.

    Failed logs qualify too: the original bug exported before its failed gate.
    No basename globbing and no deletion of unrelated files or aliases.
    """
    project = confinement.lexical(project)
    logs = tuple(confinement.lexical(log) for log in logs)
    quarantine = project / ".pio/bench-quarantine/legacy-shared-output"
    directories = (project, project / "build", project / "build_latest", quarantine)
    # Validate lexical roots/ancestors before discovery or any filesystem write.
    confinement.preflight(directories=directories, files=logs).revalidate()
    names = set()
    for log in logs:
        for line in Path(log).read_text().splitlines():
            line = line.replace("Copying " + str(project) + "/", "Copying ", 1)
            match = re.fullmatch(r"Copying \.pio/build/(warp4_tesla_ble_iram_bench(?:_minimal)?)/firmware(?:_esptool|_ota)?\.(?:bin|elf) -> build/([^/]+)", line)
            if match and match[1] in VARIANTS:
                if match[2] in (".", ".."):
                    raise ValueError("refusing lexical quarantine source traversal")
                names.add(match[2])
    known = {project / "build" / name for name in names}
    candidates = []
    for directory in (project / "build", project / "build_latest"):
        if not directory.exists():
            continue
        for path in sorted(directory.iterdir()):
            # Match direct lexical aliases only; do not follow alias chains or
            # inspect their referents. Only the selected leaf link is unlinked.
            if path.is_symlink() and direct_logged_alias(path, project, known):
                candidates.append((path, {"link": str(path.readlink())}))
            elif path in known:
                confinement.preflight(files=(path,)).revalidate()
                candidates.append((path, {"sha256": sha(path), "bytes": path.stat().st_size}))
    targets = [(path, evidence, quarantine / ("REJECTED-UNVALIDATED-LEGACY-" + path.parent.name + "-" + path.name))
               for path, evidence in candidates]
    # All sources, unlink-only alias leaves, and ALL absent destinations form
    # one plan. A conflict in a later target cannot cause partial earlier moves.
    plan = confinement.preflight(directories=directories,
        files=(*logs, *(path for path, evidence, _ in targets if "sha256" in evidence),
               *(target for _, _, target in targets)),
        links=tuple(path for path, evidence, _ in targets if "link" in evidence))
    for _, _, target in targets:
        if target.exists() or target.is_symlink():
            raise ValueError(f"quarantine target already exists: {target}")
    plan.revalidate()
    for path, evidence, _ in targets:
        if "sha256" in evidence and sha(path) != evidence["sha256"]:
            raise ValueError("quarantine source bytes changed before mutation")
    quarantine.mkdir(parents=True, exist_ok=True)
    manifest = []
    # Record link targets before moving files (and making their aliases dangling).
    for path, evidence, target in targets:
        if "link" in evidence:
            path.unlink()
        else:
            path.rename(target)
            if sha(target) != evidence["sha256"]:
                raise ValueError("quarantined bytes changed")
        manifest.append({"source": str(path), "quarantine": str(target) if "sha256" in evidence else None, **evidence})
    return manifest
