#pragma once

namespace dsvr {

// Native frame-rate application via the game's own rate setter
// (0x140CE2CF0: RCX=owner, EDX=rate, R8B=flag; resets QPC epoch/counters).
// Installed like the other gameplay hooks (first accepted frame, pinned
// bytes); applies the effective rate (display_rate.h) once per change at
// the presentation owner's frame boundary -- never inside the wait helper,
// whose second argument is an absolute frame index, not an FPS value.
bool FrameRateHookInstall(bool vr_enabled);
void FrameRateHookUninstall();

}  // namespace dsvr
