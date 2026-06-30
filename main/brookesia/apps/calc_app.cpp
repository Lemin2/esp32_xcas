#include "brookesia/apps/calc_app.hpp"

namespace brookesia {

CalcApp::CalcApp(ServiceHub &services) : ui_(services.board(), services.casService())
{
}

bool CalcApp::init()
{
    return true;
}

void CalcApp::onFocus()
{
    ui_.show();
}

void CalcApp::onBlur()
{
    ui_.hide();
}

void CalcApp::handleKeyboardState(uint64_t pressedMask)
{
    ui_.handleKeyboardState(pressedMask);
}

void CalcApp::render()
{
    ui_.render();
}

} // namespace brookesia
