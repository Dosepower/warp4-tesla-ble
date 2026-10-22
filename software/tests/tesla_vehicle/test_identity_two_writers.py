"""Execute production snapshot with deterministic store/lock scheduling, then stress.
Hooks exist only in temporary copies; no replacement snapshot algorithm is tested.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

S = Path(__file__).resolve().parents[2]
MODE = sys.argv.pop(1) if len(sys.argv) > 1 and sys.argv[1] in ('sanitize', 'tsan') else 'normal'

class TwoWriters(unittest.TestCase):
    def test_failed_mutex_is_unavailable(self):
        module = S / 'src/modules/tesla_vehicle'
        platform = (module/'tesla_ble_clean_platform.h').read_text()
        platform = platform.replace('new(std::nothrow) std::mutex', 'nullptr')
        code = '''
#include <cassert>
#include "tesla_ble_result.h"
int main() {
    TeslaBleIdentitySnapshot snapshot;
    assert(!snapshot.synchronization_initialized());
    snapshot.publish({1,0,42});
    assert(!snapshot.read());
    TeslaBleSession session;
    assert(!session.synchronization_initialized());
    session.publish({1,0,42});
    assert(!session.identity());
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            p = Path(td)
            (p/'tesla_ble_result.h').write_text((module/'tesla_ble_result.h').read_text())
            (p/'tesla_ble_clean_platform.h').write_text(platform)
            (p/'test.cpp').write_text(code)
            subprocess.run(['g++', '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(S/'lib/TeslaBLEClean/include'), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
            subprocess.run([str(p/'test')], check=True)

    def test_publication_call_sites_are_task_only(self):
        from test_iram_permanent_allocation import function
        module = S/'src/modules/tesla_vehicle'
        source = (module/'tesla_vehicle.cpp').read_text()
        profiles = (module/'tesla_profiles.inc').read_text()
        # Exhaustive publication-site inventory: new call sites require review.
        self.assertEqual(source.count('session.publish('), 2)
        self.assertEqual(profiles.count('session.publish('), 1)
        self.assertEqual(source.count('refresh_live_session();'), 1)
        startup = function(source, 'bool TeslaVehicle::start_runtime(bool storage_only)')
        self.assertLess(startup.index('session.publish('), startup.index('xTaskCreatePinnedToCore(task_entry'))
        self.assertIn('session.publish(', function(source, 'void TeslaVehicle::loop()'))
        self.assertIn('refresh_live_session();', function(source, 'void TeslaVehicle::process_commands()'))
        self.assertIn('process_commands();', function(source, 'void TeslaVehicle::task_loop()'))
        self.assertIn('->task_loop();', function(source, 'void TeslaVehicle::task_entry(void *context)'))
        gatt = (module/'tesla_ble_gatt.cpp').read_text()
        body = function(gatt, 'void TeslaVehicle::gatt_callback(')
        self.assertNotIn('refresh_live_session', body)
        self.assertNotIn('process_commands', body)
        self.assertNotIn('session.publish', body)
        sites = []
        for path in module.iterdir():
            if path.suffix in ('.cpp', '.inc'):
                sites += [path.name] * path.read_text().count('session.publish(')
        self.assertCountEqual(sites, ['tesla_vehicle.cpp', 'tesla_vehicle.cpp', 'tesla_profiles.inc'])
        reader = function((module/'tesla_ble_result.h').read_text(),
                          '__attribute__((noinline)) std::optional<TeslaBleIdentity> read() const')
        self.assertIn('attempt < 3', reader)
        self.assertNotIn('Guard', reader)
        self.assertNotIn('while', reader)

    def test_interleaving_and_readers(self):
        module = S / 'src/modules/tesla_vehicle'
        header = (module / 'tesla_ble_result.h').read_text()
        point = 'generation_low.store(static_cast<uint32_t>(identity.generation));'
        self.assertEqual(header.count(point), 1)
        header = header.replace(point, point + '\n        after_low(identity.generation);')
        platform = (module / 'tesla_ble_clean_platform.h').read_text()
        point = 'handle_->lock(); return true;'
        self.assertEqual(platform.count(point), 1)
        platform = platform.replace(point, 'if (!handle_->try_lock()) { writer_blocked(); handle_->lock(); } return true;')
        code = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
static std::mutex schedule;
static std::condition_variable cv;
static bool paused=false, done=false, blocked=false;
static std::atomic<bool> orchestrate{true};
static thread_local bool writer_b=false;
static void writer_blocked() {
    if (!orchestrate.load() || !writer_b) return;
    std::lock_guard<std::mutex> lock(schedule); blocked=true; cv.notify_all();
}
static void after_low(uint64_t generation) {
    if (!orchestrate.load() || generation!=0x100000001ULL) return;
    std::unique_lock<std::mutex> lock(schedule); paused=true; cv.notify_all();
    cv.wait(lock, [] { return done || blocked; });
}
#include "tesla_ble_result.h"
int main() {
    const TeslaBleIdentity a{0x100000001ULL,0,0x112233445500ULL};
    const TeslaBleIdentity b{0x200000002ULL,1,0xaabbccddee11ULL};
    TeslaBleIdentitySnapshot snapshot;
    std::thread first([&] { snapshot.publish(a); });
    { std::unique_lock<std::mutex> lock(schedule); cv.wait(lock, [] { return paused; }); }
    // Reader during A's odd publication must fail closed, without waiting.
    assert(!snapshot.read());
    std::thread second([&] {
        writer_b=true; snapshot.publish(b);
        std::lock_guard<std::mutex> lock(schedule); done=true; cv.notify_all();
    });
    first.join(); second.join();
    const auto got=snapshot.read();
    if (got) std::printf("accepted generation=0x%llx index=%d mac=0x%llx; B blocked=%d\n",
        (unsigned long long)got->generation, got->index, (unsigned long long)got->mac, blocked);
    if (!got || (*got!=a && *got!=b)) {
        std::puts("FAIL: accepted identity was never published by A or B"); return 1;
    }
    assert(blocked); // Serialization, not suppression of either writer.
    assert(*got==b);
    orchestrate.store(false);
    snapshot.publish(a);
    std::atomic<bool> start{false};
    std::atomic<int> finished{0};
    std::atomic<unsigned> accepted{0}, unavailable{0};
    std::vector<std::thread> threads;
    for (int w=0; w<2; ++w) threads.emplace_back([&,w] {
        while (!start.load()) std::this_thread::yield();
        for (int i=0;i<100000;++i) snapshot.publish(w ? b : a);
        ++finished;
    });
    for (int r=0;r<4;++r) threads.emplace_back([&] {
        while (!start.load()) std::this_thread::yield();
        do {
            const auto value=snapshot.read();
            if (value) { assert(*value==a || *value==b); ++accepted; }
            else ++unavailable;
        } while (finished.load()!=2);
    });
    start.store(true);
    for (auto &t:threads) t.join();
    assert(accepted.load()>0);
    std::printf("PASS: deterministic A-low/B-attempt/A-finish/B-finish; 2 writers x 100000, 4 readers; accepted=%u unavailable=%u\n", accepted.load(), unavailable.load());
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            p = Path(td)
            (p/'tesla_ble_result.h').write_text(header)
            (p/'tesla_ble_clean_platform.h').write_text(platform)
            (p/'test.cpp').write_text(code)
            flags = ['-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
            if MODE == 'sanitize': flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
            if MODE == 'tsan': flags += ['-fsanitize=thread', '-fno-omit-frame-pointer', '-no-pie']
            freertos = p/'freertos'
            freertos.mkdir()
            mocks = S/'tests/tesla_vehicle/cleanroom/target_sync/freertos'
            critical = (mocks/'FreeRTOS.h').read_text()
            self.assertEqual(critical.count('mux->mutex.lock();'), 1)
            critical = critical.replace('mux->mutex.lock();',
                'if (!mux->mutex.try_lock()) { writer_blocked(); mux->mutex.lock(); }')
            (freertos/'FreeRTOS.h').write_text(critical)
            (freertos/'semphr.h').write_text((mocks/'semphr.h').read_text())
            for backend in ('native', 'esp-critical-model'):
                with self.subTest(backend=backend):
                    target = ['-DESP_PLATFORM', '-I'+str(p)] if backend != 'native' else []
                    subprocess.run(['g++', *flags, *target, '-I'+str(S/'lib/TeslaBLEClean/include'), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
                    print('BACKEND:', backend, 'MODE:', MODE, flush=True)
                    subprocess.run([str(p/'test')], check=True, timeout=120)

if __name__ == '__main__': unittest.main()
