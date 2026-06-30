#include "container/app_container.hpp"

#include <array>

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
    CalcApp(board::CardputerBsp &board, xcas::XcasService &service) : ui_(board, service) {}

    bool init() override
    {
        return true;
    }

    void handleKeyboardState(uint64_t pressedMask) override
    {
        ui_.handleKeyboardState(pressedMask);
    }

    void render() override
    {
        ui_.render();
    }

private:
    xcas::XcasUi ui_;
};

} // namespace

AppContainer::AppContainer() : services_(board_, casService_) {}

bool AppContainer::start()
{
    board_.initializeDisplay();
    board_.initializeKeyboard();
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
    return board_.scanKeyboardState();
}

void AppContainer::handleKeyboardState(uint64_t pressedMask)
{
    if (calcApp_) {
        calcApp_->handleKeyboardState(pressedMask);
    }
}

void AppContainer::render()
{
    if (calcApp_) {
        calcApp_->render();
    }
}

void AppContainer::drawBootSplash()
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

} // namespace container
