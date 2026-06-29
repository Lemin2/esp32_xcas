#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_lcd_types.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace board {

class CardputerBsp {
public:
    static constexpr int kDisplayWidth = 240;
    static constexpr int kDisplayHeight = 135;

    void initializeDisplay();
    void initializeKeyboard();
    void initializeMidiUart();
    void presentFrame(const uint16_t *framebuffer);
    void presentArea(int x1, int y1, int x2, int y2, const uint16_t *pixels);
    uint64_t scanKeyboardState();
    void writeMidi(const uint8_t *data, std::size_t len);
    int readMidiByte(uint8_t *byte, TickType_t timeout);

private:
    static bool onColorTransferDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
    void setKeyboardRow(uint8_t row);

    esp_lcd_panel_handle_t panel_ = nullptr;
    SemaphoreHandle_t lcd_flush_done_ = nullptr;
};

} // namespace board
