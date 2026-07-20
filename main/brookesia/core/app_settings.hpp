#pragma once

#include <cstdint>

namespace brookesia::settings {

struct AppSettings {
    int angle_index = 0;
    int digits_index = 0;
    int formula_preview_mode = 0; // 0 = graphic objects, 1 = text fallback
    bool fn_app_switch_enabled = false;
    bool wifi_enabled = false;
    bool bt_hid_enabled = false;
    bool status_show_wifi = true;
    bool status_show_bt = true;
    bool status_show_memory = true;
    bool status_memory_mb = false;
    bool status_show_clock = true;
    int clock_year = 2026;
    int clock_month = 1;
    int clock_day = 1;
    int clock_hour = 0;
    int clock_minute = 0;
    char wifi_ssid[33] = {};
    char wifi_pass[65] = {};
};

// Loads settings from /data/settings.cfg when storage is mounted.
// Missing or invalid files fall back to defaults.
void load();

// Saves current settings to /data/settings.cfg. Returns false on I/O failure.
bool save();

AppSettings get();
void set(const AppSettings &value);

} // namespace brookesia::settings
