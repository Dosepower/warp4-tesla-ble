#pragma once
// Only OS/config/SDK boundaries are substituted. TeslaVehicle and its Runtime,
// worker, main, queues ownership, adapters and protocol remain production code.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <csetjmp>
#include <thread>
#include <chrono>
#include <LittleFS.h>
inline size_t strlcpy(char *to, const char *from, size_t size) {
    const size_t length = std::strlen(from);
    if (size) { std::memcpy(to, from, std::min(length, size - 1)); to[std::min(length, size - 1)] = 0; }
    return length;
}
enum class ConfigSource { File, API };
using Language = int;
inline bool fail_checked_string = false;
inline bool host_restore_service_enabled = false;
inline unsigned host_restore_calls = 0;
inline unsigned host_config_objects = 0;
class Config {
public:
    Config() = default;
    template <typename F> Config(Config other, F) : children(std::move(other.children)) {}
    static Config Object(std::initializer_list<std::pair<std::string, Config>> values) {
        ++host_config_objects;
        Config c; for (const auto &v : values) { c.children[v.first] = std::make_shared<Config>(v.second); } return c;
    }
    static Config Bool(bool v) { Config c; c.boolean = v; return c; }
    static Config Int(int v, int, int) { Config c; c.integer = v; return c; }
    static Config Uint(uint32_t v, uint32_t, uint32_t) { Config c; c.integer = v; return c; }
    void updateUint(uint32_t value) { integer = value; }
    static Config Str(const char *v, int, int) { Config c; c.text = v; return c; }
    static Config Float(float v) { Config c; c.floating = v; return c; }
    static Config Uptime() { return {}; }
    static Config Array(std::initializer_list<Config>,const Config *prototype,unsigned,unsigned max) {
        Config c;c.prototype=prototype;c.array_max=max;return c;
    }
    Config *get(size_t index) { return &elements.at(index); }
    bool removeAll() { elements.clear();return true; }
    bool setCount(size_t count) {
        if(!prototype || count>array_max) { return false; }
        elements.clear();for(size_t i=0;i<count;++i) { elements.push_back(prototype->clone()); }return true;
    }
    size_t count() const { return elements.size(); }
    Config *get(const char *key) { return children.at(key).get(); }
    bool has(const char *key) const { return children.count(key) != 0; } // test-only presence probe
    const Config *get(const char *key) const { return children.at(key).get(); }
    bool asBool() const { return boolean; }
    int asInt() const { return integer; }
    uint32_t asUint() const { return integer; }
    const String &asString() const { return text; }
    void updateBool(bool v) { boolean = v; }
    void updateString(const String &v) {
#ifdef TESLA_BLE_ALLOCATION_PROBE
        AllocationProbe::PlatformBoundary boundary;
#endif
        text = v;
    }
    bool updateStringChecked(const char *v) {
        if (fail_checked_string) { return false; }
#ifdef TESLA_BLE_ALLOCATION_PROBE
        AllocationProbe::PlatformBoundary boundary;
#endif
        text = v; return true;
    }
    void updateFloat(float v) { floating = v; }
    void updateUptime(uint64_t) {}
    void updateInt(int v) { integer = v; }
private:
    std::map<std::string, std::shared_ptr<Config>> children;
    bool boolean = false;
    int integer = 0;
    float floating = NAN;
    String text;
    const Config *prototype=nullptr;
    unsigned array_max=0;
    std::vector<Config> elements;
    friend struct HostAPI;
    Config clone() const {
        Config c=*this;c.children.clear();for(const auto &item:children) { c.children[item.first]=std::make_shared<Config>(item.second->clone()); }return c;
    }
};
using ConfigRoot = Config;
struct IModule {
    virtual ~IModule() = default;
    virtual void pre_setup() {} virtual void setup() {} virtual void register_urls() {} virtual void loop() {}
    bool initialized = false;
};
constexpr int EV_MAX_EVS = 16;
enum class EVDataSource { Tesla };
// Charging is outside the BLE module's capabilities. Tripwires cover calls at
// the host boundary; missing APIs also fail compilation rather than disappear.
inline unsigned host_charge_calls = 0;
inline void assert_charging_untouched() { assert(host_charge_calls == 0); }
struct HostChargingBoundary {
    template<class... A> void notify_auth(A&&...) { ++host_charge_calls; }
    template<class... A> void start_charging(A&&...) { ++host_charge_calls; }
    template<class... A> void set_charging_current(A&&...) { ++host_charge_calls; }
};
inline HostChargingBoundary charge_authorization, evse, evse_v2;
// Optional deterministic interleaving probe (card-confirm contention test only).
inline std::function<void()> host_profile_mac_probe;
struct HostEV {
    template<class... A> void on_ev_connected(A&&...) { ++host_charge_calls; }
    template<class... A> void start_charging(A&&...) { ++host_charge_calls; }
    uint64_t generation = 1; int index = 2; uint64_t mac = 0x112233445566;
    uint64_t get_session_generation() const { return generation; }
    bool session_exhausted() const { return exhausted; }
    bool exhausted=false;
    int get_active_ev_index() const { return index; }
    uint64_t get_active_ev_mac() const { return mac; }
    struct LiveIdentity { uint64_t generation; int index; uint64_t mac; };
    LiveIdentity get_live_identity() const { return {generation,index,mac}; }
    std::map<int,uint64_t> profiles{{0,0x112233445500},{1,0x112233445501},{2,0x112233445566}};
    uint64_t get_profile_mac(int i) const { if (host_profile_mac_probe) { host_profile_mac_probe(); } auto p=profiles.find(i);return p==profiles.end()?0:p->second; }
    struct SocPublication { int index; uint64_t mac; float soc; };
    std::vector<SocPublication> publications;
    unsigned tesla_source_updates=0;
    void set_soc(float value) { ++applied; publications.push_back({index,mac,value}); }
    void session_updated(EVDataSource source) { assert(source==EVDataSource::Tesla);++tesla_source_updates; }
    int applied = 0;
};
inline HostEV ev;
struct HostAPI {
    std::map<std::string, std::function<void(Language, String &)>> commands;
    std::map<std::string, ConfigRoot *> states;
    bool restorePersistentConfig(const char *, ConfigRoot *config) {
        ++host_restore_calls;
        if (!host_restore_service_enabled) { return false; }
        config->get("enable")->updateBool(true);
        config->get("vin")->updateString("5YJ3E1EA7KF000001");
        config->get("ble_address")->updateString("01:02:03:04:05:06");
        config->get("ev_index")->updateInt(2);
        return true;
    }
    std::map<std::string,Config> written_configs;
    void writeConfig(const char *name, ConfigRoot *config) { written_configs.insert_or_assign(name,config->clone()); }
    void removeConfig(const char *) {}
    void addPersistentConfig(const char *name, ConfigRoot *config, std::initializer_list<int>, std::initializer_list<const char *>) { states[name] = config; }
    void addState(const char *name, ConfigRoot *config) { states[name] = config; }
    void addState(const char *name, ConfigRoot *config,std::initializer_list<const char *>,std::initializer_list<const char *>) { states[name]=config; }
    template <typename F> void addCommand(const char *name, ConfigRoot *, std::initializer_list<const char *>, F f, bool) { commands[name] = f; }
};
inline HostAPI api;
inline unsigned reboot_calls = 0;
inline void trigger_reboot(const char *) { ++reboot_calls; }
struct HostQueue { size_t capacity, size; std::deque<std::vector<uint8_t>> data; std::mutex mutex; };
using QueueHandle_t = HostQueue *;
inline int queues_live = 0;
inline unsigned queue_creates=0,fail_queue_at=0;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t size) {
    if (++queue_creates==fail_queue_at) { return nullptr; }
    ++queues_live; return new HostQueue{capacity, size, {}, {}};
}
inline void vQueueDelete(QueueHandle_t q) { delete q; --queues_live; }
constexpr int pdPASS = 1, pdTRUE = 1;
inline int xQueueSend(QueueHandle_t q, const void *value, int wait) {
    assert(wait == 0); std::lock_guard<std::mutex> lock(q->mutex);
    if (q->data.size() == q->capacity) { return 0; }
    const auto *b = static_cast<const uint8_t *>(value); q->data.emplace_back(b, b + q->size); return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, void *value, int wait) {
    assert(wait == 0); std::lock_guard<std::mutex> lock(q->mutex);
    if (q->data.empty()) { return 0; } std::memcpy(value, q->data.front().data(), q->size); q->data.pop_front(); return pdTRUE;
}
inline void xQueueReset(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->mutex); q->data.clear(); }
inline size_t uxQueueSpacesAvailable(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->mutex); return q->capacity - q->data.size(); }
struct HostTask { void (*entry)(void *); void *context; std::atomic<bool> parked{false}, deleted{false}; std::string name; int stack_bytes; };
using TaskHandle_t = HostTask *;
inline std::vector<std::unique_ptr<HostTask>> tasks;
inline std::string fail_task;
inline unsigned task_launches = 0;
inline int host_created_priority=0;
inline std::function<void(TaskHandle_t)> host_task_created;
inline int xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, int stack_bytes, void *context, int priority, TaskHandle_t *out, int) {
    ++task_launches;
    if (fail_task == name) { return 0; }
    tasks.emplace_back(new HostTask{entry, context, false, false, name, stack_bytes}); *out = tasks.back().get();host_created_priority=priority; if(host_task_created)host_task_created(*out); return pdPASS;
}
// OS-only escape after the real production ACK/completion; no C++ objects
// are abandoned between this boundary and the task entry. Production stays
// compiled with -fno-exceptions, matching the Xtensa target.
inline thread_local std::jmp_buf parked_task;
inline thread_local HostTask *running_task = nullptr;
inline bool async_bluetooth = false;
inline std::atomic<unsigned> host_bt_yields{0}; // Test driver uses a real independently scheduled owned worker.
inline void vTaskSuspend(void *) { std::longjmp(parked_task, 1); }
inline void run_task(TaskHandle_t task) { running_task = task; if (setjmp(parked_task) == 0) { task->entry(task->context); } task->parked = true; running_task = nullptr; }
inline void vTaskDelete(TaskHandle_t task) { assert(task && (task->parked || (async_bluetooth && (task->name == "bluetooth" || task->name == "nimble_host"))) && !task->deleted); task->deleted = true; }
#if defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 3072; }
struct HostMasterSerial {
    std::string input;
    int available() { return static_cast<int>(input.size()); }
    int read() { const char c=input.front();input.erase(0,1);return c; }
};
inline HostMasterSerial Serial;
#endif
inline std::atomic<uint32_t> clock_ms{1};
inline uint32_t millis() { return clock_ms.load(); }
inline uint64_t now_us() { return uint64_t{millis()} * 1000; }
// Deterministic pair-trace interleaving (production macro is empty in firmware).
inline std::function<void(int)> host_pair_trace_point;
#define TESLA_PAIR_TRACE_POINT(point) do { if (host_pair_trace_point) { host_pair_trace_point(point); } } while (0)
inline std::function<void()> on_delay;
inline std::function<void(uint32_t)> on_task_delay;
inline std::atomic<void(*)()> host_bt_delay_hook{nullptr};
inline void vTaskDelay(uint32_t ms) {
    if (async_bluetooth && running_task && running_task->name == "bluetooth") {
        if (auto hook = host_bt_delay_hook.load()) { hook(); }
        host_bt_yields.fetch_add(1,std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::microseconds(50)); return;
    }
    clock_ms.fetch_add(ms); auto callback = on_delay; if (callback) { callback(); } if (on_task_delay) { on_task_delay(ms); }
    if (async_bluetooth) { std::this_thread::sleep_for(std::chrono::microseconds(50)); }
}
#define pdMS_TO_TICKS(ms) (ms)
using esp_gatt_if_t = uint8_t;
using esp_ble_addr_type_t = uint8_t;
constexpr int ESP_OK=0, ESP_GATT_OK=0, ESP_GATT_IF_NONE=255, ESP_UUID_LEN_16=2, ESP_UUID_LEN_128=16;
constexpr int ESP_GATT_CHAR_PROP_BIT_WRITE=0x08, ESP_GATT_CHAR_PROP_BIT_NOTIFY=0x10, ESP_GATT_CHAR_PROP_BIT_INDICATE=0x20;
inline int host_rx_properties=ESP_GATT_CHAR_PROP_BIT_NOTIFY;
inline unsigned host_cccd_writes=0, host_cccd_value=0;
inline std::function<void(uint16_t,uint16_t)> on_descriptor_write;
inline bool host_single_link_enforced=false;
inline unsigned host_physical_links=0;
constexpr int ESP_GATT_WRITE_TYPE_RSP=0, ESP_GATT_AUTH_REQ_NONE=0;
enum esp_gattc_cb_event_t { ESP_GATTC_REG_EVT, ESP_GATTC_OPEN_EVT, ESP_GATTC_DIS_SRVC_CMPL_EVT,
    ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_WRITE_DESCR_EVT, ESP_GATTC_WRITE_CHAR_EVT,
    ESP_GATTC_CLOSE_EVT, ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_SRVC_CHG_EVT, ESP_GATTC_NOTIFY_EVT };
struct esp_ble_gattc_cb_param_t {
    struct { int status=0; uint16_t app_id=0; } reg;
    struct { int status=0; uint16_t conn_id=0; uint8_t remote_bda[6]={}; } open;
    struct { int status=0; uint16_t conn_id=0; } dis_srvc_cmpl;
    struct { int status=0; uint16_t handle=0; } reg_for_notify;
    struct { int status=0; uint16_t conn_id=0, handle=0; } write;
    struct { int status=0; uint16_t conn_id=0; } close;
    struct { uint16_t conn_id=0; } disconnect;
    struct { uint16_t conn_id=0, handle=0, value_len=0; uint8_t remote_bda[6]={}; uint8_t *value=nullptr; bool is_notify=true; } notify;
};
struct esp_bt_uuid_t { uint16_t len=0; union { uint16_t uuid16; uint8_t uuid128[16]; } uuid{}; };
struct esp_gattc_service_elem_t { uint16_t start_handle=0, end_handle=0; };
struct esp_gattc_char_elem_t { uint16_t char_handle=0; int properties=0; };
struct esp_gattc_descr_elem_t { uint16_t handle=0; };
class BLEUUID { public: explicit BLEUUID(const char *) {} esp_bt_uuid_t *getNative() { return &id; } private: esp_bt_uuid_t id; };
class BLEAddress { public: explicit BLEAddress(const String &) {} uint8_t *getNative() { return address; } private: uint8_t address[6]={}; };
inline std::function<void()> sdk_init, sdk_disable;
inline int ble_init_calls=0, ble_deinit_calls=0;
inline bool ble_init_result = true, cleanup_result = true;
inline std::vector<std::string> cleanup_calls;
inline bool host_gatt_success=false;
inline void (*host_gatt_handler)(esp_gattc_cb_event_t,esp_gatt_if_t,esp_ble_gattc_cb_param_t *)=nullptr;
inline int esp_bluedroid_disable();
inline int esp_bluedroid_deinit();
inline int esp_bt_controller_disable();
inline int esp_bt_controller_deinit();
struct BLEDevice {
    inline static bool initialized = false;
    static bool getInitialized() { return initialized; }
    static bool init(const char *) { ++ble_init_calls; if (sdk_init) { sdk_init(); } initialized = ble_init_result; return ble_init_result; }
    static void deinit(bool release) { assert(!release); ++ble_deinit_calls; cleanup_calls.push_back("arduino-deinit");
#if defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH
        esp_bluedroid_disable();esp_bluedroid_deinit();esp_bt_controller_disable();esp_bt_controller_deinit();initialized=false;
#endif
    }
    static void setCustomGattcHandler(void (*handler)(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *)) { host_gatt_handler=handler; }
};
constexpr int ESP_BLUEDROID_STATUS_ENABLED=2, ESP_BLUEDROID_STATUS_UNINITIALIZED=0, ESP_BLUEDROID_STATUS_INITIALIZED=1;
constexpr int ESP_BT_CONTROLLER_STATUS_ENABLED=2, ESP_BT_CONTROLLER_STATUS_IDLE=0, ESP_BT_CONTROLLER_STATUS_INITED=1, BT_MODE_BLE=1, ESP_BT_MODE_BLE=1;
inline int host_status=2, controller_status=2;
inline std::atomic<unsigned> sdk_status_calls{0};
inline std::string sdk_fail_stage;
inline std::vector<std::string> lifecycle_calls;
struct esp_bt_controller_config_t {};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() esp_bt_controller_config_t{}
inline int esp_bt_controller_init(esp_bt_controller_config_t *) {
    lifecycle_calls.push_back("controller-init"); ++ble_init_calls;
    if (sdk_init) { sdk_init(); }
    if (!ble_init_result || sdk_fail_stage == "controller-init") { return -1; }
    controller_status = 1; return ESP_OK;
}
inline int esp_bt_controller_enable(int mode) {
    assert(mode == ESP_BT_MODE_BLE); lifecycle_calls.push_back("controller-enable");
    if (sdk_fail_stage == "controller-enable") { return -1; }
    controller_status = 2; return ESP_OK;
}
inline int esp_bluedroid_init() {
    lifecycle_calls.push_back("host-init");
    if (sdk_fail_stage == "host-init") { return -1; }
    host_status = 1; return ESP_OK;
}
inline int esp_bluedroid_enable() {
    lifecycle_calls.push_back("host-enable");
    if (sdk_fail_stage == "host-enable") { return -1; }
    host_status = 2; return ESP_OK;
}
inline int esp_ble_gattc_register_callback(void (*handler)(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *)) {
    lifecycle_calls.push_back("callback-register");
    if (sdk_fail_stage == "callback-register") { return -1; }
    host_gatt_handler = handler; return ESP_OK;
}
inline bool defer_gatt_registration = false;
inline unsigned app_register_calls = 0, peer_open_calls = 0;
inline esp_ble_addr_type_t peer_open_type = 255;
#if defined(TESLA_BLE_R13_MASTER_BENCH) && TESLA_BLE_R13_MASTER_BENCH
#include "../../../src/modules/tesla_vehicle/bluetooth_master_teardown.h"
inline int master_cleanup_fail_bit = 0;
inline int master_observe(int bit, int rc) {
    return bluetooth_master_teardown::observe(bit, bit == master_cleanup_fail_bit ? -73 : rc);
}
#else
inline int master_observe(int, int rc) { return rc; }
#endif
inline int esp_bluedroid_get_status() { ++sdk_status_calls; return host_status; }
inline int esp_bluedroid_disable() { cleanup_calls.push_back("host-disable"); if (sdk_disable) { sdk_disable(); } if (!cleanup_result || sdk_fail_stage == "host-disable") { return master_observe(1,-1); } host_status=1; return master_observe(1,ESP_OK); }
inline int esp_bluedroid_deinit() { cleanup_calls.push_back("host-deinit"); if (sdk_fail_stage == "host-deinit") { return -1; } host_status=0; return master_observe(2,ESP_OK); }
inline std::function<void()> on_controller_status;
inline int esp_bt_controller_get_status() { ++sdk_status_calls; if (on_controller_status) { on_controller_status(); } return controller_status; }
inline int esp_bt_controller_disable() { cleanup_calls.push_back("controller-disable"); if (sdk_fail_stage == "controller-disable") { return -1; } controller_status=1; return master_observe(4,ESP_OK); }
inline int esp_bt_controller_deinit() { cleanup_calls.push_back("controller-deinit"); if (sdk_fail_stage == "controller-deinit") { return -1; } controller_status=0; return master_observe(8,ESP_OK); }
inline bool btMemRelease(int mode) { assert(mode == BT_MODE_BLE); cleanup_calls.push_back("ble-memory-release"); return true; }
inline bool btMemReleased(int mode) { assert(mode == BT_MODE_BLE); return true; }
inline int esp_ble_gattc_app_register(uint16_t app) {
    ++app_register_calls; lifecycle_calls.push_back("app-register");
    if (sdk_fail_stage == "app-register") { return -1; }
    if(!host_gatt_success)return -1;
    if (defer_gatt_registration) { return ESP_OK; }
    esp_ble_gattc_cb_param_t p;p.reg.app_id=app;host_gatt_handler(ESP_GATTC_REG_EVT,1,&p);return ESP_OK;
}
inline int esp_ble_gattc_open(esp_gatt_if_t interface, uint8_t *address, esp_ble_addr_type_t type, bool) {
    ++peer_open_calls;
    peer_open_type=type;
    if(!host_gatt_success)return -1;
    if(host_single_link_enforced) { assert(host_physical_links==0); ++host_physical_links; }
    esp_ble_gattc_cb_param_t p;p.open.conn_id=4;std::memcpy(p.open.remote_bda,address,6);host_gatt_handler(ESP_GATTC_OPEN_EVT,interface,&p);
    p.dis_srvc_cmpl.conn_id=4;host_gatt_handler(ESP_GATTC_DIS_SRVC_CMPL_EVT,interface,&p);return ESP_OK;
}
inline int esp_ble_gattc_get_service(esp_gatt_if_t, uint16_t, esp_bt_uuid_t *, esp_gattc_service_elem_t *out, uint16_t *count, int) {
    if(!host_gatt_success){return -1;} *out={1,30};*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t, uint16_t, uint16_t, uint16_t, esp_bt_uuid_t id, esp_gattc_char_elem_t *out, uint16_t *count) {
    if(!host_gatt_success)return -1;
    const bool write=id.uuid.uuid128[12]==0x12;*out={uint16_t(write?10:20),write?ESP_GATT_CHAR_PROP_BIT_WRITE:host_rx_properties};*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t, uint16_t, uint16_t characteristic, esp_bt_uuid_t uuid, esp_gattc_descr_elem_t *out, uint16_t *count) {
    assert(characteristic==20 && uuid.len==ESP_UUID_LEN_16 && uuid.uuid.uuid16==0x2902);
    if(!host_gatt_success){return -1;} out->handle=21;*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_register_for_notify(esp_gatt_if_t interface, uint8_t *, uint16_t handle) {
    if(!host_gatt_success){return -1;} esp_ble_gattc_cb_param_t p;p.reg_for_notify.handle=handle;host_gatt_handler(ESP_GATTC_REG_FOR_NOTIFY_EVT,interface,&p);return ESP_OK;
}
inline int esp_ble_gattc_write_char_descr(esp_gatt_if_t interface, uint16_t connection, uint16_t handle, int size, uint8_t *bytes, int type, int auth) {
    assert(size==2 && bytes[1]==0 && handle==21 && type==ESP_GATT_WRITE_TYPE_RSP && auth==ESP_GATT_AUTH_REQ_NONE);
    ++host_cccd_writes; host_cccd_value=bytes[0];
    if(!host_gatt_success){return -1;}
    if(on_descriptor_write) { on_descriptor_write(connection,handle); return ESP_OK; }
    esp_ble_gattc_cb_param_t p;p.write.conn_id=connection;p.write.handle=handle;host_gatt_handler(ESP_GATTC_WRITE_DESCR_EVT,interface,&p);return ESP_OK;
}
inline int gatt_write_result = -1;
inline std::function<void()> on_gatt_write;
inline std::function<void(const uint8_t *,size_t)> on_gatt_bytes;
inline int esp_ble_gattc_write_char(esp_gatt_if_t, uint16_t, uint16_t, size_t size, uint8_t *bytes, int, int) {
    if (on_gatt_bytes) { on_gatt_bytes(bytes,size); }
    if (on_gatt_write) { on_gatt_write(); }
    return gatt_write_result;
}
inline int gatt_close_result = -1;
inline std::function<void()> on_gatt_close;
inline int esp_ble_gattc_close(esp_gatt_if_t, uint16_t) {
    if (on_gatt_close) { on_gatt_close(); }
    return gatt_close_result;
}
// Public deterministic HOST entropy only, never a production RNG fallback.
inline void esp_fill_random(void *out, size_t size) {
    static uint32_t seed = 17;
    auto *p = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < size; ++i) { seed = seed * 1664525u + 1013904223u; p[i] = seed >> 24; }
}

#if TESLA_BLE_MANUAL_VEHICLE_UI
enum esp_gap_ble_cb_event_t { ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, ESP_GAP_BLE_SCAN_START_COMPLETE_EVT, ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT, ESP_GAP_BLE_SCAN_RESULT_EVT };
constexpr int ESP_BT_STATUS_SUCCESS=0,ESP_GAP_SEARCH_INQ_CMPL_EVT=1,ESP_GAP_SEARCH_INQ_RES_EVT=0;
constexpr int BLE_SCAN_TYPE_ACTIVE=1,BLE_ADDR_TYPE_PUBLIC=0,BLE_SCAN_FILTER_ALLOW_ALL=0,BLE_SCAN_DUPLICATE_ENABLE=1;
struct esp_ble_scan_params_t { int scan_type=0,own_addr_type=0,scan_filter_policy=0,scan_interval=0,scan_window=0,scan_duplicate=0; };
struct esp_ble_gap_cb_param_t {
 struct { int status=0; } scan_param_cmpl,scan_start_cmpl,scan_stop_cmpl;
 struct { int search_evt=0;uint8_t ble_adv[62]{},bda[6]{1,2,3,4,5,6};uint8_t adv_data_len=0,scan_rsp_len=0,ble_addr_type=0;int rssi=-50;unsigned ble_evt_type=0; } scan_rst;
};
inline void (*host_gap_handler)(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t *)=nullptr;
inline unsigned scan_start_calls=0,scan_stop_calls=0,scan_seconds=0;
inline std::string scan_fail;
inline bool scan_defer_stop=false;
inline int esp_ble_gap_register_callback(void (*cb)(esp_gap_ble_cb_event_t,esp_ble_gap_cb_param_t *)) { host_gap_handler=cb;return scan_fail=="register"?-1:0; }
inline int esp_ble_gap_set_scan_params(esp_ble_scan_params_t *p) {
 assert(p->scan_type==BLE_SCAN_TYPE_ACTIVE && p->scan_window<=p->scan_interval);
 if(scan_fail=="params") { return -1; }
 esp_ble_gap_cb_param_t v;host_gap_handler(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,&v);return 0;
}
inline int esp_ble_gap_start_scanning(unsigned seconds) {
 ++scan_start_calls;scan_seconds=seconds;if(scan_fail=="start") { return -1; }
 esp_ble_gap_cb_param_t v;host_gap_handler(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,&v);return 0;
}
inline int esp_ble_gap_stop_scanning() {
 ++scan_stop_calls;if(scan_fail=="stop") { return -1; }
 esp_ble_gap_cb_param_t v;if(!scan_defer_stop) { host_gap_handler(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT,&v); }return 0;
}
inline void host_emit_target(const char *name) {
 esp_ble_gap_cb_param_t p;p.scan_rst.adv_data_len=strlen(name)+2;p.scan_rst.ble_adv[0]=strlen(name)+1;p.scan_rst.ble_adv[1]=9;
 memcpy(p.scan_rst.ble_adv+2,name,strlen(name));host_gap_handler(ESP_GAP_BLE_SCAN_RESULT_EVT,&p);
}
inline void host_emit_done() { esp_ble_gap_cb_param_t p;p.scan_rst.search_evt=ESP_GAP_SEARCH_INQ_CMPL_EVT;host_gap_handler(ESP_GAP_BLE_SCAN_RESULT_EVT,&p); }
#endif
