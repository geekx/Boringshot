// Must precede the C++/WinRT headers so winrt::com_ptr / as<> understand classic COM.
#include <unknwn.h>
#include <inspectable.h>

#include "capture_wgc.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

namespace {

BOOL CALLBACK collectMonitor(HMONITOR m, HDC, LPRECT r, LPARAM lp) {
    reinterpret_cast<std::vector<std::pair<HMONITOR, RECT>>*>(lp)->push_back({m, *r});
    return TRUE;
}

winrt::com_ptr<ID3D11Device> makeDevice() {
    winrt::com_ptr<ID3D11Device> d;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                        D3D11_SDK_VERSION, d.put(), nullptr, nullptr)))
            return d;
        d = nullptr;
    }
    return d;
}

}  // namespace

bool wgcSupported() {
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool wgcBorderless() {
    try {
        return wgcSupported() && winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
                                     L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
    } catch (...) {
        return false;
    }
}

bool grabWithWgc(const RECT& virt, gfx::Image& out) {
    try {
        const auto d3d = makeDevice();
        if (!d3d) return false;
        winrt::com_ptr<ID3D11DeviceContext> ctx;
        d3d->GetImmediateContext(ctx.put());
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(d3d.as<IDXGIDevice>().get(), inspectable.put()));
        const auto device = inspectable.as<wdx::Direct3D11::IDirect3DDevice>();
        const auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

        std::vector<std::pair<HMONITOR, RECT>> monitors;
        EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
        const int W = virt.right - virt.left, H = virt.bottom - virt.top;
        if (W <= 0 || H <= 0 || monitors.empty()) return false;
        gfx::Image img;
        img.width = W;
        img.height = H;
        img.px.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0xFF000000u);

        for (const auto& [monitor, rc] : monitors) {
            wgc::GraphicsCaptureItem item{nullptr};
            winrt::check_hresult(
                interop->CreateForMonitor(monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));
            auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
            auto session = pool.CreateCaptureSession(item);
            try {
                session.IsCursorCaptureEnabled(false);
            } catch (...) {
            }
            try {
                session.IsBorderRequired(false);  // Win11+; older builds keep the border
            } catch (...) {
            }
            session.StartCapture();
            wgc::Direct3D11CaptureFrame frame{nullptr};
            for (int i = 0; i < 200 && !frame; ++i) {  // first frame normally lands within a vsync or two
                frame = pool.TryGetNextFrame();
                if (!frame) Sleep(5);
            }
            if (!frame) {
                session.Close();
                pool.Close();
                return false;
            }
            winrt::com_ptr<ID3D11Texture2D> tex;
            const auto access =
                frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), tex.put_void()));
            D3D11_TEXTURE2D_DESC desc{};
            tex->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            winrt::com_ptr<ID3D11Texture2D> staging;
            winrt::check_hresult(d3d->CreateTexture2D(&desc, nullptr, staging.put()));
            ctx->CopyResource(staging.get(), tex.get());
            D3D11_MAPPED_SUBRESOURCE map{};
            winrt::check_hresult(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map));
            const int ox = rc.left - virt.left, oy = rc.top - virt.top;
            const int mw = std::min({static_cast<int>(desc.Width), static_cast<int>(rc.right - rc.left), W - ox});
            const int mh = std::min({static_cast<int>(desc.Height), static_cast<int>(rc.bottom - rc.top), H - oy});
            for (int y = 0; y < mh && ox >= 0 && oy >= 0; ++y) {
                const auto* src = static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch;
                std::uint32_t* dst = &img.px[static_cast<std::size_t>(oy + y) * static_cast<std::size_t>(W) +
                                             static_cast<std::size_t>(ox)];
                std::memcpy(dst, src, static_cast<std::size_t>(mw) * 4u);
                for (int x = 0; x < mw; ++x) dst[x] |= 0xFF000000u;
            }
            ctx->Unmap(staging.get(), 0);
            frame.Close();
            session.Close();
            pool.Close();
        }
        out = std::move(img);
        return true;
    } catch (...) {
        return false;
    }
}
