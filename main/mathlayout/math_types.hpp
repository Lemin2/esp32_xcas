#pragma once

namespace xcas::mathlayout
{

enum class MathStyle
{
    Display,
    Text,
    Script,
    ScriptScript,
};

enum class MathAtomType
{
    Ord,
    Bin,
    Rel,
    Open,
    Close,
    Punct,
    Op,
};

} // namespace xcas::mathlayout
