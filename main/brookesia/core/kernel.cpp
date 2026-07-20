#include "brookesia/core/kernel.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#if CONFIG_XCAS_ENABLE_WIFI
#include "esp_wifi.h"
#endif

#include "brookesia/apps/calc_app.hpp"
#include "brookesia/apps/files_app.hpp"
#include "brookesia/apps/graph_app.hpp"
#include "brookesia/apps/project_app.hpp"
#include "brookesia/apps/settings_app.hpp"
#include "brookesia/core/app_settings.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

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

void animSetOpa(void *obj, int32_t v)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void animSetTranslateY(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v), LV_PART_MAIN);
}

void animHideComplete(lv_anim_t *a)
{
    if (a == nullptr) {
        return;
    }
    auto *obj = static_cast<lv_obj_t *>(a->var);
    if (obj == nullptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, 0, LV_PART_MAIN);
}

bool wifiConnected()
{
#if CONFIG_XCAS_ENABLE_WIFI
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
#else
    return false;
#endif
}

} // namespace

Kernel::Kernel()
        : board_(board::createSelectedBsp()),
            services_(*board_, casService_)
{
}

bool Kernel::start()
{
    settings::load();

    board_->initializeDisplay();
    board_->initializeKeyboard();
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
    return board_->scanKeyboardState();
}

void Kernel::updateKeyboard()
{
    board_->updateKeyboard();
}

uint64_t Kernel::keyboardState() const
{
    return board_->keyboardState();
}

bool Kernel::fnActive() const
{
    return board_->fnActive();
}

bool Kernel::shiftActive() const
{
    return board_->shiftActive();
}

bool Kernel::popMappedKey(uint32_t &key)
{
    return board_->popMappedKey(key);
}

void Kernel::pushMappedKey(uint32_t key)
{
    board_->pushMappedKey(key);
}

void Kernel::setModifierState(bool fnActive, bool shiftActive)
{
    fn_locked_ = fnActive;
    shift_locked_ = shiftActive;
}

void Kernel::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly_pressed = pressedMask & ~previous_key_mask_;
    previous_key_mask_ = pressedMask;
    current_key_mask_ = pressedMask;

    constexpr uint64_t kScreenshotBit = (1ULL << 24); // 'p'
    if (fn_locked_ && ((newly_pressed & kScreenshotBit) != 0U)) {
        screenshot_pending_ = true;
    }

    if ((newly_pressed & kOptBit) != 0U) {
        auto &app = apps_[static_cast<size_t>(router_.current())];
        const bool consumed = app && app->handleMenuButton();
        if (!consumed) {
            menu_open_ = !menu_open_;
            if (menu_open_) {
                menu_index_ = static_cast<int>(router_.current());
            }
            showAppMenu(menu_open_);
        }
    }

    if (menu_open_) {
        return;
    }

    const bool fn_active = fn_locked_;
    const settings::AppSettings app_settings = settings::get();
    if (app_settings.fn_app_switch_enabled && fn_active && ((newly_pressed & kQBit) != 0U)) {
        prevRoute();
    } else if (app_settings.fn_app_switch_enabled && fn_active && ((newly_pressed & kWBit) != 0U)) {
        nextRoute();
    }

    (void)pressedMask;
}

void Kernel::handleMappedKey(uint32_t key)
{
    if (menu_open_) {
        if (menu_group_ == nullptr) {
            return;
        }
        if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
            lv_group_focus_prev(menu_group_);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
            lv_group_focus_next(menu_group_);
        } else if (key == LV_KEY_ENTER) {
            lv_group_send_data(menu_group_, key);
        } else if (key == LV_KEY_ESC) {
            menu_open_ = false;
            showAppMenu(false);
        } else {
            lv_group_send_data(menu_group_, key);
        }
        return;
    }

    auto &app = apps_[static_cast<size_t>(router_.current())];
    if (app) {
        app->handleMappedKey(key);
    }
}

void Kernel::debugSubmitFormula(const char *formula)
{
    if (formula == nullptr || formula[0] == '\0') {
        return;
    }

    if (router_.current() != Route::Calc) {
        switchTo(Route::Calc);
    }

    auto *calc = static_cast<CalcApp *>(apps_[static_cast<size_t>(Route::Calc)].get());
    if (calc == nullptr) {
        return;
    }
    calc->debugSubmitFormula(formula);
}

void Kernel::debugEmitFormulaImage(const char *formula)
{
    if (formula == nullptr || formula[0] == '\0') {
        return;
    }

    if (router_.current() != Route::Calc) {
        switchTo(Route::Calc);
    }

    auto *calc = static_cast<CalcApp *>(apps_[static_cast<size_t>(Route::Calc)].get());
    if (calc == nullptr) {
        return;
    }
    calc->debugEmitFormulaImage(formula);
}

void Kernel::requestScreenshot()
{
    screenshot_pending_ = true;
}

bool Kernel::lockLvgl(uint32_t timeout_ms)
{
    return board_->lockLvgl(timeout_ms);
}

void Kernel::unlockLvgl()
{
    board_->unlockLvgl();
}

void Kernel::render()
{
    // While the launcher/app-switch menu overlay covers the screen, the
    // focused app's render() would otherwise keep doing full-cost work every
    // frame (e.g. GraphApp re-evaluating plots and re-drawing cursor lines)
    // purely underneath a hidden layer. Skip it so the menu stays responsive;
    // queued CAS results simply get drained on the next frame once the menu
    // closes, so nothing is lost.
    if (!menu_open_) {
        auto &app = apps_[static_cast<size_t>(router_.current())];
        if (app) {
            app->render();
        }
    }

    ensureStatusBar();
    updateStatusBar();
    updateGlobalKeyboardBinding();
    bringSystemChromeToFront();

    pumpLvgl();

    if (screenshot_pending_) {
        screenshot_pending_ = false;
        if (board_->beginScreenshotCapture()) {
            lv_obj_t *screen = lv_screen_active();
            if (screen != nullptr) {
                lv_obj_invalidate(screen);
            }
            lv_refr_now(nullptr);
            board_->emitScreenshot();
            board_->endScreenshotCapture();
        }
    }
}

void Kernel::pumpLvgl()
{
    if (board_->usesExternalLvglPort()) {
        return;
    }

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

void Kernel::ensureAppMenu()
{
    if (menu_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    const bool touch = board_->hasTouchInput();
    const lv_coord_t overlay_w = touch ? 540 : 230;
    const lv_coord_t overlay_h = touch ? 270 : 122;
    const lv_coord_t tile_w = touch ? 160 : 66;
    const lv_coord_t tile_h = touch ? 108 : 50;

    menu_overlay_ = lv_obj_create(screen);
    lv_obj_set_size(menu_overlay_, overlay_w, overlay_h);
    lv_obj_align(menu_overlay_, LV_ALIGN_CENTER, 0, 7);
    ui_theme::applyMenuOverlay(menu_overlay_, LV_COLOR_MAKE(24, 30, 46), LV_COLOR_MAKE(24, 30, 46), 0);
    lv_obj_set_flex_flow(menu_overlay_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(menu_overlay_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(menu_overlay_, touch ? 12 : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(menu_overlay_, touch ? 12 : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(menu_overlay_, touch ? 12 : 0, LV_PART_MAIN);
    lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_SCROLLABLE);

    menu_group_ = lv_group_create();
    lv_group_set_editing(menu_group_, false);

    static const char *const kNames[kRouteCount] = {"Calc", "Graph", "Files", "Program", "Settings"};
    static const char *const kIcons[kRouteCount] = {
        LV_SYMBOL_LIST, LV_SYMBOL_IMAGE, LV_SYMBOL_DIRECTORY, LV_SYMBOL_FILE, LV_SYMBOL_SETTINGS};
    static const uint32_t kTileRgb[kRouteCount] = {0x3D6CE0U, 0xE0913AU, 0x32B36BU, 0x9B5BE0U, 0x5A6472U};

    for (size_t i = 0; i < kRouteCount; ++i) {
        const uint32_t rgb = kTileRgb[i];
        const uint8_t red = static_cast<uint8_t>((rgb >> 16) & 0xFFU);
        const uint8_t green = static_cast<uint8_t>((rgb >> 8) & 0xFFU);
        const uint8_t blue = static_cast<uint8_t>(rgb & 0xFFU);
        const lv_color_t tile_color = LV_COLOR_MAKE(red, green, blue);

        lv_obj_t *tile = lv_button_create(menu_overlay_);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, &Kernel::appMenuTileEventCb, LV_EVENT_ALL, this);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        ui_theme::applyMenuTile(tile, tile_color);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        static lv_style_prop_t trans_props[] = {
            LV_STYLE_OUTLINE_WIDTH,
            LV_STYLE_TRANSFORM_WIDTH,
            LV_STYLE_TRANSFORM_HEIGHT,
            LV_STYLE_PROP_INV,
        };
        static lv_style_transition_dsc_t trans_dsc;
        static bool trans_inited = false;
        if (!trans_inited) {
            lv_style_transition_dsc_init(&trans_dsc, trans_props, lv_anim_path_ease_out, 140, 0, nullptr);
            trans_inited = true;
        }
        lv_obj_set_style_transition(tile, &trans_dsc, LV_PART_MAIN);

        lv_obj_t *icon = lv_label_create(tile);
        lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);
        ui_theme::applyText14(icon);
        lv_obj_set_style_text_color(icon, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_label_set_text(icon, kIcons[i]);

        lv_obj_t *name = lv_label_create(tile);
        lv_obj_add_flag(name, LV_OBJ_FLAG_EVENT_BUBBLE);
        ui_theme::applyText14(name);
        lv_obj_set_style_text_color(name, LV_COLOR_MAKE(236, 240, 248), LV_PART_MAIN);
        lv_label_set_text(name, kNames[i]);

        menu_items_[i] = tile;
        lv_group_add_obj(menu_group_, tile);
    }

    menu_ready_ = true;
    menu_index_cached_ = -1;
}

void Kernel::appMenuTileEventCb(lv_event_t *e)
{
    auto *self = static_cast<Kernel *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    const int index = self->appMenuIndexForTile(lv_event_get_current_target_obj(e));
    if (index < 0) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        self->menu_index_ = index;
        self->updateAppMenu();
    } else if (code == LV_EVENT_PRESSED) {
        self->menu_index_ = index;
        self->updateAppMenu();
    } else if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        self->switchTo(static_cast<Route>(index));
        self->menu_open_ = false;
        self->showAppMenu(false);
    } else if (code == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        self->switchTo(static_cast<Route>(index));
        self->menu_open_ = false;
        self->showAppMenu(false);
    }
}

int Kernel::appMenuIndexForTile(lv_obj_t *tile) const
{
    for (size_t i = 0; i < kRouteCount; ++i) {
        if (menu_items_[i] == tile) {
            return static_cast<int>(i);
        }
    }
    return -1;
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
        lv_anim_delete(menu_overlay_, nullptr);
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(menu_overlay_);
        menu_index_cached_ = -1;
        if (board_->hasTouchInput()) {
            lv_obj_set_style_opa(menu_overlay_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_translate_y(menu_overlay_, 0, LV_PART_MAIN);
            updateAppMenu();
            if (menu_group_ != nullptr) {
                lv_group_set_default(menu_group_);
                lv_group_focus_obj(menu_items_[menu_index_]);
            }
            return;
        }

        lv_obj_set_style_opa(menu_overlay_, LV_OPA_0, LV_PART_MAIN);
        lv_obj_set_style_translate_y(menu_overlay_, 6, LV_PART_MAIN);

        lv_anim_t fade_in;
        lv_anim_init(&fade_in);
        lv_anim_set_var(&fade_in, menu_overlay_);
        lv_anim_set_exec_cb(&fade_in, animSetOpa);
        lv_anim_set_values(&fade_in, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_duration(&fade_in, 170);
        lv_anim_set_path_cb(&fade_in, lv_anim_path_ease_out);
        lv_anim_start(&fade_in);

        lv_anim_t rise_in;
        lv_anim_init(&rise_in);
        lv_anim_set_var(&rise_in, menu_overlay_);
        lv_anim_set_exec_cb(&rise_in, animSetTranslateY);
        lv_anim_set_values(&rise_in, 6, 0);
        lv_anim_set_duration(&rise_in, 170);
        lv_anim_set_path_cb(&rise_in, lv_anim_path_ease_out);
        lv_anim_start(&rise_in);

        updateAppMenu();
        if (menu_group_ != nullptr) {
            lv_group_set_default(menu_group_);
            lv_group_focus_obj(menu_items_[menu_index_]);
        }
    } else {
        lv_anim_delete(menu_overlay_, nullptr);
        if (board_->hasTouchInput()) {
            lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(menu_overlay_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_translate_y(menu_overlay_, 0, LV_PART_MAIN);
            menu_index_cached_ = -1;
            return;
        }

        lv_anim_t fade_out;
        lv_anim_init(&fade_out);
        lv_anim_set_var(&fade_out, menu_overlay_);
        lv_anim_set_exec_cb(&fade_out, animSetOpa);
        lv_anim_set_values(&fade_out, LV_OPA_COVER, LV_OPA_0);
        lv_anim_set_duration(&fade_out, 120);
        lv_anim_set_path_cb(&fade_out, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&fade_out, animHideComplete);
        lv_anim_start(&fade_out);

        lv_anim_t drop_out;
        lv_anim_init(&drop_out);
        lv_anim_set_var(&drop_out, menu_overlay_);
        lv_anim_set_exec_cb(&drop_out, animSetTranslateY);
        lv_anim_set_values(&drop_out, 0, 4);
        lv_anim_set_duration(&drop_out, 120);
        lv_anim_set_path_cb(&drop_out, lv_anim_path_ease_in);
        lv_anim_start(&drop_out);
    }
}

void Kernel::updateAppMenu()
{
    if (!menu_ready_) {
        return;
    }

    if (menu_index_cached_ == menu_index_) {
        return;
    }

    const int previous_index = menu_index_cached_;
    menu_index_cached_ = menu_index_;

    for (size_t i = 0; i < kRouteCount; ++i) {
        lv_obj_t *tile = menu_items_[i];
        if (tile == nullptr) {
            continue;
        }
        const int current = static_cast<int>(i);
        const bool selected = (current == menu_index_);
        const bool previously_selected = (current == previous_index);
        if (!selected && !previously_selected) {
            continue;
        }
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
    apps_[static_cast<size_t>(Route::Graph)] = std::make_unique<GraphApp>(services_);
    apps_[static_cast<size_t>(Route::Files)] = std::make_unique<FilesApp>();
    apps_[static_cast<size_t>(Route::Project)] = std::make_unique<ProjectApp>(services_);
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
        app->releaseUi();
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
    lv_obj_set_size(status_bar_, board_->displayWidth(), board_->statusBarHeight());
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    ui_theme::applyPanel(status_bar_, LV_COLOR_MAKE(22, 30, 46), LV_COLOR_MAKE(22, 30, 46), 0, 4, 0, 0);

    if (board_->hasTouchInput()) {
        const lv_coord_t button_size = static_cast<lv_coord_t>(std::max(56, board_->statusBarHeight() - 8));
        const lv_coord_t gap = 4;

        status_launcher_btn_ = lv_button_create(status_bar_);
        lv_obj_set_size(status_launcher_btn_, button_size, button_size);
        lv_obj_align(status_launcher_btn_, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_add_event_cb(status_launcher_btn_, &Kernel::statusLauncherEventCb, LV_EVENT_CLICKED, this);
        lv_obj_t *launcher_icon = lv_label_create(status_launcher_btn_);
        ui_theme::applyText14(launcher_icon);
        lv_obj_set_style_text_font(launcher_icon, ui_theme::textFont16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(launcher_icon, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_label_set_text(launcher_icon, LV_SYMBOL_HOME);
        lv_obj_center(launcher_icon);

        status_app_menu_btn_ = lv_button_create(status_bar_);
        lv_obj_set_size(status_app_menu_btn_, button_size, button_size);
        lv_obj_align(status_app_menu_btn_, LV_ALIGN_LEFT_MID, 2 + button_size + gap, 0);
        lv_obj_add_event_cb(status_app_menu_btn_, &Kernel::statusAppMenuEventCb, LV_EVENT_CLICKED, this);
        lv_obj_t *app_menu_icon = lv_label_create(status_app_menu_btn_);
        ui_theme::applyText14(app_menu_icon);
        lv_obj_set_style_text_font(app_menu_icon, ui_theme::textFont16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(app_menu_icon, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_label_set_text(app_menu_icon, LV_SYMBOL_LIST);
        lv_obj_center(app_menu_icon);

#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        status_keyboard_btn_ = lv_button_create(status_bar_);
        lv_obj_set_size(status_keyboard_btn_, button_size, button_size);
        lv_obj_align(status_keyboard_btn_, LV_ALIGN_LEFT_MID, 2 + (button_size + gap) * 2, 0);
        lv_obj_add_event_cb(status_keyboard_btn_, &Kernel::statusKeyboardEventCb, LV_EVENT_CLICKED, this);
        lv_obj_t *keyboard_icon = lv_label_create(status_keyboard_btn_);
        ui_theme::applyText14(keyboard_icon);
        lv_obj_set_style_text_font(keyboard_icon, ui_theme::textFont16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(keyboard_icon, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_label_set_text(keyboard_icon, LV_SYMBOL_KEYBOARD);
        lv_obj_center(keyboard_icon);
#endif
    }

    status_left_ = lv_label_create(status_bar_);
    ui_theme::applyText14(status_left_);
    lv_obj_set_style_text_color(status_left_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
    lv_obj_align(status_left_, LV_ALIGN_LEFT_MID, board_->hasTouchInput() ? 184 : 0, 0);

    status_center_ = lv_label_create(status_bar_);
    ui_theme::applyText14(status_center_);
    lv_obj_set_style_text_color(status_center_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
    lv_obj_align(status_center_, LV_ALIGN_CENTER, 0, 0);

    status_right_ = lv_label_create(status_bar_);
    ui_theme::applyText14(status_right_);
    lv_obj_set_style_text_color(status_right_, LV_COLOR_MAKE(240, 244, 252), LV_PART_MAIN);
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
        route_text = "PROGRAM";
        break;
    case Route::Settings:
        route_text = "SETTINGS";
        break;
    }

    const settings::AppSettings cfg = settings::get();
    const bool fn_on = fn_locked_;
    const bool shift_on = shift_locked_;
    const bool busy = casService_.busy();
    const bool show_wifi = cfg.status_show_wifi;
    const bool show_memory = cfg.status_show_memory;
    const bool show_clock = cfg.status_show_clock;

    const uint32_t now_minute = show_clock ? static_cast<uint32_t>(std::time(nullptr) / 60) : 0U;
    const bool state_changed =
        status_route_cached_ != router_.current() ||
        status_fn_cached_ != fn_on ||
        status_shift_cached_ != shift_on ||
        status_busy_cached_ != busy ||
        status_wifi_cached_ != show_wifi ||
        status_memory_cached_ != show_memory ||
        status_memory_mb_cached_ != cfg.status_memory_mb ||
        status_clock_cached_ != show_clock ||
        (show_clock && status_clock_minute_cached_ != now_minute);

    if (!state_changed) {
        return;
    }

    status_route_cached_ = router_.current();
    status_fn_cached_ = fn_on;
    status_shift_cached_ = shift_on;
    status_busy_cached_ = busy;
    status_wifi_cached_ = show_wifi;
    status_memory_cached_ = show_memory;
    status_memory_mb_cached_ = cfg.status_memory_mb;
    status_clock_cached_ = show_clock;
    status_clock_minute_cached_ = now_minute;

    char left[48];
    char center[64];
    char right[24];

    std::snprintf(left, sizeof(left), "%s  RAD", route_text);
    const char *fn_token = fn_on ? "[FN] " : "";
    const char *shift_token = shift_on ? "[CAPS] " : "";
    char link_tokens[16] = {};
    if (show_wifi && wifiConnected()) {
        std::snprintf(link_tokens, sizeof(link_tokens), "Wi ");
    }
    std::snprintf(center, sizeof(center), "%s%s%s%s",
                  link_tokens,
                  fn_token,
                  shift_token,
                  busy ? "CAS*" : "");

    if (show_memory) {
        const uint32_t free_kb = static_cast<uint32_t>(
            (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) + heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024U);
        if (cfg.status_memory_mb) {
            std::snprintf(right, sizeof(right), "%luM", static_cast<unsigned long>((free_kb + 512U) / 1024U));
        } else {
            std::snprintf(right, sizeof(right), "%luK", static_cast<unsigned long>(free_kb));
        }
    } else if (show_clock) {
        std::time_t now = std::time(nullptr);
        std::tm tm_now = {};
        localtime_r(&now, &tm_now);
        std::snprintf(right, sizeof(right), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    } else {
        right[0] = '\0';
    }

    if (std::strcmp(status_left_cache_, left) != 0) {
        std::snprintf(status_left_cache_, sizeof(status_left_cache_), "%s", left);
        lv_label_set_text(status_left_, status_left_cache_);
    }
    if (std::strcmp(status_center_cache_, center) != 0) {
        std::snprintf(status_center_cache_, sizeof(status_center_cache_), "%s", center);
        lv_label_set_text(status_center_, status_center_cache_);
    }
    if (std::strcmp(status_right_cache_, right) != 0) {
        std::snprintf(status_right_cache_, sizeof(status_right_cache_), "%s", right);
        lv_label_set_text(status_right_, status_right_cache_);
    }

}

void Kernel::ensureGlobalKeyboard()
{
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
    if (global_keyboard_ != nullptr || !board_->hasTouchInput()) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    global_keyboard_ = lv_keyboard_create(screen);
    const lv_coord_t keyboard_h = static_cast<lv_coord_t>(std::max(260, board_->displayHeight() / 3));
    lv_obj_set_size(global_keyboard_, board_->displayWidth(), keyboard_h);
    lv_obj_align(global_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(global_keyboard_, ui_theme::textFont16(), LV_PART_ITEMS);
    lv_obj_add_event_cb(global_keyboard_, &Kernel::globalKeyboardEventCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(global_keyboard_, &Kernel::globalKeyboardEventCb, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(global_keyboard_, LV_OBJ_FLAG_HIDDEN);
#endif
}

bool Kernel::isObjectVisible(lv_obj_t *obj) const
{
    while (obj != nullptr) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        obj = lv_obj_get_parent(obj);
    }
    return true;
}

lv_obj_t *Kernel::findFocusedTextarea(lv_obj_t *root) const
{
    if (root == nullptr || !isObjectVisible(root)) {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_textarea_class) && lv_obj_has_state(root, LV_STATE_FOCUSED)) {
        return root;
    }
    const uint32_t child_count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        if (lv_obj_t *found = findFocusedTextarea(lv_obj_get_child(root, i)); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void Kernel::updateGlobalKeyboardBinding()
{
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
    if (!board_->hasTouchInput() || isCalcRoute(router_.current())) {
        return;
    }

    if (menu_open_ && global_keyboard_textarea_ == nullptr && !global_keyboard_visible_) {
        return;
    }

    bool raise_keyboard = false;

    if (global_keyboard_textarea_ != nullptr) {
        if (isObjectVisible(global_keyboard_textarea_) && lv_obj_has_state(global_keyboard_textarea_, LV_STATE_FOCUSED)) {
            ensureGlobalKeyboard();
            if (global_keyboard_ != nullptr) {
                if (!global_keyboard_visible_) {
                    global_keyboard_visible_ = true;
                    lv_obj_clear_flag(global_keyboard_, LV_OBJ_FLAG_HIDDEN);
                    raise_keyboard = true;
                }
                if (raise_keyboard) {
                    lv_obj_move_foreground(global_keyboard_);
                }
                return;
            }
        }

        global_keyboard_textarea_ = nullptr;
        if (global_keyboard_ != nullptr) {
            lv_keyboard_set_textarea(global_keyboard_, nullptr);
        }
    }

    if (global_keyboard_textarea_ == nullptr) {
        ++global_keyboard_poll_skip_;
        if ((global_keyboard_poll_skip_ & 0x03U) != 0U) {
            return;
        }
    } else {
        global_keyboard_poll_skip_ = 0;
    }

    lv_obj_t *textarea = findFocusedTextarea(lv_screen_active());
    if (textarea == nullptr) {
        return;
    }

    ensureGlobalKeyboard();
    if (global_keyboard_ == nullptr) {
        return;
    }

    if (textarea != global_keyboard_textarea_) {
        global_keyboard_textarea_ = textarea;
        global_keyboard_poll_skip_ = 0;
        lv_keyboard_set_textarea(global_keyboard_, textarea);
        raise_keyboard = true;
    }
    if (!global_keyboard_visible_) {
        global_keyboard_visible_ = true;
        lv_obj_clear_flag(global_keyboard_, LV_OBJ_FLAG_HIDDEN);
        raise_keyboard = true;
    }
    if (raise_keyboard) {
        lv_obj_move_foreground(global_keyboard_);
    }
#endif
}

void Kernel::bringSystemChromeToFront()
{
    if (status_bar_ != nullptr) {
        if (lv_obj_get_parent(status_bar_) != nullptr) {
            lv_obj_move_foreground(status_bar_);
        }
    }
    if (menu_open_ && menu_overlay_ != nullptr) {
        if (lv_obj_get_parent(menu_overlay_) != nullptr) {
            lv_obj_move_foreground(menu_overlay_);
        }
    }
}

void Kernel::statusLauncherEventCb(lv_event_t *e)
{
    auto *self = static_cast<Kernel *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    self->menu_open_ = !self->menu_open_;
    if (self->menu_open_) {
        self->menu_index_ = static_cast<int>(self->router_.current());
    }
    self->showAppMenu(self->menu_open_);
    self->bringSystemChromeToFront();
}

void Kernel::statusAppMenuEventCb(lv_event_t *e)
{
    auto *self = static_cast<Kernel *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    auto &app = self->apps_[static_cast<size_t>(self->router_.current())];
    if (app && app->handleMenuButton()) {
        if (self->menu_open_) {
            self->menu_open_ = false;
            self->showAppMenu(false);
        }
        self->bringSystemChromeToFront();
        return;
    }

    self->menu_open_ = !self->menu_open_;
    if (self->menu_open_) {
        self->menu_index_ = static_cast<int>(self->router_.current());
    }
    self->showAppMenu(self->menu_open_);
    self->bringSystemChromeToFront();
}

void Kernel::statusKeyboardEventCb(lv_event_t *e)
{
    auto *self = static_cast<Kernel *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    auto &app = self->apps_[static_cast<size_t>(self->router_.current())];
    if (app) {
        if (app->handleKeyboardToggle()) {
            self->bringSystemChromeToFront();
            return;
        }
    }

#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
    self->ensureGlobalKeyboard();
    if (self->global_keyboard_ != nullptr) {
        self->global_keyboard_visible_ = !self->global_keyboard_visible_;
        if (self->global_keyboard_visible_) {
            lv_obj_clear_flag(self->global_keyboard_, LV_OBJ_FLAG_HIDDEN);
            self->updateGlobalKeyboardBinding();
            lv_obj_move_foreground(self->global_keyboard_);
        } else {
            lv_obj_add_flag(self->global_keyboard_, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
    self->bringSystemChromeToFront();
}

void Kernel::globalKeyboardEventCb(lv_event_t *e)
{
    auto *self = static_cast<Kernel *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    auto &app = self->apps_[static_cast<size_t>(self->router_.current())];
    if (app) {
        app->handleMappedKey(code == LV_EVENT_READY ? LV_KEY_ENTER : LV_KEY_ESC);
    }
    if (self->global_keyboard_ != nullptr) {
        self->global_keyboard_visible_ = false;
        lv_obj_add_flag(self->global_keyboard_, LV_OBJ_FLAG_HIDDEN);
    }
}

void Kernel::drawBootSplash()
{
    if (board_->usesExternalLvglPort()) {
        lv_obj_t *screen = lv_screen_active();
        if (screen != nullptr) {
            ui_theme::applyPage(screen, LV_COLOR_MAKE(20, 20, 20));
        }
        ESP_LOGI(kTag, "boot splash skipped; external LVGL port active");
        return;
    }

    std::vector<uint16_t> line(static_cast<size_t>(board_->displayWidth()));
    for (int y = 0; y < board_->displayHeight(); ++y) {
        uint16_t color = rgb565(20, 20, 20);
        if (y < board_->displayHeight() / 3) {
            color = rgb565(220, 40, 40);
        } else if (y < (board_->displayHeight() * 2) / 3) {
            color = rgb565(40, 190, 70);
        } else {
            color = rgb565(40, 110, 220);
        }
        std::fill(line.begin(), line.end(), color);
        board_->presentArea(0, y, board_->displayWidth(), y + 1, line.data());
    }
    ESP_LOGI(kTag, "boot splash rendered");
}

} // namespace brookesia
