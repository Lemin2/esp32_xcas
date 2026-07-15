#include "mathlayout/layout/layout_debug_dump.hpp"

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace xcas::mathlayout
{
namespace
{

std::string indentText(const int indent)
{
    return std::string(static_cast<size_t>(indent < 0 ? 0 : indent) * 2U, ' ');
}

std::string nodeName(const MathExprNode &node)
{
    if (std::holds_alternative<NumberExpr>(node)) {
        return "Number";
    }
    if (std::holds_alternative<IdentifierExpr>(node)) {
        return "Identifier";
    }
    if (std::holds_alternative<BinaryOpExpr>(node)) {
        return "BinaryOp";
    }
    if (std::holds_alternative<UnaryOpExpr>(node)) {
        return "UnaryOp";
    }
    if (std::holds_alternative<FunctionCallExpr>(node)) {
        return "FunctionCall";
    }
    if (std::holds_alternative<FractionExpr>(node)) {
        return "Fraction";
    }
    if (std::holds_alternative<PowerExpr>(node)) {
        return "Power";
    }
    if (std::holds_alternative<RootExpr>(node)) {
        return "Root";
    }
    if (std::holds_alternative<SumExpr>(node)) {
        return "Sum";
    }
    if (std::holds_alternative<ProductExpr>(node)) {
        return "Product";
    }
    if (std::holds_alternative<LimitExpr>(node)) {
        return "Limit";
    }
    return "Matrix";
}

std::string atomName(const MathAtomType atom)
{
    switch (atom) {
    case MathAtomType::Ord:
        return "Ord";
    case MathAtomType::Bin:
        return "Bin";
    case MathAtomType::Rel:
        return "Rel";
    case MathAtomType::Open:
        return "Open";
    case MathAtomType::Close:
        return "Close";
    case MathAtomType::Punct:
        return "Punct";
    case MathAtomType::Op:
        return "Op";
    }
    return "Ord";
}

} // namespace

std::string dumpMathExpr(const MathExpr &expr, const int indent)
{
    std::ostringstream oss;
    const std::string pad = indentText(indent);

    std::visit(
        [&](const auto &n) {
            using T = std::decay_t<decltype(n)>;
            oss << pad << nodeName(expr.node);

            if constexpr (std::is_same_v<T, NumberExpr>) {
                oss << "(" << n.value << ")\n";
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                oss << "(" << n.name << ")\n";
            } else if constexpr (std::is_same_v<T, BinaryOpExpr>) {
                oss << "(" << n.op << ")\n";
                if (n.lhs) {
                    oss << dumpMathExpr(*n.lhs, indent + 1);
                }
                if (n.rhs) {
                    oss << dumpMathExpr(*n.rhs, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, UnaryOpExpr>) {
                oss << "(" << n.op << ")\n";
                if (n.operand) {
                    oss << dumpMathExpr(*n.operand, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                oss << "(" << n.name << ")\n";
                for (const auto &arg : n.args) {
                    if (arg) {
                        oss << dumpMathExpr(*arg, indent + 1);
                    }
                }
            } else if constexpr (std::is_same_v<T, FractionExpr>) {
                oss << "\n";
                if (n.numerator) {
                    oss << dumpMathExpr(*n.numerator, indent + 1);
                }
                if (n.denominator) {
                    oss << dumpMathExpr(*n.denominator, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, PowerExpr>) {
                oss << "\n";
                if (n.base) {
                    oss << dumpMathExpr(*n.base, indent + 1);
                }
                if (n.exponent) {
                    oss << dumpMathExpr(*n.exponent, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, RootExpr>) {
                oss << "\n";
                if (n.degree) {
                    oss << dumpMathExpr(*n.degree, indent + 1);
                }
                if (n.radicand) {
                    oss << dumpMathExpr(*n.radicand, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, SumExpr>) {
                oss << "(" << n.variable << ")\n";
                if (n.lower) {
                    oss << dumpMathExpr(*n.lower, indent + 1);
                }
                if (n.upper) {
                    oss << dumpMathExpr(*n.upper, indent + 1);
                }
                if (n.body) {
                    oss << dumpMathExpr(*n.body, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, ProductExpr>) {
                oss << "(" << n.variable << ")\n";
                if (n.lower) {
                    oss << dumpMathExpr(*n.lower, indent + 1);
                }
                if (n.upper) {
                    oss << dumpMathExpr(*n.upper, indent + 1);
                }
                if (n.body) {
                    oss << dumpMathExpr(*n.body, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, LimitExpr>) {
                oss << "(" << n.variable << ")\n";
                if (n.approach) {
                    oss << dumpMathExpr(*n.approach, indent + 1);
                }
                if (n.body) {
                    oss << dumpMathExpr(*n.body, indent + 1);
                }
            } else if constexpr (std::is_same_v<T, MatrixExpr>) {
                const size_t cols = n.rows.empty() ? 0U : n.rows.front().size();
                oss << "(" << n.rows.size() << "x" << cols << ")\n";
                for (const auto &row : n.rows) {
                    for (const auto &cell : row) {
                        if (cell) {
                            oss << dumpMathExpr(*cell, indent + 1);
                        }
                    }
                }
            }
        },
        expr.node);

    return oss.str();
}

std::string dumpLayoutBox(const LayoutBox &box, const int indent)
{
    std::ostringstream oss;
    oss << indentText(indent)
        << "Box(label=" << box.debugLabel
        << ", w=" << box.width
        << ", h=" << box.height
        << ", bl=" << box.baseline
        << ", atom=" << atomName(box.atomType)
        << ", origin=(" << box.origin.x << "," << box.origin.y << ")"
        << ")\n";

    for (const auto &child : box.children) {
        oss << dumpLayoutBox(child, indent + 1);
    }
    return oss.str();
}

std::vector<AstSampleCase> createAstSampleCases()
{
    auto n1 = makeExpr(NumberExpr{"1"});
    auto n2 = makeExpr(NumberExpr{"2"});
    auto x = makeExpr(IdentifierExpr{"x"});

    auto frac = makeExpr(FractionExpr{n1, n2});
    auto x2 = makeExpr(PowerExpr{x, makeExpr(NumberExpr{"2"})});

    auto sumBody = makeExpr(PowerExpr{makeExpr(IdentifierExpr{"k"}), makeExpr(NumberExpr{"2"})});
    auto sumExpr = makeExpr(SumExpr{"k", makeExpr(NumberExpr{"1"}), makeExpr(IdentifierExpr{"n"}), sumBody});

    std::vector<std::vector<MathExprPtr>> matrixRows;
    matrixRows.push_back({makeExpr(IdentifierExpr{"a"}), makeExpr(IdentifierExpr{"b"})});
    matrixRows.push_back({makeExpr(IdentifierExpr{"c"}), makeExpr(IdentifierExpr{"d"})});

    std::vector<AstSampleCase> out;
    out.push_back({"1/2", frac});
    out.push_back({"x^2", x2});
    out.push_back({"sum(k,1,n,k^2)", sumExpr});
    out.push_back({"[[a,b],[c,d]]", makeExpr(MatrixExpr{std::move(matrixRows)})});
    return out;
}

LayoutBox createSampleLayoutTree()
{
    LayoutBox root;
    root.debugLabel = "root";
    root.width = 42.0F;
    root.height = 20.0F;
    root.baseline = 14.0F;
    root.atomType = MathAtomType::Ord;

    LayoutBox numerator;
    numerator.debugLabel = "numerator";
    numerator.width = 18.0F;
    numerator.height = 8.0F;
    numerator.baseline = 6.0F;
    numerator.atomType = MathAtomType::Ord;

    LayoutBox denominator;
    denominator.debugLabel = "denominator";
    denominator.width = 20.0F;
    denominator.height = 8.0F;
    denominator.baseline = 6.0F;
    denominator.atomType = MathAtomType::Ord;

    root.addChild(numerator, 12, 1);
    root.addChild(denominator, 11, 11);
    return root;
}

bool validateLayoutTree(const LayoutBox &box)
{
    if (!box.hasValidBaseline()) {
        return false;
    }

    for (const auto &child : box.children) {
        if (!validateLayoutTree(child)) {
            return false;
        }
    }

    return true;
}

} // namespace xcas::mathlayout
