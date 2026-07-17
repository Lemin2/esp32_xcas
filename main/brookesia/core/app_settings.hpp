#pragma once

#include <cstdint>

namespace brookesia::settings {

struct AppSettings {
    int angle_index = 0;
    int digits_index = 0;
    bool fn_app_switch_enabled = false;
    bool wifi_enabled = false;
    bool bt_hid_enabled = false;
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
