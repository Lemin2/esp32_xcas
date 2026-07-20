#include "brookesia/core/app_settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "brookesia/apps/fs_util.hpp"

namespace brookesia::settings {
namespace {

constexpr char kSettingsPath[] = "/data/settings.cfg";

AppSettings s_settings{};
bool s_loaded = false;

bool parseBool(const char *value)
{
    if (value == nullptr) {
        return false;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
        std::strcmp(value, "ON") == 0) {
        return true;
    }
    return false;
}

void trimNewline(char *s)
{
    if (s == nullptr) {
        return;
    }
    size_t n = std::strlen(s);
    while (n > 0) {
        const char c = s[n - 1];
        if (c == '\n' || c == '\r') {
            s[n - 1] = '\0';
            --n;
            continue;
        }
        break;
    }
}

void clampSettings(AppSettings &v)
{
    if (v.angle_index < 0 || v.angle_index > 1) {
        v.angle_index = 0;
    }
    if (v.digits_index < 0 || v.digits_index > 3) {
        v.digits_index = 0;
    }
    if (v.formula_preview_mode < 0 || v.formula_preview_mode > 1) {
        v.formula_preview_mode = 0;
    }
    if (v.clock_year < 2024 || v.clock_year > 2099) {
        v.clock_year = 2026;
    }
    if (v.clock_month < 1 || v.clock_month > 12) {
        v.clock_month = 1;
    }
    if (v.clock_day < 1 || v.clock_day > 31) {
        v.clock_day = 1;
    }
    if (v.clock_hour < 0 || v.clock_hour > 23) {
        v.clock_hour = 0;
    }
    if (v.clock_minute < 0 || v.clock_minute > 59) {
        v.clock_minute = 0;
    }
}

} // namespace

void load()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    s_settings = AppSettings{};
    if (!ensureStorageMounted()) {
        return;
    }

    FILE *f = std::fopen(kSettingsPath, "r");
    if (f == nullptr) {
        return;
    }

    char line[192];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        trimNewline(line);
        char *eq = std::strchr(line, '=');
        if (eq == nullptr) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (std::strcmp(key, "angle_index") == 0) {
            s_settings.angle_index = std::atoi(value);
        } else if (std::strcmp(key, "digits_index") == 0) {
            s_settings.digits_index = std::atoi(value);
        } else if (std::strcmp(key, "formula_preview_mode") == 0) {
            s_settings.formula_preview_mode = std::atoi(value);
        } else if (std::strcmp(key, "fn_app_switch") == 0) {
            s_settings.fn_app_switch_enabled = parseBool(value);
        } else if (std::strcmp(key, "wifi_enabled") == 0) {
            s_settings.wifi_enabled = parseBool(value);
        } else if (std::strcmp(key, "bt_hid_enabled") == 0) {
            s_settings.bt_hid_enabled = parseBool(value);
        } else if (std::strcmp(key, "status_show_wifi") == 0) {
            s_settings.status_show_wifi = parseBool(value);
        } else if (std::strcmp(key, "status_show_bt") == 0) {
            s_settings.status_show_bt = parseBool(value);
        } else if (std::strcmp(key, "status_show_memory") == 0) {
            s_settings.status_show_memory = parseBool(value);
        } else if (std::strcmp(key, "status_memory_mb") == 0) {
            s_settings.status_memory_mb = parseBool(value);
        } else if (std::strcmp(key, "status_show_clock") == 0) {
            s_settings.status_show_clock = parseBool(value);
        } else if (std::strcmp(key, "clock_year") == 0) {
            s_settings.clock_year = std::atoi(value);
        } else if (std::strcmp(key, "clock_month") == 0) {
            s_settings.clock_month = std::atoi(value);
        } else if (std::strcmp(key, "clock_day") == 0) {
            s_settings.clock_day = std::atoi(value);
        } else if (std::strcmp(key, "clock_hour") == 0) {
            s_settings.clock_hour = std::atoi(value);
        } else if (std::strcmp(key, "clock_minute") == 0) {
            s_settings.clock_minute = std::atoi(value);
        } else if (std::strcmp(key, "wifi_ssid") == 0) {
            std::snprintf(s_settings.wifi_ssid, sizeof(s_settings.wifi_ssid), "%s", value);
        } else if (std::strcmp(key, "wifi_pass") == 0) {
            std::snprintf(s_settings.wifi_pass, sizeof(s_settings.wifi_pass), "%s", value);
        }
    }

    std::fclose(f);
    clampSettings(s_settings);
}

bool save()
{
    if (!s_loaded) {
        load();
    }
    if (!ensureStorageMounted()) {
        return false;
    }

    FILE *f = std::fopen(kSettingsPath, "w");
    if (f == nullptr) {
        return false;
    }

    std::fprintf(f, "angle_index=%d\n", s_settings.angle_index);
    std::fprintf(f, "digits_index=%d\n", s_settings.digits_index);
    std::fprintf(f, "formula_preview_mode=%d\n", s_settings.formula_preview_mode);
    std::fprintf(f, "fn_app_switch=%d\n", s_settings.fn_app_switch_enabled ? 1 : 0);
    std::fprintf(f, "wifi_enabled=%d\n", s_settings.wifi_enabled ? 1 : 0);
    std::fprintf(f, "bt_hid_enabled=%d\n", s_settings.bt_hid_enabled ? 1 : 0);
    std::fprintf(f, "status_show_wifi=%d\n", s_settings.status_show_wifi ? 1 : 0);
    std::fprintf(f, "status_show_bt=%d\n", s_settings.status_show_bt ? 1 : 0);
    std::fprintf(f, "status_show_memory=%d\n", s_settings.status_show_memory ? 1 : 0);
    std::fprintf(f, "status_memory_mb=%d\n", s_settings.status_memory_mb ? 1 : 0);
    std::fprintf(f, "status_show_clock=%d\n", s_settings.status_show_clock ? 1 : 0);
    std::fprintf(f, "clock_year=%d\n", s_settings.clock_year);
    std::fprintf(f, "clock_month=%d\n", s_settings.clock_month);
    std::fprintf(f, "clock_day=%d\n", s_settings.clock_day);
    std::fprintf(f, "clock_hour=%d\n", s_settings.clock_hour);
    std::fprintf(f, "clock_minute=%d\n", s_settings.clock_minute);
    std::fprintf(f, "wifi_ssid=%s\n", s_settings.wifi_ssid);
    std::fprintf(f, "wifi_pass=%s\n", s_settings.wifi_pass);
    std::fclose(f);
    return true;
}

AppSettings get()
{
    if (!s_loaded) {
        load();
    }
    return s_settings;
}

void set(const AppSettings &value)
{
    s_settings = value;
    clampSettings(s_settings);
    s_loaded = true;
}

} // namespace brookesia::settings
