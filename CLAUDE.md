# Boringshot — 工作约定

规划文档：[`docs/PLAN.md`](docs/PLAN.md)（Snipaste-like 截图标注 + CAD 式多重引线）。

## 风格：酷 · 稳健 · 迅捷 · 敏捷 · 高效

- **酷**：交互与视觉有记忆点（MLEADER、呼出水波纹、书法彩蛋），但不花哨堆砌。
- **稳健**：边界情况显式处理（零长度、空文字、非法间距）；不承诺做不到的事（水印≠防截图）。
- **迅捷**：常驻模式 CPU 可忽略——预渲染缓存、按需重绘，不做每帧排版。
- **敏捷**：小步提交，每步可编译、可测试、可发布。
- **高效**：零第三方依赖优先；核心逻辑纯 C++20，与平台层解耦。

## 硬性规则

1. **写入都要测试**：每个新增/修改的逻辑都要有对应测试（`tests/`），提交前本地跑通：
   ```sh
   cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
   ```
   平台层（`app/win`）无法在 Linux 单测时，至少提供 `--smoke` 自检并在 Windows CI 中运行。
2. **Action 编译安装包默认走 Release**：`.github/workflows/build.yml` 的 `build_type` 默认 `Release`；
   CMake 单配置生成器未指定时也默认 `Release`。
3. **自带运行时**：MSVC 使用 `/MT` 静态 CRT（`CMAKE_MSVC_RUNTIME_LIBRARY`），用户无需安装 VC++ Redist。
4. 警告即错误（GCC/Clang `-Wall -Wextra -Wconversion -Werror`，MSVC `/W4 /WX`）。

## 目录

| 路径 | 内容 |
|---|---|
| `core/` | 平台无关核心：MLEADER 几何与交互状态机、命令栈、水印模板与平铺 |
| `app/win/` | Win32 常驻外壳：单例、托盘、全局热键、穿透水印覆盖层（D2D + UpdateLayeredWindow） |
| `tests/` | 零依赖单元测试（自带极简测试框架） |
| `packaging/` | Inno Setup 安装脚本 |
