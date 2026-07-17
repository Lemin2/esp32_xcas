# Mathlayout Symbol Probe Report（2026-07-15）

## 测试范围
- 希腊字母：小写/大写常用集合
- 常用数学字符：∞, ≤, ≥, ≠, ±, ∈, ∪, ∩, →
- 组合表达式：α+β=γ, x→∞, x≤y 等

## 测试方法
- 设备端命令：ML RENDER_SHOT
- 抓图脚本：tools/serial_formula_render_capture.ps1
- 用例文件：tools/mathlayout_symbol_probe_cases.txt
- 输出目录：captures/symbol_probe

补充验证：
- 括号专项用例：tools/mathlayout_bracket_probe_cases.txt
- 输出目录：captures/bracket_probe

## 结果结论
1. 关键结构符号（几何绘制）
- 矩阵括号、向量括号、根号钩线、极限箭头、大算符主体均可见，且会随行高/内容高度变化。
- 圆括号、方括号、花括号、绝对值竖线已切换为 marker + canvas 几何绘制，括号专项截图均可见。

2. 字体字形字符（非几何绘制）
- 希腊字母与 ∞ 在当前 UI 字体链路中大多显示为 tofu 方块（缺字）。
- 关系/集合类字符（≤ ≥ ≠ ∈ ∪ ∩）同样存在缺字现象，个别字符可见性取决于字体覆盖。

3. 根因判断
- 渲染链路 UTF-8 发送与文件读取已修正（串口脚本写入 UTF8，读取公式文件使用 `Get-Content -Encoding UTF8`）。
- 主要问题为目标字体缺少对应字形，不是 canvas 绘制命令错误。

## 证据截图（示例）
- 矩阵可伸缩符号：captures/06_expr.png
- 向量可伸缩符号：captures/08_expr.png
- 极限箭头：captures/03_expr.png
- 括号家族专项：captures/bracket_probe/01_expr.png ~ 05_expr.png
- 希腊字母缺字示例：captures/symbol_probe/01_expr.png
- infinity 缺字示例：captures/symbol_probe/18_expr.png

## 建议下一步
1. 字体方案
- 扩展现有字体子集，加入 Greek + Mathematical Operators + Arrows + Set Symbols。
- 或引入二级 fallback font，并在 textFont14 组合链中优先补数学字符覆盖。

2. 过渡性 fallback
- 对已知缺字字符增加降级映射（例如 α->alpha, ∞->inf），仅在 hasGlyph=false 时触发，避免 tofu。

3. 回归门禁
- 将 symbol_probe 用例加入常规批量渲染回归，按“是否出现 tofu”做自动检查。
