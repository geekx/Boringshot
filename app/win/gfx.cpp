#include "gfx.h"

#include "boringshot/selection.h"

#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>

namespace gfx {
namespace {

constexpr wchar_t kAnnotationFont[] = L"Microsoft YaHei UI";

std::size_t index(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

void fillTriangle(ID2D1RenderTarget* rt, const std::array<bs::Vec2, 3>& t, ID2D1Brush* brush) {
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(f.GetAddressOf());
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(f->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
        return;
    sink->BeginFigure(toD2D(t[0]), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(toD2D(t[1]));
    sink->AddLine(toD2D(t[2]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close())) rt->FillGeometry(path.Get(), brush);
}

bs::Vec2 mid(bs::Vec2 a, bs::Vec2 b) { return (a + b) * 0.5f; }

}  // namespace

Factories& factories() {
    static Factories f = [] {
        Factories r;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, r.d2d.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(r.dwrite.GetAddressOf()));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(r.wic.GetAddressOf()));
        return r;
    }();
    return f;
}

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

ComPtr<IDWriteTextLayout> textLayout(const std::wstring& text, float fontSize) {
    auto& f = factories();
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(f.dwrite->CreateTextFormat(kAnnotationFont, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          fontSize, L"", format.GetAddressOf())))
        return layout;
    f.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), 8192.0f,
                               8192.0f, layout.GetAddressOf());
    return layout;
}

D2D1_SIZE_F measureText(const std::wstring& text, float fontSize) {
    auto layout = textLayout(text.empty() ? std::wstring(L" ") : text, fontSize);
    DWRITE_TEXT_METRICS m{};
    if (!layout || FAILED(layout->GetMetrics(&m))) return D2D1::SizeF(0, fontSize);
    return D2D1::SizeF(text.empty() ? 0.0f : m.widthIncludingTrailingWhitespace, m.height);
}

D2D1_RECT_F caretRect(const std::wstring& text, float fontSize) {
    auto layout = textLayout(text, fontSize);
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    if (!layout) return D2D1::RectF(0, 0, 2, fontSize);
    const UINT32 pos = text.empty() ? 0u : static_cast<UINT32>(text.size() - 1);
    layout->HitTestTextPosition(pos, text.empty() ? FALSE : TRUE, &x, &y, &m);
    const float h = m.height > 0 ? m.height : fontSize * 1.33f;
    return D2D1::RectF(x, y, x + 2.0f, y + h);
}

bs::MLeaderGeometry leaderGeometry(const bs::Shape& s) {
    bs::MLeaderGeometry g;
    g.arrowSize = 3.0f * s.strokeWidth + 8.0f;
    g.arrowHalfWidth = 1.5f * s.strokeWidth + 3.5f;
    g.textHeight = measureText(L"Ag", s.fontSize).height;
    g.landingGap = 2.0f + s.strokeWidth;
    return g;
}

bs::Vec2 textOrigin(const bs::Shape& s) {
    if (s.kind != bs::ShapeKind::MLeader) return s.a;
    bs::MLeaderInput in = s.leader;
    in.textWidth = measureText(s.text, s.fontSize).width;
    const auto l = bs::layoutMLeader(in, leaderGeometry(s));
    return {l.textRect.left, l.textRect.top};
}

void drawShape(ID2D1RenderTarget* rt, const bs::Shape& s) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(toD2D(s.color), brush.GetAddressOf()))) return;
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(f.GetAddressOf());
    ComPtr<ID2D1StrokeStyle> round;
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                     D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                         nullptr, 0, round.GetAddressOf());
    const float w = s.strokeWidth;

    switch (s.kind) {
        case bs::ShapeKind::Rect:
            rt->DrawRectangle(toD2D(bs::normalizedRect(s.a, s.b)), brush.Get(), w);
            break;
        case bs::ShapeKind::Ellipse: {
            const bs::Rect r = bs::normalizedRect(s.a, s.b);
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                                          r.width() * 0.5f, r.height() * 0.5f),
                            brush.Get(), w);
            break;
        }
        case bs::ShapeKind::Line:
            rt->DrawLine(toD2D(s.a), toD2D(s.b), brush.Get(), w, round.Get());
            break;
        case bs::ShapeKind::Arrow: {
            const auto head = bs::arrowHeadTriangle(s.a, s.b, 3.0f * w + 10.0f, 1.5f * w + 5.0f);
            if (!head) break;
            // Stop the shaft at the head's base so a thick stroke never pokes through the tip.
            rt->DrawLine(toD2D(s.a), toD2D(mid((*head)[1], (*head)[2])), brush.Get(), w, round.Get());
            fillTriangle(rt, *head, brush.Get());
            break;
        }
        case bs::ShapeKind::Pen: {
            if (s.points.size() < 2) break;
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(f->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
                break;
            sink->BeginFigure(toD2D(s.points[0]), D2D1_FIGURE_BEGIN_HOLLOW);
            for (std::size_t i = 1; i < s.points.size(); ++i) sink->AddLine(toD2D(s.points[i]));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) rt->DrawGeometry(path.Get(), brush.Get(), w, round.Get());
            break;
        }
        case bs::ShapeKind::Text: {
            if (auto layout = textLayout(s.text, s.fontSize))
                rt->DrawTextLayout(toD2D(s.a), layout.Get(), brush.Get());
            break;
        }
        case bs::ShapeKind::MLeader: {
            bs::MLeaderInput in = s.leader;
            in.textWidth = measureText(s.text, s.fontSize).width;
            const auto l = bs::layoutMLeader(in, leaderGeometry(s));
            if (l.arrowHead) {
                rt->DrawLine(toD2D(mid((*l.arrowHead)[1], (*l.arrowHead)[2])), toD2D(l.kink), brush.Get(),
                             w, round.Get());
                fillTriangle(rt, *l.arrowHead, brush.Get());
            }
            rt->DrawLine(toD2D(l.landingStart), toD2D(l.landingEnd), brush.Get(), w, round.Get());
            if (!s.text.empty()) {
                if (auto layout = textLayout(s.text, s.fontSize))
                    rt->DrawTextLayout(D2D1::Point2F(l.textRect.left, l.textRect.top), layout.Get(),
                                       brush.Get());
            }
            break;
        }
    }
}

void drawShapes(ID2D1RenderTarget* rt, const std::vector<bs::Shape>& shapes) {
    for (const auto& s : shapes) drawShape(rt, s);
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
    drawShapes(rt.Get(), shapes);
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
