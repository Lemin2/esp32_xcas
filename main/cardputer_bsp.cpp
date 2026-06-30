#include "cardputer_bsp.hpp"

#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace board {
namespace {

constexpr char kTag[] = "cardputer_bsp";

constexpr gpio_num_t kPinLcdBl = GPIO_NUM_38;
constexpr gpio_num_t kPinLcdRst = GPIO_NUM_33;
constexpr gpio_num_t kPinLcdDc = GPIO_NUM_34;
constexpr gpio_num_t kPinLcdMosi = GPIO_NUM_35;
constexpr gpio_num_t kPinLcdSclk = GPIO_NUM_36;
constexpr gpio_num_t kPinLcdCs = GPIO_NUM_37;

constexpr gpio_num_t kPinKbdA0 = GPIO_NUM_8;
constexpr gpio_num_t kPinKbdA1 = GPIO_NUM_9;
constexpr gpio_num_t kPinKbdA2 = GPIO_NUM_11;

constexpr gpio_num_t kPinPortATx = GPIO_NUM_2;
constexpr gpio_num_t kPinPortARx = GPIO_NUM_1;

constexpr gpio_num_t kKeyboardCols[] = {
    GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7,
};

constexpr int kDisplayBitsPerPixel = 16;
constexpr spi_host_device_t kDisplayHost = SPI2_HOST;
constexpr uart_port_t kMidiSynthUart = UART_NUM_1;
constexpr int kMidiSynthBaudRate = 31250;
constexpr int kMidiSynthTxBufferLen = 256;
constexpr int kMidiSynthRxBufferLen = 512;
constexpr int kKeyRowCount = 4;
constexpr int kKeyColCount = 14;

constexpr int keyIndex(int row, int col)
{
    return row * kKeyColCount + col;
}

} // namespace

bool CardputerBsp::onColorTransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *user_ctx)
{
    auto *self = static_cast<CardputerBsp *>(user_ctx);
    if (self == nullptr || self->lcd_flush_done_ == nullptr) {
        return false;
    }
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(self->lcd_flush_done_, &high_task_woken);
    return high_task_woken == pdTRUE;
}

void CardputerBsp::initializeDisplay()
{
    gpio_config_t backlight_config = {};
    backlight_config.pin_bit_mask = (1ULL << kPinLcdBl);
    backlight_config.mode = GPIO_MODE_OUTPUT;
    backlight_config.pull_up_en = GPIO_PULLUP_DISABLE;
    backlight_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    backlight_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    ESP_ERROR_CHECK(gpio_set_level(kPinLcdBl, 1));

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = kPinLcdMosi;
    bus_config.miso_io_num = -1;
    bus_config.sclk_io_num = kPinLcdSclk;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = kDisplayWidth * 20 * (kDisplayBitsPerPixel / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(kDisplayHost, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = nullptr;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = kPinLcdCs;
    io_config.dc_gpio_num = kPinLcdDc;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 1;
    io_config.on_color_trans_done = &CardputerBsp::onColorTransferDone;
    io_config.user_ctx = this;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    auto bus_handle = static_cast<esp_lcd_spi_bus_handle_t>(kDisplayHost);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(bus_handle, &io_config, &io_handle));

    lcd_flush_done_ = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(lcd_flush_done_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = kPinLcdRst;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_config.bits_per_pixel = kDisplayBitsPerPixel;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_, 40, 52));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
}

void CardputerBsp::initializeKeyboard()
{
    gpio_config_t selector_config = {};
    selector_config.pin_bit_mask = (1ULL << kPinKbdA0) | (1ULL << kPinKbdA1) | (1ULL << kPinKbdA2);
    selector_config.mode = GPIO_MODE_OUTPUT;
    selector_config.pull_up_en = GPIO_PULLUP_DISABLE;
    selector_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    selector_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&selector_config));

    uint64_t column_mask = 0;
    for (gpio_num_t col : kKeyboardCols) {
        column_mask |= (1ULL << col);
    }
    gpio_config_t column_config = {};
    column_config.pin_bit_mask = column_mask;
    column_config.mode = GPIO_MODE_INPUT;
    column_config.pull_up_en = GPIO_PULLUP_ENABLE;
    column_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    column_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&column_config));

    setKeyboardRow(0);
}

void CardputerBsp::initializeMidiUart()
{
    uart_config_t uart_config = {};
    uart_config.baud_rate = kMidiSynthBaudRate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(kMidiSynthUart, kMidiSynthRxBufferLen, kMidiSynthTxBufferLen, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(kMidiSynthUart, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(kMidiSynthUart, kPinPortATx, kPinPortARx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void CardputerBsp::presentFrame(const uint16_t *framebuffer)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, 0, 0, kDisplayWidth, kDisplayHeight, framebuffer));
    stashScreenshotRegion(0, 0, kDisplayWidth, kDisplayHeight, framebuffer);
    xSemaphoreTake(lcd_flush_done_, portMAX_DELAY);
}

void CardputerBsp::presentArea(int x1, int y1, int x2, int y2, const uint16_t *pixels)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, x1, y1, x2, y2, pixels));
    stashScreenshotRegion(x1, y1, x2, y2, pixels);
    xSemaphoreTake(lcd_flush_done_, portMAX_DELAY);
}

void CardputerBsp::stashScreenshotRegion(int x1, int y1, int x2, int y2, const uint16_t *pixels)
{
    if (screenshot_buf_ == nullptr) {
        return;
    }

    const int w = x2 - x1;
    const int h = y2 - y1;
    for (int row = 0; row < h; ++row) {
        const int dy = y1 + row;
        if (dy < 0 || dy >= kDisplayHeight) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int dx = x1 + col;
            if (dx < 0 || dx >= kDisplayWidth) {
                continue;
            }
            screenshot_buf_[dy * kDisplayWidth + dx] = pixels[row * w + col];
        }
    }
}

bool CardputerBsp::beginScreenshotCapture()
{
    if (screenshot_buf_ == nullptr) {
        screenshot_buf_ = static_cast<uint16_t *>(heap_caps_malloc(
            static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    return screenshot_buf_ != nullptr;
}

void CardputerBsp::endScreenshotCapture()
{
    if (screenshot_buf_ != nullptr) {
        heap_caps_free(screenshot_buf_);
        screenshot_buf_ = nullptr;
    }
}

void CardputerBsp::emitScreenshot()
{
    if (screenshot_buf_ == nullptr) {
        printf("SHOT_ERR no_buffer\n");
        return;
    }

    static const char kB64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const auto *bytes = reinterpret_cast<const uint8_t *>(screenshot_buf_);
    const size_t total = static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t);

    printf("SHOT_BEGIN w=%d h=%d fmt=rgb565le bytes=%u\n", kDisplayWidth, kDisplayHeight,
           static_cast<unsigned>(total));

    // 16 groups of 3 input bytes -> 64 base64 chars per emitted line.
    char line[65];
    size_t i = 0;
    while (i < total) {
        int n = 0;
        for (int group = 0; group < 16 && i < total; ++group) {
            const uint32_t b0 = bytes[i];
            const uint32_t b1 = (i + 1 < total) ? bytes[i + 1] : 0;
            const uint32_t b2 = (i + 2 < total) ? bytes[i + 2] : 0;
            const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
            line[n++] = kB64[(triple >> 18) & 0x3F];
            line[n++] = kB64[(triple >> 12) & 0x3F];
            line[n++] = (i + 1 < total) ? kB64[(triple >> 6) & 0x3F] : '=';
            line[n++] = (i + 2 < total) ? kB64[triple & 0x3F] : '=';
            i += 3;
        }
        line[n] = '\0';
        printf("SHOT:%s\n", line);
    }

    printf("SHOT_END\n");
}

uint64_t CardputerBsp::scanKeyboardState()
{
    uint64_t pressed = 0;
    for (uint8_t row = 0; row < 8; ++row) {
        setKeyboardRow(row);
        esp_rom_delay_us(5);

        for (uint8_t col = 0; col < 7; ++col) {
            if (gpio_get_level(kKeyboardCols[col]) != 0) {
                continue;
            }

            const int scanned_y = row > 3 ? (row - 4) : row;
            const int logical_y = 3 - scanned_y;
            const int logical_x = row > 3 ? (col * 2) : (col * 2 + 1);
            if (logical_y >= 0 && logical_y < kKeyRowCount && logical_x >= 0 && logical_x < kKeyColCount) {
                pressed |= (1ULL << keyIndex(logical_y, logical_x));
            }
        }
    }
    return pressed;
}

void CardputerBsp::writeMidi(const uint8_t *data, std::size_t len)
{
    const int written = uart_write_bytes(kMidiSynthUart, data, len);
    if (written != static_cast<int>(len)) {
        ESP_LOGW(kTag, "PORT.A short write: %d", written);
    }
}

int CardputerBsp::readMidiByte(uint8_t *byte, TickType_t timeout)
{
    return uart_read_bytes(kMidiSynthUart, byte, 1, timeout);
}

void CardputerBsp::setKeyboardRow(uint8_t row)
{
    gpio_set_level(kPinKbdA0, row & 0x01U);
    gpio_set_level(kPinKbdA1, (row >> 1) & 0x01U);
    gpio_set_level(kPinKbdA2, (row >> 2) & 0x01U);
}

} // namespace board
