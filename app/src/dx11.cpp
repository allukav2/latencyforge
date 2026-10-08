#include "dx11.hpp"

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace lfapp {

bool Dx11::init(HWND hwnd, std::string& error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
                                   &m_device, &got, &m_context);
    if (FAILED(hr)) {
        // VM 等で GPU が使えない場合はソフトウェアラスタライザ (WARP)。
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &m_device,
                               &got, &m_context);
        m_warp = true;
    }
    if (FAILED(hr)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "D3D11CreateDevice failed (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
        error = buf;
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(m_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        error = "DXGI factory unavailable";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &sd, nullptr, nullptr, &m_swap);
    if (FAILED(hr)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "CreateSwapChainForHwnd failed (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
        error = buf;
        return false;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    createTarget();
    return true;
}

void Dx11::createTarget() {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv);
    }
}

void Dx11::resize(UINT width, UINT height) {
    if (!m_swap || width == 0 || height == 0) return;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
    m_swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createTarget();
}

void Dx11::beginFrame(const float clearColor[4]) {
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
}

void Dx11::present() { m_swap->Present(1, 0); }

}  // namespace lfapp
