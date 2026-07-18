#pragma once

#include <string>

#include "brookesia/core/app.hpp"
#include "lvgl.h"

namespace brookesia {

class PlaceholderApp final : public App {
public:
    explicit PlaceholderApp(std::string title);

    bool init() override;
    void onFocus() override;
    void releaseUi() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void render() override;

private:
    void ensureUi();

    std::string title_;
    lv_obj_t *title_label_ = nullptr;
    lv_obj_t *hint_label_ = nullptr;
    bool ui_ready_ = false;
    uint64_t last_mask_ = 0;
};

} // namespace brookesia
