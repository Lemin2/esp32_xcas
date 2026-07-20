#pragma once

#include "lvgl.h"

namespace brookesia::ui_fonts
{

void init();
const lv_font_t *textFont14();
const lv_font_t *textFont16();
const lv_font_t *textFont(int size);

} // namespace brookesia::ui_fonts
