#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

#include "brookesia/core/app.hpp"
#include "brookesia/core/service_hub.hpp"

namespace brookesia {

// XCAS script program manager stored under /data/programs.
class ProjectApp final : public App {
public:
    explicit ProjectApp(ServiceHub &services);

    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void releaseUi() override;
    bool handleMenuButton() override;
    void handleMappedKey(uint32_t key) override;
    void render() override;

private:
    enum class Mode : uint8_t { Menu, Editor };
    enum class Action : uint8_t { Open, NewProgram, Edit, Run, Delete };

    struct ProgramEntry {
        std::string name;
        size_t size = 0;
    };

    struct MenuEntry {
        Action action = Action::Open;
        int program_index = -1;
        lv_obj_t *row = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *value = nullptr;
        bool disabled = false;
    };

    void ensureUi();
    void ensureProgramDir();
    void scan();
    void rebuildMenu();
    void refreshSelection();
    void styleMenuEntry(MenuEntry &entry, bool selected);
    void syncFocusGroup();
    void activateSelected();
    void openEditor(bool new_program);
    void saveEditor(bool refresh_menu = true);
    void deleteSelectedProgram();
    void runSelectedProgram();
    void submitNextScriptLine();
    std::string selectedPath() const;
    std::string makeProgramName() const;
    MenuEntry *selectedEntry();
    int indexForRow(lv_obj_t *row) const;
    static void menuEntryEventCb(lv_event_t *e);

    ServiceHub &services_;
    xcas::XcasService &cas_;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *menu_ = nullptr;
    lv_obj_t *menu_page_ = nullptr;
    lv_obj_t *editor_panel_ = nullptr;
    lv_obj_t *editor_box_ = nullptr;
    lv_group_t *group_ = nullptr;

    std::vector<ProgramEntry> programs_;
    std::vector<MenuEntry> menu_entries_;
    Mode mode_ = Mode::Menu;
    int selected_ = 0;
    int next_id_ = 1;
    bool mounted_ = false;
    bool ui_ready_ = false;
    bool actions_visible_ = false;
    bool suppress_menu_events_ = false;
    std::string editor_path_;
    std::string run_script_;
    size_t run_pos_ = 0;
};

} // namespace brookesia
