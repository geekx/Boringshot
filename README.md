# Boringshot

Windows 原生截图标注工具：CAD 式多重引线（MLEADER）、常驻水印 / 遮罩、呼出动画、手写笔书法彩蛋。
风格：**酷 · 稳健 · 迅捷 · 敏捷 · 高效**。完整规划见 [`docs/PLAN.md`](docs/PLAN.md)，工作约定见 [`CLAUDE.md`](CLAUDE.md)。

## 当前进度（P0）

- `core/`：MLEADER 几何（45° 折点吸附、自动左右对齐、横肩托底、箭头三角）+ 三阶段交互状态机；撤销/重做命令栈；水印模板 `{user}/{date}/{time}` 与错行平铺。
- `app/win/`：常驻宿主——单例、托盘菜单、`Ctrl+Alt+W` 切换水印；穿透置顶覆盖层（Direct2D 预渲染 → `UpdateLayeredWindow`，稳态零开销）。

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
