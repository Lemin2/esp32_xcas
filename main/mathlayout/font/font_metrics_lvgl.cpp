#include "mathlayout/font/font_metrics.hpp"

namespace xcas::mathlayout
{

extern const lv_font_t lv_font_noto_math_14;

namespace
{

bool loadGlyphDsc(const lv_font_t *font, const uint32_t codepoint, const uint32_t nextCodepoint, lv_font_glyph_dsc_t *out)
{
    if (font == nullptr || out == nullptr) {
        return false;
    }
    return lv_font_get_glyph_dsc(font, out, codepoint, nextCodepoint);
}

} // namespace

LvglFontMetrics::LvglFontMetrics(const lv_font_t *font)
    : font_(font)
{
}

uint16_t LvglFontMetrics::glyphAdvance(const uint32_t codepoint, const uint32_t nextCodepoint) const
{
    if (font_ == nullptr) {
        return 0;
    }
    return lv_font_get_glyph_width(font_, codepoint, nextCodepoint);
}

GlyphBBox LvglFontMetrics::glyphBBox(const uint32_t codepoint, const uint32_t nextCodepoint) const
{
    GlyphBBox bbox{};
    lv_font_glyph_dsc_t dsc{};
    if (!loadGlyphDsc(font_, codepoint, nextCodepoint, &dsc)) {
        return bbox;
    }

    bbox.ofsX = dsc.ofs_x;
    bbox.ofsY = dsc.ofs_y;
    bbox.width = static_cast<int16_t>(dsc.box_w);
    bbox.height = static_cast<int16_t>(dsc.box_h);
    return bbox;
}

int16_t LvglFontMetrics::lineHeight() const
{
    if (font_ == nullptr) {
        return 0;
    }
    return static_cast<int16_t>(font_->line_height);
}

bool LvglFontMetrics::hasGlyph(const uint32_t codepoint) const
{
    lv_font_glyph_dsc_t dsc{};
    if (!loadGlyphDsc(font_, codepoint, 0U, &dsc)) {
        return false;
    }
    return !dsc.is_placeholder;
}

const lv_font_t *defaultMathFont()
{
    return &lv_font_noto_math_14;
}

} // namespace xcas::mathlayout
