// Spent-contact valley rearm (lane B bench follow-up). No game, no XR:
// pure frame-driven lifecycle. ERVR-derived design, reimplemented
// (see blade_rearm_math.h header note).
#include <cmath>
#include <cstdio>
#include <limits>

#include "../src/core/blade_rearm_math.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  // ---- fresh blade is live ----
  {
    BladeRearm r;
    CHECK(r.AllowContact(0.0f, 0.0, 0.6f, 2.0));
    CHECK(r.Rearmed(0.0f));
  }
  // ---- a hit spends; the follow-through cannot reopen until valley + speedup --
  {
    BladeRearm r;
    r.ArmSpent(8.0f, 2.0f, 0.0);
    CHECK(!r.AllowContact(8.0f, 0.01, 0.6f, 2.0));
    CHECK(!r.AllowContact(6.0f, 0.02, 0.6f, 2.0));
    CHECK(!r.AllowContact(5.0f, 0.03, 0.6f, 2.0));
    // Valley is max(0.6 * 8, 2) = 4.8. Bottoming out ends the spend but the
    // tip (4.7) is far below the rearm line (4.7 * 1.5 + 0.5 = 7.55).
    CHECK(!r.AllowContact(4.7f, 0.04, 0.6f, 2.0));
    CHECK(!r.AllowContact(6.0f, 0.05, 0.6f, 2.0));
    CHECK(r.AllowContact(7.6f, 0.06, 0.6f, 2.0));  // sped up again: live
    CHECK(r.AllowContact(1.0f, 0.07, 0.6f, 2.0));  // never spent again
  }
  // ---- timeout ends the spend for tips that never slow, still needs speedup --
  {
    BladeRearm r;
    r.ArmSpent(8.0f, 0.5f, 0.0);
    for (int i = 1; i <= 10; ++i)
      CHECK(!r.AllowContact(6.0f, i * 0.1, 0.6f, 2.0));  // holds 6, no valley
    CHECK(!r.AllowContact(6.0f, 2.0, 0.6f, 2.0));  // timeout ends it...
    CHECK(!r.AllowContact(6.0f, 2.1, 0.6f, 2.0));  // ...but 6 is no speedup
    CHECK(r.AllowContact(9.6f, 2.2, 0.6f, 2.0));   // 6 * 1.5 + 0.5 = 9.5
  }
  // ---- bad input fails soft without corrupting state ----
  {
    BladeRearm r;
    r.ArmSpent(8.0f, 2.0f, 0.0);
    CHECK(!r.AllowContact(std::numeric_limits<float>::quiet_NaN(), 0.01, 0.6f,
                          2.0));
    CHECK(!r.AllowContact(7.0f, 0.02, 0.6f, 2.0));  // still spent
    r.Reset();
    CHECK(r.AllowContact(0.0f, 0.03, 0.6f, 2.0));
  }
  std::puts("test_blade_rearm: PASS");
  return 0;
}
