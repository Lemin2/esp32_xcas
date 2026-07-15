#pragma once

#include <string>

#include "mathlayout/ast/math_expr.hpp"
#include "mathlayout/font/font_metrics.hpp"
#include "mathlayout/layout/layout_box.hpp"
#include "mathlayout/math_types.hpp"

namespace xcas::mathlayout
{

struct LayoutContext
{
    const IFontMetrics *fontMetrics = nullptr;
    float emScale = 1.0F;
    bool strictMode = false;
};

struct LayoutResult
{
    LayoutBox box{};
    bool ok = true;
    std::string error;
};

class LayoutEngine
{
public:
    LayoutResult layout(const MathExpr &expr, MathStyle style, const LayoutContext &context) const;
};

} // namespace xcas::mathlayout
