#pragma once

#include <string>
#include <vector>

namespace xcas::mathlayout
{

// A 2D character canvas with a baseline row index.
// row 0 = topmost line.
struct TextBox
{
    std::vector<std::string> lines;
    int baseline = 0; // row index of the math baseline

    bool empty() const { return lines.empty(); }
    int height() const { return static_cast<int>(lines.size()); }
    int width() const;
};

// Render a mathematical expression string to a TextBox using a recursive
// descent parser that understands / ^ _ sqrt(...) and basic arithmetic.
// No LVGL or font metrics required — pure character-grid layout.
TextBox renderText(const std::string &expr, int depth = 0);

// Flatten a TextBox to a single printable string (lines joined with \n).
std::string textBoxToString(const TextBox &box);

} // namespace xcas::mathlayout
