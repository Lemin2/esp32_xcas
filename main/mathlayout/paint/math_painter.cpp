#include "mathlayout/paint/math_painter.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "lvgl.h"

LV_FONT_DECLARE(lv_font_symbols_14)

namespace xcas::mathlayout
{
namespace
{

constexpr uint32_t kSymMatLTop = 0xE100U;
constexpr uint32_t kSymMatLMid = 0xE101U;
constexpr uint32_t kSymMatLBot = 0xE102U;
constexpr uint32_t kSymMatRTop = 0xE103U;
constexpr uint32_t kSymMatRMid = 0xE104U;
constexpr uint32_t kSymMatRBot = 0xE105U;

constexpr uint32_t kSymSqrtStem = 0xE110U;
constexpr uint32_t kSymSqrtArm = 0xE111U;
constexpr uint32_t kSymSqrtHook = 0xE112U;

constexpr uint32_t kSymArrowRight = 0xE113U;

constexpr uint32_t kSymSigmaTop = 0xE120U;
constexpr uint32_t kSymSigmaMid = 0xE121U;
constexpr uint32_t kSymSigmaBot = 0xE122U;
constexpr uint32_t kSymPiTop = 0xE123U;
constexpr uint32_t kSymPiMid = 0xE124U;
constexpr uint32_t kSymPiBot = 0xE125U;

constexpr uint32_t kSymParenLTop = 0xE130U;
constexpr uint32_t kSymParenLMid = 0xE131U;
constexpr uint32_t kSymParenLBot = 0xE132U;
constexpr uint32_t kSymParenRTop = 0xE133U;
constexpr uint32_t kSymParenRMid = 0xE134U;
constexpr uint32_t kSymParenRBot = 0xE135U;

constexpr uint32_t kSymBrackLTop = 0xE136U;
constexpr uint32_t kSymBrackLMid = 0xE137U;
constexpr uint32_t kSymBrackLBot = 0xE138U;
constexpr uint32_t kSymBrackRTop = 0xE139U;
constexpr uint32_t kSymBrackRMid = 0xE13AU;
constexpr uint32_t kSymBrackRBot = 0xE13BU;

constexpr uint32_t kSymBraceLTop = 0xE13CU;
constexpr uint32_t kSymBraceLMid = 0xE13DU;
constexpr uint32_t kSymBraceLBot = 0xE13EU;
constexpr uint32_t kSymBraceRTop = 0xE13FU;
constexpr uint32_t kSymBraceRMid = 0xE140U;
constexpr uint32_t kSymBraceRBot = 0xE141U;

constexpr uint32_t kSymBarL = 0xE142U;
constexpr uint32_t kSymBarR = 0xE143U;

struct Utf8Glyph
{
    std::string text;
    uint32_t codepoint = 0;
};

bool decodeUtf8At(const std::string &s, size_t pos, Utf8Glyph &out, size_t &next)
{
    if (pos >= s.size()) {
        return false;
    }

    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    size_t len = 1;
    uint32_t cp = c0;

    if ((c0 & 0x80U) == 0x00U) {
        len = 1;
        cp = c0;
    } else if ((c0 & 0xE0U) == 0xC0U && pos + 1 < s.size()) {
        len = 2;
        cp = static_cast<uint32_t>(c0 & 0x1FU);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1]) & 0x3FU);
    } else if ((c0 & 0xF0U) == 0xE0U && pos + 2 < s.size()) {
        len = 3;
        cp = static_cast<uint32_t>(c0 & 0x0FU);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1]) & 0x3FU);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 2]) & 0x3FU);
    } else if ((c0 & 0xF8U) == 0xF0U && pos + 3 < s.size()) {
        len = 4;
        cp = static_cast<uint32_t>(c0 & 0x07U);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1]) & 0x3FU);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 2]) & 0x3FU);
        cp = (cp << 6) | static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 3]) & 0x3FU);
    } else {
        len = 1;
        cp = c0;
    }

    out.text = s.substr(pos, len);
    out.codepoint = cp;
    next = pos + len;
    return true;
}

uint32_t decodeFirstCodepoint(const std::string &s)
{
    Utf8Glyph g;
    size_t next = 0;
    if (!decodeUtf8At(s, 0, g, next)) {
        return 0;
    }
    return g.codepoint;
}

bool shouldUseSymbolsFont(uint32_t cp)
{
    if (cp >= 0x0370U && cp <= 0x03FFU) {
        return true;
    }
    switch (cp) {
    case 0x2208U: // ∈
    case 0x221EU: // ∞
    case 0x2229U: // ∩
    case 0x222AU: // ∪
    case 0x2212U: // −
    case 0x00B1U: // ±
    case 0x2260U: // ≠
    case 0x2264U: // ≤
    case 0x2265U: // ≥
    case 0x2190U: // ←
    case 0x2192U: // →
        return true;
    default:
        return false;
    }
}

std::vector<Utf8Glyph> splitUtf8Glyphs(const std::string &line)
{
    std::vector<Utf8Glyph> out;
    for (size_t i = 0; i < line.size();) {
        Utf8Glyph g;
        size_t next = i;
        if (!decodeUtf8At(line, i, g, next)) {
            break;
        }
        out.push_back(std::move(g));
        i = next;
    }
    return out;
}

struct CanvasStorage
{
    lv_draw_buf_t *draw_buf = nullptr;
};

struct IntRect
{
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

void addLine(DrawList &draw_list, int x1, int y1, int x2, int y2, int width = 1);

enum class StretchSymbolKind
{
    MatrixLeft,
    MatrixRight,
    ParenLeft,
    ParenRight,
    BracketLeft,
    BracketRight,
    BraceLeft,
    BraceRight,
    BarLeft,
    BarRight,
    Sigma,
    Pi,
};

struct StretchSymbolDef
{
    StretchSymbolKind kind;
    uint32_t top;
    uint32_t mid;
    uint32_t bot;
};

bool getStretchSymbolDef(uint32_t cp, StretchSymbolDef &out)
{
    switch (cp) {
    case kSymMatLTop:
    case kSymMatLMid:
    case kSymMatLBot:
        out = {StretchSymbolKind::MatrixLeft, kSymMatLTop, kSymMatLMid, kSymMatLBot};
        return true;
    case kSymMatRTop:
    case kSymMatRMid:
    case kSymMatRBot:
        out = {StretchSymbolKind::MatrixRight, kSymMatRTop, kSymMatRMid, kSymMatRBot};
        return true;
    case kSymParenLTop:
    case kSymParenLMid:
    case kSymParenLBot:
        out = {StretchSymbolKind::ParenLeft, kSymParenLTop, kSymParenLMid, kSymParenLBot};
        return true;
    case kSymParenRTop:
    case kSymParenRMid:
    case kSymParenRBot:
        out = {StretchSymbolKind::ParenRight, kSymParenRTop, kSymParenRMid, kSymParenRBot};
        return true;
    case kSymBrackLTop:
    case kSymBrackLMid:
    case kSymBrackLBot:
        out = {StretchSymbolKind::BracketLeft, kSymBrackLTop, kSymBrackLMid, kSymBrackLBot};
        return true;
    case kSymBrackRTop:
    case kSymBrackRMid:
    case kSymBrackRBot:
        out = {StretchSymbolKind::BracketRight, kSymBrackRTop, kSymBrackRMid, kSymBrackRBot};
        return true;
    case kSymBraceLTop:
    case kSymBraceLMid:
    case kSymBraceLBot:
        out = {StretchSymbolKind::BraceLeft, kSymBraceLTop, kSymBraceLMid, kSymBraceLBot};
        return true;
    case kSymBraceRTop:
    case kSymBraceRMid:
    case kSymBraceRBot:
        out = {StretchSymbolKind::BraceRight, kSymBraceRTop, kSymBraceRMid, kSymBraceRBot};
        return true;
    case kSymBarL:
        out = {StretchSymbolKind::BarLeft, kSymBarL, kSymBarL, kSymBarL};
        return true;
    case kSymBarR:
        out = {StretchSymbolKind::BarRight, kSymBarR, kSymBarR, kSymBarR};
        return true;
    case kSymSigmaTop:
    case kSymSigmaMid:
    case kSymSigmaBot:
        out = {StretchSymbolKind::Sigma, kSymSigmaTop, kSymSigmaMid, kSymSigmaBot};
        return true;
    case kSymPiTop:
    case kSymPiMid:
    case kSymPiBot:
        out = {StretchSymbolKind::Pi, kSymPiTop, kSymPiMid, kSymPiBot};
        return true;
    default:
        return false;
    }
}

void emitStretchSymbol(DrawList &draw_list,
                       StretchSymbolKind kind,
                       int x,
                       int top,
                       int bottom,
                       int cell_width)
{
    const int left = x;
    const int right = x + std::max(1, cell_width) - 1;
    const int inner_l = left + std::max(1, cell_width / 4);
    const int inner_r = right - std::max(1, cell_width / 4);
    const int inner_mid_l = left + std::max(1, cell_width / 3);
    const int inner_mid_r = right - std::max(1, cell_width / 3);
    const int total_h = std::max(1, bottom - top + 1);
    const int center_y = (top + bottom) / 2;
    const int upper_mid = top + std::max(1, total_h / 3);
    const int lower_mid = bottom - std::max(1, total_h / 3);

    switch (kind) {
    case StretchSymbolKind::MatrixLeft:
    case StretchSymbolKind::BracketLeft:
        addLine(draw_list, inner_l, top + 1, inner_l, bottom - 1);
        addLine(draw_list, inner_l, top + 1, right - 1, top + 1);
        addLine(draw_list, inner_l, bottom - 1, right - 1, bottom - 1);
        return;
    case StretchSymbolKind::MatrixRight:
    case StretchSymbolKind::BracketRight:
        addLine(draw_list, inner_r, top + 1, inner_r, bottom - 1);
        addLine(draw_list, left + 1, top + 1, inner_r, top + 1);
        addLine(draw_list, left + 1, bottom - 1, inner_r, bottom - 1);
        return;
    case StretchSymbolKind::ParenLeft:
        addLine(draw_list, right - 1, top + 1, inner_l, upper_mid);
        addLine(draw_list, inner_l, upper_mid, inner_l, lower_mid);
        addLine(draw_list, inner_l, lower_mid, right - 1, bottom - 1);
        return;
    case StretchSymbolKind::ParenRight:
        addLine(draw_list, left + 1, top + 1, inner_r, upper_mid);
        addLine(draw_list, inner_r, upper_mid, inner_r, lower_mid);
        addLine(draw_list, inner_r, lower_mid, left + 1, bottom - 1);
        return;
    case StretchSymbolKind::BraceLeft:
        addLine(draw_list, right - 1, top + 1, inner_mid_l, top + 1);
        addLine(draw_list, inner_mid_l, top + 1, left + 1, center_y);
        addLine(draw_list, left + 1, center_y, inner_mid_l, center_y);
        addLine(draw_list, left + 1, center_y, inner_mid_l, bottom - 1);
        addLine(draw_list, inner_mid_l, bottom - 1, right - 1, bottom - 1);
        return;
    case StretchSymbolKind::BraceRight:
        addLine(draw_list, left + 1, top + 1, inner_mid_r, top + 1);
        addLine(draw_list, inner_mid_r, top + 1, right - 1, center_y);
        addLine(draw_list, inner_mid_r, center_y, right - 1, center_y);
        addLine(draw_list, right - 1, center_y, inner_mid_r, bottom - 1);
        addLine(draw_list, inner_mid_r, bottom - 1, left + 1, bottom - 1);
        return;
    case StretchSymbolKind::BarLeft:
        addLine(draw_list, inner_mid_l, top + 1, inner_mid_l, bottom - 1);
        return;
    case StretchSymbolKind::BarRight:
        addLine(draw_list, inner_mid_r, top + 1, inner_mid_r, bottom - 1);
        return;
    case StretchSymbolKind::Sigma:
        addLine(draw_list, left + 1, top + 1, right - 1, top + 1);
        addLine(draw_list, right - 1, top + 1, left + 1, center_y);
        addLine(draw_list, left + 1, center_y, right - 1, center_y);
        addLine(draw_list, left + 1, center_y, right - 1, bottom - 1);
        addLine(draw_list, left + 1, bottom - 1, right - 1, bottom - 1);
        return;
    case StretchSymbolKind::Pi:
        addLine(draw_list, left + 1, top + 1, right - 1, top + 1);
        addLine(draw_list, inner_l, top + 1, inner_l, bottom - 1);
        addLine(draw_list, inner_r, top + 1, inner_r, bottom - 1);
        return;
    }
}

bool tryEmitStretchSymbol(DrawList &draw_list,
                          const std::vector<std::vector<Utf8Glyph>> &rows,
                          std::vector<std::vector<bool>> &consumed,
                          int row,
                          int col,
                          int cell_width,
                          int row_pitch,
                          int line_height)
{
    if (row < 0 || row >= static_cast<int>(rows.size())) {
        return false;
    }
    if (col < 0 || col >= static_cast<int>(rows[static_cast<size_t>(row)].size())) {
        return false;
    }
    if (consumed[static_cast<size_t>(row)][static_cast<size_t>(col)]) {
        return false;
    }

    StretchSymbolDef def;
    const uint32_t cp = rows[static_cast<size_t>(row)][static_cast<size_t>(col)].codepoint;
    if (!getStretchSymbolDef(cp, def)) {
        return false;
    }

    int end_row = row;
    if (def.top == def.mid && def.mid == def.bot) {
        if (row > 0 && col < static_cast<int>(rows[static_cast<size_t>(row - 1)].size()) &&
            rows[static_cast<size_t>(row - 1)][static_cast<size_t>(col)].codepoint == cp) {
            return false;
        }
        while (end_row + 1 < static_cast<int>(rows.size()) &&
               col < static_cast<int>(rows[static_cast<size_t>(end_row + 1)].size()) &&
               rows[static_cast<size_t>(end_row + 1)][static_cast<size_t>(col)].codepoint == cp) {
            ++end_row;
        }
    } else {
        if (cp != def.top) {
            return false;
        }
        bool seen_bot = false;
        while (end_row + 1 < static_cast<int>(rows.size()) &&
               col < static_cast<int>(rows[static_cast<size_t>(end_row + 1)].size())) {
            const uint32_t next_cp = rows[static_cast<size_t>(end_row + 1)][static_cast<size_t>(col)].codepoint;
            if (next_cp == def.mid) {
                ++end_row;
                continue;
            }
            if (next_cp == def.bot) {
                ++end_row;
                seen_bot = true;
            }
            break;
        }
        if (!seen_bot) {
            return false;
        }
    }

    if (end_row == row) {
        return false;
    }

    for (int r = row; r <= end_row; ++r) {
        consumed[static_cast<size_t>(r)][static_cast<size_t>(col)] = true;
    }

    const int top = row * row_pitch;
    const int bottom = end_row * row_pitch + line_height - 1;
    emitStretchSymbol(draw_list, def.kind, col * cell_width, top, bottom, cell_width);
    return true;
}

void addLine(DrawList &draw_list, int x1, int y1, int x2, int y2, int width)
{
    DrawLineCommand cmd;
    cmd.x1 = x1;
    cmd.y1 = y1;
    cmd.x2 = x2;
    cmd.y2 = y2;
    cmd.width = std::max(1, width);
    draw_list.commands.emplace_back(std::move(cmd));
}

bool emitSpecialSymbol(DrawList &draw_list,
                       uint32_t codepoint,
                       int x,
                       int y,
                       int cell_width,
                       int line_height)
{
    const int left = x;
    const int right = x + std::max(1, cell_width) - 1;
    const int top = y;
    const int bottom = y + std::max(1, line_height) - 1;
    const int mid_y = (top + bottom) / 2;

    if (codepoint == kSymMatLTop || codepoint == kSymMatLMid || codepoint == kSymMatLBot) {
        const int vx = left + std::max(1, cell_width / 4);
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        if (codepoint == kSymMatLTop) {
            addLine(draw_list, vx, top + 1, right - 1, top + 1);
        }
        if (codepoint == kSymMatLBot) {
            addLine(draw_list, vx, bottom - 1, right - 1, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymMatRTop || codepoint == kSymMatRMid || codepoint == kSymMatRBot) {
        const int vx = right - std::max(1, cell_width / 4);
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        if (codepoint == kSymMatRTop) {
            addLine(draw_list, left + 1, top + 1, vx, top + 1);
        }
        if (codepoint == kSymMatRBot) {
            addLine(draw_list, left + 1, bottom - 1, vx, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymSqrtStem) {
        const int vx = right - std::max(1, cell_width / 5);
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        return true;
    }
    if (codepoint == kSymSqrtArm) {
        addLine(draw_list, left + 1, bottom - 1, right - 1, top + 1);
        return true;
    }
    if (codepoint == kSymSqrtHook) {
        const int p1x = left + 1;
        const int p1y = mid_y;
        const int p2x = left + std::max(2, cell_width / 3);
        const int p2y = bottom - 1;
        const int p3x = right - 1;
        const int p3y = top + 1;
        addLine(draw_list, p1x, p1y, p2x, p2y);
        addLine(draw_list, p2x, p2y, p3x, p3y);
        return true;
    }

    if (codepoint == kSymArrowRight) {
        const int ay = mid_y;
        const int head = std::max(2, cell_width / 3);
        addLine(draw_list, left + 1, ay, right - 2, ay);
        addLine(draw_list, right - head, ay - std::max(1, line_height / 4), right - 1, ay);
        addLine(draw_list, right - head, ay + std::max(1, line_height / 4), right - 1, ay);
        return true;
    }

    if (codepoint == kSymSigmaTop) {
        addLine(draw_list, left + 1, top + 1, right - 1, top + 1);
        addLine(draw_list, right - 1, top + 1, left + 1, mid_y);
        return true;
    }
    if (codepoint == kSymSigmaMid) {
        addLine(draw_list, left + 1, mid_y, right - 1, mid_y);
        return true;
    }
    if (codepoint == kSymSigmaBot) {
        addLine(draw_list, left + 1, mid_y, right - 1, bottom - 1);
        addLine(draw_list, left + 1, bottom - 1, right - 1, bottom - 1);
        return true;
    }

    if (codepoint == kSymPiTop) {
        addLine(draw_list, left + 1, top + 1, right - 1, top + 1);
        return true;
    }
    if (codepoint == kSymPiMid || codepoint == kSymPiBot) {
        const int lx = left + std::max(1, cell_width / 4);
        const int rx = right - std::max(1, cell_width / 4);
        addLine(draw_list, lx, top + 1, lx, bottom - 1);
        addLine(draw_list, rx, top + 1, rx, bottom - 1);
        return true;
    }

    if (codepoint == kSymParenLTop || codepoint == kSymParenLMid || codepoint == kSymParenLBot) {
        const int ix = left + std::max(1, cell_width / 4);
        if (codepoint == kSymParenLTop) {
            addLine(draw_list, right - 1, top + 1, ix, mid_y);
        } else if (codepoint == kSymParenLMid) {
            addLine(draw_list, ix, top + 1, ix, bottom - 1);
        } else {
            addLine(draw_list, ix, mid_y, right - 1, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymParenRTop || codepoint == kSymParenRMid || codepoint == kSymParenRBot) {
        const int ix = right - std::max(1, cell_width / 4);
        if (codepoint == kSymParenRTop) {
            addLine(draw_list, left + 1, top + 1, ix, mid_y);
        } else if (codepoint == kSymParenRMid) {
            addLine(draw_list, ix, top + 1, ix, bottom - 1);
        } else {
            addLine(draw_list, left + 1, bottom - 1, ix, mid_y);
        }
        return true;
    }

    if (codepoint == kSymBrackLTop || codepoint == kSymBrackLMid || codepoint == kSymBrackLBot) {
        const int vx = left + std::max(1, cell_width / 4);
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        if (codepoint == kSymBrackLTop) {
            addLine(draw_list, vx, top + 1, right - 1, top + 1);
        }
        if (codepoint == kSymBrackLBot) {
            addLine(draw_list, vx, bottom - 1, right - 1, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymBrackRTop || codepoint == kSymBrackRMid || codepoint == kSymBrackRBot) {
        const int vx = right - std::max(1, cell_width / 4);
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        if (codepoint == kSymBrackRTop) {
            addLine(draw_list, left + 1, top + 1, vx, top + 1);
        }
        if (codepoint == kSymBrackRBot) {
            addLine(draw_list, left + 1, bottom - 1, vx, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymBraceLTop || codepoint == kSymBraceLMid || codepoint == kSymBraceLBot) {
        const int mx = left + std::max(1, cell_width / 3);
        if (codepoint == kSymBraceLTop) {
            addLine(draw_list, right - 1, top + 1, mx, top + 1);
            addLine(draw_list, mx, top + 1, left + 1, mid_y);
        } else if (codepoint == kSymBraceLMid) {
            addLine(draw_list, left + 1, mid_y, mx, mid_y);
        } else {
            addLine(draw_list, left + 1, mid_y, mx, bottom - 1);
            addLine(draw_list, mx, bottom - 1, right - 1, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymBraceRTop || codepoint == kSymBraceRMid || codepoint == kSymBraceRBot) {
        const int mx = right - std::max(1, cell_width / 3);
        if (codepoint == kSymBraceRTop) {
            addLine(draw_list, left + 1, top + 1, mx, top + 1);
            addLine(draw_list, mx, top + 1, right - 1, mid_y);
        } else if (codepoint == kSymBraceRMid) {
            addLine(draw_list, mx, mid_y, right - 1, mid_y);
        } else {
            addLine(draw_list, right - 1, mid_y, mx, bottom - 1);
            addLine(draw_list, mx, bottom - 1, left + 1, bottom - 1);
        }
        return true;
    }

    if (codepoint == kSymBarL || codepoint == kSymBarR) {
        const int vx = (codepoint == kSymBarL) ? (left + std::max(1, cell_width / 3))
                                               : (right - std::max(1, cell_width / 3));
        addLine(draw_list, vx, top + 1, vx, bottom - 1);
        return true;
    }

    return false;
}

int maxGlyphAdvance(const TextBox &box, const lv_font_t *font)
{
    if (font == nullptr) {
        return 8;
    }

    int max_advance = 0;
    for (const std::string &line : box.lines) {
        const std::vector<Utf8Glyph> glyphs = splitUtf8Glyphs(line);
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const uint32_t cp = glyphs[i].codepoint;
            const uint32_t next = (i + 1U < glyphs.size()) ? glyphs[i + 1U].codepoint : 0U;
            int adv = 0;
            if (shouldUseSymbolsFont(cp)) {
                adv = static_cast<int>(lv_font_get_glyph_width(&lv_font_symbols_14, cp, next));
            }
            if (adv == 0) {
                adv = static_cast<int>(lv_font_get_glyph_width(font, cp, next));
            }
            max_advance = std::max(max_advance, adv);
        }
    }

    if (max_advance <= 0) {
        max_advance = static_cast<int>(lv_font_get_glyph_width(font, static_cast<uint32_t>('M'), 0));
    }
    if (max_advance <= 0) {
        max_advance = 8;
    }
    return max_advance + 1;
}

void onCanvasDelete(lv_event_t *e)
{
    if (e == nullptr) {
        return;
    }

    lv_obj_t *canvas = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (canvas == nullptr) {
        return;
    }

    auto *storage = static_cast<CanvasStorage *>(lv_obj_get_user_data(canvas));
    if (storage == nullptr) {
        return;
    }

    if (storage->draw_buf != nullptr) {
        lv_draw_buf_destroy(storage->draw_buf);
        storage->draw_buf = nullptr;
    }
    delete storage;
    lv_obj_set_user_data(canvas, nullptr);
}

bool ensureCanvasStorage(lv_obj_t *canvas, int width, int height)
{
    if (canvas == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    auto *storage = static_cast<CanvasStorage *>(lv_obj_get_user_data(canvas));
    if (storage == nullptr) {
        storage = new CanvasStorage();
        lv_obj_set_user_data(canvas, storage);
        lv_obj_add_event_cb(canvas, onCanvasDelete, LV_EVENT_DELETE, nullptr);
    }

    const bool needs_recreate =
        storage->draw_buf == nullptr ||
        storage->draw_buf->header.w != width ||
        storage->draw_buf->header.h != height ||
        storage->draw_buf->header.cf != LV_COLOR_FORMAT_RGB565;
    if (!needs_recreate) {
        return true;
    }

    if (storage->draw_buf != nullptr) {
        lv_draw_buf_destroy(storage->draw_buf);
        storage->draw_buf = nullptr;
    }

    storage->draw_buf = lv_draw_buf_create(static_cast<uint32_t>(width),
                                           static_cast<uint32_t>(height),
                                           LV_COLOR_FORMAT_RGB565,
                                           LV_STRIDE_AUTO);
    if (storage->draw_buf == nullptr) {
        return false;
    }

    lv_canvas_set_draw_buf(canvas, storage->draw_buf);
    return true;
}

IntRect normalizeViewport(const PaintViewport &viewport, int fallback_w, int fallback_h)
{
    IntRect out;
    const int w = (viewport.width > 0) ? viewport.width : fallback_w;
    const int h = (viewport.height > 0) ? viewport.height : fallback_h;
    out.x1 = std::max(0, viewport.x);
    out.y1 = std::max(0, viewport.y);
    out.x2 = out.x1 + std::max(1, w) - 1;
    out.y2 = out.y1 + std::max(1, h) - 1;
    return out;
}

bool rectIntersects(const IntRect &a, const IntRect &b)
{
    return !(a.x2 < b.x1 || a.x1 > b.x2 || a.y2 < b.y1 || a.y1 > b.y2);
}

int regionCode(int x, int y, const IntRect &clip)
{
    int code = 0;
    if (x < clip.x1) {
        code |= 1;
    } else if (x > clip.x2) {
        code |= 2;
    }
    if (y < clip.y1) {
        code |= 4;
    } else if (y > clip.y2) {
        code |= 8;
    }
    return code;
}

bool clipLineToRect(int &x1, int &y1, int &x2, int &y2, const IntRect &clip)
{
    int c1 = regionCode(x1, y1, clip);
    int c2 = regionCode(x2, y2, clip);

    while (true) {
        if ((c1 | c2) == 0) {
            return true;
        }
        if ((c1 & c2) != 0) {
            return false;
        }

        const int out = (c1 != 0) ? c1 : c2;
        int x = 0;
        int y = 0;

        if ((out & 8) != 0) {
            y = clip.y2;
            const int dy = y2 - y1;
            x = (dy == 0) ? x1 : (x1 + (x2 - x1) * (clip.y2 - y1) / dy);
        } else if ((out & 4) != 0) {
            y = clip.y1;
            const int dy = y2 - y1;
            x = (dy == 0) ? x1 : (x1 + (x2 - x1) * (clip.y1 - y1) / dy);
        } else if ((out & 2) != 0) {
            x = clip.x2;
            const int dx = x2 - x1;
            y = (dx == 0) ? y1 : (y1 + (y2 - y1) * (clip.x2 - x1) / dx);
        } else {
            x = clip.x1;
            const int dx = x2 - x1;
            y = (dx == 0) ? y1 : (y1 + (y2 - y1) * (clip.x1 - x1) / dx);
        }

        if (out == c1) {
            x1 = x;
            y1 = y;
            c1 = regionCode(x1, y1, clip);
        } else {
            x2 = x;
            y2 = y;
            c2 = regionCode(x2, y2, clip);
        }
    }
}

void simplifyLine(int &x1, int &y1, int &x2, int &y2)
{
    const int dx = x2 - x1;
    const int dy = y2 - y1;
    if (std::abs(dy) <= 1) {
        y2 = y1;
    }
    if (std::abs(dx) <= 1) {
        x2 = x1;
    }
}

bool drawOneCommand(lv_layer_t &layer,
                    const DrawCommand &command,
                    const lv_font_t *font,
                    lv_color_t text_color,
                    const ProgressivePaintState &state)
{
    if (font == nullptr) {
        return false;
    }

    const IntRect viewport = normalizeViewport(state.viewport, 1, 1);
    if (const auto *glyph = std::get_if<DrawGlyphCommand>(&command)) {
        const uint32_t cp = decodeFirstCodepoint(glyph->text);
        if (cp == 0U) {
            return false;
        }
        const lv_font_t *glyph_font = shouldUseSymbolsFont(cp) ? &lv_font_symbols_14 : font;
        const int glyph_w = std::max(1, static_cast<int>(lv_font_get_glyph_width(glyph_font, cp, 0)));
        const int glyph_h = std::max(1, static_cast<int>(lv_font_get_line_height(glyph_font)));
        IntRect glyph_rect = {
            state.offset_x + glyph->x,
            state.offset_y + glyph->y,
            state.offset_x + glyph->x + glyph_w - 1,
            state.offset_y + glyph->y + glyph_h - 1,
        };
        if (!rectIntersects(glyph_rect, viewport)) {
            return false;
        }

        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.color = text_color;
        dsc.font = glyph_font;
        dsc.opa = LV_OPA_COVER;

        lv_point_t point = {
            static_cast<lv_coord_t>(state.offset_x + glyph->x),
            static_cast<lv_coord_t>(state.offset_y + glyph->y),
        };
        lv_draw_character(&layer, &dsc, &point, cp);
        return true;
    }

    if (const auto *line = std::get_if<DrawLineCommand>(&command)) {
        int x1 = state.offset_x + line->x1;
        int y1 = state.offset_y + line->y1;
        int x2 = state.offset_x + line->x2;
        int y2 = state.offset_y + line->y2;

        simplifyLine(x1, y1, x2, y2);
        if (!clipLineToRect(x1, y1, x2, y2, viewport)) {
            return false;
        }
        if (x1 == x2 && y1 == y2) {
            return false;
        }

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = text_color;
        dsc.width = std::max(1, line->width);
        dsc.opa = LV_OPA_COVER;
        dsc.p1.x = static_cast<lv_value_precise_t>(x1);
        dsc.p1.y = static_cast<lv_value_precise_t>(y1);
        dsc.p2.x = static_cast<lv_value_precise_t>(x2);
        dsc.p2.y = static_cast<lv_value_precise_t>(y2);
        lv_draw_line(&layer, &dsc);
        return true;
    }

    if (const auto *rect = std::get_if<DrawRectCommand>(&command)) {
        IntRect rect_i = {
            state.offset_x + rect->x,
            state.offset_y + rect->y,
            state.offset_x + rect->x + std::max(1, rect->width) - 1,
            state.offset_y + rect->y + std::max(1, rect->height) - 1,
        };
        if (!rectIntersects(rect_i, viewport)) {
            return false;
        }

        lv_area_t coords = {
            static_cast<lv_coord_t>(std::max(rect_i.x1, viewport.x1)),
            static_cast<lv_coord_t>(std::max(rect_i.y1, viewport.y1)),
            static_cast<lv_coord_t>(std::min(rect_i.x2, viewport.x2)),
            static_cast<lv_coord_t>(std::min(rect_i.y2, viewport.y2)),
        };

        if (rect->filled) {
            lv_draw_fill_dsc_t dsc;
            lv_draw_fill_dsc_init(&dsc);
            dsc.color = text_color;
            dsc.opa = LV_OPA_COVER;
            lv_draw_fill(&layer, &dsc, &coords);
        } else {
            lv_draw_border_dsc_t dsc;
            lv_draw_border_dsc_init(&dsc);
            dsc.color = text_color;
            dsc.width = 1;
            dsc.side = LV_BORDER_SIDE_FULL;
            dsc.opa = LV_OPA_COVER;
            lv_draw_border(&layer, &dsc, &coords);
        }
        return true;
    }

    return false;
}

} // namespace

DrawList buildDrawList(const TextBox &box, const lv_font_t *font, int line_space)
{
    DrawList draw_list;
    if (box.empty()) {
        return draw_list;
    }

    const int cell_width = maxGlyphAdvance(box, font);
    const int line_height = std::max(1, static_cast<int>(font != nullptr ? lv_font_get_line_height(font) : 16));
    const int row_pitch = line_height + std::max(0, line_space);

    int max_cols = 1;
    for (const std::string &line : box.lines) {
        max_cols = std::max(max_cols, static_cast<int>(splitUtf8Glyphs(line).size()));
    }

    draw_list.width = std::max(1, max_cols * cell_width);
    draw_list.height = std::max(1, box.height() * row_pitch - std::max(0, line_space));
    draw_list.baseline = box.baseline * row_pitch + (line_height / 2);
    constexpr size_t kMaxDrawCommands = 32000;
    const size_t estimated = static_cast<size_t>(max_cols) * static_cast<size_t>(box.height());
    draw_list.commands.reserve(std::min(estimated, kMaxDrawCommands));

    std::vector<std::vector<Utf8Glyph>> rows;
    rows.reserve(static_cast<size_t>(box.height()));
    std::vector<std::vector<bool>> consumed;
    consumed.reserve(static_cast<size_t>(box.height()));
    for (const std::string &line : box.lines) {
        rows.push_back(splitUtf8Glyphs(line));
        consumed.emplace_back(rows.back().size(), false);
    }

    bool command_budget_exhausted = false;
    for (int row = 0; row < box.height(); ++row) {
        if (command_budget_exhausted) {
            break;
        }
        const std::vector<Utf8Glyph> &glyphs = rows[static_cast<size_t>(row)];
        const int y = row * row_pitch;
        int col = 0;
        while (col < static_cast<int>(glyphs.size())) {
            if (consumed[static_cast<size_t>(row)][static_cast<size_t>(col)]) {
                ++col;
                continue;
            }
            const Utf8Glyph &g = glyphs[static_cast<size_t>(col)];
            if (tryEmitStretchSymbol(draw_list, rows, consumed, row, col, cell_width, row_pitch, line_height)) {
                ++col;
                continue;
            }
            if ((g.codepoint == static_cast<uint32_t>('-') || g.codepoint == static_cast<uint32_t>('_')) &&
                col + 1 < static_cast<int>(glyphs.size())) {
                int run_end = col;
                while (run_end + 1 < static_cast<int>(glyphs.size()) &&
                       glyphs[static_cast<size_t>(run_end + 1)].codepoint == g.codepoint) {
                    ++run_end;
                }
                if (run_end > col) {
                    if (draw_list.commands.size() >= kMaxDrawCommands) {
                        command_budget_exhausted = true;
                        break;
                    }
                    DrawLineCommand cmd;
                    cmd.x1 = col * cell_width;
                    cmd.x2 = (run_end + 1) * cell_width - 1;
                    cmd.y1 = y + line_height / 2;
                    cmd.y2 = cmd.y1;
                    cmd.width = 1;
                    draw_list.commands.emplace_back(std::move(cmd));
                    col = run_end + 1;
                    continue;
                }
            }

            if (g.codepoint != static_cast<uint32_t>(' ')) {
                if (emitSpecialSymbol(draw_list, g.codepoint, col * cell_width, y, cell_width, line_height)) {
                    ++col;
                    continue;
                }
                if (draw_list.commands.size() >= kMaxDrawCommands) {
                    command_budget_exhausted = true;
                    break;
                }
                DrawGlyphCommand cmd;
                cmd.text = g.text;
                cmd.x = col * cell_width;
                cmd.y = y;
                draw_list.commands.emplace_back(std::move(cmd));
            }
            ++col;
        }
    }

    return draw_list;
}

bool paintDrawListToCanvas(lv_obj_t *canvas,
                           const DrawList &draw_list,
                           const lv_font_t *font,
                           lv_color_t text_color,
                           lv_color_t bg_color,
                           int offset_x,
                           int offset_y)
{
    if (canvas == nullptr || font == nullptr || draw_list.width <= 0 || draw_list.height <= 0) {
        return false;
    }

    ProgressivePaintState state;
    if (!beginProgressivePaintToCanvas(canvas, draw_list, bg_color, state, offset_x, offset_y, nullptr)) {
        return false;
    }

    bool finished = false;
    while (!finished) {
        if (!stepProgressivePaintToCanvas(canvas,
                                          draw_list,
                                          font,
                                          text_color,
                                          state,
                                          draw_list.commands.size() + 1U,
                                          draw_list.commands.size(),
                                          &finished)) {
            return false;
        }
    }
    return true;
}

bool beginProgressivePaintToCanvas(lv_obj_t *canvas,
                                   const DrawList &draw_list,
                                   lv_color_t bg_color,
                                   ProgressivePaintState &state,
                                   int offset_x,
                                   int offset_y,
                                   const PaintViewport *viewport)
{
    if (canvas == nullptr || draw_list.width <= 0 || draw_list.height <= 0) {
        return false;
    }

    PaintViewport vp;
    if (viewport != nullptr) {
        vp = *viewport;
    } else {
        vp.x = 0;
        vp.y = 0;
        vp.width = draw_list.width + std::max(0, offset_x);
        vp.height = draw_list.height + std::max(0, offset_y);
    }
    vp.width = std::max(1, vp.width);
    vp.height = std::max(1, vp.height);

    if (!ensureCanvasStorage(canvas, vp.width, vp.height)) {
        return false;
    }

    lv_obj_set_size(canvas, vp.width, vp.height);
    lv_canvas_fill_bg(canvas, bg_color, LV_OPA_COVER);

    state.next_line_scan = 0;
    state.next_other_scan = 0;
    state.line_pass_done = false;
    state.initialized = true;
    state.offset_x = offset_x - vp.x;
    state.offset_y = offset_y - vp.y;
    state.viewport = {0, 0, vp.width, vp.height};
    return true;
}

bool stepProgressivePaintToCanvas(lv_obj_t *canvas,
                                  const DrawList &draw_list,
                                  const lv_font_t *font,
                                  lv_color_t text_color,
                                  ProgressivePaintState &state,
                                  size_t max_commands,
                                  size_t max_line_commands,
                                  bool *out_finished)
{
    if (out_finished != nullptr) {
        *out_finished = false;
    }
    if (canvas == nullptr || font == nullptr || !state.initialized) {
        return false;
    }

    const size_t cmd_cap = std::max<size_t>(1, max_commands);
    const size_t line_cap = std::max<size_t>(1, max_line_commands);
    size_t drawn_total = 0;
    size_t drawn_lines = 0;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    if (!state.line_pass_done) {
        while (state.next_line_scan < draw_list.commands.size() &&
               drawn_total < cmd_cap &&
               drawn_lines < line_cap) {
            const DrawCommand &cmd = draw_list.commands[state.next_line_scan];
            ++state.next_line_scan;
            if (!std::holds_alternative<DrawLineCommand>(cmd)) {
                continue;
            }
            if (drawOneCommand(layer, cmd, font, text_color, state)) {
                ++drawn_total;
                ++drawn_lines;
            }
        }
        if (state.next_line_scan >= draw_list.commands.size()) {
            state.line_pass_done = true;
        }
    }

    const bool allow_non_line_pass = cmd_cap > line_cap;
    if (allow_non_line_pass && state.line_pass_done && drawn_total < cmd_cap) {
        while (state.next_other_scan < draw_list.commands.size() && drawn_total < cmd_cap) {
            const DrawCommand &cmd = draw_list.commands[state.next_other_scan];
            ++state.next_other_scan;
            if (std::holds_alternative<DrawLineCommand>(cmd)) {
                continue;
            }
            if (drawOneCommand(layer, cmd, font, text_color, state)) {
                ++drawn_total;
            }
        }
    }

    lv_canvas_finish_layer(canvas, &layer);

    const bool finished = state.line_pass_done && state.next_other_scan >= draw_list.commands.size();
    if (out_finished != nullptr) {
        *out_finished = finished;
    }
    return true;
}

bool paintTextBoxToCanvas(lv_obj_t *canvas,
                          const TextBox &box,
                          const lv_font_t *font,
                          lv_color_t text_color,
                          lv_color_t bg_color,
                          DrawList *out_draw_list)
{
    DrawList draw_list = buildDrawList(box, font);
    if (out_draw_list != nullptr) {
        *out_draw_list = draw_list;
    }
    return paintDrawListToCanvas(canvas, draw_list, font, text_color, bg_color);
}

} // namespace xcas::mathlayout