// Test-only DSVR-named DLL. No game gates, hooks, config, XR or native engine
// code are linked. Readback replaces XR; bridge validation is production code.
#include "reshade_fixture.h"
#include "../src/core/reshade_bridge.h"
#include <windows.h>
#include <d3d11.h>

namespace {
dsvr::RenderHost host;
dsvr::ReShadeBridge bridge{host};
ReShadeFixtureSnapshot snapshot;

void Capture(void*, std::uintptr_t device) {
  ++snapshot.captures;
  snapshot.device = device;  // test owns device for the fixture's full lifetime
}
void Poll(void*) { ++snapshot.polls; }
void Readback(void*, const dsvr::HostSource& source, dsvr::HostStamp stamp) {
  ++snapshot.submissions;
  snapshot.stamp = stamp;
  snapshot.image = source.image;
  snapshot.swapchain = source.swapchain;
  snapshot.thread = source.thread;
  auto* texture = reinterpret_cast<ID3D11Texture2D*>(source.image);
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  snapshot.width = desc.Width;
  snapshot.height = desc.Height;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  auto* device = reinterpret_cast<ID3D11Device*>(source.device);
  ID3D11Texture2D* stage = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &stage))) return;
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  context->CopyResource(stage, texture);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context->Map(stage, 0, D3D11_MAP_READ, 0, &mapped))) {
    snapshot.pixel = *static_cast<const unsigned*>(mapped.pData);
    ++snapshot.readbacks;
    context->Unmap(stage, 0);
  }
  context->Release();
  stage->Release();
}
}  // namespace

extern "C" __declspec(dllexport) bool FixtureStart() {
  const bool configured = host.Configure(true, dsvr::RenderHostKind::ReShade,
                                          {nullptr, Capture, Poll, Readback});
  bridge.SetReady(configured);
  return configured;
}
extern "C" __declspec(dllexport) void FixtureSnapshot(ReShadeFixtureSnapshot* out) {
  *out = snapshot;
}
extern "C" __declspec(dllexport) void __stdcall DsvrReShadeFrameV1(
    std::uintptr_t swapchain, void* resource, void* window) {
  snapshot.result = bridge.Frame(swapchain, resource, window);
}
extern "C" __declspec(dllexport) void __stdcall DsvrReShadeInvalidateV1(
    std::uintptr_t swapchain) {
  bridge.Invalidate(swapchain);
}
