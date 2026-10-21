#include "lab_scenario.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace dsvr {
namespace {

bool Finite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         std::fabs(v.x) < 10000 && std::fabs(v.y) < 10000 && std::fabs(v.z) < 10000;
}
float Distance(Vec3 a, Vec3 b) { return VLen({a.x-b.x, a.y-b.y, a.z-b.z}); }
float PlanarDistance(Vec3 a, Vec3 b) { return VLen({a.x-b.x, 0, a.z-b.z}); }
int PhaseIndex(LabPhase phase) {
  if (phase == LabPhase::Idle) return 0;
  if (phase == LabPhase::Walk) return 1;
  if (phase == LabPhase::Stop) return 2;
  return -1;
}
bool AnyInput(const VirtualPadState& pad) {
  return pad.buttons || pad.left_trigger || pad.right_trigger ||
         pad.thumb_lx || pad.thumb_ly || pad.thumb_rx || pad.thumb_ry;
}
std::string Number(float value) {
  if (!std::isfinite(value)) return "null";
  char text[40]{};
  const auto converted = std::to_chars(text, text + sizeof(text), value,
                                       std::chars_format::general, 9);
  return converted.ec == std::errc{} ? std::string(text, converted.ptr) : "null";
}

}  // namespace

bool LabScenario::Active() const {
  return result_.phase == LabPhase::AwaitCapture ||
         result_.phase == LabPhase::AwaitClose || PhaseIndex(result_.phase) >= 0;
}

bool LabScenario::OwnsInput() const {
  return release_pending_ || result_.phase == LabPhase::AwaitClose ||
         PhaseIndex(result_.phase) >= 0;
}

unsigned LabScenario::Blockers(const LabSafety& safety, const LabObservation& o,
                               std::uint64_t now) const {
  unsigned bits = 0;
  if (!safety.enabled) bits |= kLabDisabled;
  if (!safety.authorized) bits |= kLabUnauthorized;
  if (!safety.pinned_executable) bits |= kLabExecutableUnknown;
  if (!safety.offline_verified) bits |= kLabOfflineUnknown;
  if (!safety.isolated_save_verified) bits |= kLabIsolationUnknown;
  if (!o.gameplay_verified) bits |= kLabGameplayUnknown;
  if (!o.safe_scene_verified) bits |= kLabSafeSceneUnknown;
  if (!o.player || !o.scene_epoch || !o.sequence || !o.root_valid || !o.camera_valid ||
      !Finite(o.root) || !Finite(o.camera) || o.at_ms > now ||
      now - o.at_ms > limits_.lease_ms || o.user_intervened)
    bits |= kLabObservationInvalid;
  const std::uint64_t duration = static_cast<std::uint64_t>(limits_.idle_ms) +
                                limits_.walk_ms + limits_.stop_ms;
  if (!limits_.idle_ms || !limits_.walk_ms || !limits_.stop_ms ||
      !limits_.lease_ms || limits_.lease_ms > 1000 ||
      limits_.timeout_ms > 20000 || duration >= limits_.timeout_ms ||
      limits_.settle_ms >= limits_.stop_ms ||
      !std::isfinite(limits_.walk_axis) || limits_.walk_axis <= 0 || limits_.walk_axis > .5f ||
      !std::isfinite(limits_.minimum_walk) || limits_.minimum_walk <= 0 ||
      !std::isfinite(limits_.minimum_camera_motion) || limits_.minimum_camera_motion <= 0 ||
      !std::isfinite(limits_.maximum_travel) || limits_.maximum_travel <= limits_.minimum_walk ||
      !std::isfinite(limits_.stationary_tolerance) || limits_.stationary_tolerance <= 0)
    bits |= kLabLimitsInvalid;
  return bits;
}

bool LabScenario::Start(const LabSafety& safety, const LabObservation& o,
                        std::uint64_t now) {
  if (Active() || release_pending_) return false;
  const auto run = result_.run + 1;
  result_ = {};
  result_.run = run;
  result_.limits = limits_;
  result_.blockers = Blockers(safety, o, now);
  if (result_.blockers) {
    End(LabPhase::Blocked, LabReason::NotReady);
    return false;
  }
  result_.phase = LabPhase::AwaitCapture;
  player_ = o.player;
  epoch_ = o.scene_epoch;
  sequence_ = 0;
  origin_ = o.root;
  started_ms_ = last_now_ = now;
  observation_ms_ = o.at_ms;
  return true;
}

void LabScenario::End(LabPhase phase, LabReason reason) {
  // Keep ownership for one final neutral poll before releasing it.
  release_pending_ = release_pending_ || PhaseIndex(result_.phase) >= 0 ||
                     result_.phase == LabPhase::AwaitClose;
  result_.phase = phase;
  result_.reason = reason;
}

void LabScenario::Abort(LabReason reason) {
  if (Active()) End(LabPhase::Aborted, reason);
}

void LabScenario::BeginPhase(LabPhase phase, std::uint64_t now,
                             const LabObservation& o) {
  result_.phase = phase;
  phase_ms_ = now;
  if (phase == LabPhase::Idle) origin_ = o.root;
  if (phase == LabPhase::Walk) { walk_origin_ = o.root; camera_origin_ = o.camera; }
  if (phase == LabPhase::Stop) stop_reference_ = o.root;
}

void LabScenario::Observe(const LabSafety& safety, const LabObservation& o,
                          std::uint64_t now, TraceCaptureState capture) {
  if (!Active()) return;
  if (now < last_now_) { Abort(LabReason::ClockReversed); return; }
  last_now_ = now;
  if (now - started_ms_ > limits_.timeout_ms) {
    End(LabPhase::Failed, LabReason::Timeout); return;
  }
  if (now - observation_ms_ > limits_.lease_ms) {
    Abort(LabReason::StaleObservation); return;
  }
  result_.blockers = Blockers(safety, o, now);
  if (o.user_intervened) { Abort(LabReason::UserIntervened); return; }
  if (result_.blockers) { Abort(LabReason::SafetyChanged); return; }
  if (o.player != player_ || o.scene_epoch != epoch_) {
    Abort(LabReason::IdentityChanged); return;
  }
  if (capture == TraceCaptureState::Dropped || capture == TraceCaptureState::IoError ||
      capture == TraceCaptureState::Interrupted) {
    End(LabPhase::Failed, LabReason::CaptureFailed); return;
  }
  if (result_.phase == LabPhase::AwaitClose) {
    // Capture acknowledgement must not turn late root motion into a pass.
    result_.stop_drift = std::max(result_.stop_drift, Distance(o.root, stop_reference_));
    if (result_.stop_drift > limits_.stationary_tolerance) {
      End(LabPhase::Failed, LabReason::RootDidNotStop); return;
    }
    if (o.sequence > sequence_) { sequence_ = o.sequence; observation_ms_ = o.at_ms; }
    if (capture == TraceCaptureState::Complete) End(LabPhase::Passed, LabReason::None);
    return;
  }
  if (result_.phase == LabPhase::AwaitCapture) {
    if (capture != TraceCaptureState::Recording) return;
    BeginPhase(LabPhase::Idle, now, o);
  } else if (capture != TraceCaptureState::Recording) {
    End(LabPhase::Failed, LabReason::CaptureEndedEarly); return;
  }
  if (o.sequence <= sequence_) {
    if (now - observation_ms_ > limits_.lease_ms) Abort(LabReason::StaleObservation);
    return;  // a replayed sample cannot refresh the lease or complete a phase
  }
  sequence_ = o.sequence;
  observation_ms_ = o.at_ms;
  if (Distance(o.root, origin_) > limits_.maximum_travel) {
    Abort(LabReason::UnsafeTravel); return;
  }
  const int index = PhaseIndex(result_.phase);
  ++result_.observations[index];
  const auto elapsed = now - phase_ms_;
  if (result_.phase == LabPhase::Idle &&
      Distance(o.root, origin_) > limits_.stationary_tolerance) {
    End(LabPhase::Failed, LabReason::RootMovedWhileIdle); return;
  }
  if (result_.phase == LabPhase::Walk) {
    result_.walk_distance = PlanarDistance(o.root, walk_origin_);
    result_.camera_distance = Distance(o.camera, camera_origin_);
  }
  if (result_.phase == LabPhase::Stop) {
    if (elapsed <= limits_.settle_ms) stop_reference_ = o.root;
    else result_.stop_drift = std::max(result_.stop_drift, Distance(o.root, stop_reference_));
  }
  const unsigned duration = index == 0 ? limits_.idle_ms :
                            index == 1 ? limits_.walk_ms : limits_.stop_ms;
  if (elapsed < duration) return;
  if (!result_.polls[index]) { End(LabPhase::Failed, LabReason::InputNotPolled); return; }
  if (result_.observations[index] < 2) {
    End(LabPhase::Failed, LabReason::ObservationsMissing); return;
  }
  if (index == 1 && result_.walk_distance < limits_.minimum_walk) {
    End(LabPhase::Failed, LabReason::RootDidNotMove); return;
  }
  if (index == 1 && result_.camera_distance < limits_.minimum_camera_motion) {
    End(LabPhase::Failed, LabReason::CameraDidNotMove); return;
  }
  if (index == 2 && result_.stop_drift > limits_.stationary_tolerance) {
    End(LabPhase::Failed, LabReason::RootDidNotStop); return;
  }
  BeginPhase(index == 0 ? LabPhase::Walk : index == 1 ? LabPhase::Stop :
                                                      LabPhase::AwaitClose, now, o);
}

bool LabScenario::Poll(const VirtualPadState& physical, const VirtualPadState& motion,
                       std::uint64_t now, VirtualPadState& out) {
  const bool owned = PhaseIndex(result_.phase) >= 0 || result_.phase == LabPhase::AwaitClose;
  if (!owned && !release_pending_) return false;
  out = {};
  if (!owned) { release_pending_ = false; return true; }
  if (now < last_now_) Abort(LabReason::ClockReversed);
  else if (now - observation_ms_ > limits_.lease_ms) Abort(LabReason::StaleObservation);
  else if (now - started_ms_ > limits_.timeout_ms) End(LabPhase::Failed, LabReason::Timeout);
  else if (AnyInput(physical) || AnyInput(motion)) Abort(LabReason::UserIntervened);
  else {
    const int phase = PhaseIndex(result_.phase);
    const unsigned duration = phase == 0 ? limits_.idle_ms :
                              phase == 1 ? limits_.walk_ms : limits_.stop_ms;
    // Observations normally advance phases first. A delayed owning-thread
    // sample cannot extend native controls or stand in for endpoint evidence.
    if (phase >= 0 && now - phase_ms_ >= duration) Abort(LabReason::PhaseDeadline);
  }
  last_now_ = std::max(last_now_, now);
  if (!Active()) { release_pending_ = false; return true; }
  const int index = PhaseIndex(result_.phase);
  if (index >= 0) ++result_.polls[index];
  if (result_.phase == LabPhase::Walk) out.thumb_ly = PadAxis(limits_.walk_axis);
  return true;
}

const char* LabPhaseName(LabPhase phase) {
  switch (phase) {
    case LabPhase::Dormant: return "dormant";
    case LabPhase::Blocked: return "blocked";
    case LabPhase::AwaitCapture: return "await_capture";
    case LabPhase::Idle: return "idle";
    case LabPhase::Walk: return "walk";
    case LabPhase::Stop: return "stop";
    case LabPhase::AwaitClose: return "await_close";
    case LabPhase::Passed: return "passed";
    case LabPhase::Failed: return "failed";
    case LabPhase::Aborted: return "aborted";
  }
  return "invalid";
}

const char* LabReasonName(LabReason reason) {
  switch (reason) {
    case LabReason::None: return "none";
    case LabReason::NotReady: return "not_ready";
    case LabReason::InputNotPolled: return "input_not_polled";
    case LabReason::ObservationsMissing: return "observations_missing";
    case LabReason::RootDidNotMove: return "root_did_not_move";
    case LabReason::CameraDidNotMove: return "camera_did_not_move";
    case LabReason::RootMovedWhileIdle: return "root_moved_while_idle";
    case LabReason::RootDidNotStop: return "root_did_not_stop";
    case LabReason::UnsafeTravel: return "unsafe_travel";
    case LabReason::IdentityChanged: return "identity_changed";
    case LabReason::SafetyChanged: return "safety_changed";
    case LabReason::StaleObservation: return "stale_observation";
    case LabReason::InvalidObservation: return "invalid_observation";
    case LabReason::ClockReversed: return "clock_reversed";
    case LabReason::UserIntervened: return "user_intervened";
    case LabReason::CaptureFailed: return "capture_failed";
    case LabReason::CaptureEndedEarly: return "capture_ended_early";
    case LabReason::Timeout: return "timeout";
    case LabReason::RequestedAbort: return "requested_abort";
    case LabReason::Contended: return "contended";
    case LabReason::PhaseDeadline: return "phase_deadline";
  }
  return "invalid";
}

std::string LabResultJson(const LabResult& r) {
  const char* verdict = r.phase == LabPhase::Passed ? "PASS" :
      r.phase == LabPhase::Blocked ? "BLOCKED" :
      r.phase == LabPhase::Failed || r.phase == LabPhase::Aborted ? "FAIL" : "UNVERIFIED";
  char text[2048]{};
  const int count = std::snprintf(text, sizeof(text),
      "{\"scenario\":\"idle-walk-stop-v1\",\"scenario_verdict\":\"%s\","
      "\"runtime_verified\":false,\"phase\":\"%s\",\"reason\":\"%s\","
      "\"run\":%llu,\"blockers\":%u,\"polls\":[%u,%u,%u],"
      "\"observations\":[%u,%u,%u],\"walk_units\":%s,\"camera_units\":%s,"
      "\"stop_drift_units\":%s,\"limits\":{\"idle_ms\":%u,\"walk_ms\":%u,"
      "\"stop_ms\":%u,\"settle_ms\":%u,\"lease_ms\":%u,\"timeout_ms\":%u,"
      "\"walk_axis\":%s,\"minimum_walk_units\":%s,\"minimum_camera_motion_units\":%s,"
      "\"maximum_travel_units\":%s,\"stationary_tolerance_units\":%s}}",
      verdict, LabPhaseName(r.phase), LabReasonName(r.reason),
      static_cast<unsigned long long>(r.run), r.blockers,
      r.polls[0], r.polls[1], r.polls[2], r.observations[0], r.observations[1],
      r.observations[2], Number(r.walk_distance).c_str(), Number(r.camera_distance).c_str(),
      Number(r.stop_drift).c_str(), r.limits.idle_ms, r.limits.walk_ms, r.limits.stop_ms,
      r.limits.settle_ms, r.limits.lease_ms, r.limits.timeout_ms,
      Number(r.limits.walk_axis).c_str(), Number(r.limits.minimum_walk).c_str(),
      Number(r.limits.minimum_camera_motion).c_str(), Number(r.limits.maximum_travel).c_str(),
      Number(r.limits.stationary_tolerance).c_str());
  if (count < 0 || static_cast<std::size_t>(count) >= sizeof(text))
    return "{\"scenario_verdict\":\"FAIL\",\"runtime_verified\":false,\"reason\":\"report_error\"}";
  return text;
}

}  // namespace dsvr
