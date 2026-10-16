#!/usr/bin/env python3
"""Execute production migration code, including allocation-failure ownership.

No hardware. The host ABI has a larger Arena::BlockHeader; only its target-width
static_assert is exempted, not allocator logic. The target-request case explicitly
uses 8176 payload bytes and 164 32-byte records. API types/function are extracted
verbatim, with only their unrelated platform dependencies stubbed.
"""
import hashlib
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


class PermanentAllocationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="warp-permanent-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.build = Path(cls.temp.name)
        tools = ROOT / "software/src/tools"
        header = (tools / "arena.h").read_text()
        assertion = "    static_assert(sizeof(BlockHeader) == MAX_ALIGNMENT);"
        assert header.count(assertion) == 1
        (cls.build / "arena.h").write_text(header.replace(assertion, "    // Target-width assertion exempted for native ABI only."))
        for name in ("arena.cpp", "malloc.h"):
            (cls.build / name).write_bytes((tools / name).read_bytes())
        for name in ("event_log_prefix.h", "gcc_warnings.h"):
            (cls.build / name).write_text("// Unused native platform shim.\n")
        (cls.build / "main_dependencies.h").write_text(
            "struct ArenaLogger { void printfln(const char *, ...) {} };\n"
            "inline ArenaLogger arena_logger;\n#define logger arena_logger\n")
        api = ROOT / "software/src/modules/api"
        header = (api / "api.h").read_text()
        declarations = []
        for name in ("CommandCallback", "ResponseCallback"):
            line = next(line for line in header.splitlines() if line.startswith("typedef ") and line.endswith(name + ";"))
            declarations.append(line)
        for name in ("StateRegistration", "CommandRegistration", "ResponseRegistration"):
            declarations.append(function(header, "struct " + name) + ";")
        (cls.build / "production_registrations.inc").write_text("\n".join(declarations))
        body = function((api / "api.cpp").read_text(), "void API::register_events()")
        (cls.build / "production_register_events.inc").write_text(body)
        source = ROOT / "software/tests/tesla_vehicle/host/test_api_permanent_allocation.cpp"
        cls.exe = cls.build / "test"
        command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti", "-g", "-I" + str(cls.build)]
        if os.environ.get("TESLA_PERMANENT_SANITIZE") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
        command += [str(source), str(cls.build / "arena.cpp"), "-o", str(cls.exe)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        print("Production-source SHA256:", {name: hashlib.sha256((tools / name).read_bytes()).hexdigest()
                                            for name in ("arena.h", "arena.cpp", "malloc.h")}, flush=True)

    def run_case(self, mode):
        result = subprocess.run([str(self.exe), mode], text=True, capture_output=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        self.assertEqual(result.returncode, 0, f"{mode}: exit={result.returncode}\n{result.stdout}{result.stderr}")
        print(result.stdout, end="", flush=True)

    def test_null_destination_preserves_source_and_retry(self):
        self.run_case("template")

    def test_empty_vector_does_not_allocate(self):
        self.run_case("empty")

    def test_exact_164_command_5248_byte_arena_failure(self):
        self.run_case("target")

    def test_state_failure_keeps_owner_and_other_tables(self):
        self.run_case("states")

    def test_command_failure_keeps_all_164_callbacks(self):
        self.run_case("commands")

    def test_response_failure_keeps_owner_and_other_tables(self):
        self.run_case("responses")

    def test_success_releases_collector_unchanged(self):
        self.run_case("success")


if __name__ == "__main__":
    unittest.main()
