#pragma once

namespace dsvr {

// Startup-only normal render-host selection. The ReShade path installs no DXGI
// hooks/dummy device and has no hot fallback.
bool RenderHookInstall(bool vr_enabled, bool verbose, bool reshade_host = false);
// Observer-only UI presentation hook. It does not configure or dispatch through
// RenderHost and never captures an XR device.
bool ObserverMenuHookInstall();
void RenderHookUninstall();

}  // namespace dsvr
