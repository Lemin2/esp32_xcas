#include "brookesia/core/kernel.hpp"

#include <atomic>
#include <array>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "xcas_app";
constexpr uint64_t kFnBit = (1ULL << 28);
constexpr uint64_t kShiftBit = (1ULL << 29);

struct KeyLabel {
    const char *base;
    const char *shifted;
};

constexpr KeyLabel kKeyMap[4][14] = {
    {
        {"`", "~"}, {"1", "!"}, {"2", "@"}, {"3", "#"}, {"4", "$"}, {"5", "%"}, {"6", "^"},
        {"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"}, {"-", "_"}, {"=", "+"}, {"Backspace", "Backspace"},
    },
    {
        {"Tab", "Tab"}, {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"}, {"t", "T"}, {"y", "Y"},
        {"u", "U"}, {"i", "I"}, {"o", "O"}, {"p", "P"}, {"[", "{"}, {"]", "}"}, {"\\", "|"},
    },
    {
        {"Fn", "Fn"}, {"Shift", "Shift"}, {"a", "A"}, {"s", "S"}, {"d", "D"}, {"f", "F"}, {"g", "G"},
        {"h", "H"}, {"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"}, {"'", "\""}, {"Enter", "Enter"},
    },
    {
        {"Ctrl", "Ctrl"}, {"Opt", "Opt"}, {"Alt", "Alt"}, {"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"},
        {"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"}, {".", ">"}, {"/", "?"}, {"Space", "Space"},
    },
};

constexpr int keyRowCount = 4;
constexpr int keyColCount = 14;

bool keyIs(const KeyLabel &key, const char *name)
{
    return std::strcmp(key.base, name) == 0;
}

bool mapToLvglKey(int keyIndex, bool fnActive, bool shiftActive, uint32_t &out)
{
    const int row = keyIndex / keyColCount;
    const int col = keyIndex % keyColCount;
    if (row < 0 || row >= keyRowCount || col < 0 || col >= keyColCount) {
        return false;
    }

    const KeyLabel &key = kKeyMap[row][col];
    if (keyIs(key, "Fn") || keyIs(key, "Shift") || keyIs(key, "Ctrl") || keyIs(key, "Opt") || keyIs(key, "Alt")) {
        return false;
    }

    if (fnActive) {
        if (keyIs(key, "q") || keyIs(key, "w") || keyIs(key, "p")) {
            return false;
        }
        if (keyIs(key, ";")) {
            out = LV_KEY_UP;
            return true;
        }
        if (keyIs(key, ".")) {
            out = LV_KEY_DOWN;
            return true;
        }
        if (keyIs(key, ",")) {
            out = LV_KEY_LEFT;
            return true;
        }
        if (keyIs(key, "/")) {
            out = LV_KEY_RIGHT;
            return true;
        }
        if (keyIs(key, "Backspace")) {
            out = LV_KEY_DEL;
            return true;
        }
        if (keyIs(key, "`")) {
            out = LV_KEY_ESC;
            return true;
        }
    }

    if (keyIs(key, "Enter")) {
        out = LV_KEY_ENTER;
        return true;
    }
    if (keyIs(key, "Backspace")) {
        out = LV_KEY_BACKSPACE;
        return true;
    }
    if (keyIs(key, "Tab")) {
        out = '\t';
        return true;
    }
    if (keyIs(key, "Space")) {
        out = ' ';
        return true;
    }

    const char *label = shiftActive ? key.shifted : key.base;
    if (label != nullptr && label[0] != '\0' && label[1] == '\0') {
        out = static_cast<uint32_t>(static_cast<unsigned char>(label[0]));
        return true;
    }

    return false;
}

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
            uint32_t key = 0;
            while (popMappedKey(key)) {
                kernel_.handleMappedKey(key);
            }
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
        uint64_t previous_mask = 0;
        bool fn_latched = false;
        bool shift_latched = false;
        uint64_t last_fn_toggle_us = 0;
        uint64_t last_shift_toggle_us = 0;
        constexpr uint64_t kToggleDebounceUs = 120000;
        for (;;) {
            const uint64_t raw_mask = kernel_.scanKeyboardState();
            keyboard_state_.store(raw_mask, std::memory_order_relaxed);

            const uint64_t newly_pressed = raw_mask & ~previous_mask;
            const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());

            if ((newly_pressed & kFnBit) != 0U) {
                if (now_us - last_fn_toggle_us >= kToggleDebounceUs) {
                    fn_latched = !fn_latched;
                    last_fn_toggle_us = now_us;
                }
            }
            if ((newly_pressed & kShiftBit) != 0U) {
                if (now_us - last_shift_toggle_us >= kToggleDebounceUs) {
                    shift_latched = !shift_latched;
                    last_shift_toggle_us = now_us;
                }
            }

            const bool fn_active = ((raw_mask & kFnBit) != 0U) || fn_latched;
            const bool shift_active = ((raw_mask & kShiftBit) != 0U) || shift_latched;

            for (int idx = 0; idx < 56; ++idx) {
                const uint64_t bit = (1ULL << idx);
                if ((newly_pressed & bit) == 0U) {
                    continue;
                }

                uint32_t mapped = 0;
                if (mapToLvglKey(idx, fn_active, shift_active, mapped)) {
                    pushMappedKey(mapped);
                }
            }

            previous_mask = raw_mask;
            TickType_t d = pdMS_TO_TICKS(10);
            if (d == 0) {
                d = 1;
            }
            vTaskDelay(d);
        }
    }

    void pushMappedKey(uint32_t key)
    {
        const uint8_t tail = mapped_tail_.load(std::memory_order_relaxed);
        uint8_t head = mapped_head_.load(std::memory_order_acquire);
        const uint8_t next_tail = static_cast<uint8_t>((tail + 1U) % mapped_keys_.size());

        if (next_tail == head) {
            head = static_cast<uint8_t>((head + 1U) % mapped_keys_.size());
            mapped_head_.store(head, std::memory_order_release);
        }

        mapped_keys_[tail] = key;
        mapped_tail_.store(next_tail, std::memory_order_release);
    }

    bool popMappedKey(uint32_t &key)
    {
        const uint8_t head = mapped_head_.load(std::memory_order_relaxed);
        const uint8_t tail = mapped_tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }

        key = mapped_keys_[head];
        const uint8_t next_head = static_cast<uint8_t>((head + 1U) % mapped_keys_.size());
        mapped_head_.store(next_head, std::memory_order_release);
        return true;
    }

    brookesia::Kernel kernel_{};
    std::atomic<uint64_t> keyboard_state_{0};
    std::array<uint32_t, 64> mapped_keys_{};
    std::atomic<uint8_t> mapped_head_{0};
    std::atomic<uint8_t> mapped_tail_{0};
};

} // namespace

extern "C" void app_main(void)
{
    static XcasApplication app;
    app.run();
}
