#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "brookesia/core/app.hpp"

namespace brookesia {

// Minimal notes manager stored on the FAT "storage" partition. Creates
// timestamped note files (proj_*.txt) and lets the user browse or delete
// them. Pairs with the Files app, which can preview the same content.
class ProjectApp final : public App {
public:
    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void render() override;

private:
    void ensureUi();
    void scan();
    void refreshList();
    void createNote();
    void deleteSelected();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *body_ = nullptr;
    lv_obj_t *hint_ = nullptr;

    std::vector<std::string> notes_;
    int selected_ = 0;
    int next_id_ = 1;
    uint64_t prev_mask_ = 0;
    bool ui_ready_ = false;
    bool mounted_ = false;
};

} // namespace brookesia
