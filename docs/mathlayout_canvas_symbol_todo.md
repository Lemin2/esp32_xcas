# Mathlayout Canvas 真符号渲染 TODO（2026-07-15）

## 目标
在 canvas 路径实现可自适应的“真数学符号”绘制，减少对字体字形可用性的依赖，重点覆盖矩阵、根号、极限、幂，并验证希腊字母与常用数学字符显示。

## P0 - 渲染内核改造
- [x] 为关键符号添加几何绘制通道（非字形依赖）：
  - [x] 可伸缩矩阵括号（随内容高度拉伸）
  - [x] 可伸缩括号家族（圆括号/方括号/花括号/绝对值竖线）
  - [x] 可伸缩根号（钩+横线覆盖）
  - [x] 大算符（sum/product）可伸缩主体 + 上下限对齐
  - [x] 极限箭头与下标布局（lim 下标块）
  - [ ] 幂/下标在 canvas 下的基线偏移与碰撞修正
- [x] 保持普通文本/变量仍走字体渲染，避免全量手工字形实现
- [x] 维持与现有 DrawCommand 兼容，不破坏 UI 调用层

验收公式：
- sum(k^2,k,1,n)
- product(k,k,1,n)
- limit((sin(x))/x,x,0)
- sqrt((1+x^2)/(1-x))
- [[1,2],[3,4]]
- [x,y,z]

## P1 - 符号集可见性验证
- [x] 新增字符探针公式集（单字符和组合）：
  - [x] 希腊：alpha beta gamma delta theta lambda mu pi sigma omega（大小写）
  - [x] 数学：infinity, <=, >=, !=, +- , ->, <-, <= >?（按 GIAC 输出可达字符）
  - [x] 集合：in, subset, union, intersection（若输入法支持）
- [x] 逐条执行 RENDER_SHOT 并输出截图
- [x] 记录缺字清单（字体缺失 vs 渲染逻辑问题）
- [x] 修复探针脚本 UTF-8 文件读取并复测（编码链路确认）

验收产物：
- captures/symbol_probe/*.png
- captures/bracket_probe/*.png
- docs/mathlayout_symbol_probe_report.md

## P2 - 稳定性与回归
- [ ] 跑批量回归脚本，确认旧样例不退化
- [ ] 对复杂表达式做宽度保护（超过气泡宽度回退 label）
- [ ] 修复或记录未完成项

验收命令：
- powershell -ExecutionPolicy Bypass -File tools/ml_render_batch_test.ps1 -Port COM7 -Baud 460800
- powershell -ExecutionPolicy Bypass -File tools/serial_formula_render_capture.ps1 -Port COM7 -Baud 460800 -FormulasFile tools/mathlayout_formula_export_extended.txt -OutDir captures

## 实施顺序
1. P0：先改几何绘制（核心）
2. P1：补字符探针 + 出报告
3. P2：回归与收尾
