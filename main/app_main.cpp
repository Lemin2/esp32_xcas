#include "brookesia/core/kernel.hpp"

#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "xcas_app";

class XcasApplication {
public:
    XcasApplication() = default;

    void run()
    {
        if (!kernel_.start()) {
            ESP_LOGE(kTag, "brookesia kernel start failed");
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

        ESP_LOGI(kTag, "brookesia GUI started");
    }

private:
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
            kernel_.handleKeyboardState(keyboard_state_.load(std::memory_order_relaxed));
            kernel_.render();
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
            keyboard_state_.store(kernel_.scanKeyboardState(), std::memory_order_relaxed);
            TickType_t d = pdMS_TO_TICKS(10);
            if (d == 0) {
                d = 1;
            }
            vTaskDelay(d);
        }
    }

    brookesia::Kernel kernel_{};
    std::atomic<uint64_t> keyboard_state_{0};
};

} // namespace

extern "C" void app_main(void)
{
    static XcasApplication app;
    app.run();
}
