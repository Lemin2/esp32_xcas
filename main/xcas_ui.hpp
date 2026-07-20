#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "cardputer_bsp/cardputer_bsp.hpp"
#include "mathlayout/paint/math_painter.hpp"
#include "xcas_service.hpp"

namespace xcas
{

    class XcasUi
    {
    public:
        XcasUi(board::IBsp &board, XcasService &service);

        void enqueueInputKey(uint32_t key);
        void render();
        void show();
        void hide();
        void releaseUi();
        bool toggleScreenKeyboard();
        void debugSubmitFormula(const std::string &formula);
        void debugEmitFormulaImage(const std::string &formula);

    private:
        static void lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
        static void onHistoryRowEvent(lv_event_t *e);
        static void lvglKeyboardRead(lv_indev_t *indev, lv_indev_data_t *data);
        static void onInputBoxEvent(lv_event_t *e);
        static void onKeyboardModeEvent(lv_event_t *e);
        static void onScreenKeyboardEvent(lv_event_t *e);
        static void onPreviewBackEvent(lv_event_t *e);

        void initializeLvgl();
        void loadSession();
        void saveSession();
        void buildLayout();
        void updateHeaderText();
        void setStatusText(const char *text);
        void updateBusyBinding(bool busy);
        void appendInput(const char *text);
        void appendHistory(const std::string &line);
        void refreshHistoryList();
        void clearHistorySelection();
        void moveHistorySelection(int delta);
        void selectHistoryIndex(int index, bool applyToInput);
        void submitInput();
        void queueFormulaShot(std::vector<uint16_t> &&pixels, int width, int height);
        void clearPendingFormulaShot();
        void processPendingFormulaShot();
        void setEditorFullscreen(bool enabled);
        void refreshEditorPreview();
        void openHistoryFullscreenPreview();
        void closeHistoryFullscreenPreview();
        void renderFullscreenPreview(const std::string &line);
        void panFullscreenPreview(int dx, int dy);
        void updateFullscreenPreviewPosition();
        bool beginFullscreenPreviewPaint();
        void stepFullscreenPreviewPaint(size_t max_commands, size_t max_line_commands);

        static std::string trimCopy(const std::string &s);
        static int findTopLevelChar(const std::string &s, char ch);
        static bool hasOuterParens(const std::string &s);
        static std::string renderNatural2D(const std::string &expr, int depth = 0);
        static std::string centerText(const std::string &s, int width);

        void setScreenKeyboardMathMode(bool enabled);
        void setScreenKeyboardMathPage(int page);
        void setScreenKeyboardVisible(bool visible);
        lv_coord_t screenKeyboardHeight() const;
        lv_coord_t screenKeyboardBottomReserved() const;
        void updateScreenKeyboardLayout();

        board::IBsp &board_;
        XcasService &service_;

        std::array<uint16_t, board::IBsp::kDisplayWidth * 20> lv_draw_pixels_{};

        lv_display_t *lv_display_;
        lv_indev_t *keyboard_indev_;
        lv_group_t *input_group_;
        lv_obj_t *header_label_;
        lv_obj_t *status_label_;
        lv_obj_t *info_label_;
        lv_obj_t *history_panel_;
        lv_obj_t *history_list_;
        lv_obj_t *editor_panel_;
        lv_obj_t *editor_preview_host_;
        lv_obj_t *editor_preview_formula_;
        lv_obj_t *editor_preview_label_;
        lv_obj_t *editor_preview_back_btn_;
        lv_obj_t *editor_hint_label_;
        lv_obj_t *input_box_;
        lv_obj_t *screen_keyboard_;
        lv_obj_t *keyboard_mode_bar_;
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
        int selected_history_index_;
        uint64_t last_render_us_;
        std::array<uint32_t, 64> key_queue_{};
        uint8_t key_queue_head_ = 0;
        uint8_t key_queue_tail_ = 0;
        uint32_t pending_release_key_ = 0;
        bool has_pending_release_ = false;
        bool lvgl_initialized_;
        bool session_loaded_ = false;
        bool redraw_recovery_pending_;
        bool subjects_initialized_;
        bool editor_fullscreen_ = false;
        bool screen_keyboard_math_ = false;
        int screen_keyboard_page_ = 0;
        bool screen_keyboard_visible_ = true;

        std::vector<uint16_t> pending_formula_shot_pixels_;
        size_t pending_formula_shot_byte_offset_ = 0;
        int pending_formula_shot_width_ = 0;
        int pending_formula_shot_height_ = 0;
        int64_t pending_formula_shot_started_us_ = 0;
        int64_t pending_formula_shot_last_emit_us_ = 0;
        bool pending_formula_shot_active_ = false;

        bool preview_use_objects_ = false;
        int preview_content_w_ = 0;
        int preview_content_h_ = 0;
        int preview_pan_x_ = 0;
        int preview_pan_y_ = 0;
        bool preview_paint_pending_ = false;
        mathlayout::DrawList preview_draw_list_;
        mathlayout::LvglObjectPaintState preview_paint_state_;
        std::string preview_line_;
    };

} // namespace xcas
