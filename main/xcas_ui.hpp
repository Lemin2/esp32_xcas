#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "cardputer_bsp.hpp"
#include "xcas_service.hpp"

namespace xcas
{

    class XcasUi
    {
    public:
        struct KeyLabel
        {
            const char *base;
            const char *shifted;
        };

        static constexpr int kKeyRowCount = 4;
        static constexpr int kKeyColCount = 14;
        static constexpr int kKeyCount = kKeyRowCount * kKeyColCount;

        XcasUi(board::CardputerBsp &board, XcasService &service);

        void handleKeyboardState(uint64_t pressed_mask);
        void render();
        void show();
        void hide();

    private:
        static constexpr int kFnRow = 2;
        static constexpr int kFnCol = 0;
        static constexpr int kShiftRow = 2;
        static constexpr int kShiftCol = 1;

        static int keyIndex(int row, int col);
        static const KeyLabel &keyAt(int row, int col);
        static bool keyIsDown(uint64_t mask, int row, int col);
        static bool keyIs(const KeyLabel &key, const char *name);
        static void lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
        static void onHistoryRowEvent(lv_event_t *e);

        void initializeLvgl();
        void buildLayout();
        void updateHeaderText();
        void setStatusText(const char *text);
        void updateBusyBinding(bool busy);
        void recallHistory(int delta);
        void appendInput(const char *text);
        void appendHistory(const std::string &line);
        void refreshHistoryList();
        void selectHistoryIndex(int index, bool applyToInput);
        void submitInput();

        board::CardputerBsp &board_;
        XcasService &service_;

        std::array<uint16_t, board::CardputerBsp::kDisplayWidth * 20> lv_draw_pixels_{};

        lv_display_t *lv_display_;
        lv_obj_t *header_label_;
        lv_obj_t *status_label_;
        lv_obj_t *info_label_;
        lv_obj_t *history_panel_;
        lv_obj_t *history_list_;
        lv_obj_t *input_box_;
        lv_obj_t *root_;

        lv_style_t busy_style_;

        lv_subject_t header_subject_;
        lv_subject_t status_subject_;
        lv_subject_t busy_subject_;
        std::array<char, 96> header_text_buf_{};
        std::array<char, 96> header_text_prev_buf_{};
        std::array<char, 128> status_text_buf_{};
        std::array<char, 128> status_text_prev_buf_{};

        std::vector<std::string> history_lines_;
        std::vector<std::string> command_history_;
        std::string history_draft_input_;
        int selected_history_index_;
        int history_cursor_;
        uint64_t previous_key_mask_;
        uint64_t last_render_us_;
        bool lvgl_initialized_;
        bool redraw_recovery_pending_;
        bool fn_toggled_;
        bool caps_toggled_;
        bool subjects_initialized_;
    };

} // namespace xcas
