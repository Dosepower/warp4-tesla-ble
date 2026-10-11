#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <tesla_clean/storage.hpp>
#ifndef TESLA_BLE_HOST_FILESYSTEM
#define TESLA_BLE_HOST_FILESYSTEM
#endif
#ifdef TESLA_BLE_MODULE_HOST_TEST
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string &s) : std::string(s) {}
    explicit operator bool() const { return true; } // Real WString OOM tested separately.
    void toUpperCase() { std::transform(begin(), end(), begin(), [](unsigned char c) { return std::toupper(c); }); }
};
#else
using String = std::string;
#endif
inline std::function<bool(const std::string &)> fs_fault;
// Optional short-read model: requested count and current offset -> count.
inline std::function<size_t(size_t, size_t)> fs_read_count;
inline bool fs_fails(const std::string &operation) { return fs_fault && fs_fault(operation); }
struct File {
    std::vector<uint8_t> *bytes = nullptr;
    bool writing = false;
    size_t cursor = 0;
    explicit operator bool() const { return bytes != nullptr; }
    bool isDirectory() const { return false; }
    size_t size() const { return bytes ? bytes->size() : 0; }
    size_t read(uint8_t *output, size_t count) {
        if (!bytes || writing || fs_fails("read")) { return 0; }
        count = std::min(count, bytes->size() - std::min(cursor, bytes->size()));
        if (fs_read_count) { count = std::min(count, fs_read_count(count, cursor)); }
        if (count) { std::copy_n(bytes->data() + cursor, count, output); }
        cursor += count; return count;
    }
    size_t write(const uint8_t *data, size_t count) {
        if (!bytes || !writing || fs_fails("write")) { write_error = true; return 0; }
        bytes->resize(cursor + count); std::copy_n(data, count, bytes->data() + cursor);
        cursor += count; return count;
    }
    bool write_error = false;
    void flush() { if (bytes && writing) { write_error = fs_fails("flush") || write_error; } }
    int getWriteError() const { return write_error ? 1 : 0; }
    void close() {
        // Arduino close is void. Model failed write-close persistence by
        // truncation; production MUST detect it through reopening/verification.
        // Read-close has no observable error result, but always invalidates.
        if (bytes && writing && fs_fails("close:write")) {
            write_error = true; if (!bytes->empty()) { bytes->pop_back(); }
        }
        bytes = nullptr; cursor = 0;
    }
};
struct HostLittleFS {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> failures;
    std::vector<std::string> removals;
    bool exists(const String &path) { return files.count(path) != 0; }
    bool remove(const String &path) {
        removals.push_back(path);
        if (failures.count(path) || fs_fails("remove:" + path)) { return false; }
        return files.erase(path) != 0;
    }
    bool rename(const String &from, const String &to) {
        if (!exists(from) || fs_fails("rename:" + from + ":" + to)) { return false; }
        files[to] = files[from]; files.erase(from); return true;
    }
    File open(const String &path, const char *mode) {
        if (fs_fails("open:" + path + ":" + mode)) { return {}; }
        if (*mode == 'w') { files[path] = {}; }
        return exists(path) ? File{&files[path], *mode == 'w'} : File{};
    }
};
inline HostLittleFS LittleFS;
inline tesla_clean::LoadResult tesla_ble_host_read_into(const char *path, uint8_t *buffer,
                                                                   size_t capacity, size_t &size) {
#ifdef TESLA_BLE_ALLOCATION_PROBE
    AllocationProbe::PlatformBoundary boundary;
#endif
    using Result = tesla_clean::LoadResult;
    String source(path);
    if (!LittleFS.exists(source)) { source += ".bak"; }
    if (!LittleFS.exists(source)) { return Result::Absent; }
    File file = LittleFS.open(source, "r");
    if (!file || file.size() == 0 || file.size() > capacity) { file.close(); return Result::Error; }
    const size_t expected = file.size(); size = file.read(buffer, expected); file.close();
    if (size != expected) { size = 0; return Result::Error; }
    return Result::Loaded;
}
