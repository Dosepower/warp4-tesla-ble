// Host regression test for the production MQR1 wrapper (src/modules/mqtt/mqtt_start_diag.h).
// Compiled against the real SDK esp_err.h / esp_heap_caps.h of the build variant; only the
// link-level SDK functions (heap_caps_* and esp_mqtt_client_start) are replaced by recorders.
// This proves wrapper semantics (one call, original rc, snapshot order/class), NOT device behaviour.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

#include "mqtt_client_stub/mqtt_client.h"
#include "../../src/modules/mqtt/mqtt_start_diag.h"

static std::vector<std::string> calls;
static std::vector<uint32_t> caps_seen;
static int start_calls = 0;
static esp_err_t next_rc = ESP_OK;
static esp_mqtt_client_handle_t seen_client = nullptr;
static size_t heap_free = 6048, heap_largest = 4084, heap_min = 760;
static size_t free_after_start = 6048, largest_after_start = 4084;

extern "C" size_t heap_caps_get_free_size(uint32_t caps) { calls.push_back("free"); caps_seen.push_back(caps); return heap_free; }
extern "C" size_t heap_caps_get_largest_free_block(uint32_t caps) { calls.push_back("largest"); caps_seen.push_back(caps); return heap_largest; }
extern "C" size_t heap_caps_get_minimum_free_size(uint32_t caps) { calls.push_back("min"); caps_seen.push_back(caps); return heap_min; }
extern "C" esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    calls.push_back("start");
    ++start_calls;
    seen_client = client;
    heap_free = free_after_start;      // simulate the heap effect of the SDK call
    heap_largest = largest_after_start;
    return next_rc;
}

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void reset(esp_err_t rc, size_t f0, size_t l0, size_t f1, size_t l1)
{
    calls.clear(); caps_seen.clear(); start_calls = 0; next_rc = rc; seen_client = nullptr;
    heap_free = f0; heap_largest = l0; heap_min = 760; free_after_start = f1; largest_after_start = l1;
}

static void case_rc(esp_err_t rc, const char *name)
{
    // task-creation failure model: heap unchanged; success model: stack+TCB consumed
    const bool ok = rc == ESP_OK;
    reset(rc, 6048, 4084, ok ? 6048 - 6144 / 2 : 6048, ok ? 1000 : 4084);
    Mqr1StartRecord rec;
    memset(&rec, 0xA5, sizeof(rec));
    esp_mqtt_client_handle_t h = reinterpret_cast<esp_mqtt_client_handle_t>(0x1234);
    esp_err_t got = mqr1_start_observed(h, &rec);
    CHECK(got == rc);
    CHECK(rec.rc == rc);
    CHECK(start_calls == 1);
    CHECK(seen_client == h);
    const std::vector<std::string> order = {"free", "largest", "min", "start", "free", "largest", "min"};
    CHECK(calls == order);
    for (uint32_t c : caps_seen) CHECK(c == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    CHECK(rec.before.free_b == 6048 && rec.before.largest_b == 4084 && rec.before.min_b == 760);
    CHECK(rec.after.free_b == (ok ? 6048u - 3072u : 6048u) && rec.after.largest_b == (ok ? 1000u : 4084u));

    char buf[224];
    int n = mqr1_format_start(buf, sizeof(buf), "network", false, 6144, &rec);
    CHECK(n > 0 && (size_t)n < sizeof(buf));
    char want[64];
    snprintf(want, sizeof(want), "rc=%d(%s)", (int)rc, name);
    CHECK(strstr(buf, want) != nullptr);
    CHECK(strstr(buf, "MQR1 start trigger=network ") == buf);
    CHECK(strstr(buf, "stack=6144 caps=INTERNAL|8BIT") != nullptr);
    CHECK(strstr(buf, "pre=6048/4084/760") != nullptr);
    CHECK(start_calls == 1); // formatting does not call the SDK again
    printf("CASE rc=%s line: %s\n", name, buf);
}

int main()
{
    // The FreeRTOS heap class used for task stack/TCB in this SDK is INTERNAL|8BIT (heap_idf.c).
    CHECK(MQR1_HEAP_CAPS == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    CHECK((MQR1_HEAP_CAPS & MALLOC_CAP_SPIRAM) == 0);
    CHECK((MQR1_HEAP_CAPS & MALLOC_CAP_EXEC) == 0);

    case_rc(ESP_OK, "OK");
    case_rc(ESP_FAIL, "FAIL");
    case_rc(ESP_ERR_INVALID_ARG, "INVALID_ARG");
    case_rc(ESP_ERR_NO_MEM, "OTHER"); // not produced by this SDK's start, but must pass through unchanged

    // Worst-case field widths stay inside the production 224-byte buffer.
    Mqr1StartRecord big;
    big.rc = (esp_err_t)0x7fffffff;
    big.before = {0xffffffffu, 0xffffffffu, 0xffffffffu};
    big.after = big.before;
    char buf[224];
    int n = mqr1_format_start(buf, sizeof(buf), "reconfigure", true, 6144, &big);
    CHECK(n > 0 && (size_t)n < sizeof(buf));
    printf("WORST len=%d\n", n);

    // Truncation into a tiny buffer is bounded and NUL-terminated.
    char tiny[8];
    memset(tiny, 'X', sizeof(tiny));
    mqr1_format_start(tiny, sizeof(tiny), "network", false, 6144, &big);
    CHECK(tiny[7] == '\0');

    printf(failures ? "RESULT FAIL %d\n" : "RESULT PASS\n", failures);
    return failures ? 1 : 0;
}
