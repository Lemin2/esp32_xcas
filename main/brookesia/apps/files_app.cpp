#include "brookesia/apps/files_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "cardputer_bsp.hpp"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr size_t kMaxTextBytes = 900;
constexpr size_t kMaxNameBytes = 48;

void seedReadme()
{
    char path[80];
    std::snprintf(path, sizeof(path), "%s/readme.txt", kStoragePath);
    struct stat st = {};
    if (stat(path, &st) == 0) {
        return;
    }
    FILE *f = std::fopen(path, "w");
    if (f != nullptr) {
        std::fputs("M5Cardputer giac storage.\nPrograms live in /data/programs.\n", f);
        std::fclose(f);
    }
}

bool isDirectoryMode(mode_t mode)
{
    return (mode & S_IFDIR) != 0;
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
    lv_obj_set_style_pad_all(root_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    menu_ = lv_menu_create(root_);
    lv_obj_set_size(menu_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(menu_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_menu_set_mode_header(menu_, LV_MENU_HEADER_TOP_FIXED);
    lv_menu_set_mode_root_back_button(menu_, LV_MENU_ROOT_BACK_BUTTON_DISABLED);
    menu_page_ = lv_menu_page_create(menu_, nullptr);
    lv_menu_set_page(menu_, menu_page_);

    group_ = lv_group_create();
    lv_group_set_editing(group_, false);

    text_panel_ = lv_obj_create(root_);
    lv_obj_remove_style_all(text_panel_);
    lv_obj_set_size(text_panel_, lv_pct(100), lv_pct(100));
    ui_theme::applyPage(text_panel_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_add_flag(text_panel_, LV_OBJ_FLAG_HIDDEN);

    text_box_ = lv_textarea_create(text_panel_);
    lv_obj_set_size(text_box_, w - 8, h - 24);
    lv_obj_align(text_box_, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_max_length(text_box_, kMaxTextBytes);
    ui_theme::applyText14(text_box_);
    lv_obj_set_style_text_color(text_box_, LV_COLOR_MAKE(16, 24, 36), LV_PART_MAIN);
    lv_obj_set_style_bg_color(text_box_, LV_COLOR_MAKE(252, 252, 248), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(text_box_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(text_box_, LV_COLOR_MAKE(210, 216, 226), LV_PART_MAIN);
    lv_obj_set_style_border_width(text_box_, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(text_box_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(text_box_, LV_COLOR_MAKE(34, 92, 180), LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(text_box_, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_add_state(text_box_, LV_STATE_FOCUSED);

    ui_ready_ = true;
}

std::string FilesApp::absolutePath(const std::string &name) const
{
    if (current_dir_ == kStoragePath) {
        return std::string(kStoragePath) + "/" + name;
    }
    return current_dir_ + "/" + name;
}

std::string FilesApp::selectedPath() const
{
    if (selected_ < 0 || selected_ >= static_cast<int>(menu_entries_.size())) {
        return {};
    }
    const int file_index = menu_entries_[selected_].file_index;
    if (file_index < 0 || file_index >= static_cast<int>(files_.size())) {
        return {};
    }
    return absolutePath(files_[file_index].name);
}

void FilesApp::scan()
{
    files_.clear();
    mounted_ = ensureStorageMounted();
    if (!mounted_) {
        return;
    }
    seedReadme();

    DIR *dir = opendir(current_dir_.c_str());
    if (dir == nullptr) {
        current_dir_ = kStoragePath;
        dir = opendir(current_dir_.c_str());
        if (dir == nullptr) {
            return;
        }
    }

    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const std::string path = absolutePath(ent->d_name);
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }
        FileEntry item;
        item.name = ent->d_name;
        item.is_dir = isDirectoryMode(st.st_mode);
        item.size = static_cast<size_t>(st.st_size);
        files_.push_back(item);
    }
    closedir(dir);

    std::sort(files_.begin(), files_.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });
}

void FilesApp::rebuildMenu()
{
    if (!ui_ready_) {
        return;
    }
    mode_ = Mode::Browser;
    lv_obj_clear_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(text_panel_, LV_OBJ_FLAG_HIDDEN);
    suppress_menu_events_ = true;
    if (group_ != nullptr) {
        lv_group_remove_all_objs(group_);
    }
    lv_obj_clean(menu_page_);
    menu_entries_.clear();
    suppress_menu_events_ = false;

    auto add = [this](Action action, int file_index, const char *name, const char *value, bool disabled) {
        MenuEntry entry;
        entry.action = action;
        entry.file_index = file_index;
        entry.disabled = disabled;
        entry.row = lv_menu_cont_create(menu_page_);
        lv_obj_add_flag(entry.row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(entry.row, &FilesApp::menuEntryEventCb, LV_EVENT_ALL, this);
        lv_obj_set_height(entry.row, 23);
        lv_obj_set_width(entry.row, lv_pct(100));
        lv_obj_set_flex_flow(entry.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(entry.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        ui_theme::applyRowCard(entry.row, LV_COLOR_MAKE(208, 214, 224), 4, 6, 6);
        entry.name = lv_label_create(entry.row);
        ui_theme::applyText14(entry.name);
        lv_label_set_text(entry.name, name);
        lv_obj_set_flex_grow(entry.name, 1);
        entry.value = lv_label_create(entry.row);
        ui_theme::applyText14(entry.value);
        lv_label_set_text(entry.value, value == nullptr ? "" : value);
        lv_obj_set_style_max_width(entry.value, 72, LV_PART_MAIN);
        menu_entries_.push_back(entry);
    };

    if (actions_visible_) {
        add(Action::NewFile, -1, "New file", "+", !mounted_);
        add(Action::NewFolder, -1, "New folder", "+", !mounted_);
        add(Action::Edit, -1, "Edit selected", "", files_.empty());
        add(Action::Delete, -1, "Delete selected", "", files_.empty());
    }
    if (current_dir_ != kStoragePath) {
        add(Action::Parent, -1, "..", "dir", false);
    }
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
        char meta[24];
        if (files_[i].is_dir) {
            std::snprintf(meta, sizeof(meta), "dir");
        } else {
            std::snprintf(meta, sizeof(meta), "%uB", static_cast<unsigned>(files_[i].size));
        }
        add(Action::Open, i, files_[i].name.c_str(), meta, false);
    }

    if (selected_ >= static_cast<int>(menu_entries_.size())) {
        selected_ = menu_entries_.empty() ? 0 : static_cast<int>(menu_entries_.size()) - 1;
    }
    syncFocusGroup();
    refreshSelection();
}

void FilesApp::menuEntryEventCb(lv_event_t *e)
{
    auto *self = static_cast<FilesApp *>(lv_event_get_user_data(e));
    if (self == nullptr || self->suppress_menu_events_) {
        return;
    }
    const int index = self->indexForRow(lv_event_get_target_obj(e));
    if (index < 0 || index >= static_cast<int>(self->menu_entries_.size())) {
        return;
    }
    MenuEntry &entry = self->menu_entries_[index];
    if (entry.disabled) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        self->selected_ = index;
        self->refreshSelection();
    } else if (code == LV_EVENT_CLICKED) {
        self->selected_ = index;
        self->activateSelected();
    } else if (code == LV_EVENT_KEY) {
        const uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            self->selected_ = index;
            self->activateSelected();
        }
    }
}

int FilesApp::indexForRow(lv_obj_t *row) const
{
    for (int i = 0; i < static_cast<int>(menu_entries_.size()); ++i) {
        if (menu_entries_[i].row == row) {
            return i;
        }
    }
    return -1;
}

void FilesApp::styleMenuEntry(MenuEntry &entry, bool selected)
{
    if (entry.row == nullptr || entry.name == nullptr) {
        return;
    }
    lv_obj_set_style_bg_opa(entry.row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(entry.row, LV_COLOR_MAKE(24, 84, 192), LV_PART_MAIN);
    lv_color_t color;
    if (selected) {
        color = LV_COLOR_MAKE(255, 255, 255);
    } else {
        color = LV_COLOR_MAKE(16, 24, 36);
    }
    if (entry.disabled && !selected) {
        color = LV_COLOR_MAKE(132, 138, 148);
    }
    lv_obj_set_style_text_color(entry.name, color, LV_PART_MAIN);
    if (entry.value != nullptr) {
        lv_obj_set_style_text_color(entry.value, color, LV_PART_MAIN);
    }
}

void FilesApp::refreshSelection()
{
    for (int i = 0; i < static_cast<int>(menu_entries_.size()); ++i) {
        if (menu_entries_[i].row == nullptr) {
            continue;
        }
        styleMenuEntry(menu_entries_[i], i == selected_);
        if (i == selected_) {
            lv_obj_scroll_to_view(menu_entries_[i].row, LV_ANIM_ON);
        }
    }
}

void FilesApp::syncFocusGroup()
{
    if (group_ == nullptr) {
        return;
    }
    lv_group_remove_all_objs(group_);
    for (auto &entry : menu_entries_) {
        if (!entry.disabled && entry.row != nullptr) {
            lv_group_add_obj(group_, entry.row);
        }
    }
    if (selected_ >= 0 && selected_ < static_cast<int>(menu_entries_.size()) && !menu_entries_[selected_].disabled) {
        lv_group_focus_obj(menu_entries_[selected_].row);
    }
}

void FilesApp::goParent()
{
    if (current_dir_ == kStoragePath) {
        return;
    }
    const size_t slash = current_dir_.find_last_of('/');
    if (slash == std::string::npos || slash <= std::strlen(kStoragePath)) {
        current_dir_ = kStoragePath;
    } else {
        current_dir_.erase(slash);
    }
    selected_ = 0;
    scan();
    rebuildMenu();
}

void FilesApp::openSelected()
{
    const std::string path = selectedPath();
    if (path.empty()) {
        return;
    }
    const int file_index = menu_entries_[selected_].file_index;
    if (files_[file_index].is_dir) {
        current_dir_ = path;
        selected_ = 0;
        scan();
        rebuildMenu();
    } else {
        showFile(false);
    }
}

void FilesApp::showFile(bool edit)
{
    const std::string path = selectedPath();
    if (path.empty()) {
        return;
    }
    active_path_ = path;
    char buf[kMaxTextBytes + 1] = {};
    FILE *f = std::fopen(path.c_str(), "r");
    if (f != nullptr) {
        const size_t n = std::fread(buf, 1, kMaxTextBytes, f);
        buf[n] = '\0';
        std::fclose(f);
    }
    lv_textarea_set_text(text_box_, buf);
    lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(text_panel_, LV_OBJ_FLAG_HIDDEN);
    mode_ = edit ? Mode::Editor : Mode::Viewer;
}

void FilesApp::saveEditor(bool refresh_menu)
{
    if (active_path_.empty()) {
        return;
    }
    FILE *f = std::fopen(active_path_.c_str(), "w");
    if (f != nullptr) {
        const char *text = lv_textarea_get_text(text_box_);
        std::fputs(text == nullptr ? "" : text, f);
        std::fclose(f);
    }
    if (!refresh_menu) {
        return;
    }
    scan();
    rebuildMenu();
}

void FilesApp::startNameEdit(Action action)
{
    pending_name_action_ = action;
    active_path_.clear();
    lv_textarea_set_max_length(text_box_, kMaxNameBytes);
    lv_textarea_set_text(text_box_, action == Action::NewFolder ? "folder" : "file.txt");
    lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(text_panel_, LV_OBJ_FLAG_HIDDEN);
    mode_ = Mode::NameEdit;
}

void FilesApp::commitNameEdit()
{
    const char *raw = lv_textarea_get_text(text_box_);
    if (raw == nullptr || raw[0] == '\0' || std::strchr(raw, '/') != nullptr) {
        rebuildMenu();
        return;
    }
    const std::string path = absolutePath(raw);
    if (pending_name_action_ == Action::NewFolder) {
        mkdir(path.c_str(), 0775);
    } else {
        FILE *f = std::fopen(path.c_str(), "w");
        if (f != nullptr) {
            std::fclose(f);
        }
    }
    lv_textarea_set_max_length(text_box_, kMaxTextBytes);
    scan();
    rebuildMenu();
}

void FilesApp::deleteSelected()
{
    const std::string path = selectedPath();
    if (path.empty()) {
        return;
    }
    const int file_index = menu_entries_[selected_].file_index;
    if (file_index >= 0 && file_index < static_cast<int>(files_.size()) && files_[file_index].is_dir) {
        rmdir(path.c_str());
    } else {
        std::remove(path.c_str());
    }
    scan();
    rebuildMenu();
}

void FilesApp::activateSelected()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(menu_entries_.size())) {
        return;
    }
    MenuEntry &entry = menu_entries_[selected_];
    if (entry.disabled) {
        return;
    }
    if (entry.action == Action::Parent) {
        goParent();
    } else if (entry.action == Action::Open) {
        openSelected();
    } else if (entry.action == Action::NewFile || entry.action == Action::NewFolder) {
        startNameEdit(entry.action);
    } else if (entry.action == Action::Edit) {
        showFile(true);
    } else if (entry.action == Action::Delete) {
        deleteSelected();
    }
}

void FilesApp::onFocus()
{
    ensureUi();
    if (group_ != nullptr) {
        lv_group_set_default(group_);
    }
    if (root_ != nullptr) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    scan();
    rebuildMenu();
}

void FilesApp::onBlur()
{
    if (mode_ == Mode::Editor) {
        saveEditor(false);
    }
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void FilesApp::releaseUi()
{
    if (group_ != nullptr) {
        lv_group_delete(group_);
        group_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    menu_ = nullptr;
    menu_page_ = nullptr;
    text_panel_ = nullptr;
    text_box_ = nullptr;
    menu_entries_.clear();
    suppress_menu_events_ = false;
    mode_ = Mode::Browser;
    ui_ready_ = false;
}

void FilesApp::handleKeyboardState(uint64_t pressedMask)
{
    prev_mask_ = pressedMask;
}

bool FilesApp::handleMenuButton()
{
    if (mode_ != Mode::Browser) {
        return false;
    }
    actions_visible_ = !actions_visible_;
    selected_ = 0;
    rebuildMenu();
    return true;
}

void FilesApp::handleMappedKey(uint32_t key)
{
    if (mode_ == Mode::Browser) {
        if (key == LV_KEY_UP) {
            if (group_ != nullptr) lv_group_focus_prev(group_);
        } else if (key == LV_KEY_DOWN) {
            if (group_ != nullptr) lv_group_focus_next(group_);
        } else if (key == LV_KEY_ENTER) {
            if (group_ != nullptr) lv_group_send_data(group_, key);
        } else if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            goParent();
        } else if (group_ != nullptr) {
            lv_group_send_data(group_, key);
        }
        return;
    }
    if (mode_ == Mode::Viewer) {
        if (key == LV_KEY_ESC || key == LV_KEY_ENTER) rebuildMenu();
        return;
    }

    if (key == LV_KEY_ESC) {
        if (mode_ == Mode::NameEdit) {
            lv_textarea_set_max_length(text_box_, kMaxTextBytes);
            commitNameEdit();
        } else {
            saveEditor();
        }
    } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        lv_textarea_delete_char(text_box_);
    } else if (key == LV_KEY_ENTER) {
        if (mode_ == Mode::NameEdit) {
            commitNameEdit();
        } else {
            lv_textarea_add_char(text_box_, '\n');
        }
    } else if (key >= 32U && key <= 126U) {
        lv_textarea_add_char(text_box_, static_cast<uint32_t>(key));
    }
}

void FilesApp::render()
{
    (void)prev_mask_;
}

} // namespace brookesia
