#include "mathlayout/layout/layout_engine.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace xcas::mathlayout
{
namespace
{

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

LayoutBox makePlaceholderBox(const MathExpr &expr, const MathStyle style, const LayoutContext &context)
{
    const int16_t fontLineHeight = (context.fontMetrics == nullptr) ? 14 : context.fontMetrics->lineHeight();
    const float styleScale = (style == MathStyle::Display || style == MathStyle::Text) ? 1.0F
                           : (style == MathStyle::Script) ? 0.8F
                           : 0.65F;

    const float h = std::max(1.0F, static_cast<float>(fontLineHeight) * context.emScale * styleScale);
    LayoutBox box{};
    box.width = std::max(1.0F, h * 0.65F);
    box.height = h;
    box.baseline = std::clamp(h * 0.75F, 0.0F, h - 0.01F);
    box.atomType = MathAtomType::Ord;
    box.debugLabel = "placeholder:" + nodeName(expr.node);
    return box;
}

float styleScale(const MathStyle style)
{
    if (style == MathStyle::Display || style == MathStyle::Text) {
        return 1.0F;
    }
    if (style == MathStyle::Script) {
        return 0.8F;
    }
    return 0.65F;
}

LayoutBox measureTextRun(const std::string &text, const LayoutContext &context, const MathStyle style)
{
    LayoutBox box{};
    box.debugLabel = "text-run:" + text;
    box.atomType = MathAtomType::Ord;

    const float scale = styleScale(style) * context.emScale;
    const int16_t lineH = (context.fontMetrics == nullptr) ? 14 : context.fontMetrics->lineHeight();
    box.height = std::max(1.0F, static_cast<float>(lineH) * scale);
    box.baseline = std::clamp(box.height * 0.75F, 0.0F, box.height - 0.01F);

    if (text.empty() || context.fontMetrics == nullptr) {
        box.width = std::max(1.0F, box.height * 0.4F);
        return box;
    }

    float width = 0.0F;
    for (size_t i = 0; i < text.size(); ++i) {
        const uint32_t cp = static_cast<unsigned char>(text[i]);
        const uint32_t next = (i + 1U < text.size()) ? static_cast<unsigned char>(text[i + 1U]) : 0U;
        width += static_cast<float>(context.fontMetrics->glyphAdvance(cp, next)) * scale;
    }
    box.width = std::max(1.0F, width);
    return box;
}

LayoutResult layoutNode(const MathExpr &expr, const MathStyle style, const LayoutContext &context)
{
    LayoutResult result{};

    std::visit(
        [&](const auto &n) {
            using T = std::decay_t<decltype(n)>;

            if constexpr (std::is_same_v<T, NumberExpr>) {
                result.box = measureTextRun(n.value, context, style);
            } else if constexpr (std::is_same_v<T, IdentifierExpr>) {
                result.box = measureTextRun(n.name, context, style);
            } else if constexpr (std::is_same_v<T, FunctionCallExpr>) {
                // B1 keeps function names as plain text-run boxes; argument layout arrives in B3.
                result.box = measureTextRun(n.name, context, style);
            } else {
                result.box = makePlaceholderBox(expr, style, context);
            }
        },
        expr.node);

    if (!result.box.hasValidBaseline()) {
        result.ok = false;
        result.error = "Generated box has invalid baseline";
    }

    return result;
}

} // namespace

LayoutResult LayoutEngine::layout(const MathExpr &expr, const MathStyle style, const LayoutContext &context) const
{
    LayoutResult result{};

    if (context.fontMetrics == nullptr) {
        result.ok = false;
        result.error = "LayoutContext.fontMetrics is null";
        result.box = makePlaceholderBox(expr, style, context);
        return result;
    }

    return layoutNode(expr, style, context);
}

} // namespace xcas::mathlayout
