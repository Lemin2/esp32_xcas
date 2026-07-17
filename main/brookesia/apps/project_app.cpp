#include "brookesia/apps/project_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_timer.h"

#include "cardputer_bsp.hpp"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr uint64_t kUpBit = (1ULL << 39);        // ';'
constexpr uint64_t kDownBit = (1ULL << 53);      // '.'
constexpr uint64_t kEnterBit = (1ULL << 41);     // Enter
constexpr uint64_t kBackspaceBit = (1ULL << 13); // Backspace

constexpr char kPrefix[] = "proj_";

} // namespace

bool ProjectApp::init()
{
    return true;
}

void ProjectApp::ensureUi()
{
    if (ui_ready_) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    const lv_coord_t w = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayWidth);
    const lv_coord_t h = static_cast<lv_coord_t>(board::CardputerBsp::kDisplayHeight);

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h - 16);
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 16);
    ui_theme::applyPage(root_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_set_style_pad_all(root_, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root_, 3, LV_PART_MAIN);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_AUTO);

    title_ = lv_label_create(root_);
    ui_theme::applyText16(title_);
    lv_obj_set_style_text_color(title_, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_label_set_text(title_, "Projects");

    list_ = lv_list_create(root_);
    lv_obj_set_width(list_, w - 12);
    lv_obj_set_flex_grow(list_, 1);
    ui_theme::applyPanel(list_, LV_COLOR_MAKE(252, 252, 248), LV_COLOR_MAKE(220, 224, 232), 8, 4, 4);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    status_ = lv_label_create(root_);
    ui_theme::applyText14(status_);
    lv_obj_set_style_text_color(status_, LV_COLOR_MAKE(100, 112, 132), LV_PART_MAIN);
    lv_label_set_text(status_, "Enter new, Backspace delete");

    ui_ready_ = true;
}

void ProjectApp::scan()
{
    notes_.clear();
    mounted_ = ensureStorageMounted();
    if (!mounted_) {
        return;
    }

    DIR *dir = opendir(kStoragePath);
    if (dir == nullptr) {
        return;
    }

    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strncmp(ent->d_name, kPrefix, std::strlen(kPrefix)) == 0) {
            notes_.emplace_back(ent->d_name);
            int id = 0;
            if (std::sscanf(ent->d_name + std::strlen(kPrefix), "%d", &id) == 1 &&
                id >= next_id_) {
                next_id_ = id + 1;
            }
        }
    }
    closedir(dir);
    std::sort(notes_.begin(), notes_.end());

    if (selected_ >= static_cast<int>(notes_.size())) {
        selected_ = notes_.empty() ? 0 : static_cast<int>(notes_.size()) - 1;
    }
}

void ProjectApp::refreshList()
{
    if (!ui_ready_) {
        return;
    }

    lv_obj_clean(list_);

    if (!mounted_) {
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, "Storage unavailable");
        ui_theme::applyText14(row);
        lv_label_set_text(status_, "LittleFS /data not mounted");
        return;
    }

    if (notes_.empty()) {
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, "No projects yet");
        ui_theme::applyText14(row);
        lv_label_set_text(status_, "Press Enter to create");
        return;
    }

    for (int i = 0; i < static_cast<int>(notes_.size()); ++i) {
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, notes_[i].c_str());
        ui_theme::applyText14(row);
        lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_right(row, 6, LV_PART_MAIN);

        if (i == selected_) {
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(row, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
            lv_obj_set_style_text_color(row, LV_COLOR_MAKE(255, 255, 255), LV_PART_MAIN);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        } else {
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_text_color(row, LV_COLOR_MAKE(16, 24, 36), LV_PART_MAIN);
        }
    }

    char status[64];
    std::snprintf(status, sizeof(status), "%d project(s)  Enter:new  Bksp:del",
                  static_cast<int>(notes_.size()));
    lv_label_set_text(status_, status);
}

void ProjectApp::createNote()
{
    if (!mounted_) {
        return;
    }
    char name[40];
    std::snprintf(name, sizeof(name), "%s%03d.txt", kPrefix, next_id_);
    char path[96];
    std::snprintf(path, sizeof(path), "%s/%s", kStoragePath, name);

    FILE *f = std::fopen(path, "w");
    if (f != nullptr) {
        const uint32_t up_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        std::fprintf(f, "Project %03d\nCreated at uptime %lus\n", next_id_,
                     static_cast<unsigned long>(up_s));
        std::fclose(f);
        ++next_id_;
    }
    scan();
    refreshList();
}

void ProjectApp::deleteSelected()
{
    if (!mounted_ || notes_.empty()) {
        return;
    }
    char path[96];
    std::snprintf(path, sizeof(path), "%s/%s", kStoragePath, notes_[selected_].c_str());
    std::remove(path);
    scan();
    refreshList();
}

void ProjectApp::onFocus()
{
    ensureUi();
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    scan();
    refreshList();
}

void ProjectApp::onBlur()
{
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ProjectApp::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly = pressedMask & ~prev_mask_;
    prev_mask_ = pressedMask;

    if ((newly & kEnterBit) != 0U) {
        createNote();
        return;
    }
    if ((newly & kBackspaceBit) != 0U) {
        deleteSelected();
        return;
    }

    bool dirty = false;
    if (!notes_.empty()) {
        if ((newly & kUpBit) != 0U) {
            selected_ = (selected_ + static_cast<int>(notes_.size()) - 1) %
                        static_cast<int>(notes_.size());
            dirty = true;
        }
        if ((newly & kDownBit) != 0U) {
            selected_ = (selected_ + 1) % static_cast<int>(notes_.size());
            dirty = true;
        }
    }

    if (dirty) {
        refreshList();
    }
}

void ProjectApp::render()
{
    (void)prev_mask_;
}

} // namespace brookesia
