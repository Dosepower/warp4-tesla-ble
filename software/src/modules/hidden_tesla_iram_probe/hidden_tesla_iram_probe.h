#pragma once
#include "module.h"
#include <stdint.h>
class HiddenTeslaIramProbe final : public IModule {
public:
    void register_events() override;
    void loop() override;
private:
    uint32_t pre_count = 0;
    bool post_emitted = false;
};
