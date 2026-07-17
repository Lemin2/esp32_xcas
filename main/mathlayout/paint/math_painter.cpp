#include "mathlayout/paint/math_painter.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
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
    case 0x220FU: // ∏
    case 0x2211U: // ∑
    case 0x221AU: // √
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

struct LinePointStorage
{
    lv_point_precise_t points[2]{};
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
        addLine(draw_list, vx, top + 1, vx, bottom - 1, 2);
        return true;
    }
    if (codepoint == kSymSqrtArm) {
        addLine(draw_list, left + 1, bottom - 1, right - 1, top + 1, 2);
        return true;
    }
    if (codepoint == kSymSqrtHook) {
        const int p1x = left + 1;
        const int p1y = mid_y;
        const int p2x = left + std::max(2, cell_width / 3);
        const int p2y = bottom - 1;
        const int p3x = right - 1;
        const int p3y = top + 1;
        addLine(draw_list, p1x, p1y, p2x, p2y, 2);
        addLine(draw_list, p2x, p2y, p3x, p3y, 2);
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
        addLine(draw_list, left + 1, top + 1, right - 1, top + 1, 2);
        addLine(draw_list, right - 1, top + 1, left + 1, mid_y, 2);
        return true;
    }
    if (codepoint == kSymSigmaMid) {
        addLine(draw_list, left + 1, mid_y, right - 1, mid_y, 2);
        return true;
    }
    if (codepoint == kSymSigmaBot) {
        addLine(draw_list, left + 1, mid_y, right - 1, bottom - 1, 2);
        addLine(draw_list, left + 1, bottom - 1, right - 1, bottom - 1, 2);
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

void onLineObjectDelete(lv_event_t *e)
{
    if (e == nullptr) {
        return;
    }

    lv_obj_t *line = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (line == nullptr) {
        return;
    }

    auto *storage = static_cast<LinePointStorage *>(lv_obj_get_user_data(line));
    delete storage;
    lv_obj_set_user_data(line, nullptr);
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

lv_obj_t *createSolidObject(lv_obj_t *host,
                            int x,
                            int y,
                            int width,
                            int height,
                            lv_color_t color)
{
    if (host == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    lv_obj_t *obj = lv_obj_create(host);
    if (obj == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    return obj;
}

bool createObjectForCommand(lv_obj_t *host,
                            const DrawCommand &command,
                            const lv_font_t *font,
                            lv_color_t text_color,
                            const LvglObjectPaintState &state)
{
    if (host == nullptr || font == nullptr) {
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

        lv_obj_t *label = lv_label_create(host);
        if (label == nullptr) {
            return false;
        }

        lv_obj_remove_style_all(label);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(label, state.offset_x + glyph->x, state.offset_y + glyph->y);
        lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, glyph_font, LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(label, glyph->text.c_str());
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

        const int line_width = std::max(1, line->width);
        if (x1 == x2 || y1 == y2) {
            const int x = std::min(x1, x2);
            const int y = std::min(y1, y2);
            const int width = (x1 == x2) ? line_width : (std::abs(x2 - x1) + 1);
            const int height = (y1 == y2) ? line_width : (std::abs(y2 - y1) + 1);
            return createSolidObject(host, x, y, width, height, text_color) != nullptr;
        }

        auto *storage = new (std::nothrow) LinePointStorage();
        if (storage == nullptr) {
            return false;
        }

        const int left = std::min(x1, x2);
        const int top = std::min(y1, y2);
        const int width = std::max(1, std::abs(x2 - x1) + line_width + 1);
        const int height = std::max(1, std::abs(y2 - y1) + line_width + 1);
        storage->points[0].x = static_cast<lv_value_precise_t>(x1 - left);
        storage->points[0].y = static_cast<lv_value_precise_t>(y1 - top);
        storage->points[1].x = static_cast<lv_value_precise_t>(x2 - left);
        storage->points[1].y = static_cast<lv_value_precise_t>(y2 - top);

        lv_obj_t *line_obj = lv_line_create(host);
        if (line_obj == nullptr) {
            delete storage;
            return false;
        }

        lv_obj_remove_style_all(line_obj);
        lv_obj_clear_flag(line_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(line_obj, left, top);
        lv_obj_set_size(line_obj, width, height);
        lv_obj_set_style_line_color(line_obj, text_color, LV_PART_MAIN);
        lv_obj_set_style_line_width(line_obj, line_width, LV_PART_MAIN);
        lv_obj_set_style_line_opa(line_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_user_data(line_obj, storage);
        lv_obj_add_event_cb(line_obj, onLineObjectDelete, LV_EVENT_DELETE, nullptr);
        lv_line_set_points(line_obj, storage->points, 2);
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

        const int x1 = std::max(rect_i.x1, viewport.x1);
        const int y1 = std::max(rect_i.y1, viewport.y1);
        const int x2 = std::min(rect_i.x2, viewport.x2);
        const int y2 = std::min(rect_i.y2, viewport.y2);

        lv_obj_t *obj = lv_obj_create(host);
        if (obj == nullptr) {
            return false;
        }

        lv_obj_remove_style_all(obj);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(obj, x1, y1);
        lv_obj_set_size(obj, std::max(1, x2 - x1 + 1), std::max(1, y2 - y1 + 1));
        if (rect->filled) {
            lv_obj_set_style_bg_color(obj, text_color, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_color(obj, text_color, LV_PART_MAIN);
            lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
            lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
            lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
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

bool beginTileRenderToLvglObjects(lv_obj_t *host,
                                  const DrawList &draw_list,
                                  lv_color_t bg_color,
                                  LvglObjectPaintState &state,
                                  int offset_x,
                                  int offset_y,
                                  const PaintViewport *viewport)
{
    if (host == nullptr || draw_list.width <= 0 || draw_list.height <= 0) {
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

    lv_obj_clean(host);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(host, vp.width, vp.height);
    lv_obj_set_style_bg_color(host, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(host, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(host, 0, LV_PART_MAIN);

    state.next_line_scan = 0;
    state.next_other_scan = 0;
    state.line_pass_done = false;
    state.initialized = true;
    state.offset_x = offset_x - vp.x;
    state.offset_y = offset_y - vp.y;
    state.viewport = {0, 0, vp.width, vp.height};
    lv_obj_invalidate(host);
    return true;
}

bool stepTileRenderToLvglObjects(lv_obj_t *host,
                                 const DrawList &draw_list,
                                 const lv_font_t *font,
                                 lv_color_t text_color,
                                 LvglObjectPaintState &state,
                                 size_t max_commands,
                                 size_t max_line_commands,
                                 bool *out_finished)
{
    if (out_finished != nullptr) {
        *out_finished = false;
    }
    if (host == nullptr || font == nullptr || !state.initialized) {
        return false;
    }

    const size_t cmd_cap = std::max<size_t>(1, max_commands);
    const size_t line_cap = std::max<size_t>(1, max_line_commands);
    size_t visited_total = 0;
    size_t visited_lines = 0;

    if (!state.line_pass_done) {
        while (state.next_line_scan < draw_list.commands.size() &&
               visited_total < cmd_cap &&
               visited_lines < line_cap) {
            const DrawCommand &cmd = draw_list.commands[state.next_line_scan];
            ++state.next_line_scan;
            if (!std::holds_alternative<DrawLineCommand>(cmd)) {
                continue;
            }
            (void)createObjectForCommand(host, cmd, font, text_color, state);
            ++visited_total;
            ++visited_lines;
        }
        if (state.next_line_scan >= draw_list.commands.size()) {
            state.line_pass_done = true;
        }
    }

    const bool allow_non_line_pass = cmd_cap > line_cap;
    if (allow_non_line_pass && state.line_pass_done && visited_total < cmd_cap) {
        while (state.next_other_scan < draw_list.commands.size() && visited_total < cmd_cap) {
            const DrawCommand &cmd = draw_list.commands[state.next_other_scan];
            ++state.next_other_scan;
            if (std::holds_alternative<DrawLineCommand>(cmd)) {
                continue;
            }
            (void)createObjectForCommand(host, cmd, font, text_color, state);
            ++visited_total;
        }
    }

    lv_obj_update_layout(host);
    lv_obj_invalidate(host);

    const bool finished = state.line_pass_done && state.next_other_scan >= draw_list.commands.size();
    if (out_finished != nullptr) {
        *out_finished = finished;
    }
    return true;
}

bool renderDrawListToLvglObjects(lv_obj_t *host,
                                 const DrawList &draw_list,
                                 const lv_font_t *font,
                                 lv_color_t text_color,
                                 lv_color_t bg_color,
                                 int offset_x,
                                 int offset_y,
                                 const PaintViewport *viewport)
{
    if (host == nullptr || font == nullptr || draw_list.width <= 0 || draw_list.height <= 0) {
        return false;
    }

    LvglObjectPaintState state;
    if (!beginTileRenderToLvglObjects(host, draw_list, bg_color, state, offset_x, offset_y, viewport)) {
        return false;
    }

    bool finished = false;
    while (!finished) {
        if (!stepTileRenderToLvglObjects(host,
                                         draw_list,
                                         font,
                                         text_color,
                                         state,
                                         256,
                                         192,
                                         &finished)) {
            return false;
        }
    }
    return true;
}

} // namespace xcas::mathlayout
