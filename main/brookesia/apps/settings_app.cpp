#include "brookesia/apps/settings_app.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#if CONFIG_XCAS_ENABLE_WIFI
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#endif

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#if CONFIG_XCAS_ENABLE_BLUETOOTH
#include "esp_bt.h"
#if CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt_main.h"
#endif
#endif

#include "cardputer_bsp.hpp"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/app_settings.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

#if CONFIG_XCAS_ENABLE_WIFI
constexpr bool kWifiBuilt = true;
#else
constexpr bool kWifiBuilt = false;
#endif

#if CONFIG_XCAS_ENABLE_BLUETOOTH
constexpr bool kBtBuilt = true;
#else
constexpr bool kBtBuilt = false;
#endif

const char *const kAngleValues[] = {"RAD", "DEG"};
constexpr int kAngleCount = 2;

const int kDigitsValues[] = {12, 15, 20, 30};
constexpr int kDigitsCount = 4;

const char *const kPreviewModeValues[] = {"Graphic", "Text"};
constexpr int kPreviewModeCount = 2;

bool isSdMounted()
{
    struct stat st = {};
    return stat("/sdcard", &st) == 0;
}

sdmmc_card_t *s_sd_card = nullptr;

bool mountExternalSd()
{
    (void)s_sd_card;
    return isSdMounted();
}

void unmountExternalSd()
{
    if (s_sd_card != nullptr) {
        esp_vfs_fat_sdcard_unmount("/sdcard", s_sd_card);
        s_sd_card = nullptr;
    }
}

void maskPassword(const char *src, char *dst, size_t dst_size)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == nullptr || src[0] == '\0') {
        std::snprintf(dst, dst_size, "(empty)");
        return;
    }

    const size_t n = std::strlen(src);
    const size_t stars = (n < (dst_size - 1)) ? n : (dst_size - 1);
    for (size_t i = 0; i < stars; ++i) {
        dst[i] = '*';
    }
    dst[stars] = '\0';
}

#if CONFIG_XCAS_ENABLE_WIFI
bool s_wifi_initialized = false;

bool wifiInitOnce()
{
    if (s_wifi_initialized) {
        return true;
    }

    if (esp_netif_init() != ESP_OK) {
        return false;
    }
    const esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) {
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return false;
    }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        return false;
    }
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }

    s_wifi_initialized = true;
    return true;
}
#endif

} // namespace

SettingsApp::SettingsApp(ServiceHub &services) : services_(services), cas_(services.casService())
{
}

bool SettingsApp::init()
{
    return true;
}

void SettingsApp::ensureUi()
{
    if (ui_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    const lv_coord_t w = static_cast<lv_coord_t>(services_.board().displayWidth());
    const lv_coord_t h = static_cast<lv_coord_t>(services_.board().displayHeight());
    const lv_coord_t status_h = static_cast<lv_coord_t>(services_.board().statusBarHeight());
    const bool touch = services_.board().hasTouchInput();

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h - status_h);
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, status_h);
    ui_theme::applyPage(root_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_set_style_pad_all(root_, touch ? 10 : 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    menu_ = lv_menu_create(root_);
    lv_obj_set_size(menu_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(menu_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_menu_set_mode_header(menu_, LV_MENU_HEADER_TOP_FIXED);
    lv_menu_set_mode_root_back_button(menu_, LV_MENU_ROOT_BACK_BUTTON_ENABLED);

    group_ = lv_group_create();
    lv_group_set_editing(group_, false);

    root_page_ = lv_menu_page_create(menu_, "System");
    calc_page_ = lv_menu_page_create(menu_, "Calculator");
    wifi_page_ = lv_menu_page_create(menu_, "WiFi");
    bt_page_ = lv_menu_page_create(menu_, "Bluetooth");
    clock_page_ = lv_menu_page_create(menu_, "Clock");
    status_page_ = lv_menu_page_create(menu_, "Status bar");

    MenuEntry *calc_root = addMenuEntry(root_page_, Page::Root, Item::CalcPage, "Calculator");
    MenuEntry *wifi_root = addMenuEntry(root_page_, Page::Root, Item::WifiPage, "WiFi");
    MenuEntry *bt_root = addMenuEntry(root_page_, Page::Root, Item::BtPage, "Bluetooth");
    MenuEntry *clock_root = addMenuEntry(root_page_, Page::Root, Item::ClockPage, "Clock");
    MenuEntry *status_root = addMenuEntry(root_page_, Page::Root, Item::StatusBarPage, "Status bar");
    addMenuEntry(root_page_, Page::Root, Item::SdCard, "SD card");
    addMenuEntry(root_page_, Page::Root, Item::Memory, "Memory");
    addMenuEntry(root_page_, Page::Root, Item::ClearSession, "Clear session");

    if (calc_root != nullptr) {
        lv_menu_set_load_page_event(menu_, calc_root->row, calc_page_);
    }
    if (wifi_root != nullptr) {
        lv_menu_set_load_page_event(menu_, wifi_root->row, wifi_page_);
    }
    if (bt_root != nullptr) {
        lv_menu_set_load_page_event(menu_, bt_root->row, bt_page_);
    }
    if (clock_root != nullptr) {
        lv_menu_set_load_page_event(menu_, clock_root->row, clock_page_);
    }
    if (status_root != nullptr) {
        lv_menu_set_load_page_event(menu_, status_root->row, status_page_);
    }

    addMenuEntry(calc_page_, Page::Calculator, Item::Angle, "Angle mode");
    addMenuEntry(calc_page_, Page::Calculator, Item::Precision, "Precision");
    addMenuEntry(calc_page_, Page::Calculator, Item::FormulaPreviewMode, "Formula preview");
    addMenuEntry(calc_page_, Page::Calculator, Item::FnSwitch, "Fn app switch", true);

    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiEnable, "WiFi power", true);
    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiScan, "Scan networks");
    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiConnect, "Connect saved");
    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiSsid, "SSID");
    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiPassword, "Password");
    addMenuEntry(wifi_page_, Page::Wifi, Item::WifiStatus, "Status");

    addMenuEntry(bt_page_, Page::Bluetooth, Item::BtEnable, "Bluetooth power", true);
    addMenuEntry(bt_page_, Page::Bluetooth, Item::BtScan, "Scan devices");
    addMenuEntry(bt_page_, Page::Bluetooth, Item::BtConnect, "Connect saved");
    addMenuEntry(bt_page_, Page::Bluetooth, Item::BtStatus, "Status");

    addMenuEntry(clock_page_, Page::Clock, Item::ClockDate, "Date");
    addMenuEntry(clock_page_, Page::Clock, Item::ClockTime, "Time");
    addMenuEntry(clock_page_, Page::Clock, Item::ClockSync, "Sync network");

    addMenuEntry(status_page_, Page::StatusBar, Item::StatusWifiIcon, "WiFi icon", true);
    addMenuEntry(status_page_, Page::StatusBar, Item::StatusBtIcon, "Bluetooth icon", true);
    addMenuEntry(status_page_, Page::StatusBar, Item::StatusMemory, "Free memory", true);
    addMenuEntry(status_page_, Page::StatusBar, Item::StatusMemoryUnit, "Memory unit");
    addMenuEntry(status_page_, Page::StatusBar, Item::StatusClock, "Clock", true);

    lv_menu_set_page(menu_, root_page_);

    ui_ready_ = true;
}

SettingsApp::MenuEntry *SettingsApp::addMenuEntry(lv_obj_t *page_obj, Page page, Item item,
                                                  const char *name, bool with_toggle)
{
    if (entry_count_ >= kMaxMenuEntries) {
        return nullptr;
    }

    MenuEntry &entry = entries_[entry_count_++];
    entry.item = item;
    entry.page = page;
    entry.row = lv_menu_cont_create(page_obj);
    lv_obj_add_flag(entry.row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(entry.row, &SettingsApp::menuEntryEventCb, LV_EVENT_ALL, this);
    const bool touch = services_.board().hasTouchInput();
    lv_obj_set_height(entry.row, touch ? 52 : 24);
    lv_obj_set_width(entry.row, lv_pct(100));
    lv_obj_set_flex_flow(entry.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(entry.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_theme::applyRowCard(entry.row, LV_COLOR_MAKE(208, 214, 224), touch ? 8 : 4, touch ? 12 : 7, touch ? 12 : 7);

    entry.name = lv_label_create(entry.row);
    lv_obj_add_flag(entry.name, LV_OBJ_FLAG_EVENT_BUBBLE);
    ui_theme::applyText14(entry.name);
    lv_label_set_text(entry.name, name);
    lv_obj_set_flex_grow(entry.name, 1);

    if (with_toggle) {
        entry.toggle = lv_switch_create(entry.row);
        lv_obj_add_flag(entry.toggle, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(entry.toggle, touch ? 54 : 28, touch ? 30 : 16);
    } else {
        entry.value = lv_label_create(entry.row);
        lv_obj_add_flag(entry.value, LV_OBJ_FLAG_EVENT_BUBBLE);
        ui_theme::applyText14(entry.value);
        lv_label_set_long_mode(entry.value, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_max_width(entry.value, touch ? 260 : 132, LV_PART_MAIN);
    }

    return &entry;
}

void SettingsApp::menuEntryEventCb(lv_event_t *e)
{
    auto *self = static_cast<SettingsApp *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    lv_obj_t *target = lv_event_get_target_obj(e);
    MenuEntry *entry = self->findEntryByRow(target);
    if (entry == nullptr || entry->disabled) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        self->selected_item_ = entry->item;
        self->refreshMenu();
    } else if (code == LV_EVENT_CLICKED) {
        self->selected_item_ = entry->item;
        self->activateSelected();
        self->refreshMenu();
    } else if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            self->selected_item_ = entry->item;
            self->activateSelected();
            self->refreshMenu();
        }
    }
}

SettingsApp::MenuEntry *SettingsApp::findEntry(Item item)
{
    for (int i = 0; i < entry_count_; ++i) {
        if (entries_[i].item == item) {
            return &entries_[i];
        }
    }
    return nullptr;
}

SettingsApp::MenuEntry *SettingsApp::findEntryByRow(lv_obj_t *row)
{
    for (int i = 0; i < entry_count_; ++i) {
        if (entries_[i].row == row) {
            return &entries_[i];
        }
    }
    return nullptr;
}

bool SettingsApp::isSelectable(const MenuEntry &entry) const
{
    return entry.page == current_page_ && !entry.disabled;
}

void SettingsApp::ensureSelectionOnPage()
{
    for (int i = 0; i < entry_count_; ++i) {
        if (entries_[i].item == selected_item_ && isSelectable(entries_[i])) {
            return;
        }
    }
    for (int i = 0; i < entry_count_; ++i) {
        if (isSelectable(entries_[i])) {
            selected_item_ = entries_[i].item;
            return;
        }
    }
}

void SettingsApp::setCurrentPage(Page page)
{
    current_page_ = page;
    if (menu_ != nullptr) {
        lv_obj_t *page_obj = root_page_;
        if (page == Page::Calculator) {
            page_obj = calc_page_;
        } else if (page == Page::Wifi) {
            page_obj = wifi_page_;
        } else if (page == Page::Bluetooth) {
            page_obj = bt_page_;
        } else if (page == Page::Clock) {
            page_obj = clock_page_;
        } else if (page == Page::StatusBar) {
            page_obj = status_page_;
        }
        lv_menu_set_page(menu_, page_obj);
    }
    ensureSelectionOnPage();
    syncFocusGroup();
}

void SettingsApp::refreshMenu()
{
    if (!ui_ready_) {
        return;
    }

    if (MenuEntry *entry = findEntry(Item::Angle); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kAngleValues[angle_index_]);
    }

    char digbuf[16];
    std::snprintf(digbuf, sizeof(digbuf), "%d", kDigitsValues[digits_index_]);
    if (MenuEntry *entry = findEntry(Item::Precision); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, digbuf);
    }

    if (MenuEntry *entry = findEntry(Item::FormulaPreviewMode); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kPreviewModeValues[formula_preview_mode_]);
    }

    if (MenuEntry *entry = findEntry(Item::FnSwitch); entry != nullptr && entry->toggle != nullptr) {
        if (fn_app_switch_enabled_) {
            lv_obj_add_state(entry->toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(entry->toggle, LV_STATE_CHECKED);
        }
    }

    if (MenuEntry *entry = findEntry(Item::CalcPage); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kAngleValues[angle_index_]);
    }

    if (MenuEntry *entry = findEntry(Item::WifiPage); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kWifiBuilt ? wifi_state_text_.data() : "Unavailable");
    }
    if (MenuEntry *entry = findEntry(Item::WifiEnable); entry != nullptr && entry->toggle != nullptr) {
        if (wifi_enabled_ && kWifiBuilt) {
            lv_obj_add_state(entry->toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(entry->toggle, LV_STATE_CHECKED);
        }
    }

    settings::AppSettings cfg = settings::get();
    std::string ssid_text;
    if (editing_ && edit_item_ == Item::WifiSsid) {
        ssid_text = std::string("EDIT:") + edit_buffer_ + "_";
    } else if (cfg.wifi_ssid[0] == '\0') {
        ssid_text = "(empty)";
    } else {
        ssid_text = cfg.wifi_ssid;
    }
    if (MenuEntry *entry = findEntry(Item::WifiSsid); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, ssid_text.c_str());
    }

    char pass_masked[65];
    std::string pass_text;
    if (editing_ && edit_item_ == Item::WifiPassword) {
        pass_text = std::string("EDIT:") + edit_buffer_ + "_";
    } else {
        maskPassword(cfg.wifi_pass, pass_masked, sizeof(pass_masked));
        pass_text = pass_masked;
    }
    if (MenuEntry *entry = findEntry(Item::WifiPassword); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, pass_text.c_str());
    }
    if (MenuEntry *entry = findEntry(Item::WifiStatus); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kWifiBuilt ? wifi_state_text_.data() : "Unavailable");
    }

    if (MenuEntry *entry = findEntry(Item::BtPage); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kBtBuilt ? bt_state_text_.data() : "Unavailable");
    }
    if (MenuEntry *entry = findEntry(Item::BtEnable); entry != nullptr && entry->toggle != nullptr) {
        if (bt_hid_enabled_ && kBtBuilt) {
            lv_obj_add_state(entry->toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(entry->toggle, LV_STATE_CHECKED);
        }
    }
    if (MenuEntry *entry = findEntry(Item::BtStatus); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kBtBuilt ? bt_state_text_.data() : "Unavailable");
    }

    if (MenuEntry *entry = findEntry(Item::SdCard); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, sd_state_text_.data());
    }
    if (MenuEntry *entry = findEntry(Item::ClearSession); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, "calc+graph");
    }

    char date_buf[16];
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", clock_year_, clock_month_, clock_day_);
    if (MenuEntry *entry = findEntry(Item::ClockDate); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, date_buf);
    }
    char time_buf[12];
    std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", clock_hour_, clock_minute_);
    if (MenuEntry *entry = findEntry(Item::ClockTime); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, time_buf);
    }
    if (MenuEntry *entry = findEntry(Item::ClockSync); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, (kWifiBuilt && wifi_connected_) ? "online" : "WiFi needed");
    }
    if (MenuEntry *entry = findEntry(Item::ClockPage); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, time_buf);
    }

    auto setToggle = [this](Item item, bool enabled) {
        if (MenuEntry *entry = findEntry(item); entry != nullptr && entry->toggle != nullptr) {
            if (enabled) {
                lv_obj_add_state(entry->toggle, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(entry->toggle, LV_STATE_CHECKED);
            }
        }
    };
    setToggle(Item::StatusWifiIcon, status_show_wifi_);
    setToggle(Item::StatusBtIcon, status_show_bt_);
    setToggle(Item::StatusMemory, status_show_memory_);
    setToggle(Item::StatusClock, status_show_clock_);
    if (MenuEntry *entry = findEntry(Item::StatusMemoryUnit); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, status_memory_mb_ ? "MB" : "KB");
    }

    for (int i = 0; i < entry_count_; ++i) {
        MenuEntry &entry = entries_[i];
        entry.disabled = false;
        if ((entry.item == Item::WifiPage || entry.item == Item::WifiEnable ||
             entry.item == Item::WifiScan || entry.item == Item::WifiConnect ||
             entry.item == Item::WifiSsid || entry.item == Item::WifiPassword ||
             entry.item == Item::WifiStatus) &&
            !kWifiBuilt) {
            entry.disabled = true;
        }
        if ((entry.item == Item::BtPage || entry.item == Item::BtEnable ||
             entry.item == Item::BtScan || entry.item == Item::BtConnect ||
             entry.item == Item::BtStatus) &&
            !kBtBuilt) {
            entry.disabled = true;
        }
        if (entry.item == Item::ClockSync && (!kWifiBuilt || !wifi_connected_)) {
            entry.disabled = true;
        }
    }

    ensureSelectionOnPage();

    for (int i = 0; i < entry_count_; ++i) {
        MenuEntry &entry = entries_[i];
        const bool sel = (entry.page == current_page_ && entry.item == selected_item_);
        lv_obj_set_style_bg_opa(entry.row, sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(entry.row, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
        lv_color_t text_col;
        if (sel) {
            text_col = LV_COLOR_MAKE(255, 255, 255);
        } else if (entry.disabled) {
            text_col = LV_COLOR_MAKE(130, 136, 144);
        } else {
            text_col = LV_COLOR_MAKE(16, 24, 36);
        }
        lv_obj_set_style_text_color(entry.name, text_col, LV_PART_MAIN);
        if (entry.value != nullptr) {
            lv_obj_set_style_text_color(entry.value, text_col, LV_PART_MAIN);
        }
        if (entry.toggle != nullptr) {
            if (entry.disabled) {
                lv_obj_add_state(entry.toggle, LV_STATE_DISABLED);
            } else {
                lv_obj_remove_state(entry.toggle, LV_STATE_DISABLED);
            }
        }
        if (entry.disabled) {
            lv_obj_add_state(entry.row, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(entry.row, LV_STATE_DISABLED);
        }
    }

    if (MenuEntry *entry = findEntry(selected_item_); entry != nullptr && entry->row != nullptr) {
        lv_obj_scroll_to_view(entry->row, LV_ANIM_ON);
    }
}

void SettingsApp::syncFocusGroup()
{
    if (group_ == nullptr) {
        return;
    }
    lv_group_remove_all_objs(group_);
    for (int i = 0; i < entry_count_; ++i) {
        if (isSelectable(entries_[i]) && entries_[i].row != nullptr) {
            lv_group_add_obj(group_, entries_[i].row);
        }
    }
    focusSelected();
}

void SettingsApp::focusSelected()
{
    if (group_ == nullptr) {
        return;
    }
    if (MenuEntry *entry = findEntry(selected_item_); entry != nullptr && isSelectable(*entry)) {
        lv_group_focus_obj(entry->row);
    }
}

void SettingsApp::applyHorizontalAction(int dir)
{
    if (selected_item_ == Item::Angle) {
        angle_index_ = (angle_index_ + kAngleCount + dir) % kAngleCount;
        applyAngle();
        settings::AppSettings cfg = settings::get();
        cfg.angle_index = angle_index_;
        settings::set(cfg);
        settings::save();
    } else if (selected_item_ == Item::Precision) {
        digits_index_ = (digits_index_ + kDigitsCount + dir) % kDigitsCount;
        applyDigits();
        settings::AppSettings cfg = settings::get();
        cfg.digits_index = digits_index_;
        settings::set(cfg);
        settings::save();
    } else if (selected_item_ == Item::FormulaPreviewMode) {
        formula_preview_mode_ = (formula_preview_mode_ + kPreviewModeCount + dir) % kPreviewModeCount;
        settings::AppSettings cfg = settings::get();
        cfg.formula_preview_mode = formula_preview_mode_;
        settings::set(cfg);
        settings::save();
    } else if (selected_item_ == Item::FnSwitch) {
        fn_app_switch_enabled_ = !fn_app_switch_enabled_;
        applyFnSwitch();
    } else if (selected_item_ == Item::WifiEnable && kWifiBuilt) {
        wifi_enabled_ = !wifi_enabled_;
        applyWifi();
    } else if (selected_item_ == Item::BtEnable && kBtBuilt) {
        bt_hid_enabled_ = !bt_hid_enabled_;
        applyBtHid();
    } else if (selected_item_ == Item::SdCard) {
        if (sd_ready_) {
            unmountExternalSd();
        } else {
            (void)mountExternalSd();
        }
        sd_ready_ = isSdMounted();
        std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s",
                      sd_ready_ ? "mounted FATFS" : "not mounted");
    } else if (selected_item_ == Item::ClockDate) {
        adjustClockDate(dir);
    } else if (selected_item_ == Item::ClockTime) {
        adjustClockTime(dir);
    } else if (selected_item_ == Item::StatusWifiIcon) {
        status_show_wifi_ = !status_show_wifi_;
        applyStatusSettings();
    } else if (selected_item_ == Item::StatusBtIcon) {
        status_show_bt_ = !status_show_bt_;
        applyStatusSettings();
    } else if (selected_item_ == Item::StatusMemory) {
        status_show_memory_ = !status_show_memory_;
        applyStatusSettings();
    } else if (selected_item_ == Item::StatusMemoryUnit) {
        status_memory_mb_ = !status_memory_mb_;
        applyStatusSettings();
    } else if (selected_item_ == Item::StatusClock) {
        status_show_clock_ = !status_show_clock_;
        applyStatusSettings();
    }
}

void SettingsApp::activateSelected()
{
    MenuEntry *entry = findEntry(selected_item_);
    if (entry == nullptr || entry->disabled) {
        return;
    }

    if (selected_item_ == Item::CalcPage) {
        setCurrentPage(Page::Calculator);
    } else if (selected_item_ == Item::WifiPage) {
        setCurrentPage(Page::Wifi);
    } else if (selected_item_ == Item::BtPage) {
        setCurrentPage(Page::Bluetooth);
    } else if (selected_item_ == Item::ClockPage) {
        setCurrentPage(Page::Clock);
    } else if (selected_item_ == Item::StatusBarPage) {
        setCurrentPage(Page::StatusBar);
    } else if (selected_item_ == Item::WifiEnable || selected_item_ == Item::BtEnable ||
               selected_item_ == Item::FnSwitch || selected_item_ == Item::SdCard ||
               selected_item_ == Item::Angle || selected_item_ == Item::Precision ||
               selected_item_ == Item::FormulaPreviewMode ||
               selected_item_ == Item::ClockDate || selected_item_ == Item::ClockTime ||
               selected_item_ == Item::StatusWifiIcon || selected_item_ == Item::StatusBtIcon ||
               selected_item_ == Item::StatusMemory || selected_item_ == Item::StatusMemoryUnit ||
               selected_item_ == Item::StatusClock) {
        applyHorizontalAction(1);
    } else if (selected_item_ == Item::WifiScan) {
        scanWifi();
    } else if (selected_item_ == Item::WifiConnect) {
        connectWifi();
    } else if (selected_item_ == Item::WifiSsid || selected_item_ == Item::WifiPassword) {
        beginEdit(selected_item_);
    } else if (selected_item_ == Item::BtScan) {
        scanBluetooth();
    } else if (selected_item_ == Item::BtConnect) {
        connectBluetooth();
    } else if (selected_item_ == Item::ClockSync) {
        syncClockNetwork();
    } else if (selected_item_ == Item::ClearSession) {
        clearSession();
    }
}

void SettingsApp::clearSession()
{
    if (!ensureStorageMounted()) {
        return;
    }
    std::remove("/data/calc_session.txt");
    std::remove("/data/graph_session.txt");
}

void SettingsApp::adjustClockDate(int dir)
{
    clock_day_ += dir;
    if (clock_day_ < 1) {
        clock_day_ = 31;
        --clock_month_;
    } else if (clock_day_ > 31) {
        clock_day_ = 1;
        ++clock_month_;
    }
    if (clock_month_ < 1) {
        clock_month_ = 12;
        --clock_year_;
    } else if (clock_month_ > 12) {
        clock_month_ = 1;
        ++clock_year_;
    }
    if (clock_year_ < 2024) {
        clock_year_ = 2099;
    } else if (clock_year_ > 2099) {
        clock_year_ = 2024;
    }
    applyClockToSystem();
}

void SettingsApp::adjustClockTime(int dir)
{
    clock_minute_ += dir;
    if (clock_minute_ < 0) {
        clock_minute_ = 59;
        --clock_hour_;
    } else if (clock_minute_ > 59) {
        clock_minute_ = 0;
        ++clock_hour_;
    }
    if (clock_hour_ < 0) {
        clock_hour_ = 23;
    } else if (clock_hour_ > 23) {
        clock_hour_ = 0;
    }
    applyClockToSystem();
}

void SettingsApp::applyClockToSystem()
{
    settings::AppSettings cfg = settings::get();
    cfg.clock_year = clock_year_;
    cfg.clock_month = clock_month_;
    cfg.clock_day = clock_day_;
    cfg.clock_hour = clock_hour_;
    cfg.clock_minute = clock_minute_;
    settings::set(cfg);
    settings::save();

    std::tm tm_value = {};
    tm_value.tm_year = clock_year_ - 1900;
    tm_value.tm_mon = clock_month_ - 1;
    tm_value.tm_mday = clock_day_;
    tm_value.tm_hour = clock_hour_;
    tm_value.tm_min = clock_minute_;
    tm_value.tm_sec = 0;
    const std::time_t epoch = std::mktime(&tm_value);
    if (epoch > 0) {
        timeval tv = {};
        tv.tv_sec = epoch;
        settimeofday(&tv, nullptr);
    }
}

void SettingsApp::syncClockNetwork()
{
#if CONFIG_XCAS_ENABLE_WIFI
    if (!wifi_connected_) {
        return;
    }
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "SNTP sync");
#endif
}

void SettingsApp::applyStatusSettings()
{
    settings::AppSettings cfg = settings::get();
    cfg.status_show_wifi = status_show_wifi_;
    cfg.status_show_bt = status_show_bt_;
    cfg.status_show_memory = status_show_memory_;
    cfg.status_memory_mb = status_memory_mb_;
    cfg.status_show_clock = status_show_clock_;
    settings::set(cfg);
    settings::save();
}

void SettingsApp::applyAngle()
{
    cas_.submit(angle_index_ == 0 ? "angle_radian:=1" : "angle_radian:=0");
}

void SettingsApp::applyDigits()
{
    char cmd[24];
    std::snprintf(cmd, sizeof(cmd), "DIGITS:=%d", kDigitsValues[digits_index_]);
    cas_.submit(cmd);
}

void SettingsApp::applyFnSwitch()
{
    settings::AppSettings cfg = settings::get();
    cfg.fn_app_switch_enabled = fn_app_switch_enabled_;
    settings::set(cfg);
    settings::save();
}

void SettingsApp::applyWifi()
{
    settings::AppSettings cfg = settings::get();
    cfg.wifi_enabled = wifi_enabled_;
    settings::set(cfg);
    settings::save();

#if CONFIG_XCAS_ENABLE_WIFI
    if (!wifi_enabled_) {
        wifi_connected_ = false;
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "OFF");
        if (s_wifi_initialized) {
            esp_wifi_disconnect();
        }
        return;
    }

    if (!wifiInitOnce()) {
        wifi_connected_ = false;
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "INIT FAIL");
        return;
    }
    if (cfg.wifi_ssid[0] == '\0') {
        wifi_connected_ = false;
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "NO SSID");
        return;
    }

    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "CONNECTING...");

    wifi_config_t wifi_cfg = {};
    std::strncpy(reinterpret_cast<char *>(wifi_cfg.sta.ssid), cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1] = '\0';
    std::strncpy(reinterpret_cast<char *>(wifi_cfg.sta.password), cfg.wifi_pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1] = '\0';
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        wifi_connected_ = false;
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "CFG FAIL");
        return;
    }
    if (esp_wifi_connect() != ESP_OK) {
        wifi_connected_ = false;
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "CONNECT FAIL");
        return;
    }

    wifi_connected_ = false;
    for (int i = 0; i < 25; ++i) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            wifi_connected_ = true;
            std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "CONNECTED");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!wifi_connected_) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "TIMEOUT");
    }
#else
    wifi_connected_ = false;
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "Unavailable");
#endif
}

void SettingsApp::scanWifi()
{
#if CONFIG_XCAS_ENABLE_WIFI
    if (!wifi_enabled_) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "WiFi OFF");
        return;
    }
    if (!wifiInitOnce()) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "INIT FAIL");
        return;
    }

    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "SCANNING...");
    wifi_scan_config_t scan_cfg = {};
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "SCAN FAIL");
        return;
    }

    uint16_t ap_count = 0;
    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "SCAN EMPTY");
        return;
    }
    if (ap_count == 0) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "0 AP");
        return;
    }

    wifi_ap_record_t ap = {};
    uint16_t one = 1;
    if (esp_wifi_scan_get_ap_records(&one, &ap) == ESP_OK && one > 0) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "%u AP %s",
                      static_cast<unsigned>(ap_count), reinterpret_cast<const char *>(ap.ssid));
    } else {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "%u AP",
                      static_cast<unsigned>(ap_count));
    }
#else
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "Unavailable");
#endif
}

void SettingsApp::connectWifi()
{
    if (!kWifiBuilt) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "Unavailable");
        return;
    }
    wifi_enabled_ = true;
    applyWifi();
}

void SettingsApp::applyBtHid()
{
    settings::AppSettings cfg = settings::get();
    cfg.bt_hid_enabled = bt_hid_enabled_;
    settings::set(cfg);
    settings::save();

#if CONFIG_XCAS_ENABLE_BLUETOOTH
    bt_ready_ = false;
    if (bt_hid_enabled_) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
            esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
                return;
            }
        }
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
                return;
            }
        }
#if CONFIG_BT_BLUEDROID_ENABLED
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
            if (esp_bluedroid_init() != ESP_OK) {
                return;
            }
        }
        if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
            if (esp_bluedroid_enable() != ESP_OK) {
                return;
            }
        }
#endif
        bt_ready_ = true;
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "ON stack");
    } else {
#if CONFIG_BT_BLUEDROID_ENABLED
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
            esp_bluedroid_disable();
        }
        if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
            esp_bluedroid_deinit();
        }
#endif
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_bt_controller_disable();
        }
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "OFF");
    }
#else
    bt_ready_ = false;
    std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "Unavailable");
#endif
}

void SettingsApp::scanBluetooth()
{
    if (!kBtBuilt) {
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "Unavailable");
        return;
    }
    if (!bt_hid_enabled_) {
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "BT OFF");
        return;
    }
    std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "SCAN READY");
}

void SettingsApp::connectBluetooth()
{
    if (!kBtBuilt) {
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "Unavailable");
        return;
    }
    bt_hid_enabled_ = true;
    applyBtHid();
    if (bt_ready_) {
        std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "PAIR READY");
    }
}

void SettingsApp::refreshWifiStatus()
{
    if (!kWifiBuilt) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "Unavailable");
        return;
    }
    if (!wifi_enabled_) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "OFF");
        return;
    }
    if (wifi_connected_) {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "CONNECTED");
    } else if (wifi_state_text_[0] == '\0') {
        std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "IDLE");
    }
}

void SettingsApp::beginEdit(Item item)
{
    if (!kWifiBuilt) {
        return;
    }
    settings::AppSettings cfg = settings::get();
    editing_ = true;
    edit_item_ = item;
    edit_length_ = 0;
    edit_buffer_[0] = '\0';
    if (item == Item::WifiSsid) {
        std::snprintf(edit_buffer_, sizeof(edit_buffer_), "%s", cfg.wifi_ssid);
    } else if (item == Item::WifiPassword) {
        std::snprintf(edit_buffer_, sizeof(edit_buffer_), "%s", cfg.wifi_pass);
    }
    edit_length_ = static_cast<int>(std::strlen(edit_buffer_));
}

void SettingsApp::commitEdit()
{
    if (!editing_) {
        return;
    }

    settings::AppSettings cfg = settings::get();
    if (edit_item_ == Item::WifiSsid) {
        std::strncpy(cfg.wifi_ssid, edit_buffer_, sizeof(cfg.wifi_ssid) - 1);
        cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    } else if (edit_item_ == Item::WifiPassword) {
        std::strncpy(cfg.wifi_pass, edit_buffer_, sizeof(cfg.wifi_pass) - 1);
        cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
    }
    settings::set(cfg);
    settings::save();

    editing_ = false;
    edit_buffer_[0] = '\0';
    edit_length_ = 0;

    if (wifi_enabled_) {
        applyWifi();
    }
}

void SettingsApp::cancelEdit()
{
    editing_ = false;
    edit_buffer_[0] = '\0';
    edit_length_ = 0;
}

void SettingsApp::onFocus()
{
    settings::load();
    const settings::AppSettings cfg = settings::get();

    angle_index_ = (cfg.angle_index == 1) ? 1 : 0;
    digits_index_ = (cfg.digits_index >= 0 && cfg.digits_index < kDigitsCount) ? cfg.digits_index : 0;
    formula_preview_mode_ = (cfg.formula_preview_mode >= 0 && cfg.formula_preview_mode < kPreviewModeCount) ? cfg.formula_preview_mode : 0;
    fn_app_switch_enabled_ = cfg.fn_app_switch_enabled;
    wifi_enabled_ = kWifiBuilt && cfg.wifi_enabled;
    bt_hid_enabled_ = kBtBuilt && cfg.bt_hid_enabled;
    status_show_wifi_ = cfg.status_show_wifi;
    status_show_bt_ = cfg.status_show_bt;
    status_show_memory_ = cfg.status_show_memory;
    status_memory_mb_ = cfg.status_memory_mb;
    status_show_clock_ = cfg.status_show_clock;
    clock_year_ = cfg.clock_year;
    clock_month_ = cfg.clock_month;
    clock_day_ = cfg.clock_day;
    clock_hour_ = cfg.clock_hour;
    clock_minute_ = cfg.clock_minute;
    sd_ready_ = isSdMounted();
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "%s", kWifiBuilt ? "IDLE" : "Unavailable");
    std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "%s",
                  kBtBuilt ? (bt_hid_enabled_ ? "ON stack" : "OFF") : "Unavailable");
    std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s", sd_ready_ ? "mounted FATFS" : "not mounted");

    ensureUi();
    if (group_ != nullptr) {
        lv_group_set_default(group_);
    }
    setCurrentPage(Page::Root);
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }

    applyAngle();
    applyDigits();
    applyWifi();
    applyBtHid();

    refreshMenu();
}

void SettingsApp::onBlur()
{
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::releaseUi()
{
    if (group_ != nullptr) {
        lv_group_delete(group_);
        group_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    menu_ = nullptr;
    root_page_ = nullptr;
    calc_page_ = nullptr;
    wifi_page_ = nullptr;
    bt_page_ = nullptr;
    clock_page_ = nullptr;
    status_page_ = nullptr;
    entries_ = {};
    entry_count_ = 0;
    current_page_ = Page::Root;
    editing_ = false;
    ui_ready_ = false;
}

void SettingsApp::handleMappedKey(uint32_t key)
{
    if (!editing_) {
        bool dirty = false;
        if (key == LV_KEY_UP) {
            if (group_ != nullptr) {
                lv_group_focus_prev(group_);
            }
            dirty = true;
        } else if (key == LV_KEY_DOWN) {
            if (group_ != nullptr) {
                lv_group_focus_next(group_);
            }
            dirty = true;
        } else if (key == LV_KEY_LEFT || key == LV_KEY_ESC) {
            if (current_page_ != Page::Root) {
                setCurrentPage(Page::Root);
                dirty = true;
            }
        } else if (key == LV_KEY_ENTER) {
            if (group_ != nullptr) {
                lv_group_send_data(group_, key);
            }
            dirty = true;
        } else if (group_ != nullptr) {
            lv_group_send_data(group_, key);
        }

        if (dirty) {
            refreshMenu();
        }
        return;
    }

    if (key == LV_KEY_ESC) {
        cancelEdit();
        refreshMenu();
        return;
    }
    if (key == LV_KEY_ENTER) {
        commitEdit();
        refreshMenu();
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (edit_length_ > 0) {
            --edit_length_;
            edit_buffer_[edit_length_] = '\0';
            refreshMenu();
        }
        return;
    }

    if (key >= 32U && key <= 126U && std::isprint(static_cast<int>(key)) != 0) {
        const int max_len = (edit_item_ == Item::WifiSsid) ? 32 : 64;
        if (edit_length_ < max_len) {
            edit_buffer_[edit_length_] = static_cast<char>(key);
            ++edit_length_;
            edit_buffer_[edit_length_] = '\0';
            refreshMenu();
        }
    }
}

void SettingsApp::render()
{
    if (!ui_ready_) {
        return;
    }

    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (now_ms - last_about_ms_ < 500) {
        return;
    }
    last_about_ms_ = now_ms;

    const uint32_t free_kb =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024);
    const uint32_t largest_kb =
        static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024);
    const uint32_t internal_kb =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    const uint32_t spiram_kb =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    const uint32_t up_s = now_ms / 1000;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "F:%lu L:%lu I:%lu P:%lu %lus",
                  static_cast<unsigned long>(free_kb),
                  static_cast<unsigned long>(largest_kb),
                  static_cast<unsigned long>(internal_kb),
                  static_cast<unsigned long>(spiram_kb),
                  static_cast<unsigned long>(up_s));
    if (MenuEntry *entry = findEntry(Item::Memory); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, buf);
    }

    sd_ready_ = isSdMounted();
    refreshWifiStatus();
    std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s",
                  sd_ready_ ? "mounted FATFS" : "not mounted");
    if (MenuEntry *entry = findEntry(Item::WifiPage); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kWifiBuilt ? wifi_state_text_.data() : "Unavailable");
    }
    if (MenuEntry *entry = findEntry(Item::WifiStatus); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, kWifiBuilt ? wifi_state_text_.data() : "Unavailable");
    }
    if (MenuEntry *entry = findEntry(Item::SdCard); entry != nullptr && entry->value != nullptr) {
        lv_label_set_text(entry->value, sd_state_text_.data());
    }
}

} // namespace brookesia
