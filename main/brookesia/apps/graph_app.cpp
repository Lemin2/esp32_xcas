#include "brookesia/apps/graph_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "esp_timer.h"

#include "brookesia/core/ui_theme.hpp"
#include "brookesia/apps/fs_util.hpp"

#if LV_USE_GESTURE_RECOGNITION
#include "src/indev/lv_indev_gesture.h"
#endif

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr int kFuncColorsCount = 4;
const lv_color_t kFuncColors[kFuncColorsCount] = {
    LV_COLOR_MAKE(220, 80, 60),
    LV_COLOR_MAKE(60, 160, 220),
    LV_COLOR_MAKE(60, 200, 140),
    LV_COLOR_MAKE(220, 180, 40),
};

const char *const kDefaultExprs[] = {
    "sin(x)",
    "cos(x)",
    "tan(x)",
    "x^2",
};

const char *const kMenuInputLabels[] = {
    "Plot page",
    "Table page",
    "Edit function",
    "Toggle function",
    "Cycle color",
    "Reset function",
};

const char *const kMenuPlotLabels[] = {
    "Input page",
    "Table page",
    "Cursor on/off",
    "Reset view",
    "Zoom in",
    "Zoom out",
    "Scale x",
    "Scale y",
    "Scale factor",
    "Region zoom",
    "Equal scale on/off",
};

const char *const kMenuTableLabels[] = {
    "Input page",
    "Plot page",
    "Range start",
    "Range end",
    "Step",
    "Rebuild table",
};

void styleLine(lv_obj_t *line, lv_color_t color, int width)
{
    if (line == nullptr) {
        return;
    }
    lv_obj_set_style_line_width(line, width, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, color, LV_PART_MAIN);
}

lv_point_t measureLabel(lv_obj_t *label, const char *text)
{
    lv_point_t size{1, 1};
    if (label == nullptr || text == nullptr) {
        return size;
    }
    const lv_font_t *font = static_cast<const lv_font_t *>(lv_obj_get_style_text_font(label, LV_PART_MAIN));
    if (font == nullptr) {
        font = ui_theme::textFont14();
    }
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    size.x = std::max<lv_coord_t>(1, size.x);
    size.y = std::max<lv_coord_t>(1, size.y);
    return size;
}

int clampCoord(int value, int max_value)
{
    return std::clamp(value, 0, std::max(0, max_value));
}

lv_obj_t *createPlotLine(lv_obj_t *parent, lv_color_t color, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    styleLine(line, color, width);
    return line;
}

} // namespace

GraphApp::GraphApp(ServiceHub &services) : services_(services)
{
    for (int i = 0; i < kMaxFuncs; ++i) {
        funcs_[i].color = kFuncColors[i % kFuncColorsCount];
    }
}

bool GraphApp::init()
{
    return true;
}

void GraphApp::addDefaultFunctions()
{
    if (funcs_[0].expr[0] == '\0') {
        std::strncpy(funcs_[0].expr, kDefaultExprs[0], kMaxExprLen - 1);
        funcs_[0].enabled = true;
    }
    if (funcs_[1].expr[0] == '\0') {
        std::strncpy(funcs_[1].expr, kDefaultExprs[1], kMaxExprLen - 1);
        funcs_[1].enabled = true;
    }
}

void GraphApp::ensureUi()
{
    if (ui_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, displayW(), rootH());
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, services_.board().statusBarHeight());
    ui_theme::applyPage(root_, LV_COLOR_MAKE(11, 15, 24));
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    input_page_ = lv_obj_create(root_);
    lv_obj_remove_style_all(input_page_);
    lv_obj_set_size(input_page_, displayW(), rootH());
    lv_obj_clear_flag(input_page_, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme::applyPage(input_page_, LV_COLOR_MAKE(16, 20, 30));
    lv_obj_set_flex_flow(input_page_, LV_FLEX_FLOW_COLUMN);
    const bool touch = services_.board().hasTouchInput();
    lv_obj_set_style_pad_all(input_page_, touch ? 10 : 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(input_page_, touch ? 10 : 3, LV_PART_MAIN);

    input_list_ = lv_obj_create(input_page_);
    lv_obj_remove_style_all(input_list_);
    lv_obj_set_width(input_list_, displayW() - 8);
    lv_obj_set_flex_grow(input_list_, 1);
    lv_obj_set_flex_flow(input_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(input_list_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(input_list_, touch ? 8 : 2, LV_PART_MAIN);

    for (int i = 0; i < kMaxFuncs; ++i) {
        lv_obj_t *row = lv_obj_create(input_list_);
        input_rows_[i] = row;
        lv_obj_set_width(row, displayW() - (touch ? 20 : 8));
        lv_obj_set_height(row, touch ? 56 : 22);
        ui_theme::applyRowCard(row, LV_COLOR_MAKE(40, 55, 78), touch ? 8 : 4, touch ? 10 : 2, touch ? 10 : 2);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        funcs_[i].checkbox = lv_checkbox_create(row);
        lv_checkbox_set_text(funcs_[i].checkbox, "");
        lv_obj_set_width(funcs_[i].checkbox, touch ? 44 : 20);
        lv_obj_set_height(funcs_[i].checkbox, touch ? 44 : 20);
        lv_obj_set_style_text_color(funcs_[i].checkbox, LV_COLOR_MAKE(220, 230, 245), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(funcs_[i].checkbox, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(funcs_[i].checkbox, 0, LV_PART_MAIN);

        funcs_[i].color_label = lv_label_create(row);
        lv_label_set_text(funcs_[i].color_label, "■");
        ui_theme::applyText14(funcs_[i].color_label);
        lv_obj_set_style_text_color(funcs_[i].color_label, funcs_[i].color, LV_PART_MAIN);
        lv_obj_set_width(funcs_[i].color_label, touch ? 34 : 18);

        funcs_[i].expr_label = lv_label_create(row);
        ui_theme::applyText14(funcs_[i].expr_label);
        lv_obj_set_style_text_color(funcs_[i].expr_label, LV_COLOR_MAKE(230, 236, 246), LV_PART_MAIN);
        lv_obj_set_style_pad_left(funcs_[i].expr_label, 2, LV_PART_MAIN);
        lv_label_set_long_mode(funcs_[i].expr_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(funcs_[i].expr_label, displayW() - (touch ? 110 : 40));
    }

    plot_page_ = lv_obj_create(root_);
    lv_obj_remove_style_all(plot_page_);
    lv_obj_set_size(plot_page_, displayW(), rootH());
    lv_obj_clear_flag(plot_page_, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme::applyPage(plot_page_, LV_COLOR_MAKE(8, 10, 18));

    plot_area_ = lv_obj_create(plot_page_);
    lv_obj_remove_style_all(plot_area_);
    lv_obj_set_pos(plot_area_, 0, 0);
    lv_obj_set_size(plot_area_, displayW(), rootH());
    ui_theme::applyPage(plot_area_, LV_COLOR_MAKE(8, 10, 18));
    lv_obj_clear_flag(plot_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(plot_area_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(plot_area_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE));
    lv_obj_add_flag(plot_area_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_event_cb(plot_area_, &GraphApp::onPlotTouchEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(plot_area_, &GraphApp::onPlotTouchEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(plot_area_, &GraphApp::onPlotTouchEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(plot_area_, &GraphApp::onPlotTouchEvent, LV_EVENT_PRESS_LOST, this);
#if LV_USE_GESTURE_RECOGNITION
    lv_obj_add_event_cb(plot_area_, &GraphApp::onPlotTouchEvent, LV_EVENT_GESTURE, this);
    if (lv_indev_t *indev = lv_indev_active()) {
        lv_indev_set_gesture_min_distance(indev, 8);
        lv_indev_set_gesture_min_velocity(indev, 1);
        lv_indev_set_pinch_up_threshold(indev, 1.08f);
        lv_indev_set_pinch_down_threshold(indev, 0.92f);
    }
#endif

    axis_x_ = createPlotLine(plot_area_, LV_COLOR_MAKE(70, 84, 110), 1);
    axis_y_ = createPlotLine(plot_area_, LV_COLOR_MAKE(70, 84, 110), 1);
    cursor_line_ = createPlotLine(plot_area_, LV_COLOR_MAKE(240, 220, 80), 1);
    cursor_h_line_ = createPlotLine(plot_area_, LV_COLOR_MAKE(240, 220, 80), 1);
    if (cursor_line_) lv_obj_add_flag(cursor_line_, LV_OBJ_FLAG_HIDDEN);
    if (cursor_h_line_) lv_obj_add_flag(cursor_h_line_, LV_OBJ_FLAG_HIDDEN);

    cursor_info_label_ = lv_label_create(plot_area_);
    if (cursor_info_label_) {
        ui_theme::applyText14(cursor_info_label_);
        lv_obj_set_style_text_color(cursor_info_label_, LV_COLOR_MAKE(245, 235, 160), LV_PART_MAIN);
        lv_obj_set_style_bg_color(cursor_info_label_, LV_COLOR_MAKE(8, 10, 18), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cursor_info_label_, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(cursor_info_label_, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(cursor_info_label_, 1, LV_PART_MAIN);
        lv_obj_align(cursor_info_label_, LV_ALIGN_TOP_LEFT, 2, 2);
        lv_obj_add_flag(cursor_info_label_, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < kMaxTicks; ++i) {
        x_tick_lines_[i] = createPlotLine(plot_area_, LV_COLOR_MAKE(45, 60, 82), 1);
        y_tick_lines_[i] = createPlotLine(plot_area_, LV_COLOR_MAKE(45, 60, 82), 1);
        x_tick_labels_[i] = lv_label_create(plot_area_);
        y_tick_labels_[i] = lv_label_create(plot_area_);
        if (x_tick_labels_[i]) {
            ui_theme::applyText14(x_tick_labels_[i]);
            lv_obj_set_style_text_color(x_tick_labels_[i], LV_COLOR_MAKE(130, 145, 170), LV_PART_MAIN);
        }
        if (y_tick_labels_[i]) {
            ui_theme::applyText14(y_tick_labels_[i]);
            lv_obj_set_style_text_color(y_tick_labels_[i], LV_COLOR_MAKE(130, 145, 170), LV_PART_MAIN);
        }
    }

    for (int fi = 0; fi < kMaxFuncs; ++fi) {
        for (int si = 0; si < kMaxPlotSegments; ++si) {
            funcs_[fi].segments[si] = nullptr;
        }
    }

    table_page_ = lv_obj_create(root_);
    lv_obj_remove_style_all(table_page_);
    lv_obj_set_size(table_page_, displayW(), rootH());
    lv_obj_clear_flag(table_page_, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme::applyPage(table_page_, LV_COLOR_MAKE(14, 18, 28));
    lv_obj_set_flex_flow(table_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(table_page_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(table_page_, 2, LV_PART_MAIN);

    table_obj_ = lv_table_create(table_page_);
    lv_obj_set_width(table_obj_, displayW() - 8);
    lv_obj_set_flex_grow(table_obj_, 1);
    ui_theme::applyPanel(table_obj_, LV_COLOR_MAKE(20, 26, 38), LV_COLOR_MAKE(44, 58, 82));
    ui_theme::applyText14(table_obj_);
    lv_obj_set_style_text_color(table_obj_, LV_COLOR_MAKE(230, 236, 246), LV_PART_MAIN);

    buildMenuOverlay();
    buildEntryOverlay();
    addDefaultFunctions();
    refreshPageVisibility();
    updateInputPage();
    plot_dirty_ = true;
    table_dirty_ = true;
    ui_ready_ = true;
}

void GraphApp::buildMenuOverlay()
{
    menu_overlay_ = lv_obj_create(root_);
    lv_obj_remove_style_all(menu_overlay_);
    const bool touch = services_.board().hasTouchInput();
    lv_obj_set_size(menu_overlay_, touch ? 420 : 224, touch ? 360 : 112);
    lv_obj_align(menu_overlay_, LV_ALIGN_CENTER, 0, 0);
    ui_theme::applyMenuOverlay(menu_overlay_, LV_COLOR_MAKE(22, 28, 42), LV_COLOR_MAKE(64, 80, 110));
    lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);

    menu_list_ = lv_menu_create(menu_overlay_);
    lv_obj_set_size(menu_list_, touch ? 404 : 216, touch ? 344 : 104);
    lv_obj_align(menu_list_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(menu_list_, LV_COLOR_MAKE(22, 28, 42), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu_list_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_list_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_list_, touch ? 8 : 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_menu_set_mode_header(menu_list_, LV_MENU_HEADER_TOP_FIXED);
    lv_menu_set_mode_root_back_button(menu_list_, LV_MENU_ROOT_BACK_BUTTON_DISABLED);
    menu_page_ = lv_menu_page_create(menu_list_, nullptr);
    lv_menu_set_page(menu_list_, menu_page_);
    menu_items_.clear();
    menu_rows_.clear();
}

void GraphApp::buildEntryOverlay()
{
    entry_overlay_ = lv_obj_create(root_);
    lv_obj_remove_style_all(entry_overlay_);
    const bool touch = services_.board().hasTouchInput();
    lv_obj_set_size(entry_overlay_, touch ? 520 : 232, touch ? 118 : 54);
    lv_obj_align(entry_overlay_, LV_ALIGN_BOTTOM_MID, 0, -2);
    ui_theme::applyPanel(entry_overlay_, LV_COLOR_MAKE(18, 26, 42), LV_COLOR_MAKE(64, 84, 116), 8, touch ? 8 : 3, touch ? 8 : 3);
    lv_obj_set_flex_flow(entry_overlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(entry_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(entry_overlay_, LV_OBJ_FLAG_HIDDEN);

    entry_title_ = lv_label_create(entry_overlay_);
    ui_theme::applyText16(entry_title_);
    lv_obj_set_style_text_color(entry_title_, LV_COLOR_MAKE(180, 200, 230), LV_PART_MAIN);

    entry_box_ = lv_textarea_create(entry_overlay_);
    lv_obj_set_size(entry_box_, touch ? 504 : 224, touch ? 54 : 24);
    lv_textarea_set_one_line(entry_box_, true);
    ui_theme::applyText14(entry_box_);
    lv_obj_set_style_text_color(entry_box_, LV_COLOR_MAKE(240, 242, 246), LV_PART_MAIN);
    lv_obj_set_style_bg_color(entry_box_, LV_COLOR_MAKE(28, 36, 52), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(entry_box_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(entry_box_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(entry_box_, touch ? 8 : 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(entry_box_, LV_COLOR_MAKE(240, 220, 80), LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(entry_box_, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_add_state(entry_box_, LV_STATE_FOCUSED);
}

void GraphApp::refreshPageVisibility()
{
    if (input_page_) {
        if (page_ == Page::Input) lv_obj_clear_flag(input_page_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(input_page_, LV_OBJ_FLAG_HIDDEN);
    }
    if (plot_page_) {
        if (page_ == Page::Plot) lv_obj_clear_flag(plot_page_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(plot_page_, LV_OBJ_FLAG_HIDDEN);
    }
    if (table_page_) {
        if (page_ == Page::Table) lv_obj_clear_flag(table_page_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(table_page_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::showPage(Page page)
{
    page_ = page;
    cursor_mode_ = false;
    region_zoom_active_ = false;
    region_zoom_anchor_set_ = false;
    closeMenu();
    if (entry_overlay_) lv_obj_add_flag(entry_overlay_, LV_OBJ_FLAG_HIDDEN);
    refreshPageVisibility();
    if (page_ == Page::Input) updateInputPage();
    if (page_ == Page::Plot) updatePlotPage();
    if (page_ == Page::Table) {
        updateTablePage();
        if (!table_dirty_ && !eval_pending_ && !table_rebuild_pending_) {
            beginTableRebuild();
        }
    }
}

void GraphApp::onFocus()
{
    if (!session_loaded_) {
        loadSession();
        session_loaded_ = true;
    }

    ensureUi();
    if (root_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    if (plot_equal_scale_) {
        normalizePlotAspect();
    }
    markPlotDirty();
    markTableDirty();
    updateInputPage();
    updatePlotPage();
    updateTablePage();
}

void GraphApp::onBlur()
{
    saveSession();
    if (root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::releaseUi()
{
    if (menu_group_ != nullptr) {
        lv_group_delete(menu_group_);
        menu_group_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }

    root_ = nullptr;
    input_page_ = nullptr;
    plot_page_ = nullptr;
    table_page_ = nullptr;
    menu_overlay_ = nullptr;
    menu_list_ = nullptr;
    menu_page_ = nullptr;
    entry_overlay_ = nullptr;
    input_list_ = nullptr;
    plot_area_ = nullptr;
    table_obj_ = nullptr;
    axis_x_ = nullptr;
    axis_y_ = nullptr;
    cursor_line_ = nullptr;
    cursor_h_line_ = nullptr;
    cursor_info_label_ = nullptr;
    entry_title_ = nullptr;
    entry_box_ = nullptr;
    for (int i = 0; i < kMaxFuncs; ++i) {
        input_rows_[i] = nullptr;
        funcs_[i].checkbox = nullptr;
        funcs_[i].expr_label = nullptr;
        funcs_[i].color_label = nullptr;
        for (int s = 0; s < kMaxPlotSegments; ++s) {
            funcs_[i].segments[s] = nullptr;
        }
    }
    for (int i = 0; i < kMaxTicks; ++i) {
        x_tick_lines_[i] = nullptr;
        y_tick_lines_[i] = nullptr;
        x_tick_labels_[i] = nullptr;
        y_tick_labels_[i] = nullptr;
    }
    menu_rows_.clear();
    menu_open_ = false;
    menu_kind_ = MenuKind::None;
    entry_kind_ = EntryKind::None;
    ui_ready_ = false;
}

bool GraphApp::handleMenuButton()
{
    ensureUi();
    openPageMenu();
    return true;
}

void GraphApp::loadSession()
{
    if (!ensureStorageMounted()) {
        return;
    }

    FILE *f = std::fopen("/data/graph_session.txt", "r");
    if (f == nullptr) {
        return;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        size_t n = std::strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            --n;
        }

        if (std::strncmp(line, "plot_x_min=", 11) == 0) {
            plot_x_min_ = std::strtof(line + 11, nullptr);
        } else if (std::strncmp(line, "plot_x_max=", 11) == 0) {
            plot_x_max_ = std::strtof(line + 11, nullptr);
        } else if (std::strncmp(line, "plot_y_min=", 11) == 0) {
            plot_y_min_ = std::strtof(line + 11, nullptr);
        } else if (std::strncmp(line, "plot_y_max=", 11) == 0) {
            plot_y_max_ = std::strtof(line + 11, nullptr);
        } else if (std::strncmp(line, "table_start=", 12) == 0) {
            table_start_ = std::strtof(line + 12, nullptr);
        } else if (std::strncmp(line, "table_end=", 10) == 0) {
            table_end_ = std::strtof(line + 10, nullptr);
        } else if (std::strncmp(line, "table_step=", 11) == 0) {
            table_step_ = std::strtof(line + 11, nullptr);
        } else if (std::strncmp(line, "f", 1) == 0) {
            int idx = -1;
            int enabled = 0;
            char expr[kMaxExprLen] = {};
            if (std::sscanf(line, "f%d=%d|%95[^\n]", &idx, &enabled, expr) == 3) {
                if (idx >= 0 && idx < kMaxFuncs) {
                    funcs_[idx].enabled = (enabled != 0);
                    std::snprintf(funcs_[idx].expr, sizeof(funcs_[idx].expr), "%s", expr);
                }
            }
        }
    }

    std::fclose(f);
}

void GraphApp::saveSession() const
{
    if (!ensureStorageMounted()) {
        return;
    }

    FILE *f = std::fopen("/data/graph_session.txt", "w");
    if (f == nullptr) {
        return;
    }

    std::fprintf(f, "plot_x_min=%g\n", static_cast<double>(plot_x_min_));
    std::fprintf(f, "plot_x_max=%g\n", static_cast<double>(plot_x_max_));
    std::fprintf(f, "plot_y_min=%g\n", static_cast<double>(plot_y_min_));
    std::fprintf(f, "plot_y_max=%g\n", static_cast<double>(plot_y_max_));
    std::fprintf(f, "table_start=%g\n", static_cast<double>(table_start_));
    std::fprintf(f, "table_end=%g\n", static_cast<double>(table_end_));
    std::fprintf(f, "table_step=%g\n", static_cast<double>(table_step_));
    for (int i = 0; i < kMaxFuncs; ++i) {
        std::fprintf(f, "f%d=%d|%s\n", i, funcs_[i].enabled ? 1 : 0, funcs_[i].expr);
    }

    std::fclose(f);
}

float GraphApp::parseEntryValue(const char *text, float fallback) const
{
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char *end = nullptr;
    float value = std::strtof(text, &end);
    if (end == text) {
        return fallback;
    }
    return value;
}

int GraphApp::xToPlot(float x) const
{
    if (plot_x_max_ <= plot_x_min_) return 0;
    float ratio = (x - plot_x_min_) / (plot_x_max_ - plot_x_min_);
    int px = static_cast<int>(ratio * static_cast<float>(displayW() - 1));
    return std::clamp(px, 0, displayW() - 1);
}

int GraphApp::yToPlot(float y) const
{
    if (plot_y_max_ <= plot_y_min_) return rootH() / 2;
    float ratio = (plot_y_max_ - y) / (plot_y_max_ - plot_y_min_);
    int py = static_cast<int>(ratio * static_cast<float>(rootH() - 1));
    return std::clamp(py, 0, rootH() - 1);
}

float GraphApp::plotXAt(int sample) const
{
    if (kPlotSamples <= 1) return plot_x_min_;
    const float t = static_cast<float>(sample) / static_cast<float>(kPlotSamples - 1);
    return plot_x_min_ + t * (plot_x_max_ - plot_x_min_);
}

float GraphApp::cursorYValue() const
{
    if (active_plot_func_ < 0 || active_plot_func_ >= kMaxFuncs) return NAN;
    const PlotFunc &func = funcs_[active_plot_func_];
    if (!func.enabled || cursor_sample_ < 0 || cursor_sample_ >= static_cast<int>(func.plot_values.size())) return NAN;
    return func.plot_values[static_cast<size_t>(cursor_sample_)];
}

float GraphApp::finiteFallbackY() const
{
    if (plot_y_max_ > plot_y_min_) {
        return plot_y_max_;
    }
    return 1.0f;
}

float GraphApp::computeDerivative(int func_index, int sample) const
{
    if (func_index < 0 || func_index >= kMaxFuncs) return NAN;
    const std::vector<float> &values = funcs_[func_index].plot_values;
    if (values.size() != static_cast<size_t>(kPlotSamples)) return NAN;
    const float dx = (plot_x_max_ - plot_x_min_) / static_cast<float>(kPlotSamples - 1);
    if (!(dx > 0.0f)) return NAN;
    const bool prev_ok = sample > 0 && std::isfinite(values[sample - 1]);
    const bool next_ok = sample + 1 < kPlotSamples && std::isfinite(values[sample + 1]);
    if (prev_ok && next_ok) return (values[sample + 1] - values[sample - 1]) / (2.0f * dx);
    if (next_ok) return (values[sample + 1] - values[sample]) / dx;
    if (prev_ok) return (values[sample] - values[sample - 1]) / dx;
    return NAN;
}

float GraphApp::niceStep(float range, int max_ticks) const
{
    if (!(range > 0.0f) || max_ticks <= 0) return 1.0f;
    const float raw = range / static_cast<float>(max_ticks);
    const float mag = std::pow(10.0f, std::floor(std::log10(raw)));
    const float norm = raw / mag;
    if (norm < 1.5f) return mag;
    if (norm < 3.5f) return 2.0f * mag;
    if (norm < 7.5f) return 5.0f * mag;
    return 10.0f * mag;
}

int GraphApp::displayW() const
{
    return services_.board().displayWidth();
}

int GraphApp::rootH() const
{
    return services_.board().displayHeight() - services_.board().statusBarHeight();
}

void GraphApp::markPlotDirty()
{
    ++generation_;
    plot_dirty_ = true;
    for (auto &func : funcs_) {
        func.plot_ready = false;
    }
}

void GraphApp::markTableDirty()
{
    ++generation_;
    table_dirty_ = true;
    for (auto &func : funcs_) {
        func.table_ready = false;
    }
}

void GraphApp::resetPlotView()
{
    plot_x_min_ = -6.0f;
    plot_x_max_ = 6.0f;
    plot_y_min_ = -4.0f;
    plot_y_max_ = 4.0f;
    plot_equal_scale_ = true;
    region_zoom_active_ = false;
    region_zoom_anchor_set_ = false;
    markPlotDirty();
}

void GraphApp::normalizePlotAspect()
{
    const float x_span = plot_x_max_ - plot_x_min_;
    const float y_span = plot_y_max_ - plot_y_min_;
    if (!(x_span > 0.0f) || !(y_span > 0.0f)) {
        return;
    }

    const float plot_aspect = static_cast<float>(displayW()) / static_cast<float>(rootH());
    const float current_aspect = x_span / y_span;
    const float cx = (plot_x_min_ + plot_x_max_) * 0.5f;
    const float cy = (plot_y_min_ + plot_y_max_) * 0.5f;

    if (current_aspect > plot_aspect) {
        const float half = x_span * 0.5f;
        const float new_half_y = half / plot_aspect;
        plot_x_min_ = cx - half;
        plot_x_max_ = cx + half;
        plot_y_min_ = cy - new_half_y;
        plot_y_max_ = cy + new_half_y;
    } else {
        const float half = y_span * 0.5f;
        const float new_half_x = half * plot_aspect;
        plot_x_min_ = cx - new_half_x;
        plot_x_max_ = cx + new_half_x;
        plot_y_min_ = cy - half;
        plot_y_max_ = cy + half;
    }
}

void GraphApp::applyUniformScale(float factor)
{
    if (!(factor > 0.0f)) {
        return;
    }

    const float cx = (plot_x_min_ + plot_x_max_) * 0.5f;
    const float cy = (plot_y_min_ + plot_y_max_) * 0.5f;
    const float half_x = (plot_x_max_ - plot_x_min_) * 0.5f * factor;
    const float half_y = (plot_y_max_ - plot_y_min_) * 0.5f * factor;
    plot_x_min_ = cx - half_x;
    plot_x_max_ = cx + half_x;
    plot_y_min_ = cy - half_y;
    plot_y_max_ = cy + half_y;
    if (plot_equal_scale_) {
        normalizePlotAspect();
    }
    markPlotDirty();
}

void GraphApp::applyAxisScale(float factor, bool scale_x)
{
    if (!(factor > 0.0f)) {
        return;
    }

    const float cx = (plot_x_min_ + plot_x_max_) * 0.5f;
    const float cy = (plot_y_min_ + plot_y_max_) * 0.5f;
    plot_equal_scale_ = false;
    if (scale_x) {
        const float half_x = (plot_x_max_ - plot_x_min_) * 0.5f * factor;
        plot_x_min_ = cx - half_x;
        plot_x_max_ = cx + half_x;
    } else {
        const float half_y = (plot_y_max_ - plot_y_min_) * 0.5f * factor;
        plot_y_min_ = cy - half_y;
        plot_y_max_ = cy + half_y;
    }
    markPlotDirty();
}

void GraphApp::panPlotByPixels(int dx, int dy)
{
    const int w = std::max(1, displayW());
    const int h = std::max(1, rootH());
    const float xr = plot_x_max_ - plot_x_min_;
    const float yr = plot_y_max_ - plot_y_min_;
    if (!(xr > 0.0f) || !(yr > 0.0f)) {
        return;
    }

    plot_x_min_ -= static_cast<float>(dx) * xr / static_cast<float>(w);
    plot_x_max_ -= static_cast<float>(dx) * xr / static_cast<float>(w);
    plot_y_min_ += static_cast<float>(dy) * yr / static_cast<float>(h);
    plot_y_max_ += static_cast<float>(dy) * yr / static_cast<float>(h);
    markPlotDirty();
}

void GraphApp::zoomPlotAt(float factor, int px, int py)
{
    factor = std::clamp(factor, 0.2f, 5.0f);
    const int w = std::max(1, displayW());
    const int h = std::max(1, rootH());
    const float xr = plot_x_max_ - plot_x_min_;
    const float yr = plot_y_max_ - plot_y_min_;
    if (!(xr > 0.0f) || !(yr > 0.0f)) {
        return;
    }

    px = std::clamp(px, 0, w - 1);
    py = std::clamp(py, 0, h - 1);
    const float cx = plot_x_min_ + static_cast<float>(px) * xr / static_cast<float>(w);
    const float cy = plot_y_max_ - static_cast<float>(py) * yr / static_cast<float>(h);
    plot_x_min_ = cx - (cx - plot_x_min_) * factor;
    plot_x_max_ = cx + (plot_x_max_ - cx) * factor;
    plot_y_min_ = cy - (cy - plot_y_min_) * factor;
    plot_y_max_ = cy + (plot_y_max_ - cy) * factor;
    markPlotDirty();
}

void GraphApp::beginPlotTouchPreview()
{
    plot_touch_dragging_ = true;
    plot_touch_pending_ = false;
    plot_touch_dx_ = 0;
    plot_touch_dy_ = 0;
    plot_last_pinch_scale_ = 1.0f;
    plot_touch_start_x_min_ = plot_x_min_;
    plot_touch_start_x_max_ = plot_x_max_;
    plot_touch_start_y_min_ = plot_y_min_;
    plot_touch_start_y_max_ = plot_y_max_;
    plot_touch_pending_x_min_ = plot_x_min_;
    plot_touch_pending_x_max_ = plot_x_max_;
    plot_touch_pending_y_min_ = plot_y_min_;
    plot_touch_pending_y_max_ = plot_y_max_;
    showPlotTouchPreview("Touch plot");
}

void GraphApp::updatePlotTouchPanPreview(int dx, int dy)
{
    const int w = std::max(1, displayW());
    const int h = std::max(1, rootH());
    const float xr = plot_touch_start_x_max_ - plot_touch_start_x_min_;
    const float yr = plot_touch_start_y_max_ - plot_touch_start_y_min_;
    if (!(xr > 0.0f) || !(yr > 0.0f)) {
        return;
    }

    plot_touch_dx_ += dx;
    plot_touch_dy_ += dy;
    plot_touch_pending_x_min_ = plot_touch_start_x_min_ - static_cast<float>(plot_touch_dx_) * xr / static_cast<float>(w);
    plot_touch_pending_x_max_ = plot_touch_start_x_max_ - static_cast<float>(plot_touch_dx_) * xr / static_cast<float>(w);
    plot_touch_pending_y_min_ = plot_touch_start_y_min_ + static_cast<float>(plot_touch_dy_) * yr / static_cast<float>(h);
    plot_touch_pending_y_max_ = plot_touch_start_y_max_ + static_cast<float>(plot_touch_dy_) * yr / static_cast<float>(h);
    plot_touch_pending_ = true;
    showPlotTouchPreview("Pan");
}

void GraphApp::updatePlotTouchZoomPreview(float scale, int px, int py)
{
    if (!std::isfinite(scale) || !(scale > 0.01f)) {
        return;
    }

    const int w = std::max(1, displayW());
    const int h = std::max(1, rootH());
    const float xr = plot_touch_start_x_max_ - plot_touch_start_x_min_;
    const float yr = plot_touch_start_y_max_ - plot_touch_start_y_min_;
    if (!(xr > 0.0f) || !(yr > 0.0f)) {
        return;
    }

    px = std::clamp(px, 0, w - 1);
    py = std::clamp(py, 0, h - 1);
    const float factor = std::clamp(1.0f / scale, 0.2f, 5.0f);
    const float cx = plot_touch_start_x_min_ + static_cast<float>(px) * xr / static_cast<float>(w);
    const float cy = plot_touch_start_y_max_ - static_cast<float>(py) * yr / static_cast<float>(h);
    plot_touch_pending_x_min_ = cx - (cx - plot_touch_start_x_min_) * factor;
    plot_touch_pending_x_max_ = cx + (plot_touch_start_x_max_ - cx) * factor;
    plot_touch_pending_y_min_ = cy - (cy - plot_touch_start_y_min_) * factor;
    plot_touch_pending_y_max_ = cy + (plot_touch_start_y_max_ - cy) * factor;
    plot_touch_pending_ = true;
    showPlotTouchPreview(scale >= 1.0f ? "Zoom in" : "Zoom out");
}

void GraphApp::finishPlotTouchPreview(bool commit)
{
    if (commit && plot_touch_pending_) {
        plot_x_min_ = plot_touch_pending_x_min_;
        plot_x_max_ = plot_touch_pending_x_max_;
        plot_y_min_ = plot_touch_pending_y_min_;
        plot_y_max_ = plot_touch_pending_y_max_;
        markPlotDirty();
    }
    plot_touch_dragging_ = false;
    plot_touch_pending_ = false;
    plot_last_pinch_scale_ = 1.0f;
    if (cursor_info_label_ != nullptr && !cursor_mode_) {
        lv_obj_add_flag(cursor_info_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::showPlotTouchPreview(const char *action)
{
    if (cursor_info_label_ == nullptr) {
        return;
    }
    std::snprintf(cursor_info_buf_.data(),
                  cursor_info_buf_.size(),
                  "%s dx=%d dy=%d\nx %.3g..%.3g  y %.3g..%.3g",
                  action != nullptr ? action : "Touch",
                  plot_touch_dx_,
                  plot_touch_dy_,
                  static_cast<double>(plot_touch_pending_x_min_),
                  static_cast<double>(plot_touch_pending_x_max_),
                  static_cast<double>(plot_touch_pending_y_min_),
                  static_cast<double>(plot_touch_pending_y_max_));
    lv_label_set_text(cursor_info_label_, cursor_info_buf_.data());
    lv_obj_clear_flag(cursor_info_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cursor_info_label_);
}

void GraphApp::onPlotTouchEvent(lv_event_t *e)
{
    auto *self = static_cast<GraphApp *>(lv_event_get_user_data(e));
    if (self == nullptr || self->plot_area_ == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        self->beginPlotTouchPreview();
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        self->finishPlotTouchPreview(code == LV_EVENT_RELEASED);
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == nullptr) {
        return;
    }

#if LV_USE_GESTURE_RECOGNITION
    if (!self->plot_touch_gesture_configured_) {
        lv_indev_set_gesture_min_distance(indev, 8);
        lv_indev_set_gesture_min_velocity(indev, 1);
        lv_indev_set_pinch_up_threshold(indev, 1.08f);
        lv_indev_set_pinch_down_threshold(indev, 0.92f);
        self->plot_touch_gesture_configured_ = true;
    }
#endif

#if LV_USE_GESTURE_RECOGNITION
    if (code == LV_EVENT_GESTURE) {
        const lv_indev_gesture_type_t gesture_type = lv_event_get_gesture_type(e);
        if (gesture_type == LV_INDEV_GESTURE_PINCH) {
            const lv_indev_gesture_state_t state = lv_event_get_gesture_state(e, LV_INDEV_GESTURE_PINCH);
            const float scale = lv_event_get_pinch_scale(e);
            if (state == LV_INDEV_GESTURE_STATE_ENDED) {
                self->finishPlotTouchPreview(true);
                return;
            }
            if (std::isfinite(scale) && scale > 0.01f) {
                lv_point_t point{};
                lv_indev_get_point(indev, &point);
                if (std::fabs(scale - self->plot_last_pinch_scale_) > 0.01f || !self->plot_touch_pending_) {
                    self->updatePlotTouchZoomPreview(scale, point.x, point.y);
                    self->plot_last_pinch_scale_ = scale;
                }
            }
            return;
        }
    }
#endif

    if (code == LV_EVENT_PRESSING && self->plot_touch_dragging_) {
        lv_point_t vect{};
        lv_indev_get_vect(indev, &vect);
        if (vect.x != 0 || vect.y != 0) {
            self->updatePlotTouchPanPreview(vect.x, vect.y);
        }
        return;
    }
}

void GraphApp::startRegionZoom()
{
    region_zoom_active_ = true;
    region_zoom_anchor_set_ = false;
    region_zoom_anchor_sample_ = cursor_sample_;
    cursor_mode_ = true;
}

void GraphApp::finishRegionZoom()
{
    if (!region_zoom_active_) {
        return;
    }

    const int left = std::min(region_zoom_anchor_sample_, cursor_sample_);
    const int right = std::max(region_zoom_anchor_sample_, cursor_sample_);
    if (right <= left) {
        region_zoom_active_ = false;
        region_zoom_anchor_set_ = false;
        return;
    }

    const float x0 = plotXAt(left);
    const float x1 = plotXAt(right);
    const float x_span = std::max(0.0001f, x1 - x0);
    const float y_center = (plot_y_min_ + plot_y_max_) * 0.5f;
    const float y_span = plot_y_max_ - plot_y_min_;

    plot_x_min_ = x0;
    plot_x_max_ = x1;

    if (plot_equal_scale_) {
        const float plot_aspect = static_cast<float>(displayW()) / static_cast<float>(rootH());
        const float half_x = x_span * 0.5f;
        const float new_half_y = half_x / plot_aspect;
        plot_y_min_ = y_center - new_half_y;
        plot_y_max_ = y_center + new_half_y;
    } else {
        const float half_y = y_span * 0.5f;
        plot_y_min_ = y_center - half_y;
        plot_y_max_ = y_center + half_y;
    }

    region_zoom_active_ = false;
    region_zoom_anchor_set_ = false;
    cursor_mode_ = false;
    markPlotDirty();
}

void GraphApp::submitEvaluation(EvalKind kind, int func_index)
{
    if (func_index < 0 || func_index >= kMaxFuncs) return;
    const PlotFunc &func = funcs_[func_index];
    if (!func.enabled || func.expr[0] == '\0') {
        return;
    }

    const float dx_plot = (plot_x_max_ - plot_x_min_) / static_cast<float>(kPlotSamples - 1);
    if (kind == EvalKind::Plot) {
        if (services_.casService().submitSampledReal(func.expr, plot_x_min_, dx_plot, kPlotSamples)) {
            pending_kind_ = kind;
            pending_func_ = func_index;
            pending_generation_ = generation_;
            eval_pending_ = true;
        }
    } else {
        const float span = table_end_ - table_start_;
        const int rows = table_rows_ > 0 ? table_rows_ : 1;
        const float step = (rows > 1) ? span / static_cast<float>(rows - 1) : 0.0f;
        if (services_.casService().submitSampledReal(func.expr, table_start_, step, rows)) {
            pending_kind_ = kind;
            pending_func_ = func_index;
            pending_generation_ = generation_;
            eval_pending_ = true;
        }
    }
}

void GraphApp::scheduleNextEvaluation()
{
    if (eval_pending_ || !ui_ready_) {
        return;
    }

    if (plot_dirty_) {
        for (int i = 0; i < kMaxFuncs; ++i) {
            if (funcs_[i].enabled && !funcs_[i].plot_ready) {
                submitEvaluation(EvalKind::Plot, i);
                return;
            }
        }
        plot_dirty_ = false;
        updatePlotLimits();
        rebuildPlotTickLabels();
        for (int i = 0; i < kMaxFuncs; ++i) {
            if (funcs_[i].plot_ready) {
                rebuildPlotSegments(i);
            }
        }
        updatePlotPage();
    }

    if (table_dirty_) {
        for (int i = 0; i < kMaxFuncs; ++i) {
            if (funcs_[i].enabled && !funcs_[i].table_ready) {
                submitEvaluation(EvalKind::Table, i);
                return;
            }
        }
        table_dirty_ = false;
        updateTablePage();
        if (page_ == Page::Table) {
            beginTableRebuild();
        }
    }

    if (table_rebuild_pending_) {
        // Only touch LVGL table when the table page is visible, and keep each frame tiny.
        if (page_ != Page::Table || menu_open_ || entry_kind_ != EntryKind::None) {
            return;
        }
        constexpr int kCellsPerFrame = 2;
        table_rebuild_pending_ = !rebuildTableChunk(kCellsPerFrame);
    }
}

void GraphApp::onEvaluationSamples(std::vector<float> values)
{
    if (pending_generation_ != generation_) {
        eval_pending_ = false;
        pending_kind_ = EvalKind::None;
        return;
    }

    PlotFunc &func = funcs_[pending_func_];
    if (pending_kind_ == EvalKind::Plot) {
        func.plot_values = std::move(values);
        func.plot_ready = true;
        updatePlotLimits();
        rebuildPlotSegments(pending_func_);
        for (int i = 0; i < kMaxFuncs; ++i) {
            if (funcs_[i].plot_ready) {
                rebuildPlotSegments(i);
            }
        }
        updatePlotPage();
    } else if (pending_kind_ == EvalKind::Table) {
        func.table_values = std::move(values);
        func.table_ready = true;
    }

    eval_pending_ = false;
    pending_kind_ = EvalKind::None;
    pending_func_ = -1;
}

void GraphApp::updatePlotLimits()
{
    if (!(plot_x_max_ > plot_x_min_)) {
        plot_x_min_ = -6.0f;
        plot_x_max_ = 6.0f;
    }
    if (!(plot_y_max_ > plot_y_min_)) {
        plot_y_min_ = -4.0f;
        plot_y_max_ = 4.0f;
    }

    if (plot_equal_scale_) {
        normalizePlotAspect();
    }
}

void GraphApp::rebuildPlotSegments(int func_index)
{
    if (func_index < 0 || func_index >= kMaxFuncs || plot_area_ == nullptr) {
        return;
    }

    PlotFunc &func = funcs_[func_index];
    const std::vector<float> &values = func.plot_values;
    if (!func.enabled || values.size() != static_cast<size_t>(kPlotSamples)) {
        for (int s = 0; s < kMaxPlotSegments; ++s) {
            if (func.segments[s]) lv_obj_add_flag(func.segments[s], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    for (int i = 0; i < kPlotSamples; ++i) {
        func.points[i].x = static_cast<lv_value_precise_t>(xToPlot(plotXAt(i)));
        if (!std::isfinite(values[i])) {
            func.points[i].y = static_cast<lv_value_precise_t>(rootH() + 10);
        } else {
            func.points[i].y = static_cast<lv_value_precise_t>(yToPlot(values[i]));
        }
    }

    int seg = 0;
    int start = -1;
    auto flushSegment = [&](int end_index) {
        if (start < 0 || seg >= kMaxPlotSegments) {
            return;
        }
        const int len = end_index - start;
        if (len >= 1) {
            if (func.segments[seg] == nullptr) {
                func.segments[seg] = createPlotLine(plot_area_, func.color, 2);
                if (func.segments[seg] != nullptr) {
                    lv_obj_set_style_line_rounded(func.segments[seg], true, LV_PART_MAIN);
                    lv_obj_add_flag(func.segments[seg], LV_OBJ_FLAG_HIDDEN);
                }
            }
            if (func.segments[seg] == nullptr) {
                start = -1;
                return;
            }
            lv_line_set_points(func.segments[seg], &func.points[start], len);
            lv_obj_clear_flag(func.segments[seg], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_line_color(func.segments[seg], func.color, LV_PART_MAIN);
            ++seg;
        }
        start = -1;
    };

    for (int i = 0; i < kPlotSamples; ++i) {
        const bool finite = std::isfinite(values[i]);
        bool break_segment = !finite;
        if (finite && i > 0 && std::isfinite(values[i - 1])) {
            const float dy = std::fabs(func.points[i].y - func.points[i - 1].y);
            if (dy > static_cast<float>(rootH()) * 0.72f) {
                break_segment = true;
            }
        }
        if (finite && !break_segment && start < 0) {
            start = i;
        }
        if (break_segment) {
            flushSegment(i);
        }
    }
    flushSegment(kPlotSamples);
    for (int i = seg; i < kMaxPlotSegments; ++i) {
        if (func.segments[i]) lv_obj_add_flag(func.segments[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::rebuildPlotTickLabels()
{
    if (plot_area_ == nullptr) {
        return;
    }

    const float x_step = niceStep(plot_x_max_ - plot_x_min_, kMaxTicks);
    const float y_step = niceStep(plot_y_max_ - plot_y_min_, kMaxTicks);

    int axis_y = (plot_y_min_ < 0.0f && plot_y_max_ > 0.0f) ? yToPlot(0.0f) : (rootH() - 1);
    int axis_x = (plot_x_min_ < 0.0f && plot_x_max_ > 0.0f) ? xToPlot(0.0f) : 0;

    axis_x_pts_[0] = {0, static_cast<lv_value_precise_t>(axis_y)};
    axis_x_pts_[1] = {static_cast<lv_value_precise_t>(displayW() - 1), static_cast<lv_value_precise_t>(axis_y)};
    axis_y_pts_[0] = {static_cast<lv_value_precise_t>(axis_x), 0};
    axis_y_pts_[1] = {static_cast<lv_value_precise_t>(axis_x), static_cast<lv_value_precise_t>(rootH() - 1)};
    if (axis_x_) lv_line_set_points(axis_x_, axis_x_pts_.data(), 2);
    if (axis_y_) lv_line_set_points(axis_y_, axis_y_pts_.data(), 2);

    auto pickTickValues = [&](float min_v, float max_v, float step) {
        std::vector<float> values;
        if (!(step > 0.0f) || !(max_v >= min_v)) {
            return values;
        }
        const float start = std::ceil(min_v / step) * step;
        if (start > max_v) {
            return values;
        }

        int total = static_cast<int>(std::floor((max_v - start) / step)) + 1;
        if (total <= 0) {
            return values;
        }

        if (total <= kMaxTicks) {
            values.reserve(static_cast<size_t>(total));
            for (int i = 0; i < total; ++i) {
                values.push_back(start + static_cast<float>(i) * step);
            }
            return values;
        }

        values.reserve(kMaxTicks);
        float last = std::numeric_limits<float>::quiet_NaN();
        for (int i = 0; i < kMaxTicks; ++i) {
            const float t = (kMaxTicks > 1)
                                ? static_cast<float>(i) / static_cast<float>(kMaxTicks - 1)
                                : 0.0f;
            const int sample_idx = static_cast<int>(std::round(t * static_cast<float>(total - 1)));
            const float value = start + static_cast<float>(sample_idx) * step;
            if (values.empty() || std::fabs(value - last) > step * 0.25f) {
                values.push_back(value);
                last = value;
            }
        }
        return values;
    };

    const auto x_ticks = pickTickValues(plot_x_min_, plot_x_max_, x_step);
    int idx = 0;
    for (float x : x_ticks) {
        if (idx >= kMaxTicks) {
            break;
        }
        int px = xToPlot(x);
        const int tick_top = clampCoord(axis_y - 3, rootH() - 1);
        const int tick_bottom = clampCoord(axis_y + 3, rootH() - 1);
        tick_pts_x_[idx][0] = {static_cast<lv_value_precise_t>(px), static_cast<lv_value_precise_t>(tick_top)};
        tick_pts_x_[idx][1] = {static_cast<lv_value_precise_t>(px), static_cast<lv_value_precise_t>(tick_bottom)};
        if (x_tick_lines_[idx]) {
            lv_line_set_points(x_tick_lines_[idx], tick_pts_x_[idx].data(), 2);
            lv_obj_clear_flag(x_tick_lines_[idx], LV_OBJ_FLAG_HIDDEN);
        }
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(x));
        if (x_tick_labels_[idx]) {
            lv_label_set_text(x_tick_labels_[idx], buf);
            const lv_point_t label_size = measureLabel(x_tick_labels_[idx], buf);
            const int label_x = clampCoord(px - static_cast<int>(label_size.x) / 2, displayW() - static_cast<int>(label_size.x));
            const int below_y = axis_y + 5;
            const int above_y = axis_y - static_cast<int>(label_size.y) - 5;
            const int label_y = clampCoord((below_y + label_size.y <= rootH()) ? below_y : above_y,
                                           rootH() - static_cast<int>(label_size.y));
            lv_obj_set_pos(x_tick_labels_[idx], label_x, label_y);
            lv_obj_clear_flag(x_tick_labels_[idx], LV_OBJ_FLAG_HIDDEN);
        }
        ++idx;
    }
    for (; idx < kMaxTicks; ++idx) {
        if (x_tick_lines_[idx]) lv_obj_add_flag(x_tick_lines_[idx], LV_OBJ_FLAG_HIDDEN);
        if (x_tick_labels_[idx]) lv_obj_add_flag(x_tick_labels_[idx], LV_OBJ_FLAG_HIDDEN);
    }

    const auto y_ticks = pickTickValues(plot_y_min_, plot_y_max_, y_step);
    idx = 0;
    for (float y : y_ticks) {
        if (idx >= kMaxTicks) {
            break;
        }
        int py = yToPlot(y);
        const int tick_left = clampCoord(axis_x - 3, displayW() - 1);
        const int tick_right = clampCoord(axis_x + 3, displayW() - 1);
        tick_pts_y_[idx][0] = {static_cast<lv_value_precise_t>(tick_left), static_cast<lv_value_precise_t>(py)};
        tick_pts_y_[idx][1] = {static_cast<lv_value_precise_t>(tick_right), static_cast<lv_value_precise_t>(py)};
        if (y_tick_lines_[idx]) {
            lv_line_set_points(y_tick_lines_[idx], tick_pts_y_[idx].data(), 2);
            lv_obj_clear_flag(y_tick_lines_[idx], LV_OBJ_FLAG_HIDDEN);
        }
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(y));
        if (y_tick_labels_[idx]) {
            lv_label_set_text(y_tick_labels_[idx], buf);
            const lv_point_t label_size = measureLabel(y_tick_labels_[idx], buf);
            const int right_x = axis_x + 5;
            const int left_x = axis_x - static_cast<int>(label_size.x) - 5;
            const int label_x = clampCoord((right_x + label_size.x <= displayW()) ? right_x : left_x,
                                           displayW() - static_cast<int>(label_size.x));
            const int label_y = clampCoord(py - static_cast<int>(label_size.y) / 2,
                                           rootH() - static_cast<int>(label_size.y));
            lv_obj_set_pos(y_tick_labels_[idx], label_x, label_y);
            lv_obj_clear_flag(y_tick_labels_[idx], LV_OBJ_FLAG_HIDDEN);
        }
        ++idx;
    }
    for (; idx < kMaxTicks; ++idx) {
        if (y_tick_lines_[idx]) lv_obj_add_flag(y_tick_lines_[idx], LV_OBJ_FLAG_HIDDEN);
        if (y_tick_labels_[idx]) lv_obj_add_flag(y_tick_labels_[idx], LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::beginTableRebuild()
{
    if (table_obj_ == nullptr) {
        return;
    }

    if (table_rows_ <= 0) {
        table_rows_ = std::max(1, static_cast<int>(std::floor((table_end_ - table_start_) / table_step_)) + 1);
    }
    if (table_rows_ > kMaxTableRows) {
        table_rows_ = kMaxTableRows;
    }

    lv_table_set_row_cnt(table_obj_, static_cast<uint16_t>(table_rows_ + 1));
    lv_table_set_col_cnt(table_obj_, static_cast<uint16_t>(kMaxFuncs + 1));

    lv_table_set_col_width(table_obj_, 0, 52);
    for (int i = 0; i < kMaxFuncs; ++i) {
        lv_table_set_col_width(table_obj_, static_cast<uint16_t>(i + 1), 68);
    }

    lv_table_set_cell_value(table_obj_, 0, 0, "x");
    for (int i = 0; i < kMaxFuncs; ++i) {
        char header[12];
        std::snprintf(header, sizeof(header), "f%d", i + 1);
        lv_table_set_cell_value(table_obj_, 0, static_cast<uint16_t>(i + 1), header);
    }

    table_rebuild_row_ = 0;
    table_rebuild_col_ = 0;
    table_rebuild_pending_ = true;
}

bool GraphApp::rebuildTableChunk(int max_cells)
{
    if (table_obj_ == nullptr) {
        return true;
    }
    if (max_cells <= 0) {
        max_cells = 1;
    }

    const uint64_t start_us = static_cast<uint64_t>(esp_timer_get_time());
    constexpr uint64_t kBudgetUs = 1200;

    int written = 0;
    while (table_rebuild_row_ < table_rows_ && written < max_cells) {
        const int row = table_rebuild_row_;
        char cell[32];
        const int col = table_rebuild_col_;
        if (col == 0) {
            const float x = (table_rows_ <= 1)
                                ? table_start_
                                : (table_start_ + static_cast<float>(row) * (table_end_ - table_start_) /
                                                      static_cast<float>(table_rows_ - 1));
            std::snprintf(cell, sizeof(cell), "%.6g", static_cast<double>(x));
            lv_table_set_cell_value(table_obj_, static_cast<uint16_t>(row + 1), 0, cell);
        } else {
            const int func_index = col - 1;
            if (func_index >= 0 && func_index < kMaxFuncs) {
                if (!funcs_[func_index].enabled) {
                    lv_table_set_cell_value(table_obj_, static_cast<uint16_t>(row + 1), static_cast<uint16_t>(col), "--");
                } else {
                    const auto &values = funcs_[func_index].table_values;
                    const char *text = "undef";
                    if (row < static_cast<int>(values.size()) && std::isfinite(values[row])) {
                        std::snprintf(cell, sizeof(cell), "%.6g", static_cast<double>(values[row]));
                        text = cell;
                    } else if (row < static_cast<int>(values.size()) && std::isinf(values[row])) {
                        text = values[row] > 0 ? "inf" : "-inf";
                    }
                    lv_table_set_cell_value(table_obj_, static_cast<uint16_t>(row + 1), static_cast<uint16_t>(col), text);
                }
            }
        }

        ++written;
        ++table_rebuild_col_;
        if (table_rebuild_col_ > kMaxFuncs) {
            table_rebuild_col_ = 0;
            ++table_rebuild_row_;
        }

        if ((static_cast<uint64_t>(esp_timer_get_time()) - start_us) >= kBudgetUs) {
            break;
        }
    }

    return table_rebuild_row_ >= table_rows_;
}

void GraphApp::updateInputPage()
{
    if (input_list_ == nullptr) return;
    for (int i = 0; i < kMaxFuncs; ++i) {
        if (funcs_[i].checkbox) {
            if (funcs_[i].enabled) lv_obj_add_state(funcs_[i].checkbox, LV_STATE_CHECKED);
            else lv_obj_clear_state(funcs_[i].checkbox, LV_STATE_CHECKED);
        }
        if (funcs_[i].expr_label) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "f%d(x) = %s", i + 1, funcs_[i].expr[0] ? funcs_[i].expr : "<empty>");
            lv_label_set_text(funcs_[i].expr_label, buf);
        }
        if (funcs_[i].color_label) {
            lv_obj_set_style_text_color(funcs_[i].color_label, funcs_[i].color, LV_PART_MAIN);
        }
        if (input_rows_[i]) {
            if (i == selected_func_) {
                lv_obj_set_style_bg_opa(input_rows_[i], LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_bg_color(input_rows_[i], LV_COLOR_MAKE(26, 34, 52), LV_PART_MAIN);
                lv_obj_set_style_border_color(input_rows_[i], LV_COLOR_MAKE(110, 140, 190), LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_opa(input_rows_[i], LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_color(input_rows_[i], LV_COLOR_MAKE(40, 55, 78), LV_PART_MAIN);
            }
        }
    }
}

void GraphApp::updatePlotPage()
{
}

void GraphApp::buildMenuItems()
{
    menu_items_.clear();
    switch (menu_kind_) {
    case MenuKind::PageInput:
        menu_items_.push_back({"Plot page", MenuKind::PageInput, 0});
        menu_items_.push_back({"Table page", MenuKind::PageInput, 1});
        menu_items_.push_back({"Edit function", MenuKind::PageInput, 2});
        menu_items_.push_back({"Toggle function", MenuKind::PageInput, 3});
        menu_items_.push_back({"Cycle color", MenuKind::PageInput, 4});
        menu_items_.push_back({"Reset function", MenuKind::PageInput, 5});
        break;
    case MenuKind::PagePlot:
        menu_items_.push_back({"Input page", MenuKind::PagePlot, 0});
        menu_items_.push_back({"Table page", MenuKind::PagePlot, 1});
        menu_items_.push_back({"Cursor on/off", MenuKind::PagePlot, 2});
        menu_items_.push_back({"Reset view", MenuKind::PagePlot, 3});
        menu_items_.push_back({"Zoom in", MenuKind::PagePlot, 4});
        menu_items_.push_back({"Zoom out", MenuKind::PagePlot, 5});
        menu_items_.push_back({"Scale x", MenuKind::PagePlot, 6});
        menu_items_.push_back({"Scale y", MenuKind::PagePlot, 7});
        menu_items_.push_back({"Scale factor", MenuKind::PagePlot, 8});
        menu_items_.push_back({"Region zoom", MenuKind::PagePlot, 9});
        menu_items_.push_back({"Equal scale on/off", MenuKind::PagePlot, 10});
        break;
    case MenuKind::PageTable:
        menu_items_.push_back({"Input page", MenuKind::PageTable, 0});
        menu_items_.push_back({"Plot page", MenuKind::PageTable, 1});
        menu_items_.push_back({"Range start", MenuKind::PageTable, 2});
        menu_items_.push_back({"Range end", MenuKind::PageTable, 3});
        menu_items_.push_back({"Step", MenuKind::PageTable, 4});
        menu_items_.push_back({"Rebuild table", MenuKind::PageTable, 5});
        break;
    default:
        break;
    }
}

void GraphApp::onMenuButtonEvent(lv_event_t *e)
{
    auto *self = static_cast<GraphApp *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    lv_obj_t *target = lv_event_get_current_target_obj(e);
    if (target == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    const intptr_t index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (index < 0 || index >= static_cast<intptr_t>(self->menu_items_.size())) {
        return;
    }

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_set_style_bg_color(target, LV_COLOR_MAKE(92, 116, 172), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(target, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(target, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
        return;
    }

    if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(target, LV_COLOR_MAKE(220, 230, 245), LV_PART_MAIN);
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        self->activateMenuItem(static_cast<int>(index));
        return;
    }

    if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) {
            self->activateMenuItem(static_cast<int>(index));
        } else if (key == LV_KEY_ESC) {
            self->closeMenu();
        }
    }
}

void GraphApp::updateTablePage()
{
}

void GraphApp::updateMenuOverlay()
{
    if (menu_overlay_ == nullptr || menu_list_ == nullptr || menu_page_ == nullptr) return;

    buildMenuItems();
    lv_obj_clean(menu_page_);
    menu_rows_.clear();

    for (size_t i = 0; i < menu_items_.size(); ++i) {
        const MenuItem &item = menu_items_[i];
        lv_obj_t *btn = lv_menu_cont_create(menu_page_);
        if (btn == nullptr) {
            continue;
        }
        menu_rows_.push_back(btn);
        const bool touch = services_.board().hasTouchInput();
        lv_obj_set_width(btn, touch ? 380 : 206);
        lv_obj_set_height(btn, touch ? 48 : 22);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, item.label);
        lv_obj_set_width(label, touch ? 360 : 198);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        ui_theme::applyText14(btn);
        ui_theme::applyText14(label);
        lv_obj_set_style_text_color(btn, LV_COLOR_MAKE(220, 230, 245), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, LV_COLOR_MAKE(220, 230, 245), LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, touch ? 12 : 4, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, touch ? 12 : 4, LV_PART_MAIN);
        lv_obj_set_style_pad_top(btn, touch ? 8 : 1, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(btn, touch ? 8 : 1, LV_PART_MAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(btn, &GraphApp::onMenuButtonEvent, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(btn, &GraphApp::onMenuButtonEvent, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(btn, &GraphApp::onMenuButtonEvent, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, &GraphApp::onMenuButtonEvent, LV_EVENT_KEY, this);
        if (menu_group_ != nullptr) {
            lv_group_add_obj(menu_group_, btn);
        }
    }

    if (menu_group_ != nullptr && !menu_rows_.empty()) {
        lv_group_focus_obj(menu_rows_.front());
    }

}

void GraphApp::activateMenuItem(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= menu_items_.size()) {
        return;
    }

    const MenuItem item = menu_items_[static_cast<size_t>(index)];
    if (item.kind == MenuKind::PageInput) {
        switch (item.value) {
        case 0: showPage(Page::Plot); break;
        case 1: showPage(Page::Table); break;
        case 2: startEntry(EntryKind::FunctionExpr, selected_func_); break;
        case 3: toggleFunction(selected_func_); break;
        case 4: cycleFunctionColor(selected_func_); break;
        case 5: startEntry(EntryKind::FunctionExpr, selected_func_); break;
        }
    } else if (item.kind == MenuKind::PagePlot) {
        switch (item.value) {
        case 0: showPage(Page::Input); break;
        case 1: showPage(Page::Table); break;
        case 2: cursor_mode_ = !cursor_mode_; break;
        case 3: resetPlotView(); break;
        case 4: applyUniformScale(0.8f); break;
        case 5: applyUniformScale(1.25f); break;
        case 6: startEntry(EntryKind::ScaleXFactor); break;
        case 7: startEntry(EntryKind::ScaleYFactor); break;
        case 8: startEntry(EntryKind::ScaleFactor); break;
        case 9: startRegionZoom(); break;
        case 10:
            plot_equal_scale_ = !plot_equal_scale_;
            if (plot_equal_scale_) {
                normalizePlotAspect();
            }
            markPlotDirty();
            break;
        }
    } else if (item.kind == MenuKind::PageTable) {
        switch (item.value) {
        case 0: showPage(Page::Input); break;
        case 1: showPage(Page::Plot); break;
        case 2: startEntry(EntryKind::TableStart); break;
        case 3: startEntry(EntryKind::TableEnd); break;
        case 4: startEntry(EntryKind::TableStep); break;
        case 5: markTableDirty(); break;
        }
    }

    closeMenu();
}

void GraphApp::updateEntryOverlay()
{
    if (entry_overlay_ == nullptr || entry_title_ == nullptr || entry_box_ == nullptr) return;
    lv_label_set_text(entry_title_, entry_title_buf_.data());
    lv_textarea_set_text(entry_box_, entry_buffer_);
    lv_textarea_set_cursor_pos(entry_box_, LV_TEXTAREA_CURSOR_LAST);
}

void GraphApp::openMenu(MenuKind kind)
{
    menu_kind_ = kind;
    menu_open_ = true;
    if (menu_group_ != nullptr) {
        lv_group_delete(menu_group_);
        menu_group_ = nullptr;
    }
    menu_group_ = lv_group_create();
    if (menu_group_ != nullptr) {
        lv_group_set_editing(menu_group_, false);
    }
    if (menu_overlay_) {
        updateMenuOverlay();
        lv_obj_clear_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(menu_overlay_);
    }
}

void GraphApp::closeMenu()
{
    menu_open_ = false;
    if (menu_group_ != nullptr) {
        lv_group_delete(menu_group_);
        menu_group_ = nullptr;
    }
    if (menu_overlay_) {
        lv_obj_add_flag(menu_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GraphApp::startEntry(EntryKind kind, int func_index)
{
    entry_kind_ = kind;
    entry_func_index_ = func_index;
    entry_length_ = 0;
    entry_shift_lock_ = false;
    entry_buffer_[0] = '\0';

    switch (kind) {
    case EntryKind::FunctionExpr:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Expr f%d", func_index + 1);
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "%s", funcs_[func_index].expr);
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "%s", funcs_[func_index].expr);
        break;
    case EntryKind::TableStart:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Table start");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "%.6g", static_cast<double>(table_start_));
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "%.6g", static_cast<double>(table_start_));
        break;
    case EntryKind::TableEnd:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Table end");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "%.6g", static_cast<double>(table_end_));
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "%.6g", static_cast<double>(table_end_));
        break;
    case EntryKind::TableStep:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Table step");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "%.6g", static_cast<double>(table_step_));
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "%.6g", static_cast<double>(table_step_));
        break;
    case EntryKind::ScaleFactor:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Scale factor");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "1.25");
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "1.25");
        break;
    case EntryKind::ScaleXFactor:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Scale x factor");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "1.25");
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "1.25");
        break;
    case EntryKind::ScaleYFactor:
        std::snprintf(entry_title_buf_.data(), entry_title_buf_.size(), "Scale y factor");
        std::snprintf(entry_prev_buf_.data(), entry_prev_buf_.size(), "1.25");
        std::snprintf(entry_buffer_, sizeof(entry_buffer_), "1.25");
        break;
    default:
        return;
    }

    entry_length_ = static_cast<int>(std::strlen(entry_buffer_));

    updateEntryOverlay();
    if (entry_overlay_) {
        lv_obj_clear_flag(entry_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(entry_overlay_);
    }
}

void GraphApp::finishEntry(bool confirm)
{
    if (confirm && entry_kind_ != EntryKind::None) {
        const char *text = entry_box_ != nullptr ? lv_textarea_get_text(entry_box_) : entry_buffer_;
        if (text == nullptr) {
            text = "";
        }
        if (entry_kind_ == EntryKind::FunctionExpr && entry_func_index_ >= 0 && entry_func_index_ < kMaxFuncs) {
            std::strncpy(funcs_[entry_func_index_].expr, text, kMaxExprLen - 1);
            funcs_[entry_func_index_].expr[kMaxExprLen - 1] = '\0';
            funcs_[entry_func_index_].enabled = true;
            funcs_[entry_func_index_].plot_ready = false;
            funcs_[entry_func_index_].table_ready = false;
            markPlotDirty();
            markTableDirty();
        } else if (entry_kind_ == EntryKind::TableStart) {
            table_start_ = parseEntryValue(text, table_start_);
            table_dirty_ = true;
        } else if (entry_kind_ == EntryKind::TableEnd) {
            table_end_ = parseEntryValue(text, table_end_);
            table_dirty_ = true;
        } else if (entry_kind_ == EntryKind::TableStep) {
            table_step_ = std::max(0.0001f, parseEntryValue(text, table_step_));
            table_dirty_ = true;
        } else if (entry_kind_ == EntryKind::ScaleFactor) {
            applyUniformScale(std::max(0.0001f, parseEntryValue(text, 1.25f)));
        } else if (entry_kind_ == EntryKind::ScaleXFactor) {
            applyAxisScale(std::max(0.0001f, parseEntryValue(text, 1.25f)), true);
        } else if (entry_kind_ == EntryKind::ScaleYFactor) {
            applyAxisScale(std::max(0.0001f, parseEntryValue(text, 1.25f)), false);
        }
    }

    entry_kind_ = EntryKind::None;
    entry_func_index_ = -1;
    entry_length_ = 0;
    entry_buffer_[0] = '\0';
    if (entry_overlay_) lv_obj_add_flag(entry_overlay_, LV_OBJ_FLAG_HIDDEN);
    if (menu_overlay_) lv_obj_move_foreground(root_);
}

void GraphApp::toggleFunction(int index)
{
    if (index < 0 || index >= kMaxFuncs) return;
    funcs_[index].enabled = !funcs_[index].enabled;
    if (!funcs_[index].enabled) {
        funcs_[index].plot_ready = false;
        funcs_[index].table_ready = false;
        for (int s = 0; s < kMaxPlotSegments; ++s) {
            if (funcs_[index].segments[s]) lv_obj_add_flag(funcs_[index].segments[s], LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        markPlotDirty();
        markTableDirty();
    }
    updateInputPage();
}

void GraphApp::cycleFunctionColor(int index)
{
    if (index < 0 || index >= kMaxFuncs) return;
    static int palette_index[kMaxFuncs] = {0, 1, 2, 3};
    palette_index[index] = (palette_index[index] + 1) % kFuncColorsCount;
    funcs_[index].color = kFuncColors[palette_index[index]];
    if (funcs_[index].color_label) {
        lv_obj_set_style_text_color(funcs_[index].color_label, funcs_[index].color, LV_PART_MAIN);
    }
    markPlotDirty();
}

void GraphApp::selectFunction(int index)
{
    if (index < 0) index = 0;
    if (index >= kMaxFuncs) index = kMaxFuncs - 1;
    selected_func_ = index;
    updateInputPage();
}

void GraphApp::buildInputPage()
{
    updateInputPage();
}

void GraphApp::buildPlotPage()
{
    updatePlotPage();
    rebuildPlotTickLabels();
}

void GraphApp::buildTablePage()
{
    updateTablePage();
}

void GraphApp::handleEntryMappedKey(uint32_t key)
{
    if (entry_kind_ == EntryKind::None) return;

    if (key == LV_KEY_ENTER) {
        finishEntry(true);
        return;
    }
    if (key == LV_KEY_ESC) {
        finishEntry(false);
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (entry_length_ > 0) entry_buffer_[--entry_length_] = '\0';
        updateEntryOverlay();
        return;
    }
    if (key >= 32U && key <= 126U && entry_length_ < static_cast<int>(sizeof(entry_buffer_)) - 1) {
        entry_buffer_[entry_length_++] = static_cast<char>(key);
        entry_buffer_[entry_length_] = '\0';
        updateEntryOverlay();
    }
}

void GraphApp::handleInputPageMappedKey(uint32_t key)
{
    if (key == LV_KEY_UP && selected_func_ > 0) {
        selectFunction(selected_func_ - 1);
    } else if (key == LV_KEY_DOWN && selected_func_ + 1 < kMaxFuncs) {
        selectFunction(selected_func_ + 1);
    } else if (key == ' ') {
        toggleFunction(selected_func_);
    } else if (key == LV_KEY_ENTER) {
        startEntry(EntryKind::FunctionExpr, selected_func_);
    } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
        cycleFunctionColor(selected_func_);
    } else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        showPage(Page::Plot);
    }
}

void GraphApp::handlePlotPageMappedKey(uint32_t key)
{
    if (region_zoom_active_) {
        if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            region_zoom_active_ = false;
            region_zoom_anchor_set_ = false;
            return;
        }
        if (key == LV_KEY_ENTER) {
            if (!region_zoom_anchor_set_) {
                region_zoom_anchor_sample_ = cursor_sample_;
                region_zoom_anchor_set_ = true;
            } else {
                finishRegionZoom();
            }
            return;
        }
    }

    if (key == LV_KEY_ENTER) {
        cursor_mode_ = !cursor_mode_;
        return;
    }

    if (!cursor_mode_) {
        const float xr = plot_x_max_ - plot_x_min_;
        const float yr = plot_y_max_ - plot_y_min_;
        if (key == LV_KEY_LEFT) { plot_x_min_ -= xr * 0.12f; plot_x_max_ -= xr * 0.12f; markPlotDirty(); }
        else if (key == LV_KEY_RIGHT) { plot_x_min_ += xr * 0.12f; plot_x_max_ += xr * 0.12f; markPlotDirty(); }
        else if (key == LV_KEY_UP) { const float cx = (plot_x_min_ + plot_x_max_) * 0.5f; const float cy = (plot_y_min_ + plot_y_max_) * 0.5f; plot_x_min_ = cx - xr * 0.35f; plot_x_max_ = cx + xr * 0.35f; plot_y_min_ = cy - yr * 0.35f; plot_y_max_ = cy + yr * 0.35f; markPlotDirty(); }
        else if (key == LV_KEY_DOWN) { const float cx = (plot_x_min_ + plot_x_max_) * 0.5f; const float cy = (plot_y_min_ + plot_y_max_) * 0.5f; plot_x_min_ = cx - xr * 0.75f; plot_x_max_ = cx + xr * 0.75f; plot_y_min_ = cy - yr * 0.75f; plot_y_max_ = cy + yr * 0.75f; markPlotDirty(); }
        else if (key == 'r' || key == 'R') { resetPlotView(); }
        return;
    }

    if (key == LV_KEY_LEFT && cursor_sample_ > 0) --cursor_sample_;
    else if (key == LV_KEY_RIGHT && cursor_sample_ + 1 < kPlotSamples) ++cursor_sample_;
    else if (key == LV_KEY_UP) {
        for (int step = 1; step <= kMaxFuncs; ++step) {
            const int idx = (active_plot_func_ + kMaxFuncs - step) % kMaxFuncs;
            if (funcs_[idx].enabled) { active_plot_func_ = idx; break; }
        }
    } else if (key == LV_KEY_DOWN) {
        for (int step = 1; step <= kMaxFuncs; ++step) {
            const int idx = (active_plot_func_ + step) % kMaxFuncs;
            if (funcs_[idx].enabled) { active_plot_func_ = idx; break; }
        }
    } else if ((key == 'z' || key == 'Z') && funcs_[active_plot_func_].enabled) {
        char expr[256];
        std::snprintf(expr, sizeof(expr), "solve(%s=0,x)", funcs_[active_plot_func_].expr);
        if (services_.casService().submit(expr)) pending_kind_ = EvalKind::None;
    } else if ((key == 'd' || key == 'D') && funcs_[active_plot_func_].enabled) {
        char expr[256];
        const float x0 = plotXAt(cursor_sample_);
        std::snprintf(expr, sizeof(expr), "evalf(subst(diff(%s,x),x,%.8f))", funcs_[active_plot_func_].expr, static_cast<double>(x0));
        if (services_.casService().submit(expr)) pending_kind_ = EvalKind::None;
    } else if ((key == 'x' || key == 'X') && funcs_[active_plot_func_].enabled) {
        for (int step = 1; step <= kMaxFuncs; ++step) {
            const int idx = (active_plot_func_ + step) % kMaxFuncs;
            if (funcs_[idx].enabled) {
                char expr[256];
                std::snprintf(expr, sizeof(expr), "solve(%s=%s,x)", funcs_[active_plot_func_].expr, funcs_[idx].expr);
                services_.casService().submit(expr);
                break;
            }
        }
    }
}

void GraphApp::handleTablePageMappedKey(uint32_t key)
{
    if (table_obj_ == nullptr) return;
    if (key == LV_KEY_LEFT) {
        lv_obj_scroll_by_bounded(table_obj_, -34, 0, LV_ANIM_ON);
    } else if (key == LV_KEY_RIGHT) {
        lv_obj_scroll_by_bounded(table_obj_, 34, 0, LV_ANIM_ON);
    } else if (key == LV_KEY_UP) {
        lv_obj_scroll_by_bounded(table_obj_, 0, -18, LV_ANIM_ON);
    } else if (key == LV_KEY_DOWN) {
        lv_obj_scroll_by_bounded(table_obj_, 0, 18, LV_ANIM_ON);
    }
}

void GraphApp::openPageMenu()
{
    switch (page_) {
    case Page::Input:
        openMenu(MenuKind::PageInput);
        break;
    case Page::Plot:
        openMenu(MenuKind::PagePlot);
        break;
    case Page::Table:
        openMenu(MenuKind::PageTable);
        break;
    }
}

void GraphApp::handleMappedKey(uint32_t key)
{
    ensureUi();
    if (key == '\t') {
        if (entry_kind_ == EntryKind::None && !menu_open_) {
            if (page_ == Page::Input) showPage(Page::Plot);
            else if (page_ == Page::Plot) showPage(Page::Table);
            else showPage(Page::Input);
        }
        return;
    }

    if (entry_kind_ != EntryKind::None) {
        handleEntryMappedKey(key);
        return;
    }

    if (menu_open_) {
        if (menu_group_ == nullptr) return;
        if (key == LV_KEY_UP || key == LV_KEY_LEFT) lv_group_focus_prev(menu_group_);
        else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) lv_group_focus_next(menu_group_);
        else if (key == LV_KEY_ENTER) lv_group_send_data(menu_group_, key);
        else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) closeMenu();
        else lv_group_send_data(menu_group_, key);
        return;
    }

    if (key == LV_KEY_ESC) {
        openPageMenu();
        return;
    }

    switch (page_) {
    case Page::Input: handleInputPageMappedKey(key); break;
    case Page::Plot: handlePlotPageMappedKey(key); break;
    case Page::Table: handleTablePageMappedKey(key); break;
    }
}

void GraphApp::render()
{
    ensureUi();
    if (!ui_ready_) return;

    std::vector<float> sampled;
    if (services_.casService().pollSampledResult(sampled)) {
        onEvaluationSamples(std::move(sampled));
    }

    scheduleNextEvaluation();
    if (page_ == Page::Plot && cursor_mode_ && cursor_line_) {
        const float px = static_cast<float>(cursor_sample_) * static_cast<float>(displayW() - 1) / static_cast<float>(kPlotSamples - 1);
        const float x = plotXAt(cursor_sample_);
        const float y = cursorYValue();
        cursor_pts_[0] = {static_cast<lv_value_precise_t>(px), 0};
        cursor_pts_[1] = {static_cast<lv_value_precise_t>(px), static_cast<lv_value_precise_t>(rootH() - 1)};
        lv_line_set_points(cursor_line_, cursor_pts_.data(), 2);
        lv_obj_clear_flag(cursor_line_, LV_OBJ_FLAG_HIDDEN);
        if (cursor_h_line_ != nullptr && std::isfinite(y)) {
            const int py = yToPlot(y);
            cursor_h_pts_[0] = {0, static_cast<lv_value_precise_t>(py)};
            cursor_h_pts_[1] = {static_cast<lv_value_precise_t>(displayW() - 1), static_cast<lv_value_precise_t>(py)};
            lv_line_set_points(cursor_h_line_, cursor_h_pts_.data(), 2);
            lv_obj_clear_flag(cursor_h_line_, LV_OBJ_FLAG_HIDDEN);
        } else if (cursor_h_line_ != nullptr) {
            lv_obj_add_flag(cursor_h_line_, LV_OBJ_FLAG_HIDDEN);
        }
        if (cursor_info_label_ != nullptr) {
            if (std::isfinite(y)) {
                std::snprintf(cursor_info_buf_.data(), cursor_info_buf_.size(), "f%d  x=%.3g  f(x)=%.3g", active_plot_func_ + 1, static_cast<double>(x), static_cast<double>(y));
            } else {
                std::snprintf(cursor_info_buf_.data(), cursor_info_buf_.size(), "f%d  x=%.3g  f(x)=nan", active_plot_func_ + 1, static_cast<double>(x));
            }
            lv_label_set_text(cursor_info_label_, cursor_info_buf_.data());
            lv_obj_clear_flag(cursor_info_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(cursor_info_label_);
        }
    } else if (cursor_line_) {
        lv_obj_add_flag(cursor_line_, LV_OBJ_FLAG_HIDDEN);
        if (cursor_h_line_) lv_obj_add_flag(cursor_h_line_, LV_OBJ_FLAG_HIDDEN);
        if (cursor_info_label_ && !plot_touch_dragging_) lv_obj_add_flag(cursor_info_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace brookesia
