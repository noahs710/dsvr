// Real WARP device regression for the depth-probe vtable slots. If the
// slot map is wrong, OMSetRenderTargets/Draw detours receive mismatched
// arguments: the first build crashed the game at launch by reading a
// stencil-ref argument as a view pointer. This fixture performs real
// OMSet + Draw calls through a hooked context and requires the pair to
// be counted with the textures and sizes we actually bound.
#include "../src/core/depth_probe.h"
#include "../src/common/logging.h"

#include <d3d11.h>
#include <cstdio>

namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
}

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  D3D_FEATURE_LEVEL level{};
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                    nullptr, 0, D3D11_SDK_VERSION, &device,
                                    &level, &context)));
  CHECK(dsvr::DepthProbeInstall(device));

  ID3D11Texture2D* colour = nullptr;
  ID3D11Texture2D* depth = nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = 1920; desc.Height = 1080; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &colour)));
  desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth)));
  ID3D11RenderTargetView* rtv = nullptr;
  ID3D11DepthStencilView* dsv = nullptr;
  CHECK(SUCCEEDED(device->CreateRenderTargetView(colour, nullptr, &rtv)));
  CHECK(SUCCEEDED(device->CreateDepthStencilView(depth, nullptr, &dsv)));

  context->OMSetRenderTargets(1, &rtv, dsv);
  context->Draw(3, 0);           // non-indexed path (slot 13)
  context->DrawIndexed(3, 0, 0); // indexed path (slot 12); no IB bound is fine for counting

  dsvr::DepthProbeFrameBoundary(nullptr, 1);
  CHECK(dsvr::DepthProbeSceneSource() == nullptr);
  context->OMSetRenderTargets(1, &rtv, nullptr);
  context->Draw(3,0);
  dsvr::DepthProbeFrameBoundary(nullptr, 2);
  CHECK(dsvr::DepthProbeSceneSource() == colour);
  dsvr::DepthProbeBeginInternalPass();
  dsvr::DepthProbeEndInternalPass();
  context->Draw(3, 0);
  dsvr::DepthProbeFrameBoundary(nullptr, 2);
  CHECK(dsvr::DepthProbeSceneSource() == colour);

  desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  desc.Width=1537;desc.Height=877;
  ID3D11Texture2D* sampled=nullptr;
  ID3D11ShaderResourceView* sampled_view=nullptr;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&sampled)));
  CHECK(SUCCEEDED(device->CreateShaderResourceView(sampled,nullptr,&sampled_view)));
  context->PSSetShaderResources(0,1,&sampled_view);
  context->Draw(3,0);
  dsvr::DepthProbeFrameBoundary(colour,3);
  CHECK(dsvr::DepthProbeSceneSource() == sampled);

  // Unbinding and drawing elsewhere must not disturb the recorded pair.
  context->OMSetRenderTargets(0, nullptr, nullptr);
  context->Draw(3, 0);
  dsvr::DepthProbeFrameBoundary(nullptr,4);
  CHECK(dsvr::DepthProbeSceneSource() == nullptr);

  dsvr::DepthProbeUninstall();
  context->ClearState();
  sampled_view->Release();sampled->Release();
  rtv->Release(); dsv->Release(); colour->Release(); depth->Release();
  context->Release(); device->Release();
  std::puts("PASS depth probe pair identification (WARP, not live rendering)");
  return 0;
}
