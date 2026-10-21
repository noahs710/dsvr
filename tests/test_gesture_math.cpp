#include "../src/core/gesture_math.h"

#include <cmath>
#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  using namespace dsvr;

  const float dt = 1.0f / 90.0f;
  const Vec3 blade{0, 0, -1};  // blade points along -Z

  // Slow wave: never fires.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    bool fired = false;
    for (int i = 0; i < 90; ++i) {
      p.x += 0.5f * dt;  // 0.5 m/s
      fired |= t.Feed(p, blade, dt).kind != GestureKind::None;
    }
    check(!fired);
  }

  // Fast sideways swing: fires Slash exactly once, re-arms on slowdown.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    int slashes = 0;
    for (int i = 0; i < 45; ++i) {  // accelerate to ~3 m/s sideways
      p.x += 3.0f * dt;
      if (t.Feed(p, blade, dt).kind == GestureKind::Slash) ++slashes;
    }
    check(slashes == 1);
    check(t.PeakSpeed() > 2.0f);
    for (int i = 0; i < 45; ++i) {  // held fast: still no second fire
      p.x += 3.0f * dt;
      if (t.Feed(p, blade, dt).kind != GestureKind::None) ++slashes;
    }
    check(slashes == 1);
    Vec3 still = p;
    for (int i = 0; i < 30; ++i) t.Feed(still, blade, dt);  // decelerate
    check(t.Armed());
    for (int i = 0; i < 45; ++i) {  // second swing fires again
      p.x -= 3.0f * dt;
      if (t.Feed(p, blade, dt).kind == GestureKind::Slash) ++slashes;
    }
    check(slashes == 2);
  }

  // Forward stab along the blade: Thrust, at the lower thrust threshold.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    bool thrust = false;
    for (int i = 0; i < 45; ++i) {
      p.z -= 1.2f * dt;  // 1.2 m/s along blade, below swing_speed_min
      thrust |= t.Feed(p, blade, dt).kind == GestureKind::Thrust;
    }
    check(thrust);
  }

  // Tracking loss (bad dt / NaN) never fires and resets velocity.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    check(t.Feed(p, blade, 0.0f).kind == GestureKind::None);
    Vec3 nan{NAN, 0, 0};
    check(t.Feed(nan, blade, dt).kind == GestureKind::None);
    check(t.Speed() == 0.0f);
  }

  // (a) Thrust below thrust_speed_min never fires.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    bool fired = false;
    for (int i = 0; i < 90; ++i) {
      p.z -= 0.5f * dt;  // 0.5 m/s along blade, below 1.0 thrust min
      fired |= t.Feed(p, blade, dt).kind != GestureKind::None;
    }
    check(!fired);
  }

  // (b) Reset() mid-swing clears speed and re-arms.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    int slashes = 0;
    for (int i = 0; i < 45; ++i) {
      p.x += 3.0f * dt;
      if (t.Feed(p, blade, dt).kind == GestureKind::Slash) ++slashes;
    }
    check(slashes == 1);
    t.Reset();
    check(t.Speed() == 0.0f);
    check(t.Armed());
    check(t.PeakSpeed() == 0.0f);
    slashes = 0;
    for (int i = 0; i < 45; ++i) {
      p.x += 3.0f * dt;
      if (t.Feed(p, blade, dt).kind == GestureKind::Slash) ++slashes;
    }
    check(slashes == 1);
  }

  // (c) Flipped blade {0,0,1} still classifies thrust by absolute alignment.
  {
    GestureTracker t;
    Vec3 flipped{0, 0, 1};
    Vec3 p{0, 0, 0};
    bool thrust = false;
    for (int i = 0; i < 45; ++i) {
      p.z += 1.2f * dt;  // 1.2 m/s along flipped blade
      thrust |= t.Feed(p, flipped, dt).kind == GestureKind::Thrust;
    }
    check(thrust);
  }

  // Tracking teleport (pose relocates metres in one frame) never fires:
  // the live log showed a 326811 m/s phantom slash from exactly this.
  {
    GestureTracker t;
    Vec3 p{0, 0, 0};
    t.Feed(p, blade, dt);
    Vec3 teleported{5.0f, 0, 0};
    check(t.Feed(teleported, blade, dt).kind == GestureKind::None);
    check(t.Speed() == 0.0f);
    bool fired = false;
    for (int i = 0; i < 10; ++i)  // normal motion resumes cleanly
      fired |= t.Feed(teleported, blade, dt).kind != GestureKind::None;
    check(!fired);
  }

  std::printf("gesture math: %d failures\n", failures);
  return failures ? 1 : 0;
}
