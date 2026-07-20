#include "xcas_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/app_settings.hpp"
#include "brookesia/core/ui_theme.hpp"
#include "mathlayout/paint/math_painter.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace xcas
{
    namespace
    {
        constexpr lv_color_t kBgColor = LV_COLOR_MAKE(245, 245, 238);
        constexpr lv_color_t kPanelColor = LV_COLOR_MAKE(255, 255, 255);
        constexpr lv_color_t kTextColor = LV_COLOR_MAKE(16, 24, 36);
        constexpr lv_color_t kStatusColor = LV_COLOR_MAKE(88, 104, 122);
        constexpr lv_color_t kAccentColor = LV_COLOR_MAKE(24, 84, 192);
    #if defined(CONFIG_XCAS_BOARD_TAB5) && CONFIG_XCAS_BOARD_TAB5
        constexpr bool kBoardTab5 = true;
    #else
        constexpr bool kBoardTab5 = false;
    #endif

#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        static const char *const kMathFuncKeyboardMap[] = {
            "sin(", "cos(", "tan(", "sqrt(", LV_SYMBOL_BACKSPACE, "\n",
            "asin(", "acos(", "atan(", "root(", "^", "\n",
            "log(", "ln(", "exp(", "abs(", "pi", "e", "\n",
            "diff(", "int(", "limit(", "sum(", LV_SYMBOL_OK, "",};

        static const char *const kMathSymbolKeyboardMap[] = {
            "solve(", "subst(", "factor(", "simplify(", LV_SYMBOL_BACKSPACE, "\n",
            "expand(", "normal(", "det(", "matrix(", "seq(", "\n",
            "plot(", "[", "]", "(", ")", "\n",
            "x", "y", "n", ",", ";", "=", LV_SYMBOL_OK, "",};

        static const char *const kMathNumberKeyboardMap[] = {
            "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n",
            "4", "5", "6", "/", "\n",
            "1", "2", "3", "*", "\n",
            "0", ".", "+", "-", LV_SYMBOL_OK, "",};

        static const lv_buttonmatrix_ctrl_t kMathKeyboardCtrl[] = {
            LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
            LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
            LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
            LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
            LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1, LV_BUTTONMATRIX_CTRL_WIDTH_1,
        };
#endif

        size_t emitRgb565ImageBase64Chunk(const uint8_t *bytes, size_t total, size_t offset, int max_lines)
        {
            static const char kB64[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            char line[65];
            int emitted_lines = 0;
            while (offset < total && emitted_lines < max_lines) {
                int n = 0;
                for (int group = 0; group < 16 && offset < total; ++group) {
                    const uint32_t b0 = bytes[offset];
                    const uint32_t b1 = (offset + 1 < total) ? bytes[offset + 1] : 0;
                    const uint32_t b2 = (offset + 2 < total) ? bytes[offset + 2] : 0;
                    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
                    line[n++] = kB64[(triple >> 18) & 0x3F];
                    line[n++] = kB64[(triple >> 12) & 0x3F];
                    line[n++] = (offset + 1 < total) ? kB64[(triple >> 6) & 0x3F] : '=';
                    line[n++] = (offset + 2 < total) ? kB64[triple & 0x3F] : '=';
                    offset += 3;
                }
                line[n] = '\0';
                std::printf("SHOT:%s\n", line);
                ++emitted_lines;
            }
            return offset;
        }

        inline void putPixel(std::vector<uint16_t> &pixels,
                             int width,
                             int height,
                             int x,
                             int y,
                             uint16_t color)
        {
            if (x < 0 || y < 0 || x >= width || y >= height) {
                return;
            }
            pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = color;
        }

        void drawLineRgb565(std::vector<uint16_t> &pixels,
                            int width,
                            int height,
                            int x1,
                            int y1,
                            int x2,
                            int y2,
                            int thickness,
                            uint16_t color)
        {
            int dx = std::abs(x2 - x1);
            int sx = x1 < x2 ? 1 : -1;
            int dy = -std::abs(y2 - y1);
            int sy = y1 < y2 ? 1 : -1;
            int err = dx + dy;
            const int half = std::max(0, thickness / 2);

            while (true) {
                for (int oy = -half; oy <= half; ++oy) {
                    for (int ox = -half; ox <= half; ++ox) {
                        putPixel(pixels, width, height, x1 + ox, y1 + oy, color);
                    }
                }

                if (x1 == x2 && y1 == y2) {
                    break;
                }

                const int e2 = err << 1;
                if (e2 >= dy) {
                    err += dy;
                    x1 += sx;
                }
                if (e2 <= dx) {
                    err += dx;
                    y1 += sy;
                }
            }
        }

        void fillRectRgb565(std::vector<uint16_t> &pixels,
                            int width,
                            int height,
                            int x,
                            int y,
                            int w,
                            int h,
                            uint16_t color)
        {
            if (w <= 0 || h <= 0) {
                return;
            }

            const int x0 = std::max(0, x);
            const int y0 = std::max(0, y);
            const int x1 = std::min(width, x + w);
            const int y1 = std::min(height, y + h);

            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    putPixel(pixels, width, height, xx, yy, color);
                }
            }
        }

        uint32_t decodeGlyphCodepoint(const std::string &text)
        {
            if (text.empty()) {
                return 0;
            }
            const auto b0 = static_cast<uint8_t>(text[0]);
            if (b0 < 0x80U) {
                return b0;
            }
            if ((b0 & 0xE0U) == 0xC0U && text.size() >= 2) {
                const auto b1 = static_cast<uint8_t>(text[1]);
                return ((b0 & 0x1FU) << 6) | (b1 & 0x3FU);
            }
            if ((b0 & 0xF0U) == 0xE0U && text.size() >= 3) {
                const auto b1 = static_cast<uint8_t>(text[1]);
                const auto b2 = static_cast<uint8_t>(text[2]);
                return ((b0 & 0x0FU) << 12) | ((b1 & 0x3FU) << 6) | (b2 & 0x3FU);
            }
            if ((b0 & 0xF8U) == 0xF0U && text.size() >= 4) {
                const auto b1 = static_cast<uint8_t>(text[1]);
                const auto b2 = static_cast<uint8_t>(text[2]);
                const auto b3 = static_cast<uint8_t>(text[3]);
                return ((b0 & 0x07U) << 18) | ((b1 & 0x3FU) << 12) | ((b2 & 0x3FU) << 6) | (b3 & 0x3FU);
            }
            return 0;
        }

        void drawGlyphRgb565(std::vector<uint16_t> &pixels,
                             int width,
                             int height,
                             const lv_font_t *font,
                             const xcas::mathlayout::DrawGlyphCommand &glyph,
                             uint16_t color)
        {
            if (font == nullptr || glyph.text.empty()) {
                return;
            }

            const uint32_t cp = decodeGlyphCodepoint(glyph.text);
            if (cp == 0) {
                return;
            }

            lv_font_glyph_dsc_t dsc{};
            if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0) || dsc.box_w == 0 || dsc.box_h == 0) {
                return;
            }

            lv_draw_buf_t *dynamic_bitmap = nullptr;
            const auto *bitmap = static_cast<const uint8_t *>(lv_font_get_glyph_static_bitmap(&dsc));
            int stride = dsc.stride != 0 ? static_cast<int>(dsc.stride) : static_cast<int>((dsc.box_w + 7U) / 8U);
            bool bitmap_is_a8 = dsc.format == LV_FONT_GLYPH_FORMAT_A8;
            if (bitmap == nullptr) {
                dynamic_bitmap = lv_draw_buf_create(dsc.box_w, dsc.box_h, LV_COLOR_FORMAT_A8, 0);
                if (dynamic_bitmap != nullptr) {
                    const auto *glyph_data = static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&dsc, dynamic_bitmap));
                    if (glyph_data != nullptr) {
                        bitmap = glyph_data->data;
                        stride = static_cast<int>(glyph_data->header.stride);
                        bitmap_is_a8 = true;
                    }
                }
            }
            if (bitmap == nullptr) {
                if (dynamic_bitmap != nullptr) {
                    lv_draw_buf_destroy(dynamic_bitmap);
                }
                lv_font_glyph_release_draw_data(&dsc);
                return;
            }

            const int baseline_from_top = static_cast<int>(font->line_height) - static_cast<int>(font->base_line);
            const int x0 = glyph.x + static_cast<int>(dsc.ofs_x);
            const int y0 = glyph.y + baseline_from_top - static_cast<int>(dsc.box_h) - static_cast<int>(dsc.ofs_y);

            for (int yy = 0; yy < static_cast<int>(dsc.box_h); ++yy) {
                for (int xx = 0; xx < static_cast<int>(dsc.box_w); ++xx) {
                    const size_t row = static_cast<size_t>(yy) * static_cast<size_t>(stride);
                    uint8_t alpha = 0;
                    if (bitmap_is_a8) {
                        alpha = bitmap[row + static_cast<size_t>(xx)];
                    } else if (dsc.format == LV_FONT_GLYPH_FORMAT_A4) {
                        const uint8_t packed = bitmap[row + static_cast<size_t>(xx / 2)];
                        alpha = (xx & 1) ? static_cast<uint8_t>((packed & 0x0FU) * 17U)
                                         : static_cast<uint8_t>(((packed >> 4) & 0x0FU) * 17U);
                    } else if (dsc.format == LV_FONT_GLYPH_FORMAT_A2) {
                        const uint8_t packed = bitmap[row + static_cast<size_t>(xx / 4)];
                        const int shift = 6 - ((xx & 3) * 2);
                        alpha = static_cast<uint8_t>(((packed >> shift) & 0x03U) * 85U);
                    } else if (dsc.format == LV_FONT_GLYPH_FORMAT_A1) {
                        const uint8_t packed = bitmap[row + static_cast<size_t>(xx / 8)];
                        alpha = (packed & (0x80U >> (xx & 7))) ? 255 : 0;
                    }
                    if (alpha > 24) {
                        putPixel(pixels, width, height, x0 + xx, y0 + yy, color);
                    }
                }
            }

            if (dynamic_bitmap != nullptr) {
                lv_draw_buf_destroy(dynamic_bitmap);
            }
            lv_font_glyph_release_draw_data(&dsc);
        }

        std::vector<uint16_t> rasterizeDrawListRgb565(const xcas::mathlayout::DrawList &draw_list,
                                                       int width,
                                                       int height,
                                                       const lv_font_t *font,
                                                       int offset_x,
                                                       int offset_y,
                                                       lv_color_t text_color,
                                                       lv_color_t bg_color)
        {
            const uint16_t fg = lv_color_to_u16(text_color);
            const uint16_t bg = lv_color_to_u16(bg_color);
            std::vector<uint16_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), bg);

            for (const auto &command : draw_list.commands) {
                if (const auto *line = std::get_if<xcas::mathlayout::DrawLineCommand>(&command)) {
                    drawLineRgb565(pixels,
                                   width,
                                   height,
                                   line->x1 + offset_x,
                                   line->y1 + offset_y,
                                   line->x2 + offset_x,
                                   line->y2 + offset_y,
                                   std::max(1, line->width),
                                   fg);
                    continue;
                }

                if (const auto *rect = std::get_if<xcas::mathlayout::DrawRectCommand>(&command)) {
                    if (rect->filled) {
                        fillRectRgb565(pixels, width, height, rect->x + offset_x, rect->y + offset_y, rect->width, rect->height, fg);
                    } else {
                        drawLineRgb565(pixels, width, height, rect->x + offset_x, rect->y + offset_y, rect->x + rect->width - 1 + offset_x, rect->y + offset_y, 1, fg);
                        drawLineRgb565(pixels, width, height, rect->x + offset_x, rect->y + offset_y, rect->x + offset_x, rect->y + rect->height - 1 + offset_y, 1, fg);
                        drawLineRgb565(pixels, width, height, rect->x + rect->width - 1 + offset_x, rect->y + offset_y, rect->x + rect->width - 1 + offset_x, rect->y + rect->height - 1 + offset_y, 1, fg);
                        drawLineRgb565(pixels, width, height, rect->x + offset_x, rect->y + rect->height - 1 + offset_y, rect->x + rect->width - 1 + offset_x, rect->y + rect->height - 1 + offset_y, 1, fg);
                    }
                    continue;
                }

                if (const auto *glyph = std::get_if<xcas::mathlayout::DrawGlyphCommand>(&command)) {
                    xcas::mathlayout::DrawGlyphCommand shifted = *glyph;
                    shifted.x += offset_x;
                    shifted.y += offset_y;
                    drawGlyphRgb565(pixels, width, height, font, shifted, fg);
                }
            }

            return pixels;
        }

        xcas::mathlayout::DrawList scaleDrawListToFit(const xcas::mathlayout::DrawList &src,
                                                      int max_width,
                                                      int max_height)
        {
            if (src.width <= 0 || src.height <= 0 || max_width <= 0 || max_height <= 0) {
                return src;
            }

            const float sx = static_cast<float>(max_width) / static_cast<float>(src.width);
            const float sy = static_cast<float>(max_height) / static_cast<float>(src.height);
            const float scale = std::min(1.0F, std::min(sx, sy));
            if (scale >= 0.999F) {
                return src;
            }

            xcas::mathlayout::DrawList out;
            out.width = std::max(1, static_cast<int>(std::lround(static_cast<float>(src.width) * scale)));
            out.height = std::max(1, static_cast<int>(std::lround(static_cast<float>(src.height) * scale)));
            out.baseline = std::max(0, static_cast<int>(std::lround(static_cast<float>(src.baseline) * scale)));
            out.commands.reserve(src.commands.size());

            auto sc = [scale](int v) {
                return static_cast<int>(std::lround(static_cast<float>(v) * scale));
            };

            for (const auto &cmd : src.commands) {
                if (const auto *g = std::get_if<xcas::mathlayout::DrawGlyphCommand>(&cmd)) {
                    xcas::mathlayout::DrawGlyphCommand gg = *g;
                    gg.x = sc(g->x);
                    gg.y = sc(g->y);
                    out.commands.push_back(std::move(gg));
                    continue;
                }
                if (const auto *l = std::get_if<xcas::mathlayout::DrawLineCommand>(&cmd)) {
                    xcas::mathlayout::DrawLineCommand ll = *l;
                    ll.x1 = sc(l->x1);
                    ll.y1 = sc(l->y1);
                    ll.x2 = sc(l->x2);
                    ll.y2 = sc(l->y2);
                    ll.width = std::max(1, sc(std::max(1, l->width)));
                    out.commands.push_back(std::move(ll));
                    continue;
                }
                if (const auto *r = std::get_if<xcas::mathlayout::DrawRectCommand>(&cmd)) {
                    xcas::mathlayout::DrawRectCommand rr = *r;
                    rr.x = sc(r->x);
                    rr.y = sc(r->y);
                    rr.width = std::max(1, sc(std::max(1, r->width)));
                    rr.height = std::max(1, sc(std::max(1, r->height)));
                    out.commands.push_back(std::move(rr));
                }
            }

            return out;
        }

        struct FormulaShotTaskArgs
        {
            std::vector<uint16_t> pixels;
            int width = 0;
            int height = 0;
        };

        void formulaShotTask(void *arg)
        {
            std::unique_ptr<FormulaShotTaskArgs> job(static_cast<FormulaShotTaskArgs *>(arg));
            if (!job || job->pixels.empty() || job->width <= 0 || job->height <= 0) {
                std::printf("SHOT_ERR invalid_formula_image\n");
                std::fflush(stdout);
                vTaskDelete(nullptr);
                return;
            }

            const auto *bytes = reinterpret_cast<const uint8_t *>(job->pixels.data());
            const size_t total = job->pixels.size() * sizeof(uint16_t);
            size_t offset = 0;

            std::printf("SHOT_BEGIN w=%d h=%d fmt=rgb565le bytes=%u kind=formula\n",
                        job->width, job->height, static_cast<unsigned>(total));

            while (offset < total) {
                offset = emitRgb565ImageBase64Chunk(bytes, total, offset, 4);
                // Yield so UI and input handling stay responsive.
                vTaskDelay(1);
            }

            std::printf("SHOT_END\n");
            std::fflush(stdout);
            vTaskDelete(nullptr);
        }

        bool looksLikeMathExpr(const std::string &line)
        {
            return line.find_first_of("+-*/^()[]0123456789") != std::string::npos ||
                   line.find("sqrt") != std::string::npos ||
                   line.find("sum") != std::string::npos ||
                   line.find("product") != std::string::npos ||
                   line.find("limit") != std::string::npos;
        }

        bool isBracketMatrixLine(const std::string &line)
        {
            return line.size() >= 4 &&
                   line.front() == '[' && line[1] == '[' &&
                   line[line.size() - 2] == ']' && line.back() == ']';
        }

        std::string makeMatrixObjectSource(const std::string &line)
        {
            // Prefer native matrix parsing in mathlayout first.
            return std::string("matrix(") + line + ")";
        }

    } // namespace

    XcasUi::XcasUi(board::CardputerBsp &board, XcasService &service)
        : board_(board),
          service_(service),
          lv_display_(nullptr),
                    keyboard_indev_(nullptr),
                    input_group_(nullptr),
          header_label_(nullptr),
          status_label_(nullptr),
          info_label_(nullptr),
          history_panel_(nullptr),
          history_list_(nullptr),
          editor_panel_(nullptr),
          editor_preview_host_(nullptr),
          editor_preview_formula_(nullptr),
          editor_preview_label_(nullptr),
          editor_preview_back_btn_(nullptr),
          editor_hint_label_(nullptr),
          input_box_(nullptr),
          screen_keyboard_(nullptr),
          keyboard_mode_bar_(nullptr),
          root_(nullptr),
          selected_history_index_(-1),
          last_render_us_(0),
          lvgl_initialized_(false),
          redraw_recovery_pending_(false),
          subjects_initialized_(false)
    {
    }

    void XcasUi::lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
    {
        auto *self = static_cast<XcasUi *>(lv_display_get_user_data(disp));
        // if (self == nullptr) {
        //     lv_display_flush_ready(disp);
        //     return;
        // }

        const int x1 = static_cast<int>(area->x1);
        const int y1 = static_cast<int>(area->y1);
        const int x2 = static_cast<int>(area->x2);
        const int y2 = static_cast<int>(area->y2);

        // if (x2 < x1 || y2 < y1) {
        //     lv_display_flush_ready(disp);
        //     return;
        // }

        // const int clipped_x1 = (x1 < 0) ? 0 : x1;
        // const int clipped_y1 = (y1 < 0) ? 0 : y1;
        // const int clipped_x2 = (x2 >= board::CardputerBsp::kDisplayWidth) ? (board::CardputerBsp::kDisplayWidth - 1) : x2;
        // const int clipped_y2 = (y2 >= board::CardputerBsp::kDisplayHeight) ? (board::CardputerBsp::kDisplayHeight - 1) : y2;

        // if (clipped_x2 < clipped_x1 || clipped_y2 < clipped_y1) {
        //     lv_display_flush_ready(disp);
        //     return;
        // }

        // const int src_stride = (x2 - x1 + 1);
        // const int src_offset_x = (clipped_x1 - x1);
        // const int src_offset_y = (clipped_y1 - y1);
        // const int draw_width = (clipped_x2 - clipped_x1 + 1);
        // const int draw_height = (clipped_y2 - clipped_y1 + 1);
        const uint16_t *src = reinterpret_cast<const uint16_t *>(px_map);
        self->board_.presentArea(x1, y1, x2 + 1, y2 + 1, src);

        // if (clipped_x1 == x1 && clipped_y1 == y1 && clipped_x2 == x2 && clipped_y2 == y2) {
        //     self->board_.presentArea(x1, y1, x2 + 1, y2 + 1, src);
        // } else {
        //     for (int row = 0; row < draw_height; ++row) {
        //         const uint16_t *row_src = src + ((src_offset_y + row) * src_stride) + src_offset_x;
        //         self->board_.presentArea(clipped_x1, clipped_y1 + row, clipped_x1 + draw_width, clipped_y1 + row + 1, row_src);
        //     }
        // }
        lv_display_flush_ready(disp);
    }

    void XcasUi::onHistoryRowEvent(lv_event_t *e)
    {
        if (e == nullptr) {
            return;
        }

        auto *self = static_cast<XcasUi *>(lv_event_get_user_data(e));
        if (self == nullptr) {
            return;
        }

        lv_obj_t *row = static_cast<lv_obj_t *>(lv_event_get_target(e));
        if (row == nullptr) {
            return;
        }

        const intptr_t idx_tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
        const int index = static_cast<int>(idx_tag);
        self->selectHistoryIndex(index, false);
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            self->openHistoryFullscreenPreview();
        } else {
            self->setStatusText("History browse");
        }
    }

    void XcasUi::onPreviewBackEvent(lv_event_t *e)
    {
        auto *self = static_cast<XcasUi *>(lv_event_get_user_data(e));
        if (self == nullptr || lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        self->closeHistoryFullscreenPreview();
        self->setStatusText("Preview closed");
    }

    void XcasUi::lvglKeyboardRead(lv_indev_t *indev, lv_indev_data_t *data)
    {
        if (indev == nullptr || data == nullptr) {
            return;
        }

        auto *self = static_cast<XcasUi *>(lv_indev_get_user_data(indev));
        if (self == nullptr) {
            data->state = LV_INDEV_STATE_RELEASED;
            data->key = 0;
            return;
        }

        if (self->has_pending_release_) {
            data->key = 0;
            data->state = LV_INDEV_STATE_RELEASED;
            self->has_pending_release_ = false;
            return;
        }

        if (self->key_queue_head_ == self->key_queue_tail_) {
            data->key = 0;
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        const uint32_t key = self->key_queue_[self->key_queue_head_];
        self->key_queue_head_ = static_cast<uint8_t>((self->key_queue_head_ + 1U) % self->key_queue_.size());

        self->pending_release_key_ = 0;
        self->has_pending_release_ = true;

        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
    }

    void XcasUi::onInputBoxEvent(lv_event_t *e)
    {
        if (e == nullptr) {
            return;
        }

        auto *self = static_cast<XcasUi *>(lv_event_get_user_data(e));
        if (self == nullptr) {
            return;
        }

        const lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_FOCUSED || code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED) {
            if (self->board_.hasTouchInput()) {
                self->setScreenKeyboardVisible(true);
            }
            return;
        }

        if (code == LV_EVENT_READY) {
            if (self->selected_history_index_ >= 0) {
                self->selectHistoryIndex(self->selected_history_index_, true);
                self->clearHistorySelection();
                self->setStatusText("Loaded to input");
            } else {
                self->submitInput();
            }
            return;
        }

        if (code == LV_EVENT_VALUE_CHANGED) {
            return;
        }

        if (code != LV_EVENT_KEY) {
            return;
        }

        const uint32_t key = lv_event_get_key(e);
        if (key == 0) {
            return;
        }

        if (key == LV_KEY_UP) {
            self->moveHistorySelection(-1);
            self->setStatusText("History up");
            lv_event_stop_processing(e);
            return;
        }
        if (key == LV_KEY_DOWN) {
            self->moveHistorySelection(1);
            self->setStatusText("History down");
            lv_event_stop_processing(e);
            return;
        }
        if (key == '\t') {
            lv_event_stop_processing(e);
            return;
        }
        if (key == LV_KEY_ESC) {
            self->clearHistorySelection();
            if (self->input_box_ != nullptr) {
                lv_textarea_set_text(self->input_box_, "");
            }
            self->updateHeaderText();
            self->setStatusText("Esc");
            lv_event_stop_processing(e);
            return;
        }
        if (key == LV_KEY_DEL) {
            self->clearHistorySelection();
            if (self->input_box_ != nullptr) {
                lv_textarea_delete_char_forward(self->input_box_);
            }
            self->updateHeaderText();
            self->setStatusText("Del");
            lv_event_stop_processing(e);
            return;
        }

        self->clearHistorySelection();
    }

    void XcasUi::onKeyboardModeEvent(lv_event_t *e)
    {
        if (e == nullptr || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
            return;
        }

        auto *self = static_cast<XcasUi *>(lv_event_get_user_data(e));
        if (self == nullptr) {
            return;
        }

        auto *matrix = static_cast<lv_obj_t *>(lv_event_get_target(e));
        const uint32_t id = lv_buttonmatrix_get_selected_button(matrix);
        const char *text = lv_buttonmatrix_get_button_text(matrix, id);
        if (text == nullptr || std::strcmp(text, "ABC") == 0) {
            self->setScreenKeyboardMathMode(false);
        } else if (std::strcmp(text, "Func") == 0) {
            self->setScreenKeyboardMathPage(0);
        } else if (std::strcmp(text, "Sym") == 0) {
            self->setScreenKeyboardMathPage(1);
        } else if (std::strcmp(text, "123") == 0) {
            self->setScreenKeyboardMathPage(2);
        }
    }

    void XcasUi::onScreenKeyboardEvent(lv_event_t *e)
    {
        if (e == nullptr) {
            return;
        }

        auto *self = static_cast<XcasUi *>(lv_event_get_user_data(e));
        if (self == nullptr) {
            return;
        }

        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_READY) {
            self->submitInput();
            return;
        }
        if (code == LV_EVENT_CANCEL) {
            self->setScreenKeyboardVisible(false);
        }
    }

    void XcasUi::initializeLvgl()
    {
        if (lvgl_initialized_ && root_ != nullptr)
        {
            return;
        }

        if (lv_display_ == nullptr) {
            if (board_.usesExternalLvglPort()) {
                lv_display_ = lv_display_get_default();
            } else {
                lv_init();

                lv_display_ = lv_display_create(board_.displayWidth(), board_.displayHeight());
                lv_display_set_color_format(lv_display_, LV_COLOR_FORMAT_RGB565);
                lv_display_set_buffers(
                    lv_display_,
                    lv_draw_pixels_.data(),
                    nullptr,
                    static_cast<uint32_t>(lv_draw_pixels_.size() * sizeof(lv_draw_pixels_[0])),
                    LV_DISPLAY_RENDER_MODE_PARTIAL);
                lv_display_set_flush_cb(lv_display_, &XcasUi::lvglFlush);
                lv_display_set_user_data(lv_display_, this);
                lv_display_set_default(lv_display_);
            }

            if (board_.hasPhysicalKeyboard()) {
                keyboard_indev_ = lv_indev_create();
                lv_indev_set_type(keyboard_indev_, LV_INDEV_TYPE_KEYPAD);
                lv_indev_set_read_cb(keyboard_indev_, &XcasUi::lvglKeyboardRead);
                lv_indev_set_user_data(keyboard_indev_, this);

                input_group_ = lv_group_create();
                lv_group_set_default(input_group_);
                lv_indev_set_group(keyboard_indev_, input_group_);
            }
        } else if (input_group_ != nullptr) {
            lv_group_remove_all_objs(input_group_);
            lv_group_set_default(input_group_);
        }

        buildLayout();
        lvgl_initialized_ = true;
    }

    void XcasUi::buildLayout()
    {
        const lv_coord_t screen_w = static_cast<lv_coord_t>(board_.displayWidth());
        const lv_coord_t screen_h = static_cast<lv_coord_t>(board_.displayHeight());
        const lv_coord_t top_reserved = static_cast<lv_coord_t>(board_.statusBarHeight());
        const lv_coord_t bottom_reserved = screenKeyboardBottomReserved();

        lv_obj_t *screen = lv_scr_act();
        brookesia::ui_theme::applyPage(screen, kBgColor);
        lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

        lv_obj_t *root = lv_obj_create(screen);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, screen_w, screen_h - top_reserved - bottom_reserved);
        lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, top_reserved);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_left(root, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_right(root, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_top(root, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(root, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_row(root, 3, LV_PART_MAIN);

        header_label_ = nullptr;
        if (!subjects_initialized_) {
            lv_subject_init_string(&header_subject_, header_text_buf_.data(), header_text_prev_buf_.data(), header_text_buf_.size(), "");
            lv_subject_init_int(&busy_subject_, 0);
            subjects_initialized_ = true;
        }

        history_panel_ = lv_obj_create(root);
    #if LV_USE_OBJ_PROPERTY
        const lv_property_t history_props[] = {
            { .id = LV_PROPERTY_OBJ_W, .num = screen_w - 8 },
        };
        lv_obj_set_properties(history_panel_, history_props, sizeof(history_props) / sizeof(history_props[0]));
    #else
        lv_obj_set_width(history_panel_, screen_w - 8);
    #endif
        lv_obj_set_flex_grow(history_panel_, 1);
        lv_obj_set_style_min_height(history_panel_, 40, LV_PART_MAIN);
        lv_obj_set_style_radius(history_panel_, 6, LV_PART_MAIN);
        brookesia::ui_theme::applyPanel(history_panel_, kPanelColor, LV_COLOR_MAKE(208, 214, 224));
        lv_obj_set_scrollbar_mode(history_panel_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_flex_flow(history_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(history_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        history_list_ = lv_obj_create(history_panel_);
        lv_obj_set_width(history_list_, lv_pct(100));
        lv_obj_set_height(history_list_, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(history_list_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(history_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_bg_opa(history_list_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(history_list_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(history_list_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(history_list_, 1, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(history_list_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(history_list_, LV_OBJ_FLAG_SCROLLABLE);

        editor_panel_ = nullptr;
        editor_preview_host_ = nullptr;
        editor_preview_formula_ = nullptr;
        editor_preview_label_ = nullptr;
        editor_hint_label_ = nullptr;

        info_label_ = nullptr;

        input_box_ = lv_textarea_create(root);
    #if LV_USE_OBJ_PROPERTY
        const lv_property_t input_props[] = {
            { .id = LV_PROPERTY_OBJ_W, .num = screen_w - 8 },
                { .id = LV_PROPERTY_OBJ_H, .num = screen_h >= 480 ? 56 : 40 },
        };
        lv_obj_set_properties(input_box_, input_props, sizeof(input_props) / sizeof(input_props[0]));
    #else
            lv_obj_set_size(input_box_, screen_w - 8, screen_h >= 480 ? 56 : 40);
    #endif
        lv_textarea_set_one_line(input_box_, true);
            lv_textarea_set_placeholder_text(input_box_, "1+1");
            lv_obj_set_flex_grow(input_box_, 0);
        lv_obj_set_style_bg_color(input_box_, kPanelColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input_box_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(input_box_, kTextColor, LV_PART_MAIN);
        lv_obj_set_style_border_color(input_box_, LV_COLOR_MAKE(208, 214, 224), LV_PART_MAIN);
        lv_obj_set_style_border_width(input_box_, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_left(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_right(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_top(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(input_box_, kAccentColor, LV_PART_CURSOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(input_box_, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_DEFAULT);
        brookesia::ui_theme::applyText14(input_box_);
        lv_obj_set_style_text_color(input_box_, kTextColor, LV_PART_MAIN);
            lv_obj_add_flag(input_box_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_state(input_box_, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_READY, this);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_KEY, this);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_VALUE_CHANGED, this);

        if (input_group_ != nullptr) {
            lv_group_add_obj(input_group_, input_box_);
            lv_group_focus_obj(input_box_);
        }

        editor_preview_host_ = lv_obj_create(screen);
        lv_obj_remove_style_all(editor_preview_host_);
        lv_obj_set_size(editor_preview_host_, screen_w, screen_h - top_reserved - bottom_reserved);
        lv_obj_align(editor_preview_host_, LV_ALIGN_TOP_LEFT, 0, top_reserved);
        ui_theme::applyPage(editor_preview_host_, LV_COLOR_MAKE(8, 10, 14));
        lv_obj_add_flag(editor_preview_host_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(editor_preview_host_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_flag(editor_preview_host_, LV_OBJ_FLAG_HIDDEN);

        editor_hint_label_ = nullptr;

        editor_preview_formula_ = lv_obj_create(editor_preview_host_);
        lv_obj_remove_style_all(editor_preview_formula_);
        lv_obj_clear_flag(editor_preview_formula_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(editor_preview_formula_, LV_OBJ_FLAG_HIDDEN);

        editor_preview_label_ = lv_label_create(editor_preview_host_);
        brookesia::ui_theme::applyText14(editor_preview_label_);
        lv_obj_set_style_text_color(editor_preview_label_, LV_COLOR_MAKE(236, 240, 248), LV_PART_MAIN);
        lv_obj_set_width(editor_preview_label_, screen_w - 8);
        lv_label_set_long_mode(editor_preview_label_, LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(editor_preview_label_, LV_OBJ_FLAG_HIDDEN);

        editor_preview_back_btn_ = lv_button_create(screen);
        lv_obj_set_size(editor_preview_back_btn_, board_.hasTouchInput() ? 112 : 58, board_.hasTouchInput() ? 46 : 24);
        lv_obj_align(editor_preview_back_btn_, LV_ALIGN_TOP_RIGHT, -8, top_reserved + 6);
        lv_obj_add_flag(editor_preview_back_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(editor_preview_back_btn_, &XcasUi::onPreviewBackEvent, LV_EVENT_CLICKED, this);
        lv_obj_t *back_label = lv_label_create(editor_preview_back_btn_);
        brookesia::ui_theme::applyText14(back_label);
        lv_label_set_text(back_label, "Back");
        lv_obj_center(back_label);

        status_label_ = nullptr;

    #if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        static const char *const kKeyboardModeMap[] = {"ABC", "Func", "Sym", "123", ""};
        keyboard_mode_bar_ = lv_buttonmatrix_create(screen);
        lv_buttonmatrix_set_map(keyboard_mode_bar_, kKeyboardModeMap);
        lv_obj_set_size(keyboard_mode_bar_, screen_w, 56);
        lv_obj_align(keyboard_mode_bar_, LV_ALIGN_BOTTOM_MID, 0, -screenKeyboardHeight());
        lv_obj_set_style_bg_color(keyboard_mode_bar_, LV_COLOR_MAKE(28, 34, 48), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(keyboard_mode_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(keyboard_mode_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(keyboard_mode_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(keyboard_mode_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(keyboard_mode_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboard_mode_bar_, LV_COLOR_MAKE(245, 248, 252), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(keyboard_mode_bar_, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard_mode_bar_, LV_COLOR_MAKE(16, 24, 36), LV_PART_ITEMS);
        lv_obj_set_style_text_font(keyboard_mode_bar_, brookesia::ui_theme::textFont16(), LV_PART_ITEMS);
        lv_obj_set_style_radius(keyboard_mode_bar_, 6, LV_PART_ITEMS);
        lv_obj_set_style_pad_top(keyboard_mode_bar_, 8, LV_PART_ITEMS);
        lv_obj_set_style_pad_bottom(keyboard_mode_bar_, 8, LV_PART_ITEMS);
        lv_obj_add_event_cb(keyboard_mode_bar_, &XcasUi::onKeyboardModeEvent, LV_EVENT_VALUE_CHANGED, this);

        screen_keyboard_ = lv_keyboard_create(screen);
        lv_obj_set_size(screen_keyboard_, screen_w, screenKeyboardHeight());
        lv_obj_align(screen_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(screen_keyboard_, input_box_);
        lv_obj_set_style_text_font(screen_keyboard_, brookesia::ui_theme::textFont16(), LV_PART_ITEMS);
        lv_obj_add_event_cb(screen_keyboard_, &XcasUi::onScreenKeyboardEvent, LV_EVENT_READY, this);
        lv_obj_add_event_cb(screen_keyboard_, &XcasUi::onScreenKeyboardEvent, LV_EVENT_CANCEL, this);
        setScreenKeyboardMathMode(false);
        setScreenKeyboardVisible(screen_keyboard_visible_);
    #endif

        lv_style_init(&busy_style_);
        updateBusyBinding(false);
        root_ = root;
        if (history_lines_.empty()) {
            // history_lines_.push_back("CAS ready. Enter to eval.");
        }
        refreshHistoryList();
        setEditorFullscreen(false);
    }

    void XcasUi::setScreenKeyboardMathMode(bool enabled)
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        if (screen_keyboard_ == nullptr) {
            return;
        }
        screen_keyboard_math_ = enabled;
        if (enabled) {
            setScreenKeyboardMathPage(screen_keyboard_page_);
        } else {
            screen_keyboard_math_ = false;
            lv_keyboard_set_mode(screen_keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
        }
#else
        (void)enabled;
#endif
    }

    void XcasUi::setScreenKeyboardMathPage(int page)
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        if (screen_keyboard_ == nullptr) {
            return;
        }
        screen_keyboard_math_ = true;
        screen_keyboard_page_ = std::clamp(page, 0, 2);
        const char *const *map = kMathFuncKeyboardMap;
        if (screen_keyboard_page_ == 1) {
            map = kMathSymbolKeyboardMap;
        } else if (screen_keyboard_page_ == 2) {
            map = kMathNumberKeyboardMap;
        }
        lv_keyboard_set_map(screen_keyboard_, LV_KEYBOARD_MODE_USER_1, map, kMathKeyboardCtrl);
        lv_keyboard_set_mode(screen_keyboard_, LV_KEYBOARD_MODE_USER_1);
#else
        (void)page;
#endif
    }

    lv_coord_t XcasUi::screenKeyboardHeight() const
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        return static_cast<lv_coord_t>(std::clamp(board_.displayHeight() / 3, 180, 320));
#else
        return 0;
#endif
    }

    lv_coord_t XcasUi::screenKeyboardBottomReserved() const
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        return screen_keyboard_visible_ ? static_cast<lv_coord_t>(screenKeyboardHeight() + 56) : 0;
#else
        return 0;
#endif
    }

    void XcasUi::updateScreenKeyboardLayout()
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        const lv_coord_t screen_w = static_cast<lv_coord_t>(board_.displayWidth());
        const lv_coord_t screen_h = static_cast<lv_coord_t>(board_.displayHeight());
        const lv_coord_t top_reserved = static_cast<lv_coord_t>(board_.statusBarHeight());
        const lv_coord_t keyboard_h = screenKeyboardHeight();
        const lv_coord_t bottom_reserved = screenKeyboardBottomReserved();

        if (root_ != nullptr) {
            lv_obj_set_size(root_, screen_w, screen_h - top_reserved - bottom_reserved);
        }
        if (editor_preview_host_ != nullptr) {
            lv_obj_set_size(editor_preview_host_, screen_w, screen_h - top_reserved - bottom_reserved);
        }
        if (editor_preview_back_btn_ != nullptr) {
            lv_obj_align(editor_preview_back_btn_, LV_ALIGN_TOP_RIGHT, -8, top_reserved + 6);
        }
        if (keyboard_mode_bar_ != nullptr) {
            lv_obj_set_size(keyboard_mode_bar_, screen_w, 56);
            lv_obj_align(keyboard_mode_bar_, LV_ALIGN_BOTTOM_MID, 0, -keyboard_h);
        }
        if (screen_keyboard_ != nullptr) {
            lv_obj_set_size(screen_keyboard_, screen_w, keyboard_h);
            lv_obj_align(screen_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        updateFullscreenPreviewPosition();
#endif
    }

    void XcasUi::setScreenKeyboardVisible(bool visible)
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        screen_keyboard_visible_ = visible;
        if (screen_keyboard_ != nullptr) {
            if (visible) {
                lv_obj_clear_flag(screen_keyboard_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(screen_keyboard_);
            } else {
                lv_obj_add_flag(screen_keyboard_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (keyboard_mode_bar_ != nullptr) {
            if (visible) {
                lv_obj_clear_flag(keyboard_mode_bar_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(keyboard_mode_bar_);
            } else {
                lv_obj_add_flag(keyboard_mode_bar_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        updateScreenKeyboardLayout();
#else
        (void)visible;
#endif
    }

    bool XcasUi::toggleScreenKeyboard()
    {
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        initializeLvgl();
        setScreenKeyboardVisible(!screen_keyboard_visible_);
        return true;
#else
        return false;
#endif
    }

    void XcasUi::updateHeaderText()
    {
        // Header is intentionally hidden to maximize formula viewport.
    }

    void XcasUi::setStatusText(const char *text)
    {
        if (text == nullptr) {
            return;
        }

        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, text);
        }
    }

    void XcasUi::updateBusyBinding(bool busy)
    {
        if (subjects_initialized_) {
            lv_subject_set_int(&busy_subject_, busy ? 1 : 0);
        }
    }

    std::string XcasUi::trimCopy(const std::string &s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) {
            ++b;
        }
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) {
            --e;
        }
        return s.substr(b, e - b);
    }

    int XcasUi::findTopLevelChar(const std::string &s, char ch)
    {
        int depth = 0;
        for (int i = 0; i < static_cast<int>(s.size()); ++i) {
            const char c = s[static_cast<size_t>(i)];
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            } else if (c == ch && depth == 0) {
                return i;
            }
        }
        return -1;
    }

    bool XcasUi::hasOuterParens(const std::string &s)
    {
        if (s.size() < 2 || s.front() != '(' || s.back() != ')') {
            return false;
        }
        int depth = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
                if (depth == 0 && i + 1 < s.size()) {
                    return false;
                }
            }
        }
        return depth == 0;
    }

    std::string XcasUi::centerText(const std::string &s, int width)
    {
        if (static_cast<int>(s.size()) >= width) {
            return s;
        }
        const int gap = width - static_cast<int>(s.size());
        const int left = gap / 2;
        const int right = gap - left;
        return std::string(static_cast<size_t>(left), ' ') + s + std::string(static_cast<size_t>(right), ' ');
    }

    namespace
    {
        const char *mathSymbol(const char *utf8_symbol, const char *ascii_fallback, uint32_t codepoint)
        {
            LV_UNUSED(ascii_fallback);
            LV_UNUSED(codepoint);
            return utf8_symbol;
        }

        struct RenderBox
        {
            std::vector<std::string> lines;
            int baseline = 0;
        };

        std::string localTrimCopy(const std::string &s)
        {
            size_t b = 0;
            size_t e = s.size();
            while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) {
                ++b;
            }
            while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) {
                --e;
            }
            return s.substr(b, e - b);
        }

        std::string localCenterText(const std::string &s, int width)
        {
            if (static_cast<int>(s.size()) >= width) {
                return s;
            }
            const int gap = width - static_cast<int>(s.size());
            const int left = gap / 2;
            const int right = gap - left;
            return std::string(static_cast<size_t>(left), ' ') + s + std::string(static_cast<size_t>(right), ' ');
        }

        RenderBox makeTextBox(const std::string &text)
        {
            RenderBox box;
            box.lines.push_back(text);
            box.baseline = 0;
            return box;
        }

        std::vector<std::string> splitTopLevelArgs(const std::string &s)
        {
            std::vector<std::string> args;
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            size_t start = 0;

            for (size_t i = 0; i < s.size(); ++i) {
                const char c = s[i];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (c == ',' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
                    args.push_back(localTrimCopy(s.substr(start, i - start)));
                    start = i + 1;
                }
            }
            args.push_back(localTrimCopy(s.substr(start)));
            return args;
        }

        bool hasBalancedBrackets(const std::string &s)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            for (char c : s) {
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                    if (paren_depth < 0) {
                        return false;
                    }
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                    if (bracket_depth < 0) {
                        return false;
                    }
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                    if (brace_depth < 0) {
                        return false;
                    }
                }
            }
            return paren_depth == 0 && bracket_depth == 0 && brace_depth == 0;
        }

        int findRightmostTopLevelOp(const std::string &s, const char *ops, bool minus_binary_only)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            int pos = -1;

            auto is_binary_minus = [&s](int i) {
                if (i <= 0 || s[static_cast<size_t>(i)] != '-') {
                    return false;
                }
                int j = i - 1;
                while (j >= 0 && std::isspace(static_cast<unsigned char>(s[static_cast<size_t>(j)]))) {
                    --j;
                }
                if (j < 0) {
                    return false;
                }
                const char prev = s[static_cast<size_t>(j)];
                return !(prev == '(' || prev == '[' || prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '^' || prev == ',');
            };

            for (int i = 0; i < static_cast<int>(s.size()); ++i) {
                const char c = s[static_cast<size_t>(i)];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
                    for (const char *p = ops; *p != '\0'; ++p) {
                        if (c == *p) {
                            if (minus_binary_only && c == '-' && !is_binary_minus(i)) {
                                break;
                            }
                            pos = i;
                            break;
                        }
                    }
                }
            }
            return pos;
        }

        int findRightmostTopLevelRel(const std::string &s)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            int pos = -1;

            for (int i = 0; i < static_cast<int>(s.size()); ++i) {
                const char c = s[static_cast<size_t>(i)];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && c == '=') {
                    pos = i;
                }
            }
            return pos;
        }

        int findRightmostTopLevelComma(const std::string &s)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            int pos = -1;

            for (int i = 0; i < static_cast<int>(s.size()); ++i) {
                const char c = s[static_cast<size_t>(i)];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && c == ',') {
                    pos = i;
                }
            }
            return pos;
        }

        std::string joinWithAtomSpacing(const std::string &lhs, const std::string &op, const std::string &rhs, const bool punct)
        {
            if (punct) {
                return lhs + op + " " + rhs;
            }
            return lhs + " " + op + " " + rhs;
        }

        int findLeftmostTopLevelPow(const std::string &s)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            for (int i = 0; i < static_cast<int>(s.size()); ++i) {
                const char c = s[static_cast<size_t>(i)];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && c == '^') {
                    return i;
                }
            }
            return -1;
        }

        int findLeftmostTopLevelSub(const std::string &s)
        {
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            for (int i = 0; i < static_cast<int>(s.size()); ++i) {
                const char c = s[static_cast<size_t>(i)];
                if (c == '(') {
                    ++paren_depth;
                } else if (c == ')') {
                    --paren_depth;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                } else if (c == '{') {
                    ++brace_depth;
                } else if (c == '}') {
                    --brace_depth;
                } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && c == '_') {
                    return i;
                }
            }
            return -1;
        }

        bool extractScriptOperand(const std::string &s, size_t start, std::string &operand, size_t &next)
        {
            if (start >= s.size()) {
                return false;
            }

            if (s[start] == '{') {
                int depth = 1;
                size_t i = start + 1;
                while (i < s.size() && depth > 0) {
                    if (s[i] == '{') {
                        ++depth;
                    } else if (s[i] == '}') {
                        --depth;
                    }
                    ++i;
                }
                if (depth != 0 || i <= start + 1) {
                    return false;
                }
                operand = localTrimCopy(s.substr(start + 1, i - start - 2));
                next = i;
                return true;
            }

            size_t i = start;
            if (s[i] == '(' || s[i] == '[') {
                const char open = s[i];
                const char close = (open == '(') ? ')' : ']';
                int depth = 1;
                ++i;
                while (i < s.size() && depth > 0) {
                    if (s[i] == open) {
                        ++depth;
                    } else if (s[i] == close) {
                        --depth;
                    }
                    ++i;
                }
                if (depth != 0) {
                    return false;
                }
                operand = localTrimCopy(s.substr(start, i - start));
                next = i;
                return true;
            }

            if (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.') {
                ++i;
                while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.')) {
                    ++i;
                }
                operand = localTrimCopy(s.substr(start, i - start));
                next = i;
                return true;
            }

            operand = s.substr(start, 1);
            next = start + 1;
            return true;
        }

        // Flatten a RenderBox to a single-line string.
        std::string flatBox(const RenderBox &box)
        {
            if (box.lines.empty()) return "";
            if (box.lines.size() == 1) return localTrimCopy(box.lines.front());
            std::string s;
            for (size_t i = 0; i < box.lines.size(); ++i) {
                if (i) s += " ";
                s += localTrimCopy(box.lines[i]);
            }
            return s;
        }

        // Returns true if t needs outer parens when used as frac operand.
        bool needsParensInFrac(const std::string &t)
        {
            if (t.size() >= 2 && t.front() == '(' && t.back() == ')') return false;
            int d = 0;
            for (char c : t) {
                if (c == '(' || c == '[') ++d;
                else if (c == ')' || c == ']') --d;
                else if (d == 0 && (c == '+' || c == '-' || c == '=' || c == ',')) return true;
            }
            return false;
        }

        RenderBox fracBox(RenderBox num, RenderBox den)
        {
            // Compact inline style keeps numerator/denominator visually tighter.
            std::string ns = flatBox(num);
            std::string ds = flatBox(den);
            if (needsParensInFrac(ns)) ns = "(" + ns + ")";
            if (needsParensInFrac(ds)) ds = "(" + ds + ")";
            return makeTextBox(ns + " / " + ds);
        }

        RenderBox powerBox(RenderBox base, RenderBox exp)
        {
            // Inline ASCII representation: base^(exp) — avoids multi-line
            // spacing that lv_label wraps incorrectly.
            // Use compact notation: base followed by ^(exp) on same line,
            // except when base itself is multi-line (e.g. fraction).
            const std::string base_flat = (base.lines.size() == 1)
                ? localTrimCopy(base.lines.front())
                : ([&base]() {
                      std::string s;
                      for (size_t i = 0; i < base.lines.size(); ++i) {
                          if (i) s += " ";
                          s += localTrimCopy(base.lines[i]);
                      }
                      return s;
                  })();
            const std::string exp_flat = (exp.lines.size() == 1)
                ? localTrimCopy(exp.lines.front())
                : ([&exp]() {
                      std::string s;
                      for (size_t i = 0; i < exp.lines.size(); ++i) {
                          if (i) s += " ";
                          s += localTrimCopy(exp.lines[i]);
                      }
                      return s;
                  })();

            // Wrap exp in parens only if it contains operators
            const bool exp_needs_parens =
                exp_flat.find_first_of("+-*/^_,") != std::string::npos ||
                exp_flat.size() > 1;
            const std::string exp_str = exp_needs_parens ? ("^(" + exp_flat + ")") : ("^" + exp_flat);

            return makeTextBox(base_flat + exp_str);
        }

        RenderBox subscriptBox(RenderBox base, RenderBox sub)
        {
            const std::string base_flat = (base.lines.size() == 1)
                ? localTrimCopy(base.lines.front())
                : ([&base]() {
                      std::string s;
                      for (size_t i = 0; i < base.lines.size(); ++i) {
                          if (i) s += " ";
                          s += localTrimCopy(base.lines[i]);
                      }
                      return s;
                  })();
            const std::string sub_flat = (sub.lines.size() == 1)
                ? localTrimCopy(sub.lines.front())
                : ([&sub]() {
                      std::string s;
                      for (size_t i = 0; i < sub.lines.size(); ++i) {
                          if (i) s += " ";
                          s += localTrimCopy(sub.lines[i]);
                      }
                      return s;
                  })();
            const bool sub_needs_parens =
                sub_flat.find_first_of("+-*/^_,") != std::string::npos ||
                sub_flat.size() > 1;
            const std::string sub_str = sub_needs_parens ? ("_(" + sub_flat + ")") : ("_" + sub_flat);

            return makeTextBox(base_flat + sub_str);
        }

        RenderBox sqrtBox(RenderBox inner)
        {
            return makeTextBox(std::string(mathSymbol("√", "sqrt", 0x221A)) + "(" + flatBox(inner) + ")");
        }

        RenderBox matrixBox(const std::vector<std::vector<std::string>> &rows)
        {
            if (rows.empty()) {
                return makeTextBox("[]");
            }

            size_t max_cols = 0;
            for (const auto &row : rows) {
                max_cols = std::max(max_cols, row.size());
            }

            std::vector<size_t> col_w(max_cols, 1);
            for (const auto &row : rows) {
                for (size_t c = 0; c < row.size(); ++c) {
                    col_w[c] = std::max(col_w[c], localTrimCopy(row[c]).size());
                }
            }

            RenderBox out;
            out.baseline = static_cast<int>(rows.size() / 2);
            for (size_t r = 0; r < rows.size(); ++r) {
                std::string line = "[ ";

                for (size_t c = 0; c < max_cols; ++c) {
                    std::string cell = (c < rows[r].size()) ? localTrimCopy(rows[r][c]) : "";
                    if (cell.empty()) {
                        cell = "0";
                    }
                    line += localCenterText(cell, static_cast<int>(col_w[c]));
                    if (c + 1 < max_cols) {
                        line += "  ";
                    }
                }
                line += " ]";
                out.lines.push_back(line);
            }
            return out;
        }
    } // namespace

    std::string XcasUi::renderNatural2D(const std::string &expr, int depth)
    {
        if (depth > 8) {
            return expr;
        }

        std::string s = trimCopy(expr);
        if (s.empty()) {
            return s;
        }

        if (!hasBalancedBrackets(s)) {
            return s;
        }

        while (hasOuterParens(s)) {
            s = trimCopy(s.substr(1, s.size() - 2));
        }

        const int rel_pos = findRightmostTopLevelRel(s);
        if (rel_pos > 0 && rel_pos + 1 < static_cast<int>(s.size())) {
            const std::string lhs = renderNatural2D(s.substr(0, static_cast<size_t>(rel_pos)), depth + 1);
            const std::string rhs = renderNatural2D(s.substr(static_cast<size_t>(rel_pos + 1)), depth + 1);
            return joinWithAtomSpacing(lhs, "=", rhs, false);
        }

        const int comma_pos = findRightmostTopLevelComma(s);
        if (comma_pos > 0 && comma_pos + 1 < static_cast<int>(s.size())) {
            const std::string lhs = renderNatural2D(s.substr(0, static_cast<size_t>(comma_pos)), depth + 1);
            const std::string rhs = renderNatural2D(s.substr(static_cast<size_t>(comma_pos + 1)), depth + 1);
            return joinWithAtomSpacing(lhs, ",", rhs, true);
        }

        const int add_sub_pos = findRightmostTopLevelOp(s, "+-", true);
        if (add_sub_pos > 0 && add_sub_pos + 1 < static_cast<int>(s.size())) {
            const char op = s[static_cast<size_t>(add_sub_pos)];
            const std::string lhs = renderNatural2D(s.substr(0, static_cast<size_t>(add_sub_pos)), depth + 1);
            const std::string rhs = renderNatural2D(s.substr(static_cast<size_t>(add_sub_pos + 1)), depth + 1);
            return joinWithAtomSpacing(lhs, (op == '+') ? "+" : "−", rhs, false);
        }

        const int mul_pos = findRightmostTopLevelOp(s, "*", false);
        if (mul_pos > 0 && mul_pos + 1 < static_cast<int>(s.size())) {
            const std::string lhs_raw = trimCopy(s.substr(0, static_cast<size_t>(mul_pos)));
            const std::string rhs_raw = trimCopy(s.substr(static_cast<size_t>(mul_pos + 1)));

            auto is_identifier = [](const std::string &name) {
                if (name.empty()) {
                    return false;
                }
                const unsigned char c0 = static_cast<unsigned char>(name.front());
                if (!(std::isalpha(c0) || c0 == '_')) {
                    return false;
                }
                for (size_t i = 1; i < name.size(); ++i) {
                    const unsigned char c = static_cast<unsigned char>(name[i]);
                    if (!(std::isalnum(c) || c == '_')) {
                        return false;
                    }
                }
                return true;
            };

            if (is_identifier(lhs_raw) && hasOuterParens(rhs_raw) && rhs_raw.size() >= 2) {
                const std::string arg_text = rhs_raw.substr(1, rhs_raw.size() - 2);
                if (findRightmostTopLevelComma(arg_text) >= 0) {
                    const std::vector<std::string> args = splitTopLevelArgs(arg_text);
                    std::string rendered_args;
                    for (size_t i = 0; i < args.size(); ++i) {
                        std::string arg = renderNatural2D(args[i], depth + 1);
                        std::replace(arg.begin(), arg.end(), '\n', ' ');
                        arg = trimCopy(arg);
                        if (i != 0) {
                            rendered_args += ", ";
                        }
                        rendered_args += arg;
                    }
                    return lhs_raw + "(" + rendered_args + ")";
                }
            }

            auto render_group_aware = [depth](const std::string &segment) {
                const std::string trimmed = trimCopy(segment);
                if (trimmed.empty()) {
                    return trimmed;
                }
                if (hasOuterParens(trimmed)) {
                    std::string inner = renderNatural2D(trimmed.substr(1, trimmed.size() - 2), depth + 1);
                    std::replace(inner.begin(), inner.end(), '\n', ' ');
                    return std::string("(") + trimCopy(inner) + ")";
                }
                return renderNatural2D(trimmed, depth + 1);
            };
            const std::string lhs = render_group_aware(s.substr(0, static_cast<size_t>(mul_pos)));
            const std::string rhs = render_group_aware(s.substr(static_cast<size_t>(mul_pos + 1)));
            // `×` may be absent in the active UI font on device, which renders as blank.
            // Use '*' to keep multiplication visible in all themes/fonts.
            const char *mul = "*";
            return joinWithAtomSpacing(lhs, mul, rhs, false);
        }

        const int div_pos = findRightmostTopLevelOp(s, "/", false);
        if (div_pos > 0 && div_pos + 1 < static_cast<int>(s.size())) {
            RenderBox n = makeTextBox(renderNatural2D(s.substr(0, static_cast<size_t>(div_pos)), depth + 1));
            RenderBox d = makeTextBox(renderNatural2D(s.substr(static_cast<size_t>(div_pos + 1)), depth + 1));
            return flatBox(fracBox(n, d));
        }

        const int sub_pos = findLeftmostTopLevelSub(s);
        const int pow_pos = findLeftmostTopLevelPow(s);
        const int first_script_pos =
            (sub_pos < 0) ? pow_pos : ((pow_pos < 0) ? sub_pos : std::min(sub_pos, pow_pos));

        if (first_script_pos > 0) {
            const std::string base_src = trimCopy(s.substr(0, static_cast<size_t>(first_script_pos)));
            if (!base_src.empty()) {
                std::string sup_src;
                std::string sub_src;
                size_t i = static_cast<size_t>(first_script_pos);
                bool parsed_any = false;
                while (i < s.size()) {
                    const char op = s[i];
                    if (op != '^' && op != '_') {
                        break;
                    }
                    std::string operand;
                    size_t next = i + 1;
                    if (!extractScriptOperand(s, i + 1, operand, next)) {
                        break;
                    }
                    parsed_any = true;
                    if (op == '^' && sup_src.empty()) {
                        sup_src = operand;
                    } else if (op == '_' && sub_src.empty()) {
                        sub_src = operand;
                    }
                    i = next;
                }

                if (parsed_any && i == s.size()) {
                    RenderBox base_box = makeTextBox(renderNatural2D(base_src, depth + 1));

                    if (!sup_src.empty() && sub_src.empty()) {
                        // Pure superscript: base with exp in top-right
                        RenderBox exp_box = makeTextBox(renderNatural2D(sup_src, depth + 1));
                        RenderBox result = powerBox(base_box, exp_box);
                        std::string out;
                        for (size_t li = 0; li < result.lines.size(); ++li) {
                            if (li != 0) out += "\n";
                            out += result.lines[li];
                        }
                        return out;
                    }

                    if (sub_src.empty() == false && sup_src.empty()) {
                        // Pure subscript: base with sub in bottom-right
                        RenderBox sub_box = makeTextBox(renderNatural2D(sub_src, depth + 1));
                        RenderBox result = subscriptBox(base_box, sub_box);
                        std::string out;
                        for (size_t li = 0; li < result.lines.size(); ++li) {
                            if (li != 0) out += "\n";
                            out += result.lines[li];
                        }
                        return out;
                    }

                    if (!sub_src.empty() && !sup_src.empty()) {
                        // Both: stack sub below base first, then put sup on top-right
                        RenderBox sub_box = makeTextBox(renderNatural2D(sub_src, depth + 1));
                        RenderBox exp_box = makeTextBox(renderNatural2D(sup_src, depth + 1));
                        RenderBox with_sub = subscriptBox(base_box, sub_box);
                        RenderBox result   = powerBox(with_sub, exp_box);
                        std::string out;
                        for (size_t li = 0; li < result.lines.size(); ++li) {
                            if (li != 0) out += "\n";
                            out += result.lines[li];
                        }
                        return out;
                    }
                }
            }
        }

        if (pow_pos > 0 && pow_pos + 1 < static_cast<int>(s.size())) {
            RenderBox base = makeTextBox(renderNatural2D(s.substr(0, static_cast<size_t>(pow_pos)), depth + 1));
            RenderBox exp = makeTextBox(renderNatural2D(s.substr(static_cast<size_t>(pow_pos + 1)), depth + 1));
            RenderBox p = powerBox(base, exp);
            std::string out;
            for (size_t i = 0; i < p.lines.size(); ++i) {
                if (i != 0) {
                    out += "\n";
                }
                out += p.lines[i];
            }
            return out;
        }

        const size_t lp = s.find('(');
        if (lp != std::string::npos && s.back() == ')' && lp > 0) {
            const std::string fn = trimCopy(s.substr(0, lp));
            const std::string arg_text = s.substr(lp + 1, s.size() - lp - 2);
            const std::vector<std::string> args = splitTopLevelArgs(arg_text);

            if ((fn == "sqrt" || fn == "root") && args.size() == 1) {
                RenderBox in = makeTextBox(renderNatural2D(args[0], depth + 1));
                return flatBox(sqrtBox(in));
            }

            if ((fn == "sum" || fn == "sigma") && args.size() >= 4) {
                const std::string body = renderNatural2D(args[0], depth + 1);
                const std::string var = trimCopy(args[1]);
                const std::string lo = renderNatural2D(args[2], depth + 1);
                const std::string hi = renderNatural2D(args[3], depth + 1);
                return std::string(mathSymbol("∑", "sum", 0x2211)) + "_(" + var + "=" + lo + ")^(" + hi + ") " + body;
            }

            if ((fn == "product" || fn == "prod") && args.size() >= 4) {
                const std::string body = renderNatural2D(args[0], depth + 1);
                const std::string var = trimCopy(args[1]);
                const std::string lo = renderNatural2D(args[2], depth + 1);
                const std::string hi = renderNatural2D(args[3], depth + 1);
                return std::string(mathSymbol("∏", "prod", 0x220F)) + "_(" + var + "=" + lo + ")^(" + hi + ") " + body;
            }

            if ((fn == "limit" || fn == "lim") && args.size() >= 3) {
                const std::string body = renderNatural2D(args[0], depth + 1);
                const std::string var = trimCopy(args[1]);
                const std::string at = renderNatural2D(args[2], depth + 1);
                const char *arrow = mathSymbol("→", "->", 0x2192);
                return "lim\n" + var + arrow + at + "  " + body;
            }

            if (fn == "matrix" && !args.empty()) {
                std::string raw = trimCopy(args[0]);
                if (raw.size() >= 4 && raw.front() == '[' && raw[1] == '[' && raw[raw.size() - 2] == ']' && raw.back() == ']') {
                    raw = raw.substr(1, raw.size() - 2);
                }

                if (raw.size() >= 2 && raw.front() == '[' && raw.back() == ']') {
                    raw = raw.substr(1, raw.size() - 2);
                    std::vector<std::string> row_exprs = splitTopLevelArgs(raw);
                    std::vector<std::vector<std::string>> rows;
                    rows.reserve(row_exprs.size());

                    for (std::string row_expr : row_exprs) {
                        row_expr = trimCopy(row_expr);
                        if (row_expr.size() >= 2 && row_expr.front() == '[' && row_expr.back() == ']') {
                            row_expr = row_expr.substr(1, row_expr.size() - 2);
                        }
                        std::vector<std::string> cols = splitTopLevelArgs(row_expr);
                        for (std::string &cell : cols) {
                            cell = renderNatural2D(cell, depth + 1);
                            std::replace(cell.begin(), cell.end(), '\n', ' ');
                            cell = trimCopy(cell);
                        }
                        rows.push_back(std::move(cols));
                    }

                    RenderBox m = matrixBox(rows);
                    std::string out;
                    for (size_t i = 0; i < m.lines.size(); ++i) {
                        if (i != 0) {
                            out += "\n";
                        }
                        out += m.lines[i];
                    }
                    return out;
                }
            }

            if (fn == "pow" && args.size() == 2) {
                RenderBox base = makeTextBox(renderNatural2D(args[0], depth + 1));
                RenderBox exp = makeTextBox(renderNatural2D(args[1], depth + 1));
                RenderBox p = powerBox(base, exp);
                std::string out;
                for (size_t i = 0; i < p.lines.size(); ++i) {
                    if (i != 0) {
                        out += "\n";
                    }
                    out += p.lines[i];
                }
                return out;
            }

            std::string rendered_args;
            for (size_t i = 0; i < args.size(); ++i) {
                std::string arg = renderNatural2D(args[i], depth + 1);
                std::replace(arg.begin(), arg.end(), '\n', ' ');
                arg = trimCopy(arg);
                if (i != 0) {
                    rendered_args += ", ";
                }
                rendered_args += arg;
            }
            return fn + "(" + rendered_args + ")";
        }

        if (s.size() >= 4 && s.front() == '[' && s[1] == '[' && s[s.size() - 2] == ']' && s.back() == ']') {
            std::string raw = s.substr(1, s.size() - 2);
            raw = raw.substr(1, raw.size() - 2);
            std::vector<std::string> row_exprs = splitTopLevelArgs(raw);
            std::vector<std::vector<std::string>> rows;
            rows.reserve(row_exprs.size());
            for (std::string row_expr : row_exprs) {
                row_expr = trimCopy(row_expr);
                if (row_expr.size() >= 2 && row_expr.front() == '[' && row_expr.back() == ']') {
                    row_expr = row_expr.substr(1, row_expr.size() - 2);
                }
                std::vector<std::string> cols = splitTopLevelArgs(row_expr);
                for (std::string &cell : cols) {
                    cell = renderNatural2D(cell, depth + 1);
                    std::replace(cell.begin(), cell.end(), '\n', ' ');
                    cell = trimCopy(cell);
                }
                rows.push_back(std::move(cols));
            }
            RenderBox m = matrixBox(rows);
            std::string out;
            for (size_t i = 0; i < m.lines.size(); ++i) {
                if (i != 0) {
                    out += "\n";
                }
                out += m.lines[i];
            }
            return out;
        }

        return s;
    }

    void XcasUi::appendInput(const char *text)
    {
        if (input_box_ == nullptr || text == nullptr)
        {
            return;
        }
        lv_textarea_add_text(input_box_, text);
    }

    void XcasUi::setEditorFullscreen(bool enabled)
    {
        editor_fullscreen_ = enabled;

        if (root_ != nullptr) {
            if (enabled) {
                lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (editor_preview_host_ != nullptr) {
            if (enabled) {
                lv_obj_clear_flag(editor_preview_host_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(editor_preview_host_);
            } else {
                lv_obj_add_flag(editor_preview_host_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (editor_preview_back_btn_ != nullptr) {
            if (enabled) {
                lv_obj_clear_flag(editor_preview_back_btn_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(editor_preview_back_btn_);
            } else {
                lv_obj_add_flag(editor_preview_back_btn_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (!enabled && input_group_ != nullptr && input_box_ != nullptr) {
            lv_group_focus_obj(input_box_);
        }

        updateHeaderText();
    }

    void XcasUi::refreshEditorPreview()
    {
        // Single-box editor mode: no detached preview panel.
    }

    void XcasUi::openHistoryFullscreenPreview()
    {
        if (selected_history_index_ < 0 || selected_history_index_ >= static_cast<int>(history_lines_.size())) {
            return;
        }

        std::string line = history_lines_[static_cast<size_t>(selected_history_index_)];
        if (line.size() > 2 && line[0] == '>' && line[1] == ' ') {
            line = line.substr(2);
        }

        preview_line_ = line;
        preview_pan_x_ = 0;
        preview_pan_y_ = 0;
        renderFullscreenPreview(preview_line_);
        setEditorFullscreen(true);
        setStatusText("Preview");
    }

    void XcasUi::closeHistoryFullscreenPreview()
    {
        setEditorFullscreen(false);
        preview_line_.clear();
        preview_use_objects_ = false;
        preview_paint_pending_ = false;
        preview_draw_list_ = {};
        preview_paint_state_ = {};
        preview_content_w_ = 0;
        preview_content_h_ = 0;
        preview_pan_x_ = 0;
        preview_pan_y_ = 0;
    }

    void XcasUi::updateFullscreenPreviewPosition()
    {
        if (editor_preview_host_ == nullptr) {
            return;
        }

        constexpr int kTopInset = 18;
        const int view_w = board_.displayWidth();
        const int view_h = board_.displayHeight() - board_.statusBarHeight() - kTopInset;

        lv_obj_t *obj = preview_use_objects_ ? editor_preview_formula_ : editor_preview_label_;
        if (obj == nullptr || preview_content_w_ <= 0 || preview_content_h_ <= 0) {
            return;
        }

        if (preview_use_objects_) {
            lv_obj_set_pos(editor_preview_formula_, 0, kTopInset);
            lv_obj_set_size(editor_preview_formula_, view_w, view_h);
            return;
        }

        int x = 0;
        if (preview_content_w_ <= view_w) {
            x = (view_w - preview_content_w_) / 2;
        } else {
            const int min_x = view_w - preview_content_w_;
            x = std::clamp(preview_pan_x_, min_x, 0);
        }

        int y = 0;
        if (preview_content_h_ <= view_h) {
            y = kTopInset + (view_h - preview_content_h_) / 2;
        } else {
            const int min_y = kTopInset + (view_h - preview_content_h_);
            y = std::clamp(kTopInset + preview_pan_y_, min_y, kTopInset);
        }

        lv_obj_set_pos(obj, x, y);
    }

    bool XcasUi::beginFullscreenPreviewPaint()
    {
        if (!preview_use_objects_ || editor_preview_formula_ == nullptr) {
            return false;
        }

        constexpr int kTopInset = 18;
        const int view_w = board_.displayWidth();
        const int view_h = board_.displayHeight() - board_.statusBarHeight() - kTopInset;
        if (view_w <= 0 || view_h <= 0 || preview_content_w_ <= 0 || preview_content_h_ <= 0) {
            return false;
        }

        int draw_x = 0;
        if (preview_content_w_ <= view_w) {
            draw_x = (view_w - preview_content_w_) / 2;
        } else {
            const int min_x = view_w - preview_content_w_;
            draw_x = std::clamp(preview_pan_x_, min_x, 0);
        }

        int draw_y = 0;
        if (preview_content_h_ <= view_h) {
            draw_y = (view_h - preview_content_h_) / 2;
        } else {
            const int min_y = view_h - preview_content_h_;
            draw_y = std::clamp(preview_pan_y_, min_y, 0);
        }

        mathlayout::PaintViewport viewport;
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = view_w;
        viewport.height = view_h;

        const bool begin_ok = mathlayout::beginTileRenderToLvglObjects(
            editor_preview_formula_,
            preview_draw_list_,
            LV_COLOR_MAKE(8, 10, 14),
            preview_paint_state_,
            draw_x,
            draw_y,
            &viewport);
        if (!begin_ok) {
            preview_paint_pending_ = false;
            return false;
        }

        preview_paint_pending_ = true;
        updateFullscreenPreviewPosition();
        // First chunk: line-only, keep entry frame light.
        stepFullscreenPreviewPaint(kBoardTab5 ? 32 : 64, kBoardTab5 ? 8 : 48);
        return true;
    }

    void XcasUi::stepFullscreenPreviewPaint(size_t max_commands, size_t max_line_commands)
    {
        if (!preview_paint_pending_ || !preview_use_objects_ || editor_preview_formula_ == nullptr) {
            return;
        }

        const lv_font_t *math_font = brookesia::ui_theme::textFont14();
        bool finished = false;
        const bool step_ok = mathlayout::stepTileRenderToLvglObjects(
            editor_preview_formula_,
            preview_draw_list_,
            math_font,
            LV_COLOR_MAKE(236, 240, 248),
            preview_paint_state_,
            max_commands,
            max_line_commands,
            &finished);
        if (!step_ok) {
            preview_paint_pending_ = false;
            setStatusText("Preview paint failed");
            return;
        }

        if (finished) {
            preview_paint_pending_ = false;
        }

        if (kBoardTab5) {
            vTaskDelay(1);
        }
    }

    void XcasUi::panFullscreenPreview(int dx, int dy)
    {
        if (!editor_fullscreen_) {
            return;
        }

        preview_pan_x_ += dx;
        preview_pan_y_ += dy;
        if (preview_use_objects_) {
            (void)beginFullscreenPreviewPaint();
        } else {
            updateFullscreenPreviewPosition();
        }
    }

    void XcasUi::renderFullscreenPreview(const std::string &line)
    {
        if (editor_preview_host_ == nullptr || editor_preview_formula_ == nullptr) {
            return;
        }

        preview_use_objects_ = false;
        preview_content_w_ = 0;
        preview_content_h_ = 0;

        lv_obj_add_flag(editor_preview_formula_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(editor_preview_formula_);
        if (editor_preview_label_ != nullptr) {
            lv_obj_add_flag(editor_preview_label_, LV_OBJ_FLAG_HIDDEN);
        }

        auto showTextPreview = [&](const char *status) {
            if (editor_preview_label_ == nullptr) {
                return;
            }
            preview_use_objects_ = false;
            preview_paint_pending_ = false;
            preview_draw_list_ = {};
            const std::string fallback = renderNatural2D(line);
            lv_label_set_text(editor_preview_label_, fallback.c_str());
            lv_obj_clear_flag(editor_preview_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(editor_preview_label_);
            preview_content_w_ = lv_obj_get_width(editor_preview_label_);
            preview_content_h_ = lv_obj_get_height(editor_preview_label_);
            if (preview_content_w_ <= 0) {
                preview_content_w_ = board_.displayWidth();
            }
            if (preview_content_h_ <= 0) {
                preview_content_h_ = 20;
            }
            setStatusText(status);
            updateFullscreenPreviewPosition();
        };

        brookesia::settings::load();
        const bool force_text_preview = brookesia::settings::get().formula_preview_mode == 1;
        if (force_text_preview) {
            showTextPreview("Preview text");
            return;
        }

        const lv_font_t *math_font = brookesia::ui_theme::textFont14();
        const bool bracket_matrix = isBracketMatrixLine(line);
        std::vector<std::string> candidates;
        candidates.reserve(3);
        if (bracket_matrix) {
            candidates.push_back(makeMatrixObjectSource(line));
            candidates.push_back(renderNatural2D(line));
            candidates.push_back(line);
        } else {
            candidates.push_back(line);
            candidates.push_back(renderNatural2D(line));
        }

        const int view_w = board_.displayWidth() - 8;
        const int view_h = board_.displayHeight() - board_.statusBarHeight() - 12;
        const int max_object_area = 480000;
        std::string fail_reason = "unknown";
        int fail_w = 0;
        int fail_h = 0;
        int fail_area = 0;
        size_t fail_candidate = 0;
        for (const std::string &object_source : candidates) {
            xcas::mathlayout::DrawList draw_list;
            try {
                const xcas::mathlayout::TextBox box = xcas::mathlayout::renderText(object_source);
                draw_list = xcas::mathlayout::buildDrawList(box, math_font);
            }
            catch (const std::bad_alloc &) {
                fail_reason = "build_bad_alloc";
                ++fail_candidate;
                continue;
            }
            draw_list = scaleDrawListToFit(draw_list, view_w, view_h);
            const int area = draw_list.width * draw_list.height;
            ++fail_candidate;
            fail_w = draw_list.width;
            fail_h = draw_list.height;
            fail_area = area;

            if (draw_list.width <= 0 || draw_list.height <= 0) {
                fail_reason = "empty_draw_list";
                continue;
            }
            if (draw_list.width > 4096 || draw_list.height > 4096) {
                fail_reason = "oversize_dim";
                continue;
            }
            if (area <= 0) {
                fail_reason = "invalid_area";
                continue;
            }
            if (area > max_object_area) {
                fail_reason = "area_limit";
                continue;
            }

            const bool can_objects =
                draw_list.width > 0 && draw_list.height > 0 &&
                draw_list.width <= 4096 && draw_list.height <= 4096 &&
                area > 0 && area <= max_object_area;
            if (!can_objects) {
                fail_reason = "constraint_reject";
                continue;
            }

            {
                preview_use_objects_ = true;
                preview_draw_list_ = std::move(draw_list);
                preview_content_w_ = preview_draw_list_.width;
                preview_content_h_ = preview_draw_list_.height;
                lv_obj_clear_flag(editor_preview_formula_, LV_OBJ_FLAG_HIDDEN);
                if (beginFullscreenPreviewPaint()) {
                    break;
                }
                preview_use_objects_ = false;
                preview_draw_list_ = {};
                preview_content_w_ = 0;
                preview_content_h_ = 0;
                lv_obj_add_flag(editor_preview_formula_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clean(editor_preview_formula_);
                fail_reason = "paint_init_failed";
                continue;
            }
        }

        if (!preview_use_objects_) {
            char status[96];
            std::snprintf(status, sizeof(status), "Preview fallback: %s", fail_reason.c_str());
            showTextPreview(status);
            std::printf("ML_UI preview_object_fail reason=%s cand=%u w=%d h=%d area=%d\n",
                        fail_reason.c_str(),
                        static_cast<unsigned>(fail_candidate),
                        fail_w,
                        fail_h,
                        fail_area);
            std::fflush(stdout);
        }

        updateFullscreenPreviewPosition();
    }

    void XcasUi::appendHistory(const std::string &line)
    {
        if (history_list_ == nullptr || history_panel_ == nullptr)
        {
            return;
        }
        if (!lv_obj_is_valid(history_list_) || !lv_obj_is_valid(history_panel_)) {
            return;
        }

        history_lines_.push_back(line);
        const std::size_t kMaxHistoryLines = 16;
        if (history_lines_.size() > kMaxHistoryLines) {
            history_lines_.erase(history_lines_.begin(), history_lines_.begin() + static_cast<long>(history_lines_.size() - kMaxHistoryLines));
        }
        refreshHistoryList();
        // Auto-scroll to bottom for new results (only if no manual selection active)
        if (selected_history_index_ < 0) {
            lv_obj_scroll_to_y(history_panel_, LV_COORD_MAX, LV_ANIM_ON);
        }
    }

    void XcasUi::refreshHistoryList()
    {
        if (history_list_ == nullptr || history_panel_ == nullptr) {
            return;
        }
        if (!lv_obj_is_valid(history_list_) || !lv_obj_is_valid(history_panel_)) {
            return;
        }

        lv_obj_clean(history_list_);
        const size_t newest_index = history_lines_.empty() ? 0U : (history_lines_.size() - 1U);
        const bool history_heavy_mode = history_lines_.size() > 10U;
        for (size_t i = 0; i < history_lines_.size(); ++i) {
            const std::string &line = history_lines_[i];
            const bool is_input = (line.size() >= 2 && line[0] == '>' && line[1] == ' ');
            const bool is_selected = (selected_history_index_ == static_cast<int>(i));
            const bool is_math_output = (!is_input && looksLikeMathExpr(line));
            const bool is_bracket_matrix_line =
                line.size() >= 4 &&
                line.front() == '[' && line[1] == '[' &&
                line[line.size() - 2] == ']' && line.back() == ']';

            lv_obj_t *row = lv_obj_create(history_list_);
            lv_obj_set_width(row, lv_pct(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                row,
                is_input ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_START);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(row, &XcasUi::onHistoryRowEvent, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(row, &XcasUi::onHistoryRowEvent, LV_EVENT_DOUBLE_CLICKED, this);

            lv_obj_t *bubble = lv_obj_create(row);
            lv_obj_set_user_data(bubble, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(bubble, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_event_cb(bubble, &XcasUi::onHistoryRowEvent, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(bubble, &XcasUi::onHistoryRowEvent, LV_EVENT_DOUBLE_CLICKED, this);
            const lv_coord_t bubble_max_w = is_math_output
                ? static_cast<lv_coord_t>(board_.displayWidth() * 96 / 100)
                : static_cast<lv_coord_t>(board_.displayWidth() * 82 / 100);
            lv_obj_set_width(bubble, LV_SIZE_CONTENT);
            lv_obj_set_height(bubble, LV_SIZE_CONTENT);
            lv_obj_set_style_max_width(bubble, bubble_max_w, LV_PART_MAIN);
            lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(bubble, 6, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(bubble, 0, LV_PART_MAIN);
            const bool touch = board_.hasTouchInput();
            lv_obj_set_style_pad_left(bubble, touch ? 16 : 6, LV_PART_MAIN);
            lv_obj_set_style_pad_right(bubble, touch ? 16 : 6, LV_PART_MAIN);
            lv_obj_set_style_pad_top(bubble, touch ? 10 : 2, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(bubble, touch ? 10 : 2, LV_PART_MAIN);
            lv_obj_set_layout(bubble, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

            lv_color_t bubble_bg = LV_COLOR_MAKE(220, 238, 222);
            lv_color_t bubble_text = kTextColor;
            if (is_input) {
                bubble_bg = LV_COLOR_MAKE(214, 230, 250);
            } else {
                bubble_bg = LV_COLOR_MAKE(220, 238, 222);
            }
            lv_obj_set_style_bg_color(bubble, bubble_bg, LV_PART_MAIN);

            std::string shown = line;
            if (is_input && line.size() > 2) {
                shown = line.substr(2);
            }

            if (is_selected) {
                lv_obj_set_style_bg_color(bubble, kAccentColor, LV_PART_MAIN);
                bubble_bg = kAccentColor;
                bubble_text = LV_COLOR_MAKE(255, 255, 255);
            }

            bool used_formula_objects = false;
            const bool allow_formula_objects =
                is_math_output &&
                !kBoardTab5 &&
                !history_heavy_mode &&
                (i == newest_index || is_selected);
            if (allow_formula_objects) {
                std::string object_source = line;
                if (is_bracket_matrix_line) {
                    size_t pos = 0;
                    while ((pos = object_source.find("],[", pos)) != std::string::npos) {
                        object_source.replace(pos, 3, "]\n[");
                        pos += 3;
                    }

                    int depth = 0;
                    std::string wrapped;
                    wrapped.reserve(object_source.size() + 16);
                    for (char ch : object_source) {
                        if (ch == '[') {
                            ++depth;
                            wrapped.push_back(ch);
                            continue;
                        }
                        if (ch == ']') {
                            depth = std::max(0, depth - 1);
                            wrapped.push_back(ch);
                            continue;
                        }
                        if (ch == ',' && depth >= 2) {
                            wrapped.push_back(',');
                            wrapped.push_back('\n');
                            continue;
                        }
                        wrapped.push_back(ch);
                    }
                    object_source = std::move(wrapped);
                }

                const lv_coord_t bubble_inner_max_w = bubble_max_w - (touch ? 32 : 12);
                const int max_object_area = 24000;
                const size_t max_object_source_len = 256;
                const size_t max_object_commands = 768;
                if (object_source.size() > max_object_source_len) {
                    std::printf("ML_UI history_object_skip reason=source_too_long len=%u\n",
                                static_cast<unsigned>(object_source.size()));
                    std::fflush(stdout);
                } else {
                    try {
                        const xcas::mathlayout::TextBox box = xcas::mathlayout::renderText(object_source);
                        const lv_font_t *math_font = brookesia::ui_theme::textFont14();
                        xcas::mathlayout::DrawList draw_list = xcas::mathlayout::buildDrawList(box, math_font);
                        int area = draw_list.width * draw_list.height;

                        if (is_bracket_matrix_line) {
                            const bool over_limit =
                                !(draw_list.width > 0 && draw_list.width <= bubble_inner_max_w && area > 0 && area <= max_object_area);
                            if (over_limit) {
                                const std::string compact_matrix = renderNatural2D(line);
                                const xcas::mathlayout::TextBox compact_box = xcas::mathlayout::renderText(compact_matrix);
                                xcas::mathlayout::DrawList compact_draw_list = xcas::mathlayout::buildDrawList(compact_box, math_font);
                                const int compact_area = compact_draw_list.width * compact_draw_list.height;
                                const bool compact_ok =
                                    (compact_draw_list.width > 0 && compact_draw_list.width <= bubble_inner_max_w &&
                                     compact_area > 0 && compact_area <= max_object_area);
                                if (compact_ok) {
                                    draw_list = std::move(compact_draw_list);
                                    area = compact_area;
                                }
                            }
                        }
                        if (draw_list.width > 0 &&
                            draw_list.width <= bubble_inner_max_w &&
                            area > 0 &&
                            area <= max_object_area &&
                            draw_list.commands.size() <= max_object_commands) {
                            lv_obj_t *formula = lv_obj_create(bubble);
                            lv_obj_remove_style_all(formula);
                            lv_obj_clear_flag(formula, LV_OBJ_FLAG_SCROLLABLE);
                            used_formula_objects = xcas::mathlayout::renderDrawListToLvglObjects(
                                formula,
                                draw_list,
                                math_font,
                                bubble_text,
                                bubble_bg);
                        }
                    }
                    catch (const std::bad_alloc &) {
                        std::printf("ML_UI history_object_skip reason=bad_alloc len=%u\n",
                                    static_cast<unsigned>(object_source.size()));
                        std::fflush(stdout);
                    }
                }
            }

            if (!used_formula_objects) {
                lv_obj_t *label = lv_label_create(bubble);
                lv_obj_set_style_max_width(label, bubble_max_w - (touch ? 32 : 12), LV_PART_MAIN);
                lv_obj_set_width(label, LV_SIZE_CONTENT);
                lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
                brookesia::ui_theme::applyText14(label);
                lv_obj_set_style_text_color(label, bubble_text, LV_PART_MAIN);
                lv_obj_set_style_text_align(label, is_input ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
                lv_label_set_text(label, shown.c_str());
            }
        }

        if (selected_history_index_ >= static_cast<int>(history_lines_.size())) {
            selected_history_index_ = static_cast<int>(history_lines_.empty() ? -1 : (history_lines_.size() - 1));
        }

        // NOTE: scrolling is handled by the caller (appendHistory / selectHistoryIndex)
        // to allow per-entry fine-grained scroll control.
    }

    void XcasUi::clearHistorySelection()
    {
        if (selected_history_index_ < 0) {
            return;
        }

        selected_history_index_ = -1;
        refreshHistoryList();
    }

    void XcasUi::moveHistorySelection(int delta)
    {
        if (history_lines_.empty()) {
            return;
        }

        int index = selected_history_index_;
        if (index < 0) {
            index = static_cast<int>(history_lines_.size()) - 1;
        } else {
            index += delta;
        }

        if (index < 0) {
            index = 0;
        }
        if (index >= static_cast<int>(history_lines_.size())) {
            index = static_cast<int>(history_lines_.size()) - 1;
        }

        selectHistoryIndex(index, false);
    }

    void XcasUi::selectHistoryIndex(int index, bool applyToInput)
    {
        if (history_lines_.empty()) {
            selected_history_index_ = -1;
            return;
        }

        if (index < 0) index = 0;
        if (index >= static_cast<int>(history_lines_.size()))
            index = static_cast<int>(history_lines_.size()) - 1;

        selected_history_index_ = index;
        refreshHistoryList();

        // Keep keyboard navigation deterministic by always aligning to the
           // selected entry instead of pixel-stepping the whole panel.
        lv_obj_t *row = lv_obj_get_child(history_list_, index);
        if (row != nullptr) {
              lv_obj_scroll_to_view_recursive(row, LV_ANIM_ON);
        }

        if (!applyToInput || input_box_ == nullptr) return;

        const std::string &line = history_lines_[selected_history_index_];
        const char *text = line.c_str();
        if (!line.empty() && line[0] == '>' && line.size() > 2) text = line.c_str() + 2;
        lv_textarea_set_text(input_box_, text);
        lv_textarea_set_cursor_pos(input_box_, LV_TEXTAREA_CURSOR_LAST);
        refreshEditorPreview();
        setStatusText("History selected");
    }

    void XcasUi::submitInput()
    {
        if (input_box_ == nullptr)
        {
            return;
        }

        const char *expr = lv_textarea_get_text(input_box_);
        if (expr == nullptr || expr[0] == '\0')
        {
            return;
        }

        if (service_.busy()) {
            setStatusText("Evaluator busy...");
            updateBusyBinding(true);
            return;
        }

        if (!service_.submit(expr)) {
            setStatusText("Queue full or invalid input");
            updateBusyBinding(true);
            return;
        }

        appendHistory(std::string("> ") + expr);
        clearHistorySelection();
        lv_textarea_set_text(input_box_, "");
        refreshEditorPreview();
        updateHeaderText();
        setStatusText("Calculating...");
        updateBusyBinding(true);
    }

    void XcasUi::debugSubmitFormula(const std::string &formula)
    {
        initializeLvgl();
        if (input_box_ == nullptr) {
            return;
        }

        lv_textarea_set_text(input_box_, formula.c_str());
        lv_textarea_set_cursor_pos(input_box_, LV_TEXTAREA_CURSOR_LAST);
        refreshEditorPreview();
        submitInput();
    }

    void XcasUi::debugEmitFormulaImage(const std::string &formula)
    {
        initializeLvgl();

        const mathlayout::TextBox box = mathlayout::renderText(formula);
        const lv_font_t *font = ui_theme::textFont14();
        mathlayout::DrawList draw_list = mathlayout::buildDrawList(box, font);
        if (draw_list.width <= 0 || draw_list.height <= 0) {
            std::printf("SHOT_ERR empty_formula\n");
            return;
        }

        // Keep debug export bounded but allow images larger than the physical
        // viewport so complex formula captures are not clipped to the screen.
        constexpr int kMaxDebugFormulaShotW = 1600;
        constexpr int kMaxDebugFormulaShotH = 1200;
        constexpr int kDebugFormulaShotPad = 12;
        const int shot_w = std::min(draw_list.width + kDebugFormulaShotPad * 2, kMaxDebugFormulaShotW);
        const int shot_h = std::min(draw_list.height + kDebugFormulaShotPad * 2, kMaxDebugFormulaShotH);

        std::vector<uint16_t> pixels = rasterizeDrawListRgb565(
            draw_list,
            shot_w,
            shot_h,
            font,
            kDebugFormulaShotPad,
            kDebugFormulaShotPad,
            kTextColor,
            LV_COLOR_MAKE(255, 255, 255));
        if (pixels.empty()) {
            std::printf("SHOT_ERR rasterize_failed\n");
            return;
        }

        queueFormulaShot(std::move(pixels), shot_w, shot_h);
    }

    void XcasUi::queueFormulaShot(std::vector<uint16_t> &&pixels, int width, int height)
    {
        if (pixels.empty() || width <= 0 || height <= 0) {
            clearPendingFormulaShot();
            std::printf("SHOT_ERR invalid_formula_image\n");
            return;
        }

        auto job = std::make_unique<FormulaShotTaskArgs>();
        job->pixels = std::move(pixels);
        job->width = width;
        job->height = height;

        TaskHandle_t task = nullptr;
        const BaseType_t ok = xTaskCreatePinnedToCore(
            formulaShotTask,
            "formula_shot_tx",
            6144,
            job.get(),
            1,
            &task,
            0);
        if (ok != pdPASS) {
            std::printf("SHOT_ERR task_create_failed\n");
            std::fflush(stdout);
            return;
        }

        (void)task;
        job.release();
    }

    void XcasUi::clearPendingFormulaShot()
    {
        pending_formula_shot_pixels_.clear();
        pending_formula_shot_pixels_.shrink_to_fit();
        pending_formula_shot_byte_offset_ = 0;
        pending_formula_shot_width_ = 0;
        pending_formula_shot_height_ = 0;
        pending_formula_shot_started_us_ = 0;
        pending_formula_shot_last_emit_us_ = 0;
        pending_formula_shot_active_ = false;
    }

    void XcasUi::processPendingFormulaShot()
    {
        // Formula shot streaming is handled by a dedicated task.
    }

    void XcasUi::enqueueInputKey(uint32_t key)
    {
        initializeLvgl();

        // Any user interaction takes priority over debug image export.
        if (pending_formula_shot_active_) {
            clearPendingFormulaShot();
            std::printf("SHOT_ERR canceled_by_input\n");
        }

        if (editor_fullscreen_) {
            constexpr int kPanStep = 16;
            if (key == ' ' || key == LV_KEY_ESC || key == LV_KEY_ENTER) {
                closeHistoryFullscreenPreview();
                setStatusText("Preview closed");
                return;
            }
            if (key == LV_KEY_LEFT) {
                panFullscreenPreview(kPanStep, 0);
                return;
            }
            if (key == LV_KEY_RIGHT) {
                panFullscreenPreview(-kPanStep, 0);
                return;
            }
            if (key == LV_KEY_UP || key == LV_KEY_PREV) {
                panFullscreenPreview(0, kPanStep);
                return;
            }
            if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
                panFullscreenPreview(0, -kPanStep);
                return;
            }
            return;
        }

        if (key == ' ' && selected_history_index_ >= 0) {
            openHistoryFullscreenPreview();
            return;
        }

        if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            moveHistorySelection(-1);
            setStatusText("History up");
            return;
        }
        if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            moveHistorySelection(1);
            setStatusText("History down");
            return;
        }
        if (key == LV_KEY_ENTER && selected_history_index_ >= 0) {
            selectHistoryIndex(selected_history_index_, true);
            clearHistorySelection();
            setStatusText("Loaded to input");
            return;
        }

        const uint8_t next_tail = static_cast<uint8_t>((key_queue_tail_ + 1U) % key_queue_.size());
        if (next_tail == key_queue_head_) {
            key_queue_head_ = static_cast<uint8_t>((key_queue_head_ + 1U) % key_queue_.size());
        }

        key_queue_[key_queue_tail_] = key;
        key_queue_tail_ = next_tail;
    }

    void XcasUi::render()
    {
        initializeLvgl();

        if (editor_fullscreen_ && preview_use_objects_ && preview_paint_pending_) {
            stepFullscreenPreviewPaint(kBoardTab5 ? 32 : 96, kBoardTab5 ? 8 : 48);
        }

        std::string result;
        if (service_.pollResult(result))
        {
            appendHistory(result);
            setStatusText("Done");
            updateBusyBinding(false);
        }
        else if (service_.busy())
        {
            if (std::strcmp(status_text_buf_.data(), "Calculating...") != 0) {
                setStatusText("Calculating...");
            }
            updateBusyBinding(true);
        }
        else {
            updateBusyBinding(false);
        }

        if (redraw_recovery_pending_)
        {
            lv_obj_invalidate(lv_scr_act());
            redraw_recovery_pending_ = false;
        }

        // NOTE: lv_tick_inc()/lv_timer_handler() are pumped centrally by
        // Kernel::pumpLvgl() every frame so that the display keeps flushing
        // regardless of which app is currently focused.
    }

    void XcasUi::show()
    {
        initializeLvgl();
        if (!session_loaded_) {
            loadSession();
            session_loaded_ = true;
        }
        if (root_ != nullptr) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
        }
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        setScreenKeyboardVisible(screen_keyboard_visible_);
#endif
        if (input_group_ != nullptr && input_box_ != nullptr) {
            lv_group_focus_obj(input_box_);
        }
        redraw_recovery_pending_ = true;
    }

    void XcasUi::hide()
    {
        saveSession();
        if (root_ != nullptr) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        if (screen_keyboard_ != nullptr) {
            lv_obj_add_flag(screen_keyboard_, LV_OBJ_FLAG_HIDDEN);
        }
        if (keyboard_mode_bar_ != nullptr) {
            lv_obj_add_flag(keyboard_mode_bar_, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    }

    void XcasUi::releaseUi()
    {
        closeHistoryFullscreenPreview();
        if (input_group_ != nullptr) {
            lv_group_remove_all_objs(input_group_);
        }
        if (editor_preview_host_ != nullptr) {
            lv_obj_delete(editor_preview_host_);
        }
        if (editor_preview_back_btn_ != nullptr) {
            lv_obj_delete(editor_preview_back_btn_);
        }
#if CONFIG_XCAS_USE_SCREEN_KEYBOARD
        if (screen_keyboard_ != nullptr) {
            lv_obj_delete(screen_keyboard_);
        }
        if (keyboard_mode_bar_ != nullptr) {
            lv_obj_delete(keyboard_mode_bar_);
        }
#endif
        if (root_ != nullptr) {
            lv_obj_delete(root_);
        }
        header_label_ = nullptr;
        status_label_ = nullptr;
        info_label_ = nullptr;
        history_panel_ = nullptr;
        history_list_ = nullptr;
        editor_panel_ = nullptr;
        editor_preview_host_ = nullptr;
        editor_preview_formula_ = nullptr;
        editor_preview_label_ = nullptr;
        editor_preview_back_btn_ = nullptr;
        editor_hint_label_ = nullptr;
        input_box_ = nullptr;
        screen_keyboard_ = nullptr;
        keyboard_mode_bar_ = nullptr;
        root_ = nullptr;
        lv_style_reset(&busy_style_);
        lvgl_initialized_ = false;
        editor_fullscreen_ = false;
        selected_history_index_ = -1;
    }

    void XcasUi::loadSession()
    {
        if (!brookesia::ensureStorageMounted()) {
            return;
        }

        FILE *f = std::fopen("/data/calc_session.txt", "r");
        if (f == nullptr) {
            return;
        }

        history_lines_.clear();

        char line[256];
        while (std::fgets(line, sizeof(line), f) != nullptr) {
            size_t n = std::strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[n - 1] = '\0';
                --n;
            }

            if (std::strncmp(line, "INPUT=", 6) == 0) {
                if (input_box_ != nullptr) {
                    lv_textarea_set_text(input_box_, line + 6);
                }
                continue;
            }

            if (std::strncmp(line, "H=", 2) == 0) {
                history_lines_.emplace_back(line + 2);
            }
        }
        std::fclose(f);

        if (history_lines_.empty()) {
            // history_lines_.push_back("CAS ready. Enter to eval.");
        }

        selected_history_index_ = -1;
        refreshHistoryList();
    }

    void XcasUi::saveSession()
    {
        if (!brookesia::ensureStorageMounted()) {
            return;
        }

        FILE *f = std::fopen("/data/calc_session.txt", "w");
        if (f == nullptr) {
            return;
        }

        if (input_box_ != nullptr) {
            const char *input = lv_textarea_get_text(input_box_);
            std::fprintf(f, "INPUT=%s\n", input == nullptr ? "" : input);
        } else {
            std::fputs("INPUT=\n", f);
        }

        for (const std::string &line : history_lines_) {
            std::fprintf(f, "H=%s\n", line.c_str());
        }

        std::fclose(f);
    }

} // namespace xcas
