#pragma once

#include <array>
#include <cstdint>

#include "lvgl.h"

#include "brookesia/core/app.hpp"

namespace brookesia {

// Lightweight function plotter. Cycles through built-in math functions and
// renders them with an LVGL polyline. Fully self-contained (uses <cmath>),
// so it does not touch the giac evaluator and is safe on the UI task.
class GraphApp final : public App {
public:
    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void render() override;

private:
    void ensureUi();
    void recompute();
    void updateTitle();

    static constexpr int kSamples = 116;
    static constexpr int kPlotW = 232;
    static constexpr int kPlotH = 92;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    lv_obj_t *plot_ = nullptr;
    lv_obj_t *curve_ = nullptr;
    lv_obj_t *axis_x_ = nullptr;
    lv_obj_t *axis_y_ = nullptr;

    std::array<lv_point_precise_t, kSamples> points_{};
    std::array<lv_point_precise_t, 2> axis_x_pts_{};
    std::array<lv_point_precise_t, 2> axis_y_pts_{};

    int func_index_ = 0;
    float x_span_ = 6.0f;
    uint64_t prev_mask_ = 0;
    bool ui_ready_ = false;
};

} // namespace brookesia
