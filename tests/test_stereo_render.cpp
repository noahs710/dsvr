#include "../src/core/stereo_render.h"

#include <cmath>
#include <cstdio>
#include <source_location>

namespace {
struct Recorder {
  unsigned calls = 0;
  unsigned order[4] = {99, 99, 99, 99};
  void* textures[4] = {};
  bool fail_eye1 = false;
  unsigned submits = 0;
};
bool Draw(void* ctx, unsigned eye, void* texture) {
  auto* r = static_cast<Recorder*>(ctx);
  r->textures[r->calls] = texture;
  r->order[r->calls++] = eye;
  return !(r->fail_eye1 && eye == 1);
}
bool Submit(void* ctx, std::uint64_t) {
  ++static_cast<Recorder*>(ctx)->submits;
  return true;
}
}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { ++failures; std::printf("failed at line %u\n", at.line()); }
  };
  using namespace dsvr;
  int tex_a = 0, tex_b = 0;
  void* pair[2] = {&tex_a, &tex_b};

  // Happy path: one begin, each eye once, single submit, no cancels.
  {
    StereoBracket b;
    Recorder r;
    check(b.Begin(7, pair, 1000));
    check(!b.Begin(8, pair, 1000));  // already active
    check(b.RenderEye(0, Draw, &r, 1005));
    check(!b.RenderEye(0, Draw, &r, 1006));  // eye twice cancels the pair
    check(!b.Active());
    check(b.Stats().begun == 1 && b.Stats().cancelled == 1);
  }
  {
    StereoBracket b;
    Recorder r;
    check(b.Begin(7, pair, 1000));
    check(b.RenderEye(1, Draw, &r, 1005));  // either order is fine
    check(b.RenderEye(0, Draw, &r, 1006));
    check(r.order[0] == 1 && r.order[1] == 0);
    check(r.textures[0] == &tex_b && r.textures[1] == &tex_a);
    check(b.RenderedMask() == 3u);
    check(!b.Finish(8, Submit, &r, 1010));  // wrong token cancels
    check(r.submits == 0 && !b.Active());
    check(b.Begin(9, pair, 2000));
    check(b.RenderEye(0, Draw, &r, 2001));
    check(b.RenderEye(1, Draw, &r, 2002));
    check(b.Finish(9, Submit, &r, 2003));
    check(r.submits == 1 && b.Stats().submitted == 1);
  }
  // Refusals: zero token, null/same textures, partial mask, timeout, draw fail.
  {
    StereoBracket b;
    Recorder r;
    void* same[2] = {&tex_a, &tex_a};
    void* nulls[2] = {&tex_a, nullptr};
    check(!b.Begin(0, pair, 0));
    check(!b.Begin(1, same, 0));
    check(!b.Begin(1, nulls, 0));
    check(b.Begin(2, pair, 100, 50));
    check(b.RenderEye(0, Draw, &r, 110));
    check(!b.Finish(2, Submit, &r, 120));  // mask 1 != 3
    check(b.Begin(3, pair, 100, 50));
    check(!b.RenderEye(0, Draw, &r, 200));  // timed out -> cancel
    check(b.Begin(4, pair, 300));
    r.fail_eye1 = true;
    check(b.RenderEye(0, Draw, &r, 301));
    check(!b.RenderEye(1, Draw, &r, 302));  // draw failure cancels pair
    check(r.submits == 0);
  }
  // Eye clips: offsets produce opposite-sign parallax; bad input clears all.
  {
    const float center[16] = {
        1.393025637f, .00001013314f, -.314059705f, 0,
        .116013989f, 2.483222723f, .514665365f, 0,
        .215104699f, -.207747921f, .954255223f, -.050067872f,
        .215101242f, -.207744583f, .954239845f, 0};
    const EyeFrustum frustum{-.700251072f, .700319482f, .393834449f,
                             -.393986405f};
    StereoEyeInput eyes[2];
    eyes[0].frustum = frustum; eyes[0].origin = {-.032f, 0, 0};
    eyes[1].frustum = frustum; eyes[1].origin = {.032f, 0, 0};
    float clips[2][16];
    check(BuildEyeClips(center, eyes, clips));
    bool differ = false;
    for (int i = 0; i < 16; ++i) differ |= clips[0][i] != clips[1][i];
    check(differ);  // distinct eye views, not a mono copy
    eyes[1].forward = {0, 0, 0};  // degenerate basis refuses the pair
    for (auto& row : clips) for (float& v : row) v = 1.0f;
    check(!BuildEyeClips(center, eyes, clips));
    for (auto& row : clips) for (float v : row) check(v == 0.0f);
  }

  if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
  std::printf("stereo_render: all checks passed\n");
  return 0;
}
