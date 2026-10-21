#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "camera_math.h"
#include "pad_math.h"
#include "vr_trace.h"

namespace dsvr {

// Evidence from trusted adapters, not user-configurable claims. Unknown is false.
struct LabSafety {
  bool enabled = false;
  bool authorized = false;
  bool pinned_executable = false;
  bool offline_verified = false;
  bool isolated_save_verified = false;
};

struct LabObservation {
  std::uint64_t at_ms = 0;
  std::uint64_t sequence = 0;
  std::uintptr_t player = 0;
  std::uint64_t scene_epoch = 0;
  bool gameplay_verified = false;
  bool safe_scene_verified = false;
  bool user_intervened = false;
  bool root_valid = false;
  bool camera_valid = false;
  Vec3 root{};
  Vec3 camera{};
};

enum LabBlocker : unsigned {
  kLabDisabled = 1 << 0, kLabUnauthorized = 1 << 1,
  kLabExecutableUnknown = 1 << 2, kLabOfflineUnknown = 1 << 3,
  kLabIsolationUnknown = 1 << 4, kLabGameplayUnknown = 1 << 5,
  kLabSafeSceneUnknown = 1 << 6, kLabObservationInvalid = 1 << 7,
  kLabLimitsInvalid = 1 << 8
};

enum class LabPhase {
  Dormant, Blocked, AwaitCapture, Idle, Walk, Stop, AwaitClose,
  Passed, Failed, Aborted
};
enum class LabReason {
  None, NotReady, InputNotPolled, ObservationsMissing, RootDidNotMove, CameraDidNotMove,
  RootMovedWhileIdle, RootDidNotStop, UnsafeTravel, IdentityChanged,
  SafetyChanged, StaleObservation, InvalidObservation, ClockReversed,
  UserIntervened, CaptureFailed, CaptureEndedEarly, Timeout, RequestedAbort,
  Contended, PhaseDeadline
};

// Bounded diagnostic controls, NOT stock gameplay tolerances or VR acceptance.
// Distances are native game units; no assumed metres/world-scale conversion.
struct LabLimits {
  unsigned idle_ms = 1000;
  unsigned walk_ms = 500;
  unsigned stop_ms = 1000;
  unsigned settle_ms = 500;
  unsigned lease_ms = 100;
  unsigned timeout_ms = 5000;
  float walk_axis = .35f;
  float minimum_walk = .02f;
  float minimum_camera_motion = .001f;
  float maximum_travel = .75f;
  float stationary_tolerance = .02f;
};

struct LabResult {
  LabPhase phase = LabPhase::Dormant;
  LabReason reason = LabReason::None;
  unsigned blockers = 0;
  std::uint64_t run = 0;
  std::array<unsigned, 3> polls{};
  std::array<unsigned, 3> observations{};
  float walk_distance = 0;
  float camera_distance = 0;
  float stop_drift = 0;
  LabLimits limits{};
};

// Owning adapter serializes calls; the state machine never sleeps or does I/O.
// Poll is the native input seam, independent of OpenXR/controller validity.
class LabScenario {
 public:
  explicit LabScenario(LabLimits limits = {}) : limits_(limits) {}
  bool Start(const LabSafety&, const LabObservation&, std::uint64_t now);
  void Observe(const LabSafety&, const LabObservation&, std::uint64_t now,
               TraceCaptureState);
  // True => replace the returned native pad with out (including neutral).
  // False => do not modify ordinary input. Any user input aborts lab control.
  bool Poll(const VirtualPadState& physical, const VirtualPadState& motion,
            std::uint64_t now, VirtualPadState& out);
  void Abort(LabReason);
  LabResult Result() const { return result_; }
  bool Active() const;
  bool OwnsInput() const;

 private:
  unsigned Blockers(const LabSafety&, const LabObservation&, std::uint64_t) const;
  void End(LabPhase, LabReason);
  void BeginPhase(LabPhase, std::uint64_t, const LabObservation&);
  LabLimits limits_;
  LabResult result_{};
  std::uint64_t started_ms_ = 0, phase_ms_ = 0, last_now_ = 0;
  std::uint64_t observation_ms_ = 0, sequence_ = 0, epoch_ = 0;
  std::uintptr_t player_ = 0;
  Vec3 origin_{}, walk_origin_{}, camera_origin_{}, stop_reference_{};
  bool release_pending_ = false;
};

const char* LabPhaseName(LabPhase);
const char* LabReasonName(LabReason);
// A state-machine report is not a native runtime/GPU/comfort certificate.
std::string LabResultJson(const LabResult&);

}  // namespace dsvr
