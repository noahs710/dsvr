#pragma once

#include "render_host.h"

namespace dsvr {

bool RenderWindowBelongsToProcess(void* window);

// Native validation shared by the production core and offline D3D11 tests.
// Publishing readiness does not select a host or initialize XR. Only gated
// production startup may publish it in the real core; no test export exists.
class ReShadeBridge {
 public:
  explicit ReShadeBridge(RenderHost& host) : host_(host) {}
  void SetReady(bool ready) { ready_.store(ready); }
  HostDispatch Frame(std::uintptr_t swapchain, void* resource, void* window);
  void Invalidate(std::uintptr_t swapchain);

 private:
  RenderHost& host_;
  std::atomic<bool> ready_{false};
};

}  // namespace dsvr
