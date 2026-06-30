#include "brookesia/apps/graph_app.hpp"

#include <cmath>
#include <cstdio>

#include "cardputer_bsp.hpp"

namespace brookesia {
namespace {

constexpr uint64_t kUpBit = (1ULL << 39);    // ';'
constexpr uint64_t kDownBit = (1ULL << 53);  // '.'
constexpr uint64_t kLeftBit = (1ULL << 52);  // ','
constexpr uint64_t kRightBit = (1ULL << 54); // '/'
constexpr uint64_t kEnterBit = (1ULL << 41); // Enter

float fnSquare(float x) { return x * x; }
float fnCube(float x) { return x * x * x; }
float fnInv(float x) { return (x == 0.0f) ? NAN : 1.0f / x; }
float fnSqrt(float x) { return (x < 0.0f) ? NAN : std::sqrt(x); }
float fnLn(float x) { return (x <= 0.0f) ? NAN : std::log(x); }
float fnAbs(float x) { return std::fabs(x); }

struct FuncEntry {
    const char *label;
    float (*fn)(float);
};

constexpr FuncEntry kFuncs[] = {
    {"sin(x)", &std::sin},
    {"cos(x)", &std::cos},
    {"tan(x)", &std::tan},
    {"x^2", &fnSquare},
    {"x^3", &fnCube},
    {"1/x", &fnInv},
    {"sqrt(x)", &fnSqrt},
    {"exp(x)", &std::exp},
    {"ln(x)", &fnLn},
    {"|x|", &fnAbs},
};

constexpr int kFuncCount = static_cast<int>(sizeof(kFuncs) / sizeof(kFuncs[0]));

} // namespace

bool GraphApp::init()
{
    return true;
}

void GraphApp::ensureUi()
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
    lv_obj_set_style_bg_color(root_, LV_COLOR_MAKE(245, 245, 238), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(root_);
    lv_obj_set_style_text_font(title_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 0, 0);

    plot_ = lv_obj_create(root_);
    lv_obj_set_size(plot_, kPlotW, kPlotH);
    lv_obj_align(plot_, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_radius(plot_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(plot_, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(plot_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(plot_, LV_COLOR_MAKE(208, 214, 224), LV_PART_MAIN);
    lv_obj_set_style_border_width(plot_, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(plot_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(plot_, LV_OBJ_FLAG_SCROLLABLE);

    axis_x_ = lv_line_create(plot_);
    lv_obj_set_style_line_color(axis_x_, LV_COLOR_MAKE(198, 204, 214), LV_PART_MAIN);
    lv_obj_set_style_line_width(axis_x_, 1, LV_PART_MAIN);

    axis_y_ = lv_line_create(plot_);
    lv_obj_set_style_line_color(axis_y_, LV_COLOR_MAKE(198, 204, 214), LV_PART_MAIN);
    lv_obj_set_style_line_width(axis_y_, 1, LV_PART_MAIN);

    curve_ = lv_line_create(plot_);
    lv_obj_set_style_line_color(curve_, LV_COLOR_MAKE(208, 64, 64), LV_PART_MAIN);
    lv_obj_set_style_line_width(curve_, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(curve_, true, LV_PART_MAIN);

    hint_ = lv_label_create(root_);
    lv_obj_set_style_text_font(hint_, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_, LV_COLOR_MAKE(120, 130, 144), LV_PART_MAIN);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(hint_, ",/ func   ;/. zoom   Enter reset");

    ui_ready_ = true;
}

void GraphApp::recompute()
{
    if (!ui_ready_) {
        return;
    }

    const FuncEntry &entry = kFuncs[func_index_];

    float values[kSamples];
    float y_max = 0.0f;
    for (int i = 0; i < kSamples; ++i) {
        const float x = -x_span_ + (2.0f * x_span_ * static_cast<float>(i)) /
                                       static_cast<float>(kSamples - 1);
        float y = entry.fn(x);
        values[i] = y;
        if (std::isfinite(y)) {
            const float a = std::fabs(y);
            if (a > y_max) {
                y_max = a;
            }
        }
    }

    float y_span = y_max;
    if (!(y_span > 0.5f)) {
        y_span = 0.5f;
    }
    if (y_span > 1.0e4f) {
        y_span = 1.0e4f;
    }

    const float half_h = static_cast<float>(kPlotH) / 2.0f;
    for (int i = 0; i < kSamples; ++i) {
        const float px = (static_cast<float>(kPlotW - 1) * static_cast<float>(i)) /
                         static_cast<float>(kSamples - 1);
        float y = values[i];
        if (!std::isfinite(y)) {
            y = (y > 0.0f) ? y_span : -y_span;
        }
        float py = half_h - (y / y_span) * (half_h - 1.0f);
        if (py < 0.0f) {
            py = 0.0f;
        }
        if (py > static_cast<float>(kPlotH - 1)) {
            py = static_cast<float>(kPlotH - 1);
        }
        points_[i].x = static_cast<lv_value_precise_t>(px);
        points_[i].y = static_cast<lv_value_precise_t>(py);
    }
    lv_line_set_points(curve_, points_.data(), kSamples);

    axis_x_pts_[0].x = 0;
    axis_x_pts_[0].y = static_cast<lv_value_precise_t>(half_h);
    axis_x_pts_[1].x = kPlotW - 1;
    axis_x_pts_[1].y = static_cast<lv_value_precise_t>(half_h);
    lv_line_set_points(axis_x_, axis_x_pts_.data(), 2);

    const float zero_px = (static_cast<float>(kPlotW - 1) * (0.0f + x_span_)) / (2.0f * x_span_);
    axis_y_pts_[0].x = static_cast<lv_value_precise_t>(zero_px);
    axis_y_pts_[0].y = 0;
    axis_y_pts_[1].x = static_cast<lv_value_precise_t>(zero_px);
    axis_y_pts_[1].y = kPlotH - 1;
    lv_line_set_points(axis_y_, axis_y_pts_.data(), 2);
}

void GraphApp::updateTitle()
{
    if (title_ == nullptr) {
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "f(x) = %s   x:[-%.1f, %.1f]", kFuncs[func_index_].label,
                  static_cast<double>(x_span_), static_cast<double>(x_span_));
    lv_label_set_text(title_, buf);
}

void GraphApp::onFocus()
{
    ensureUi();
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    updateTitle();
    recompute();
}

void GraphApp::onBlur()
{
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly = pressedMask & ~prev_mask_;
    prev_mask_ = pressedMask;

    bool dirty = false;

    if ((newly & kLeftBit) != 0U) {
        func_index_ = (func_index_ + kFuncCount - 1) % kFuncCount;
        dirty = true;
    }
    if ((newly & kRightBit) != 0U) {
        func_index_ = (func_index_ + 1) % kFuncCount;
        dirty = true;
    }
    if ((newly & kUpBit) != 0U) {
        x_span_ *= 0.7f;
        if (x_span_ < 0.5f) {
            x_span_ = 0.5f;
        }
        dirty = true;
    }
    if ((newly & kDownBit) != 0U) {
        x_span_ *= 1.4f;
        if (x_span_ > 100.0f) {
            x_span_ = 100.0f;
        }
        dirty = true;
    }
    if ((newly & kEnterBit) != 0U) {
        x_span_ = 6.0f;
        dirty = true;
    }

    if (dirty) {
        updateTitle();
        recompute();
    }
}

void GraphApp::render()
{
    (void)prev_mask_;
}

} // namespace brookesia
