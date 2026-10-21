#include "../src/core/stereo_gate.h"

#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  // Default: stereo not requested, mono is the presentation.
  CHECK(DecideStereo(false, true, true) == StereoDecision::MonoDefault);
  CHECK(DecideStereo(false, false, false) == StereoDecision::MonoDefault);
  // Requested but no native eye renderer bound: refuse, never fake it.
  // This is the production state until a native two-eye renderer exists;
  // a caller must continue mono on this decision, not duplicate the image.
  CHECK(DecideStereo(true, true, false) == StereoDecision::RefuseNoRenderer);
  CHECK(DecideStereo(true, false, false) == StereoDecision::RefuseNoRenderer);
  // Renderer bound but no live session yet: wait in mono, no refusal spam.
  CHECK(DecideStereo(true, false, true) == StereoDecision::MonoDefault);
  // Only a bound renderer with a live session may attempt stereo. The
  // Begin/End contract itself (one pair, token and thread checks) is
  // covered by xr_stereo_frame; this gate is what production consults
  // before calling it.
  CHECK(DecideStereo(true, true, true) == StereoDecision::AttemptStereo);
  std::puts("PASS stereo caller gate");
  return 0;
}
