// Freshness definitions for AFR (T4), recorded before implementation:
// - Frame sequence counts submitted stereo pairs starting at 0.
// - The eye submitted on a frame has age 0 on that frame; an eye last
//   submitted N frames ago has age N. Age is measured in frames, never
//   milliseconds, so it stays meaningful across 72/90 Hz runtimes.
// - Under alternating submission each eye is refreshed every second
//   frame, so at submission the stale eye has age 1 (it was drawn on
//   the previous frame). Comfort claims need the headset run; these
//   numbers only define what the metrics must report.
#include "../src/core/afr_policy.h"

#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  CHECK(DecideAfr(false, false) == AfrDecision::Disabled);
  CHECK(DecideAfr(false, true) == AfrDecision::Disabled);
  // AFR on a mono image is meaningless and must refuse.
  CHECK(DecideAfr(true, false) == AfrDecision::RefuseStereoInactive);
  CHECK(DecideAfr(true, true) == AfrDecision::Alternate);
  // Alternation: even frames draw eye 0, odd frames draw eye 1.
  CHECK(AfrEyeForFrame(0) == 0 && AfrEyeForFrame(1) == 1);
  CHECK(AfrEyeForFrame(2) == 0 && AfrEyeForFrame(3) == 1);
  CHECK(AfrEyeForFrame(100) == 0 && AfrEyeForFrame(101) == 1);
  // Freshness accounting per the definitions above.
  CHECK(AfrEyeAgeFrames(10, 10) == 0);
  CHECK(AfrEyeAgeFrames(10, 9) == 1);
  CHECK(AfrEyeAgeFrames(10, 8) == 2);
  CHECK(AfrEyeAgeFrames(9, 10) == 0);  // future stamps clamp, never wrap
  std::puts("PASS afr policy");
  return 0;
}
