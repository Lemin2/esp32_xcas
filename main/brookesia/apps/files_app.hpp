#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "brookesia/core/app.hpp"

namespace brookesia {

// Simple browser for the on-flash FAT "storage" partition mounted at /data.
// Lists files with sizes and previews their first bytes. Degrades gracefully
// if directory enumeration is unavailable in this build.
class FilesApp final : public App {
public:
    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void render() override;

private:
    enum class Mode { List, Preview };

    void ensureUi();
    void scan();
    void refreshList();
    void preview();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *preview_panel_ = nullptr;
    lv_obj_t *preview_title_ = nullptr;
    lv_obj_t *preview_body_ = nullptr;

    std::vector<std::string> names_;
    Mode mode_ = Mode::List;
    int selected_ = 0;
    uint64_t prev_mask_ = 0;
    bool ui_ready_ = false;
    bool mounted_ = false;
};

} // namespace brookesia
