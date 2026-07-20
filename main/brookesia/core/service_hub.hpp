#pragma once

#include "cardputer_bsp/cardputer_bsp.hpp"
#include "xcas_service.hpp"

namespace brookesia {

class ServiceHub {
public:
    ServiceHub(board::IBsp &board, xcas::XcasService &casService)
        : board_(board), casService_(casService)
    {
    }

    board::IBsp &board()
    {
        return board_;
    }

    xcas::XcasService &casService()
    {
        return casService_;
    }

private:
    board::IBsp &board_;
    xcas::XcasService &casService_;
};

} // namespace brookesia
