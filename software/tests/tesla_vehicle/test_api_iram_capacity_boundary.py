#!/usr/bin/env python3
"""Capacity-boundary regression for the boot-time API registration tables (r10 boot panic).

Compiles the UNMODIFIED production sources tools/arena.{h,cpp}, tools/malloc.h
(make_permanent), the API registration structs from modules/api/api.h and the body of
API::register_events() from modules/api/api.cpp for a 32-bit ILP32 host (-m32), so that
sizeof(StateRegistration)=20, sizeof(CommandRegistration)=32 and Arena::BlockHeader=16
match the Xtensa target. The iram_arena is constructed exactly like tools/malloc.cpp
(min_align 4, first block 8192, block 1024) and adopts an 8192-byte "manual" region.
heap_caps_aligned_alloc(MALLOC_CAP_IRAM) is a double whose largest free block is
controlled by the test (models the shared runtime IRAM heap after Config slot use).

Cases (each run in a child process so a NULL store is observed as a signal):
  fit_in_arena   : Ns/Nc small enough for the 8176-byte arena -> must succeed even with 0 heap.
  heap_backed    : WARP4 r10 census counts, IRAM heap block >= table bytes -> succeeds.
  r10_exhausted  : WARP4 r10 census counts, IRAM heap block < table bytes -> production
                   make_permanent stores through nullptr (target: StoreProhibited).
The production gate (test_production_counts_fit_guaranteed_iram) asserts that the r10
registration census fits the only *guaranteed* IRAM capacity (the 8176-byte arena). It is
RED for r10: commands then depend on an unguaranteed IRAM-heap block.
"""
import json
import os
import shutil
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / "software/src/tools"
API = ROOT / "software/src/modules/api"

# Static census of the r10 build (scripts/bootfix_api_registration_census.py, loops resolved:
# meters 5 slots x (1 persistent config + 4 states), other loop hits are callbacks).
R10_STATES = 170 + 4 * 5
R10_COMMANDS_CENSUS = 161 + 4 * 1
# Measured on the device: official r10 coredump (ELF sha256 9962bf03...), faulting frame of
# API::register_events()+126: a4 = 0xad commands, a11 = 0x15a0 = 5536 B, a7 (dst) = 0, EXCVADDR 0.
# The static census under-counts by 8; the measured value is authoritative.
R10_COMMANDS = 173
STOCK_COMMANDS = 153
ARENA_USABLE = 8192 - 16


def extract(text, signature):
    start = text.index(signature)
    i = text.index("{", start)
    depth, j = 1, i + 1
    while depth:
        depth += (text[j] == "{") - (text[j] == "}")
        j += 1
    return text[start:j]


HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>
#include <span>
enum class Language { German, English };
struct String { char *p = nullptr; };
struct ConfigRoot {};
static size_t iram_largest_block = 0;   // controlled IRAM-heap capacity
static size_t iram_heap_requests = 0, iram_heap_last = 0;
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_IRAM 4
#define MALLOC_CAP_SPIRAM 8
extern "C" void *heap_caps_aligned_alloc(size_t a, size_t s, uint32_t caps) {
    if (caps == MALLOC_CAP_IRAM) { ++iram_heap_requests; iram_heap_last = s; if (s > iram_largest_block) return nullptr; }
    void *r = nullptr; if (posix_memalign(&r, a < sizeof(void*) ? sizeof(void*) : a, s)) return nullptr; return r;
}
#include "malloc.h"
#include "registrations.inc"
alignas(16) static uint8_t manual_iram[8192];
Arena iram_arena{[](size_t a, size_t s){return heap_caps_aligned_alloc(a, s, MALLOC_CAP_IRAM);}, 4, 8192, 1024};
Arena dram_arena{[](size_t a, size_t s){return heap_caps_aligned_alloc(a, s, MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);}, 1, 8192, 1024};
Arena psram_arena{[](size_t a, size_t s){return heap_caps_aligned_alloc(a, s, MALLOC_CAP_SPIRAM);}, 1, 8192, 8192};
void *perm_aligned_alloc(size_t al, size_t s, RAM r) {
    switch (r) { case DRAM: return dram_arena.aligned_alloc(al, s); case IRAM: return iram_arena.aligned_alloc(al, s);
                 case PSRAM: return psram_arena.aligned_alloc(al, s); default: return nullptr; }
}
struct Collector { std::vector<StateRegistration> states; std::vector<CommandRegistration> commands; std::vector<ResponseRegistration> responses; };
struct API { std::span<StateRegistration> states; std::span<CommandRegistration> commands; std::span<ResponseRegistration> responses;
             Collector *reg_collector = new Collector; void register_events(); };
#include "register_events.inc"
static_assert(sizeof(StateRegistration) == 20, "target width");
static_assert(sizeof(CommandRegistration) == 32, "target width");
static_assert(sizeof(Arena::BlockHeader) == 16, "target width");
int main(int argc, char **argv) {
    size_t ns = strtoul(argv[1], 0, 10), nc = strtoul(argv[2], 0, 10); iram_largest_block = strtoul(argv[3], 0, 10);
    iram_arena.setup({{manual_iram, sizeof(manual_iram)}}); dram_arena.setup({}); psram_arena.setup({});
    API api; static const char *p = "x"; static ConfigRoot c; size_t calls = 0;
    for (size_t i = 0; i < ns; ++i) api.reg_collector->states.push_back(StateRegistration{p, nullptr, nullptr, &c, 0});
    for (size_t i = 0; i < nc; ++i) api.reg_collector->commands.push_back(CommandRegistration{p, nullptr, &c, [&calls](Language, String &){ ++calls; }, 0});
    printf("{\"stage\":\"before\",\"states_bytes\":%zu,\"commands_bytes\":%zu}\n", ns * sizeof(StateRegistration), nc * sizeof(CommandRegistration)); fflush(stdout);
    api.register_events();
    String e; for (auto &cmd : api.commands) cmd.callback(Language::German, e);
    printf("{\"stage\":\"after\",\"commands\":%zu,\"calls\":%zu,\"iram_heap_requests\":%zu,\"iram_heap_last\":%zu,\"cmd_in_arena\":%d}\n",
           api.commands.size(), calls, iram_heap_requests, iram_heap_last,
           (int)((uint8_t*)api.commands.data() >= manual_iram && (uint8_t*)api.commands.data() < manual_iram + sizeof(manual_iram)));
    return calls == nc ? 0 : 3;
}
"""


class ApiIramCapacityBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cxx = shutil.which("g++")
        if cxx is None:
            raise unittest.SkipTest("g++ missing")
        cls.tmp = tempfile.TemporaryDirectory(prefix="warp-iram-capacity-")
        cls.addClassCleanup(cls.tmp.cleanup)
        b = Path(cls.tmp.name)
        for name in ("arena.h", "arena.cpp", "malloc.h"):
            shutil.copyfile(TOOLS / name, b / name)  # verbatim production sources
        for name in ("event_log_prefix.h", "gcc_warnings.h"):
            (b / name).write_text("\n")
        (b / "main_dependencies.h").write_text(
            "struct ArenaLogger { void printfln(const char *, ...) {} };\n"
            "inline ArenaLogger arena_logger;\n#define logger arena_logger\n")
        header = (API / "api.h").read_text()
        decl = [next(l for l in header.splitlines() if l.startswith("typedef ") and l.endswith(n + ";"))
                for n in ("CommandCallback", "ResponseCallback")]
        decl = [d for d in decl if "CommandCallback" in d] + [
            "struct IChunkedResponse; struct Ownership;", decl[1]]
        for n in ("StateRegistration", "CommandRegistration", "ResponseRegistration"):
            decl.append(extract(header, "struct " + n + " {") + ";")
        (b / "registrations.inc").write_text("\n".join(decl) + "\n")
        (b / "register_events.inc").write_text(extract((API / "api.cpp").read_text(), "void API::register_events()") + "\n")
        (b / "harness.cpp").write_text(HARNESS)
        cls.binaries = {}
        for mode, flags in (("normal", []), ("asan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
            out = b / ("harness_" + mode)
            r = subprocess.run([cxx, "-m32", "-std=gnu++20", "-O1", "-g", *flags, "-I", str(b), str(b / "harness.cpp"),
                                str(b / "arena.cpp"), "-o", str(out)], capture_output=True, text=True)
            if r.returncode != 0 and mode == "asan":
                cls.binaries[mode] = None  # ASan 32-bit runtime may be absent; reported, not hidden
                cls.asan_error = r.stderr[-800:]
                continue
            assert r.returncode == 0, r.stderr[-3000:]
            cls.binaries[mode] = out

    def run_case(self, ns, nc, iram_block, mode="normal"):
        exe = self.binaries[mode]
        if exe is None:
            self.skipTest("32-bit ASan unavailable: " + getattr(self, "asan_error", ""))
        env = dict(os.environ, ASAN_OPTIONS="handle_segv=1:detect_leaks=0")
        r = subprocess.run([str(exe), str(ns), str(nc), str(iram_block)], capture_output=True, text=True, env=env, timeout=60)
        lines = [json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]
        return r, lines

    def test_fit_in_arena_needs_no_heap(self):
        r, lines = self.run_case(100, 100, 0)  # 2000 + 3200 <= 8176
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(lines[-1]["cmd_in_arena"], 1)
        self.assertEqual(lines[-1]["iram_heap_requests"], 0)

    def test_r10_counts_succeed_only_with_heap_block(self):
        r, lines = self.run_case(R10_STATES, R10_COMMANDS, R10_COMMANDS * 32)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(lines[-1]["calls"], R10_COMMANDS)
        self.assertEqual(lines[-1]["cmd_in_arena"], 0)
        self.assertEqual(lines[-1]["iram_heap_last"], R10_COMMANDS * 32)

    def test_r10_mechanism_null_store_when_iram_heap_block_too_small(self):
        for mode in ("normal", "asan"):
            with self.subTest(mode=mode):
                r, lines = self.run_case(R10_STATES, R10_COMMANDS, R10_COMMANDS * 32 - 4, mode)
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual(lines[-1]["stage"], "before")  # crashed inside register_events
                if mode == "normal":
                    self.assertEqual(r.returncode, -signal.SIGSEGV)
                else:
                    self.assertIn("SEGV on unknown address 0x00000000", r.stderr)

    def test_measured_r10_request_matches_coredump(self):
        r, lines = self.run_case(R10_STATES, R10_COMMANDS, R10_COMMANDS * 32)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(lines[-1]["iram_heap_last"], 0x15A0)  # a11 in the coredump

    def test_stock_counts_also_depend_on_iram_heap(self):
        """The arena-only gate was wrong: stock 2.13.4 (153 commands) also needs a heap block.

        So "does not fit the 8176 B arena" is NOT a defect by itself; the boot acceptance
        criterion is the largest free MALLOC_CAP_IRAM block at setup():301 vs. the request.
        """
        r, lines = self.run_case(R10_STATES, STOCK_COMMANDS, STOCK_COMMANDS * 32)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(lines[-1]["cmd_in_arena"], 0)
        self.assertEqual(lines[-1]["iram_heap_last"], STOCK_COMMANDS * 32)
        r, lines = self.run_case(R10_STATES, STOCK_COMMANDS, STOCK_COMMANDS * 32 - 4)
        self.assertEqual(r.returncode, -signal.SIGSEGV)  # stock is equally unguarded


if __name__ == "__main__":
    unittest.main()
