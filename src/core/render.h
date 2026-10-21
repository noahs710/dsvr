#pragma once

namespace dsvr {

// Startup-only host selection. Both paths use the same gated XR dispatcher;
// the ReShade path installs no DXGI hooks/dummy device and has no hot fallback.
bool RenderHookInstall(bool vr_enabled, bool verbose, bool reshade_host = false);
void RenderHookUninstall();

}  // namespace dsvr
