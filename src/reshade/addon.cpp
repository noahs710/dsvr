// Thin ReShade SDK adapter. The core remains the only OpenXR owner.
#include <reshade.hpp>
#include "../common/version.h"

#include <cstdint>

namespace {
using FrameFn = void(__stdcall*)(std::uintptr_t, void*, void*);
using InvalidateFn = void(__stdcall*)(std::uintptr_t);

// Acquire an existing module reference for the duration of each callback. Do
// not LoadLibrary, start the core, or cache a pointer across module unloading.
void OnPresent(reshade::api::command_queue*, reshade::api::swapchain* swap,
               const reshade::api::rect*, const reshade::api::rect*,
               uint32_t, const reshade::api::rect*) {
  if (!swap || !swap->get_device() ||
      swap->get_device()->get_api() != reshade::api::device_api::d3d11 ||
      !swap->get_hwnd()) return;  // also excludes offscreen XR swapchains
  HMODULE core = nullptr;
  if (!GetModuleHandleExW(0, L"DSVR.dll", &core)) return;
  const auto frame = reinterpret_cast<FrameFn>(
      GetProcAddress(core, "DsvrReShadeFrameV1"));
  if (frame) {
    // D3D11 resource handles are native ID3D11Resource pointers. Let the core
    // QI the texture and acquire its device; do not treat a ReShade swapchain
    // object (or an OpenXR session handle) as an IDXGISwapChain pointer.
    const auto count = swap->get_back_buffer_count();
    if (count) {
      const auto index = swap->get_current_back_buffer_index();
      if (index < count) {
        const auto image = swap->get_back_buffer(index);
        if (image.handle)
          frame(reinterpret_cast<std::uintptr_t>(swap),
                reinterpret_cast<void*>(image.handle), swap->get_hwnd());
      }
    }
  }
  FreeLibrary(core);
}

void OnDestroySwapchain(reshade::api::swapchain* swap, bool /*resize*/) {
  if (!swap || !swap->get_device() ||
      swap->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  HMODULE core = nullptr;
  if (!GetModuleHandleExW(0, L"DSVR.dll", &core)) return;
  const auto invalidate = reinterpret_cast<InvalidateFn>(
      GetProcAddress(core, "DsvrReShadeInvalidateV1"));
  if (invalidate) invalidate(reinterpret_cast<std::uintptr_t>(swap));
  FreeLibrary(core);
}
}  // namespace

extern "C" __declspec(dllexport) const char* NAME = "DSVR D3D11 host";
extern "C" __declspec(dllexport) const char* DSVR_VERSION = dsvr::kModVersion;
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Forwards mono source frames to the pinned DSVR core; no second XR owner.";

extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE self, HMODULE host) {
  if (!reshade::register_addon(self, host)) return FALSE;
  reshade::register_event<reshade::addon_event::present>(OnPresent);
  reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  return TRUE;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE self, HMODULE host) {
  reshade::unregister_event<reshade::addon_event::present>(OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
  reshade::unregister_addon(self, host);
}
