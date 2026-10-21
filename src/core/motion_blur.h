#pragma once
#include <cstdint>
namespace dsvr {
struct MotionBlurStatus {bool installed=false;std::uint64_t reads=0,blocked_enables=0;};
// Applies in flatscreen and VR. Setup records/restores the original preference;
// these hooks prevent native option reads/writes from enabling blur while loaded.
bool MotionBlurHookInstall(bool enabled);
void MotionBlurHookUninstall();
MotionBlurStatus MotionBlurGetStatus();
}
