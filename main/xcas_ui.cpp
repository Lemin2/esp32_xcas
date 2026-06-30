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
          header_label_(nullptr),
          status_label_(nullptr),
          info_label_(nullptr),
          history_panel_(nullptr),
          history_list_(nullptr),
          input_box_(nullptr),
          root_(nullptr),
          selected_history_index_(-1),
          history_cursor_(-1),
          previous_key_mask_(0),
          last_render_us_(0),
          lvgl_initialized_(false),
          redraw_recovery_pending_(false),
          fn_toggled_(false),
          caps_toggled_(false),
          subjects_initialized_(false)
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
        const lv_coord_t screen_w = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayWidth);
        const lv_coord_t screen_h = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayHeight);
        const lv_coord_t top_reserved = 16; // global status bar space managed by kernel

        lv_obj_t *screen = lv_scr_act();
        lv_obj_set_style_bg_color(screen, kBgColor, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

        lv_obj_t *root = lv_obj_create(screen);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, screen_w, screen_h - top_reserved);
        lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, top_reserved);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_left(root, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_right(root, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_top(root, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(root, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_row(root, 3, LV_PART_MAIN);

        header_label_ = lv_label_create(root);
        lv_obj_set_style_text_color(header_label_, kAccentColor, LV_PART_MAIN);
        lv_obj_set_style_text_font(header_label_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_width(header_label_, lv_pct(100));
        lv_label_set_long_mode(header_label_, LV_LABEL_LONG_DOT);
        if (!subjects_initialized_) {
            lv_subject_init_string(&header_subject_, header_text_buf_.data(), header_text_prev_buf_.data(), header_text_buf_.size(), "");
            lv_subject_init_int(&busy_subject_, 0);
            subjects_initialized_ = true;
        }
        lv_label_bind_text(header_label_, &header_subject_, "%s");
        updateHeaderText();

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
        lv_obj_set_style_bg_color(history_panel_, kPanelColor, LV_PART_MAIN);
        lv_obj_set_style_border_color(history_panel_, LV_COLOR_MAKE(208, 214, 224), LV_PART_MAIN);
        lv_obj_set_style_border_width(history_panel_, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(history_panel_, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_row(history_panel_, 2, LV_PART_MAIN);
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

        info_label_ = nullptr;

        input_box_ = lv_textarea_create(root);
    #if LV_USE_OBJ_PROPERTY
        const lv_property_t input_props[] = {
            { .id = LV_PROPERTY_OBJ_W, .num = screen_w - 8 },
            { .id = LV_PROPERTY_OBJ_H, .num = 30 },
        };
        lv_obj_set_properties(input_box_, input_props, sizeof(input_props) / sizeof(input_props[0]));
    #else
        lv_obj_set_size(input_box_, screen_w - 8, 30);
    #endif
        lv_textarea_set_one_line(input_box_, true);
        lv_textarea_set_placeholder_text(input_box_, "1+1");
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
        lv_obj_set_style_text_font(input_box_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_add_state(input_box_, LV_STATE_FOCUSED);

        status_label_ = nullptr;

        lv_style_init(&busy_style_);
        updateBusyBinding(false);
        root_ = root;
        history_lines_.clear();
        history_lines_.push_back("CAS ready. Enter to eval.");
        refreshHistoryList();
    }

    void XcasUi::updateHeaderText()
    {
        if (subjects_initialized_) {
            lv_subject_copy_string(&header_subject_, "CAS / Expression Mode");
        }
    }

    void XcasUi::recallHistory(int delta)
    {
        if (input_box_ == nullptr || command_history_.empty()) {
            return;
        }

        if (history_cursor_ < 0 && delta < 0) {
            const char *current = lv_textarea_get_text(input_box_);
            history_draft_input_ = current ? current : "";
            history_cursor_ = static_cast<int>(command_history_.size()) - 1;
        } else {
            history_cursor_ += delta;
        }

        if (history_cursor_ < -1) {
            history_cursor_ = -1;
        }

        if (history_cursor_ >= static_cast<int>(command_history_.size())) {
            history_cursor_ = static_cast<int>(command_history_.size()) - 1;
        }

        if (history_cursor_ == -1) {
            lv_textarea_set_text(input_box_, history_draft_input_.c_str());
            setStatusText("Draft restored");
            return;
        }

        lv_textarea_set_text(input_box_, command_history_[history_cursor_].c_str());
        lv_textarea_set_cursor_pos(input_box_, LV_TEXTAREA_CURSOR_LAST);
        setStatusText("History recall");
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
        if (history_list_ == nullptr || history_panel_ == nullptr)
        {
            return;
        }

        history_lines_.push_back(line);
        const std::size_t kMaxHistoryLines = 40;
        if (history_lines_.size() > kMaxHistoryLines) {
            history_lines_.erase(history_lines_.begin(), history_lines_.begin() + static_cast<long>(history_lines_.size() - kMaxHistoryLines));
        }
        refreshHistoryList();
    }

    void XcasUi::refreshHistoryList()
    {
        if (history_list_ == nullptr || history_panel_ == nullptr) {
            return;
        }

        lv_obj_t *selected_row = nullptr;
        lv_obj_clean(history_list_);
        for (size_t i = 0; i < history_lines_.size(); ++i) {
            const std::string &line = history_lines_[i];
            const bool is_input = (line.size() >= 2 && line[0] == '>' && line[1] == ' ');

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

            lv_obj_t *bubble = lv_label_create(row);
            const lv_coord_t bubble_max_w =
                static_cast<lv_coord_t>(board::CardputerBsp::kDisplayWidth * 82 / 100);
            lv_obj_set_style_max_width(bubble, bubble_max_w, LV_PART_MAIN);
            lv_obj_set_width(bubble, LV_SIZE_CONTENT);
            lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_radius(bubble, 6, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_pad_left(bubble, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_right(bubble, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_top(bubble, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(bubble, 2, LV_PART_MAIN);
            lv_obj_set_style_text_font(bubble, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(bubble, kTextColor, LV_PART_MAIN);
            if (is_input) {
                lv_obj_set_style_bg_color(bubble, LV_COLOR_MAKE(214, 230, 250), LV_PART_MAIN);
                lv_obj_set_style_text_align(bubble, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_color(bubble, LV_COLOR_MAKE(220, 238, 222), LV_PART_MAIN);
                lv_obj_set_style_text_align(bubble, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            }
            lv_label_set_text(bubble, line.c_str());

            if (selected_history_index_ == static_cast<int>(i)) {
                lv_obj_set_style_bg_color(bubble, kAccentColor, LV_PART_MAIN);
                lv_obj_set_style_text_color(bubble, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
                selected_row = row;
            }
        }

        if (selected_history_index_ >= static_cast<int>(history_lines_.size())) {
            selected_history_index_ = static_cast<int>(history_lines_.empty() ? -1 : (history_lines_.size() - 1));
        }

        if (selected_row != nullptr) {
            lv_obj_scroll_to_view_recursive(selected_row, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(history_panel_, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }

    void XcasUi::selectHistoryIndex(int index, bool applyToInput)
    {
        if (history_lines_.empty()) {
            selected_history_index_ = -1;
            return;
        }

        if (index < 0) {
            index = 0;
        }
        if (index >= static_cast<int>(history_lines_.size())) {
            index = static_cast<int>(history_lines_.size()) - 1;
        }

        selected_history_index_ = index;
        refreshHistoryList();

        if (!applyToInput || input_box_ == nullptr) {
            return;
        }

        const std::string &line = history_lines_[selected_history_index_];
        const char *text = line.c_str();
        if (!line.empty() && line[0] == '>' && line.size() > 2) {
            text = line.c_str() + 2;
        }

        lv_textarea_set_text(input_box_, text);
        lv_textarea_set_cursor_pos(input_box_, LV_TEXTAREA_CURSOR_LAST);
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
        command_history_.emplace_back(expr);
        history_cursor_ = -1;
        history_draft_input_.clear();
        lv_textarea_set_text(input_box_, "");
        setStatusText("Calculating...");
        updateBusyBinding(true);
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
            setStatusText(fn_toggled_ ? "FN locked" : "FN unlocked");
        }
        if ((newly_pressed & shift_bit) != 0U)
        {
            caps_toggled_ = !caps_toggled_;
            setStatusText(caps_toggled_ ? "CAPS ON" : "caps off");
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

                // Enter loads the highlighted history entry into the input box
                // while browsing; otherwise it evaluates the current expression.
                if (keyIs(key, "Enter"))
                {
                    if (selected_history_index_ >= 0)
                    {
                        selectHistoryIndex(selected_history_index_, true);
                        selected_history_index_ = -1;
                        refreshHistoryList();
                        setStatusText("Loaded to input");
                    }
                    else
                    {
                        submitInput();
                    }
                    continue;
                }

                if (fn_active)
                {
                    bool fn_handled = true;
                    if (keyIs(key, "`"))
                    {
                        lv_textarea_set_text(input_box_, "");
                        setStatusText("Esc");
                    }
                    else if (keyIs(key, "Backspace"))
                    {
                        lv_textarea_delete_char_forward(input_box_);
                        setStatusText("Del");
                    }
                    else if (keyIs(key, ";"))
                    {
                        if (selected_history_index_ < 0) {
                            selectHistoryIndex(static_cast<int>(history_lines_.size()) - 1, false);
                        } else {
                            selectHistoryIndex(selected_history_index_ - 1, false);
                        }
                        setStatusText("History up");
                    }
                    else if (keyIs(key, "."))
                    {
                        if (selected_history_index_ < 0) {
                            selectHistoryIndex(static_cast<int>(history_lines_.size()) - 1, false);
                        } else {
                            selectHistoryIndex(selected_history_index_ + 1, false);
                        }
                        setStatusText("History down");
                    }
                    else if (keyIs(key, ","))
                    {
                        moveCursor(-1);
                        setStatusText("Left");
                    }
                    else if (keyIs(key, "/"))
                    {
                        moveCursor(1);
                        setStatusText("Right");
                    }
                    else
                    {
                        fn_handled = false;
                    }

                    if (fn_handled)
                    {
                        continue;
                    }
                    // No Fn-specific action for this key: fall through so the key
                    // still produces its normal character (letters, digits, ...).
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
                    appendInput("^");
                    continue;
                }

                if (keyIs(key, "`"))
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
            setStatusText("Done");
            updateBusyBinding(false);
        }
        else if (service_.busy())
        {
            setStatusText("Calculating...");
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
        if (root_ != nullptr) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
        }
        redraw_recovery_pending_ = true;
    }

    void XcasUi::hide()
    {
        if (root_ != nullptr) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }

} // namespace xcas
