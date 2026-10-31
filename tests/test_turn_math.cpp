#include "../src/core/turn_math.h"

#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
  using namespace dsvr;
  const float dt = 1.0f / 90.0f;
  const float snap_rad = 45.0f * 0.0174532925f;

  // Centred / deadzoned stick never turns.
  {
    TurnTracker t;
    check(t.Feed(0.0f, dt) == 0.0f);
    check(t.Feed(0.2f, dt) == 0.0f);
  }

  // Default params are smooth turning (snap off): a held stick yields a
  // small per-frame delta, centred stick yields zero.
    {
    TurnTracker t;
    check(t.Feed(0.0f, dt) == 0.0f);
    float first = t.Feed(1.0f, dt);
    check(first > 0.0f);
    check(near(first, 360.0f * 0.0174532925f * dt));
    check(t.Feed(0.0f, dt) == 0.0f);
    }

  // Snap: one fire per push, silent while held, re-arms on release.
  {
    TurnParams snap;
    snap.snap_enabled = true;
    snap.smooth_enabled = false;
    TurnTracker t(snap);
    check(near(t.Feed(0.9f, dt), snap_rad));
    check(t.Feed(0.9f, dt) == 0.0f);
    check(t.Feed(0.9f, dt) == 0.0f);
    check(t.Feed(0.4f, dt) == 0.0f);   // above re-arm: still latched
    check(t.Feed(0.9f, dt) == 0.0f);
    check(t.Feed(0.1f, dt) == 0.0f);   // released: re-arms silently
    check(near(t.Feed(-0.9f, dt), -snap_rad));  // left fires negative
  }

  // Smooth: integrates at configured speed, zero at deadzone edge.
  {
    TurnParams p;
    p.snap_enabled = false;
    p.smooth_enabled = true;
    p.smooth_speed_dps = 120.0f;
    TurnTracker t(p);
    check(t.Feed(0.25f, dt) == 0.0f);
    float total = 0.0f;
    for (int i = 0; i < 45; ++i) total += t.Feed(1.0f, dt);  // 0.5 s
    check(near(total, 60.0f * 0.0174532925f));
  }

  // Bad frames change nothing and cannot fire.
  {
    TurnParams snap;
    snap.snap_enabled = true;
    snap.smooth_enabled = false;
    TurnTracker t(snap);
      check(t.Feed(0.9f, 0.0f) == 0.0f);
    check(t.Feed(NAN, dt) == 0.0f);
    check(near(t.Feed(0.9f, dt), snap_rad));  // still armed afterwards
  }

  // Both modes on: snap wins, no smooth drift while latched.
  {
    TurnParams p;
    p.snap_enabled = true;
    p.smooth_enabled = true;
    TurnTracker t(p);
    check(near(t.Feed(0.9f, dt), snap_rad));
    check(t.Feed(0.9f, dt) == 0.0f);
  }

  // Boundary: exactly 0.5 fires, 0.49 does not.
  {
    TurnParams snap;
    snap.snap_enabled = true;
      snap.smooth_enabled = false;
      TurnTracker t(snap);
    check(near(t.Feed(0.5f, dt), snap_rad));
    }
  {
    TurnParams snap;
    snap.snap_enabled = true;
    snap.smooth_enabled = false;
    TurnTracker t(snap);
    check(t.Feed(0.49f, dt) == 0.0f);
  }

  // Latch edge: holding 0.3 exactly stays latched, 0.29 re-arms.
  {
    TurnParams snap;
    snap.snap_enabled = true;
    snap.smooth_enabled = false;
    TurnTracker t(snap);
    check(near(t.Feed(0.9f, dt), snap_rad));
    check(t.SnapLatched());
    check(t.Feed(0.3f, dt) == 0.0f);
    check(t.SnapLatched());
    check(t.Feed(0.29f, dt) == 0.0f);
    check(!t.SnapLatched());
    check(near(t.Feed(0.9f, dt), snap_rad));
  }

  // snap_degrees = 30 emits 30 deg in radians.
  {
    TurnParams p;
    p.snap_enabled = true;
    p.smooth_enabled = false;
    p.snap_degrees = 30.0f;
    TurnTracker t(p);
    check(near(t.Feed(0.9f, dt), 30.0f * 0.0174532925f));
  }

  // Smooth symmetry: -0.625 is the negative of +0.625 for the same dt.
  {
    TurnParams p;
    p.snap_enabled = false;
    p.smooth_enabled = true;
    p.smooth_speed_dps = 120.0f;
    TurnTracker tp(p);
    TurnTracker tn(p);
    float pos = tp.Feed(0.625f, dt);
    float neg = tn.Feed(-0.625f, dt);
    check(pos != 0.0f);
    check(near(neg, -pos));
  }

  std::printf("turn math: %d failures\n", failures);
  return failures ? 1 : 0;
}
