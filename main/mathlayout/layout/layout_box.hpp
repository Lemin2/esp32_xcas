#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mathlayout/math_types.hpp"

namespace xcas::mathlayout
{

struct LayoutPoint
{
    int16_t x = 0;
    int16_t y = 0;
};

struct LayoutBox
{
    float width = 0.0F;
    float height = 1.0F;
    float baseline = 0.0F;
    MathAtomType atomType = MathAtomType::Ord;
    std::string debugLabel;
    LayoutPoint origin{};
    std::vector<LayoutBox> children;

    bool hasValidBaseline() const
    {
        return baseline >= 0.0F && baseline < height;
    }

    void addChild(LayoutBox child, int16_t x, int16_t y)
    {
        child.origin = {x, y};
        children.push_back(child);
    }
};

} // namespace xcas::mathlayout
