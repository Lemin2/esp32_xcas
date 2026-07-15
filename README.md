# esp32_giac

在 ESP32-S3（M5Stack Cardputer）上运行的 giac/Xcas CAS 固件，UI 基于 LVGL，构建系统基于 ESP-IDF。

## 项目状态

- 目标芯片：ESP32-S3
- 目标板：M5Stack Cardputer（240x135 屏幕，4x14 键盘矩阵）
- 运行模式：GUI（Brookesia 内核 + 多应用）
- 已集成应用：Calc / Graph / Files / Project / Settings
- 额外调试能力：串口截图导出（base64 RGB565）

## 主要组件

- giac（CAS 内核）：`components/giac`
- LVGL：`managed_components/lvgl__lvgl`
- ESP-IDF：通过环境 `IDF_PATH` 提供（不直接作为本仓库源码）

## 构建与烧录

1. 配置 ESP-IDF 环境（v6.0.1 或兼容版本）。
2. 在 VS Code 使用 ESP-IDF 扩展执行构建与烧录。
3. 推荐流程：

- 先 build
- 再 flash

注意：flash 不会自动重新编译，修改代码后必须先 build 再 flash。

## 串口截图功能

设备端：

- 按键触发：`Fn(锁定) + P`
- 输出格式：串口打印
  - `SHOT_BEGIN ...`
  - 多行 `SHOT:<base64>`
  - `SHOT_END`

主机端：

- 使用根目录脚本 `decode_shot.ps1` 将串口日志中的 `SHOT:` 行解码为 PNG。

## 自动化公式截图测试

设备端串口命令（新增）：

- `ML SUBMIT <expr>`: 仅提交公式（不自动截图）
- `ML SHOT`: 立即触发一次截图导出
- `ML RUN <expr>`: 提交公式，等待渲染后自动截图
- `ML HELP`: 打印命令帮助

主机端批量测试脚本：

- `tools/serial_mathlayout_capture.ps1`

示例：

- `powershell -ExecutionPolicy Bypass -File tools/serial_mathlayout_capture.ps1 -Port COM7 -Baud 460800`

该脚本将：

1. 从 `tools/mathlayout_formulas.txt` 读取公式。
2. 逐条发送 `ML RUN <expr>` 到串口。
3. 捕获 `SHOT_BEGIN/SHOT/SHOT_END` 到 `captures/*.txt`。
4. 自动调用 `decode_shot.ps1` 生成 `captures/*.png`。

## 许可证判断

### 结论

本项目当前最合适的整体分发许可证是：**GPL-3.0-or-later**。

### 依据

- `components/giac/src/*` 头部声明为 “either version 3 of the License, or (at your option) any later version”。
- `components/giac/COPYING` 提供 GNU GPL v3 文本。

由于固件将 giac 与本项目代码链接并一起分发，整体分发应遵循 GPLv3+ 约束。

### 与其他组件的关系

- LVGL（MIT）与 GPLv3 兼容。
- ESP-IDF（Apache-2.0）与 GPLv3 兼容。
- 各第三方组件仍保持其原始许可证与版权声明。

## 许可证文件

- 根目录 `LICENSE`：GNU GPL v3 文本（来自 `components/giac/COPYING`）
- 第三方组件许可证：
  - giac：`components/giac/COPYING`
  - lvgl：`managed_components/lvgl__lvgl/LICENCE.txt`

## 免责声明

本项目按“现状”提供，不附带任何明示或暗示担保。