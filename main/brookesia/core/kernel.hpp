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
    void updateKeyboard();
    uint64_t keyboardState() const;
    bool fnActive() const;
    bool shiftActive() const;
    bool popMappedKey(uint32_t &key);
    void pushMappedKey(uint32_t key);
    void setModifierState(bool fnActive, bool shiftActive);
    void handleKeyboardState(uint64_t pressedMask);
    void handleMappedKey(uint32_t key);
    void debugSubmitFormula(const char *formula);
    void debugEmitFormulaImage(const char *formula);
    void requestScreenshot();
    bool lockLvgl(uint32_t timeout_ms);
    void unlockLvgl();
    void render();

private:
    bool buildApps();
    void switchTo(Route route);
    void focusCurrentApp();
    void blurCurrentApp();
    void nextRoute();
    void prevRoute();
    void ensureStatusBar();
    void ensureGlobalKeyboard();
    void updateStatusBar();
    void updateGlobalKeyboardBinding();
    void bringSystemChromeToFront();
    void drawBootSplash();
    void ensureAppMenu();
    void showAppMenu(bool show);
    void updateAppMenu();
    void pumpLvgl();
    int appMenuIndexForTile(lv_obj_t *tile) const;
    lv_obj_t *findFocusedTextarea(lv_obj_t *root) const;
    bool isObjectVisible(lv_obj_t *obj) const;
    static void appMenuTileEventCb(lv_event_t *e);
    static void statusLauncherEventCb(lv_event_t *e);
    static void statusAppMenuEventCb(lv_event_t *e);
    static void statusKeyboardEventCb(lv_event_t *e);
    static void globalKeyboardEventCb(lv_event_t *e);

    static constexpr size_t kRouteCount = 5;

    board::CardputerBsp board_;
    xcas::XcasService casService_;
    ServiceHub services_;
    Router router_;
    std::array<std::unique_ptr<App>, kRouteCount> apps_{};
    lv_obj_t *status_bar_ = nullptr;
    lv_obj_t *status_launcher_btn_ = nullptr;
    lv_obj_t *status_app_menu_btn_ = nullptr;
    lv_obj_t *status_keyboard_btn_ = nullptr;
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
    lv_group_t *menu_group_ = nullptr;
    bool menu_ready_ = false;
    bool menu_open_ = false;
    int menu_index_ = 0;
    lv_obj_t *global_keyboard_ = nullptr;
    bool global_keyboard_visible_ = false;
    uint64_t last_lvgl_tick_us_ = 0;
    bool screenshot_pending_ = false;
};

} // namespace brookesia
