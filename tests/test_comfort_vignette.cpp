#include "../src/core/comfort_vignette.h"
#include <cstdio>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  const float head[4]{.1f, .2f, .3f, .92736185f};
  const auto ramp = [&](int hz, float strength, bool rotate, bool turn, bool move) {
    ComfortVignette effect;
    float amount = 0;
    for (int i = 0; i <= hz; ++i) {
      const float angle = 2.f * i / hz;
      const float q[4]{0, std::sin(angle * .5f), 0, std::cos(angle * .5f)};
      amount = effect.Update({move ? .6f : 0, turn ? .7f / hz : 0},
          rotate ? q : head, 1'000'000'000LL + 1'000'000'000LL * i / hz,
          1, true, strength);
    }
    return amount;
  };
  const float quiet = ramp(90, 1, false, false, true);
  CHECK(std::fabs(quiet - .5f) < .0001f); // held nonidentity pose adds no boost
  for (int hz : {30, 60, 120, 144})
    CHECK(std::fabs(ramp(hz, 1, false, false, true) - quiet) < .00001f);
  CHECK(ramp(90, 1, true, false, true) > quiet + .2f);
  CHECK(ramp(90, 1, true, false, false) == 0); // physical-only head turns
  CHECK(ramp(90, 1, false, true, false) > .3f);
  CHECK(std::fabs(ramp(90, .25f, false, false, true) - quiet * .25f) < .00001f);

  ComfortVignette effect;
  auto update = [&](std::int64_t time, ComfortMotion motion = {1, 0},
                    std::uint64_t epoch = 1, bool enabled = true) {
    return effect.Update(motion, head, time, epoch, enabled, 1);
  };
  CHECK(update(1'000'000'000) == 0);
  float amount = update(1'060'000'000);
  CHECK(amount > .63f && amount < .64f); // 60 ms attack
  CHECK(update(1'060'000'000) == amount); // duplicate callback cannot spend dt
  float release = update(1'310'000'000, {});
  CHECK(std::fabs(release - amount / std::exp(1.f)) < .00001f);
  CHECK(update(1'320'000'000, {1,0}, 2) == 0); // recenter
  CHECK(update(1'330'000'000, {1,0}, 2) > 0);
  CHECK(update(1'340'000'000, {1,0}, 2, false) == 0); // Off is immediate
  CHECK(update(1'350'000'000) == 0);
  CHECK(update(1'360'000'000) > 0);
  CHECK(update(2'000'000'000) == 0); // stale gap
  CHECK(update(1'000'000'000) == 0); // clock regression
  CHECK(update(1'010'000'000) > 0);
  CHECK(update(1'020'000'000, {std::numeric_limits<float>::quiet_NaN(),0}) == 0);
  const float invalid[4]{};
  CHECK(effect.Update({1,0}, invalid, 1'030'000'000, 1, true, 1) == 0);
  std::puts("PASS comfort timing, motion selection, head boost and lifecycle");
}
