#pragma once

#include <cstdint>

namespace dsvr {

// On the natural follow-camera owner, after its original update. Read-only,
// capture-bounded; no native method invocation or active-slot inference.
void CameraObserveNative(void* follow_cam, std::uint64_t capture_id);

}  // namespace dsvr
