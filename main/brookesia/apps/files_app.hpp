#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "brookesia/apps/fs_util.hpp"
#include "brookesia/core/app.hpp"

namespace brookesia {

// Compact file manager for the on-flash LittleFS storage partition.
class FilesApp final : public App {
public:
    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void releaseUi() override;
    bool handleMenuButton() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void handleMappedKey(uint32_t key) override;
    void render() override;

private:
    enum class Mode : uint8_t { Browser, Viewer, Editor, NameEdit };
    enum class Action : uint8_t { Parent, Open, NewFile, NewFolder, Edit, Delete };

    struct FileEntry {
        std::string name;
        bool is_dir = false;
        size_t size = 0;
    };

    struct MenuEntry {
        Action action = Action::Open;
        int file_index = -1;
        lv_obj_t *row = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *value = nullptr;
        bool disabled = false;
    };

    void ensureUi();
    void scan();
    void rebuildMenu();
    void refreshSelection();
    void styleMenuEntry(MenuEntry &entry, bool selected);
    void syncFocusGroup();
    void activateSelected();
    void openSelected();
    void showFile(bool edit);
    void saveEditor(bool refresh_menu = true);
    void startNameEdit(Action action);
    void commitNameEdit();
    void deleteSelected();
    void goParent();
    std::string absolutePath(const std::string &name) const;
    std::string selectedPath() const;
    int indexForRow(lv_obj_t *row) const;
    static void menuEntryEventCb(lv_event_t *e);

    lv_obj_t *root_ = nullptr;
    lv_obj_t *menu_ = nullptr;
    lv_obj_t *menu_page_ = nullptr;
    lv_obj_t *text_panel_ = nullptr;
    lv_obj_t *text_box_ = nullptr;
    lv_group_t *group_ = nullptr;

    std::vector<FileEntry> files_;
    std::vector<MenuEntry> menu_entries_;
    std::string current_dir_ = kStoragePath;
    std::string active_path_;
    Action pending_name_action_ = Action::NewFile;
    Mode mode_ = Mode::Browser;
    int selected_ = 0;
    uint64_t prev_mask_ = 0;
    bool ui_ready_ = false;
    bool mounted_ = false;
    bool actions_visible_ = false;
    bool suppress_menu_events_ = false;
};

} // namespace brookesia
