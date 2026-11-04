// Cast-aim selection + two-hand bow steering (lane C input side).
// No game, no XR: pure math/logic on DSVR types. ERVR-derived design,
// reimplemented (see cast_aim_math.h header note).
#include <cmath>
#include <cstdio>

#include "../src/core/cast_aim_math.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {

bool QuatUnit(dsvr::AimQuat q) {
  const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  return std::fabs(n - 1.f) < 1e-4f;
}

float QuatAngle(dsvr::AimQuat q) {
  return dsvr::VLen(dsvr::AimQuatRotationVector(q));
}

}  // namespace

int main() {
  using namespace dsvr;
  // ---- selector: nothing aims until a press or a held ranged weapon ----
  {
    CastAimSelector s;
    bool casting = true;
    CHECK(s.AimHand(10.0, &casting) == -1 && !casting);
    s.NotePress(1, 10.0, 1.5);
    CHECK(s.AimHand(10.0, &casting) == 1 && casting);
    CHECK(s.AimHand(11.4, &casting) == 1 && casting);
    CHECK(s.AimHand(11.6, &casting) == -1 && !casting);  // window expired
    s.SetPassiveHand(0);
    CHECK(s.AimHand(12.0, &casting) == 0 && !casting);  // passive aims
    s.NotePress(1, 12.5, 1.5);
    CHECK(s.AimHand(13.0) == 1);  // live press beats passive
    CHECK(s.AimHand(14.1) == 0);  // expiry falls back to passive
    s.SetPassiveHand(5);
    CHECK(s.AimHand(14.2) == -1);  // invalid passive clears
    CastAimSelector bad;
    bad.NotePress(-1, 1.0, 1.0);
    bad.NotePress(2, 1.0, 1.0);
    bad.NotePress(0, 1.0, -1.0);
    CHECK(bad.AimHand(1.5) == -1);  // invalid presses ignored
    bad.Reset();
    bad.NotePress(0, 5.0, 1.0);
    bad.Reset();
    CHECK(bad.AimHand(5.5) == -1);  // reset clears the press
  }
  // ---- steering: released wrist is always 1:1, never active ----
  {
    BowAimSteer steer;
    const AimQuat wrist{0, 0, 0, 1};
    for (int i = 0; i < 10; ++i) {
      const AimQuat out =
          steer.Update(wrist, {0.3f, 0, 0}, false, 1.f / 90.f);
      CHECK(QuatUnit(out) && QuatAngle(out) < 1e-5f);
    }
    CHECK(!steer.Active());
  }
  // ---- steering: coincident controllers never acquire ----
  {
    BowAimSteer steer;
    const AimQuat out = steer.Update({0, 0, 0, 1}, {0.01f, 0, 0}, true,
                                     1.f / 90.f);
    CHECK(QuatUnit(out) && QuatAngle(out) < 1e-5f && !steer.Active());
  }
  // ---- steering: separation acquires; twist about the line is removed ----
  {
    BowAimSteer steer;
    AimQuat out = steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, true, 1.f / 90.f);
    CHECK(QuatUnit(out) && steer.Active());
    // Roll the wrist 45 deg about the grip line itself: the line expressed
    // in the wrist frame does not move, so no new correction may appear and
    // the output must equal the rolled wrist 1:1.
    const AimQuat rolled{0.383f, 0, 0, 0.924f};  // 45 deg about X
    out = steer.Update(rolled, {0.3f, 0, 0}, true, 1.f / 90.f);
    CHECK(QuatUnit(out));
    CHECK(QuatAngle(AimQuatMul(AimQuatConj(rolled), out)) < 0.02f);
  }
  // ---- steering: a 60-degree line move converges, clamped at 80 ----
  {
    BowAimSteer steer;
    for (int i = 0; i < 5; ++i)
      steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, true, 1.f / 90.f);
    AimQuat out{0, 0, 0, 1};
    const float c = 0.5f, s = 0.8660254f;  // 60 deg toward +Y in the XY plane
    for (int i = 0; i < 120; ++i)
      out = steer.Update({0, 0, 0, 1}, {0.3f * c, 0.3f * s, 0}, true,
                         1.f / 90.f);
    CHECK(QuatUnit(out));
    const float got = QuatAngle(out);
    CHECK(std::fabs(got - 1.0471976f) < 0.05f);  // ~60 deg, within the 80 cap
  }
  // ---- steering: the 80-degree clamp holds past it ----
  {
    BowAimSteer steer;
    for (int i = 0; i < 5; ++i)
      steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, true, 1.f / 90.f);
    AimQuat out{0, 0, 0, 1};
    for (int i = 0; i < 200; ++i)
      out = steer.Update({0, 0, 0, 1}, {-0.3f, 0.05f, 0}, true, 1.f / 90.f);
    CHECK(QuatUnit(out) && QuatAngle(out) <= 1.3962634f + 0.05f);
  }
  // ---- steering: antipodal flip holds finite, release decays to wrist ----
  {
    BowAimSteer steer;
    for (int i = 0; i < 5; ++i)
      steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, true, 1.f / 90.f);
    AimQuat out = steer.Update({0, 0, 0, 1}, {-0.3f, 0, 0}, true, 1.f / 90.f);
    CHECK(QuatUnit(out));  // exact flip: hold, never NaN
    for (int i = 0; i < 90; ++i)
      out = steer.Update({0, 0, 0, 1}, {-0.3f, 0, 0}, false, 1.f / 90.f);
    CHECK(QuatUnit(out) && QuatAngle(out) < 1e-3f && !steer.Active());
  }
  // ---- steering: bad dt resets instead of exploding ----
  {
    BowAimSteer steer;
    for (int i = 0; i < 5; ++i)
      steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, true, 1.f / 90.f);
    CHECK(steer.Active());
    const AimQuat out =
        steer.Update({0, 0, 0, 1}, {0.3f, 0, 0}, false, 0.f);
    CHECK(QuatUnit(out) && QuatAngle(out) < 1e-5f && !steer.Active());
  }
  std::puts("test_cast_aim: PASS");
  return 0;
}
