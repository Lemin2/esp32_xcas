#include "brookesia/core/ui_theme.hpp"

#include "brookesia/core/ui_fonts.hpp"

namespace brookesia::ui_theme
{
namespace
{

constexpr lv_color_t kText14Color = LV_COLOR_MAKE(16, 24, 36);
constexpr lv_color_t kText16Color = LV_COLOR_MAKE(24, 84, 192);

static lv_style_t s_text14;
static lv_style_t s_text16;
static bool s_initialized = false;

void initOnce()
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;
    ui_fonts::init();

    lv_style_init(&s_text14);
    lv_style_set_text_font(&s_text14, ui_fonts::textFont14());
    lv_style_set_text_color(&s_text14, kText14Color);

    lv_style_init(&s_text16);
    lv_style_set_text_font(&s_text16, ui_fonts::textFont16());
    lv_style_set_text_color(&s_text16, kText16Color);
}

void applyBaseCard(lv_obj_t *obj,
                   lv_color_t bg_color,
                   lv_color_t border_color,
                   lv_coord_t radius,
                   lv_coord_t pad_all,
                   lv_coord_t pad_row,
                   lv_coord_t border_width)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, border_width, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, pad_all, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, pad_row, LV_PART_MAIN);
}

} // namespace

void applyPage(lv_obj_t *obj, lv_color_t bg_color)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}

void applyPanel(lv_obj_t *obj,
                lv_color_t bg_color,
                lv_color_t border_color,
                lv_coord_t radius,
                lv_coord_t pad_all,
                lv_coord_t pad_row,
                lv_coord_t border_width)
{
    if (obj == nullptr) {
        return;
    }
    applyBaseCard(obj, bg_color, border_color, radius, pad_all, pad_row, border_width);
}

void applyInput(lv_obj_t *obj,
                lv_color_t bg_color,
                lv_color_t border_color,
                lv_color_t text_color,
                lv_coord_t pad_all)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, text_color, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, pad_all, LV_PART_MAIN);
}

void applyMenuOverlay(lv_obj_t *obj,
                      lv_color_t bg_color,
                      lv_color_t border_color,
                      lv_coord_t border_width)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_radius(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, border_width, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(obj, 6, LV_PART_MAIN);
}

void applyMenuTile(lv_obj_t *obj, lv_color_t bg_color)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_radius(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 1, LV_PART_MAIN);
}

void applyRowCard(lv_obj_t *obj,
                  lv_color_t border_color,
                  lv_coord_t radius,
                  lv_coord_t pad_left,
                  lv_coord_t pad_right)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj, pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, pad_right, LV_PART_MAIN);
}

void applyText14(lv_obj_t *obj)
{
    initOnce();
    if (obj != nullptr) {
        lv_obj_add_style(obj, &s_text14, LV_PART_MAIN);
    }
}

void applyText16(lv_obj_t *obj)
{
    initOnce();
    if (obj != nullptr) {
        lv_obj_add_style(obj, &s_text16, LV_PART_MAIN);
    }
}

const lv_font_t *textFont14()
{
    initOnce();
    return ui_fonts::textFont14();
}

const lv_font_t *textFont16()
{
    initOnce();
    return ui_fonts::textFont16();
}

} // namespace brookesia::ui_theme