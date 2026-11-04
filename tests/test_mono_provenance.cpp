#include "../src/core/mono_provenance.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <source_location>

namespace {
dsvr::MonoProvenance ValidProvenance() {
  dsvr::MonoProvenance p;
  p.sequence = 42;
  p.expected_sequence = 42;
  p.space_epoch = 3;
  p.current_epoch = 3;
  p.session_focused = true;
  p.source_focused = true;
  p.orientation_valid = true;
  p.position_valid = true;
  p.pose_valid = true;
  p.now_ms = 10'000;
  p.captured_ms = 9'950;
  p.predicted_ns = 1'111'111'100;
  p.display_time_ns = 1'111'111'100;
  p.display_period_ns = 11'111'111;
  p.vfov_rad = 1.9f;
  p.fov_aspect = 0.96f;
  return p;
}
}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) {
      ++failures;
      std::printf("failed at line %u\n", at.line());
    }
  };
  using dsvr::MonoProvenanceFailure;

  auto expect = [&](const dsvr::MonoProvenance& p, const char* name) {
    const char* got = MonoProvenanceFailure(p);
    const bool ok = name == nullptr ? got == nullptr
                                    : got != nullptr && std::strcmp(got, name) == 0;
    if (!ok) {
      ++failures;
      std::printf("expected gate %s, got %s\n", name ? name : "none",
                  got ? got : "none");
    }
  };

  // Happy path: one fresh, matched, camera-bound receipt.
  expect(ValidProvenance(), nullptr);

  // Camera receipt identity: missing, mismatched, and the first failure wins.
  {
    auto p = ValidProvenance();
    p.sequence = 0;
    expect(p, "sequence");
    p = ValidProvenance();
    p.sequence = 41;
    expect(p, "sequence");
    p = ValidProvenance();
    p.sequence = 0;
    p.space_epoch = 9;
    expect(p, "sequence");
  }

  // Tracking-space generation must match the live epoch.
  {
    auto p = ValidProvenance();
    p.space_epoch = 4;
    expect(p, "epoch");
  }

  // Focus gates the image only while the session claims focus: a VISIBLE
  // session is already composited and must not force the recovery room.
  {
    auto p = ValidProvenance();
    p.source_focused = false;
    expect(p, "focus");
    p.session_focused = false;
    expect(p, nullptr);
  }

  // Both tracked pose halves are required.
  {
    auto p = ValidProvenance();
    p.orientation_valid = false;
    expect(p, "tracking");
    p = ValidProvenance();
    p.position_valid = false;
    expect(p, "tracking");
  }

  // Freshness: exactly 100 ms old is still this frame's image; 101 ms is not,
  // and a future timestamp is a broken clock, not a fresh receipt.
  {
    auto p = ValidProvenance();
    p.captured_ms = 9'900;
    expect(p, nullptr);
    p.captured_ms = 9'899;
    expect(p, "stale");
    p = ValidProvenance();
    p.captured_ms = 10'001;
    expect(p, "stale");
  }

  // Prediction: one display period of lead is tolerated (a faster game loop);
  // more is a broken receipt. Zero or negative is never usable.
  {
    auto p = ValidProvenance();
    p.predicted_ns = p.display_time_ns + p.display_period_ns;
    expect(p, nullptr);
    p.predicted_ns = p.display_time_ns + p.display_period_ns + 1;
    expect(p, "predict");
    p = ValidProvenance();
    p.predicted_ns = 0;
    expect(p, "predict");
    p = ValidProvenance();
    p.predicted_ns = -1;
    expect(p, "predict");
    // The game may also present behind the prediction; that is fine.
    p = ValidProvenance();
    p.predicted_ns = p.display_time_ns - p.display_period_ns;
    expect(p, nullptr);
  }

  // Projection sanity, including non-finite values.
  {
    auto p = ValidProvenance();
    p.vfov_rad = 0.5f;
    expect(p, "fov");
    p.vfov_rad = 3.1f;
    expect(p, "fov");
    p.vfov_rad = std::numeric_limits<float>::quiet_NaN();
    expect(p, "fov");
    p = ValidProvenance();
    p.fov_aspect = 0.0f;
    expect(p, "fov");
    p.fov_aspect = -1.0f;
    expect(p, "fov");
    p.fov_aspect = std::numeric_limits<float>::quiet_NaN();
    expect(p, "fov");
    p = ValidProvenance();
    p.vfov_rad = 0.5001f;
    p.fov_aspect = 0.0001f;
    expect(p, nullptr);
  }

  // The pose itself must be finite and usable.
  {
    auto p = ValidProvenance();
    p.pose_valid = false;
    expect(p, "pose");
  }

  if (!failures) std::printf("mono_provenance: checks passed\n");
  return failures ? 1 : 0;
}
