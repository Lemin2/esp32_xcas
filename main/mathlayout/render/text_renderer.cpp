#include "mathlayout/render/text_renderer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace xcas::mathlayout
{

// ── TextBox utilities ────────────────────────────────────────────────────────

int TextBox::width() const
{
    int w = 0;
    for (const auto &l : lines) {
        w = std::max(w, static_cast<int>(l.size()));
    }
    return w;
}

static void padRight(TextBox &box, int target_w)
{
    for (auto &l : box.lines) {
        while (static_cast<int>(l.size()) < target_w) {
            l += ' ';
        }
    }
}

static void padTop(TextBox &box, int extra_rows)
{
    for (int i = 0; i < extra_rows; ++i) {
        box.lines.insert(box.lines.begin(), std::string(static_cast<size_t>(std::max(0, box.width())), ' '));
        ++box.baseline;
    }
}

static void padBottom(TextBox &box, int extra_rows)
{
    const int w = box.width();
    for (int i = 0; i < extra_rows; ++i) {
        box.lines.push_back(std::string(static_cast<size_t>(std::max(0, w)), ' '));
    }
}

// Concatenate two boxes horizontally, aligning on their baselines.
static TextBox hcat(TextBox lhs, TextBox rhs)
{
    if (lhs.empty()) return rhs;
    if (rhs.empty()) return lhs;

    const int base = std::max(lhs.baseline, rhs.baseline);
    const int lhs_below = lhs.height() - lhs.baseline - 1;
    const int rhs_below = rhs.height() - rhs.baseline - 1;
    const int below = std::max(lhs_below, rhs_below);

    const int lhs_top = base - lhs.baseline;
    const int rhs_top = base - rhs.baseline;

    padTop(lhs, lhs_top - 0); // already counted baseline
    padTop(rhs, rhs_top - 0);
    padBottom(lhs, below - lhs_below);
    padBottom(rhs, below - rhs_below);

    const int lw = lhs.width();
    const int rw = rhs.width();
    padRight(lhs, lw);
    padRight(rhs, rw);

    TextBox out;
    out.baseline = base;
    out.lines.resize(static_cast<size_t>(lhs.height()), "");
    for (int i = 0; i < lhs.height(); ++i) {
        out.lines[static_cast<size_t>(i)] = lhs.lines[static_cast<size_t>(i)] + rhs.lines[static_cast<size_t>(i)];
    }
    return out;
}

static TextBox makeText(const std::string &s)
{
    TextBox b;
    b.lines.push_back(s);
    b.baseline = 0;
    return b;
}

// ── Minimal expression parser ────────────────────────────────────────────────
// Supports: number, identifier, fn(args...), base^exp, base_sub,
//           num/den (fraction), sqrt(x), +, -, *, unary -

static std::string localTrim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

static bool hasOuterParens(const std::string &s)
{
    if (s.size() < 2 || s.front() != '(' || s.back() != ')') return false;
    int d = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++d;
        else if (s[i] == ')') { --d; if (d == 0 && i + 1 < s.size()) return false; }
    }
    return d == 0;
}

// Find rightmost top-level occurrence of any char in ops (respects parens/brackets/braces).
static int findRightOp(const std::string &s, const char *ops, bool binary_minus_only = false)
{
    int pd = 0, bd = 0, cd = 0, pos = -1;
    auto is_binary = [&s](int i) {
        if (i <= 0) return false;
        int j = i - 1;
        while (j >= 0 && s[static_cast<size_t>(j)] == ' ') --j;
        if (j < 0) return false;
        char p = s[static_cast<size_t>(j)];
        return !(p == '(' || p == '[' || p == '{' || p == '+' || p == '-' || p == '*' || p == '/' || p == '^' || p == ',');
    };
    for (int i = 0; i < static_cast<int>(s.size()); ++i) {
        char c = s[static_cast<size_t>(i)];
        if (c == '(') ++pd; else if (c == ')') --pd;
        else if (c == '[') ++bd; else if (c == ']') --bd;
        else if (c == '{') ++cd; else if (c == '}') --cd;
        else if (pd == 0 && bd == 0 && cd == 0) {
            for (const char *p = ops; *p; ++p) {
                if (c == *p) {
                    if (binary_minus_only && c == '-' && !is_binary(i)) break;
                    pos = i;
                    break;
                }
            }
        }
    }
    return pos;
}

// Find leftmost ^ or _ at top level (used after all binary ops have been consumed).
static int findLeftScript(const std::string &s, char sym)
{
    int pd = 0, bd = 0, cd = 0;
    for (int i = 0; i < static_cast<int>(s.size()); ++i) {
        char c = s[static_cast<size_t>(i)];
        if (c == '(') ++pd; else if (c == ')') --pd;
        else if (c == '[') ++bd; else if (c == ']') --bd;
        else if (c == '{') ++cd; else if (c == '}') --cd;
        else if (pd == 0 && bd == 0 && cd == 0 && c == sym) return i;
    }
    return -1;
}

// Extract a script operand starting at `start`, advance `next`.
static bool extractScriptOp(const std::string &s, size_t start, std::string &op, size_t &next)
{
    if (start >= s.size()) return false;
    if (s[start] == '{') {
        int d = 1; size_t i = start + 1;
        while (i < s.size() && d > 0) { if (s[i] == '{') ++d; else if (s[i] == '}') --d; ++i; }
        if (d != 0) return false;
        op = localTrim(s.substr(start + 1, i - start - 2));
        next = i; return true;
    }
    if (s[start] == '(' || s[start] == '[') {
        char cl = s[start] == '(' ? ')' : ']';
        int d = 1; size_t i = start + 1;
        while (i < s.size() && d > 0) { if (s[i] == s[start]) ++d; else if (s[i] == cl) --d; ++i; }
        op = localTrim(s.substr(start, i - start));
        next = i; return true;
    }
    size_t i = start;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.')) ++i;
    if (i == start) { op = s.substr(start, 1); next = start + 1; return true; }
    op = s.substr(start, i - start);
    next = i; return true;
}

static std::vector<std::string> splitArgs(const std::string &s)
{
    std::vector<std::string> args;
    int pd = 0, bd = 0; size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') ++pd; else if (c == ')') --pd;
        else if (c == '[') ++bd; else if (c == ']') --bd;
        else if (c == ',' && pd == 0 && bd == 0) {
            args.push_back(localTrim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    args.push_back(localTrim(s.substr(start)));
    return args;
}

// ── Fraction ─────────────────────────────────────────────────────────────────
static TextBox makeFrac(const TextBox &num, const TextBox &den)
{
    const int nw = num.width(), dw = den.width();
    const int w = std::max(nw, dw) + 2;
    auto center = [&](const std::string &t, int target) {
        const int pad = target - static_cast<int>(t.size());
        const int l = pad / 2, r = pad - l;
        return std::string(static_cast<size_t>(std::max(0, l)), ' ') + t
             + std::string(static_cast<size_t>(std::max(0, r)), ' ');
    };
    TextBox out;
    out.baseline = static_cast<int>(num.lines.size()); // fraction bar row
    for (const auto &l : num.lines) out.lines.push_back(center(localTrim(l), w));
    out.lines.push_back(std::string(static_cast<size_t>(std::max(0, w)), '-'));
    for (const auto &l : den.lines) out.lines.push_back(center(localTrim(l), w));
    return out;
}

// ── Superscript (raised right) ────────────────────────────────────────────────
static TextBox makePower(TextBox base, TextBox exp)
{
    // exp sits at the top-right of base (raised by exp.height rows)
    padRight(base, base.width());
    padRight(exp,  exp.width());
    const int bw = base.width(), ew = exp.width();
    const int eh = exp.height(), bh = base.height();
    const int total = eh + bh;
    TextBox out;
    out.baseline = eh + base.baseline;
    out.lines.resize(static_cast<size_t>(total), std::string(static_cast<size_t>(bw + ew), ' '));
    for (int i = 0; i < eh; ++i)
        out.lines[static_cast<size_t>(i)] = std::string(static_cast<size_t>(bw), ' ') + exp.lines[static_cast<size_t>(i)];
    for (int i = 0; i < bh; ++i)
        out.lines[static_cast<size_t>(eh + i)] = base.lines[static_cast<size_t>(i)] + std::string(static_cast<size_t>(ew), ' ');
    return out;
}

// ── Subscript (lowered right) ─────────────────────────────────────────────────
static TextBox makeSub(TextBox base, TextBox sub)
{
    padRight(base, base.width());
    padRight(sub,  sub.width());
    const int bw = base.width(), sw = sub.width();
    const int bh = base.height(), sh = sub.height();
    TextBox out;
    out.baseline = base.baseline;
    out.lines.resize(static_cast<size_t>(bh + sh), std::string(static_cast<size_t>(bw + sw), ' '));
    for (int i = 0; i < bh; ++i)
        out.lines[static_cast<size_t>(i)] = base.lines[static_cast<size_t>(i)] + std::string(static_cast<size_t>(sw), ' ');
    for (int i = 0; i < sh; ++i)
        out.lines[static_cast<size_t>(bh + i)] = std::string(static_cast<size_t>(bw), ' ') + sub.lines[static_cast<size_t>(i)];
    return out;
}

// ── Square root ───────────────────────────────────────────────────────────────
static TextBox makeSqrt(TextBox inner)
{
    padRight(inner, inner.width());
    const int iw = inner.width(), ih = inner.height();
    TextBox out;
    out.baseline = inner.baseline + 1; // +1 for overline row
    // overline row
    out.lines.push_back("  " + std::string(static_cast<size_t>(std::max(0, iw)), '_'));
    // body rows: prepend radical symbol column
    for (int i = 0; i < ih; ++i) {
        const bool is_baseline = (i == inner.baseline);
        out.lines.push_back((is_baseline ? "\\/" : " |") + inner.lines[static_cast<size_t>(i)]);
    }
    return out;
}

// ── Main recursive renderer ───────────────────────────────────────────────────
TextBox renderText(const std::string &expr, int depth)
{
    if (depth > 10) return makeText(expr);

    std::string s = localTrim(expr);
    if (s.empty()) return makeText("");

    // Strip balanced outer parens only if the expression is fully wrapped.
    while (hasOuterParens(s)) {
        s = localTrim(s.substr(1, s.size() - 2));
    }

    // = (relation)
    {
        const int p = findRightOp(s, "=");
        if (p > 0 && p + 1 < static_cast<int>(s.size())) {
            TextBox lhsb = renderText(s.substr(0, static_cast<size_t>(p)), depth + 1);
            TextBox op   = makeText(" = ");
            TextBox rhsb = renderText(s.substr(static_cast<size_t>(p + 1)), depth + 1);
            return hcat(hcat(lhsb, op), rhsb);
        }
    }

    // + -
    {
        const int p = findRightOp(s, "+-", true);
        if (p > 0 && p + 1 < static_cast<int>(s.size())) {
            const char op_c = s[static_cast<size_t>(p)];
            TextBox lhsb = renderText(s.substr(0, static_cast<size_t>(p)), depth + 1);
            TextBox op   = makeText(op_c == '+' ? " + " : " - ");
            TextBox rhsb = renderText(s.substr(static_cast<size_t>(p + 1)), depth + 1);
            return hcat(hcat(lhsb, op), rhsb);
        }
    }

    // *
    {
        const int p = findRightOp(s, "*");
        if (p > 0 && p + 1 < static_cast<int>(s.size())) {
            TextBox lhsb = renderText(s.substr(0, static_cast<size_t>(p)), depth + 1);
            TextBox op   = makeText(" * ");
            TextBox rhsb = renderText(s.substr(static_cast<size_t>(p + 1)), depth + 1);
            return hcat(hcat(lhsb, op), rhsb);
        }
    }

    // / (fraction)
    {
        const int p = findRightOp(s, "/");
        if (p > 0 && p + 1 < static_cast<int>(s.size())) {
            TextBox num = renderText(s.substr(0, static_cast<size_t>(p)), depth + 1);
            TextBox den = renderText(s.substr(static_cast<size_t>(p + 1)), depth + 1);
            return makeFrac(num, den);
        }
    }

    // ^ and _ scripts (leftmost first, consume chain)
    {
        const int pp = findLeftScript(s, '^');
        const int sp = findLeftScript(s, '_');
        const int fp = (pp < 0) ? sp : ((sp < 0) ? pp : std::min(pp, sp));
        if (fp > 0) {
            const std::string base_src = s.substr(0, static_cast<size_t>(fp));
            TextBox base = renderText(base_src, depth + 1);
            std::string sup_src, sub_src;
            size_t i = static_cast<size_t>(fp);
            bool any = false;
            while (i < s.size()) {
                const char sym = s[i];
                if (sym != '^' && sym != '_') break;
                std::string op; size_t nxt;
                if (!extractScriptOp(s, i + 1, op, nxt)) break;
                any = true;
                if (sym == '^' && sup_src.empty()) sup_src = op;
                else if (sym == '_' && sub_src.empty()) sub_src = op;
                i = nxt;
            }
            if (any && i == s.size()) {
                if (!sub_src.empty()) base = makeSub(base, renderText(sub_src, depth + 1));
                if (!sup_src.empty()) base = makePower(base, renderText(sup_src, depth + 1));
                return base;
            }
        }
    }

    // Function call: name(args...)
    {
        const size_t lp = s.find('(');
        if (lp != std::string::npos && s.back() == ')' && lp > 0) {
            const std::string fn  = localTrim(s.substr(0, lp));
            const std::string arg = s.substr(lp + 1, s.size() - lp - 2);
            const auto args       = splitArgs(arg);

            if ((fn == "sqrt" || fn == "root") && args.size() == 1) {
                return makeSqrt(renderText(args[0], depth + 1));
            }

            // ── D1: sum(body, var, lo, hi) ──────────────────────────────────
            if ((fn == "sum" || fn == "sigma") && args.size() >= 4) {
                // Layout:
                //   hi
                //   Σ  body        ← baseline
                //   var=lo
                const std::string body_s = localTrim(args[0]);
                const std::string var_s  = localTrim(args[1]);
                const std::string lo_s   = localTrim(args[2]);
                const std::string hi_s   = localTrim(args[3]);
                TextBox body = renderText(body_s, depth + 1);
                TextBox hi   = renderText(hi_s,   depth + 1);
                TextBox lo_v = renderText(var_s + "=" + lo_s, depth + 1);
                // sigma column: hi / Sigma / lo
                const int sym_w = std::max({hi.width(), 1, lo_v.width()});
                auto centerStr = [&](const TextBox &b, int w) {
                    std::string flat;
                    for (const auto &l : b.lines) flat += localTrim(l);
                    const int pad = w - static_cast<int>(flat.size());
                    const int lp = std::max(0, pad / 2), rp = std::max(0, pad - lp);
                    return std::string(static_cast<size_t>(lp), ' ') + flat + std::string(static_cast<size_t>(rp), ' ');
                };
                TextBox sigma;
                sigma.lines.push_back(centerStr(hi,   sym_w));
                sigma.lines.push_back(centerStr(makeText("S"), sym_w)); // 'S' for Sigma
                sigma.lines.push_back(centerStr(lo_v, sym_w));
                sigma.baseline = 1; // Sigma row
                return hcat(hcat(sigma, makeText(" ")), body);
            }

            // ── D1: product(body, var, lo, hi) ──────────────────────────────
            if ((fn == "product" || fn == "prod") && args.size() >= 4) {
                const std::string body_s = localTrim(args[0]);
                const std::string var_s  = localTrim(args[1]);
                const std::string lo_s   = localTrim(args[2]);
                const std::string hi_s   = localTrim(args[3]);
                TextBox body = renderText(body_s, depth + 1);
                TextBox hi   = renderText(hi_s,   depth + 1);
                TextBox lo_v = renderText(var_s + "=" + lo_s, depth + 1);
                const int sym_w = std::max({hi.width(), 1, lo_v.width()});
                auto centerStr = [&](const TextBox &b, int w) {
                    std::string flat;
                    for (const auto &l : b.lines) flat += localTrim(l);
                    const int pad = w - static_cast<int>(flat.size());
                    const int lp = std::max(0, pad / 2), rp = std::max(0, pad - lp);
                    return std::string(static_cast<size_t>(lp), ' ') + flat + std::string(static_cast<size_t>(rp), ' ');
                };
                TextBox pi_box;
                pi_box.lines.push_back(centerStr(hi,   sym_w));
                pi_box.lines.push_back(centerStr(makeText("P"), sym_w)); // 'P' for Pi
                pi_box.lines.push_back(centerStr(lo_v, sym_w));
                pi_box.baseline = 1;
                return hcat(hcat(pi_box, makeText(" ")), body);
            }

            // ── D2: limit(body, var, approach) ──────────────────────────────
            if ((fn == "limit" || fn == "lim") && args.size() >= 3) {
                // Layout:
                //   lim     body    ← baseline
                //  var->at
                const std::string body_s = localTrim(args[0]);
                const std::string var_s  = localTrim(args[1]);
                const std::string at_s   = localTrim(args[2]);
                TextBox body    = renderText(body_s,            depth + 1);
                TextBox under   = makeText(var_s + "->" + at_s);
                // lim column: "lim" on top, limit subscript below
                const int col_w = std::max(3, under.width());
                auto centerLine = [](const std::string &t, int w) {
                    const int pad = w - static_cast<int>(t.size());
                    const int lp = std::max(0, pad / 2), rp = std::max(0, pad - lp);
                    return std::string(static_cast<size_t>(lp), ' ') + t + std::string(static_cast<size_t>(rp), ' ');
                };
                TextBox lim_col;
                lim_col.lines.push_back(centerLine("lim", col_w));
                lim_col.lines.push_back(centerLine(under.lines.front(), col_w));
                lim_col.baseline = 0; // "lim" row is baseline
                return hcat(hcat(lim_col, makeText(" ")), body);
            }

            // ── D3: matrix([[...],[...]]) ────────────────────────────────────
            if (fn == "matrix" && !args.empty()) {
                // args[0] is the whole row-list: [[r0c0,r0c1,...],[r1c0,...]]
                // Strip outer [ ] to get comma-separated row strings like [r0c0,r0c1],[r1c0,...]
                std::string raw = localTrim(args[0]);
                // Remove outermost [[ ... ]] if present
                if (raw.size() >= 4 && raw.front() == '[' && raw[1] == '[' &&
                    raw.back() == ']' && raw[raw.size()-2] == ']') {
                    raw = localTrim(raw.substr(1, raw.size() - 2)); // strip outer [ ]
                } else if (raw.size() >= 2 && raw.front() == '[' && raw.back() == ']') {
                    raw = localTrim(raw.substr(1, raw.size() - 2));
                }
                // Split by top-level '],['  to get individual rows
                // Each row is like [r0c0,r0c1] or r0c0,r0c1
                std::vector<std::string> row_strs;
                {
                    int pd = 0, bd = 0; size_t start = 0;
                    for (size_t ki = 0; ki < raw.size(); ++ki) {
                        char c = raw[ki];
                        if (c == '(') ++pd; else if (c == ')') --pd;
                        else if (c == '[') ++bd; else if (c == ']') {
                            --bd;
                            // split point is after ']' when bracket depth goes back to 0
                            if (bd == 0 && pd == 0) {
                                // check next non-space is ','
                                size_t nxt = ki + 1;
                                while (nxt < raw.size() && raw[nxt] == ' ') ++nxt;
                                if (nxt < raw.size() && raw[nxt] == ',') {
                                    row_strs.push_back(localTrim(raw.substr(start, ki - start + 1)));
                                    start = nxt + 1;
                                    ki = nxt;
                                }
                            }
                        }
                    }
                    row_strs.push_back(localTrim(raw.substr(start)));
                }
                // Render each cell
                std::vector<std::vector<TextBox>> cells;
                for (const auto &row_str : row_strs) {
                    std::string rr = localTrim(row_str);
                    if (rr.size() >= 2 && rr.front() == '[' && rr.back() == ']')
                        rr = localTrim(rr.substr(1, rr.size() - 2));
                    const auto col_strs = splitArgs(rr);
                    std::vector<TextBox> row_cells;
                    for (const auto &cs : col_strs)
                        row_cells.push_back(renderText(localTrim(cs), depth + 1));
                    cells.push_back(std::move(row_cells));
                }
                if (cells.empty()) return makeText("[]");
                const size_t ncols = cells[0].size();
                const size_t nrows = cells.size();
                std::vector<int> col_w(ncols, 1);
                for (const auto &row : cells)
                    for (size_t c = 0; c < row.size(); ++c)
                        col_w[c] = std::max(col_w[c], row[c].width());
                std::vector<int> row_h(nrows, 1);
                for (size_t r = 0; r < nrows; ++r)
                    for (const auto &cell : cells[r])
                        row_h[r] = std::max(row_h[r], cell.height());
                const int total_h = [&]{ int h=0; for(int v:row_h) h+=v; return h; }();
                TextBox mat;
                mat.baseline = total_h / 2; // will be adjusted below for sep rows
                int abs_row = 0;
                int baseline_adj = 0; // how many sep rows fall before baseline
                for (size_t r = 0; r < nrows; ++r) {
                    const bool is_first_content_row = (r == 0);
                    const bool is_last_content_row  = (r + 1 == nrows);
                    for (int sub = 0; sub < row_h[r]; ++sub) {
                        const bool is_first_abs = (is_first_content_row && sub == 0);
                        const bool is_last_abs  = (is_last_content_row  && sub == row_h[r] - 1);
                        const char *lb = is_first_abs ? "/" : (is_last_abs ? "\\" : "|");
                        const char *rb = is_first_abs ? "\\" : (is_last_abs ? "/" : "|");
                        std::string line;
                        line += lb;
                        line += " ";
                        for (size_t c = 0; c < ncols; ++c) {
                            const TextBox &cell = cells[r][c];
                            const int top_pad = (row_h[r] - cell.height()) / 2;
                            std::string cell_line;
                            if (sub < top_pad || sub >= top_pad + cell.height()) {
                                cell_line = std::string(static_cast<size_t>(col_w[c]), ' ');
                            } else {
                                const int ci = sub - top_pad;
                                cell_line = ci < cell.height()
                                    ? cell.lines[static_cast<size_t>(ci)] : "";
                                while (static_cast<int>(cell_line.size()) < col_w[c])
                                    cell_line += ' ';
                            }
                            line += cell_line;
                            if (c + 1 < ncols) line += " | ";
                        }
                        line += " ";
                        line += rb;
                        mat.lines.push_back(line);
                        ++abs_row;
                    }
                    // Horizontal row separator between matrix rows (not after last row)
                    if (r + 1 < nrows) {
                        std::string sep_line;
                        sep_line += "|";
                        sep_line += "-";
                        for (size_t c = 0; c < ncols; ++c) {
                            sep_line += std::string(static_cast<size_t>(col_w[c]), '-');
                            if (c + 1 < ncols) sep_line += "-+-";
                        }
                        sep_line += "-";
                        sep_line += "|";
                        mat.lines.push_back(sep_line);
                        // If this separator falls before the baseline, push baseline down
                        if (abs_row <= total_h / 2) ++baseline_adj;
                        ++abs_row;
                    }
                }
                mat.baseline = total_h / 2 + baseline_adj;
                return mat;
            }

            // ── D4: Tall parentheses — wrap any sub-expression with parens ──
            // (handled transparently by the outer-paren strip above for single-
            //  arg calls; explicit "paren(expr)" form for testing)
            if (fn == "paren" && args.size() == 1) {
                TextBox inner = renderText(args[0], depth + 1);
                const int h = inner.height();
                if (h <= 1) return hcat(hcat(makeText("("), inner), makeText(")"));
                // Build tall bracket column
                TextBox lb, rb;
                for (int i = 0; i < h; ++i) {
                    lb.lines.push_back(i == 0 ? "/" : (i + 1 == h ? "\\" : "|"));
                    rb.lines.push_back(i == 0 ? "\\" : (i + 1 == h ? "/" : "|"));
                }
                lb.baseline = inner.baseline;
                rb.baseline = inner.baseline;
                return hcat(hcat(lb, inner), rb);
            }
            TextBox result = makeText(fn + "(");
            for (size_t k = 0; k < args.size(); ++k) {
                if (k > 0) result = hcat(result, makeText(", "));
                result = hcat(result, renderText(args[k], depth + 1));
            }
            result = hcat(result, makeText(")"));
            return result;
        }
    }

    // [[...],[...]] matrix literal (no function wrapper)
    if (s.size() >= 4 && s.front() == '[' && s[1] == '[' &&
        s.back() == ']' && s[s.size() - 2] == ']') {
        return renderText("matrix(" + s + ")", depth + 1);
    }

    // Atom
    return makeText(s);
}

// ── Output ────────────────────────────────────────────────────────────────────
std::string textBoxToString(const TextBox &box)
{
    if (box.empty()) return "";
    std::string out;
    for (size_t i = 0; i < box.lines.size(); ++i) {
        if (i > 0) out += '\n';
        out += box.lines[i];
    }
    return out;
}

} // namespace xcas::mathlayout
