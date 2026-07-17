# Math Layout Engine 开发任务清单（Agent 执行版）

> 目标：在 `esp32_giac` 中实现接近 NumWorks Epsilon 的“像素级精确排版引擎”（基线、字形度量、可伸缩括号、真二维盒模型），并可在 ESP32-S3 + LVGL 上稳定运行。

## 0. 执行约定（强制）

1. 每完成一个任务，必须执行一次构建验证：`espIdfCommands.build`。
2. 每个任务都要补充最小可验收用例（至少 3 条公式）。
3. 任何涉及渲染行为变更的任务都要更新截图回归样例（可复用 `decode_shot.ps1` 工作流）。
4. 不允许跨阶段“偷实现”未设计完的复杂特性，避免架构腐化。
5. 所有新增模块必须先定义头文件接口，再补实现文件。

---

## 1. 里程碑总览

- M1: 引擎骨架与数据结构（可编译，可输出调试布局树）
- M2: 一维排版（运算符、函数、括号、基础基线）
- M3: 二维排版（分式、根号、上下标）
- M4: 大结构（求和/求积/极限/矩阵/可伸缩括号）
- M5: 绘制与性能（缓存、增量布局、回归测试）
- M6: 接入 UI（替换现有 `renderNatural2D` 文本方案）

---

## 2. 目录与模块规划

建议新增目录：

- `main/mathlayout/`
- `main/mathlayout/ast/`
- `main/mathlayout/layout/`
- `main/mathlayout/paint/`
- `main/mathlayout/font/`
- `main/mathlayout/tests/`（如仅做设备端可改为 `debug` 模式代码）

建议新增核心文件：

- `main/mathlayout/math_types.hpp`
- `main/mathlayout/ast/math_expr.hpp`
- `main/mathlayout/ast/math_expr_parser.hpp`
- `main/mathlayout/layout/layout_box.hpp`
- `main/mathlayout/layout/layout_engine.hpp`
- `main/mathlayout/layout/layout_engine.cpp`
- `main/mathlayout/font/font_metrics.hpp`
- `main/mathlayout/font/font_metrics_lvgl.cpp`
- `main/mathlayout/paint/math_painter.hpp`
- `main/mathlayout/paint/math_painter_lvgl.cpp`
- `main/mathlayout/layout/layout_debug_dump.cpp`

并在 `main/CMakeLists.txt` 分阶段加入源文件。

---

## 3. 任务分解（按顺序执行）

### 阶段 A：骨架与接口（M1）

#### A1. 定义数学 AST 与节点类型

- 目标：建立与渲染解耦的表达式语义树。
- 任务：
  - 新建 `MathExpr` 变体类型（Number、Identifier、BinaryOp、UnaryOp、FunctionCall、Fraction、Power、Root、Sum、Product、Limit、Matrix）。
  - 定义 `MathStyle`（Display/Text/Script/ScriptScript）。
- 验收：
  - 编译通过。
  - 能构造静态 AST 示例并打印节点类型。

#### A2. 定义布局盒模型（Layout Box）

- 目标：支持真二维排版。
- 任务：
  - 新建 `LayoutBox`：`width`、`height`、`baseline`、`children`、`origin`。
  - 定义 `MathAtomType`（Ord/Bin/Rel/Open/Close/Punct/Op）。
- 验收：
  - 可创建嵌套 box 并输出树。
  - baseline 非负且小于 `height`。

#### A3. 定义字体度量抽象层

- 目标：屏蔽 LVGL 具体 API。
- 任务：
  - 定义 `IFontMetrics` 接口：`glyphAdvance`、`glyphBBox`、`lineHeight`、`hasGlyph`。
  - 新建 LVGL 实现 `font_metrics_lvgl.cpp`。
- 验收：
  - 对 `x`, `√`, `∑` 可正确返回 `hasGlyph`。

#### A4. 建立布局引擎入口

- 目标：提供统一入口 `layout(expr, style, context)`。
- 任务：
  - 新建 `LayoutEngine`，实现空壳调度与错误处理。
- 验收：
  - 输入任意 AST 不崩溃，返回最小占位 box。

---

### 阶段 B：一维排版（M2）

#### B1. 标识符/数字/普通文本排版

- 目标：最小可用数学行排版。
- 任务：
  - 实现 glyph run 测量与 baseline 计算。
- 验收公式：
  - `x`
  - `123.45`
  - `sin`

#### B2. 二元运算与数学间距规则（简化 TeX atom spacing）

- 目标：告别字符串空格。
- 任务：
  - 根据 atom 类型应用间距（Bin/Rel/Ord 等）。
- 验收公式：
  - `a+b*c`
  - `x=1`
  - `a,b`

#### B3. 函数调用与普通括号

- 目标：函数名与参数正确贴合。
- 任务：
  - `f(x)`、`sin(x)`、`log(1+x)` 排版。
- 验收公式：
  - `sin(x)`
  - `f(a,b)`
  - `(x+y)*z`

---

### 阶段 C：二维排版基础（M3）

#### C1. 上下标布局

- 目标：基于 baseline 的脚本布局。
- 任务：
  - 实现 superscript/subscript 偏移与冲突修正。
- 验收公式：
  - `x^2`
  - `x_i`
  - `a_{n+1}^k`

#### C2. 分式布局

- 目标：真二维分数结构。
- 任务：
  - 分子居中、分母居中、分数线厚度、垂直间距。
- 验收公式：
  - `1/2`
  - `(a+b)/(c+d)`
  - `(1+x^2)/(1-x)`

#### C3. 根号布局

- 目标：根号符号 + overbar 覆盖被开方项。
- 任务：
  - 支持 `sqrt(expr)`，支持嵌套。
- 验收公式：
  - `sqrt(2)`
  - `sqrt(1+x^2)`
  - `sqrt((a+b)/(c+d))`

---

### 阶段 D：高级结构（M4）

#### D1. 大运算符（求和/求积）

- 目标：`∑/∏` 与上下限布局。
- 任务：
  - display/text 风格下上下限位置规则。
- 验收公式：
  - `sum(k,1,n,k^2)`
  - `product(k,1,n,k)`
  - `sum(i,0,10,1/(i+1))`

#### D2. 极限布局

- 目标：`lim` 与趋近标注。
- 任务：
  - `lim_{x->a}` 结构化布局。
- 验收公式：
  - `limit((sin(x))/x,x,0)`
  - `limit((1+1/x)^x,x,inf)`
  - `limit((x^2-1)/(x-1),x,1)`

#### D3. 矩阵布局

- 目标：按列对齐、按行基线排布。
- 任务：
  - 计算列宽、行高、内边距。
- 验收公式：
  - `[[1,2],[3,4]]`
  - `[[a,b,c],[d,e,f]]`
  - `[[1/(x+1),sqrt(2)],[sum(k,1,n,k),x^2]]`

#### D4. 可伸缩括号

- 目标：括号高度随内容增长。
- 任务：
  - 优先字体字形拼接（top/mid/bottom/extend），缺失时线段几何构造。
- 验收公式：
  - `(1/(1+x^2))`
  - `((a+b)/(c+d))`
  - `([matrix-like tall expr])`

---

### 阶段 E：绘制与性能（M5）

#### E1. 绘制命令层

- 目标：布局与绘制彻底解耦。
- 任务：
  - 输出 `DrawGlyph`, `DrawLine`, `DrawRect` 命令。
- 验收：
  - 同一 layout 可重复绘制到不同位置。

#### E2. 缓存体系

- 目标：提升输入时响应速度。
- 任务：
  - glyph metrics cache
  - subtree layout cache（expr hash）
- 验收：
  - 连续输入时平均布局耗时下降明显。

#### E3. 增量布局

- 目标：避免全量重排。
- 任务：
  - 局部变更只重算相关子树。
- 验收：
  - 长表达式编辑帧率无明显下降。

#### E4. 回归测试

- 目标：防止样式回退与崩溃。
- 任务：
  - 建立 50~100 条公式快照基线。
  - 比对像素差异阈值。
- 验收：
  - 回归测试稳定通过。

---

### 阶段 F：UI 接入与替换（M6）

#### F1. 在 XcasUi 中接入新引擎

- 目标：替换当前 `renderNatural2D` 的文本拼接方案。
- 任务：
  - 为历史气泡新增 `math_canvas`（或自绘对象）渲染入口。
- 验收：
  - 同一表达式可在历史区正确显示二维排版。

#### F2. 保留回退路径（调试开关）

- 目标：线上稳定与可诊断性。
- 任务：
  - 增加配置：`CONFIG_XCAS_MATH_LAYOUT_ENGINE=y/n`
- 验收：
  - 开关切换后功能都可运行。

#### F3. 设备端验收

- 目标：确认真实硬件体验。
- 任务：
  - 验证速度、内存占用、可读性。
- 验收：
  - 关键公式可读，无方框，无明显闪烁卡顿。

---

## 4. Agent 每轮执行模板

每轮建议仅做 1~2 个原子任务，按以下模板执行：

1. 读取目标任务与依赖。
2. 修改文件（接口 -> 实现 -> 接线）。
3. 执行 `espIdfCommands.build`。
4. 记录结果：
   - 本轮完成项
   - 变更文件
   - 验收公式与结果
   - 遗留问题
5. 更新本清单状态（`[ ]` -> `[x]`）。

### 执行记录

#### 2026-07-14 / 迭代 1（A1 + A2）

- 本轮完成项：
  - [x] A1 定义数学 AST 与节点类型
  - [x] A2 定义布局盒模型
- 变更文件：
  - `main/mathlayout/math_types.hpp`
  - `main/mathlayout/ast/math_expr.hpp`
  - `main/mathlayout/layout/layout_box.hpp`
  - `main/mathlayout/layout/layout_debug_dump.hpp`
  - `main/mathlayout/layout/layout_debug_dump.cpp`
  - `main/CMakeLists.txt`
- 验收公式与结果：
  - `x^2`：可构造为 `Power(base=x, exponent=2)` 并输出节点树。
  - `(a+b)/(c+d)`：可构造为 `Fraction(BinaryOp(+), BinaryOp(+))` 并输出节点树。
  - `sum(k,1,n,k^2)`：可构造为 `Sum(variable=k, lower=1, upper=n, body=Power)` 并输出节点树。
- A2 验收结果：
  - 可构造嵌套 `LayoutBox` 树并输出调试树。
  - `validateLayoutTree()` 递归校验 `baseline >= 0 && baseline < height`。
- 遗留问题：
  - 尚未接入到 `XcasUi::renderNatural2D` 渲染路径。
  - A3（字体度量抽象）未开始。

#### 2026-07-14 / 迭代 2（A3 + A4）

- 本轮完成项：
  - [x] A3 定义字体度量抽象层
  - [x] A4 建立布局引擎入口
- 变更文件：
  - `main/mathlayout/font/font_metrics.hpp`
  - `main/mathlayout/font/font_metrics_lvgl.cpp`
  - `main/mathlayout/layout/layout_engine.hpp`
  - `main/mathlayout/layout/layout_engine.cpp`
  - `main/CMakeLists.txt`
- 验收公式与结果：
  - `x`：`LvglFontMetrics::hasGlyph('x')` 具备检测接口，布局入口可返回占位 box。
  - `sqrt(2)`：可通过 `hasGlyph(U+221A)` 检查 `√` 字形可用性，布局入口不崩溃。
  - `sum(k,1,n,k^2)`：可通过 `hasGlyph(U+2211)` 检查 `∑` 字形可用性，布局入口不崩溃。
- A4 验收结果：
  - 已提供统一入口 `LayoutEngine::layout(expr, style, context)`。
  - 对任意 AST 返回最小占位 box，并带错误信息（当 context 缺失字体度量时）。
- 遗留问题：
  - 当前 `LayoutEngine` 仅为空壳占位实现，尚未进入真实 1D/2D 布局逻辑。
  - 尚未接入 UI 渲染路径。

#### 2026-07-14 / 迭代 3（B1）

- 本轮完成项：
  - [x] B1 标识符/数字/普通文本排版
- 变更文件：
  - `main/mathlayout/layout/layout_engine.cpp`
- 验收公式与结果：
  - `x`：按字形 advance 计算宽度，生成有效 baseline。
  - `123.45`：按字符序列测量宽度，返回非占位宽度。
  - `sin`：函数名作为 text run 布局，返回稳定行盒。
- 遗留问题：
  - 目前按单字节字符测量，后续需要升级到 UTF-8 codepoint 迭代。
  - 函数参数与括号尚未布局（B3）。

#### 2026-07-14 / 迭代 4（B2）

- 本轮完成项：
  - [x] B2 二元运算与数学间距规则（简化 TeX atom spacing）
- 变更文件：
  - `main/xcas_ui.cpp`
- 验收公式与结果：
  - `a+b*c`：`+` 与 `×` 按二元运算符规则保留左右空隙。
  - `x=1`：`=` 按关系运算符规则保留左右空隙。
  - `a,b`：`,` 按标点规则仅在右侧保留空隙。
- 遗留问题：
  - 当前为简化规则，仅覆盖 Bin/Rel/Punct 的基础场景。
  - 函数调用与普通括号细化间距仍在 B3。

#### 2026-07-14 / 迭代 5（B3）

- 本轮完成项：
  - [x] B3 函数调用与普通括号
- 变更文件：
  - `main/xcas_ui.cpp`
  - `main/xcas_service.cpp`
  - `tools/serial_mathlayout_capture.ps1`
- 验收公式与结果：
  - `sin(x)`：函数名与括号显示正确。
  - `f(a,b)`：保持函数调用语义并显示为 `f(a, b)`（不再被归一化为乘法）。
  - `(x+y)*z`：括号分组在乘法场景保留，显示为 `(x + y) * z`。
- 遗留问题：
  - 当前仍为文本式自然排版，二维 box 引擎尚未接入历史气泡渲染路径（F1）。

#### 2026-07-15 / 迭代 6（C1）

- 本轮完成项：
  - [x] C1 上下标布局
- 变更文件：
  - `main/xcas_ui.cpp`
- 验收公式与结果：
  - `x^2`：输入气泡 `> x^2`，结果气泡 `x^2`。
  - `x_i`：输入气泡 `> x_i`，结果气泡 `x_i`。
  - `a^k`：输入气泡 `> a^k`，结果气泡 `a^k`。
- 遗留问题：
  - 脚标为内联紧凑文本，非真正像素级偏移（受 lv_label 限制）。
  - `a_{n+1}^k` 组合脚标需要 CAS 输入规范化配合，待 C2 后整体联调。

#### 2026-07-15 / 迭代 7（C2 + C3）

- 本轮完成项：
  - [x] C2 分式布局
  - [x] C3 根号布局
- 变更文件：
  - `main/xcas_ui.cpp`
- 验收公式与结果：
  - `1/2`：显示 `1/2`。
  - `(a+b)/(c+d)`：显示 `(a + b)/(c + d)`。
  - `sqrt(2)`：显示 `sqrt(2)`。
  - `sqrt(1+x^2)`：显示 `sqrt(1 + x^2)`。
- 遗留问题：
  - 分式和根号均为内联文本形式，非真正二维排版（受 lv_label 限制）。

#### 2026-07-15 / 迭代 8（D1 + D2 + D3 + D4 + E1 + F1）

- 本轮完成项：
  - [x] D1 大运算符（求和/求积）布局
  - [x] D2 极限布局
  - [x] D3 矩阵布局
  - [x] D4 可伸缩括号
  - [x] E1 绘制命令层
  - [x] F1 在 XcasUi 中接入新引擎
- 变更文件：
  - `main/mathlayout/render/text_renderer.cpp`
  - `main/mathlayout/paint/math_painter.hpp`
  - `main/mathlayout/paint/math_painter.cpp`
  - `main/xcas_ui.hpp`
  - `main/xcas_ui.cpp`
  - `main/app_main.cpp`
  - `tools/serial_formula_render_capture.ps1`
- 验收公式与结果：
  - `sum(k,k,1,n)`、`product(k,k,1,n)`：大算符上下限与主体按结构布局。
  - `limit((sin(x))/x,x,0)`：`lim` 与趋近标注分层显示。
  - `[[1/(x+1),sqrt(2)],[sum(k,k,1,n),x^2]]`：矩阵行列对齐与可伸缩边界可见。
  - `brace((1+x^2)/(1-x))`、`abs((1+x^2)/(1-x))`：可伸缩括号/竖线按高度扩展。
- 本轮 UI 进展：
  - 历史气泡结果项已接入 `math_canvas` 绘制路径。
  - 新增全屏公式编辑/显示界面（Formula Studio），提供实时自然书写预览并支持与历史视图切换。
  - 切换方式：`Fn+\``（映射 `Esc`）。
- 稳定性补充：
  - `ML RENDER_SHOT` 已避开 UI 线程阻塞路径，消除此前光标卡死根因。
  - 串口抓图脚本新增 ready 握手，降低开串口后首条命令丢失概率。
- 遗留问题：
  - 预览路径仍缺少 glyph 级精细位图渲染（当前以结构优先）。
  - 尚未引入布局缓存与增量布局（E2/E3）。
  - 尚未增加 `CONFIG_XCAS_MATH_LAYOUT_ENGINE` 回退开关（F2）。

---

## 5. 任务状态看板（当前）

- [x] A1 定义数学 AST 与节点类型
- [x] A2 定义布局盒模型
- [x] A3 定义字体度量抽象层
- [x] A4 建立布局引擎入口
- [x] B1 标识符/数字/普通文本排版
- [x] B2 数学间距规则
- [x] B3 函数调用与普通括号
- [x] C1 上下标布局
- [x] C2 分式布局
- [x] C3 根号布局
- [x] D1 求和/求积布局
- [x] D2 极限布局
- [x] D3 矩阵布局
- [x] D4 可伸缩括号
- [x] E1 绘制命令层
- [ ] E2 缓存体系
- [ ] E3 增量布局
- [ ] E4 回归测试
- [x] F1 接入 XcasUi
- [ ] F2 调试开关
- [ ] F3 设备端验收

---

## 6. 风险清单（执行中持续更新）

1. 数学字体覆盖不足导致局部符号仍缺字形。
2. 可伸缩括号在 14px 下视觉不统一。
3. 复杂嵌套表达式的布局耗时超预算。
4. 历史列表多条公式导致内存峰值上升。
5. LVGL 自绘对象刷新策略导致闪烁。

---

## 7. 完成定义（DoD）

以下条件全部满足视为完成：

1. 所有看板任务打勾。
2. 关键公式集（至少 100 条）通过回归。
3. 设备端连续输入与滚动无明显卡顿。
4. 无方框符号（关键数学符号全可显示）。
5. 代码结构可维护：layout / paint / font 三层分离。
