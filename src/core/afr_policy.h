#pragma once

#include <cstdint>

namespace dsvr {

// Alternating-frame policy (T4). Freshness is defined in the test file:
// an eye submitted this frame has age 0; age counts frames since that
// eye was last submitted. AFR only alternates real stereo eyes - on a
// mono path it refuses, and these helpers stay unused by production
// until DecideStereo can return AttemptStereo.
enum class AfrDecision { Disabled, RefuseStereoInactive, Alternate };

inline AfrDecision DecideAfr(bool afr_enabled, bool stereo_active) {
  if (!afr_enabled) return AfrDecision::Disabled;
  if (!stereo_active) return AfrDecision::RefuseStereoInactive;
  return AfrDecision::Alternate;
}

inline int AfrEyeForFrame(std::uint64_t frame_sequence) {
  return (frame_sequence % 2 == 0) ? 0 : 1;
}

inline unsigned AfrEyeAgeFrames(std::uint64_t now_frame,
                                std::uint64_t eye_last_frame) {
  if (eye_last_frame >= now_frame) return 0;
  const auto age = now_frame - eye_last_frame;
  return age > 0xffffffffull ? 0xffffffffu : (unsigned)age;
}

}  // namespace dsvr
