#!/usr/bin/env python3
"""Run the real ESP32 heap-region subtraction source on actual ELF tables.

Native-only check: logging/config headers are shimmed, not heap logic. Host
struct pointer widths differ; every target endpoint/type/stack flag comes from
ELF32 bytes. This does not run heap_caps_init, allocate target RAM or prove boot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import struct
import subprocess
import sys
import tempfile

from bench_image_report import Elf


def check(elf_path, idf):
    elf = Elf(elf_path)
    low = elf.symbols["soc_reserved_memory_region_start"][0]
    high = elf.symbols["soc_reserved_memory_region_end"][0]
    if high <= low or (high - low) % 8:
        raise ValueError("invalid reservation table extent")
    reserved = list(struct.iter_unpack("<II", elf.at(low, high - low)))
    address, size, *_ = elf.symbols["soc_memory_regions"]
    count_address = elf.symbols["soc_memory_region_count"][0]
    count = struct.unpack("<I", elf.at(count_address, 4))[0]
    if not 0 < count <= 256 or size != count * 20:
        raise ValueError("invalid ESP32 soc_memory_region_t table")
    regions = list(struct.iter_unpack("<IIIIB3x", elf.at(address, size)))
    idf = Path(idf).resolve()
    source = idf / "components/heap/port/memory_layout_utils.c"
    # Use the actual target ROM caps header (ESP32 does not define HAS_LAYOUT_TABLE).
    caps = idf / "components/esp_rom/esp32"
    with tempfile.TemporaryDirectory(prefix="warp-heap-host-") as temp:
        root = Path(temp)
        (root / "sdkconfig.h").write_text("/* native ESP32 heap-table check only */\n")
        # The compatibility header includes inline target address classifiers;
        # this SDK source does not use them. Keep the actual layout types.
        (root / "esp_memory_utils.h").write_text("/* unused target inline helpers */\n")
        (root / "esp_log.h").write_text("\n".join(
            f"#define ESP_EARLY_LOG{level}(...) ((void)0)" for level in "VDE") + "\n")
        program = '#include <stdio.h>\n#include <assert.h>\n#include "heap_memory_layout.h"\n'
        program += 'soc_reserved_region_t native_reserved[] = {\n'
        program += ",\n".join(f"{{{start:#x}, {end:#x}}}" for start, end in reserved) + "\n};\n"
        program += '__asm__(".global soc_reserved_memory_region_start\\n"\n'
        program += '        ".set soc_reserved_memory_region_start, native_reserved\\n"\n'
        program += '        ".global soc_reserved_memory_region_end\\n"\n'
        # C constant expression emitted by extended asm is unavailable at file scope;
        # host size is two intptr_t values, checked in main before calling SDK source.
        host_record_size = struct.calcsize("P") * 2
        program += f'        ".set soc_reserved_memory_region_end, native_reserved + {len(reserved) * host_record_size}\\n");\n'
        program += 'const soc_memory_region_t soc_memory_regions[] = {\n'
        program += ",\n".join(f"{{{start:#x}, {length:#x}, {kind}, {alias:#x}, {bool(stack).__str__().lower()}}}"
                               for start, length, kind, alias, stack in regions) + "\n};\n"
        program += f'const size_t soc_memory_region_count = {count};\n'
        program += f'#include {json.dumps(str(source))}\n'
        program += 'int main(void) {\n'
        program += f'    assert(sizeof(soc_reserved_region_t) == {host_record_size});\n'
        program += '    soc_memory_region_t available[soc_get_available_memory_region_max_count()];\n'
        program += '    size_t n = soc_get_available_memory_regions(available);\n'
        program += '    printf("[");\n'
        program += '    for (size_t i = 0; i < n; ++i) {\n'
        program += '        printf("%s{\\"start\\":%lu,\\"size\\":%lu,\\"type\\":%lu,\\"startup_stack\\":%s}",\n'
        program += '               i ? "," : "", (unsigned long)available[i].start, (unsigned long)available[i].size,\n'
        program += '               (unsigned long)available[i].type, available[i].startup_stack ? "true" : "false");\n'
        program += '    }\n    puts("]");\n    return 0;\n}\n'
        harness = root / "host.c"
        harness.write_text(program)
        executable = root / "host"
        subprocess.run(["gcc", "-std=gnu17", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                        "-I" + str(root), "-I" + str(idf / "components/heap/include"), "-I" + str(caps),
                        str(harness), "-o", str(executable)], check=True)
        print(f"ELF {elf_path}; {len(reserved)} reserved, {count} SOC regions; SDK source {source}", file=sys.stderr, flush=True)
        result = subprocess.run([str(executable)], text=True, capture_output=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            print(f"Native SDK heap region check failed: returncode={result.returncode}", file=sys.stderr)
            return 1
        available = json.loads(result.stdout)
        print(json.dumps({"validation": "native_sdk_region_subtraction_not_target_heap_init_or_hardware_boot",
                          "elf_sha256": hashlib.sha256(elf.data).hexdigest(),
                          "sdk_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                          "reserved_count": len(reserved), "soc_region_count": count,
                          "available_count": len(available), "available_regions": available}, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--idf", type=Path, required=True)
    args = parser.parse_args()
    try:
        return check(args.elf, args.idf)
    except (ValueError, OSError, KeyError, struct.error, subprocess.CalledProcessError) as exc:
        print(f"native heap check setup failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
