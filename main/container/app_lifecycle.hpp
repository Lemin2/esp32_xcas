#pragma once

#include <cstdint>

namespace container {

class AppLifecycle {
public:
    virtual ~AppLifecycle() = default;

    virtual bool init() = 0;
    virtual void onFocus() {}
    virtual void onBlur() {}
    virtual void onSuspend() {}
    virtual void handleKeyboardState(uint64_t pressedMask) { (void)pressedMask; }
    virtual void handleMappedKey(uint32_t key) { (void)key; }
    virtual void render() = 0;
};

} // namespace container
