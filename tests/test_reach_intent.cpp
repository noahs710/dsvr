// Reach-to-interact bridge tests: no native prompt flag is bound, so the tests
// are about which forward hand movements are NOT a deliberate reach.
#include "../src/core/reach_intent_math.h"

#include <cmath>
#include <cstdio>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
  if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

using dsvr::ReachIntentOutput;
using dsvr::ReachIntentParams;
using dsvr::ReachIntentTrigger;

// Feed `frames` frames 10 ms apart with the hand at `offset` metres ahead of the
// head, moving at `forward`/`speed` m/s, other hand at `other` m/s.
ReachIntentOutput hold(ReachIntentTrigger& tr, long long& t, float offset,
                       float forward, float speed, float other,
                       const ReachIntentParams& p, int frames) {
  ReachIntentOutput out{};
  for (int i = 0; i < frames; ++i) {
    out = tr.Feed(offset, forward, speed, other, t, p);
    t += 10;
  }
  return out;
}

int count_presses(const ReachIntentOutput& o) { return o.pressed ? 1 : 0; }

// Both hands at rest for longer than the settle window.
void RestThenThrustFires() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 1000;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  check(tr.Armed(), "resting hands arm the gesture");
  const auto fired = hold(tr, t, 1.0f, 2.0f, 2.4f, 0.2f, p, 1);
  check(fired.pressed && fired.press_ms == p.press_ms,
        "a rested forward thrust taps interact once");
}

// A hand that was already moving when tracking started is not a reach: the
// gesture needs a rest it can be measured against.
void NoRestFirstRefuses() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 500;
  int presses = 0;
  for (int i = 0; i < 60; ++i) presses += count_presses(hold(tr, t, 1.0f, 2.0f, 2.4f, 0.2f, p, 1));
  check(presses == 0, "a hand already flying when tracking starts never interacts");
}

// Fast is not the same as forward: a sweep across the body is a swing.
void SidewaysSweepRefuses() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 1000;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  const auto out = hold(tr, t, 1.0f, 0.5f, 3.0f, 0.2f, p, 1);
  check(!out.pressed, "a fast sideways sweep does not interact");
}

// The envelope is what makes it a reach rather than a twitch: the hand has to
// arrive in the space in front of the head, not merely move toward it.
void EnvelopeBounds() {
  ReachIntentTrigger a, b;
  ReachIntentParams p;
  long long ta = 1000, tb = 1000;
  hold(a, ta, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  hold(b, tb, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  check(!hold(a, ta, 0.3f, 2.0f, 2.4f, 0.2f, p, 1).pressed,
        "a hand that never leaves the body does not interact");
  check(!hold(b, tb, 3.5f, 2.0f, 2.4f, 0.2f, p, 1).pressed,
        "a hand past the reach envelope does not interact");
  ReachIntentParams wide;
  wide.forward_max_m = 5.0f;
  ReachIntentTrigger c;
  long long tc = 1000;
  hold(c, tc, 0.2f, 0.0f, 0.0f, 0.0f, wide, 40);
  check(hold(c, tc, 3.5f, 2.0f, 2.4f, 0.2f, wide, 1).pressed,
        "widening the envelope lets the same reach through");
}

// A running gait, a clap or a two-handed swing moves both hands together; the
// free hand is the one that has to stay still.
void OtherHandBusyRefuses() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 1000;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  check(!hold(tr, t, 1.0f, 2.0f, 2.4f, 1.9f, p, 1).pressed,
        "a thrust with the other hand moving does not interact");
}

// Waving at the world must not repeat-interact: one tap per gesture, and the
// hand has to come back to rest before the next one.
void OneTapPerGesture() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 1000;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  int presses = 0;
  presses += count_presses(hold(tr, t, 1.0f, 2.0f, 2.4f, 0.2f, p, 1));
  presses += count_presses(hold(tr, t, 1.2f, 2.0f, 2.4f, 0.2f, p, 20));
  check(presses == 1, "one thrust is one tap however long it lasts");
  presses = 0;
  presses += count_presses(hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 10));
  presses += count_presses(hold(tr, t, 1.0f, 2.0f, 2.4f, 0.2f, p, 1));
  check(presses == 0, "a second thrust inside the repeat interval does not interact");
  presses = 0;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 100);
  presses += count_presses(hold(tr, t, 1.0f, 2.0f, 2.4f, 0.2f, p, 1));
  check(presses == 1, "after rest and the repeat interval the gesture works again");
}

// Body locomotion is not a thrust: a hand carried along at walking speed stays
// under the rest band, and the caller feeds head-relative speeds precisely so
// that walking the room or riding a lift cannot reach anything.
void BodyMotionIsNotAThrust() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 1000;
  int presses = 0;
  for (int i = 0; i < 80; ++i)
    presses += count_presses(hold(tr, t, 1.4f, 0.7f, 0.75f, 0.6f, p, 1));
  check(presses == 0, "walking the room never interacts");
}

// Fail-closed: a clock that steps behind the last sample, non-finite or negative
// inputs, and an impossible parameter set all reset and emit nothing.
void ClockAndNumbers() {
  ReachIntentTrigger tr;
  ReachIntentParams p;
  long long t = 5000;
  hold(tr, t, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  long long back = t - 500;
  check(!hold(tr, back, 1.0f, 2.0f, 2.4f, 0.2f, p, 1).pressed,
        "a clock that steps backwards never interacts");
  ReachIntentTrigger u;
  long long tu = 1000;
  hold(u, tu, 0.2f, 0.0f, 0.0f, 0.0f, p, 40);
  check(!u.Feed(1.0f, 2.0f, std::nanf(""), 0.2f, tu, p).pressed,
        "a non-finite speed does not interact");
  check(!u.Feed(-1.0f, 2.0f, 2.4f, 0.2f, tu + 10, p).pressed,
        "a negative speed does not interact");
  ReachIntentParams broken;
  broken.forward_max_m = broken.forward_min_m;
  ReachIntentTrigger v;
  long long tv = 1000;
  hold(v, tv, 0.2f, 0.0f, 0.0f, 0.0f, broken, 40);
  check(!hold(v, tv, 1.0f, 2.0f, 2.4f, 0.2f, broken, 1).pressed,
        "an envelope with no depth refuses the gesture");
  ReachIntentParams inverted;
  inverted.quiet_max = inverted.speed_min;
  ReachIntentTrigger w;
  long long tw = 1000;
  hold(w, tw, 0.2f, 0.0f, 0.0f, 0.0f, inverted, 40);
  check(!hold(w, tw, 1.0f, 2.0f, 2.4f, 0.2f, inverted, 1).pressed,
        "a rest band that touches the thrust band refuses the gesture");
}

void IdenticalInputsIdenticalOutput() {
  ReachIntentTrigger a, b;
  ReachIntentParams p;
  long long ta = 1000, tb = 1000;
  bool same = true;
  for (int i = 0; i < 120; ++i) {
    const float offset = 0.2f + 1.2f * static_cast<float>(i % 30) / 30.0f;
    const float speed = (i % 30) < 12 ? 2.4f : 0.1f;
    const float forward = (i % 30) < 12 ? 2.0f : 0.0f;
    const auto oa = a.Feed(offset, forward, speed, 0.1f, ta, p);
    ta += 10;
    const auto ob = b.Feed(offset, forward, speed, 0.1f, tb, p);
    tb += 10;
    if (oa.pressed != ob.pressed || oa.press_ms != ob.press_ms) same = false;
  }
  check(same, "the same hand history always produces the same interaction");
}

}  // namespace

int main() {
  RestThenThrustFires();
  NoRestFirstRefuses();
  SidewaysSweepRefuses();
  EnvelopeBounds();
  OtherHandBusyRefuses();
  OneTapPerGesture();
  BodyMotionIsNotAThrust();
  ClockAndNumbers();
  IdenticalInputsIdenticalOutput();
  if (g_fail) {
    std::printf("%d reach_intent test(s) failed\n", g_fail);
    return 1;
  }
  std::printf("reach_intent tests passed\n");
  return 0;
}
