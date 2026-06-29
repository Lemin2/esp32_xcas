#pragma once

#include <array>
#include <cstdint>
#include <string>

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

        void initializeLvgl();
        void buildLayout();
        void switchMenu(int delta);
        void appendInput(const char *text);
        void appendHistory(const std::string &line);
        void submitInput();

        board::CardputerBsp &board_;
        XcasService &service_;

        std::array<uint16_t, board::CardputerBsp::kDisplayWidth * 20> lv_draw_pixels_{};

        lv_display_t *lv_display_;
        lv_obj_t *menu_label_;
        lv_obj_t *status_label_;
        lv_obj_t *history_panel_;
        lv_obj_t *history_label_;
        lv_obj_t *input_box_;

        std::string history_text_;
        uint64_t previous_key_mask_;
        uint64_t last_render_us_;
        int menu_index_;
        bool lvgl_initialized_;
        bool redraw_recovery_pending_;
        bool fn_toggled_;
        bool caps_toggled_;
    };

} // namespace xcas
