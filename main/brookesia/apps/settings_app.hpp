#pragma once

#include <array>
#include <cstdint>

#include <string>

#include "lvgl.h"

#include "brookesia/core/app.hpp"
#include "brookesia/core/service_hub.hpp"

namespace brookesia {

// Settings page. Angle mode and precision are applied to the live giac
// context through the shared evaluator service; the About row shows live
// device telemetry.
class SettingsApp final : public App {
public:
    explicit SettingsApp(ServiceHub &services);

    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void handleMappedKey(uint32_t key) override;
    void render() override;

private:
    static constexpr int kRowCount = 9;

    void ensureUi();
    void refreshRows();
    void applyAngle();
    void applyDigits();
    void applyFnSwitch();
    void applyWifi();
    void applyBtHid();
    void beginEdit(int row);
    void commitEdit();
    void cancelEdit();
    void refreshWifiStatus();
    void moveSelection(int delta);
    void applyHorizontalAction(int dir);

    xcas::XcasService &cas_;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    std::array<lv_obj_t *, kRowCount> rows_{};
    std::array<lv_obj_t *, kRowCount> names_{};
    std::array<lv_obj_t *, kRowCount> values_{};

    int selected_ = 0;
    int angle_index_ = 0;  // 0 = RAD, 1 = DEG
    int digits_index_ = 0; // index into digits table
    bool fn_app_switch_enabled_ = false;
    bool wifi_enabled_ = false;
    bool bt_hid_enabled_ = false;
    bool wifi_connected_ = false;
    bool bt_ready_ = false;
    bool sd_ready_ = false;
    bool editing_ = false;
    int edit_row_ = -1;
    char edit_buffer_[65] = {};
    int edit_length_ = 0;
    std::array<char, 48> wifi_state_text_{};
    std::array<char, 48> bt_state_text_{};
    std::array<char, 48> sd_state_text_{};
    uint64_t prev_mask_ = 0;
    uint32_t last_about_ms_ = 0;
    bool ui_ready_ = false;
};

} // namespace brookesia
