#!/usr/bin/env python3
"""Prepare isolated BENCH ONLY SDK inputs from a patched local lib-builder.

Does not fetch, patch, build, flash, sign, encrypt or commit. Requires a complete
patched IDF source and a complete lib-builder input tree. A fresh output tree is
required; the original builder/package is never changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bench_build_identity as identity
import bench_confinement as confinement


def prepare(source, idf, base_config, destination, minimal=False):
    source, idf, base_config = [Path(p).resolve() for p in (source, idf, base_config)]
    destination = confinement.lexical(destination)
    confinement.preflight(directories=(destination,)).revalidate()
    if destination.exists():
        raise ValueError(f"fresh destination required: {destination}")
    for path in (idf / "tools/cmake/project.cmake", base_config, source / "CMakeLists.txt"):
        if not path.is_file():
            raise ValueError(f"missing input: {path}")
    destination.mkdir(parents=True)
    for name in ("main", "components", "managed_components", "tools", "patches", "configs"):
        shutil.copytree(source / name, destination / name, symlinks=True)
    for name in ("CMakeLists.txt", "partitions.csv", "dependencies.lock"):
        shutil.copy2(source / name, destination / name)
    (destination / "esp-idf").symlink_to(idf, target_is_directory=True)
    overlays = ["defconfig.esp32ethernetbrick", "defconfig.warp4-ble-bench"]
    if minimal:
        overlays.append("defconfig.esp32ethernetbrick.ble-minimal")
    previous = base_config
    for index, overlay in enumerate(overlays):
        output = destination / f"sdkconfig.stage{index}"
        subprocess.run(["python3", str(ROOT / "lib-builder/merge_defconfig.py"), str(previous),
                        str(ROOT / "lib-builder" / overlay), str(output)], check=True)
        previous = output
    shutil.copy2(previous, destination / "sdkconfig")
    inputs = [base_config] + [ROOT / "lib-builder" / n for n in overlays]
    inputs += [p for name in ("main", "components", "managed_components", "tools", "patches", "configs")
               for p in (source / name).rglob("*") if p.is_file() and ".git" not in p.parts]
    inputs += [source / n for n in ("CMakeLists.txt", "partitions.csv", "dependencies.lock")]
    provenance = {
        "scope": "BENCH ONLY; non-secure WARP4 layout on generic revision-3 ESP32 Ethernet Brick",
        "source_builder": str(source), "idf_path": str(idf), "minimal": minimal,
        "idf_upstream_commit": subprocess.check_output(["git", "-C", str(idf), "rev-parse", "HEAD"], text=True).strip(),
        "idf_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "-C", str(idf), "diff", "--binary"])).hexdigest(),
        "arduino_builder_commit": subprocess.check_output(["git", "-C", str(source / "components/arduino"), "rev-parse", "HEAD"], text=True).strip(),
        "input_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)},
    }
    (destination / "bench_sdk_inputs.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    for name in ("source", "idf", "base-config"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--finalize", action="store_true", help="Complete/verify a freshly built SDK export")
    parser.add_argument("--begin-build", action="store_true", help="Bind clean configured SDK inputs BEFORE compilation")
    parser.add_argument("--strip", default="xtensa-esp32-elf-strip")
    parser.add_argument("--minimal", action="store_true")
    args = parser.parse_args()
    if args.begin_build:
        identity.begin_sdk(args.destination)
    elif args.finalize:
        finalize(args.destination, args.strip)
    else:
        if not all((args.source, args.idf, args.base_config)):
            parser.error("preparation requires --source, --idf and --base-config")
        prepare(args.source, args.idf, args.base_config, args.destination, args.minimal)


def finalize(destination, strip):
    destination = confinement.lexical(destination)
    plan = confinement.sdk_output_plan(destination)
    provenance = json.loads((destination / "bench_sdk_inputs.json").read_text())
    source = Path(provenance["source_builder"]) / "out/tools/esp32-arduino-libs"
    sdk = destination / "out/tools/esp32-arduino-libs"
    # Keep the package schema/tool versions, not the old copied revision claims.
    package = json.loads((source / "package.json").read_text())
    package["version"] = "5.5.5+sha." + provenance["idf_upstream_commit"][:11]
    plan.revalidate()  # all copy/strip/version/identity destinations before write
    (sdk / "package.json").write_text(json.dumps(package, indent=2) + "\n")
    shutil.copy2(source / "tools.json", sdk / "tools.json")
    # The builder requests these freshly built renamed archives but omits them
    # from its copy list. Never replace them with empty or stock archives.
    for relative in ("esp-idf/mbedtls/mbedtls/library/libmbedtls_2.a", "esp-idf/esp_driver_sdm/libesp_driver_sdm_2.a"):
        archive = destination / "build" / relative
        target = sdk / "esp32/lib" / archive.name
        if archive.stat().st_size <= 8:
            raise ValueError(f"empty archive: {archive}")
        shutil.copy2(archive, target)
        subprocess.run([strip, "-g", str(target)], check=True)
    for flag in (sdk / "esp32/flags/ld_libs").read_text().split():
        if not flag.startswith("-l") or flag in ("-lm", "-lc", "-lgcc", "-lstdc++"):
            continue
        name = "lib" + flag[2:] + ".a"
        if not any((sdk / directory / name).is_file() for directory in ("esp32/lib", "esp32/ld", "esp32/qio_qspi")):
            raise ValueError(f"export missing required link archive: {name}")
    boot = destination / "build/bootloader/bootloader.bin"
    if (destination / "build/bootloader/bootloader.elf").read_bytes() != (sdk / "esp32/bin/bootloader_qio_80m.elf").read_bytes():
        raise ValueError("exported bootloader ELF differs from fresh build")
    # PlatformIO regenerates the binary from this ELF. Preserve IDF's binary as
    # an independent byte-for-byte reference for the post-merge validator.
    shutil.copy2(boot, sdk / "esp32/bin/bootloader_qio_80m.bin")
    (sdk / "versions.txt").write_text("BENCH ONLY, source/archive hashes: bench_sdk_provenance.json\n"
                                     + "esp-idf upstream: " + provenance["idf_upstream_commit"] + " + repository patches (git apply, no commits)\n"
                                     + "arduino builder: " + provenance["arduino_builder_commit"] + "\n")
    identity.seal_sdk(destination, provenance["minimal"])
    print(sdk)


if __name__ == "__main__":
    main()
