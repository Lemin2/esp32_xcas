#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "brookesia/core/kernel.hpp"
#include "xcas_service.hpp"
#include "mathlayout/render/text_renderer.hpp"

namespace {

constexpr char kTag[] = "xcas_app";
#if defined(CONFIG_XCAS_BOARD_TAB5) && CONFIG_XCAS_BOARD_TAB5
constexpr bool kBoardTab5 = true;
#else
constexpr bool kBoardTab5 = false;
#endif

bool parseDebugKey(const char *token, uint32_t &out)
{
    if (token == nullptr || token[0] == '\0') {
        return false;
    }

    if (std::strcmp(token, "UP") == 0) {
        out = LV_KEY_UP;
        return true;
    }
    if (std::strcmp(token, "DOWN") == 0) {
        out = LV_KEY_DOWN;
        return true;
    }
    if (std::strcmp(token, "LEFT") == 0) {
        out = LV_KEY_LEFT;
        return true;
    }
    if (std::strcmp(token, "RIGHT") == 0) {
        out = LV_KEY_RIGHT;
        return true;
    }
    if (std::strcmp(token, "ENTER") == 0) {
        out = LV_KEY_ENTER;
        return true;
    }
    if (std::strcmp(token, "ESC") == 0) {
        out = LV_KEY_ESC;
        return true;
    }
    if (std::strcmp(token, "SPACE") == 0) {
        out = static_cast<uint32_t>(' ');
        return true;
    }

    if (token[1] == '\0') {
        out = static_cast<uint32_t>(token[0]);
        return true;
    }

    return false;
}

void initializeAutomationStdio()
{
    fflush(stdout);
    fsync(fileno(stdout));

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);

    usb_serial_jtag_driver_config_t jtag_config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };
    const esp_err_t install_err = usb_serial_jtag_driver_install(&jtag_config);
    if (install_err != ESP_OK && install_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "usb serial jtag driver install failed: %s", esp_err_to_name(install_err));
    } else {
        usb_serial_jtag_vfs_use_driver();
        ESP_LOGI(kTag, "automation console on USB-SERIAL-JTAG");
    }
#elif defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
#if SOC_UART_SUPPORT_REF_TICK
        .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_XTAL_CLK
        .source_clk = UART_SCLK_XTAL,
#endif
    };
    const uart_port_t uart_port = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
    const esp_err_t install_err = uart_driver_install(uart_port, 256, 0, 0, nullptr, 0);
    if (install_err != ESP_OK && install_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "uart console driver install failed: %s", esp_err_to_name(install_err));
    }
    ESP_ERROR_CHECK(uart_param_config(uart_port, &uart_config));
    uart_vfs_dev_use_driver(uart_port);
    ESP_LOGI(kTag, "automation console on UART%d @ %d", CONFIG_ESP_CONSOLE_UART_NUM, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
#endif

    setvbuf(stdin, nullptr, _IONBF, 0);
}

class XcasApplication {
public:
    XcasApplication() = default;

    void run()
    {
        initializeAutomationStdio();

        if (!kernel_.start()) {
            ESP_LOGE(kTag, "brookesia kernel start failed");
            return;
        }

        const BaseType_t ui_ok = xTaskCreate(
            &XcasApplication::uiTaskEntry, "xcas_ui_task", 12 * 1024, this, 3, nullptr);
        if (ui_ok != pdPASS) {
            ESP_LOGE(kTag, "failed to create xcas_ui_task");
            return;
        }

#if CONFIG_XCAS_HAS_PHYSICAL_KEYBOARD
        const BaseType_t kbd_ok = xTaskCreatePinnedToCore(
            &XcasApplication::keyboardTaskEntry, "xcas_kbd_task", 4 * 1024, this, 4, nullptr, 1);
        if (kbd_ok != pdPASS) {
            ESP_LOGE(kTag, "failed to create xcas_kbd_task");
            return;
        }
#endif

        const BaseType_t serial_ok = xTaskCreatePinnedToCore(
            &XcasApplication::serialTaskEntry, "xcas_serial_task", 6 * 1024, this, 3, nullptr, 0);
        if (serial_ok != pdPASS) {
            ESP_LOGE(kTag, "failed to create xcas_serial_task");
            return;
        }

        ESP_LOGI(kTag, "brookesia GUI started");
        ESP_LOGI(kTag, "serial automation ready: ML SUBMIT <expr> | ML SHOT | ML RUN <expr>");
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

    static void serialTaskEntry(void *ctx)
    {
        static_cast<XcasApplication *>(ctx)->serialTask();
    }

    void uiTask()
    {
        ESP_LOGI(kTag, "ui task alive");
        for (;;) {
            if (kernel_.lockLvgl(50)) {
                kernel_.setModifierState(kernel_.fnActive(), kernel_.shiftActive());
                kernel_.handleKeyboardState(kernel_.keyboardState());
                uint32_t key = 0;
                while (kernel_.popMappedKey(key)) {
                    kernel_.handleMappedKey(key);
                }
                processDebugCommands();
                kernel_.render();
                kernel_.unlockLvgl();
            }
            TickType_t period = pdMS_TO_TICKS(kBoardTab5 ? 33 : 16);
            if (period == 0) {
                period = 1;
            }
            vTaskDelay(period);
        }
    }

    void keyboardTask()
    {
        for (;;) {
            kernel_.updateKeyboard();
            TickType_t d = pdMS_TO_TICKS(10);
            if (d == 0) {
                d = 1;
            }
            vTaskDelay(d);
        }
    }

    enum class DebugCmdType : uint8_t {
        Submit,
        Screenshot,
        Run,
        RenderShot,
        Key,
        PreviewShot,
    };

    struct DebugCommand {
        DebugCmdType type = DebugCmdType::Screenshot;
        std::array<char, 192> payload{};
    };

    void enqueueDebugCommand(DebugCmdType type, const char *payload)
    {
        const uint8_t tail = debug_tail_.load(std::memory_order_relaxed);
        uint8_t head = debug_head_.load(std::memory_order_acquire);
        const uint8_t next_tail = static_cast<uint8_t>((tail + 1U) % debug_cmds_.size());

        if (next_tail == head) {
            head = static_cast<uint8_t>((head + 1U) % debug_cmds_.size());
            debug_head_.store(head, std::memory_order_release);
        }

        DebugCommand cmd{};
        cmd.type = type;
        if (payload != nullptr) {
            std::snprintf(cmd.payload.data(), cmd.payload.size(), "%s", payload);
        }
        debug_cmds_[tail] = cmd;
        debug_tail_.store(next_tail, std::memory_order_release);
    }

    bool dequeueDebugCommand(DebugCommand &out)
    {
        const uint8_t head = debug_head_.load(std::memory_order_relaxed);
        const uint8_t tail = debug_tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }

        out = debug_cmds_[head];
        const uint8_t next_head = static_cast<uint8_t>((head + 1U) % debug_cmds_.size());
        debug_head_.store(next_head, std::memory_order_release);
        return true;
    }

    void processDebugCommands()
    {
        DebugCommand cmd{};
        while (dequeueDebugCommand(cmd)) {
            switch (cmd.type) {
            case DebugCmdType::Submit:
                kernel_.debugSubmitFormula(cmd.payload.data());
                break;
            case DebugCmdType::Screenshot:
                kernel_.requestScreenshot();
                break;
            case DebugCmdType::Run:
                kernel_.debugSubmitFormula(cmd.payload.data());
                pending_screenshot_frames_ = 45;
                break;
            case DebugCmdType::RenderShot:
                kernel_.debugEmitFormulaImage(cmd.payload.data());
                break;
            case DebugCmdType::Key: {
                uint32_t key = 0;
                if (parseDebugKey(cmd.payload.data(), key)) {
                    kernel_.pushMappedKey(key);
                }
                break;
            }
            case DebugCmdType::PreviewShot:
                // Sequence over multiple UI frames:
                // 1) move to newest output then input row, 2) enter preview, 3) screenshot, 4) close preview.
                pending_preview_stage_ = 1;
                pending_preview_frames_ = 2;
                break;
            }
        }

        if (pending_screenshot_frames_ > 0) {
            --pending_screenshot_frames_;
            if (pending_screenshot_frames_ == 0) {
                kernel_.requestScreenshot();
            }
        }

        if (pending_preview_stage_ != 0 && pending_preview_frames_ > 0) {
            --pending_preview_frames_;
            if (pending_preview_frames_ == 0) {
                switch (pending_preview_stage_) {
                case 1:
                    kernel_.pushMappedKey(LV_KEY_UP);
                    pending_preview_stage_ = 2;
                    pending_preview_frames_ = 2;
                    break;
                case 2:
                    kernel_.pushMappedKey(LV_KEY_UP);
                    pending_preview_stage_ = 3;
                    pending_preview_frames_ = 2;
                    break;
                case 3:
                    kernel_.pushMappedKey(static_cast<uint32_t>(' '));
                    pending_preview_stage_ = 4;
                    pending_preview_frames_ = 3;
                    break;
                case 4:
                    kernel_.requestScreenshot();
                    pending_preview_stage_ = 5;
                    pending_preview_frames_ = 2;
                    break;
                case 5:
                    kernel_.pushMappedKey(static_cast<uint32_t>(' '));
                    pending_preview_stage_ = 0;
                    pending_preview_frames_ = 0;
                    break;
                default:
                    pending_preview_stage_ = 0;
                    pending_preview_frames_ = 0;
                    break;
                }
            }
        }
    }

    void serialTask()
    {
        static constexpr size_t kBufSize = 256;
        char line[kBufSize] = {0};
        size_t len = 0;

        for (;;) {
            const int c = std::getchar();
            if (c < 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            if (c == '\r' || c == '\n') {
                line[len] = '\0';
                if (len > 0) {
                    handleSerialLine(line);
                }
                len = 0;
                continue;
            }

            if (len + 1U < kBufSize) {
                line[len++] = static_cast<char>(c);
            }
        }
    }

    void handleSerialLine(const char *line)
    {
        if (line == nullptr) {
            return;
        }

        if (std::strncmp(line, "ML SUBMIT ", 10) == 0) {
            enqueueDebugCommand(DebugCmdType::Submit, line + 10);
            std::printf("ML_ACK SUBMIT\n");
            return;
        }
        if (std::strcmp(line, "ML SHOT") == 0) {
            enqueueDebugCommand(DebugCmdType::Screenshot, nullptr);
            std::printf("ML_ACK SHOT\n");
            return;
        }
        if (std::strncmp(line, "ML RUN ", 7) == 0) {
            enqueueDebugCommand(DebugCmdType::Run, line + 7);
            std::printf("ML_ACK RUN\n");
            return;
        }
        if (std::strncmp(line, "ML RENDER ", 10) == 0) {
            const std::string expr(line + 10);
            const xcas::mathlayout::TextBox box = xcas::mathlayout::renderText(expr);
            std::printf("ML_RENDER_BEGIN\n");
            for (const auto &l : box.lines) {
                std::printf("ML_LINE:%s\n", l.c_str());
            }
            std::printf("ML_RENDER_END baseline=%d height=%d\n",
                        box.baseline, box.height());
            return;
        }
        if (std::strncmp(line, "ML RENDER_SHOT ", 15) == 0) {
            enqueueDebugCommand(DebugCmdType::RenderShot, line + 15);
            std::printf("ML_ACK RENDER_SHOT\n");
            return;
        }
        if (std::strncmp(line, "ML KEY ", 7) == 0) {
            enqueueDebugCommand(DebugCmdType::Key, line + 7);
            std::printf("ML_ACK KEY\n");
            return;
        }
        if (std::strncmp(line, "ML PREVIEW_SHOT ", 16) == 0) {
            enqueueDebugCommand(DebugCmdType::RenderShot, line + 16);
            std::printf("ML_ACK PREVIEW_RENDER_SHOT\n");
            return;
        }
        if (std::strcmp(line, "ML PREVIEW_SHOT") == 0) {
            enqueueDebugCommand(DebugCmdType::PreviewShot, nullptr);
            std::printf("ML_ACK PREVIEW_SHOT\n");
            return;
        }
        if (std::strcmp(line, "ML HELP") == 0) {
            std::printf("ML_HELP Commands: ML SUBMIT <expr> | ML SHOT | ML RUN <expr> | ML RENDER <expr> | ML RENDER_SHOT <expr> | ML KEY <UP|DOWN|LEFT|RIGHT|ENTER|ESC|SPACE|char> | ML PREVIEW_SHOT [expr]\n");
            return;
        }
    }

    brookesia::Kernel kernel_{};
    std::array<DebugCommand, 16> debug_cmds_{};
    std::atomic<uint8_t> debug_head_{0};
    std::atomic<uint8_t> debug_tail_{0};
    int pending_screenshot_frames_ = 0;
    int pending_preview_stage_ = 0;
    int pending_preview_frames_ = 0;
};

} // namespace

extern "C" void app_main(void)
{
    static XcasApplication app;
    app.run();
}
