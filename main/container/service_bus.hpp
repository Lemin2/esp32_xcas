#pragma once

#include "cardputer_bsp.hpp"
#include "xcas_service.hpp"

namespace container {

class ServiceBus {
public:
    ServiceBus(board::CardputerBsp &board, xcas::XcasService &casService)
        : board_(board), casService_(casService)
    {
    }

    board::CardputerBsp &board()
    {
        return board_;
    }

    xcas::XcasService &casService()
    {
        return casService_;
    }

private:
    board::CardputerBsp &board_;
    xcas::XcasService &casService_;
};

} // namespace container
