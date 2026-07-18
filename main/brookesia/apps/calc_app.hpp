#pragma once

#include "brookesia/core/app.hpp"
#include "brookesia/core/service_hub.hpp"

#include "xcas_ui.hpp"

namespace brookesia {

class CalcApp final : public App {
public:
    explicit CalcApp(ServiceHub &services);

    bool init() override;
    void onFocus() override;
    void onBlur() override;
    void releaseUi() override;
    void handleKeyboardState(uint64_t pressedMask) override;
    void handleMappedKey(uint32_t key) override;
    void render() override;
    void debugSubmitFormula(const std::string &formula);
    void debugEmitFormulaImage(const std::string &formula);

private:
    xcas::XcasUi ui_;
};

} // namespace brookesia
