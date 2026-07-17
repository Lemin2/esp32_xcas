#include "brookesia/apps/placeholder_app.hpp"

#include <utility>

#include "esp_log.h"

#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr char kTag[] = "placeholder_app";

} // namespace

PlaceholderApp::PlaceholderApp(std::string title) : title_(std::move(title))
{
}

bool PlaceholderApp::init()
{
    return true;
}

void PlaceholderApp::onFocus()
{
    ESP_LOGI(kTag, "%s focused", title_.c_str());
    ensureUi();

    lv_obj_t *screen = lv_screen_active();
    if (screen != nullptr) {
        ui_theme::applyPage(screen, LV_COLOR_MAKE(245, 245, 238));
    }

    if (title_label_ != nullptr) {
        lv_label_set_text_fmt(title_label_, "%s", title_.c_str());
    }
}

void PlaceholderApp::handleKeyboardState(uint64_t pressedMask)
{
    last_mask_ = pressedMask;
}

void PlaceholderApp::render()
{
    (void)last_mask_;
}

void PlaceholderApp::ensureUi()
{
    if (ui_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    title_label_ = lv_label_create(screen);
    ui_theme::applyText16(title_label_);
    lv_obj_set_style_text_color(title_label_, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, 8, 28);

    hint_label_ = lv_label_create(screen);
    lv_obj_set_width(hint_label_, 220);
    lv_label_set_long_mode(hint_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    ui_theme::applyText14(hint_label_);
    lv_obj_set_style_text_color(hint_label_, LV_COLOR_MAKE(88, 104, 122), LV_PART_MAIN);
    lv_obj_align(hint_label_, LV_ALIGN_TOP_LEFT, 8, 64);
    lv_label_set_text(hint_label_, "This page is scaffolded in brookesia.\nUse Opt to open app menu.\nImplementation is next batch.");

    ui_ready_ = true;
}

} // namespace brookesia
