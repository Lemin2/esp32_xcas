#pragma once

#include <cstdint>

namespace container {

enum class AppRoute : uint8_t {
    Calc = 0,
    Graph,
    Files,
    Project,
    Settings,
};

class Router {
public:
    AppRoute current() const
    {
        return current_;
    }

    void set(AppRoute route)
    {
        current_ = route;
    }

    void next()
    {
        current_ = static_cast<AppRoute>((static_cast<uint8_t>(current_) + 1U) % 5U);
    }

    void prev()
    {
        const uint8_t value = static_cast<uint8_t>(current_);
        current_ = static_cast<AppRoute>((value + 4U) % 5U);
    }

private:
    AppRoute current_ = AppRoute::Calc;
};

} // namespace container
