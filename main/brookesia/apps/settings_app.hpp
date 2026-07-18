#pragma once

#include <array>
#include <cstdint>

#include <string>

#include "lvgl.h"

#include "brookesia/core/app.hpp"
#include "brookesia/core/service_hub.hpp"

namespace brookesia {

// Settings page. Angle mode and precision are applied to the live giac
// context through the shared evaluator service; the About row shows live
// device telemetry.
class SettingsApp final : public App {
public:
    explicit SettingsApp(ServiceHub &services);

    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void releaseUi() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void handleMappedKey(uint32_t key) override;
    void render() override;

private:
    enum class Page : uint8_t {
        Root,
        Calculator,
        Wifi,
        Bluetooth,
        Clock,
        StatusBar,
    };

    enum class Item : uint8_t {
        CalcPage,
        ClockPage,
        StatusBarPage,
        Angle,
        Precision,
        FnSwitch,
        WifiPage,
        BtPage,
        SdCard,
        Memory,
        ClearSession,
        WifiEnable,
        WifiScan,
        WifiConnect,
        WifiSsid,
        WifiPassword,
        WifiStatus,
        BtEnable,
        BtScan,
        BtConnect,
        BtStatus,
        ClockDate,
        ClockTime,
        ClockSync,
        StatusWifiIcon,
        StatusBtIcon,
        StatusMemory,
        StatusMemoryUnit,
        StatusClock,
    };

    struct MenuEntry {
        Item item = Item::Angle;
        Page page = Page::Root;
        lv_obj_t *row = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *value = nullptr;
        lv_obj_t *toggle = nullptr;
        bool disabled = false;
    };

    static constexpr int kMaxMenuEntries = 29;

    void ensureUi();
    void refreshMenu();
    void applyAngle();
    void applyDigits();
    void applyFnSwitch();
    void applyWifi();
    void applyBtHid();
    void scanWifi();
    void connectWifi();
    void scanBluetooth();
    void connectBluetooth();
    void clearSession();
    void adjustClockDate(int dir);
    void adjustClockTime(int dir);
    void applyClockToSystem();
    void syncClockNetwork();
    void applyStatusSettings();
    void beginEdit(Item item);
    void commitEdit();
    void cancelEdit();
    void refreshWifiStatus();
    void syncFocusGroup();
    void focusSelected();
    void applyHorizontalAction(int dir);
    void activateSelected();
    void setCurrentPage(Page page);
    MenuEntry *addMenuEntry(lv_obj_t *page_obj, Page page, Item item, const char *name,
                            bool with_toggle = false);
    MenuEntry *findEntry(Item item);
    MenuEntry *findEntryByRow(lv_obj_t *row);
    bool isSelectable(const MenuEntry &entry) const;
    void ensureSelectionOnPage();
    static void menuEntryEventCb(lv_event_t *e);

    xcas::XcasService &cas_;

    lv_obj_t *root_ = nullptr;
    lv_obj_t *menu_ = nullptr;
    lv_obj_t *root_page_ = nullptr;
    lv_obj_t *calc_page_ = nullptr;
    lv_obj_t *wifi_page_ = nullptr;
    lv_obj_t *bt_page_ = nullptr;
    lv_obj_t *clock_page_ = nullptr;
    lv_obj_t *status_page_ = nullptr;
    lv_group_t *group_ = nullptr;
    std::array<MenuEntry, kMaxMenuEntries> entries_{};
    int entry_count_ = 0;

    Page current_page_ = Page::Root;
    Item selected_item_ = Item::Angle;
    int angle_index_ = 0;  // 0 = RAD, 1 = DEG
    int digits_index_ = 0; // index into digits table
    bool fn_app_switch_enabled_ = false;
    bool wifi_enabled_ = false;
    bool bt_hid_enabled_ = false;
    bool status_show_wifi_ = true;
    bool status_show_bt_ = true;
    bool status_show_memory_ = true;
    bool status_memory_mb_ = false;
    bool status_show_clock_ = true;
    int clock_year_ = 2026;
    int clock_month_ = 1;
    int clock_day_ = 1;
    int clock_hour_ = 0;
    int clock_minute_ = 0;
    bool wifi_connected_ = false;
    bool bt_ready_ = false;
    bool sd_ready_ = false;
    bool editing_ = false;
    Item edit_item_ = Item::WifiSsid;
    char edit_buffer_[65] = {};
    int edit_length_ = 0;
    std::array<char, 48> wifi_state_text_{};
    std::array<char, 48> bt_state_text_{};
    std::array<char, 48> sd_state_text_{};
    uint64_t prev_mask_ = 0;
    uint32_t last_about_ms_ = 0;
    bool ui_ready_ = false;
};

} // namespace brookesia
