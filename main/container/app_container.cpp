#include "container/app_container.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "esp_log.h"

#include "xcas_ui.hpp"

namespace container {
namespace {

constexpr char kTag[] = "app_container";

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

class CalcApp final : public AppLifecycle {
public:
    CalcApp(board::IBsp &board, xcas::XcasService &service) : ui_(board, service) {}

    bool init() override
    {
        return true;
    }

    void handleMappedKey(uint32_t key) override
    {
        ui_.enqueueInputKey(key);
    }

    void render() override
    {
        ui_.render();
    }

private:
    xcas::XcasUi ui_;
};

} // namespace

AppContainer::AppContainer()
        : board_(board::createSelectedBsp()),
            services_(*board_, casService_)
{
}

bool AppContainer::start()
{
    board_->initializeDisplay();
    board_->initializeKeyboard();
    drawBootSplash();

    if (!casService_.start()) {
        ESP_LOGE(kTag, "cas service start failed");
        return false;
    }

    calcApp_ = std::make_unique<CalcApp>(services_.board(), services_.casService());
    if (!calcApp_->init()) {
        ESP_LOGE(kTag, "calc app init failed");
        calcApp_.reset();
        return false;
    }

    calcApp_->onFocus();
    router_.set(AppRoute::Calc);
    return true;
}

uint64_t AppContainer::scanKeyboardState()
{
    board_->updateKeyboard();
    return board_->keyboardState();
}

void AppContainer::handleKeyboardState(uint64_t pressedMask)
{
    (void)pressedMask;
}

void AppContainer::render()
{
    if (calcApp_) {
        uint32_t key = 0;
        while (board_->popMappedKey(key)) {
            calcApp_->handleMappedKey(key);
        }
        calcApp_->render();
    }
}

void AppContainer::drawBootSplash()
{
    if (board_->usesExternalLvglPort()) {
        ESP_LOGI(kTag, "boot splash skipped; external LVGL port active");
        return;
    }

    std::vector<uint16_t> line(static_cast<size_t>(board_->displayWidth()));
    for (int y = 0; y < board_->displayHeight(); ++y) {
        uint16_t color = rgb565(20, 20, 20);
        if (y < board_->displayHeight() / 3) {
            color = rgb565(220, 40, 40);
        } else if (y < (board_->displayHeight() * 2) / 3) {
            color = rgb565(40, 190, 70);
        } else {
            color = rgb565(40, 110, 220);
        }
        std::fill(line.begin(), line.end(), color);
        board_->presentArea(0, y, board_->displayWidth(), y + 1, line.data());
    }
    ESP_LOGI(kTag, "boot splash rendered");
}

} // namespace container
