#pragma once

#include <cstdint>

namespace brookesia {

class App {
public:
    virtual ~App() = default;

    virtual bool init() = 0;
    virtual void onFocus() {}
    virtual void onBlur() {}
    virtual void onSuspend() {}
    virtual void handleKeyboardState(uint64_t pressedMask) = 0;
    virtual void handleMappedKey(uint32_t key) { (void)key; }
    virtual void render() = 0;
};

} // namespace brookesia
