// Host stub: only the two declarations the MQR1 wrapper uses, copied verbatim in signature
// from the SDK's esp-mqtt include/mqtt_client.h (handle typedef + esp_mqtt_client_start).
#pragma once
#include <esp_err.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
#ifdef __cplusplus
}
#endif
