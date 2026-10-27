
# Snipaste-like 截图标注工具 + 多重引线（Multileader）整体技术规划

> 版本：v1.0  
> 定位：Windows 原生截图/标注工具，核心差异化为 CAD 式多重引线、常驻水印/遮罩、呼出动画与手写笔书法彩蛋。  
> 技术约束：尽量纯 C/C++（C++17/20），WinUI 3 + Material Design 3，自包含 MSVC 运行时与 Windows App SDK。

---

## 目录

1. 项目定位与硬性约束
2. 术语与功能边界
3. 整体架构
4. 模块划分
5. 多重引线（Multileader）详细设计
6. 多重引线 CMF 可配置系统
7. 呼出动画：Siri 水波纹 / Win10 AI 键抖动
8. 常驻动态水印
9. 区域遮罩马赛克 / 模糊
10. 手写笔书法彩蛋（Windows Ink）
11. UI 设计（WinUI 3 + MD3）
12. 常驻后台与系统托盘
13. 部署：自带 MSVC 运行时 + 自包含 WinUI 3
14. 阶段规划
15. Prompt 片段合集（可直接投喂模型）

---

## 1. 项目定位与硬性约束

**目标**：构建一个 Windows 原生截图/标注工具，核心差异化能力是**可交互绘制的多重引线（MLEADER）**，用于对截图内容进行工程制图式的精确说明；同时提供常驻后台的水印/遮罩能力与呼出动画的情绪价值。

**硬性约束**：

- 核心代码尽量纯 C / C++（C++17/20），避免托管运行时依赖；
- UI 使用 **WinUI 3（Windows App SDK）** + **Material Design 3（MDS / MD3）** 风格；
- 安装/分发时**自带 MSVC 运行时**，不要求用户预装 VC++ Redistributable，也不要求安装 Windows App SDK Runtime；
- 多重引线交互形态对标 CAD：点击起点 → 拖动产生折弯斜线 → 自动生成横肩（landing）→ 文字悬浮于横肩上方，且文字随折点左右位置自动切换对齐方向；
- 支持手写笔设备（如平板二合一 Win10 设备）的书法彩蛋功能。

---

## 2. 术语与功能边界

| 术语 | 含义 | 本项目中的角色 |
|---|---|---|
| **Leader / 引线** | 基础“指向 + 说明”几何结构 | 底层图形基元 |
| **Multileader / MLEADER / 多重引线** | 箭头 → 折点 → 横肩 → 文字的完整形态 | **核心实现对象** |
| **Callout** | 广义引出说明，通常复用 Leader 结构 | 不独立实现，视为 Leader 预设样式 |
| **Balloon / 球标** | Leader + 圆形/多边形序号框 | 可作为内容类型扩展点，首期不做 |
| **CMF** | Color / Material / Finish，颜色 / 材质 / 表面处理 | MLEADER、水印、遮罩的样式配置体系 |

MLEADER 标准结构：

```text
箭头尖端（Arrowhead / Target）
    \
     \  ← 引线（Leader Line）
      ●  ← 折点（Kink / Dogleg）
      │
      └──── 横肩（Landing / Shoulder）
             文字（MText / Label），始终位于横肩上方
```

---

## 3. 整体架构

应用运行在两种模式下，共享同一套 Direct2D 设备上下文管理和效果管线：

```text
┌──────────────────────────────────────────────────────────┐
│  UI 层（WinUI 3 / MD3）                                    │
│  工具栏 / 样式面板 / 设置页 / 水印配置面板 / 托盘菜单        │
├──────────────────────────────────────────────────────────┤
│  Composition 合成层                                        │
│  ├── 截图模式：底图 + 注释表面（CompositionDrawingSurface） │
│  └── 常驻模式：全屏透明穿透覆盖层（水印 / 遮罩马赛克）        │
├──────────────────────────────────────────────────────────┤
│  共享 Direct2D 渲染引擎（设备上下文池、效果管线、字体缓存）   │
└──────────────────────────────────────────────────────────┘
```

### 3.1 截图模式图层模型

```text
┌─────────────────────────────────────────────┐
│  UI 层（WinUI 3 / MD3）                      │
│  工具栏、样式面板、颜色选择、保存/复制按钮     │
├─────────────────────────────────────────────┤
│  注释渲染层（Direct2D 透明叠加）               │
│  多重引线、矩形、箭头、马赛克、文字等矢量对象   │
├─────────────────────────────────────────────┤
│  底图层（捕获位图）                            │
│  Desktop Duplication / WGC 捕获结果           │
└─────────────────────────────────────────────┘
```

### 3.2 注释层渲染决策

WinUI 3 中 **SwapChainPanel 的透明混合与 WinUI 内容不完全兼容**，不能直接用 SwapChainPanel 做透明注释叠加。

**推荐方案**：使用 **Microsoft.UI.Composition.CompositionDrawingSurface**，通过 `ID2D1DeviceContext` 在其上绘制 Direct2D 内容。该方式支持在 WinUI 3 控件上方透明绘制，绕开 SwapChainPanel 混合限制。

底图捕获优先 **Windows Graphics Capture（WGC）**，延迟低、GPU 利用率高；不可用时回退 GDI BitBlt 或 Desktop Duplication。

### 3.3 常驻模式覆盖层

常驻水印和遮罩马赛克需要一个**始终置顶、鼠标穿透、不在 Alt+Tab / 任务栏中显示**的透明覆盖窗口：

```cpp
SetWindowLongPtr(hwnd, GWL_EXSTYLE,
    ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
```

**关键约束**：WinUI 3 XAML compositor 与经典 `WS_EX_LAYERED` 渲染的干净合成存在已知冲突，常驻覆盖层建议**通过 Win32 interop 创建原生覆盖窗口，内部用 Composition 的 `DesktopWindowTarget` 关联 HWND 绘制**，不要把 WinUI 控件直接放在分层窗口上。

---

## 4. 模块划分

| 模块 | 职责 |
|---|---|
| **Capture Engine** | WGC 优先捕获，回退 GDI；多显示器与 DPI 感知在底层处理完毕 |
| **Overlay Window** | 截图模式无边框置顶窗口；常驻模式穿透覆盖窗口 |
| **Annotation Engine** | 矢量对象模型（MLEADER、矩形、箭头、文本、马赛克），命令栈，撤销/重做 |
| **MLeader Engine** | 多重引线几何计算、交互状态机、CMF 样式 |
| **Watermark Engine** | 常驻水印配置、缓存预渲染、多显示器平铺 |
| **Mask Engine** | 区域遮罩马赛克/模糊的实时捕获与效果链 |
| **Animation Engine** | 呼出动画的 Composition 编排 |
| **Ink Engine** | 手写笔书法彩蛋的笔迹捕获、识别、字体渲染 |
| **Tray / Service** | 系统托盘、全局热键、单例控制、常驻进程管理 |
| **Export Manager** | 底图与注释层合成导出 PNG / JPEG / 剪贴板 |

---

## 5. 多重引线（Multileader）详细设计

### 5.1 数据结构

```cpp
struct MLeaderStyle {
    // —— 几何 ——
    float arrowSize;          // 箭头长度
    float doglegAngle;        // 折弯斜线与水平方向夹角，默认 45°
    float landingGap;         // 横肩与文字间距
    float textHeight;         // 文字高度

    // —— CMF：颜色 ——
    D2D1_COLOR_F lineColor;           // 引线与横肩颜色
    D2D1_COLOR_F arrowColor;          // 箭头填充色
    D2D1_COLOR_F textColor;           // 文字颜色
    D2D1_COLOR_F textBackgroundColor; // 文字底衬（可选）

    // —— CMF：材质 / finish ——
    enum class LineFinish { Solid, Dashed, Dotted, DashDot };
    LineFinish lineFinish;
    float lineWidth;
    float cornerRadius;               // 横肩端点圆角

    enum class ArrowShape { Triangle, Dot, Tick, OpenArrow };
    ArrowShape arrowShape;

    // —— 文字 ——
    std::wstring fontFamily;          // 可指定书法字体
    float fontWeight;
    DWRITE_FONT_STYLE fontStyle;

    // —— 预设标识 ——
    std::wstring presetName;          // "工程制图" / "技术评审" / "轻标注"
};

struct MLeader {
    D2D1_POINT_2F arrowTip;       // 箭头尖端
    D2D1_POINT_2F kink;           // 折点
    D2D1_POINT_2F landingStart;   // 横肩起点
    D2D1_POINT_2F landingEnd;     // 横肩终点
    TextContent content;          // 文字内容
    MLeaderStyle style;
    TextAlign align;              // Left / Right，自动推导
};
```

折点角度锁定到 **45° 增量**是 CAD 常见行为，可降低手动对齐成本。

### 5.2 交互状态机

| 阶段 | 用户动作 | 内部状态 |
|---|---|---|
| 1. 箭头尖端 | 点击被注释位置 | 记录 `arrowTip`，进入阶段 2 |
| 2. 折点 | 移动鼠标/点击 | 实时计算 `kink`，角度可锁定 45°；折点决定横肩 y 坐标 |
| 3. 文字锚点 | 移动鼠标/点击 | 横肩水平延伸至锚点 x，确定 `landingStart/End`；根据锚点相对折点左右关系确定文字对齐方向 |
| 4. 文字输入 | 键盘输入，Enter 确认 | 内联编辑器（WinUI `TextBox` 浮在横肩上方），实时预览 |

### 5.3 自动文字方向算法

```text
if (textAnchor.x >= kink.x) {
    align = Left;
    landingStart = kink;  // 横肩从折点向右延伸
    textRect.left = landingEnd.x + landingGap;
} else {
    align = Right;
    landingStart = kink;  // 横肩从折点向左延伸
    textRect.right = landingEnd.x - landingGap;
}
```

文字始终位于横肩**上方**，垂直偏移由 `textHeight + landingGap` 决定。横肩长度不得小于文字宽度，否则自动延长以“托住”文字。

### 5.4 Direct2D 渲染

- 斜线与横肩使用 `ID2D1PathGeometry` 或 `DrawLine`；
- 箭头：简单场景用 `D2D1_LINE_CAP_ARROW_ANCHOR`；精确控制时按线段方向计算两个后端点，用 `FillGeometry` 填充三角形。设箭头尖端 B、前方向单位向量 U、垂直向量 V、箭头长度 h、半宽 w，后端点约为 `B - hU ± wV`；
- 文字使用 **DirectWrite**（`IDWriteTextLayout`），与 Direct2D 共享设备上下文，保证高 DPI 清晰；
- 注释层在 `CompositionDrawingSurface` 上绘制，`Clear(D2D1::ColorF(0, 0))` 保持全透明。

---

## 6. 多重引线 CMF 可配置系统

### 6.1 预设体系

- **内置预设**：工程制图、技术评审、轻标注、暗色模式适配；
- **自定义预设**：保存为 JSON，支持导入/导出；
- **样式面板**：颜色、线型、箭头形状、字体分别对应独立选择器；线型用小型 Direct2D 线段示例预览，箭头用图标网格；
- **实时预览**：修改立即应用到当前选中对象与“当前样式”（对标 CAD 的 MLEADERSTYLE）。

### 6.2 CMF 配置项

| 类别 | 配置项 |
|---|---|
| Color | 引线颜色、箭头颜色、文字颜色、文字底衬颜色 |
| Material / Finish | 实线、虚线、点线、点划线、线宽、横肩端点圆角 |
| Arrow | 三角形、圆点、短刻度、开放箭头 |
| Text | 字体、字重、斜体、字号、书法字体可选 |
| Preset | 预设名称、导入/导出 JSON |

---

## 7. 呼出动画：Siri 水波纹 / Win10 AI 键抖动

### 7.1 技术选型

使用 WinUI 3 **Composition API**。`SpringVector3NaturalMotionAnimation` 提供弹簧物理动画，通过 `DampingRatio` 和 `Period` 控制“过冲—回弹—稳定”。

- **水波纹扩散**：
  - 轻量方案：`CompositionDrawingSurface` 上用 Direct2D 绘制多层同心圆，Composition 驱动 `Scale` 和 `Opacity`；
  - 高质量方案：`CompositionPath` + `PathKeyFrameAnimation` 做非圆形不规则形变，更接近 Siri 有机形态。
- **抖动 / 冲击波**：波纹扩散同时施加短促 `SpringVector3` 位移动画（幅度 2–4 px，DampingRatio ≈ 0.5–0.7）。

### 7.2 参数建议

```text
水波纹：
  扩散时长：280–350 ms
  衰减：Opacity 从 0.6 → 0，Easing = CubicBezier(0.16, 1, 0.3, 1)
  层数：3 层，延迟 40 ms

抖动：
  幅度：3 px
  DampingRatio：0.55
  Period：~50 ms
```

### 7.3 时序隔离

底图捕获必须在动画开始前完成，避免动画帧污染截图内容。动画期间覆盖层不接收鼠标输入，结束后恢复交互。

---

## 8. 常驻动态水印

### 8.1 核心机制

常驻水印是一个**始终置顶、鼠标穿透、不在 Alt+Tab / 任务栏中显示的透明窗口**（见 3.3）。水印内容（文字、倾斜角度、颜色、透明度、间距、字体）由用户配置。

绘制方式推荐**预渲染到 `ID2D1Bitmap` 缓存，覆盖层只做 Blit**，避免每帧重新排版文字，保证常驻 CPU 占用可忽略。

### 8.2 “评审防截图”语义边界

常驻水印**不是防截图的，而是让截图带上水印的**。任何屏幕捕获工具捕获的是 DWM 合成后的最终画面，顶层水印窗口内容会出现在捕获结果中，满足“评审时别人截图会带上水印”的需求。但**不要承诺“防止截取无水印画面”**——物理拍照、虚拟机直通等路径无法被纯软件覆盖层阻止。

### 8.3 配置模型

```cpp
struct WatermarkProfile {
    std::wstring text;                // 支持变量：{user} / {date} / {time}
    std::wstring fontFamily;
    float fontSize;
    D2D1_COLOR_F color;
    float opacity;                    // 全局透明度
    float rotationDeg;                // 默认 45°
    float tileSpacingX, tileSpacingY; // 平铺间距
    bool enableOnAllMonitors;
};
```

配置面板放在 WinUI 设置页，修改后通过进程内消息通知覆盖窗口重建缓存，无需重启。

---

## 9. 区域遮罩马赛克 / 模糊

### 9.1 技术路径

| 路径 | 原理 | 适用 |
|---|---|---|
| **A. 实时桌面捕获 + 模糊回绘** | 常驻进程用 Desktop Duplication / WGC 持续捕获，遮罩区域对捕获帧做模糊/马赛克后绘制到覆盖层 | 遮罩区域随底层内容实时更新；CPU/GPU 开销较高 |
| **B. 静态冻结遮罩** | 触发时捕获一次当前帧，处理后作为静态覆盖，直到用户解除 | 开销低；底层内容变化后遮罩“过时” |

**评审场景推荐 A 的按需激活**：遮罩区域默认不激活捕获，用户划定区域后才开始实时捕获处理，区域外不消耗捕获资源。性能不达标时降级为 B 并标注“静态遮罩”。

### 9.2 Direct2D 效果实现

- **马赛克**：`ScaleEffect` 缩小 + `ScaleEffect` 放大回原尺寸（nearest neighbor 插值），或自定义像素着色器；
- **模糊**：内置 `GaussianBlurEffect`；
- **区域限定**：`PushLayer` + `FillGeometry` 限定范围，或效果链后 `Composite` 与几何遮罩合成；
- **软边缘**：`FillOpacityMask` 做羽化过渡。

### 9.3 与常驻水印共存

**推荐水印层和遮罩层拆成两个独立覆盖窗口**：水印层全屏平铺、低刷新率；遮罩层局部区域、需要较高刷新率。分开后可独立控制捕获负载，也便于分别启用/禁用。

---

## 10. 手写笔书法彩蛋（Windows Ink）

### 10.1 定位与触发

**彩蛋级功能**，不纳入主标注流程。检测到数位笔输入（`PointerDeviceType.Pen`）时，在工具栏旁出现“书法”入口。

### 10.2 技术路径选择

| 路径 | 原理 | 评估 |
|---|---|---|
| **A. 笔迹→文本→书法字体渲染** | `InkRecognizer` 识别文字，`DirectWrite` 以书法字体绘制 | **推荐首期**；依赖字体，需解决瘦金体授权与内置 |
| B. 笔迹美化 / 风格化 | 保留原始笔迹路径，算法处理笔画宽度、起收笔、连笔 | 视觉最接近真实书法；算法复杂度高，首期不现实 |
| C. 墨迹回放 + 书法字体叠加 | 保留笔迹动画，识别后淡出替换为书法字体 | 体验折中，可作为 A 的过渡 |

### 10.3 实现要点

- **识别管线**：`InkStrokeContainer` → `InkDivider`（分离文本与图形笔画）→ `InkRecognizer`（中文识别）→ 识别结果；
- **渲染**：`IDWriteTextLayout` + `IDWriteTypography` 启用 OpenType 的 stylistic alternates / swashes，提升书法字体视觉质量；
- **字体策略**：首选**内置经授权的书法字体子集**（常用汉字 + 标点）；无法内置时检测系统字体，缺失回退到系统楷体（`IDWriteFontCollection` 检测）；
- **动画**：笔迹层 `Opacity` 淡出与书法文字层淡入的 Composition 交叉动画；
- **性能**：书写阶段用 `CompositionDrawingSurface` 局部刷新，避免全屏重绘影响书写延迟；
- **坐标对齐**：`InkStrokeContainer` 笔画坐标需按 DPI 和画布偏移映射到 Direct2D 渲染表面坐标。

---

## 11. UI 设计（WinUI 3 + MD3）

- **工具栏**：MD3 风格浮动工具栏（圆角、Elevation、Dynamic Color 可选）。工具项：选择/移动、MLEADER、矩形、箭头、文本、马赛克、书法彩蛋（笔输入时显示）、撤销/重做、保存/复制。
- **样式面板**：MLEADER 选中时显示线条颜色、线宽、箭头形状、线型、文字大小/字体、折弯角度预设（45°/90°/自定义）、预设切换。
- **内联文字编辑**：双击 MLEADER 进入文字编辑，`TextBox` 叠放在横肩上方，位置由 `textRect` 驱动。
- **设置页**：水印配置、遮罩配置、呼出动画开关、书法字体选择、热键配置、预设管理。
- **托盘菜单**：显示/隐藏水印、启用/禁用遮罩、打开设置、退出。
- **全局热键**：`RegisterHotKey`，触发截图流程。

**为什么不把 MLEADER 渲染交给 XAML 控件**：几何实时交互式变化（拖动折点时横肩、文字矩形、对齐方向全部联动），XAML 控件拼装会产生大量布局开销和亚像素不对齐。Direct2D 在 CompositionDrawingSurface 上绘制更可控。

---

## 12. 常驻后台与系统托盘

WinUI 3 **没有内置系统托盘支持**，必须通过 Win32 interop（`Shell_NotifyIcon`）实现。可用现成方案：`H.NotifyIcon`、`SystemTrayWinUI3` 等 NuGet 库（封装托盘图标、上下文菜单、最小化到托盘、任务栏重建处理）。

**进程模型建议**：

- **主进程常驻**：承载托盘图标、设置窗口、常驻水印和遮罩覆盖层；
- **截图时**：同一进程内创建截图覆盖窗口，完成后销毁或隐藏，不额外启动进程；
- **全局热键**：常驻阶段注册；
- **单例控制**：命名 Mutex 防止多实例叠加覆盖层。

若担心常驻内存占用，可将水印/遮罩覆盖层拆分为独立轻量 Win32 进程，与 WinUI 主进程 IPC 通信；首期不建议增加此复杂度。

---

## 13. 部署：自带 MSVC 运行时 + 自包含 WinUI 3

### 13.1 目标

用户拿到的是**免安装 VC++ Redistributable、免安装 Windows App SDK Runtime** 的文件夹或单文件 EXE。

### 13.2 MSBuild 关键配置

```xml
<WindowsPackageType>None</WindowsPackageType>
<WindowsAppSDKSelfContained>true</WindowsAppSDKSelfContained>
<SelfContained>true</SelfContained>
```

- `WindowsAppSDKSelfContained=true`：Windows App SDK 运行时直接打进输出目录；
- `SelfContained=true`：处理 .NET/VCLibs 依赖的自包含；
- 代价：输出目录明显增大（自包含 WinUI 3 通常几十到百 MB 级别），换取“零依赖安装”。

### 13.3 MSVC 运行时策略

| 策略 | 做法 | 适用 |
|---|---|---|
| **静态链接 CRT** | `/MT`（Release），`RuntimeLibrary=MultiThreaded` | 最彻底，单 EXE 不依赖 `vcruntime140.dll` / `msvcp140.dll`；但 WinUI 3 部分组件可能仍需要特定 DLL，需实测 |
| **本地部署 CRT DLL** | `/MD`，将 `vcruntime140.dll`、`msvcp140.dll` 等随 EXE 放同目录 | 兼容性更稳，不修改全局系统；Windows 加载器优先从应用目录加载 |

**推荐**：优先 `/MT` 静态链接做核心模块；若 WinUI 3 / Windows App SDK 预编译组件强制依赖动态 CRT，退回 `/MD` + 本地 DLL 同目录分发。两者都不需要用户运行 `vc_redist.x64.exe`。

### 13.4 兜底：Bootstrapper

若自包含在某些系统上出现边缘情况，EXE 启动时调用 **Bootstrapper API** 检测并引导安装缺失运行时，作为最后保险，不作为主要分发路径。

---

## 14. 阶段规划

| 阶段 | 内容 | 依赖 / 风险 |
|---|---|---|
| **P0 骨架** | 双渲染路径设备上下文管理；WinUI 3 无边框覆盖窗口 + WGC 捕获；CompositionDrawingSurface 透明注释层；常驻穿透覆盖窗口最小原型 + 托盘图标 | WinUI 3 透明叠加与 `WS_EX_LAYERED` 合成冲突需**最早验证** |
| **P1 基础标注** | 矩形、箭头、自由文本；命令栈与撤销/重做；**MLEADER CMF 预设系统** | 矢量对象模型序列化；预设导入导出 |
| **P2 MLEADER 核心** | 三阶段交互 + 45° 角度锁定 + 自动文字左右对齐 + 横肩托底；CMF 实时预览 | 折点/横肩/文字矩形实时联动几何 |
| **P3 呼出动画** | Composition 水波纹 + Spring 抖动；动画与截图时序隔离 | 覆盖层透明合成下的动画性能 |
| **P4 常驻水印** | 穿透覆盖层 + 水印缓存渲染 + 配置面板 | 多显示器 DPI；独占全屏应用可能覆盖水印层 |
| **P5 遮罩马赛克** | 按需区域实时捕获 + Direct2D 模糊/马赛克效果链 | 实时捕获性能与延迟；独占全屏降级 |
| **P6 书法彩蛋** | Windows Ink 笔迹捕获 + Ink Recognition + DirectWrite 书法字体渲染 | 字体授权与内置方案；识别准确率 |
| **P7 MD3 打磨** | 浮动工具栏、样式面板、内联文字编辑、托盘菜单 | 工具栏与覆盖层输入路由（避免抢焦点） |
| **P8 部署** | 自包含 + CRT 策略落地 + 干净 Windows 虚拟机验证 + 常驻模式内存/CPU 回归 | WinUI 3 自包含输出尺寸与 CRT 兼容性 |

---

## 15. Prompt 片段合集（可直接投喂模型）

### 片段 A：MLEADER 几何计算模块

> 我正在用 **C++17 + WinUI 3（Windows App SDK）+ Direct2D** 构建一个截图标注工具。注释层使用 **Microsoft.UI.Composition.CompositionDrawingSurface** 承载 Direct2D 绘制（不使用 SwapChainPanel，因为 WinUI 3 中 SwapChainPanel 的透明混合受限）。请帮我实现 **Multileader（多重引线）** 的几何计算模块：
>
> 输入：`arrowTip`（箭头尖端）、`kink`（折点）、`textAnchor`（文字锚点）、`style`（箭头尺寸、折弯角度、landingGap、文字高度）。
> 要求：
> 1. 折弯斜线从 `arrowTip` 到 `kink`，角度可锁定到 45° 增量；
> 2. 横肩（landing）为水平线，y 坐标由 `kink.y` 决定；
> 3. 根据 `textAnchor.x` 与 `kink.x` 的关系自动决定文字在横肩左侧还是右侧（右侧时左对齐，左侧时右对齐）；
> 4. 横肩长度不得小于文字宽度，文字始终位于横肩上方，间距为 `landingGap`；
> 5. 输出 `landingStart`、`landingEnd`、`textRect`、`align` 四个结果，供 Direct2D 渲染和 WinUI TextBox 定位使用。
> 请给出完整的 C++ 函数签名和几何计算实现，并处理 `arrowTip == kink`、文字为空等边界情况。

### 片段 B：常驻穿透覆盖层

> 在 WinUI 3 C++ 项目中，我需要创建一个**始终置顶、鼠标穿透、不在 Alt+Tab 和任务栏中显示**的全屏透明覆盖窗口，用于绘制动态水印。请给出完整的 Win32 interop 实现：
> 1. 创建 `WS_POPUP` 窗口，扩展样式设置 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`；
> 2. 使用 `Microsoft.UI.Composition` 的 `DesktopWindowTarget` 将该 HWND 关联到 Composition，或使用 `UpdateLayeredWindow` + Direct2D DC 渲染目标；
> 3. 水印内容预渲染到 `ID2D1Bitmap` 缓存，覆盖窗口只做 Blit，避免每帧重排版文字；
> 4. 提供 `EnableWatermark(bool)` 和 `UpdateWatermarkProfile(profile)` 接口，修改后重建缓存并触发一次重绘；
> 5. 处理多显示器：为每个显示器创建一个覆盖窗口，或创建一个跨所有显示器的虚拟桌面尺寸窗口（需说明两种方案的取舍）。
> 请重点处理：WinUI 3 XAML compositor 与 `WS_EX_LAYERED` 的已知合成冲突，给出你推荐的渲染路径及原因。

### 片段 C：Direct2D 区域马赛克效果链

> 我用 Direct2D 实现一个“区域马赛克 / 模糊”效果，输入是捕获到的桌面纹理（`ID2D1Bitmap`），输出是遮罩区域内的马赛克或高斯模糊结果，区域外保持透明或原样。请给出：
> 1. 使用 `ScaleEffect` 缩小 + 放大（nearest neighbor）实现马赛克的效果链 C++ 代码；
> 2. 使用 `GaussianBlurEffect` 的替代效果链，并说明 `StandardDeviation` 与模糊半径的对应关系；
> 3. 如何用 `PushLayer` + 几何遮罩将效果限制在用户绘制的矩形/自由区域内部；
> 4. 如果输入纹理需要**实时更新**（每帧来自 Desktop Duplication），效果链应如何组织以避免每帧重建 effect graph；
> 5. 性能建议：马赛克块大小、模糊标准差、效果链缓存策略之间的权衡。

### 片段 D：书法彩蛋的 Ink Recognition → DirectWrite 管线

> 我需要在 WinUI 3 C++ 应用中实现一个“手写笔书法”彩蛋：用户用数位笔在画布上书写中文，松开笔后，笔迹被识别为文本，并以指定的书法字体（瘦金体/楷体/行楷）重新渲染，带有淡入淡出的替换动画。请给出：
> 1. 从 `PointerDeviceType.Pen` 捕获笔迹，创建 `InkStrokeContainer` 和 `InkRecognizer` 的 C++/WinRT 代码；
> 2. `InkRecognizer` 识别中文的参数配置（语言标签、候选数量）；
> 3. 识别结果通过 `IDWriteTextLayout` 以指定字体渲染到 `CompositionDrawingSurface` 的 Direct2D 设备上下文；
> 4. 笔迹层淡出、书法文字淡入的 Composition 动画编排（两个 `Visual` 的 `Opacity` 交叉动画）；
> 5. 如果用户系统未安装目标书法字体，如何用 `IDWriteFontCollection` 检测并回退到系统楷体，给出检测代码。
> 请重点说明：`InkStrokeContainer` 的笔画坐标如何与 Direct2D 渲染表面坐标对齐（DPI 和画布偏移的处理）。

### 片段 E：呼出动画编排

> 在 WinUI 3 C++ 中，我需要为截图工具的呼出实现一个“Siri 式水波纹 + 轻微抖动”的动画：
> 1. 水波纹：3 层同心圆从触发点向外扩散，时长 280–350 ms，Opacity 从 0.6 衰减到 0，Easing = `CubicBezier(0.16, 1, 0.3, 1)`，每层延迟 40 ms；
> 2. 抖动：覆盖层整体施加 `SpringVector3NaturalMotionAnimation`，幅度 3 px，DampingRatio = 0.55；
> 3. 动画在 `CompositionDrawingSurface` 上绘制，不使用 XAML 控件；
> 4. 动画期间覆盖层鼠标穿透，动画结束后恢复交互；
> 5. 底图捕获必须在动画开始前完成。
> 请给出完整的 Composition 动画编排代码，并说明如何组织 3 层水波纹的 `Visual` 树和关键帧。

---

**一句话总结**：本项目架构本质是**一条截图模式管线（WGC 捕获 + CompositionDrawingSurface 上的 Direct2D 注释）+ 一条常驻模式管线（穿透覆盖层上的水印与遮罩效果链）+ 一个共享的 Direct2D 渲染/效果引擎**；MLEADER 作为一等矢量对象实现 CAD 式交互与 CMF 配置，书法彩蛋作为独立输入管线挂在标注引擎旁不侵入主流程，最终通过 WindowsAppSDKSelfContained + 本地 CRT 实现免运行时分发。