#!/usr/bin/env python3
"""Exact Tesla validator/commands with actual WString and checked Config bodies.
Config tree/registration/RTOS are bounded native boundaries, not full ConfigRoot.
The second build models the target's 14-character SSO on a 64-bit native ABI.
Only scratch copies are adapted; framework sources are never written.
"""
from pathlib import Path
import hashlib
import json
import os
import runpy
import subprocess

repo = Path(__file__).resolve().parents[4]
base = runpy.run_path(str(Path(__file__).with_name('build_target_oom_harness.py')))
definition = base['definition']
out = base['out']
core = base['core']
module = repo / 'software/src/modules/tesla_vehicle/tesla_vehicle.cpp'
source = module.read_text()
header = r'''
#define Config StringConfig
#include "target_oom_definitions.inc"
#undef Config
#include <array>
#include <atomic>
#include <cctype>
#include <functional>
#include <memory>
#include <cstring>
#include "tesla_ble_validation.h"
#include "tesla_ble_config_policy.h"
#include "tesla_ble_command_outcome.h"
inline size_t strlcpy(char *to, const char *from, size_t size) {
    size_t n = std::strlen(from);
    if (size) { std::memcpy(to, from, std::min(n, size-1)); to[std::min(n, size-1)] = 0; }
    return n;
}
enum class ConfigSource {File, API};
using Language = int;
struct Config {
    StringConfig string;
    bool boolean = false;
    int integer = 0;
    Config *leaves = nullptr;
    Config *get(const char *name) {
        if (!std::strcmp(name,"pairing_pending")) {assert(leaves); return leaves;}
        if (!std::strcmp(name,"pair_request_sent")) {assert(leaves); return leaves+1;}
        const char *keys[] = {"enable", "vin", "ble_address", "ev_index", "poll_interval", "wake_once"};
        for (unsigned n=0; n<6; ++n) { if (!std::strcmp(name, keys[n])) { assert(leaves); return leaves+n; } }
        std::abort();
    }
    const String &asString() const {return string.slot.text;}
    bool updateStringChecked(const char *s) {return string.updateStringChecked(s);}
    bool asBool() const {return boolean;}
    int asInt() const {return integer;}
    unsigned asUint() const {return integer;}
    void updateBool(bool v) {boolean=v;}
};
struct Runtime {
    std::atomic<bool> pair_requested{false}, poll_requested{false}, forget_requested{false};
    TeslaBleSession session;
    TeslaBleCommandOutcome command_outcome;
    int configured_ev_index=2;
    Runtime() {session.publish({1,2,1});}
};
struct RuntimeOwner {Runtime *pointer=nullptr; Runtime *get() const {return pointer;}};
struct HostAPI {
    std::array<std::function<void(Language, String &)>,3> commands;
    unsigned count=0;
    void addPersistentConfig(const char *,Config *,std::initializer_list<int>,std::initializer_list<const char *>) {}
    void addState(const char *,Config *) {}
    template<class F> void addCommand(const char *,Config *,std::initializer_list<int>,F f,bool) {commands.at(count++)=std::move(f);}
} api;
struct TeslaVehicle {
    std::array<Config,6> live;
    std::array<Config,2> state_leaves;
    Config config, state, empty_command;
    RuntimeOwner runtime_owner;
    std::atomic<bool> runtime_update_requested{false};
    bool accepting=false, start_result=false;
    TeslaVehicle() {config.leaves=live.data();state.leaves=state_leaves.data();}
    bool accepts_commands() const {return accepting;}
    bool start_runtime(bool) {return start_result;}
    void register_urls();
    String validate(Config &update, ConfigSource source);
};
'''
header += definition(source, 'static String checked_error_string') + '\n'
validator = definition(source, '[this](Config &update, ConfigSource source) -> String')
header += 'String TeslaVehicle::validate(Config &update, ConfigSource source) ' + validator[validator.index('{'):] + '\n'
header += definition(source, 'void TeslaVehicle::register_urls()') + '\n'
(out / 'module_validation_definitions.inc').write_text(header)
inputs = [module, core / 'WString.cpp', core / 'WString.h', repo / 'software/src/config.cpp', repo / 'software/src/config.h']
(out / 'module-validation-hashes.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, indent=2)+'\n')
for target_layout in (False, True):
    variant = out / ('target-sso' if target_layout else 'native-sso')
    variant.mkdir(exist_ok=True)
    # The only layout adaptation is SSO capacity: target _ptr is three 32-bit
    # fields, so SSOSIZE = 12 + 4 - 1 = 15, capacity excluding NUL is 14.
    text = (core / 'WString.h').read_text()
    if target_layout:
        assert text.count('SSOSIZE = sizeof(struct _ptr) + 4 - 1') == 1
        text = text.replace('SSOSIZE = sizeof(struct _ptr) + 4 - 1', 'SSOSIZE = 15')
    (variant / 'WString.h').write_text(text)
    binary = variant / 'test_tesla_ble_module_validation'
    probe=Path(os.environ.get('TESLA_CLEAN_VALIDATION_TEST',str(repo / 'software/tests/tesla_vehicle/test_tesla_ble_module_validation.cpp')))
    instrument=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie'] if os.environ.get('TESLA_CLEAN_HARNESS_SANITIZE')=='1' else []
    cmd = ['g++', *instrument, '-std=c++20', '-fno-exceptions', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections,--wrap=realloc', '-Wno-sign-compare', '-I'+str(variant), '-I'+str(out), '-I'+str(core), '-I'+str(repo / 'software/src'), '-I'+str(repo / 'software/lib/TeslaBLEClean/include'), '-I'+str(repo / 'software/src/modules/tesla_vehicle'), str(out / 'WString.cpp'), str(probe), '-o', str(binary)]
    if target_layout:
        cmd.insert(1, '-DTESLA_TEST_TARGET_SSO')
    subprocess.run(cmd, check=True)
    subprocess.run([str(binary)], check=True)
