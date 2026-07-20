#include "brookesia/apps/project_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "cardputer_bsp/cardputer_bsp.hpp"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/ui_theme.hpp"

namespace ui_theme = brookesia::ui_theme;

namespace brookesia {
namespace {

constexpr char kProgramDir[] = "/data/programs";
constexpr char kProgramPrefix[] = "prog_";
constexpr char kProgramExt[] = ".xcas";
constexpr size_t kMaxScriptBytes = 768;

bool hasSuffix(const char *s, const char *suffix)
{
    if (s == nullptr || suffix == nullptr) {
        return false;
    }
    const size_t n = std::strlen(s);
    const size_t m = std::strlen(suffix);
    return n >= m && std::strcmp(s + n - m, suffix) == 0;
}

} // namespace

ProjectApp::ProjectApp(ServiceHub &services) : services_(services), cas_(services.casService())
{
}

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

    const lv_coord_t w = static_cast<lv_coord_t>(services_.board().displayWidth());
    const lv_coord_t h = static_cast<lv_coord_t>(services_.board().displayHeight());
    const lv_coord_t status_h = static_cast<lv_coord_t>(services_.board().statusBarHeight());
    const bool touch = services_.board().hasTouchInput();

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h - status_h);
    lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, status_h);
    ui_theme::applyPage(root_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_set_style_pad_all(root_, touch ? 10 : 4, LV_PART_MAIN);
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

    editor_panel_ = lv_obj_create(root_);
    lv_obj_remove_style_all(editor_panel_);
    lv_obj_set_size(editor_panel_, lv_pct(100), lv_pct(100));
    ui_theme::applyPage(editor_panel_, LV_COLOR_MAKE(245, 245, 238));
    lv_obj_add_flag(editor_panel_, LV_OBJ_FLAG_HIDDEN);

    editor_box_ = lv_textarea_create(editor_panel_);
    lv_obj_set_size(editor_box_, w - (touch ? 20 : 8), h - status_h - (touch ? 20 : 8));
    lv_obj_align(editor_box_, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_max_length(editor_box_, kMaxScriptBytes);
    ui_theme::applyText14(editor_box_);
    lv_obj_set_style_text_color(editor_box_, LV_COLOR_MAKE(16, 24, 36), LV_PART_MAIN);
    lv_obj_set_style_bg_color(editor_box_, LV_COLOR_MAKE(252, 252, 248), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(editor_box_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(editor_box_, LV_COLOR_MAKE(210, 216, 226), LV_PART_MAIN);
    lv_obj_set_style_border_width(editor_box_, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(editor_box_, touch ? 10 : 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(editor_box_, LV_COLOR_MAKE(34, 92, 180), LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(editor_box_, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_add_state(editor_box_, LV_STATE_FOCUSED);

    ui_ready_ = true;
}

void ProjectApp::ensureProgramDir()
{
    mounted_ = ensureStorageMounted();
    if (!mounted_) {
        return;
    }
    struct stat st = {};
    if (stat(kProgramDir, &st) != 0) {
        mkdir(kProgramDir, 0775);
    }
}

void ProjectApp::scan()
{
    programs_.clear();
    next_id_ = 1;
    ensureProgramDir();
    if (!mounted_) {
        return;
    }

    DIR *dir = opendir(kProgramDir);
    if (dir == nullptr) {
        return;
    }

    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.' || !hasSuffix(ent->d_name, kProgramExt)) {
            continue;
        }
        char path[320];
        std::snprintf(path, sizeof(path), "%s/%s", kProgramDir, ent->d_name);
        struct stat st = {};
        ProgramEntry item;
        item.name = ent->d_name;
        item.size = (stat(path, &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
        programs_.push_back(item);

        int id = 0;
        if (std::sscanf(ent->d_name, "prog_%d.xcas", &id) == 1 && id >= next_id_) {
            next_id_ = id + 1;
        }
    }
    closedir(dir);
    std::sort(programs_.begin(), programs_.end(), [](const ProgramEntry &a, const ProgramEntry &b) {
        return a.name < b.name;
    });
    if (selected_ >= static_cast<int>(programs_.size() + (actions_visible_ ? 4 : 0))) {
        selected_ = 0;
    }
}

void ProjectApp::rebuildMenu()
{
    if (!ui_ready_) {
        return;
    }
    mode_ = Mode::Menu;
    lv_obj_clear_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editor_panel_, LV_OBJ_FLAG_HIDDEN);
    suppress_menu_events_ = true;
    if (group_ != nullptr) {
        lv_group_remove_all_objs(group_);
    }
    lv_obj_clean(menu_page_);
    menu_entries_.clear();
    suppress_menu_events_ = false;
    const bool touch = services_.board().hasTouchInput();

    auto add = [this, touch](Action action, int program_index, const char *name, const char *value, bool disabled) {
        MenuEntry entry;
        entry.action = action;
        entry.program_index = program_index;
        entry.disabled = disabled;
        entry.row = lv_menu_cont_create(menu_page_);
        lv_obj_add_flag(entry.row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(entry.row, &ProjectApp::menuEntryEventCb, LV_EVENT_ALL, this);
        lv_obj_set_height(entry.row, touch ? 52 : 23);
        lv_obj_set_width(entry.row, lv_pct(100));
        lv_obj_set_flex_flow(entry.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(entry.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        ui_theme::applyRowCard(entry.row, LV_COLOR_MAKE(208, 214, 224), touch ? 8 : 4, touch ? 12 : 6, touch ? 12 : 6);
        entry.name = lv_label_create(entry.row);
        lv_obj_add_flag(entry.name, LV_OBJ_FLAG_EVENT_BUBBLE);
        ui_theme::applyText14(entry.name);
        lv_label_set_text(entry.name, name);
        lv_obj_set_flex_grow(entry.name, 1);
        entry.value = lv_label_create(entry.row);
        lv_obj_add_flag(entry.value, LV_OBJ_FLAG_EVENT_BUBBLE);
        ui_theme::applyText14(entry.value);
        lv_label_set_text(entry.value, value == nullptr ? "" : value);
        lv_obj_set_style_max_width(entry.value, touch ? 180 : 72, LV_PART_MAIN);
        menu_entries_.push_back(entry);
    };

    if (actions_visible_) {
        add(Action::NewProgram, -1, "New program", "+", !mounted_);
        add(Action::Edit, -1, "Edit selected", "", programs_.empty());
        add(Action::Run, -1, "Run selected", "", programs_.empty() || cas_.busy());
        add(Action::Delete, -1, "Delete selected", "", programs_.empty());
    }
    for (int i = 0; i < static_cast<int>(programs_.size()); ++i) {
        char size_buf[20];
        std::snprintf(size_buf, sizeof(size_buf), "%uB", static_cast<unsigned>(programs_[i].size));
        add(Action::Open, i, programs_[i].name.c_str(), size_buf, false);
    }

    if (selected_ >= static_cast<int>(menu_entries_.size())) {
        selected_ = menu_entries_.empty() ? 0 : static_cast<int>(menu_entries_.size()) - 1;
    }
    syncFocusGroup();
    refreshSelection();
}

void ProjectApp::menuEntryEventCb(lv_event_t *e)
{
    auto *self = static_cast<ProjectApp *>(lv_event_get_user_data(e));
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

int ProjectApp::indexForRow(lv_obj_t *row) const
{
    for (int i = 0; i < static_cast<int>(menu_entries_.size()); ++i) {
        if (menu_entries_[i].row == row) {
            return i;
        }
    }
    return -1;
}

void ProjectApp::refreshSelection()
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

void ProjectApp::styleMenuEntry(MenuEntry &entry, bool selected)
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

ProjectApp::MenuEntry *ProjectApp::selectedEntry()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(menu_entries_.size())) {
        return nullptr;
    }
    return &menu_entries_[selected_];
}

void ProjectApp::syncFocusGroup()
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

std::string ProjectApp::makeProgramName() const
{
    char name[32];
    std::snprintf(name, sizeof(name), "%s%03d%s", kProgramPrefix, next_id_, kProgramExt);
    return name;
}

std::string ProjectApp::selectedPath() const
{
    int idx = -1;
    if (selected_ >= 0 && selected_ < static_cast<int>(menu_entries_.size())) {
        idx = menu_entries_[selected_].program_index;
    }
    if (idx < 0 && !programs_.empty()) {
        idx = 0;
    }
    if (idx < 0 || idx >= static_cast<int>(programs_.size())) {
        return {};
    }
    return std::string(kProgramDir) + "/" + programs_[idx].name;
}

void ProjectApp::openEditor(bool new_program)
{
    if (!mounted_) {
        return;
    }
    editor_path_ = new_program ? (std::string(kProgramDir) + "/" + makeProgramName()) : selectedPath();
    if (editor_path_.empty()) {
        return;
    }

    char buf[kMaxScriptBytes + 1] = {};
    FILE *f = std::fopen(editor_path_.c_str(), "r");
    if (f != nullptr) {
        const size_t n = std::fread(buf, 1, kMaxScriptBytes, f);
        buf[n] = '\0';
        std::fclose(f);
    } else if (new_program) {
        std::snprintf(buf, sizeof(buf), "// %s\n1+1\n", makeProgramName().c_str());
    }

    lv_textarea_set_text(editor_box_, buf);
    lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(editor_panel_, LV_OBJ_FLAG_HIDDEN);
    mode_ = Mode::Editor;
}

void ProjectApp::saveEditor(bool refresh_menu)
{
    if (editor_path_.empty()) {
        return;
    }
    FILE *f = std::fopen(editor_path_.c_str(), "w");
    if (f != nullptr) {
        const char *text = lv_textarea_get_text(editor_box_);
        std::fputs(text == nullptr ? "" : text, f);
        std::fclose(f);
    }
    if (!refresh_menu) {
        return;
    }
    scan();
    rebuildMenu();
}

void ProjectApp::deleteSelectedProgram()
{
    const std::string path = selectedPath();
    if (!path.empty()) {
        std::remove(path.c_str());
    }
    scan();
    rebuildMenu();
}

void ProjectApp::runSelectedProgram()
{
    const std::string path = selectedPath();
    if (path.empty() || cas_.busy()) {
        return;
    }
    FILE *f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        return;
    }
    char buf[kMaxScriptBytes + 1];
    const size_t n = std::fread(buf, 1, kMaxScriptBytes, f);
    buf[n] = '\0';
    std::fclose(f);
    run_script_ = buf;
    run_pos_ = 0;
    submitNextScriptLine();
}

void ProjectApp::submitNextScriptLine()
{
    while (run_pos_ < run_script_.size()) {
        size_t end = run_script_.find('\n', run_pos_);
        if (end == std::string::npos) {
            end = run_script_.size();
        }
        std::string line = run_script_.substr(run_pos_, end - run_pos_);
        run_pos_ = (end < run_script_.size()) ? end + 1 : end;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) {
            ++first;
        }
        if (first >= line.size() || line.compare(first, 2, "//") == 0) {
            continue;
        }
        line.erase(0, first);
        if (line.size() >= 191) {
            line.resize(191);
        }
        cas_.submit(line.c_str());
        return;
    }
    run_script_.clear();
    run_pos_ = 0;
}

void ProjectApp::activateSelected()
{
    MenuEntry *entry = selectedEntry();
    if (entry == nullptr || entry->disabled) {
        return;
    }
    if (entry->action == Action::NewProgram) {
        openEditor(true);
    } else if (entry->action == Action::Open) {
        return;
    } else if (entry->action == Action::Edit) {
        openEditor(false);
    } else if (entry->action == Action::Run) {
        runSelectedProgram();
        rebuildMenu();
    } else if (entry->action == Action::Delete) {
        deleteSelectedProgram();
    }
}

void ProjectApp::onFocus()
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

void ProjectApp::onBlur()
{
    if (mode_ == Mode::Editor) {
        saveEditor(false);
    }
    if (root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ProjectApp::releaseUi()
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
    editor_panel_ = nullptr;
    editor_box_ = nullptr;
    menu_entries_.clear();
    suppress_menu_events_ = false;
    mode_ = Mode::Menu;
    ui_ready_ = false;
}

bool ProjectApp::handleMenuButton()
{
    if (mode_ != Mode::Menu) {
        return false;
    }
    actions_visible_ = !actions_visible_;
    selected_ = 0;
    rebuildMenu();
    return true;
}

void ProjectApp::handleMappedKey(uint32_t key)
{
    if (mode_ != Mode::Editor) {
        if (key == LV_KEY_UP) {
            if (group_ != nullptr) lv_group_focus_prev(group_);
        } else if (key == LV_KEY_DOWN) {
            if (group_ != nullptr) lv_group_focus_next(group_);
        } else if (key == LV_KEY_ENTER) {
            if (group_ != nullptr) lv_group_send_data(group_, key);
        } else if (group_ != nullptr) {
            lv_group_send_data(group_, key);
        }
        return;
    }

    if (key == LV_KEY_ESC) {
        saveEditor();
    } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        lv_textarea_delete_char(editor_box_);
    } else if (key == LV_KEY_ENTER) {
        lv_textarea_add_char(editor_box_, '\n');
    } else if (key >= 32U && key <= 126U) {
        lv_textarea_add_char(editor_box_, static_cast<uint32_t>(key));
    }
}

void ProjectApp::render()
{
    if (!run_script_.empty() && !cas_.busy()) {
        std::string ignored;
        (void)cas_.pollResult(ignored);
        submitNextScriptLine();
        if (mode_ == Mode::Menu) {
            rebuildMenu();
        }
    }
}

} // namespace brookesia
