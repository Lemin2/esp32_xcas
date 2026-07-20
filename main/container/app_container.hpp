#pragma once

#include <memory>

#include "container/app_lifecycle.hpp"
#include "container/router.hpp"
#include "container/service_bus.hpp"

namespace container {

class AppContainer {
public:
    AppContainer();

    bool start();
    uint64_t scanKeyboardState();
    void handleKeyboardState(uint64_t pressedMask);
    void render();

private:
    void drawBootSplash();

    std::unique_ptr<board::IBsp> board_;
    xcas::XcasService casService_;
    ServiceBus services_;
    Router router_;
    std::unique_ptr<AppLifecycle> calcApp_;
};

} // namespace container
