// Hardware probe for the launcher's auto-fit presets (UI-free; shared by
// the GUI and any future CLI flag). Fail-soft by construction: any failed
// query leaves its field zero, which the tier model reads as the lowest
// tier. Never probes the XR runtime: the headset rate comes from the
// game's last-known-rate sidecar, not from spinning up OpenXR here.
#include "setup_internal.h"

#include <windows.h>

#include <dxgi.h>

namespace setup {

dsvr::HardwareSpec ProbeHardware() {
  dsvr::HardwareSpec spec{};
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys > 0)
    spec.system_ram_gb =
        static_cast<int>(memory.ullTotalPhys / (1024ULL * 1024ULL * 1024ULL));
  IDXGIFactory* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory))) &&
      factory) {
    for (UINT i = 0;; ++i) {
      IDXGIAdapter* adapter = nullptr;
      if (FAILED(factory->EnumAdapters(i, &adapter)) || !adapter) break;
      DXGI_ADAPTER_DESC desc{};
      const bool readable = SUCCEEDED(adapter->GetDesc(&desc));
      adapter->Release();
      if (!readable) continue;
      // Skip the Microsoft Basic Render Driver: it is not a GPU.
      if (desc.VendorId == 0x1414) continue;
      spec.discrete_gpu = true;
      if (desc.DedicatedVideoMemory > 0)
        spec.gpu_vram_gb = static_cast<int>(
            desc.DedicatedVideoMemory / (1024ULL * 1024ULL * 1024ULL));
      break;  // first real adapter owns the display
    }
    factory->Release();
  }
  return spec;
}

}  // namespace setup
