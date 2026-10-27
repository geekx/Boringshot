#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "boringshot/annotation.h"
#include "boringshot/mleader.h"
#include "boringshot/selection.h"
#include "capture.h"
#include "gfx.h"

struct CaptureOptions {
    bool animate = true;  // summon ripple + jolt
};

// Full-desktop capture overlay: window snapping, region select/resize,
// magnifier colour picker, annotation tools with CMF styles, picking existing
// shapes to drag their joints, undo/redo, copy/save/pin, summon animation.
class CaptureWindow {
public:
    struct Callbacks {
        std::function<void(gfx::Image, POINT)> pin;  // image + desktop position
        std::function<void()> closed;
    };
    bool open(HINSTANCE instance, ScreenGrab grab, Callbacks cb, CaptureOptions opts = {});
    void close();

    // Test hook: drives every tool, style, pick/drag, mosaic and the animation
    // through the real paint + composite paths. Returns nullptr or the failed check.
    const wchar_t* smoke(const bs::Rect& r);

private:
    enum class Mode { Picking, Dragging, Selected };
    enum class Tool { None, Rect, Ellipse, Line, Arrow, Pen, Text, MLeader, Mosaic };
    static constexpr int kToolCount = 9;
    enum class Action {
        Tool, Color, Width, Undo, Redo, Pin, Save, Copy, Cancel,
        Finish, Head, Tail, Font, Bold, Italic, List, Plate, MosaicMode, Delete
    };
    struct Button {
        bs::Rect rect;
        Action action;
        int arg;
    };
    enum class Edit { None, Text, LeaderLabel };
    struct PickDrag {
        bool active = false;
        int handle = -1;  // -1 = move the whole shape
        bs::Vec2 start;
        bs::Shape original;
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    // Input
    void onMouseMove(bs::Vec2 p, bool shift);
    void onLButtonDown(bs::Vec2 p);
    void onLButtonUp(bs::Vec2 p, bool shift);
    void onDoubleClick(bs::Vec2 p);
    void onRButtonUp(bs::Vec2 p);
    void onKeyDown(WPARAM key);
    void onChar(wchar_t c);
    void updateCursor();

    // Actions & styles
    void run(Action a, int arg, int dir);
    void selectTool(Tool t);
    void applyStyle(const std::function<void(bs::Shape&)>& fn);
    const bs::Shape& styleSource() const;
    std::optional<bs::ShapeKind> contextKind() const;
    void deletePicked();
    void finishPin();
    void finishCopy();
    void finishSave();
    void resetToPicking();
    gfx::Image composite();

    // Text editing
    void beginEdit(Edit kind, bs::Vec2 at);
    void beginEditExisting(std::size_t index);
    void commitEdit();
    void cancelEdit();
    void placeImeWindow();

    // Rendering
    bool ensureTarget();
    void paint();
    void paintDim(const bs::Rect& sel);
    void paintSelectionChrome(const bs::Rect& sel);
    void paintPicked();
    void paintAngle(bs::Vec2 vertex, bs::Vec2 target);
    void paintToolbar();
    void paintMagnifier();
    void paintAnimation(float elapsedMs);
    void paintButtonIcon(const Button& b, D2D1_COLOR_F fg);
    void paintCapSample(D2D1_POINT_2F c, bs::EndShape shape, bool atRight);
    bool isToggled(const Button& b) const;
    void layoutToolbar();
    bs::Rect monitorFor(const bs::Rect& r) const;
    bs::Shape styledShape(Tool t) const;
    std::optional<bs::Shape> previewShape() const;
    std::size_t hiddenIndex() const;
    bs::Rect bounds() const;
    bs::Rect activeRect() const;
    int buttonAt(bs::Vec2 p) const;
    float animElapsedMs() const;
    void invalidate();

    HWND hwnd_ = nullptr;
    ScreenGrab grab_;
    Callbacks cb_;
    CaptureOptions opts_;
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
    std::array<bs::Shape, kToolCount> templates_;  // per-tool CMF defaults
    bs::Document doc_;
    bool drawing_ = false;
    bs::Shape draft_;
    bs::MLeaderTool leader_;

    std::optional<std::size_t> picked_;
    PickDrag pick_;
    bs::Shape live_;

    Edit edit_ = Edit::None;
    bs::Shape editShape_;
    std::optional<std::size_t> editIndex_;

    bool animating_ = false;
    LONGLONG animStart_ = 0;

    std::wstring copiedNote_;
    std::vector<Button> buttons_;
    bs::Rect toolbarRect_{};
    bs::Rect subRect_{};
    int hoverButton_ = -1;
    int pressedButton_ = -1;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> shotBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dashStroke_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> glyphFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> italicFont_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, bs::kFontCount> fontFormats_;
};
