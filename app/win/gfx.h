#pragma once
// Shared Direct2D / DirectWrite / WIC plumbing for every Boringshot surface.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "boringshot/annotation.h"

namespace gfx {

using Microsoft::WRL::ComPtr;

// Opaque 32-bit BGRA pixels (alpha = 255, so straight == premultiplied).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> px;

    bool empty() const { return width <= 0 || height <= 0; }
    std::uint32_t at(int x, int y) const { return px[static_cast<std::size_t>(y) * width + x]; }
};

struct Factories {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICImagingFactory> wic;
};

// Lazily created; requires COM initialised on the calling (UI) thread.
Factories& factories();
// Releases the factories; call before CoUninitialize (a static destructor would run after it).
void shutdown();

inline D2D1_RECT_F toD2D(const bs::Rect& r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
inline D2D1_POINT_2F toD2D(bs::Vec2 p) { return D2D1::Point2F(p.x, p.y); }
inline D2D1_COLOR_F toD2D(const bs::Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

ComPtr<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget* rt, const Image& img);
Image crop(const Image& src, const bs::Rect& r);

// Label layout for a text style; size in DIPs (== px, all targets run at 96 dpi).
ComPtr<IDWriteTextLayout> textLayout(const std::wstring& text, float fontSize, const bs::TextStyle& style);
// Measures displayText(s) and stores it in s.textSize / s.leader.textWidth.
void measureShapeText(bs::Shape& s);
// Caret after the last displayed character, relative to the label origin.
D2D1_RECT_F caretRect(const bs::Shape& s);

// Where a Text / MLeader shape's label is drawn (layout origin).
bs::Vec2 textOrigin(const bs::Shape& s);

// `source` feeds Mosaic shapes (same coordinates as the shapes).
void drawShape(ID2D1RenderTarget* rt, const bs::Shape& s, const Image* source);
void drawShapes(ID2D1RenderTarget* rt, const std::vector<bs::Shape>& shapes, const Image* source,
                std::size_t skip = static_cast<std::size_t>(-1));

// Crop + annotations (translated so `selection` becomes the origin).
Image renderComposite(const Image& screen, const bs::Rect& selection,
                      const std::vector<bs::Shape>& shapes);

bool copyImageToClipboard(HWND owner, const Image& img);
bool copyTextToClipboard(HWND owner, const std::wstring& text);
bool savePng(const Image& img, const std::wstring& path);
// Shows a Save dialog (default: Pictures\Boringshot_<timestamp>.png) and writes the PNG.
bool saveWithDialog(HWND owner, const Image& img);

}  // namespace gfx
