#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>

namespace lfapp {

// D3D11 デバイス + フリップモデル スワップチェーン。ハードウェア不可なら WARP にフォールバック。
class Dx11 {
public:
    bool init(HWND hwnd, std::string& error);
    void resize(UINT width, UINT height);
    void beginFrame(const float clearColor[4]);
    void present();

    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* context() const { return m_context.Get(); }
    bool isWarp() const { return m_warp; }

private:
    void createTarget();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swap;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    bool m_warp = false;
};

}  // namespace lfapp
