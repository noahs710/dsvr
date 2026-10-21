// Offline independent-heading tests. CHECK executes even under NDEBUG.
#include "../src/core/vr_heading.h"
#include "../src/core/turn_math.h"
#include "../src/core/pad_math.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                   \
    ++g_checks;                                                          \
    if (!(cond)) {                                                       \
      ++g_failures;                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                    \
  } while (0)

using namespace dsvr;

static bool Near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}
static bool VNear(Vec3 v, float x, float y, float z) {
  return Near(v.x, x) && Near(v.y, y) && Near(v.z, z);
}

int main() {
  constexpr float kQuarterTurn = 1.57079632679f;
  // Native heading is captured once, not followed as animation faces elsewhere.
  {
    VrHeading heading;
    CHECK(!heading.IsReady());
    CHECK(heading.SeedOnce({2, 7, 0}));
    CHECK(heading.IsReady());
    Basis b = heading.BodyBasis();
    CHECK(VNear(b.r, 0, 0, -1));
    CHECK(VNear(b.u, 0, 1, 0));
    CHECK(VNear(b.f, 1, 0, 0));
    CHECK(!heading.SeedOnce({0, 0, -1}));
    CHECK(VNear(heading.BodyBasis().f, 1, 0, 0));
  }

  // Recenter is explicit; rejected captures do not disturb a valid heading.
  {
    VrHeading heading;
    CHECK(!heading.SeedOnce({0, 1, 0}));
    CHECK(!heading.IsReady());
    CHECK(heading.SeedOnce({0, 0, 1}));
    CHECK(heading.Recenter({-3, -4, 0}));
    CHECK(VNear(heading.BodyBasis().f, -1, 0, 0));
    CHECK(!heading.Recenter({0, -1, 0}));
    CHECK(!heading.Recenter({std::numeric_limits<float>::infinity(), 0, 1}));
    CHECK(!heading.Recenter({0, std::numeric_limits<float>::quiet_NaN(), 1}));
    CHECK(VNear(heading.BodyBasis().f, -1, 0, 0));
    heading.Reset();
    CHECK(!heading.IsReady());
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
    CHECK(heading.SeedOnce({0, 0, -1}));
    CHECK(VNear(heading.BodyBasis().f, 0, 0, -1));
  }

  // TurnTracker's positive-right deltas rotate +Z toward +X, without an
  // extra XR-yaw sign flip. Only explicit deltas can accumulate heading.
  // Default tracker is smooth: one full-deflection frame is a small step.
    {
    VrHeading heading;
    CHECK(!heading.ApplyTurn(kQuarterTurn));
    CHECK(heading.SeedOnce({0, 0, 1}));
    TurnParams smooth;
    smooth.snap_enabled = false;
    smooth.smooth_enabled = true;
    smooth.smooth_speed_dps = 240.0f;
    TurnTracker turn(smooth);
    const float step = turn.Feed(1.0f, 1.0f / 90.0f);
    CHECK(step > 0.0f);
    CHECK(heading.ApplyTurn(step));
    const float yaw = 240.0f * 0.0174532925f / 90.0f;
    CHECK(VNear(heading.BodyBasis().f, std::sin(yaw), 0, std::cos(yaw)));
    CHECK(heading.ApplyTurn(turn.Feed(1.0f, 1.0f / 90.0f)));
    CHECK(VNear(heading.BodyBasis().f, std::sin(2 * yaw), 0, std::cos(2 * yaw)));
    turn.Reset();
    CHECK(heading.ApplyTurn(turn.Feed(-1.0f, 1.0f / 90.0f)));
    CHECK(VNear(heading.BodyBasis().f, std::sin(yaw), 0, std::cos(yaw)));
    // Net heading is one smooth step (yaw); undo it exactly, then the
    // -90 deg turn lands on (-1, 0, 0) precisely.
    CHECK(heading.ApplyTurn(-yaw));
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
    CHECK(heading.ApplyTurn(-kQuarterTurn));
    CHECK(VNear(heading.BodyBasis().f, -1, 0, 0));
    CHECK(!heading.ApplyTurn(std::numeric_limits<float>::quiet_NaN()));
    CHECK(!heading.ApplyTurn(std::numeric_limits<float>::infinity()));
    CHECK(VNear(heading.BodyBasis().f, -1, 0, 0));
    CHECK(heading.Recenter({0, 0, 1}));
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
    heading.Reset();
    CHECK(!heading.ApplyTurn(kQuarterTurn));
    CHECK(heading.SeedOnce({0, 0, 1}));
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
  }

  // Literal +179 -> +181 degrees crosses the yaw branch cut smoothly;
  // reversing it returns to the same heading. Repeated tiny deltas avoid
  // precision loss from an unbounded float accumulator.
  {
    VrHeading heading;
    CHECK(heading.SeedOnce({0.017452406f, 0, -0.999847695f}));
    CHECK(heading.ApplyTurn(0.034906585f));
    CHECK(VNear(heading.BodyBasis().f, -0.017452406f, 0, -0.999847695f));
    CHECK(heading.ApplyTurn(-0.034906585f));
    CHECK(VNear(heading.BodyBasis().f, 0.017452406f, 0, -0.999847695f));
    CHECK(heading.Recenter({-0.017452406f, 0, -0.999847695f}));
    CHECK(heading.ApplyTurn(-0.034906585f));
    CHECK(VNear(heading.BodyBasis().f, 0.017452406f, 0, -0.999847695f));
    CHECK(heading.Recenter({0, 0, 1}));
    bool all_accepted = true;
    for (int i = 0; i < 100000; ++i)
      all_accepted = heading.ApplyTurn(0.0000157079632679f) && all_accepted;
    CHECK(all_accepted);
    CHECK(VNear(heading.BodyBasis().f, 1, 0, 0));
    CHECK(heading.Recenter({0, 0, 1}));
    all_accepted = true;
    for (int i = 0; i < 400; ++i)
      all_accepted = heading.ApplyTurn(kQuarterTurn) && all_accepted;
    CHECK(all_accepted);
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
    CHECK(heading.ApplyTurn(std::numeric_limits<float>::max()));
    Basis b = heading.BodyBasis();
    CHECK(Near(VLen(b.r), 1) && Near(VLen(b.f), 1));
    CHECK(Near(VDot(b.r, b.f), 0));
    CHECK(Near(VDot(b.r, VCross(b.u, b.f)), 1));
  }

  // Turn-in-place: head yaw inside the deadzone leaves the torso alone;
  // sustained yaw beyond it drags the body toward the head in bounded
  // steps, keeping a residual bend so shoulders never snap to the gaze.
  {
    CHECK(TorsoFollowDelta(0.0f) == 0.0f);
    CHECK(TorsoFollowDelta(0.3f) == 0.0f);
    CHECK(TorsoFollowDelta(-0.3f) == 0.0f);
    CHECK(TorsoFollowDelta(kTorsoFollowDeadzoneRad) == 0.0f);
    CHECK(TorsoFollowDelta(-kTorsoFollowDeadzoneRad) == 0.0f);
    // Small excess passes through unclamped; large excess caps the step.
    CHECK(Near(TorsoFollowDelta(kTorsoFollowDeadzoneRad + 0.1f), 0.1f, 1e-6f));
    CHECK(Near(TorsoFollowDelta(-kTorsoFollowDeadzoneRad - 0.1f), -0.1f, 1e-6f));
    CHECK(Near(TorsoFollowDelta(1.57079632679f), kTorsoFollowMaxStepRad, 1e-6f));
    CHECK(Near(TorsoFollowDelta(-1.57079632679f), -kTorsoFollowMaxStepRad, 1e-6f));
    // Degenerate inputs refuse instead of turning the avatar.
    CHECK(TorsoFollowDelta(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(TorsoFollowDelta(std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(TorsoFollowDelta(1.0f, -0.5f, 0.035f) == 0.0f);
    CHECK(TorsoFollowDelta(1.0f, 0.5f, 0.0f) == 0.0f);
    // A head held 1 rad right settles the torso 0.5 rad right: the body
    // follows but never overtakes the gaze.
    VrHeading heading;
    CHECK(heading.SeedOnce({0, 0, 1}));
    float body = 0.0f;
    for (int i = 0; i < 200; ++i) {
      const float relative = 1.0f - body;
      const float step = TorsoFollowDelta(relative);
      if (step == 0.0f) break;
      CHECK(heading.ApplyTurn(step));
      body += step;
      CHECK(body <= 1.0f);
    }
    CHECK(Near(body, 1.0f - kTorsoFollowDeadzoneRad, 0.01f));
    CHECK(VNear(heading.BodyBasis().f, std::sin(body), 0, std::cos(body)));
  }

  // Frame-rate independence: the same head yaw settles at the same rate
  // per second at 90 Hz and at 16 Hz; coarse frames take smaller steps,
  // never the full legacy cap. Zero/negative/NaN dt refuses.
  {
    CHECK(TorsoFollowDeltaDt(1.0f, 0.0f) == 0.0f);
    CHECK(TorsoFollowDeltaDt(1.0f, -0.01f) == 0.0f);
    CHECK(TorsoFollowDeltaDt(1.0f, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    auto settle = [](float dt) {
      float body = 0.0f, elapsed = 0.0f;
      for (int i = 0; i < 10000 && elapsed < 0.5f; ++i) {
        const float step = TorsoFollowDeltaDt(1.0f - body, dt);
        if (step == 0.0f) break;
        body += step;
        elapsed += dt;
      }
      return body;
    };
    const float fast = settle(1.0f / 90.0f), slow = settle(1.0f / 16.0f);
    CHECK(Near(fast, slow, 0.05f));
    CHECK(Near(fast, 1.0f - kTorsoFollowDeadzoneRad, 0.05f));
    // One 62 ms frame steps at most 1.5 rad/s * dt, far below the cap.
    CHECK(TorsoFollowDeltaDt(3.0f, 1.0f / 16.0f) < 0.1f);
    CHECK(Near(TorsoFollowDeltaDt(3.0f, 1.0f / 16.0f), 1.5f / 16.0f, 1e-5f));
  }

  // Candidate geometry only: +X stick is strafe right, +Y is forward.
  // Desired world travel stays fixed as the native movement frame turns.
  {
    VrHeading heading;
    CHECK(!heading.CandidateLocomotion(1, 0, {0, 0, 1}).valid);
    CHECK(heading.SeedOnce({0, 0, 1}));
    auto right = heading.CandidateLocomotion(1, 0, {0, 0, 1});
    CHECK(right.valid && Near(right.stick_x, 1) && Near(right.stick_y, 0));
    auto back = heading.CandidateLocomotion(0, -1, {0, 0, 1});
    CHECK(back.valid && Near(back.stick_x, 0) && Near(back.stick_y, -1));
    right = heading.CandidateLocomotion(1, 0, {1, 0, 0});
    CHECK(right.valid && Near(right.stick_x, 0) && Near(right.stick_y, 1));
    back = heading.CandidateLocomotion(0, -1, {1, 0, 0});
    CHECK(back.valid && Near(back.stick_x, 1) && Near(back.stick_y, 0));
    auto forward = heading.CandidateLocomotion(0, 1, {1, 0, 0});
    CHECK(forward.valid && Near(forward.stick_x, -1) && Near(forward.stick_y, 0));
    right = heading.CandidateLocomotion(1, 0, {0, 0, -1});
    CHECK(right.valid && Near(right.stick_x, -1) && Near(right.stick_y, 0));
    back = heading.CandidateLocomotion(0, -1, {0, 0, -1});
    CHECK(back.valid && Near(back.stick_x, 0) && Near(back.stick_y, 1));
    auto diagonal = heading.CandidateLocomotion(0.3f, 0.4f, {1, 0, 0});
    CHECK(diagonal.valid && Near(diagonal.stick_x, -0.4f) &&
          Near(diagonal.stick_y, 0.3f));
    CHECK(Near(std::hypot(diagonal.stick_x, diagonal.stick_y), 0.5f));
    diagonal = heading.CandidateLocomotion(1, 1, {0, 0, 1});
    CHECK(diagonal.valid && Near(std::hypot(diagonal.stick_x, diagonal.stick_y), 1.0f));
    auto centre = heading.CandidateLocomotion(0, 0, {-1, 0, 0});
    CHECK(centre.valid && Near(centre.stick_x, 0) && Near(centre.stick_y, 0));
    CHECK(VNear(heading.BodyBasis().f, 0, 0, 1));
    CHECK(heading.ApplyTurn(kQuarterTurn));
    forward = heading.CandidateLocomotion(0, 1, {0, 0, 1});
    CHECK(forward.valid && Near(forward.stick_x, 1) && Near(forward.stick_y, 0));

    // The existing pad convention consumes these axes without swapping or
    // inverting them. This does NOT wire the candidate into MapVrPad.
    VrPadInput pad;
    pad.left.stick_x = forward.stick_x;
    pad.left.stick_y = forward.stick_y;
    auto mapped = MapVrPad(pad);
    CHECK(mapped.thumb_lx == 32767 && mapped.thumb_ly == 0);

    auto bad = heading.CandidateLocomotion(1, 0, {0, 1, 0});
    CHECK(!bad.valid && bad.stick_x == 0 && bad.stick_y == 0);
    CHECK(!heading.CandidateLocomotion(
        std::numeric_limits<float>::quiet_NaN(), 0, {0, 0, 1}).valid);
    CHECK(!heading.CandidateLocomotion(
        0, std::numeric_limits<float>::infinity(), {0, 0, 1}).valid);
    CHECK(!heading.CandidateLocomotion(
        0, 1, {0, 0, std::numeric_limits<float>::infinity()}).valid);
    CHECK(VNear(heading.BodyBasis().f, 1, 0, 0));
    heading.Reset();
    bad = heading.CandidateLocomotion(0, 1, {0, 0, 1});
    CHECK(!bad.valid && bad.stick_x == 0 && bad.stick_y == 0);
  }

  std::printf("test_vr_heading: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
