#pragma once

#include <adapters.h>
#include <string>
#include <vector>

class TeslaBleStorageAdapter final : public TeslaBLE::StorageAdapter
{
public:
    bool load(const std::string &key, std::vector<uint8_t> &buffer) override;
    bool save(const std::string &key, const std::vector<uint8_t> &buffer) override;
    bool remove(const std::string &key) override;
    bool has_private_key();

private:
    static const char *path_for_key(const std::string &key);
};
