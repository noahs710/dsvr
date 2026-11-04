#pragma once

#include <cstdint>

namespace dsvr {

// Mono image submission gate. The XR frame may submit only the game image
// whose camera receipt matches this frame; a missing, stale or mismatched
// receipt takes the view away (the recovery room) until a fresh one arrives.
//
// Pure and clock-free: the caller supplies the current state, so every gate is
// unit-testable without an XR session or a GPU.
struct MonoProvenance {
  // Camera receipt identity, captured once at XR frame entry.
  std::uint64_t sequence = 0;
  std::uint64_t expected_sequence = 0;
  unsigned space_epoch = 0;
  unsigned current_epoch = 0;
  // Focus gates the image only while the session claims focus: a VISIBLE
  // session is already composited by the runtime and must not force a room.
  bool session_focused = true;
  bool source_focused = false;
  bool orientation_valid = false;
  bool position_valid = false;
  bool pose_valid = false;
  // Freshness: a receipt older than 100 ms is not this frame's image.
  std::uint64_t now_ms = 0;
  std::uint64_t captured_ms = 0;
  // Prediction: the game may present one display period ahead of this XR
  // frame. A larger lead is a broken receipt, not judder.
  std::int64_t predicted_ns = 0;
  std::int64_t display_time_ns = 0;
  std::int64_t display_period_ns = 0;
  // Projection sanity.
  float vfov_rad = 0.0f;
  float fov_aspect = 0.0f;
};

// The first failing gate name, or nullptr when the image may be submitted.
// Names are stable for logs: "sequence", "epoch", "focus", "tracking",
// "stale", "predict", "fov", "pose".
inline const char* MonoProvenanceFailure(const MonoProvenance& p) {
  if (!p.sequence || p.sequence != p.expected_sequence) return "sequence";
  if (p.space_epoch != p.current_epoch) return "epoch";
  if (p.session_focused && !p.source_focused) return "focus";
  if (!p.orientation_valid || !p.position_valid) return "tracking";
  if (p.now_ms < p.captured_ms || p.now_ms - p.captured_ms > 100) return "stale";
  if (p.predicted_ns <= 0 ||
      p.predicted_ns > p.display_time_ns + p.display_period_ns) return "predict";
  if (!(p.vfov_rad > 0.5f && p.vfov_rad < 3.1f) || !(p.fov_aspect > 0.0f))
    return "fov";
  return p.pose_valid ? nullptr : "pose";
}

}  // namespace dsvr
