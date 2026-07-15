#pragma once

#include <string>
#include <vector>

#include "mathlayout/ast/math_expr.hpp"
#include "mathlayout/layout/layout_box.hpp"

namespace xcas::mathlayout
{

struct AstSampleCase
{
    std::string formula;
    MathExprPtr expr;
};

std::string dumpMathExpr(const MathExpr &expr, int indent = 0);
std::string dumpLayoutBox(const LayoutBox &box, int indent = 0);
std::vector<AstSampleCase> createAstSampleCases();
LayoutBox createSampleLayoutTree();
bool validateLayoutTree(const LayoutBox &box);

} // namespace xcas::mathlayout
