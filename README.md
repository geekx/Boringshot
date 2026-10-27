# Boringshot

Windows 原生截图标注工具：CAD 式多重引线（MLEADER）、常驻水印 / 遮罩、呼出动画、手写笔书法彩蛋。
风格：**酷 · 稳健 · 迅捷 · 敏捷 · 高效**。完整规划见 [`docs/PLAN.md`](docs/PLAN.md)，工作约定见 [`CLAUDE.md`](CLAUDE.md)。

## 已具备

| 功能 | 操作 |
|---|---|
| 呼出截图 | `F1` / `Ctrl+Alt+A` / 托盘左键（热键可在 config.ini 自定义）；呼出时水波纹 + 弹簧抖动 |
| 窗口自动识别 | 悬停高亮窗口（DWM 真实边框），单击选中；拖拽自定义区域；空白处吸附整屏 |
| 选区调整 | 8 个手柄缩放、拖动移动、方向键微移（`Shift` ×10） |
| 取色 / 放大镜 | 15×15 像素放大；`C` 复制 HEX，`Shift+C` 复制 RGB；方向键逐像素移动光标 |
| 标注 | 矩形、椭圆、直线、箭头、画笔、文本、多重引线 MLEADER、马赛克/模糊；6 色 × 3 粗细；数字键 `1-8` 切工具 |
| CMF 样式条 | 随工具/选中对象出现：线型（实/虚/点/点划）、**头/尾端形状**（无/三角/开放箭头/圆点/斜刻度/方块）、字体（雅黑/宋体/楷体/Segoe/等宽）、**粗体/斜体**、列表（`•` / `1. 2. …`，行首 `* ` 自动成圆点）、文字底衬、马赛克/模糊。左键下一项，右键上一项；改动作用于当前对象并成为新默认 |
| 调整已有标注 | 单击拾取任意标注 → 拖**关节点**（矩形四角、线两端、引线箭头尖/折点/横肩端）或拖本体移动；`Shift` 吸附 45°/正方；方向键微移；`Delete` 删除；双击文字/引线标签原地改字 |
| 角度显示 | 绘制或调整引线/直线时，折点处实时显示倾斜角弧线与度数 |
| 编辑 | `Ctrl+Z` 撤回、`Ctrl+Y` / `Ctrl+Shift+Z` 重做（拖动一次 = 一步）；右键取消当前操作 / 取消拾取 / 重选 |
| 输出 | `Enter` / `Ctrl+C` / 双击选区 复制；`Ctrl+S` 存 PNG；`Esc` 退出 |
| 贴图 | `F3` / `Ctrl+T` 钉到桌面：拖动移动、滚轮缩放、`Ctrl+滚轮` 透明度、**双击 / Esc 消失**、右键菜单 |
| 常驻水印 | `Ctrl+Alt+W`：穿透置顶覆盖层（D2D 预渲染 → `UpdateLayeredWindow`，稳态零开销） |
| 设置 | 托盘「设置…」打开 `%APPDATA%\Boringshot\config.ini`，保存即生效；热键被占用 / 配置写错会弹托盘提示 |
| 截屏后端 | `auto`：Win11 用 Windows.Graphics.Capture（可关黄框），其余 GDI；失败自动回退 |

代码分层：`core/`（平台无关、全部单测）→ `app/win/`（Win32 + Direct2D/DirectWrite/WIC）。

## 构建与测试

```sh
cmake -S . -B build -G Ninja      # 默认 Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows（MSVC，静态 CRT，无需 VC++ Redist）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release   # 含 boringshot --smoke 覆盖层自检
```

## 安装包

GitHub Actions `build` 工作流默认以 **Release** 构建，产出 `Boringshot-<ver>-win64-setup.exe`（Inno Setup，免管理员）与便携 zip；打 `v*` tag 时自动发布到 Releases。
