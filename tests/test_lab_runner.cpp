#include <cstdio>

#include "../src/core/lab_runner.h"

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; \
} } while (0)

int main() {
  using namespace dsvr;
  LabInitialize(true);
  CHECK(LabStatus().phase == LabPhase::Dormant);
  CHECK(LabRequestStart());
  CHECK(!LabRequestStart());  // bounded command slot; cannot reset an active run
  LabPump();
  const auto blocked = LabStatus();
  CHECK(blocked.phase == LabPhase::Blocked);
#ifdef DSVR_ENABLE_LAB
  CHECK(!(blocked.blockers & kLabDisabled));
#else
  CHECK(blocked.blockers & kLabDisabled);
#endif
  CHECK(blocked.blockers & kLabUnauthorized);
  CHECK(blocked.blockers & kLabOfflineUnknown);
  CHECK(blocked.blockers & kLabIsolationUnknown);
  CHECK(blocked.blockers & kLabGameplayUnknown);
  CHECK(blocked.blockers & kLabSafeSceneUnknown);
  CHECK(!(blocked.blockers & kLabExecutableUnknown));
  CHECK(!LabWantsObservation());
  CHECK(VrTraceState() == TraceCaptureState::Idle);  // no file/capture was started
  VirtualPadState out{};
  out.buttons = kPadB;
  CHECK(!LabPollPad({}, {}, out));
  CHECK(out.buttons == kPadB);  // disabled adapter cannot touch ordinary input
  CHECK(LabRequestStart());
  LabRequestAbort();  // abort wins over a queued start
  LabPump();
  CHECK(LabStatus().run == blocked.run);
  LabInitialize(false);
  CHECK(LabRequestStart());
  LabPump();
  CHECK(LabStatus().blockers & kLabExecutableUnknown);
  CHECK(VrTraceState() == TraceCaptureState::Idle);
  std::puts("test_lab_runner: PASS");
  return 0;
}
