/* MQR1 diagnostic (observation only, no behaviour change).
 *
 * Wraps exactly one esp_mqtt_client_start() call, captures the native internal
 * 8-bit heap (the class FreeRTOS pvPortMalloc/pvPortMallocStack use for the
 * mqtt_task stack and TCB: MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) immediately
 * before and after the call, and returns the SDK's original return code.
 * No allocation, no retry, no logging inside the measured window.
 *
 * SDK facts (esp-mqtt mqtt_client.c, esp_mqtt_client_start):
 *   ESP_ERR_INVALID_ARG  client == NULL
 *   ESP_FAIL             state not INIT/DISCONNECTED ("Client has started")
 *   ESP_FAIL             xTaskCreate(mqtt_task, stack) != pdTRUE
 *   ESP_OK               task created
 * The xTaskCreate return value itself is not exposed by the SDK.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <mqtt_client.h>

#define MQR1_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define MQR1_MAX_LINES 32U

struct Mqr1HeapSnapshot {
    uint32_t free_b;
    uint32_t largest_b;
    uint32_t min_b;
};

struct Mqr1StartRecord {
    Mqr1HeapSnapshot before;
    Mqr1HeapSnapshot after;
    esp_err_t rc;
};

static inline void mqr1_snapshot(Mqr1HeapSnapshot *s)
{
    s->free_b    = static_cast<uint32_t>(heap_caps_get_free_size(MQR1_HEAP_CAPS));
    s->largest_b = static_cast<uint32_t>(heap_caps_get_largest_free_block(MQR1_HEAP_CAPS));
    s->min_b     = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MQR1_HEAP_CAPS));
}

// Exactly one SDK start call; its return code is returned unchanged.
static inline esp_err_t mqr1_start_observed(esp_mqtt_client_handle_t client, Mqr1StartRecord *rec)
{
    mqr1_snapshot(&rec->before);
    const esp_err_t rc = esp_mqtt_client_start(client);
    mqr1_snapshot(&rec->after);
    rec->rc = rc;
    return rc;
}

static inline const char *mqr1_rc_name(esp_err_t rc)
{
    switch (rc) {
        case ESP_OK:              return "OK";
        case ESP_FAIL:            return "FAIL";
        case ESP_ERR_INVALID_ARG: return "INVALID_ARG";
        default:                  return "OTHER";
    }
}

// Bounded fixed-size formatting into a caller-provided buffer (no allocation).
// running_before: a previous start of this client returned ESP_OK and no stop/destroy since.
static inline int mqr1_format_start(char *buf, size_t len, const char *trigger, bool running_before, uint32_t stack, const Mqr1StartRecord *rec)
{
    return snprintf(buf, len,
                    "MQR1 start trigger=%s rc=%d(%s) running_before=%u stack=%u caps=INTERNAL|8BIT pre=%u/%u/%u post=%u/%u/%u",
                    trigger, static_cast<int>(rec->rc), mqr1_rc_name(rec->rc), running_before ? 1U : 0U,
                    static_cast<unsigned>(stack),
                    static_cast<unsigned>(rec->before.free_b), static_cast<unsigned>(rec->before.largest_b), static_cast<unsigned>(rec->before.min_b),
                    static_cast<unsigned>(rec->after.free_b),  static_cast<unsigned>(rec->after.largest_b),  static_cast<unsigned>(rec->after.min_b));
}
