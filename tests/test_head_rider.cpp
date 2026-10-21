#include "../src/core/head_rider.h"

#include <cmath>
#include <cstdio>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace dsvr;
  const float identity[9] = {1,0,0, 0,1,0, 0,0,1};
  // No offset needed: solved head already at the eye point.
  Vec3 zero = HeadRiderOffsetModel({1,2,3},{1,2,3},identity);
  CHECK(zero.x==0 && zero.y==0 && zero.z==0);
  // Small offset passes through (model == world under identity root).
  Vec3 small = HeadRiderOffsetModel({1.1f,2,3},{1,2,3},identity);
  CHECK(std::fabs(small.x-.1f)<1e-5f && small.y==0 && small.z==0);
  // Large offset is bounded to the shift cap (body cannot outrun camera).
  Vec3 big = HeadRiderOffsetModel({6,2,3},{1,2,3},identity);
  CHECK(std::fabs(VLen(big)-.35f)<1e-4f && big.x>0);
  // Yawed root: world +X offset becomes model -Z under a 180-degree root.
  const float yaw180[9] = {-1,0,0, 0,1,0, 0,0,-1};
  Vec3 rotated = HeadRiderOffsetModel({1.2f,0,0},{1,0,0},yaw180);
  CHECK(std::fabs(rotated.x+.2f)<1e-5f && std::fabs(rotated.z)<1e-5f);
  // Non-finite input fails closed to zero.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  Vec3 bad = HeadRiderOffsetModel({nan,0,0},{0,0,0},identity);
  CHECK(bad.x==0 && bad.y==0 && bad.z==0);
  Vec3 bad2 = HeadRiderOffsetModel({0,0,0},{0,0,0},identity,-1.0f);
  CHECK(bad2.x==0 && bad2.y==0 && bad2.z==0);
  // Eye point sits up and forward from the head pivot.
  Vec3 eye = HeadEyePoint({0,1.6f,0},{1,0,0},{0,1,0},{0,0,1});
  CHECK(std::fabs(eye.y-1.7f)<1e-5f && std::fabs(eye.z-.08f)<1e-5f);
  std::puts("PASS head rider offset bounds/rotation/fail-closed");
  return 0;
}
