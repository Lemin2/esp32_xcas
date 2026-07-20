#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_lcd_types.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

namespace board {

class CardputerBsp {
public:
    static constexpr int kDisplayWidth = 240;
    static constexpr int kDisplayHeight = 135;

    void initializeDisplay();
    void initializeKeyboard();
    void initializeMidiUart();
    int displayWidth() const;
    int displayHeight() const;
    int statusBarHeight() const;
    bool usesExternalLvglPort() const;
    bool hasTouchInput() const;
    bool hasPhysicalKeyboard() const;
    lv_indev_t *touchInputDevice() const;
    bool lockLvgl(uint32_t timeout_ms);
    void unlockLvgl();
    void presentFrame(const uint16_t *framebuffer);
    void presentArea(int x1, int y1, int x2, int y2, const uint16_t *pixels);
    uint64_t scanKeyboardState();
    void updateKeyboard();
    uint64_t keyboardState() const;
    bool fnActive() const;
    bool shiftActive() const;
    void pushMappedKey(uint32_t key);
    bool popMappedKey(uint32_t &key);
    void writeMidi(const uint8_t *data, std::size_t len);
    int readMidiByte(uint8_t *byte, TickType_t timeout);

    // Screenshot support (debug aid). The shadow buffer is persistent and
    // continuously updated from display flushes. begin/end are lightweight
    // delimiters used by callers before/after emitScreenshot(). Safe on UI task.
    bool beginScreenshotCapture();
    void emitScreenshot();
    void endScreenshotCapture();

private:
    static bool onColorTransferDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
    void setKeyboardRow(uint8_t row);
    void stashScreenshotRegion(int x1, int y1, int x2, int y2, const uint16_t *pixels);

    esp_lcd_panel_handle_t panel_ = nullptr;
    SemaphoreHandle_t lcd_flush_done_ = nullptr;
    uint16_t *screenshot_buf_ = nullptr;
    lv_display_t *external_display_ = nullptr;
    lv_indev_t *touch_indev_ = nullptr;
    lv_indev_t *usb_hid_keyboard_indev_ = nullptr;
    std::atomic<uint64_t> keyboard_state_{0};
    std::atomic<uint64_t> previous_keyboard_state_{0};
    std::atomic<bool> fn_active_{false};
    std::atomic<bool> shift_active_{false};
    bool fn_latched_ = false;
    bool shift_latched_ = false;
    uint64_t last_fn_toggle_us_ = 0;
    uint64_t last_shift_toggle_us_ = 0;
    std::array<uint32_t, 64> mapped_keys_{};
    std::atomic<uint8_t> mapped_head_{0};
    std::atomic<uint8_t> mapped_tail_{0};
};

} // namespace board
