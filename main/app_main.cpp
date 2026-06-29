#include "cardputer_bsp.hpp"
#include "xcas_service.hpp"
#include "xcas_ui.hpp"

#include <array>
#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" int access(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return -1;
}

namespace {

constexpr char kTag[] = "xcas_app";

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

class XcasApplication {
public:
    XcasApplication() : ui_(board_, service_) {}

    void run()
    {
        board_.initializeDisplay();
        board_.initializeKeyboard();
        drawBootSplash();

        if (!service_.start()) {
            ESP_LOGE(kTag, "xcas service start failed");
            return;
        }

        const BaseType_t ui_ok = xTaskCreatePinnedToCore(
            &XcasApplication::uiTaskEntry, "xcas_ui_task", 12 * 1024, this, 4, nullptr, 1);
        if (ui_ok != pdPASS) {
            ESP_LOGE(kTag, "failed to create xcas_ui_task");
            return;
        }

        const BaseType_t kbd_ok = xTaskCreatePinnedToCore(
            &XcasApplication::keyboardTaskEntry, "xcas_kbd_task", 4 * 1024, this, 4, nullptr, 1);
        if (kbd_ok != pdPASS) {
            ESP_LOGE(kTag, "failed to create xcas_kbd_task");
            return;
        }

        ESP_LOGI(kTag, "xcas GUI started");
    }

private:
    void drawBootSplash()
    {
        std::array<uint16_t, board::CardputerBsp::kDisplayWidth> line{};
        for (int y = 0; y < board::CardputerBsp::kDisplayHeight; ++y) {
            uint16_t color = rgb565(20, 20, 20);
            if (y < board::CardputerBsp::kDisplayHeight / 3) {
                color = rgb565(220, 40, 40);
            } else if (y < (board::CardputerBsp::kDisplayHeight * 2) / 3) {
                color = rgb565(40, 190, 70);
            } else {
                color = rgb565(40, 110, 220);
            }
            line.fill(color);
            board_.presentArea(0, y, board::CardputerBsp::kDisplayWidth, y + 1, line.data());
        }
        ESP_LOGI(kTag, "boot splash rendered");
    }

    static void uiTaskEntry(void *ctx)
    {
        static_cast<XcasApplication *>(ctx)->uiTask();
    }

    static void keyboardTaskEntry(void *ctx)
    {
        static_cast<XcasApplication *>(ctx)->keyboardTask();
    }

    void uiTask()
    {
        ESP_LOGI(kTag, "ui task alive");
        TickType_t last_wake = xTaskGetTickCount();
        for (;;) {
            ui_.handleKeyboardState(keyboard_state_.load(std::memory_order_relaxed));
            ui_.render();
            TickType_t period = pdMS_TO_TICKS(16);
            if (period == 0) {
                period = 1;
            }
            vTaskDelayUntil(&last_wake, period);
        }
    }

    void keyboardTask()
    {
        for (;;) {
            keyboard_state_.store(board_.scanKeyboardState(), std::memory_order_relaxed);
            TickType_t d = pdMS_TO_TICKS(10);
            if (d == 0) {
                d = 1;
            }
            vTaskDelay(d);
        }
    }

    board::CardputerBsp board_{};
    xcas::XcasService service_{};
    xcas::XcasUi ui_;
    std::atomic<uint64_t> keyboard_state_{0};
};

} // namespace

extern "C" void app_main(void)
{
    static XcasApplication app;
    app.run();
}
