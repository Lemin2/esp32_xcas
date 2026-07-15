#pragma once

#include <cstdint>

#include "lvgl.h"

namespace xcas::mathlayout
{

struct GlyphBBox
{
    int16_t ofsX = 0;
    int16_t ofsY = 0;
    int16_t width = 0;
    int16_t height = 0;
};

class IFontMetrics
{
public:
    virtual ~IFontMetrics() = default;

    virtual uint16_t glyphAdvance(uint32_t codepoint, uint32_t nextCodepoint = 0) const = 0;
    virtual GlyphBBox glyphBBox(uint32_t codepoint, uint32_t nextCodepoint = 0) const = 0;
    virtual int16_t lineHeight() const = 0;
    virtual bool hasGlyph(uint32_t codepoint) const = 0;
};

class LvglFontMetrics final : public IFontMetrics
{
public:
    explicit LvglFontMetrics(const lv_font_t *font);

    uint16_t glyphAdvance(uint32_t codepoint, uint32_t nextCodepoint = 0) const override;
    GlyphBBox glyphBBox(uint32_t codepoint, uint32_t nextCodepoint = 0) const override;
    int16_t lineHeight() const override;
    bool hasGlyph(uint32_t codepoint) const override;

private:
    const lv_font_t *font_ = nullptr;
};

const lv_font_t *defaultMathFont();

} // namespace xcas::mathlayout
