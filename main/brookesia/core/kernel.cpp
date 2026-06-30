#include "brookesia/core/kernel.hpp"

#include <array>
#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

#include "brookesia/apps/calc_app.hpp"
#include "brookesia/apps/files_app.hpp"
#include "brookesia/apps/graph_app.hpp"
#include "brookesia/apps/project_app.hpp"
#include "brookesia/apps/settings_app.hpp"

namespace brookesia {
namespace {

constexpr char kTag[] = "brookesia_kernel";
constexpr uint64_t kFnBit = (1ULL << 28);
constexpr uint64_t kShiftBit = (1ULL << 29);
constexpr uint64_t kQBit = (1ULL << 15);
constexpr uint64_t kWBit = (1ULL << 16);
constexpr uint64_t kOptBit = (1ULL << 43);

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

} // namespace

Kernel::Kernel() : services_(board_, casService_) {}

bool Kernel::start()
{
    board_.initializeDisplay();
    board_.initializeKeyboard();
    drawBootSplash();

    if (!casService_.start()) {
        ESP_LOGE(kTag, "cas service start failed");
        return false;
    }

    if (!buildApps()) {
        ESP_LOGE(kTag, "app bootstrap failed");
        return false;
    }

    router_.set(Route::Calc);
    focusCurrentApp();
    return true;
}

uint64_t Kernel::scanKeyboardState()
{
    return board_.scanKeyboardState();
}

void Kernel::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly_pressed = pressedMask & ~previous_key_mask_;
    previous_key_mask_ = pressedMask;
    current_key_mask_ = pressedMask;

    if ((newly_pressed & kFnBit) != 0U) {
        fn_locked_ = !fn_locked_;
    }
    if ((newly_pressed & kShiftBit) != 0U) {
        shift_locked_ = !shift_locked_;
    }

    constexpr uint64_t kScreenshotBit = (1ULL << 24); // 'p'
    if (fn_locked_ && ((newly_pressed & kScreenshotBit) != 0U)) {
        screenshot_pending_ = true;
    }

    if ((newly_pressed & kOptBit) != 0U) {
        menu_open_ = !menu_open_;
        if (menu_open_) {
            menu_index_ = static_cast<int>(router_.current());
        }
        showAppMenu(menu_open_);
    }

    if (menu_open_) {
        handleAppMenu(newly_pressed);
        return;
    }

    const bool fn_active = fn_locked_;
    if (fn_active && ((newly_pressed & kQBit) != 0U)) {
        prevRoute();
    } else if (fn_active && ((newly_pressed & kWBit) != 0U)) {
        nextRoute();
    }

    auto &app = apps_[static_cast<size_t>(router_.current())];
    if (app) {
        app->handleKeyboardState(pressedMask);
    }
}

void Kernel::render()
{
    auto &app = apps_[static_cast<size_t>(router_.current())];
    if (app) {
        app->render();
    }

    ensureStatusBar();
    updateStatusBar();
    if (status_bar_ != nullptr) {
        lv_obj_move_foreground(status_bar_);
    }
    if (menu_open_ && menu_overlay_ != nullptr) {
        lv_obj_move_foreground(menu_overlay_);
    }

    pumpLvgl();

    if (screenshot_pending_) {
        screenshot_pending_ = false;
        if (board_.beginScreenshotCapture()) {
            lv_obj_t *screen = lv_screen_active();
            if (screen != nullptr) {
                lv_obj_invalidate(screen);
            }
            lv_refr_now(nullptr);
            board_.emitScreenshot();
            board_.endScreenshotCapture();
        }
    }
}

void Kernel::pumpLvgl()
{
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    uint32_t elapsed_ms = 10;
    if (last_lvgl_tick_us_ != 0 && now_us > last_lvgl_tick_us_) {
        elapsed_ms = static_cast<uint32_t>((now_us - last_lvgl_tick_us_) / 1000ULL);
        if (elapsed_ms == 0) {
            elapsed_ms = 1;
        }
    }
    last_lvgl_tick_us_ = now_us;
    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
}

void Kernel::handleAppMenu(uint64_t newlyPressed)
{
    constexpr uint64_t kUpBit = (1ULL << 39);    // ';' -> up
    constexpr uint64_t kDownBit = (1ULL << 53);  // '.' -> down
    constexpr uint64_t kLeftBit = (1ULL << 52);  // ',' -> left
    constexpr uint64_t kRightBit = (1ULL << 54); // '/' -> right
    constexpr uint64_t kEnterBit = (1ULL << 41); // Enter

    constexpr int kCols = 3;
    const int count = static_cast<int>(kRouteCount);

    if ((newlyPressed & kLeftBit) != 0U) {
        if ((menu_index_ % kCols) > 0) {
            menu_index_ -= 1;
            updateAppMenu();
        }
    }
    if ((newlyPressed & kRightBit) != 0U) {
        if ((menu_index_ % kCols) < (kCols - 1) && (menu_index_ + 1) < count) {
            menu_index_ += 1;
            updateAppMenu();
        }
    }
    if ((newlyPressed & kUpBit) != 0U) {
        if (menu_index_ - kCols >= 0) {
            menu_index_ -= kCols;
            updateAppMenu();
        }
    }
    if ((newlyPressed & kDownBit) != 0U) {
        if (menu_index_ + kCols < count) {
            menu_index_ += kCols;
            updateAppMenu();
        }
    }
    if ((newlyPressed & kEnterBit) != 0U) {
        switchTo(static_cast<Route>(menu_index_));
        menu_open_ = false;
        showAppMenu(false);
    }
}

void Kernel::ensureAppMenu()
{
    if (menu_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    menu_overlay_ = lv_obj_create(screen);
    lv_obj_set_size(menu_overlay_, 230, 122);
    lv_obj_align(menu_overlay_, LV_ALIGN_CENTER, 0, 7);
    lv_obj_set_style_radius(menu_overlay_, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(menu_overlay_, LV_COLOR_MAKE(24, 30, 46), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu_overlay_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_overlay_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_overlay_, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(menu_overlay_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(menu_overlay_, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(menu_overlay_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(menu_overlay_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_SCROLLABLE);

    static const char *const kNames[kRouteCount] = {"Calc", "Graph", "Files", "Project", "Settings"};
    static const char *const kIcons[kRouteCount] = {
        LV_SYMBOL_LIST, LV_SYMBOL_IMAGE, LV_SYMBOL_DIRECTORY, LV_SYMBOL_FILE, LV_SYMBOL_SETTINGS};
    static const uint32_t kTileRgb[kRouteCount] = {0x3D6CE0U, 0xE0913AU, 0x32B36BU, 0x9B5BE0U, 0x5A6472U};

    for (size_t i = 0; i < kRouteCount; ++i) {
        const uint32_t rgb = kTileRgb[i];
        const lv_color_t tile_color =
            LV_COLOR_MAKE((rgb >> 16) & 0xFFU, (rgb >> 8) & 0xFFU, rgb & 0xFFU);

        lv_obj_t *tile = lv_obj_create(menu_overlay_);
        lv_obj_set_size(tile, 66, 50);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(tile, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, tile_color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_row(tile, 1, LV_PART_MAIN);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *icon = lv_label_create(tile);
        lv_obj_set_style_text_color(icon, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(icon, kIcons[i]);

        lv_obj_t *name = lv_label_create(tile);
        lv_obj_set_style_text_color(name, LV_COLOR_MAKE(236, 240, 248), LV_PART_MAIN);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(name, kNames[i]);

        menu_items_[i] = tile;
    }

    menu_ready_ = true;
}

void Kernel::showAppMenu(bool show)
{
    if (show) {
        ensureAppMenu();
    }
    if (!menu_ready_ || menu_overlay_ == nullptr) {
        return;
    }

    if (show) {
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(menu_overlay_);
        updateAppMenu();
    } else {
        lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void Kernel::updateAppMenu()
{
    if (!menu_ready_) {
        return;
    }

    for (size_t i = 0; i < kRouteCount; ++i) {
        lv_obj_t *tile = menu_items_[i];
        if (tile == nullptr) {
            continue;
        }
        const bool selected = (static_cast<int>(i) == menu_index_);
        if (selected) {
            lv_obj_set_style_border_color(tile, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
            lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
            lv_obj_set_style_outline_color(tile, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
            lv_obj_set_style_outline_width(tile, 2, LV_PART_MAIN);
            lv_obj_set_style_outline_pad(tile, 1, LV_PART_MAIN);
            lv_obj_set_style_transform_width(tile, 2, LV_PART_MAIN);
            lv_obj_set_style_transform_height(tile, 2, LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
            lv_obj_set_style_outline_width(tile, 0, LV_PART_MAIN);
            lv_obj_set_style_transform_width(tile, 0, LV_PART_MAIN);
            lv_obj_set_style_transform_height(tile, 0, LV_PART_MAIN);
        }
    }
}

bool Kernel::buildApps()
{
    apps_[static_cast<size_t>(Route::Calc)] = std::make_unique<CalcApp>(services_);
    apps_[static_cast<size_t>(Route::Graph)] = std::make_unique<GraphApp>();
    apps_[static_cast<size_t>(Route::Files)] = std::make_unique<FilesApp>();
    apps_[static_cast<size_t>(Route::Project)] = std::make_unique<ProjectApp>();
    apps_[static_cast<size_t>(Route::Settings)] = std::make_unique<SettingsApp>(services_);

    for (auto &app : apps_) {
        if (!app || !app->init()) {
            return false;
        }
    }
    return true;
}

void Kernel::switchTo(Route route)
{
    if (route == router_.current()) {
        return;
    }

    blurCurrentApp();
    router_.set(route);
    focusCurrentApp();
}

void Kernel::focusCurrentApp()
{
    auto &app = apps_[static_cast<size_t>(router_.current())];
    if (app) {
        app->onFocus();
    }
}

void Kernel::blurCurrentApp()
{
    auto &app = apps_[static_cast<size_t>(router_.current())];
    if (app) {
        app->onBlur();
    }
}

void Kernel::nextRoute()
{
    Route target = router_.current();
    switch (target) {
    case Route::Calc:
        target = Route::Graph;
        break;
    case Route::Graph:
        target = Route::Files;
        break;
    case Route::Files:
        target = Route::Project;
        break;
    case Route::Project:
        target = Route::Settings;
        break;
    case Route::Settings:
        target = Route::Calc;
        break;
    }
    switchTo(target);
}

void Kernel::prevRoute()
{
    Route target = router_.current();
    switch (target) {
    case Route::Calc:
        target = Route::Settings;
        break;
    case Route::Graph:
        target = Route::Calc;
        break;
    case Route::Files:
        target = Route::Graph;
        break;
    case Route::Project:
        target = Route::Files;
        break;
    case Route::Settings:
        target = Route::Project;
        break;
    }
    switchTo(target);
}

void Kernel::ensureStatusBar()
{
    if (status_bar_ready_) {
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, board::CardputerBsp::kDisplayWidth, 16);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar_, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_bar_, LV_COLOR_MAKE(22, 30, 46), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(status_bar_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(status_bar_, 0, LV_PART_MAIN);

    status_left_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(status_left_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_left_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_left_, LV_ALIGN_LEFT_MID, 0, 0);

    status_center_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(status_center_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_center_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_center_, LV_ALIGN_CENTER, 0, 0);

    status_right_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_color(status_right_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_right_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_right_, LV_ALIGN_RIGHT_MID, 0, 0);

    status_bar_ready_ = true;
}

void Kernel::updateStatusBar()
{
    if (!status_bar_ready_) {
        return;
    }

    const char *route_text = "CALC";
    switch (router_.current()) {
    case Route::Calc:
        route_text = "CALC";
        break;
    case Route::Graph:
        route_text = "GRAPH";
        break;
    case Route::Files:
        route_text = "FILES";
        break;
    case Route::Project:
        route_text = "PROJECT";
        break;
    case Route::Settings:
        route_text = "SETTINGS";
        break;
    }

    const bool fn_on = fn_locked_;
    const bool shift_on = shift_locked_;
    const bool busy = casService_.busy();

    char left[48];
    char center[64];
    char right[24];

    std::snprintf(left, sizeof(left), "%s  RAD", route_text);
    const char *fn_token = fn_on ? "[FN] " : "";
    const char *shift_token = shift_on ? "[CAPS] " : "";
    std::snprintf(center, sizeof(center), "%s%s%s",
                  fn_token,
                  shift_token,
                  busy ? "[CAS*]" : "");

    const uint64_t secs = static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);
    const uint64_t hh = (secs / 3600ULL) % 24ULL;
    const uint64_t mm = (secs / 60ULL) % 60ULL;
    std::snprintf(right, sizeof(right), "%02llu:%02llu",
                  static_cast<unsigned long long>(hh),
                  static_cast<unsigned long long>(mm));

    lv_label_set_text(status_left_, left);
    lv_label_set_text(status_center_, center);
    lv_label_set_text(status_right_, right);
}

void Kernel::drawBootSplash()
{
    std::array<uint16_t, board::CardputerBsp::kDisplayWidth> line{};
    for (int y = 0; y < board::CardputerBsp::kDisplayHeight; ++y) {
        uint16_t color = rgb565(20, 20, 20);
        if (y < board::CardputerBsp::kDisplayHeight / 3) {
            color = rgb565(220, 40, 40);
        } else if (y < (board::CardputerBsp::kDisplayHeight * 2) / 3) {
            color = rgb565(40, 190, 70);
        } else {
            color = rgb565(40, 110, 220);
        }
        line.fill(color);
        board_.presentArea(0, y, board::CardputerBsp::kDisplayWidth, y + 1, line.data());
    }
    ESP_LOGI(kTag, "boot splash rendered");
}

} // namespace brookesia
