#pragma once
#include "module_platform.h"
#include "../../../src/modules/tesla_vehicle/bluetooth_lifecycle.h"
// Run the real SDK lifecycle worker independently, not a forged active flag.
// Only FreeRTOS scheduling/delay/park is adapted to std::thread here.
class HostBluetoothDriver {
public:
    explicit HostBluetoothDriver(BluetoothLifecycle &owner, BluetoothLifecycle::Handler handler) : owner(owner) {
        host_status = controller_status = 0; host_gatt_success = true; async_bluetooth = true;
        assert(owner.start(handler));
        auto *task = tasks.back().get();
        worker = std::thread([task] { run_task(task); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!owner.active() && !owner.failed() && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
        assert(owner.active());
    }
    ~HostBluetoothDriver() {
        owner.request_stop(); worker.join(); owner.supervise(); async_bluetooth = false;
    }
private:
    BluetoothLifecycle &owner;
    std::thread worker;
};
