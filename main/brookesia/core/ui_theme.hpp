#pragma once

#include "lvgl.h"

namespace brookesia::ui_theme
{

void applyPage(lv_obj_t *obj, lv_color_t bg_color);
void applyPanel(lv_obj_t *obj,
                lv_color_t bg_color,
                lv_color_t border_color,
                lv_coord_t radius = 6,
                lv_coord_t pad_all = 4,
                lv_coord_t pad_row = 2,
                lv_coord_t border_width = 1);
void applyInput(lv_obj_t *obj,
                lv_color_t bg_color,
                lv_color_t border_color,
                lv_color_t text_color,
                lv_coord_t pad_all = 4);
void applyMenuOverlay(lv_obj_t *obj,
                      lv_color_t bg_color,
                      lv_color_t border_color,
                      lv_coord_t border_width = 1);
void applyMenuTile(lv_obj_t *obj,
                   lv_color_t bg_color);
void applyRowCard(lv_obj_t *obj,
                  lv_color_t border_color,
                  lv_coord_t radius = 4,
                  lv_coord_t pad_left = 8,
                  lv_coord_t pad_right = 8);
void applyText14(lv_obj_t *obj);
void applyText16(lv_obj_t *obj);
const lv_font_t *textFont14();
const lv_font_t *textFont16();

} // namespace brookesia::ui_theme