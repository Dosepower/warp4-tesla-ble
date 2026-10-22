#pragma once
// ESP-IDF stores UUID128 least-significant byte first. No Arduino String,
// parser, heap allocation or fallible native-pointer dereference on reconnect.
static constexpr esp_bt_uuid_t tesla_ble_uuid(uint8_t suffix)
{
    esp_bt_uuid_t id{};
    id.len = ESP_UUID_LEN_128;
    const uint8_t bytes[16] = {0x1e, 0xb9, 0xf8, 0xeb, 0x0c, 0x96, 0x88, 0x9b, 0xf0, 0x43, 0xd1, 0xb2, suffix, 0x02, 0x00, 0x00};
    for (size_t i = 0; i < 16; ++i) { id.uuid.uuid128[i] = bytes[i]; }
    return id;
}
