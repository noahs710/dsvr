#pragma once

namespace dsvr {

// Production gate for the native stereo caller (T3). XrBeginNativeStereoFrame
// can only submit eye targets that a native renderer has actually drawn;
// at Present time the game has rendered exactly one image, so no eye
// renderer is bound yet and AttemptStereo is unreachable in production.
// The gate exists so the flag, the refusal, and the mono fallback are
// tested behaviour, not comments. Faking the second eye is a defect.
enum class StereoDecision { MonoDefault, RefuseNoRenderer, AttemptStereo };

inline StereoDecision DecideStereo(bool requested, bool session_running,
                                   bool eye_renderer_bound) {
  if (!requested) return StereoDecision::MonoDefault;
  if (!eye_renderer_bound) return StereoDecision::RefuseNoRenderer;
  if (!session_running) return StereoDecision::MonoDefault;
  return StereoDecision::AttemptStereo;
}

}  // namespace dsvr
