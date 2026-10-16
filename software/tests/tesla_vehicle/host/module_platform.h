#pragma once
// Only OS/config/SDK boundaries are substituted. TeslaVehicle and its Runtime,
// worker, main, queues ownership, adapters and protocol remain production code.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
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
#include <LittleFS.h>
inline size_t strlcpy(char *to, const char *from, size_t size) {
    const size_t length = std::strlen(from);
    if (size) { std::memcpy(to, from, std::min(length, size - 1)); to[std::min(length, size - 1)] = 0; }
    return length;
}
enum class ConfigSource { File, API };
using Language = int;
inline bool fail_checked_string = false;
class Config {
public:
    Config() = default;
    template <typename F> Config(Config other, F) : children(std::move(other.children)) {}
    static Config Object(std::initializer_list<std::pair<std::string, Config>> values) {
        Config c; for (const auto &v : values) { c.children[v.first] = std::make_shared<Config>(v.second); } return c;
    }
    static Config Bool(bool v) { Config c; c.boolean = v; return c; }
    static Config Int(int v, int, int) { Config c; c.integer = v; return c; }
    static Config Uint(uint32_t v, uint32_t, uint32_t) { Config c; c.integer = v; return c; }
    static Config Str(const char *v, int, int) { Config c; c.text = v; return c; }
    static Config Float(float v) { Config c; c.floating = v; return c; }
    static Config Uptime() { return {}; }
    Config *get(const char *key) { return children.at(key).get(); }
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
};
using ConfigRoot = Config;
struct IModule {
    virtual ~IModule() = default;
    virtual void pre_setup() {} virtual void setup() {} virtual void register_urls() {} virtual void loop() {}
    bool initialized = false;
};
constexpr int EV_MAX_EVS = 16;
enum class EVDataSource { Tesla };
struct HostEV {
    uint64_t generation = 1; int index = 2; uint64_t mac = 0x112233445566;
    uint64_t get_session_generation() const { return generation; }
    bool session_exhausted() const { return exhausted; }
    bool exhausted=false;
    int get_active_ev_index() const { return index; }
    uint64_t get_active_ev_mac() const { return mac; }
    void set_soc(float) { ++applied; } void session_updated(EVDataSource) {}
    int applied = 0;
};
inline HostEV ev;
struct HostAPI {
    std::map<std::string, std::function<void(Language, String &)>> commands;
    bool restorePersistentConfig(const char *, ConfigRoot *) { return false; }
    void writeConfig(const char *, ConfigRoot *) {} void removeConfig(const char *) {}
    void addPersistentConfig(const char *, ConfigRoot *, std::initializer_list<int>, std::initializer_list<const char *>) {}
    void addState(const char *, ConfigRoot *) {}
    template <typename F> void addCommand(const char *name, ConfigRoot *, std::initializer_list<int>, F f, bool) { commands[name] = f; }
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
struct HostTask { void (*entry)(void *); void *context; bool parked = false, deleted = false; std::string name; };
using TaskHandle_t = HostTask *;
inline std::vector<std::unique_ptr<HostTask>> tasks;
inline std::string fail_task;
inline unsigned task_launches = 0;
inline int xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, int, void *context, int, TaskHandle_t *out, int) {
    ++task_launches;
    if (fail_task == name) { return 0; }
    tasks.emplace_back(new HostTask{entry, context, false, false, name}); *out = tasks.back().get(); return pdPASS;
}
// OS-only escape after the real production ACK/completion; no C++ objects
// are abandoned between this boundary and the task entry. Production stays
// compiled with -fno-exceptions, matching the Xtensa target.
inline thread_local std::jmp_buf parked_task;
inline void vTaskSuspend(void *) { std::longjmp(parked_task, 1); }
inline void run_task(TaskHandle_t task) { if (setjmp(parked_task) == 0) { task->entry(task->context); } task->parked = true; }
inline void vTaskDelete(TaskHandle_t task) { assert(task && task->parked && !task->deleted); task->deleted = true; }
inline std::atomic<uint32_t> clock_ms{1};
inline uint32_t millis() { return clock_ms.load(); }
inline uint64_t now_us() { return uint64_t{millis()} * 1000; }
inline std::function<void()> on_delay;
inline std::function<void(uint32_t)> on_task_delay;
inline void vTaskDelay(uint32_t ms) { clock_ms.fetch_add(ms); if (on_delay) { on_delay(); } if (on_task_delay) { on_task_delay(ms); } }
#define pdMS_TO_TICKS(ms) (ms)
using esp_gatt_if_t = uint8_t;
using esp_ble_addr_type_t = uint8_t;
constexpr int ESP_OK=0, ESP_GATT_OK=0, ESP_GATT_IF_NONE=255, ESP_UUID_LEN_16=2, ESP_UUID_LEN_128=16;
constexpr int ESP_GATT_CHAR_PROP_BIT_WRITE=1, ESP_GATT_CHAR_PROP_BIT_NOTIFY=2;
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
    struct { uint16_t conn_id=0, handle=0, value_len=0; uint8_t remote_bda[6]={}; uint8_t *value=nullptr; } notify;
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
struct BLEDevice {
    static bool init(const char *) { ++ble_init_calls; if (sdk_init) { sdk_init(); } return ble_init_result; }
    static void deinit(bool release) { assert(!release); ++ble_deinit_calls; cleanup_calls.push_back("arduino-deinit"); }
    static void setCustomGattcHandler(void (*handler)(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t *)) { host_gatt_handler=handler; }
};
constexpr int ESP_BLUEDROID_STATUS_ENABLED=2, ESP_BLUEDROID_STATUS_UNINITIALIZED=0;
constexpr int ESP_BT_CONTROLLER_STATUS_ENABLED=2, ESP_BT_CONTROLLER_STATUS_IDLE=0, BT_MODE_BLE=1;
inline int host_status=2, controller_status=2;
inline int esp_bluedroid_get_status() { return host_status; }
inline int esp_bluedroid_disable() { cleanup_calls.push_back("host-disable"); if (sdk_disable) { sdk_disable(); } if (!cleanup_result) { return -1; } host_status=1; return ESP_OK; }
inline int esp_bluedroid_deinit() { cleanup_calls.push_back("host-deinit"); host_status=0; return ESP_OK; }
inline int esp_bt_controller_get_status() { return controller_status; }
inline int esp_bt_controller_disable() { cleanup_calls.push_back("controller-disable"); controller_status=1; return ESP_OK; }
inline int esp_bt_controller_deinit() { cleanup_calls.push_back("controller-deinit"); controller_status=0; return ESP_OK; }
inline bool btMemRelease(int mode) { assert(mode == BT_MODE_BLE); cleanup_calls.push_back("ble-memory-release"); return true; }
inline bool btMemReleased(int mode) { assert(mode == BT_MODE_BLE); return true; }
inline int esp_ble_gattc_app_register(uint16_t app) {
    if(!host_gatt_success)return -1;
    esp_ble_gattc_cb_param_t p;p.reg.app_id=app;host_gatt_handler(ESP_GATTC_REG_EVT,1,&p);return ESP_OK;
}
inline int esp_ble_gattc_open(esp_gatt_if_t interface, uint8_t *address, esp_ble_addr_type_t, bool) {
    if(!host_gatt_success)return -1;
    esp_ble_gattc_cb_param_t p;p.open.conn_id=4;std::memcpy(p.open.remote_bda,address,6);host_gatt_handler(ESP_GATTC_OPEN_EVT,interface,&p);
    p.dis_srvc_cmpl.conn_id=4;host_gatt_handler(ESP_GATTC_DIS_SRVC_CMPL_EVT,interface,&p);return ESP_OK;
}
inline int esp_ble_gattc_get_service(esp_gatt_if_t, uint16_t, esp_bt_uuid_t *, esp_gattc_service_elem_t *out, uint16_t *count, int) {
    if(!host_gatt_success){return -1;} *out={1,30};*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t, uint16_t, uint16_t, uint16_t, esp_bt_uuid_t id, esp_gattc_char_elem_t *out, uint16_t *count) {
    if(!host_gatt_success)return -1;
    const bool write=id.uuid.uuid128[12]==0x12;*out={uint16_t(write?10:20),write?ESP_GATT_CHAR_PROP_BIT_WRITE:ESP_GATT_CHAR_PROP_BIT_NOTIFY};*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t, uint16_t, uint16_t, esp_bt_uuid_t, esp_gattc_descr_elem_t *out, uint16_t *count) {
    if(!host_gatt_success){return -1;} out->handle=21;*count=1;return ESP_GATT_OK;
}
inline int esp_ble_gattc_register_for_notify(esp_gatt_if_t interface, uint8_t *, uint16_t handle) {
    if(!host_gatt_success){return -1;} esp_ble_gattc_cb_param_t p;p.reg_for_notify.handle=handle;host_gatt_handler(ESP_GATTC_REG_FOR_NOTIFY_EVT,interface,&p);return ESP_OK;
}
inline int esp_ble_gattc_write_char_descr(esp_gatt_if_t interface, uint16_t connection, uint16_t handle, int, uint8_t *, int, int) {
    if(!host_gatt_success){return -1;} esp_ble_gattc_cb_param_t p;p.write.conn_id=connection;p.write.handle=handle;host_gatt_handler(ESP_GATTC_WRITE_DESCR_EVT,interface,&p);return ESP_OK;
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
