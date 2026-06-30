#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include "lvgl.h"

#include "brookesia/core/app.hpp"
#include "brookesia/core/router.hpp"
#include "brookesia/core/service_hub.hpp"

namespace brookesia {

class Kernel {
public:
    Kernel();

    bool start();
    uint64_t scanKeyboardState();
    void handleKeyboardState(uint64_t pressedMask);
    void render();

private:
    bool buildApps();
    void switchTo(Route route);
    void focusCurrentApp();
    void blurCurrentApp();
    void nextRoute();
    void prevRoute();
    void ensureStatusBar();
    void updateStatusBar();
    void drawBootSplash();
    void handleAppMenu(uint64_t newlyPressed);
    void ensureAppMenu();
    void showAppMenu(bool show);
    void updateAppMenu();
    void pumpLvgl();

    static constexpr size_t kRouteCount = 5;

    board::CardputerBsp board_;
    xcas::XcasService casService_;
    ServiceHub services_;
    Router router_;
    std::array<std::unique_ptr<App>, kRouteCount> apps_{};
    lv_obj_t *status_bar_ = nullptr;
    lv_obj_t *status_left_ = nullptr;
    lv_obj_t *status_center_ = nullptr;
    lv_obj_t *status_right_ = nullptr;
    bool status_bar_ready_ = false;
    bool fn_locked_ = false;
    bool shift_locked_ = false;
    uint64_t current_key_mask_ = 0;
    uint64_t previous_key_mask_ = 0;
    lv_obj_t *menu_overlay_ = nullptr;
    lv_obj_t *menu_items_[kRouteCount] = {};
    bool menu_ready_ = false;
    bool menu_open_ = false;
    int menu_index_ = 0;
    uint64_t last_lvgl_tick_us_ = 0;
    bool screenshot_pending_ = false;
};

} // namespace brookesia
