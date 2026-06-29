#include "xcas_ui.hpp"

#include <cstdio>
#include <cstring>

#include "esp_timer.h"

namespace xcas
{
    namespace
    {

        constexpr xcas::XcasUi::KeyLabel kKeyMap[xcas::XcasUi::kKeyRowCount][xcas::XcasUi::kKeyColCount] = {
            {
                {"`", "~"},
                {"1", "!"},
                {"2", "@"},
                {"3", "#"},
                {"4", "$"},
                {"5", "%"},
                {"6", "^"},
                {"7", "&"},
                {"8", "*"},
                {"9", "("},
                {"0", ")"},
                {"-", "_"},
                {"=", "+"},
                {"Backspace", "Backspace"},
            },
            {
                {"Tab", "Tab"},
                {"q", "Q"},
                {"w", "W"},
                {"e", "E"},
                {"r", "R"},
                {"t", "T"},
                {"y", "Y"},
                {"u", "U"},
                {"i", "I"},
                {"o", "O"},
                {"p", "P"},
                {"[", "{"},
                {"]", "}"},
                {"\\", "|"},
            },
            {
                {"Fn", "Fn"},
                {"Shift", "Shift"},
                {"a", "A"},
                {"s", "S"},
                {"d", "D"},
                {"f", "F"},
                {"g", "G"},
                {"h", "H"},
                {"j", "J"},
                {"k", "K"},
                {"l", "L"},
                {";", ":"},
                {"'", "\""},
                {"Enter", "Enter"},
            },
            {
                {"Ctrl", "Ctrl"},
                {"Opt", "Opt"},
                {"Alt", "Alt"},
                {"z", "Z"},
                {"x", "X"},
                {"c", "C"},
                {"v", "V"},
                {"b", "B"},
                {"n", "N"},
                {"m", "M"},
                {",", "<"},
                {".", ">"},
                {"/", "?"},
                {"Space", "Space"},
            },
        };

        constexpr const char *kMenus[] = {
            "CALC",
            "GRAPH",
            "SOLVER",
            "SYSTEM",
        };

        constexpr lv_color_t kBgColor = LV_COLOR_MAKE(245, 245, 238);
        constexpr lv_color_t kPanelColor = LV_COLOR_MAKE(255, 255, 255);
        constexpr lv_color_t kTextColor = LV_COLOR_MAKE(16, 24, 36);
        constexpr lv_color_t kStatusColor = LV_COLOR_MAKE(88, 104, 122);
        constexpr lv_color_t kAccentColor = LV_COLOR_MAKE(24, 84, 192);

    } // namespace

    XcasUi::XcasUi(board::CardputerBsp &board, XcasService &service)
        : board_(board),
          service_(service),
          lv_display_(nullptr),
          menu_label_(nullptr),
          status_label_(nullptr),
          history_panel_(nullptr),
          history_label_(nullptr),
          input_box_(nullptr),
          previous_key_mask_(0),
          last_render_us_(0),
          menu_index_(0),
          lvgl_initialized_(false),
          redraw_recovery_pending_(false),
          fn_toggled_(false),
          caps_toggled_(false)
    {
    }

    int XcasUi::keyIndex(int row, int col)
    {
        return row * kKeyColCount + col;
    }

    const XcasUi::KeyLabel &XcasUi::keyAt(int row, int col)
    {
        return kKeyMap[row][col];
    }

    bool XcasUi::keyIsDown(uint64_t mask, int row, int col)
    {
        return (mask & (1ULL << keyIndex(row, col))) != 0U;
    }

    bool XcasUi::keyIs(const KeyLabel &key, const char *name)
    {
        return std::strcmp(key.base, name) == 0;
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

    void XcasUi::initializeLvgl()
    {
        if (lvgl_initialized_)
        {
            return;
        }

        lv_init();

        lv_display_ = lv_display_create(board::CardputerBsp::kDisplayWidth, board::CardputerBsp::kDisplayHeight);
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

        buildLayout();
        lvgl_initialized_ = true;
    }

    void XcasUi::buildLayout()
    {
        lv_obj_t *screen = lv_scr_act();
        lv_obj_set_style_bg_color(screen, kBgColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, "XCAS");
        lv_obj_set_style_text_color(title, kTextColor, LV_PART_MAIN);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 4);

        menu_label_ = lv_label_create(screen);
        lv_obj_set_style_text_color(menu_label_, kAccentColor, LV_PART_MAIN);
        lv_obj_set_style_text_font(menu_label_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(menu_label_, LV_ALIGN_TOP_RIGHT, -6, 6);
        switchMenu(0);

        history_panel_ = lv_obj_create(screen);
        lv_obj_set_size(history_panel_, board::CardputerBsp::kDisplayWidth - 10, 82);
        lv_obj_align(history_panel_, LV_ALIGN_TOP_MID, 0, 24);
        lv_obj_set_style_radius(history_panel_, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(history_panel_, kPanelColor, LV_PART_MAIN);
        lv_obj_set_style_border_color(history_panel_, LV_COLOR_MAKE(208, 214, 224), LV_PART_MAIN);
        lv_obj_set_style_border_width(history_panel_, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(history_panel_, 4, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(history_panel_, LV_SCROLLBAR_MODE_AUTO);

        history_label_ = lv_label_create(history_panel_);
        lv_obj_set_width(history_label_, lv_pct(100));
        lv_label_set_long_mode(history_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(history_label_, kTextColor, LV_PART_MAIN);
        lv_obj_set_style_text_font(history_label_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(history_label_, "NumWorks-style UI ready.\nType expression then press Enter.");

        input_box_ = lv_textarea_create(screen);
        lv_obj_set_size(input_box_, board::CardputerBsp::kDisplayWidth - 10, 22);
        lv_obj_align(input_box_, LV_ALIGN_TOP_MID, 0, 110);
        lv_textarea_set_one_line(input_box_, true);
        lv_textarea_set_placeholder_text(input_box_, "1+1");
        lv_obj_set_style_bg_color(input_box_, kPanelColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input_box_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(input_box_, kTextColor, LV_PART_MAIN);
        lv_obj_set_style_border_color(input_box_, LV_COLOR_MAKE(208, 214, 224), LV_PART_MAIN);
        lv_obj_set_style_border_width(input_box_, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_left(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_right(input_box_, 4, LV_PART_MAIN);
        lv_obj_set_style_text_color(input_box_, kStatusColor, LV_PART_CURSOR | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(input_box_, &lv_font_montserrat_14, LV_PART_MAIN);

        status_label_ = lv_label_create(screen);
        lv_obj_set_style_text_color(status_label_, kStatusColor, LV_PART_MAIN);
        lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(status_label_, LV_ALIGN_BOTTOM_LEFT, 6, -2);
        lv_label_set_text(status_label_, "FN+Q/W menu  FN+Backspace clear");
    }

    void XcasUi::switchMenu(int delta)
    {
        const int menu_count = static_cast<int>(sizeof(kMenus) / sizeof(kMenus[0]));
        menu_index_ = (menu_index_ + delta + menu_count) % menu_count;

        char text[64];
        std::snprintf(text, sizeof(text), "[%s]  %s  %s  %s", kMenus[0], kMenus[1], kMenus[2], kMenus[3]);
        // highlight selected by replacing [] around target menu only
        std::snprintf(text, sizeof(text), "%c%s%c  %c%s%c  %c%s%c  %c%s%c",
                      menu_index_ == 0 ? '[' : ' ', kMenus[0], menu_index_ == 0 ? ']' : ' ',
                      menu_index_ == 1 ? '[' : ' ', kMenus[1], menu_index_ == 1 ? ']' : ' ',
                      menu_index_ == 2 ? '[' : ' ', kMenus[2], menu_index_ == 2 ? ']' : ' ',
                      menu_index_ == 3 ? '[' : ' ', kMenus[3], menu_index_ == 3 ? ']' : ' ');
        if (menu_label_ != nullptr)
        {
            lv_label_set_text(menu_label_, text);
        }
    }

    void XcasUi::appendInput(const char *text)
    {
        if (input_box_ == nullptr || text == nullptr)
        {
            return;
        }
        lv_textarea_add_text(input_box_, text);
    }

    void XcasUi::appendHistory(const std::string &line)
    {
        if (history_label_ == nullptr || history_panel_ == nullptr)
        {
            return;
        }

        if (!history_text_.empty())
        {
            history_text_ += "\n";
        }
        history_text_ += line;

        const std::size_t kMaxHistoryChars = 2400;
        if (history_text_.size() > kMaxHistoryChars)
        {
            history_text_.erase(0, history_text_.size() - kMaxHistoryChars);
        }

        lv_label_set_text(history_label_, history_text_.c_str());
        lv_obj_scroll_to_view_recursive(history_label_, LV_ANIM_OFF);
    }

    void XcasUi::submitInput()
    {
        if (input_box_ == nullptr || status_label_ == nullptr)
        {
            return;
        }

        const char *expr = lv_textarea_get_text(input_box_);
        if (expr == nullptr || expr[0] == '\0')
        {
            return;
        }

        if (service_.busy())
        {
            lv_label_set_text(status_label_, "Evaluator busy...");
            return;
        }

        if (!service_.submit(expr))
        {
            lv_label_set_text(status_label_, "Queue full or invalid input");
            return;
        }

        appendHistory(std::string("> ") + expr);
        lv_textarea_set_text(input_box_, "");
        lv_label_set_text(status_label_, "Calculating...");
    }

    void XcasUi::handleKeyboardState(uint64_t pressed_mask)
    {
        initializeLvgl();

        const uint64_t newly_pressed = pressed_mask & ~previous_key_mask_;

        const uint64_t fn_bit = (1ULL << keyIndex(kFnRow, kFnCol));
        const uint64_t shift_bit = (1ULL << keyIndex(kShiftRow, kShiftCol));

        if ((newly_pressed & fn_bit) != 0U)
        {
            fn_toggled_ = !fn_toggled_;
            if (status_label_ != nullptr)
            {
                lv_label_set_text(status_label_, fn_toggled_ ? "FN locked" : "FN unlocked");
            }
        }
        if ((newly_pressed & shift_bit) != 0U)
        {
            caps_toggled_ = !caps_toggled_;
            if (status_label_ != nullptr)
            {
                lv_label_set_text(status_label_, caps_toggled_ ? "CAPS ON" : "caps off");
            }
        }

        const bool fn_active = fn_toggled_;

        auto moveCursor = [this](int delta)
        {
            if (input_box_ == nullptr)
            {
                return;
            }
            const char *txt = lv_textarea_get_text(input_box_);
            int32_t len = 0;
            if (txt != nullptr)
            {
                len = static_cast<int32_t>(std::strlen(txt));
            }
            int32_t pos = lv_textarea_get_cursor_pos(input_box_);
            pos += delta;
            if (pos < 0)
            {
                pos = 0;
            }
            if (pos > len)
            {
                pos = len;
            }
            lv_textarea_set_cursor_pos(input_box_, pos);
        };

        for (int row = 0; row < kKeyRowCount; ++row)
        {
            for (int col = 0; col < kKeyColCount; ++col)
            {
                const uint64_t bit = (1ULL << keyIndex(row, col));
                if ((newly_pressed & bit) == 0U)
                {
                    continue;
                }

                const KeyLabel &key = keyAt(row, col);
                if (keyIs(key, "Fn") || keyIs(key, "Shift") || keyIs(key, "Ctrl") || keyIs(key, "Opt") || keyIs(key, "Alt"))
                {
                    continue;
                }

                if (fn_active)
                {
                    if (keyIs(key, "`"))
                    {
                        lv_textarea_set_text(input_box_, "");
                        lv_label_set_text(status_label_, "Esc");
                    }
                    else if (keyIs(key, "Backspace"))
                    {
                        lv_textarea_delete_char_forward(input_box_);
                        lv_label_set_text(status_label_, "Del");
                    }
                    else if (keyIs(key, ";"))
                    {
                        switchMenu(-1);
                        lv_label_set_text(status_label_, "Up");
                    }
                    else if (keyIs(key, ","))
                    {
                        moveCursor(-1);
                        lv_label_set_text(status_label_, "Left");
                    }
                    else if (keyIs(key, "."))
                    {
                        switchMenu(1);
                        lv_label_set_text(status_label_, "Down");
                    }
                    else if (keyIs(key, "/"))
                    {
                        moveCursor(1);
                        lv_label_set_text(status_label_, "Right");
                    }
                    else if (keyIs(key, "q"))
                    {
                        switchMenu(-1);
                    }
                    else if (keyIs(key, "w"))
                    {
                        switchMenu(1);
                    }
                    else if (keyIs(key, "Enter"))
                    {
                        lv_textarea_set_text(input_box_, "");
                        lv_label_set_text(status_label_, "Input cleared");
                    }
                    continue;
                }

                if (keyIs(key, "Enter"))
                {
                    submitInput();
                    continue;
                }
                if (keyIs(key, "Backspace"))
                {
                    lv_textarea_delete_char(input_box_);
                    continue;
                }
                if (keyIs(key, "Space"))
                {
                    appendInput(" ");
                    continue;
                }
                if (keyIs(key, "Tab"))
                {
                    appendInput("x");
                    continue;
                }

                if (keyIs(key, "`") && !fn_active)
                {
                    appendInput("`");
                    continue;
                }

                const char *label = caps_toggled_ ? key.shifted : key.base;
                if (label != nullptr && label[0] != '\0' && label[1] == '\0')
                {
                    appendInput(label);
                }
            }
        }

        previous_key_mask_ = pressed_mask;
    }

    void XcasUi::render()
    {
        initializeLvgl();

        std::string result;
        if (service_.pollResult(result))
        {
            appendHistory(result);
            lv_label_set_text(status_label_, "Done");
        }
        else if (service_.busy())
        {
            lv_label_set_text(status_label_, "Calculating...");
        }

        if (redraw_recovery_pending_)
        {
            lv_obj_invalidate(lv_scr_act());
            redraw_recovery_pending_ = false;
        }

        const uint64_t now_us = esp_timer_get_time();
        uint32_t elapsed_ms = 10;
        if (last_render_us_ != 0 && now_us > last_render_us_)
        {
            elapsed_ms = static_cast<uint32_t>((now_us - last_render_us_) / 1000ULL);
            if (elapsed_ms == 0)
            {
                elapsed_ms = 1;
            }
        }
        last_render_us_ = now_us;

        lv_tick_inc(elapsed_ms);
        lv_timer_handler();
    }

} // namespace xcas
