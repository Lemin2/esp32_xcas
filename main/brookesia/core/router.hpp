#pragma once

#include <cstdint>

namespace brookesia {

enum class Route : uint8_t {
    Calc = 0,
    Graph,
    Files,
    Project,
    Settings,
};

class Router {
public:
    Route current() const
    {
        return current_;
    }

    void set(Route route)
    {
        current_ = route;
    }

    void next()
    {
        current_ = static_cast<Route>((static_cast<uint8_t>(current_) + 1U) % 5U);
    }

    void prev()
    {
        const uint8_t value = static_cast<uint8_t>(current_);
        current_ = static_cast<Route>((value + 4U) % 5U);
    }

private:
    Route current_ = Route::Calc;
};

} // namespace brookesia
