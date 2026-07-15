#include "brookesia/apps/settings_app.hpp"

#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "cardputer_bsp.hpp"

#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr uint64_t kUpBit = (1ULL << 39);    // ';'
constexpr uint64_t kDownBit = (1ULL << 53);  // '.'
constexpr uint64_t kLeftBit = (1ULL << 52);  // ','
constexpr uint64_t kRightBit = (1ULL << 54); // '/'

const char *const kAngleValues[] = {"RAD", "DEG"};
constexpr int kAngleCount = 2;

const int kDigitsValues[] = {12, 15, 20, 30};
constexpr int kDigitsCount = 4;

const char *const kRowNames[] = {"Angle mode", "Precision", "About"};

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
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(root_);
    ui_theme::applyText16(title_);
    lv_obj_set_style_text_color(title_, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_label_set_text(title_, "Settings   (;/. select  ,// change)");

    for (int i = 0; i < kRowCount; ++i) {
        rows_[i] = makeRow(root_);

        names_[i] = lv_label_create(rows_[i]);
        ui_theme::applyText14(names_[i]);
        lv_label_set_text(names_[i], kRowNames[i]);

        values_[i] = lv_label_create(rows_[i]);
        ui_theme::applyText14(values_[i]);
        lv_label_set_long_mode(values_[i], LV_LABEL_LONG_WRAP);
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

void SettingsApp::onFocus()
{
    ensureUi();
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
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

    bool dirty = false;

    if ((newly & kUpBit) != 0U) {
        selected_ = (selected_ + kRowCount - 1) % kRowCount;
        dirty = true;
    }
    if ((newly & kDownBit) != 0U) {
        selected_ = (selected_ + 1) % kRowCount;
        dirty = true;
    }

    const bool left = (newly & kLeftBit) != 0U;
    const bool right = (newly & kRightBit) != 0U;
    if (left || right) {
        const int dir = right ? 1 : -1;
        if (selected_ == 0) {
            angle_index_ = (angle_index_ + kAngleCount + dir) % kAngleCount;
            applyAngle();
            dirty = true;
        } else if (selected_ == 1) {
            digits_index_ = (digits_index_ + kDigitsCount + dir) % kDigitsCount;
            applyDigits();
            dirty = true;
        }
    }

    if (dirty) {
        refreshRows();
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
    const uint32_t up_s = now_ms / 1000;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%luKB free  %lus", static_cast<unsigned long>(free_kb),
                  static_cast<unsigned long>(up_s));
    lv_label_set_text(values_[2], buf);
}

} // namespace brookesia
