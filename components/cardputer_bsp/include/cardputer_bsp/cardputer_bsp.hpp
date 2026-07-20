#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

namespace board {

class IBsp {
public:
    static constexpr int kDisplayWidth = 240;
    static constexpr int kDisplayHeight = 135;

    virtual ~IBsp() = default;

    virtual void initializeDisplay() = 0;
    virtual void initializeKeyboard() = 0;
    virtual void initializeMidiUart() = 0;
    virtual int displayWidth() const = 0;
    virtual int displayHeight() const = 0;
    virtual int statusBarHeight() const = 0;
    virtual bool usesExternalLvglPort() const = 0;
    virtual bool hasTouchInput() const = 0;
    virtual bool hasPhysicalKeyboard() const = 0;
    virtual lv_indev_t *touchInputDevice() const = 0;
    virtual bool lockLvgl(uint32_t timeout_ms) = 0;
    virtual void unlockLvgl() = 0;
    virtual void presentFrame(const uint16_t *framebuffer) = 0;
    virtual void presentArea(int x1, int y1, int x2, int y2, const uint16_t *pixels) = 0;
    virtual uint64_t scanKeyboardState() = 0;
    virtual void updateKeyboard() = 0;
    virtual uint64_t keyboardState() const = 0;
    virtual bool fnActive() const = 0;
    virtual bool shiftActive() const = 0;
    virtual void pushMappedKey(uint32_t key) = 0;
    virtual bool popMappedKey(uint32_t &key) = 0;
    virtual void writeMidi(const uint8_t *data, std::size_t len) = 0;
    virtual int readMidiByte(uint8_t *byte, TickType_t timeout) = 0;
    virtual bool beginScreenshotCapture() = 0;
    virtual void emitScreenshot() = 0;
    virtual void endScreenshotCapture() = 0;
};

class CardputerBsp final : public IBsp {
public:
    void initializeDisplay() override;
    void initializeKeyboard() override;
    void initializeMidiUart() override;
    int displayWidth() const override;
    int displayHeight() const override;
    int statusBarHeight() const override;
    bool usesExternalLvglPort() const override;
    bool hasTouchInput() const override;
    bool hasPhysicalKeyboard() const override;
    lv_indev_t *touchInputDevice() const override;
    bool lockLvgl(uint32_t timeout_ms) override;
    void unlockLvgl() override;
    void presentFrame(const uint16_t *framebuffer) override;
    void presentArea(int x1, int y1, int x2, int y2, const uint16_t *pixels) override;
    uint64_t scanKeyboardState() override;
    void updateKeyboard() override;
    uint64_t keyboardState() const override;
    bool fnActive() const override;
    bool shiftActive() const override;
    void pushMappedKey(uint32_t key) override;
    bool popMappedKey(uint32_t &key) override;
    void writeMidi(const uint8_t *data, std::size_t len) override;
    int readMidiByte(uint8_t *byte, TickType_t timeout) override;
    bool beginScreenshotCapture() override;
    void emitScreenshot() override;
    void endScreenshotCapture() override;

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

std::unique_ptr<IBsp> createSelectedBsp();

} // namespace board