#!/usr/bin/env python3
"""Report static ESP32 IRAM placement from GNU linker map files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any


_MEMORY_RE = re.compile(
    r"^iram0_0_seg\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+x?r?\s*$",
    re.MULTILINE,
)
_SECTION_PATTERNS = {
    name: re.compile(rf"^\.iram0\.{name}\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$", re.MULTILINE)
    for name in ("vectors", "text", "data")
}
_IRAM_END_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+_iram_end\s*=", re.MULTILINE)


def _required(pattern: re.Pattern[str], text: str, label: str) -> re.Match[str]:
    matches = list(pattern.finditer(text))
    if not matches:
        raise ValueError(f"missing {label} in linker map")
    if len(matches) != 1:
        raise ValueError(f"ambiguous {label} in linker map: found {len(matches)} records")
    return matches[0]


def parse_map(text: str) -> dict[str, int]:
    memory = _required(_MEMORY_RE, text, "iram0_0_seg")
    origin = int(memory.group(1), 16)
    size = int(memory.group(2), 16)

    report: dict[str, int] = {
        "segment_origin": origin,
        "segment_size": size,
    }
    static_end = int(_required(_IRAM_END_RE, text, "_iram_end").group(1), 16)
    segment_end = origin + size
    if static_end < origin or static_end > segment_end:
        raise ValueError("_iram_end lies outside iram0_0_seg")
    previous_end = origin
    for name, pattern in _SECTION_PATTERNS.items():
        match = _required(pattern, text, f"iram0.{name}")
        address, length = (int(value, 16) for value in match.groups())
        if address < previous_end or address + length > static_end:
            raise ValueError(f"iram0.{name} section overlaps or lies outside static IRAM span")
        previous_end = address + length
        report[f"{name}_size"] = length
    report["static_end"] = static_end
    report["static_used"] = static_end - origin
    report["static_tail"] = segment_end - static_end
    return report


def compare_reports(baseline: dict[str, int], candidate: dict[str, int]) -> dict[str, int]:
    if baseline["segment_origin"] != candidate["segment_origin"] or baseline["segment_size"] != candidate["segment_size"]:
        raise ValueError("cannot compare different IRAM memory layouts")
    return {
        "static_used_delta": candidate["static_used"] - baseline["static_used"],
        "static_tail_delta": candidate["static_tail"] - baseline["static_tail"],
        "vectors_size_delta": candidate["vectors_size"] - baseline["vectors_size"],
        "text_size_delta": candidate["text_size"] - baseline["text_size"],
        "data_size_delta": candidate["data_size"] - baseline["data_size"],
    }


def read_report(path: Path) -> dict[str, int]:
    return parse_map(path.read_text(encoding="utf-8", errors="replace"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path, help="candidate linker map")
    parser.add_argument("--baseline", type=Path, help="optional baseline linker map")
    args = parser.parse_args()

    try:
        result: dict[str, Any] = {
            "measurement": "static_linker_layout_not_runtime_heap",
            "map": str(args.map), "report": read_report(args.map),
        }
        if args.baseline is not None:
            baseline = read_report(args.baseline)
            result["baseline"] = {"map": str(args.baseline), "report": baseline}
            result["delta"] = compare_reports(baseline, result["report"])
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
