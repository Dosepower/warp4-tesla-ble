"""BENCH ONLY merge -> validate -> atomic isolated export; never OTA/latest."""
import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path
import sys

Import("env")

spec = importlib.util.spec_from_file_location("bench_image_report", Path(env.subst("$PROJECT_DIR")) / "tools/bench_image_report.py")
assert spec is not None and spec.loader is not None
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
import bench_review_export
import bench_confinement as confinement

project = Path(env.subst("$PROJECT_DIR"))
build = Path(env.subst("$BUILD_DIR"))
variants = {"warp4_tesla_ble_iram_bench": "ordinary",
            "warp4_tesla_ble_iram_bench_minimal": "minimal"}
variant = variants[env["PIOENV"]]
export, staging = bench_review_export.review_paths(project / ".pio/bench-exports" / variant)
# Revoke old success even if compilation fails before the post-action runs.
confinement.hook_cleanup(project, build, (export, staging))
if env.IsCleanTarget():
    Return()

# The custom bootloader is a finished IDF binary, not PlatformIO's default
# ELF conversion (which discards chip revision limits). The existing merge hook
# expects a build-local bootloader.bin; use that exact same input in both paths.
bootloader = Path(env.GetProjectOption("board_build.arduino.custom_bootloader"))
build_bootloader = Path(env.subst("$BUILD_DIR")) / "bootloader.bin"
confinement.output_plan(roots=(build,), files=tuple(build / name for name in (
    "bootloader.bin", "firmware_esptool.bin", "bench_build_inputs.unchecked.json"))).revalidate()
if not build_bootloader.exists() or build_bootloader.read_bytes() != bootloader.read_bytes():
    shutil.copy2(bootloader, build_bootloader)

sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
builder = Path(env.GetProjectOption("custom_bench_builder"))
inputs = report.identity.capture_firmware(env, build, sdk, builder, project)
# Absolute linker symbol affects ELF SHA256, which esptool inserts into the
# application descriptor. Thus the BIN binds these PRE-compilation inputs too.
env.Append(LINKFLAGS=["-Wl,--defsym=bench_inputs_" + report.identity.digest(inputs) + "=0"])


def validate_bench(source, target, env):
    if env.GetProjectOption("custom_secure_boot", "").strip() or env.GetProjectOption("custom_provisioning") != "false":
        raise ValueError("BENCH ONLY: secure/provisioning build refused")
    metadata = json.loads((Path(env.subst("$BUILD_DIR")) / "metadata.json").read_text())
    if metadata["signature_preset"] or metadata["secure_boot"] or not metadata["split_esptool_ota"]:
        raise ValueError("BENCH ONLY: non-secure split image required")
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    # No production merge hook, firmware-info, OTA, signing, shared copy or link.
    subprocess.check_call([
        "uv", "run", "pio", "pkg", "exec", "-p", "tool-esptoolpy", "--",
        "esptool", "--chip", "esp32", "merge-bin", "--target-offset", "0x1000",
        "-o", str(build / "firmware_esptool.bin"),
        "0x1000", str(build / "bootloader.bin"), "0xe000", str(build / "partitions.bin"),
        "0x10000", str(build / "firmware.bin"), "0x640000", str(project / "boot_app0.bin")])
    result = report.validate(Path(env.subst("$BUILD_DIR")), sdk,
                             Path(env.GetProjectOption("custom_bench_builder")),
                             Path(env.subst("$PROJECT_DIR")) / "boot_app0.bin")
    report.identity.verify_live_inputs(inputs)
    result = bench_review_export.export_review(build, sdk, builder, project / "boot_app0.bin", export)
    print(f"NON-INSTALLABLE BENCH REVIEW bytes verified: {result['image']['sha256']}; not boot/flash approval")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", validate_bench)
