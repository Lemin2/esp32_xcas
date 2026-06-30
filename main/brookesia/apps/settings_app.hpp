#pragma once

#include <array>
#include <cstdint>

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
    void render() override;

private:
    static constexpr int kRowCount = 3;

    void ensureUi();
    void refreshRows();
    void applyAngle();
    void applyDigits();

    xcas::XcasService &cas_;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    std::array<lv_obj_t *, kRowCount> rows_{};
    std::array<lv_obj_t *, kRowCount> names_{};
    std::array<lv_obj_t *, kRowCount> values_{};

    int selected_ = 0;
    int angle_index_ = 0;  // 0 = RAD, 1 = DEG
    int digits_index_ = 0; // index into digits table
    uint64_t prev_mask_ = 0;
    uint32_t last_about_ms_ = 0;
    bool ui_ready_ = false;
};

} // namespace brookesia
