#pragma once

namespace dsvr {

// Called only by an accepted frame from the startup-selected render host.
// Acquires gameplay adapters once, after render installation has returned.
void CoreOnRenderFrame();

// Called only after a real successful desktop Present. Grants blur readiness
// without publishing a render host or enabling XR/gameplay adapters.
void CoreOnDesktopFrame();

// Startup-selected frame-host values, frozen at install time (core.cpp).
// The mod menu compares the live config against these so restart-gated rows
// (render host, eye resolution) show their reminder only after a real change.
bool CoreStartupRenderHost(bool& reshade_host);
bool CoreStartupEyeScale(int& render_scale_percent);

}  // namespace dsvr
