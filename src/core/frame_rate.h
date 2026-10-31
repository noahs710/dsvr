#pragma once

namespace dsvr {

// Compatibility stub: DSVR does not patch the game's native rate setter.
// High-FPS pacing and variable simulation time are delegated to the chained
// DSR-FPS-Unlock dependency; this must remain disabled to avoid a second pacer.
bool FrameRateHookInstall(bool vr_enabled);
void FrameRateHookUninstall();

}  // namespace dsvr
