#include "gfx.h"

#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>

#include "boringshot/image_ops.h"
#include "boringshot/selection.h"

namespace gfx {
namespace {

std::size_t index(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

ComPtr<ID2D1Factory> factoryOf(ID2D1RenderTarget* rt) {
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(f.GetAddressOf());
    return f;
}

void fillPolygon(ID2D1RenderTarget* rt, const std::vector<bs::Vec2>& pts, ID2D1Brush* brush, bool fill, float w,
                 ID2D1StrokeStyle* stroke, bool closed) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factoryOf(rt)->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
        return;
    sink->BeginFigure(toD2D(pts[0]), fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t i = 1; i < pts.size(); ++i) sink->AddLine(toD2D(pts[i]));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    if (fill) rt->FillGeometry(path.Get(), brush);
    else rt->DrawGeometry(path.Get(), brush, w, stroke);
}

void drawEndCap(ID2D1RenderTarget* rt, const bs::EndCap& cap, ID2D1Brush* brush, float w, ID2D1StrokeStyle* round) {
    switch (cap.shape) {
        case bs::EndShape::None:
            break;
        case bs::EndShape::Dot:
            rt->FillEllipse(D2D1::Ellipse(toD2D(cap.center), cap.radius, cap.radius), brush);
            break;
        default:
            fillPolygon(rt, cap.outline, brush, cap.filled, w, round, cap.closed);
            break;
    }
}

D2D1_DASH_STYLE dashOf(bs::LineFinish f) {
    switch (f) {
        case bs::LineFinish::Dashed: return D2D1_DASH_STYLE_DASH;
        case bs::LineFinish::Dotted: return D2D1_DASH_STYLE_DOT;
        case bs::LineFinish::DashDot: return D2D1_DASH_STYLE_DASH_DOT;
        case bs::LineFinish::Solid: break;
    }
    return D2D1_DASH_STYLE_SOLID;
}

// Readable plate under a label: dark behind light ink, light behind dark ink.
D2D1_COLOR_F plateFor(const bs::Color& c) {
    const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return luma > 0.55f ? D2D1::ColorF(0.05f, 0.05f, 0.07f, 0.62f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.82f);
}

void drawLabel(ID2D1RenderTarget* rt, const bs::Shape& s, bs::Vec2 origin, ID2D1SolidColorBrush* brush) {
    const std::wstring text = bs::displayText(s);
    if (text.empty()) return;
    auto layout = textLayout(text, s.fontSize, s.textStyle);
    if (!layout) return;
    if (s.textStyle.background) {
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        const float pad = s.fontSize * 0.25f;
        brush->SetColor(plateFor(s.color));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(origin.x - pad, origin.y - pad * 0.5f,
                                          origin.x + m.widthIncludingTrailingWhitespace + pad, origin.y + m.height + pad * 0.5f),
                              pad, pad),
            brush);
        brush->SetColor(toD2D(s.color));
    }
    rt->DrawTextLayout(toD2D(origin), layout.Get(), brush);
}

void drawMosaic(ID2D1RenderTarget* rt, const bs::Shape& s, const Image* source) {
    if (!source || source->empty()) return;
    const bs::Rect r = bs::normalizedRect(s.a, s.b);
    const int x = static_cast<int>(std::floor(r.left)), y = static_cast<int>(std::floor(r.top));
    const int w = static_cast<int>(std::ceil(r.right)) - x, h = static_cast<int>(std::ceil(r.bottom)) - y;
    const bs::PixelView view{source->px.data(), source->width, source->height};
    const bs::PixelBlock block =
        s.mosaic == bs::MosaicMode::Pixelate
            ? bs::pixelate(view, x, y, w, h, static_cast<int>(s.strokeWidth * 3.0f + 4.0f))
            : bs::boxBlur(view, x, y, w, h, static_cast<int>(s.strokeWidth * 1.5f + 3.0f));
    if (block.empty()) return;
    Image img{block.width, block.height, block.px};
    if (auto bmp = makeBitmap(rt, img))
        rt->DrawBitmap(bmp.Get(),
                       D2D1::RectF(static_cast<float>(block.x), static_cast<float>(block.y),
                                   static_cast<float>(block.x + block.width), static_cast<float>(block.y + block.height)),
                       1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

}  // namespace

namespace {
Factories& storage() {
    static Factories f;
    return f;
}
}  // namespace

Factories& factories() {
    Factories& f = storage();
    if (!f.d2d) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, f.d2d.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(f.dwrite.GetAddressOf()));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f.wic.GetAddressOf()));
    }
    return f;
}

void shutdown() { storage() = Factories{}; }

ComPtr<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget* rt, const Image& img) {
    ComPtr<ID2D1Bitmap> bmp;
    if (img.empty()) return bmp;
    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(img.width), static_cast<UINT32>(img.height)),
                     img.px.data(), static_cast<UINT32>(img.width) * 4u, props, bmp.GetAddressOf());
    return bmp;
}

Image crop(const Image& src, const bs::Rect& r) {
    Image out;
    const int x0 = std::clamp(static_cast<int>(r.left), 0, src.width);
    const int y0 = std::clamp(static_cast<int>(r.top), 0, src.height);
    const int x1 = std::clamp(static_cast<int>(r.right), 0, src.width);
    const int y1 = std::clamp(static_cast<int>(r.bottom), 0, src.height);
    if (x1 <= x0 || y1 <= y0) return out;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.px.resize(index(0, out.height, out.width));
    for (int y = 0; y < out.height; ++y)
        std::memcpy(&out.px[index(0, y, out.width)], &src.px[index(x0, y0 + y, src.width)],
                    static_cast<std::size_t>(out.width) * 4u);
    return out;
}

ComPtr<IDWriteTextLayout> textLayout(const std::wstring& text, float fontSize, const bs::TextStyle& style) {
    auto& f = factories();
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(f.dwrite->CreateTextFormat(bs::fontFamily(style.font), nullptr,
                                          style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                          style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", format.GetAddressOf())))
        return layout;
    f.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), 8192.0f, 8192.0f,
                               layout.GetAddressOf());
    return layout;
}

void measureShapeText(bs::Shape& s) {
    const std::wstring text = bs::displayText(s);
    auto layout = textLayout(text.empty() ? std::wstring(L" ") : text, s.fontSize, s.textStyle);
    DWRITE_TEXT_METRICS m{};
    if (!layout || FAILED(layout->GetMetrics(&m))) {
        s.textSize = {0.0f, s.fontSize * 1.35f};
    } else {
        s.textSize = {text.empty() ? 0.0f : m.widthIncludingTrailingWhitespace, m.height};
    }
    s.leader.textWidth = s.textSize.x;
}

D2D1_RECT_F caretRect(const bs::Shape& s) {
    const std::wstring text = bs::displayText(s);
    auto layout = textLayout(text, s.fontSize, s.textStyle);
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    if (!layout) return D2D1::RectF(0, 0, 2, s.fontSize);
    const UINT32 pos = text.empty() ? 0u : static_cast<UINT32>(text.size() - 1);
    layout->HitTestTextPosition(pos, text.empty() ? FALSE : TRUE, &x, &y, &m);
    const float h = m.height > 0 ? m.height : s.fontSize * 1.33f;
    return D2D1::RectF(x, y, x + 2.0f, y + h);
}

bs::Vec2 textOrigin(const bs::Shape& s) {
    if (s.kind != bs::ShapeKind::MLeader) return s.a;
    const auto l = bs::leaderLayout(s);
    return {l.textRect.left, l.textRect.top};
}

void drawShape(ID2D1RenderTarget* rt, const bs::Shape& s, const Image* source) {
    if (s.kind == bs::ShapeKind::Mosaic) {
        drawMosaic(rt, s, source);
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(toD2D(s.color), brush.GetAddressOf()))) return;
    const auto f = factoryOf(rt);
    const float w = s.strokeWidth;
    const bool dotted = s.finish == bs::LineFinish::Dotted;
    ComPtr<ID2D1StrokeStyle> stroke, round;
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                     dotted ? D2D1_CAP_STYLE_ROUND : D2D1_CAP_STYLE_FLAT,
                                                     D2D1_LINE_JOIN_ROUND, 10.0f, dashOf(s.finish)),
                         nullptr, 0, stroke.GetAddressOf());
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                     D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                         nullptr, 0, round.GetAddressOf());

    switch (s.kind) {
        case bs::ShapeKind::Rect:
            rt->DrawRectangle(toD2D(bs::normalizedRect(s.a, s.b)), brush.Get(), w, stroke.Get());
            break;
        case bs::ShapeKind::Ellipse: {
            const bs::Rect r = bs::normalizedRect(s.a, s.b);
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                                          r.width() * 0.5f, r.height() * 0.5f),
                            brush.Get(), w, stroke.Get());
            break;
        }
        case bs::ShapeKind::Line:
        case bs::ShapeKind::Arrow: {
            const auto head = bs::makeEndCap(s.head, s.a, s.b, w);
            const auto tail = bs::makeEndCap(s.tail, s.b, s.a, w);
            rt->DrawLine(toD2D(tail.shaftEnd), toD2D(head.shaftEnd), brush.Get(), w, stroke.Get());
            drawEndCap(rt, head, brush.Get(), w, round.Get());
            drawEndCap(rt, tail, brush.Get(), w, round.Get());
            break;
        }
        case bs::ShapeKind::Pen: {
            if (s.points.size() < 2) break;
            fillPolygon(rt, s.points, brush.Get(), false, w, stroke.Get(), false);
            break;
        }
        case bs::ShapeKind::Text:
            drawLabel(rt, s, s.a, brush.Get());
            break;
        case bs::ShapeKind::MLeader: {
            const auto l = bs::leaderLayout(s);
            const auto head = bs::makeEndCap(s.head, l.kink, l.arrowTip, w);
            const auto tail = bs::makeEndCap(s.tail, l.landingStart, l.landingEnd, w);
            if (head.shape != bs::EndShape::None || bs::length(l.arrowTip - l.kink) > 0.5f)
                rt->DrawLine(toD2D(head.shaftEnd), toD2D(l.kink), brush.Get(), w, stroke.Get());
            rt->DrawLine(toD2D(l.landingStart), toD2D(tail.shaftEnd), brush.Get(), w, stroke.Get());
            drawEndCap(rt, head, brush.Get(), w, round.Get());
            drawEndCap(rt, tail, brush.Get(), w, round.Get());
            drawLabel(rt, s, {l.textRect.left, l.textRect.top}, brush.Get());
            break;
        }
        case bs::ShapeKind::Mosaic:
            break;
    }
}

void drawShapes(ID2D1RenderTarget* rt, const std::vector<bs::Shape>& shapes, const Image* source, std::size_t skip) {
    for (std::size_t i = 0; i < shapes.size(); ++i)
        if (i != skip) drawShape(rt, shapes[i], source);
}

Image renderComposite(const Image& screen, const bs::Rect& sel, const std::vector<bs::Shape>& shapes) {
    Image base = crop(screen, sel);
    if (base.empty()) return base;
    auto& f = factories();
    ComPtr<IWICBitmap> wb;
    if (!f.wic || FAILED(f.wic->CreateBitmap(static_cast<UINT>(base.width), static_cast<UINT>(base.height),
                                             GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                             wb.GetAddressOf())))
        return {};
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(f.d2d->CreateWicBitmapRenderTarget(wb.Get(), props, rt.GetAddressOf()))) return {};
    auto bmp = makeBitmap(rt.Get(), base);
    if (!bmp) return {};

    const float ox = std::floor(sel.left), oy = std::floor(sel.top);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(D2D1::ColorF::White));
    rt->DrawBitmap(bmp.Get(), D2D1::RectF(0, 0, static_cast<float>(base.width), static_cast<float>(base.height)),
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(-ox, -oy));
    drawShapes(rt.Get(), shapes, &screen);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (FAILED(rt->EndDraw())) return {};

    const WICRect lockRect{0, 0, base.width, base.height};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(wb->Lock(&lockRect, WICBitmapLockRead, lock.GetAddressOf()))) return {};
    UINT stride = 0, size = 0;
    BYTE* data = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&size, &data);
    if (!data) return {};
    for (int y = 0; y < base.height; ++y)
        std::memcpy(&base.px[index(0, y, base.width)], data + static_cast<std::size_t>(y) * stride,
                    static_cast<std::size_t>(base.width) * 4u);
    for (auto& p : base.px) p |= 0xFF000000u;  // keep it opaque for clipboard consumers
    return base;
}

bool copyImageToClipboard(HWND owner, const Image& img) {
    if (img.empty()) return false;
    const std::size_t pixelBytes = img.px.size() * 4u;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
    if (!mem) return false;
    auto* hdr = static_cast<BITMAPINFOHEADER*>(GlobalLock(mem));
    if (!hdr) {
        GlobalFree(mem);
        return false;
    }
    *hdr = {};
    hdr->biSize = sizeof(BITMAPINFOHEADER);
    hdr->biWidth = img.width;
    hdr->biHeight = img.height;  // bottom-up
    hdr->biPlanes = 1;
    hdr->biBitCount = 32;
    hdr->biCompression = BI_RGB;
    hdr->biSizeImage = static_cast<DWORD>(pixelBytes);
    auto* dst = reinterpret_cast<std::uint32_t*>(hdr + 1);
    for (int y = 0; y < img.height; ++y)
        std::memcpy(dst + index(0, img.height - 1 - y, img.width), &img.px[index(0, y, img.width)],
                    static_cast<std::size_t>(img.width) * 4u);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_DIB, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    if (void* p = GlobalLock(mem)) {
        std::memcpy(p, text.c_str(), bytes);
        GlobalUnlock(mem);
    }
    if (!OpenClipboard(owner)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

bool savePng(const Image& img, const std::wstring& path) {
    auto& f = factories();
    if (img.empty() || !f.wic) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(f.wic->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(f.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(static_cast<UINT>(img.width), static_cast<UINT>(img.height))))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return false;
    const UINT stride = static_cast<UINT>(img.width) * 4u;
    if (FAILED(frame->WritePixels(static_cast<UINT>(img.height), stride, stride * static_cast<UINT>(img.height),
                                  reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(img.px.data())))))
        return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

bool saveWithDialog(HWND owner, const Image& img) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t file[MAX_PATH] = {};
    std::swprintf(file, MAX_PATH, L"Boringshot_%04d%02d%02d_%02d%02d%02d.png", static_cast<int>(st.wYear),
                  static_cast<int>(st.wMonth), static_cast<int>(st.wDay), static_cast<int>(st.wHour),
                  static_cast<int>(st.wMinute), static_cast<int>(st.wSecond));
    PWSTR pictures = nullptr;
    SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = pictures;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool chosen = GetSaveFileNameW(&ofn) != FALSE;
    CoTaskMemFree(pictures);
    return chosen && savePng(img, file);
}

}  // namespace gfx
