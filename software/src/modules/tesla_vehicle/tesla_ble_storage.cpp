#include "tesla_ble_storage.h"

#include <LittleFS.h>

#include "tesla_ble_key_validation.h"

const char *TeslaBleStorageAdapter::path_for_key(const std::string &key)
{
    if (key == "private_key") {
        return "/config/tesla_ble_private_key";
    }
    if (key == "session_vcsec") {
        return "/config/tesla_ble_session_vcsec";
    }
    if (key == "session_infotainment") {
        return "/config/tesla_ble_session_info";
    }
    if (key == "warp_ev_mac") {
        return "/config/tesla_ble_warp_ev_mac";
    }
    return nullptr;
}

bool TeslaBleStorageAdapter::load(const std::string &key, std::vector<uint8_t> &buffer)
{
    const char *path = path_for_key(key);
    if (path == nullptr) {
        return false;
    }

    const String backup = String(path) + ".bak";
    if (!LittleFS.exists(path) && LittleFS.exists(backup)) {
        LittleFS.rename(backup, path);
    }
    if (!LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        return false;
    }

    const size_t size = file.size();
    if (size == 0 || size > 4096) {
        file.close();
        return false;
    }

    buffer.resize(size);
    const size_t read = file.read(buffer.data(), size);
    file.close();
    if (read != size) {
        buffer.clear();
        return false;
    }
    return true;
}

bool TeslaBleStorageAdapter::save(const std::string &key, const std::vector<uint8_t> &buffer)
{
    const char *path = path_for_key(key);
    if (path == nullptr || buffer.empty() || buffer.size() > 4096) {
        return false;
    }

    const String temporary = String(path) + ".tmp";
    File file = LittleFS.open(temporary, "w");
    if (!file) {
        return false;
    }

    const size_t written = file.write(buffer.data(), buffer.size());
    file.flush();
    file.close();
    if (written != buffer.size()) {
        LittleFS.remove(temporary);
        return false;
    }

    const String backup = String(path) + ".bak";
    LittleFS.remove(backup);
    const bool had_previous = LittleFS.exists(path);
    if (had_previous && !LittleFS.rename(path, backup)) {
        LittleFS.remove(temporary);
        return false;
    }
    if (!LittleFS.rename(temporary, path)) {
        LittleFS.remove(temporary);
        if (had_previous) {
            LittleFS.rename(backup, path);
        }
        return false;
    }
    LittleFS.remove(backup);
    return true;
}

bool TeslaBleStorageAdapter::remove(const std::string &key)
{
    const char *path = path_for_key(key);
    return path != nullptr && (!LittleFS.exists(path) || LittleFS.remove(path));
}

bool TeslaBleStorageAdapter::has_private_key()
{
    std::vector<uint8_t> key;
    return load("private_key", key) && tesla_ble_valid_private_key(key);
}
