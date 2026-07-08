#include "xcas_ui.hpp"

#include <cstdio>
#include <cstring>
#include <cctype>

#include "esp_timer.h"

namespace xcas
{
    namespace
    {
        // ── Autocomplete word list ─────────────────────────────────────────────
        static const char *const kAcWords[] = {
            "abs","acos","acosh","asin","asinh","atan","atanh",
            "ceil","conj","cos","cosh","cross",
            "denom","det","diff","evalf","exp","factor","floor",
            "gcd","imag","int","irem","lcm","ln","log","max","min",
            "mod","normal","numer","pi","product",
            "re","round","seq","sign","simplify",
            "sin","sinh","solve","sqrt","subst","sum",
            "tan","tanh","zeros",
            nullptr
        };

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
                    keyboard_indev_(nullptr),
                    input_group_(nullptr),
          header_label_(nullptr),
          status_label_(nullptr),
          info_label_(nullptr),
          history_panel_(nullptr),
          history_list_(nullptr),
          input_box_(nullptr),
          ac_hint_label_(nullptr),
          root_(nullptr),
          selected_history_index_(-1),
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
        self->setStatusText("History browse");
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
            self->updateAutocomplete();
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
            if (self->ac_candidates_.empty()) {
                self->updateAutocomplete();
            } else {
                self->applyAutocomplete();
            }
            lv_event_stop_processing(e);
            return;
        }
        if (key == LV_KEY_ESC) {
            self->clearHistorySelection();
            if (self->input_box_ != nullptr) {
                lv_textarea_set_text(self->input_box_, "");
            }
            self->hideAutocomplete();
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
            self->hideAutocomplete();
            self->updateHeaderText();
            self->setStatusText("Del");
            lv_event_stop_processing(e);
            return;
        }

        self->clearHistorySelection();
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

        keyboard_indev_ = lv_indev_create();
        lv_indev_set_type(keyboard_indev_, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(keyboard_indev_, &XcasUi::lvglKeyboardRead);
        lv_indev_set_user_data(keyboard_indev_, this);

        input_group_ = lv_group_create();
            lv_group_set_default(input_group_);
        lv_indev_set_group(keyboard_indev_, input_group_);

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
            lv_obj_add_flag(input_box_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_state(input_box_, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_READY, this);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_KEY, this);
        lv_obj_add_event_cb(input_box_, &XcasUi::onInputBoxEvent, LV_EVENT_VALUE_CHANGED, this);

        if (input_group_ != nullptr) {
            lv_group_add_obj(input_group_, input_box_);
            lv_group_focus_obj(input_box_);
        }

        ac_hint_label_ = lv_label_create(input_box_);
        lv_obj_set_style_text_font(ac_hint_label_, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(ac_hint_label_, LV_COLOR_MAKE(165, 174, 188), LV_PART_MAIN);
        lv_obj_set_pos(ac_hint_label_, 6, 5);
        lv_label_set_text(ac_hint_label_, "");
        lv_obj_add_flag(ac_hint_label_, LV_OBJ_FLAG_HIDDEN);

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
        // Auto-scroll to bottom for new results (only if no manual selection active)
        if (selected_history_index_ < 0) {
            lv_obj_scroll_to_y(history_panel_, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }

    void XcasUi::refreshHistoryList()
    {
        if (history_list_ == nullptr || history_panel_ == nullptr) {
            return;
        }

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
              lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
        }

        if (!applyToInput || input_box_ == nullptr) return;

        const std::string &line = history_lines_[selected_history_index_];
        const char *text = line.c_str();
        if (!line.empty() && line[0] == '>' && line.size() > 2) text = line.c_str() + 2;
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
        clearHistorySelection();
        lv_textarea_set_text(input_box_, "");
        hideAutocomplete();
        updateHeaderText();
        setStatusText("Calculating...");
        updateBusyBinding(true);
    }

    void XcasUi::handleKeyboardState(uint64_t pressed_mask)
    {
        initializeLvgl();
        previous_key_mask_ = pressed_mask;
    }

    void XcasUi::enqueueInputKey(uint32_t key)
    {
        initializeLvgl();

        // History browsing should not depend on textarea internals.
        // Consume navigation keys here so Fn+Up/Down is deterministic.
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
        if (input_group_ != nullptr && input_box_ != nullptr) {
            lv_group_focus_obj(input_box_);
        }
        redraw_recovery_pending_ = true;
    }

    void XcasUi::hide()
    {
        if (root_ != nullptr) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ── Autocomplete ────────────────────────────────────────────────────────

    void XcasUi::updateAutocomplete()
    {
        if (input_box_ == nullptr) return;

        const char *txt = lv_textarea_get_text(input_box_);
        if (txt == nullptr) { hideAutocomplete(); return; }

        int pos = static_cast<int>(lv_textarea_get_cursor_pos(input_box_));
        if (pos <= 0) { hideAutocomplete(); return; }

        // Find start of the current word (letters/digits)
        int ws = pos - 1;
        while (ws > 0 && (std::isalpha(static_cast<unsigned char>(txt[ws - 1])) ||
                          std::isdigit(static_cast<unsigned char>(txt[ws - 1])) ||
                          txt[ws - 1] == '_')) {
            --ws;
        }
        int word_len = pos - ws;
        if (word_len < 2) { hideAutocomplete(); return; }

        std::string prefix(txt + ws, static_cast<size_t>(word_len));

        ac_candidates_.clear();
        for (int i = 0; kAcWords[i] != nullptr; ++i) {
            if (std::strncmp(kAcWords[i], prefix.c_str(), static_cast<size_t>(word_len)) == 0 &&
                std::strcmp(kAcWords[i], prefix.c_str()) != 0) {
                ac_candidates_.push_back(kAcWords[i]);
                if (ac_candidates_.size() >= 4) break;
            }
        }

        if (ac_candidates_.empty()) {
            hideAutocomplete();
            return;
        }

        ac_prefix_ = prefix;
        ac_index_ = 0;

        if (ac_hint_label_ != nullptr) {
            const char *best = ac_candidates_[0];
            const char *suffix = best + ac_prefix_.size();
            if (suffix[0] == '\0') {
                hideAutocomplete();
                return;
            }

            std::string left(txt, static_cast<size_t>(pos));
            lv_point_t sz;
            lv_text_get_size(&sz, left.c_str(), &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

            lv_label_set_text(ac_hint_label_, suffix);
            lv_obj_set_pos(ac_hint_label_, static_cast<lv_coord_t>(6 + sz.x), 5);
            lv_obj_clear_flag(ac_hint_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void XcasUi::applyAutocomplete()
    {
        if (ac_candidates_.empty() || ac_index_ >= static_cast<int>(ac_candidates_.size())) {
            hideAutocomplete();
            return;
        }

        const char *word   = ac_candidates_[static_cast<size_t>(ac_index_)];
        const char *suffix = word + ac_prefix_.size(); // chars to append

        appendInput(suffix);
        appendInput("("); // open paren for function call
        hideAutocomplete();
    }

    void XcasUi::hideAutocomplete()
    {
        ac_candidates_.clear();
        if (ac_hint_label_ != nullptr) {
            lv_label_set_text(ac_hint_label_, "");
            lv_obj_add_flag(ac_hint_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

} // namespace xcas
