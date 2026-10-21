#include "reshade_bridge.h"

#include <windows.h>
#include <d3d11.h>

namespace dsvr {

bool RenderWindowBelongsToProcess(void* window) {
  DWORD process = 0;
  return window && IsWindow(static_cast<HWND>(window)) &&
         GetWindowThreadProcessId(static_cast<HWND>(window), &process) &&
         process == GetCurrentProcessId();
}

HostDispatch ReShadeBridge::Frame(std::uintptr_t swapchain, void* resource,
                                void* window) {
  if (!ready_.load() || !swapchain || !resource ||
      !RenderWindowBelongsToProcess(window)) return HostDispatch::Refused;
  auto* native = static_cast<ID3D11Resource*>(resource);
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(native->QueryInterface(__uuidof(ID3D11Texture2D),
                                   reinterpret_cast<void**>(&texture))) || !texture)
    return HostDispatch::Refused;
  ID3D11Device* device = nullptr;
  texture->GetDevice(&device);
  HostDispatch result = HostDispatch::Refused;
  if (device) {
    result = host_.Dispatch({RenderHostKind::ReShade,
        reinterpret_cast<std::uintptr_t>(device), swapchain,
        reinterpret_cast<std::uintptr_t>(texture), GetCurrentThreadId()});
    device->Release();
  }
  texture->Release();
  return result;
}

void ReShadeBridge::Invalidate(std::uintptr_t swapchain) {
  if (ready_.load()) host_.Invalidate(RenderHostKind::ReShade, swapchain);
}

}  // namespace dsvr
