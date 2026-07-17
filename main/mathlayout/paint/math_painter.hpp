#pragma once

#include <string>
#include <variant>
#include <vector>

#include "lvgl.h"

#include "mathlayout/render/text_renderer.hpp"

namespace xcas::mathlayout
{

struct DrawGlyphCommand
{
    std::string text;
    int x = 0;
    int y = 0;
};

struct DrawLineCommand
{
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int width = 1;
};

struct DrawRectCommand
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool filled = true;
};

using DrawCommand = std::variant<DrawGlyphCommand, DrawLineCommand, DrawRectCommand>;

struct DrawList
{
    int width = 0;
    int height = 0;
    int baseline = 0;
    std::vector<DrawCommand> commands;
};

struct PaintViewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ProgressivePaintState
{
    size_t next_line_scan = 0;
    size_t next_other_scan = 0;
    bool line_pass_done = false;
    bool initialized = false;
    int offset_x = 0;
    int offset_y = 0;
    PaintViewport viewport{};
};

DrawList buildDrawList(const TextBox &box, const lv_font_t *font, int line_space = 2);

bool paintDrawListToCanvas(lv_obj_t *canvas,
                           const DrawList &draw_list,
                           const lv_font_t *font,
                           lv_color_t text_color,
                           lv_color_t bg_color,
                           int offset_x = 0,
                           int offset_y = 0);

bool beginProgressivePaintToCanvas(lv_obj_t *canvas,
                                   const DrawList &draw_list,
                                   lv_color_t bg_color,
                                   ProgressivePaintState &state,
                                   int offset_x = 0,
                                   int offset_y = 0,
                                   const PaintViewport *viewport = nullptr);

bool stepProgressivePaintToCanvas(lv_obj_t *canvas,
                                  const DrawList &draw_list,
                                  const lv_font_t *font,
                                  lv_color_t text_color,
                                  ProgressivePaintState &state,
                                  size_t max_commands,
                                  size_t max_line_commands,
                                  bool *out_finished = nullptr);

bool paintTextBoxToCanvas(lv_obj_t *canvas,
                          const TextBox &box,
                          const lv_font_t *font,
                          lv_color_t text_color,
                          lv_color_t bg_color,
                          DrawList *out_draw_list = nullptr);

} // namespace xcas::mathlayout