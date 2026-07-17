#include "brookesia/apps/settings_app.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#if CONFIG_ESP_WIFI_ENABLED
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#endif

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#if CONFIG_BT_ENABLED
#include "esp_bt.h"
#if CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt_main.h"
#endif
#endif

#include "cardputer_bsp.hpp"

#include "brookesia/core/app_settings.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr uint64_t kUpBit = (1ULL << 39);    // ';'
constexpr uint64_t kDownBit = (1ULL << 53);  // '.'
constexpr uint64_t kLeftBit = (1ULL << 52);  // ','
constexpr uint64_t kRightBit = (1ULL << 54); // '/'
constexpr uint64_t kEnterBit = (1ULL << 41); // Enter

const char *const kAngleValues[] = {"RAD", "DEG"};
constexpr int kAngleCount = 2;

const int kDigitsValues[] = {12, 15, 20, 30};
constexpr int kDigitsCount = 4;

const char *const kRowNames[] = {
    "Angle mode",
    "Precision",
    "Fn app switch",
    "WiFi link",
    "WiFi SSID",
    "WiFi Password",
    "Bluetooth HID",
    "SD card",
    "Memory",
};

const char *boolText(bool v)
{
    return v ? "ON" : "OFF";
}

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

#if CONFIG_ESP_WIFI_ENABLED
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

lv_obj_t *makeRow(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 26);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_theme::applyRowCard(row, LV_COLOR_MAKE(208, 214, 224), 5, 8, 8);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

} // namespace

SettingsApp::SettingsApp(ServiceHub &services) : cas_(services.casService())
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

    const lv_coord_t w = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayWidth);
    const lv_coord_t h = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayHeight);

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h - 16);
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 16);
    ui_theme::applyPage(root_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_set_style_pad_all(root_, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root_, 4, LV_PART_MAIN);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_AUTO);

    title_ = lv_label_create(root_);
    ui_theme::applyText16(title_);
    lv_obj_set_style_text_color(title_, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_label_set_text(title_, "Settings  ;/. sel  ,// chg  Enter edit");

    for (int i = 0; i < kRowCount; ++i) {
        rows_[i] = makeRow(root_);

        names_[i] = lv_label_create(rows_[i]);
        ui_theme::applyText14(names_[i]);
        lv_label_set_text(names_[i], kRowNames[i]);

        values_[i] = lv_label_create(rows_[i]);
        ui_theme::applyText14(values_[i]);
        lv_label_set_long_mode(values_[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_max_width(values_[i], 150, LV_PART_MAIN);
    }

    ui_ready_ = true;
}

void SettingsApp::refreshRows()
{
    if (!ui_ready_) {
        return;
    }

    lv_label_set_text(values_[0], kAngleValues[angle_index_]);

    char digbuf[16];
    std::snprintf(digbuf, sizeof(digbuf), "%d", kDigitsValues[digits_index_]);
    lv_label_set_text(values_[1], digbuf);

    lv_label_set_text(values_[2], boolText(fn_app_switch_enabled_));

    lv_label_set_text(values_[3], wifi_state_text_.data());

    settings::AppSettings cfg = settings::get();
    std::string ssid_text;
    if (editing_ && edit_row_ == 4) {
        ssid_text = std::string("EDIT:") + edit_buffer_ + "_";
    } else if (cfg.wifi_ssid[0] == '\0') {
        ssid_text = "(empty)";
    } else {
        ssid_text = cfg.wifi_ssid;
    }
    lv_label_set_text(values_[4], ssid_text.c_str());

    char pass_masked[65];
    std::string pass_text;
    if (editing_ && edit_row_ == 5) {
        pass_text = std::string("EDIT:") + edit_buffer_ + "_";
    } else {
        maskPassword(cfg.wifi_pass, pass_masked, sizeof(pass_masked));
        pass_text = pass_masked;
    }
    lv_label_set_text(values_[5], pass_text.c_str());

    lv_label_set_text(values_[6], bt_state_text_.data());

    lv_label_set_text(values_[7], sd_state_text_.data());

    for (int i = 0; i < kRowCount; ++i) {
        const bool sel = (i == selected_);
        lv_obj_set_style_bg_opa(rows_[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(rows_[i], LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
        lv_color_t text_col;
        if (sel) {
            text_col = LV_COLOR_MAKE(255, 255, 255);
        } else {
            text_col = LV_COLOR_MAKE(16, 24, 36);
        }
        lv_obj_set_style_text_color(names_[i], text_col, LV_PART_MAIN);
        lv_obj_set_style_text_color(values_[i], text_col, LV_PART_MAIN);
    }

    if (selected_ >= 0 && selected_ < kRowCount && rows_[selected_] != nullptr) {
        lv_obj_scroll_to_view(rows_[selected_], LV_ANIM_OFF);
    }
}

void SettingsApp::moveSelection(int delta)
{
    selected_ = (selected_ + kRowCount + delta) % kRowCount;
}

void SettingsApp::applyHorizontalAction(int dir)
{
    if (selected_ == 0) {
        angle_index_ = (angle_index_ + kAngleCount + dir) % kAngleCount;
        applyAngle();
        settings::AppSettings cfg = settings::get();
        cfg.angle_index = angle_index_;
        settings::set(cfg);
        settings::save();
    } else if (selected_ == 1) {
        digits_index_ = (digits_index_ + kDigitsCount + dir) % kDigitsCount;
        applyDigits();
        settings::AppSettings cfg = settings::get();
        cfg.digits_index = digits_index_;
        settings::set(cfg);
        settings::save();
    } else if (selected_ == 2) {
        fn_app_switch_enabled_ = !fn_app_switch_enabled_;
        applyFnSwitch();
    } else if (selected_ == 3) {
        wifi_enabled_ = !wifi_enabled_;
        applyWifi();
    } else if (selected_ == 4) {
        beginEdit(4);
    } else if (selected_ == 5) {
        beginEdit(5);
    } else if (selected_ == 6) {
        bt_hid_enabled_ = !bt_hid_enabled_;
        applyBtHid();
    } else if (selected_ == 7) {
        if (sd_ready_) {
            unmountExternalSd();
        } else {
            (void)mountExternalSd();
        }
        sd_ready_ = isSdMounted();
        std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s",
                      sd_ready_ ? "mounted FATFS" : "not mounted");
    }
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

#if CONFIG_ESP_WIFI_ENABLED
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
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "NOT BUILT");
#endif
}

void SettingsApp::applyBtHid()
{
    settings::AppSettings cfg = settings::get();
    cfg.bt_hid_enabled = bt_hid_enabled_;
    settings::set(cfg);
    settings::save();

#if CONFIG_BT_ENABLED
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
    std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "NOT BUILT");
#endif
}

void SettingsApp::refreshWifiStatus()
{
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

void SettingsApp::beginEdit(int row)
{
    settings::AppSettings cfg = settings::get();
    editing_ = true;
    edit_row_ = row;
    edit_length_ = 0;
    edit_buffer_[0] = '\0';
    if (row == 4) {
        std::snprintf(edit_buffer_, sizeof(edit_buffer_), "%s", cfg.wifi_ssid);
    } else if (row == 5) {
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
    if (edit_row_ == 4) {
        std::strncpy(cfg.wifi_ssid, edit_buffer_, sizeof(cfg.wifi_ssid) - 1);
        cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    } else if (edit_row_ == 5) {
        std::strncpy(cfg.wifi_pass, edit_buffer_, sizeof(cfg.wifi_pass) - 1);
        cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
    }
    settings::set(cfg);
    settings::save();

    editing_ = false;
    edit_row_ = -1;
    edit_buffer_[0] = '\0';
    edit_length_ = 0;

    if (wifi_enabled_) {
        applyWifi();
    }
}

void SettingsApp::cancelEdit()
{
    editing_ = false;
    edit_row_ = -1;
    edit_buffer_[0] = '\0';
    edit_length_ = 0;
}

void SettingsApp::onFocus()
{
    settings::load();
    const settings::AppSettings cfg = settings::get();

    angle_index_ = (cfg.angle_index == 1) ? 1 : 0;
    digits_index_ = (cfg.digits_index >= 0 && cfg.digits_index < kDigitsCount) ? cfg.digits_index : 0;
    fn_app_switch_enabled_ = cfg.fn_app_switch_enabled;
    wifi_enabled_ = cfg.wifi_enabled;
    bt_hid_enabled_ = cfg.bt_hid_enabled;
    sd_ready_ = isSdMounted();
    std::snprintf(wifi_state_text_.data(), wifi_state_text_.size(), "IDLE");
    std::snprintf(bt_state_text_.data(), bt_state_text_.size(), "%s", bt_hid_enabled_ ? "ON stack" : "OFF");
    std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s", sd_ready_ ? "mounted FATFS" : "not mounted");

    ensureUi();
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }

    applyAngle();
    applyDigits();
    applyWifi();
    applyBtHid();

    refreshRows();
}

void SettingsApp::onBlur()
{
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsApp::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly = pressedMask & ~prev_mask_;
    prev_mask_ = pressedMask;

    if (editing_) {
        if ((newly & kEnterBit) != 0U) {
            commitEdit();
            refreshRows();
        }
        return;
    }

    bool dirty = false;

    if ((newly & kUpBit) != 0U) {
        moveSelection(-1);
        dirty = true;
    }
    if ((newly & kDownBit) != 0U) {
        moveSelection(1);
        dirty = true;
    }

    if ((newly & kEnterBit) != 0U) {
        if (selected_ == 4 || selected_ == 5) {
            beginEdit(selected_);
            dirty = true;
        }
    }

    const bool left = (newly & kLeftBit) != 0U;
    const bool right = (newly & kRightBit) != 0U;
    if (left || right) {
        const int dir = right ? 1 : -1;
        applyHorizontalAction(dir);
        dirty = true;
    }

    if (dirty) {
        refreshRows();
    }
}

void SettingsApp::handleMappedKey(uint32_t key)
{
    if (!editing_) {
        bool dirty = false;
        if (key == LV_KEY_UP) {
            moveSelection(-1);
            dirty = true;
        } else if (key == LV_KEY_DOWN) {
            moveSelection(1);
            dirty = true;
        } else if (key == LV_KEY_LEFT) {
            applyHorizontalAction(-1);
            dirty = true;
        } else if (key == LV_KEY_RIGHT) {
            applyHorizontalAction(1);
            dirty = true;
        } else if (key == LV_KEY_ENTER && (selected_ == 4 || selected_ == 5)) {
            beginEdit(selected_);
            dirty = true;
        }

        if (dirty) {
            refreshRows();
        }
        return;
    }

    if (key == LV_KEY_ESC) {
        cancelEdit();
        refreshRows();
        return;
    }
    if (key == LV_KEY_ENTER) {
        commitEdit();
        refreshRows();
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (edit_length_ > 0) {
            --edit_length_;
            edit_buffer_[edit_length_] = '\0';
            refreshRows();
        }
        return;
    }

    if (key >= 32U && key <= 126U && std::isprint(static_cast<int>(key)) != 0) {
        const int max_len = (edit_row_ == 4) ? 32 : 64;
        if (edit_length_ < max_len) {
            edit_buffer_[edit_length_] = static_cast<char>(key);
            ++edit_length_;
            edit_buffer_[edit_length_] = '\0';
            refreshRows();
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
    lv_label_set_text(values_[8], buf);

    sd_ready_ = isSdMounted();
    refreshWifiStatus();
    std::snprintf(sd_state_text_.data(), sd_state_text_.size(), "%s",
                  sd_ready_ ? "mounted FATFS" : "not mounted");
    lv_label_set_text(values_[3], wifi_state_text_.data());
    lv_label_set_text(values_[7], sd_state_text_.data());
}

} // namespace brookesia
