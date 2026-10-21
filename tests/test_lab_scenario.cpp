// The production scenario state machine, with explicit observation fixtures.
// No hooks, game launch, real input or save operations.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../src/core/lab_scenario.h"

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; \
} } while (0)

dsvr::LabSafety Ready() {
  return {true, true, true, true, true};
}

dsvr::LabObservation Sample(std::uint64_t now, std::uint64_t sequence = 1) {
  dsvr::LabObservation out{};
  out.at_ms = now;
  out.sequence = sequence;
  out.player = 42;
  out.scene_epoch = 1;
  out.gameplay_verified = true;
  out.safe_scene_verified = true;
  out.root_valid = true;
  out.camera_valid = true;
  out.camera = {0, 1.65f, 0};
  return out;
}

int main(int argc, char* argv[]) {
  using namespace dsvr;
  LabScenario blocked;
  CHECK(!blocked.Start({}, Sample(1000), 1000));
  CHECK(blocked.Result().phase == LabPhase::Blocked);
  CHECK(blocked.Result().blockers & kLabDisabled);
  CHECK(blocked.Result().blockers & kLabIsolationUnknown);
  VirtualPadState out{};
  out.thumb_ly = 321;
  CHECK(!blocked.Poll({}, {}, 1000, out));
  CHECK(out.thumb_ly == 321);  // inactive lab cannot alter ordinary input
  for (unsigned gate = 0; gate < 5; ++gate) {
    auto safety = Ready();
    if (gate == 0) safety.enabled = false;
    if (gate == 1) safety.authorized = false;
    if (gate == 2) safety.pinned_executable = false;
    if (gate == 3) safety.offline_verified = false;
    if (gate == 4) safety.isolated_save_verified = false;
    LabScenario scenario;
    CHECK(!scenario.Start(safety, Sample(1000), 1000));
  }
  auto unknown = Sample(1000);
  unknown.safe_scene_verified = false;
  CHECK(!blocked.Start(Ready(), unknown, 1000));
  CHECK(blocked.Result().blockers & kLabSafeSceneUnknown);
  unknown = Sample(1000);
  unknown.gameplay_verified = false;
  CHECK(!blocked.Start(Ready(), unknown, 1000));
  CHECK(blocked.Result().blockers & kLabGameplayUnknown);
  CHECK(!blocked.Start(Ready(), Sample(800), 1000));

  // Commands alone must not count as completed phases or native movement.
  LabScenario no_polls;
  CHECK(no_polls.Start(Ready(), Sample(1000), 1000));
  no_polls.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  for (std::uint64_t now = 1050; now <= 2000; now += 50)
    no_polls.Observe(Ready(), Sample(now, (now - 1000) / 50 + 1), now,
                     TraceCaptureState::Recording);
  CHECK(no_polls.Result().phase == LabPhase::Failed);
  CHECK(no_polls.Result().reason == LabReason::InputNotPolled);

  LabScenario scenario;
  CHECK(scenario.Start(Ready(), Sample(1000), 1000));
  CHECK(scenario.Result().phase == LabPhase::AwaitCapture);
  CHECK(!scenario.Poll({}, {}, 1000, out));  // cannot move before capture starts
  CHECK(!scenario.Start(Ready(), Sample(1000), 1000));  // no active-run reset
  std::uint64_t sequence = 1;
  float root_x = 0;
  for (std::uint64_t now = 1000; now <= 3500; now += 50) {
    if (now > 2000 && now <= 2500) root_x += .02f;
    auto observation = Sample(now, sequence++);
    observation.root.x = root_x;
    observation.camera.x = root_x;
    scenario.Observe(Ready(), observation, now, TraceCaptureState::Recording);
    CHECK(scenario.Poll({}, {}, now, out));  // no controller sample is needed
    if (now >= 2000 && now < 2500) CHECK(out.thumb_ly == 11468);
    else CHECK(out.thumb_ly == 0);
    CHECK(out.buttons == 0 && out.thumb_rx == 0 && out.right_trigger == 0);
  }
  const auto measured = scenario.Result();
  CHECK(measured.phase == LabPhase::AwaitClose);
  CHECK(measured.polls[0] > 0 && measured.polls[1] > 0 && measured.polls[2] > 0);
  CHECK(measured.observations[0] >= 2 && measured.observations[1] >= 2 &&
        measured.observations[2] >= 2);
  CHECK(std::fabs(measured.walk_distance - .2f) < .0001f);
  CHECK(std::fabs(measured.camera_distance - .2f) < .0001f);
  // A finished timeline is not success until the actual writer closes.
  auto closing = Sample(3550, sequence++);
  closing.root.x = closing.camera.x = root_x;
  scenario.Observe(Ready(), closing, 3550, TraceCaptureState::Closing);
  CHECK(scenario.Result().phase == LabPhase::AwaitClose);
  auto final_observation = Sample(3600, sequence++);
  final_observation.root.x = root_x;
  final_observation.camera.x = root_x;
  scenario.Observe(Ready(), final_observation, 3600, TraceCaptureState::Complete);
  CHECK(scenario.Result().phase == LabPhase::Passed);
  CHECK(scenario.Poll({}, {}, 3600, out) && out.thumb_ly == 0);  // final neutral
  CHECK(!scenario.Poll({}, {}, 3601, out));  // release control to the user

  // Serving walk input with an unchanged native root must fail.
  LabScenario unmoved;
  CHECK(unmoved.Start(Ready(), Sample(1000), 1000));
  for (std::uint64_t now = 1000; now <= 2500; now += 50) {
    unmoved.Observe(Ready(), Sample(now, (now - 1000) / 50 + 1), now,
                    TraceCaptureState::Recording);
    (void)unmoved.Poll({}, {}, now, out);
  }
  CHECK(unmoved.Result().phase == LabPhase::Failed);
  CHECK(unmoved.Result().reason == LabReason::RootDidNotMove);

  LabScenario frozen_camera;
  CHECK(frozen_camera.Start(Ready(), Sample(1000), 1000));
  for (std::uint64_t now = 1000; now <= 2500; now += 50) {
    auto observation = Sample(now, (now - 1000) / 50 + 1);
    if (now > 2000) observation.root.x = static_cast<float>(now - 2000) * .0004f;
    frozen_camera.Observe(Ready(), observation, now, TraceCaptureState::Recording);
    (void)frozen_camera.Poll({}, {}, now, out);
  }
  CHECK(frozen_camera.Result().phase == LabPhase::Failed);
  CHECK(frozen_camera.Result().reason == LabReason::CameraDidNotMove);

  // Native polls must release walk at its deadline even if camera updates stop.
  LabScenario delayed_camera;
  CHECK(delayed_camera.Start(Ready(), Sample(1000), 1000));
  for (std::uint64_t now = 1000; now <= 2450; now += 50) {
    auto observation = Sample(now, (now - 1000) / 50 + 1);
    if (now > 2000) observation.root.x = observation.camera.x =
        static_cast<float>(now - 2000) * .0004f;
    delayed_camera.Observe(Ready(), observation, now, TraceCaptureState::Recording);
    CHECK(delayed_camera.Poll({}, {}, now, out));
  }
  CHECK(out.thumb_ly == 11468);
  CHECK(delayed_camera.Poll({}, {}, 2500, out));
  CHECK(out.thumb_ly == 0);
  CHECK(delayed_camera.Result().phase == LabPhase::Aborted);
  CHECK(delayed_camera.Result().reason == LabReason::PhaseDeadline);

  LabLimits bad_limits{};
  bad_limits.walk_axis = std::numeric_limits<float>::infinity();
  LabScenario invalid_limits(bad_limits);
  CHECK(!invalid_limits.Start(Ready(), Sample(1000), 1000));
  CHECK(invalid_limits.Result().blockers & kLabLimitsInvalid);

  LabScenario replayed;
  CHECK(replayed.Start(Ready(), Sample(1000), 1000));
  replayed.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  replayed.Observe(Ready(), Sample(1101), 1101, TraceCaptureState::Recording);
  CHECK(replayed.Result().reason == LabReason::StaleObservation);

  // A newly numbered sample cannot hide a gap in native observation coverage.
  LabScenario coverage_gap;
  CHECK(coverage_gap.Start(Ready(), Sample(1000), 1000));
  coverage_gap.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  coverage_gap.Observe(Ready(), Sample(1200, 2), 1200, TraceCaptureState::Recording);
  CHECK(coverage_gap.Result().phase == LabPhase::Aborted);
  CHECK(coverage_gap.Result().reason == LabReason::StaleObservation);

  LabScenario backwards;
  CHECK(backwards.Start(Ready(), Sample(1000), 1000));
  backwards.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  CHECK(backwards.Poll({}, {}, 999, out) && out.thumb_ly == 0);
  CHECK(backwards.Result().reason == LabReason::ClockReversed);

  LabScenario early_capture;
  CHECK(early_capture.Start(Ready(), Sample(1000), 1000));
  early_capture.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  early_capture.Observe(Ready(), Sample(1050, 2), 1050, TraceCaptureState::Complete);
  CHECK(early_capture.Result().reason == LabReason::CaptureEndedEarly);

  LabScenario drift;
  CHECK(drift.Start(Ready(), Sample(1000), 1000));
  for (std::uint64_t now = 1000; now <= 3500; now += 50) {
    auto observation = Sample(now, (now - 1000) / 50 + 1);
    if (now > 2000) observation.root.x = observation.camera.x =
        static_cast<float>((now < 2500 ? now : 2500) - 2000) * .0004f;
    drift.Observe(Ready(), observation, now, TraceCaptureState::Recording);
    (void)drift.Poll({}, {}, now, out);
  }
  CHECK(drift.Result().phase == LabPhase::AwaitClose);
  auto late_drift = Sample(3550, 52);
  late_drift.root.x = late_drift.camera.x = .25f;
  drift.Observe(Ready(), late_drift, 3550, TraceCaptureState::Complete);
  CHECK(drift.Result().phase == LabPhase::Failed);
  CHECK(drift.Result().reason == LabReason::RootDidNotStop);

  // Safety failures always yield neutral lab controls, never cached walking.
  for (unsigned failure = 0; failure < 9; ++failure) {
    LabScenario unsafe;
    CHECK(unsafe.Start(Ready(), Sample(1000), 1000));
    unsafe.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
    CHECK(unsafe.Poll({}, {}, 1000, out));
    auto observation = Sample(1050, 2);
    auto safety = Ready();
    auto capture = TraceCaptureState::Recording;
    if (failure == 0) observation.player = 43;
    if (failure == 1) observation.scene_epoch = 2;
    if (failure == 2) observation.gameplay_verified = false;
    if (failure == 3) observation.safe_scene_verified = false;
    if (failure == 4) observation.root.x = 2;
    if (failure == 5) observation.root.x = std::numeric_limits<float>::quiet_NaN();
    if (failure == 6) safety.offline_verified = false;
    if (failure == 7) capture = TraceCaptureState::Dropped;
    if (failure == 8) observation.user_intervened = true;
    unsafe.Observe(safety, observation, 1050, capture);
    CHECK(unsafe.Result().phase == LabPhase::Aborted ||
          unsafe.Result().phase == LabPhase::Failed);
    CHECK(unsafe.Poll({}, {}, 1050, out) && out.thumb_ly == 0);
    CHECK(!unsafe.Poll({}, {}, 1051, out));
  }
  LabScenario user_input;
  CHECK(user_input.Start(Ready(), Sample(1000), 1000));
  user_input.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  VirtualPadState physical{};
  physical.buttons = kPadA;
  CHECK(user_input.Poll(physical, {}, 1000, out) && out.buttons == 0);
  CHECK(user_input.Result().reason == LabReason::UserIntervened);

  LabScenario stale;
  CHECK(stale.Start(Ready(), Sample(1000), 1000));
  stale.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  CHECK(stale.Poll({}, {}, 1101, out) && out.thumb_ly == 0);
  CHECK(stale.Result().reason == LabReason::StaleObservation);

  LabScenario timeout;
  CHECK(timeout.Start(Ready(), Sample(1000), 1000));
  timeout.Observe(Ready(), Sample(6001, 2), 6001, TraceCaptureState::Idle);
  CHECK(timeout.Result().phase == LabPhase::Failed);
  CHECK(timeout.Result().reason == LabReason::Timeout);

  LabScenario abort;
  CHECK(abort.Start(Ready(), Sample(1000), 1000));
  abort.Observe(Ready(), Sample(1000), 1000, TraceCaptureState::Recording);
  abort.Abort(LabReason::RequestedAbort);
  CHECK(abort.Result().phase == LabPhase::Aborted);
  CHECK(abort.Poll({}, {}, 1000, out) && out.thumb_ly == 0);
  if (argc == 2 && std::strcmp(argv[1], "--report") == 0)
    std::puts(LabResultJson(scenario.Result()).c_str());
  else std::puts("test_lab_scenario: PASS");
  return 0;
}
