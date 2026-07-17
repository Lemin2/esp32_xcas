#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "brookesia/core/app.hpp"
#include "brookesia/core/service_hub.hpp"

namespace brookesia {

class GraphApp final : public App {
public:
    explicit GraphApp(ServiceHub &services);

    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void handleMappedKey(uint32_t key) override;
    void render() override;

private:
    static constexpr int kDisplayW = 240;
    static constexpr int kDisplayH = 135;
    static constexpr int kStatusH = 16;
    static constexpr int kRootH = kDisplayH - kStatusH;

    static constexpr int kMaxFuncs = 4;
    static constexpr int kMaxExprLen = 96;
    static constexpr int kPlotSamples = 220;
    static constexpr int kMaxPlotSegments = 8;
    static constexpr int kMaxTicks = 5;
    static constexpr int kMaxTableRows = 128;

    enum class Page { Input, Plot, Table };
    enum class MenuKind { None, PageInput, PagePlot, PageTable };
    enum class EntryKind {
        None,
        FunctionExpr,
        TableStart,
        TableEnd,
        TableStep,
        ScaleFactor,
        ScaleXFactor,
        ScaleYFactor,
    };
    enum class EvalKind { None, Plot, Table };

    struct PlotFunc {
        char expr[kMaxExprLen] = {};
        bool enabled = false;
        lv_color_t color = {};
        std::vector<float> plot_values;
        std::vector<float> table_values;
        lv_obj_t *checkbox = nullptr;
        lv_obj_t *expr_label = nullptr;
        lv_obj_t *color_label = nullptr;
        lv_obj_t *segments[kMaxPlotSegments] = {};
        std::array<lv_point_precise_t, kPlotSamples> points = {};
        bool plot_ready = false;
        bool table_ready = false;
    };

    struct MenuItem {
        const char *label = nullptr;
        MenuKind kind = MenuKind::None;
        int value = 0;
    };

    void ensureUi();
    void loadSession();
    void saveSession() const;
    void showPage(Page page);
    void refreshPageVisibility();
    void buildInputPage();
    void buildPlotPage();
    void buildTablePage();
    void buildMenuOverlay();
    void buildEntryOverlay();

    void addDefaultFunctions();
    void markPlotDirty();
    void markTableDirty();
    void resetPlotView();
    void normalizePlotAspect();
    void applyUniformScale(float factor);
    void applyAxisScale(float factor, bool scale_x);
    void startRegionZoom();
    void finishRegionZoom();
    void scheduleNextEvaluation();
    void submitEvaluation(EvalKind kind, int func_index);
    void onEvaluationSamples(std::vector<float> values);
    void updatePlotLimits();
    void rebuildPlotSegments(int func_index);
    void rebuildPlotTickLabels();
    void beginTableRebuild();
    bool rebuildTableChunk(int max_rows);
    void updateInputPage();
    void updatePlotPage();
    void updateTablePage();
    void updateMenuOverlay();
    void updateEntryOverlay();
    void activateMenuItem(int index);
    void buildMenuItems();
    static void onMenuButtonEvent(lv_event_t *e);

    void openMenu(MenuKind kind);
    void closeMenu();
    void startEntry(EntryKind kind, int func_index = -1);
    void finishEntry(bool confirm);

    void toggleFunction(int index);
    void cycleFunctionColor(int index);
    void selectFunction(int index);

    float parseEntryValue(const char *text, float fallback) const;
    int xToPlot(float x) const;
    int yToPlot(float y) const;
    float plotXAt(int sample) const;
    float finiteFallbackY() const;
    float computeDerivative(int func_index, int sample) const;
    float niceStep(float range, int max_ticks) const;

    void handleMenuInput(uint64_t newly);
    void handleEntryInput(uint64_t newly, uint64_t current_mask);
    void handleInputPageInput(uint64_t newly, uint64_t current_mask);
    void handlePlotPageInput(uint64_t newly, uint64_t current_mask);
    void handleTablePageInput(uint64_t newly, uint64_t current_mask);
    void openPageMenu();

    ServiceHub &services_;

    Page page_ = Page::Input;
    bool menu_open_ = false;
    MenuKind menu_kind_ = MenuKind::None;
    lv_group_t *menu_group_ = nullptr;

    EntryKind entry_kind_ = EntryKind::None;
    int entry_func_index_ = -1;
    char entry_buffer_[kMaxExprLen] = {};
    int entry_length_ = 0;
    bool entry_shift_lock_ = false;

    int selected_func_ = 0;
    int active_plot_func_ = 0;
    int cursor_sample_ = kPlotSamples / 2;
    bool cursor_mode_ = false;
    bool plot_equal_scale_ = true;
    bool region_zoom_active_ = false;
    bool region_zoom_anchor_set_ = false;
    int region_zoom_anchor_sample_ = 0;

    float plot_x_min_ = -6.0f;
    float plot_x_max_ = 6.0f;
    float plot_y_min_ = -4.0f;
    float plot_y_max_ = 4.0f;

    float table_start_ = 0.0f;
    float table_end_ = 10.0f;
    float table_step_ = 0.1f;
    int table_rows_ = 0;

    bool plot_dirty_ = true;
    bool table_dirty_ = true;
    bool table_rebuild_pending_ = false;
    int table_rebuild_row_ = 0;
    int table_rebuild_col_ = 0;

    EvalKind pending_kind_ = EvalKind::None;
    int pending_func_ = -1;
    uint32_t pending_generation_ = 0;
    uint32_t generation_ = 0;
    bool eval_pending_ = false;

    std::array<PlotFunc, kMaxFuncs> funcs_{};

    lv_obj_t *root_ = nullptr;
    lv_obj_t *input_page_ = nullptr;
    lv_obj_t *plot_page_ = nullptr;
    lv_obj_t *table_page_ = nullptr;
    lv_obj_t *menu_overlay_ = nullptr;
    lv_obj_t *menu_list_ = nullptr;
    lv_obj_t *entry_overlay_ = nullptr;

    lv_obj_t *input_list_ = nullptr;
    lv_obj_t *input_rows_[kMaxFuncs] = {};
    lv_obj_t *plot_area_ = nullptr;
    lv_obj_t *table_obj_ = nullptr;
    lv_obj_t *table_status_ = nullptr;
    lv_obj_t *plot_title_ = nullptr;
    lv_obj_t *table_title_ = nullptr;

    lv_obj_t *axis_x_ = nullptr;
    lv_obj_t *axis_y_ = nullptr;
    lv_obj_t *cursor_line_ = nullptr;
    lv_obj_t *x_tick_lines_[kMaxTicks] = {};
    lv_obj_t *y_tick_lines_[kMaxTicks] = {};
    lv_obj_t *x_tick_labels_[kMaxTicks] = {};
    lv_obj_t *y_tick_labels_[kMaxTicks] = {};
    std::array<lv_point_precise_t, 2> axis_x_pts_{};
    std::array<lv_point_precise_t, 2> axis_y_pts_{};
    std::array<lv_point_precise_t, 2> cursor_pts_{};
    std::array<lv_point_precise_t, 2> tick_pts_x_[kMaxTicks]{};
    std::array<lv_point_precise_t, 2> tick_pts_y_[kMaxTicks]{};

    lv_obj_t *entry_title_ = nullptr;
    lv_obj_t *entry_box_ = nullptr;

    std::array<char, 128> entry_title_buf_{};
    std::array<char, 128> entry_prev_buf_{};

    uint64_t prev_mask_ = 0;
    bool session_loaded_ = false;
    bool ui_ready_ = false;

    std::vector<MenuItem> menu_items_;
    std::vector<lv_obj_t *> menu_rows_;
};

} // namespace brookesia
