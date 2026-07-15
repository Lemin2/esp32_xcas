#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xcas::mathlayout
{

struct MathExpr;
using MathExprPtr = std::shared_ptr<MathExpr>;

struct NumberExpr
{
    std::string value;
};

struct IdentifierExpr
{
    std::string name;
};

struct BinaryOpExpr
{
    std::string op;
    MathExprPtr lhs;
    MathExprPtr rhs;
};

struct UnaryOpExpr
{
    std::string op;
    MathExprPtr operand;
};

struct FunctionCallExpr
{
    std::string name;
    std::vector<MathExprPtr> args;
};

struct FractionExpr
{
    MathExprPtr numerator;
    MathExprPtr denominator;
};

struct PowerExpr
{
    MathExprPtr base;
    MathExprPtr exponent;
};

struct RootExpr
{
    MathExprPtr degree;
    MathExprPtr radicand;
};

struct SumExpr
{
    std::string variable;
    MathExprPtr lower;
    MathExprPtr upper;
    MathExprPtr body;
};

struct ProductExpr
{
    std::string variable;
    MathExprPtr lower;
    MathExprPtr upper;
    MathExprPtr body;
};

struct LimitExpr
{
    std::string variable;
    MathExprPtr approach;
    MathExprPtr body;
};

struct MatrixExpr
{
    std::vector<std::vector<MathExprPtr>> rows;
};

using MathExprNode = std::variant<NumberExpr,
                                  IdentifierExpr,
                                  BinaryOpExpr,
                                  UnaryOpExpr,
                                  FunctionCallExpr,
                                  FractionExpr,
                                  PowerExpr,
                                  RootExpr,
                                  SumExpr,
                                  ProductExpr,
                                  LimitExpr,
                                  MatrixExpr>;

struct MathExpr
{
    MathExprNode node;

    template <typename T>
    explicit MathExpr(T value)
        : node(std::move(value))
    {
    }
};

inline MathExprPtr makeExpr(MathExprNode node)
{
    return std::make_shared<MathExpr>(MathExpr{std::move(node)});
}

} // namespace xcas::mathlayout
