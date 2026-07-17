#include "brookesia/apps/files_app.hpp"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

#include "cardputer_bsp.hpp"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr uint64_t kUpBit = (1ULL << 39);    // ';'
constexpr uint64_t kDownBit = (1ULL << 53);  // '.'
constexpr uint64_t kLeftBit = (1ULL << 52);  // ','
constexpr uint64_t kEnterBit = (1ULL << 41); // Enter

void seedReadme()
{
    char path[64];
    std::snprintf(path, sizeof(path), "%s/readme.txt", kStoragePath);
    struct stat st = {};
    if (stat(path, &st) == 0) {
        return;
    }
    FILE *f = std::fopen(path, "w");
    if (f != nullptr) {
        std::fputs("M5Cardputer giac storage.\n"
                   "Files here live on the LittleFS 'storage' partition.\n"
                   "Use the Project app to create notes.\n",
                   f);
        std::fclose(f);
    }
}

} // namespace

bool FilesApp::init()
{
    return true;
}

void FilesApp::ensureUi()
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
    lv_label_set_text(title_, "Files");

    list_ = lv_list_create(root_);
    lv_obj_set_width(list_, w - 12);
    lv_obj_set_flex_grow(list_, 1);
    ui_theme::applyPanel(list_, LV_COLOR_MAKE(252, 252, 248), LV_COLOR_MAKE(220, 224, 232), 8, 4, 4);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    preview_panel_ = lv_obj_create(root_);
    lv_obj_set_width(preview_panel_, w - 12);
    lv_obj_set_flex_grow(preview_panel_, 1);
    ui_theme::applyPanel(preview_panel_, LV_COLOR_MAKE(252, 252, 248), LV_COLOR_MAKE(220, 224, 232), 8, 4, 4);
    lv_obj_set_style_pad_all(preview_panel_, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(preview_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(preview_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(preview_panel_, LV_OBJ_FLAG_HIDDEN);

    preview_title_ = lv_label_create(preview_panel_);
    ui_theme::applyText14(preview_title_);
    lv_obj_set_style_text_color(preview_title_, LV_COLOR_MAKE(34, 68, 140), LV_PART_MAIN);

    preview_body_ = lv_label_create(preview_panel_);
    ui_theme::applyText14(preview_body_);
    lv_obj_set_style_text_color(preview_body_, LV_COLOR_MAKE(16, 24, 36), LV_PART_MAIN);
    lv_label_set_long_mode(preview_body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(preview_body_, w - 24);
    lv_obj_set_flex_grow(preview_body_, 1);

    status_ = lv_label_create(root_);
    ui_theme::applyText14(status_);
    lv_obj_set_style_text_color(status_, LV_COLOR_MAKE(100, 112, 132), LV_PART_MAIN);

    ui_ready_ = true;
}

void FilesApp::scan()
{
    names_.clear();
    mounted_ = ensureStorageMounted();
    if (!mounted_) {
        return;
    }

    seedReadme();

    DIR *dir = opendir(kStoragePath);
    if (dir == nullptr) {
        return;
    }

    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        names_.emplace_back(ent->d_name);
    }
    closedir(dir);
    std::sort(names_.begin(), names_.end());

    if (selected_ >= static_cast<int>(names_.size())) {
        selected_ = names_.empty() ? 0 : static_cast<int>(names_.size()) - 1;
    }
}

void FilesApp::refreshList()
{
    if (!ui_ready_) {
        return;
    }

    mode_ = Mode::List;
    lv_label_set_text(title_, "Files");
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(preview_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(list_);

    if (!mounted_) {
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, "Storage unavailable");
        ui_theme::applyText14(row);
        lv_label_set_text(status_, "LittleFS /data not mounted");
        return;
    }

    if (names_.empty()) {
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, "(empty)");
        ui_theme::applyText14(row);
        lv_label_set_text(status_, "No files");
        return;
    }

    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
        char line[96];
        char path[96];
        std::snprintf(path, sizeof(path), "%s/%s", kStoragePath, names_[i].c_str());
        struct stat st = {};
        long size = (stat(path, &st) == 0) ? static_cast<long>(st.st_size) : 0;

        std::snprintf(line, sizeof(line), "%s  %ldB", names_[i].c_str(), size);
        lv_obj_t *row = lv_list_add_btn(list_, nullptr, line);
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

    char status[48];
    std::snprintf(status, sizeof(status), "%d file(s)", static_cast<int>(names_.size()));
    lv_label_set_text(status_, status);
}

void FilesApp::preview()
{
    if (names_.empty() || selected_ < 0 || selected_ >= static_cast<int>(names_.size())) {
        return;
    }
    mode_ = Mode::Preview;

    char path[96];
    std::snprintf(path, sizeof(path), "%s/%s", kStoragePath, names_[selected_].c_str());
    lv_label_set_text(title_, "Preview");
    lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(preview_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(preview_title_, names_[selected_].c_str());
    lv_label_set_text(status_, "Press , to return");

    FILE *f = std::fopen(path, "r");
    if (f == nullptr) {
        lv_label_set_text(preview_body_, "(cannot open)");
        return;
    }
    char buf[201];
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    std::fclose(f);
    lv_label_set_text(preview_body_, (n == 0) ? "(empty file)" : buf);
}

void FilesApp::onFocus()
{
    ensureUi();
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    scan();
    refreshList();
}

void FilesApp::onBlur()
{
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void FilesApp::handleKeyboardState(uint64_t pressedMask)
{
    const uint64_t newly = pressedMask & ~prev_mask_;
    prev_mask_ = pressedMask;

    if (mode_ == Mode::Preview) {
        if ((newly & (kEnterBit | kLeftBit)) != 0U) {
            refreshList();
        }
        return;
    }

    bool dirty = false;
    if (!names_.empty()) {
        if ((newly & kUpBit) != 0U) {
            selected_ = (selected_ + static_cast<int>(names_.size()) - 1) %
                        static_cast<int>(names_.size());
            dirty = true;
        }
        if ((newly & kDownBit) != 0U) {
            selected_ = (selected_ + 1) % static_cast<int>(names_.size());
            dirty = true;
        }
        if ((newly & kEnterBit) != 0U) {
            preview();
            return;
        }
    }

    if (dirty) {
        refreshList();
    }
}

void FilesApp::render()
{
    (void)prev_mask_;
}

} // namespace brookesia
