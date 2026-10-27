#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "boringshot/annotation.h"
#include "boringshot/mleader.h"
#include "boringshot/selection.h"
#include "capture.h"
#include "gfx.h"

// Full-desktop capture overlay: window snapping, region select/resize,
// magnifier colour picker, annotation tools, undo/redo, copy/save/pin.
class CaptureWindow {
public:
    struct Callbacks {
        std::function<void(gfx::Image, POINT)> pin;  // image + desktop position
        std::function<void()> closed;
    };

    bool open(HINSTANCE instance, ScreenGrab grab, Callbacks cb);
    void close();

    // Test hook: select `r`, add one shape of every kind, paint and composite.
    bool smoke(const bs::Rect& r);

private:
    enum class Mode { Picking, Dragging, Selected };
    enum class Tool { None, Rect, Ellipse, Line, Arrow, Pen, Text, MLeader };
    enum class Action { Tool, Color, Width, Undo, Redo, Pin, Save, Copy, Cancel };
    struct Button {
        bs::Rect rect;
        Action action;
        int arg;
    };
    enum class Edit { None, Text, LeaderLabel };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    // Input
    void onMouseMove(bs::Vec2 p, bool shift);
    void onLButtonDown(bs::Vec2 p);
    void onLButtonUp(bs::Vec2 p, bool shift);
    void onDoubleClick(bs::Vec2 p);
    void onRButtonUp();
    void onKeyDown(WPARAM key);
    void onChar(wchar_t c);
    void updateCursor();

    // Actions
    void run(Action a, int arg);
    void selectTool(Tool t);
    void finishPin();
    void finishCopy();
    void finishSave();
    void resetToPicking();
    gfx::Image composite();

    // Text editing
    void beginEdit(Edit kind, bs::Vec2 at);
    void commitEdit();
    void cancelEdit();
    void placeImeWindow();

    // Rendering
    bool ensureTarget();
    void paint();
    void paintDim(const bs::Rect& sel);
    void paintSelectionChrome(const bs::Rect& sel);
    void paintToolbar();
    void paintMagnifier();
    void paintButtonIcon(const Button& b, D2D1_COLOR_F fg);
    void layoutToolbar();
    bs::Rect monitorFor(const bs::Rect& r) const;
    bs::Shape styledShape(bs::ShapeKind kind) const;
    std::optional<bs::Shape> previewShape() const;
    bs::Rect bounds() const;
    bs::Rect activeRect() const;
    int buttonAt(bs::Vec2 p) const;
    void invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);  // never null: that repaints every window
    }

    HWND hwnd_ = nullptr;
    ScreenGrab grab_;
    Callbacks cb_;
    float ui_ = 1.0f;  // DPI scale for chrome

    Mode mode_ = Mode::Picking;
    bs::Rect sel_{};
    std::optional<bs::Rect> hover_;
    bs::Vec2 cursor_{};
    bs::Vec2 dragStart_{};
    bs::Rect selAtDragStart_{};
    bs::Handle dragHandle_ = bs::Handle::None;

    Tool tool_ = Tool::None;
    int colorIndex_ = 0;
    int widthIndex_ = 1;
    bs::Document doc_;
    bool drawing_ = false;
    bs::Shape draft_;
    bs::MLeaderTool leader_;

    Edit edit_ = Edit::None;
    bs::Shape editShape_;

    std::wstring copiedNote_;
    std::vector<Button> buttons_;
    bs::Rect toolbarRect_{};
    int hoverButton_ = -1;
    int pressedButton_ = -1;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> shotBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> glyphFont_;
};
