#include "../src/core/stereo_math.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <source_location>

int main() {
  int failures = 0;
  auto check = [&](bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { ++failures; std::printf("failed at line %u\n", at.line()); }
  };
  auto near = [](float a, float b) { return std::fabs(a - b) < 0.002f; };
  using namespace dsvr;
  const float kDeg = 0.0174532925f;

  // Theater FOV is a horizontal setting; vertical extent follows the
  // game projection aspect so mono never stretches to a square angular field.
  {
    const auto screen = TheaterScreenExtents(60.0f * kDeg, 16.0f / 9.0f);
    check(near(screen.tan_half_h, std::tan(30.0f * kDeg)));
    check(near(screen.tan_half_v, screen.tan_half_h / (16.0f / 9.0f)));
    check(near(screen.tan_half_h / screen.tan_half_v, 16.0f / 9.0f));
    check(TheaterScreenExtents(0.0f, 1.0f).tan_half_h == 0.0f);
    check(TheaterScreenExtents(60.0f * kDeg, 0.0f).tan_half_v == 0.0f);
      const EyeFrustum asymmetric[2] = {
        FrustumFromAngles(-40*kDeg, 50*kDeg, 48*kDeg, -48*kDeg),
          FrustumFromAngles(-50*kDeg, 40*kDeg, 48*kDeg, -48*kDeg)};
    const auto wide = TheaterScreenExtents(110*kDeg, 16.0f/9.0f);
    const auto fitted = FitTheaterScreenToEyes(wide, asymmetric, 16.0f/9.0f);
    check(fitted.tan_half_h <= std::tan(40*kDeg) + 0.002f);
    check(near(fitted.tan_half_h / fitted.tan_half_v, 16.0f/9.0f));
    for (const auto& eye : asymmetric) {
      check(fitted.tan_half_h <= -eye.tan_left + 0.002f);
      check(fitted.tan_half_h <= eye.tan_right + 0.002f);
      check(fitted.tan_half_v <= eye.tan_up + 0.002f);
      check(fitted.tan_half_v <= -eye.tan_down + 0.002f);
    }
  }

  // Symmetric 90x90 eye: game frustum is itself, sub-rect fills the buffer.
  {
    EyeFrustum eye = FrustumFromAngles(-45 * kDeg, 45 * kDeg, 45 * kDeg,
                                       -45 * kDeg);
    check(near(FrustumVerticalFov(eye), 90 * kDeg));
    check(near(FrustumAspect(eye), 1.0f));
    SymmetricFrustum game = GameFrustumFor(eye);
    EyeSubRect r = SubRectForEye(game, eye);
    check(near(r.x, 0.0f) && near(r.y, 0.0f) && near(r.w, 1.0f) &&
          near(r.h, 1.0f));
  }

  // Quest-like asymmetric eye (wider outward): the symmetric game image
  // sits inset toward the nose and still inside the buffer.
  {
    EyeFrustum right_eye = FrustumFromAngles(-40 * kDeg, 50 * kDeg,
                                             48 * kDeg, -48 * kDeg);
    SymmetricFrustum game = GameFrustumFor(right_eye);
    check(near(game.vfov_rad, 96 * kDeg));
    EyeSubRect r = SubRectForEye(game, right_eye);
    // The widest-vertical game frustum is wider nasally than this eye, so
    // the rect overflows the nose-side edge (cropped by the viewport,
    // exactly as xr.cpp's shipped immersive rect behaves) -- not inset.
    check(r.x < 0.0f && r.w > 0.5f && r.x + r.w < 1.0f);
    check(near(r.y + r.h * 0.5f, 0.5f));  // vertically centred by design
    // Mirrored left eye mirrors the rect about the buffer centre.
    EyeFrustum left_eye = FrustumFromAngles(-50 * kDeg, 40 * kDeg, 48 * kDeg,
                                            -48 * kDeg);
    EyeSubRect l = SubRectForEye(game, left_eye);
    check(near(l.x, 1.0f - (r.x + r.w)) && near(l.w, r.w));
  }

  // Widest-eye policy: a narrower second eye never widens the game frustum.
  {
    EyeFrustum wide = FrustumFromAngles(-50 * kDeg, 50 * kDeg, 50 * kDeg,
                                        -50 * kDeg);
    EyeFrustum narrow = FrustumFromAngles(-40 * kDeg, 40 * kDeg, 40 * kDeg,
                                          -40 * kDeg);
    check(GameFrustumFor(wide).vfov_rad > GameFrustumFor(narrow).vfov_rad);
  }

  // Anchor helpers: midpoint cancels the half-IPD orbit; offsets are signed.
  {
    Vec3 left{-0.032f, 1.65f, 0.01f}, right{0.032f, 1.65f, 0.01f};
    Vec3 mid = EyeMidpoint(left, right);
    check(near(mid.x, 0.0f) && near(mid.y, 1.65f));
    check(near(EyeSeparation(left, right), 0.064f));
    Vec3 body_right{1, 0, 0};  // game basis, fwd = +Z
    check(EyeOffsetAlongRight(left, mid, body_right) < 0.0f);
    check(EyeOffsetAlongRight(right, mid, body_right) > 0.0f);
    check(near(EyeOffsetAlongRight(right, mid, body_right), 0.032f));
  }

  // Turn-basis helper (camera_math.h): +angle turns fwd toward +X (right),
  // up survives untouched, a full turn is the identity, handedness holds.
  {
    Vec3 r{1, 0, 0}, u{0, 1, 0}, f{0, 0, 1};
    Vec3 r2, f2;
    RotateBasisAboutUp(r, u, f, 45 * kDeg, r2, f2);
    float s = 0.70710678f;
    check(near(f2.x, s) && near(f2.z, s) && near(f2.y, 0.0f));
    check(near(VDot(r2, VCross(u, f2)), 1.0f));  // left-handed det stays +1
    RotateBasisAboutUp(r, u, f, 360 * kDeg, r2, f2);
    check(near(f2.z, 1.0f) && near(f2.x, 0.0f));
    RotateBasisAboutUp(r, u, f, -45 * kDeg, r2, f2);
    check(f2.x < 0.0f);  // negative turns left
  }

  // Measured shader rows and post-VS output, Asylum frame 2774/event 2579.
  // CPU camera objects are transposed; using that layout here must fail.
  {
    const float center[16] = {
        1.393025637f, .00001013314f, -.314059705f, 0,
        .116013989f, 2.483222723f, .514665365f, 0,
        .215104699f, -.207747921f, .954255223f, -.050067872f,
        .215101242f, -.207744583f, .954239845f, 0};
    Vec3 r{1,0,0}, u{0,1,0}, f{0,0,1};
    const float sx = VLen({center[0],center[1],center[2]});
    const EyeFrustum original{-.700251072f,.700319482f,.393834449f,-.393986405f};
    float eye[16];
    check(NativeEyeClip(center,original,{},r,u,f,eye));
    for (int i = 0; i < 16; ++i)
      check(std::fabs(eye[i]-center[i]) < .000002f);
    const float native_world[4] = {.441180080f,-.274507880f,1.659567595f,1};
    const float gpu_clip[4] = {.093369067f,.223640800f,1.685511470f,1.735551476f};
    for (int row = 0; row < 4; ++row) {
      float clip = 0;
      for (int col = 0; col < 4; ++col) clip += eye[row*4+col]*native_world[col];
      check(std::fabs(clip-gpu_clip[row]) < .000002f);
    }
    // In center-view coordinates, near points have more disparity. The
    // center-relative palette/world position is identical for the eye pair.
    const Vec3 native_r{.975522701f,0,-.219898747f};
    const Vec3 native_u{.045682777f,.978183106f,.202659552f};
    const Vec3 native_f{.215101238f,-.207744579f,.954239827f};
    float left[16], right[16];
    check(NativeEyeClip(center,original,{-.032f,0,0},r,u,f,left));
    check(NativeEyeClip(center,original,{ .032f,0,0},r,u,f,right));
    auto ndc_x = [&](const float* m, float distance) {
      const float p[4] = {native_f.x*distance,native_f.y*distance,
                          native_f.z*distance,1};
      float x = 0, w = 0;
      for (int i = 0; i < 4; ++i) { x += m[i]*p[i]; w += m[12+i]*p[i]; }
      return x/w;
    };
    check(ndc_x(left,1) > 0 && ndc_x(right,1) < 0);
    check(std::fabs((ndc_x(left,1)-ndc_x(right,1)) - .064f*sx) < .000002f);
    check(std::fabs((ndc_x(left,1)-ndc_x(right,1)) -
                   10*(ndc_x(left,10)-ndc_x(right,10))) < .000002f);
    for (int i = 8; i < 16; ++i) check(left[i] == right[i]);

    // Asymmetric frusta map the eye's actual left/right/up/down rays to
    // the viewport edges, including a canted eye and nonzero xyz origin.
    const float s = .5f, c = .866025404f;
    r = {c,0,-s}; f = {s,0,c};
    const Vec3 origin{.032f,.01f,.02f};
    const EyeFrustum asym{-1.1f,.8f,1.2f,-.9f};
    check(NativeEyeClip(center,asym,origin,r,u,f,eye));
    auto edge = [&](float vx, float vy, int component) {
      const Vec3 p{origin.x+r.x*vx+u.x*vy+f.x,
                   origin.y+r.y*vx+u.y*vy+f.y,
                   origin.z+r.z*vx+u.z*vy+f.z};
      const float world[4] = {
          native_r.x*p.x+native_u.x*p.y+native_f.x*p.z,
          native_r.y*p.x+native_u.y*p.y+native_f.y*p.z,
          native_r.z*p.x+native_u.z*p.y+native_f.z*p.z,1};
      float value = 0, w = 0;
      for (int i = 0; i < 4; ++i) {
        value += eye[component*4+i]*world[i]; w += eye[12+i]*world[i];
      }
      return value/w;
    };
    check(std::fabs(edge(asym.tan_left,0,0)+1) < .000002f);
    check(std::fabs(edge(asym.tan_right,0,0)-1) < .000002f);
    check(std::fabs(edge(0,asym.tan_up,1)-1) < .000002f);
    check(std::fabs(edge(0,asym.tan_down,1)+1) < .000002f);

    float bad[16];
    for (int i=0;i<16;++i) bad[i]=center[(i%4)*4+i/4];
    check(!NativeEyeClip(bad,original,{},r,u,f,eye));
    for (float v : eye) check(v == 0);
    for (int i=0;i<16;++i) bad[i]=center[i];
    bad[3] = 1; // absolute-world translation is outside this observed contract
    check(!NativeEyeClip(bad,original,{},r,u,f,eye));
    bad[3] = 0; bad[0] = std::numeric_limits<float>::quiet_NaN();
    check(!NativeEyeClip(bad,original,{},r,u,f,eye));
    check(!NativeEyeClip(center,{1,1,1,-1},{},r,u,f,eye));
    check(!NativeEyeClip(center,original,{},r,u,{0,0,0},eye));
    for (int i=0;i<16;++i) eye[i]=center[i];
    check(NativeEyeClip(eye,original,{}, {1,0,0},u,{0,0,1},eye));
  }

  // Pixel parity for the theater quad: the game image is copied into a sub-rect
  // of the eye buffer, so a quad wider than the source has pixels upsamples it.
  // Live report 31 Oct 2026: a "huge, blurry, badly jagged" panel.
  {
    const float t = std::tan(50.0f * kDeg);          // symmetric 100deg frustum
    const float aspect = 2144.0f / 2292.0f;          // Quest 3 eye aspect
    const EyeFrustum eyes[2] = {{-t, t, t, -t}, {-t, t, t, -t}};
    const ScreenTangentExtents req{t, t / aspect};   // 100deg requested, game shape

    // 1280x720 into a 2144x2292 eye: the vertical axis binds first.
    const auto got = FitTheaterScreenToPixels(req, eyes, 2144, 2292, 1280, 720);
    const float limit_v = 720.0f * (2.0f * t) / (2.0f * 2292.0f);
    check(got.tan_half_v > 0.0f && got.tan_half_v < req.tan_half_v);
    check(near(got.tan_half_v, limit_v));
    check(near(got.tan_half_h / got.tan_half_v, req.tan_half_h / req.tan_half_v));
    check(got.tan_half_h <= 1280.0f * (2.0f * t) / (2.0f * 2144.0f));

    // A source with more pixels than the quad needs must not be touched.
    const auto same = FitTheaterScreenToPixels(req, eyes, 2144, 2292, 4096, 4096);
    check(same.tan_half_h == req.tan_half_h && same.tan_half_v == req.tan_half_v);

    // Unknown sizes or frusta leave the request alone rather than collapsing it.
    const auto no_src = FitTheaterScreenToPixels(req, eyes, 2144, 2292, 0, 0);
    check(no_src.tan_half_h == req.tan_half_h);
    const EyeFrustum dead[2] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    const auto no_eye = FitTheaterScreenToPixels(req, dead, 2144, 2292, 1280, 720);
    check(no_eye.tan_half_h == req.tan_half_h);
    const ScreenTangentExtents zero{0.0f, 0.0f};
    check(FitTheaterScreenToPixels(zero, eyes, 2144, 2292, 1280, 720).tan_half_h == 0.0f);

    // Asymmetric eyes: the narrower frustum binds, not the wider one.
    const EyeFrustum asym[2] = {{-t, t, t, -t}, {-0.5f * t, 0.5f * t, 0.5f * t, -0.5f * t}};
    const auto tight = FitTheaterScreenToPixels(req, asym, 2144, 2292, 1280, 720);
    check(tight.tan_half_v < got.tan_half_v);
  }

  // The theater panel is fixed at 16:9 and centered on the buffer. Source
  // content is contained inside it by the tested pixel-rect fit.
  {
    const float t = std::tan(50.0f * kDeg);
    const EyeFrustum eye{-t, t, t, -t};
    const auto screen = TheaterScreenExtents(80.0f * kDeg, kTheaterAspect);
    const PixelRect r = TheaterRectForEye(screen, eye, 2144.0f, 2292.0f);
    check(near(screen.tan_half_h / screen.tan_half_v, kTheaterAspect));
    check(r.w > r.h);  // a landscape panel, not the old near-square one
    // Centred on buffer mid-height (the user-tuned midpoint), and symmetric
    // horizontally about the frustum centre.
    check(near(r.y + r.h * 0.5f, 2292.0f * 0.5f));
    check(near(r.x + r.w * 0.5f, 2144.0f * 0.5f));
    check(r.w > 0.0f && r.w < 2144.0f && r.h > 0.0f && r.h < 2292.0f);
    const PixelRect fitted = FitPixelRectToAspect(
        r, kTheaterAspect, 1440.0f, 1440.0f);
    check(fitted.w > 0.0f && near(fitted.w / fitted.h, kTheaterAspect));
    check(fitted.x >= r.x && fitted.y >= r.y &&
          fitted.x + fitted.w <= r.x + r.w &&
          fitted.y + fitted.h <= r.y + r.h);
    const PixelRect invalid = FitPixelRectToAspect(r, 0.0f, 1440.0f, 1440.0f);
    check(invalid.w == 0.0f && invalid.h == 0.0f);
    // Degenerate inputs collapse to an empty rect rather than NaN or a divide.
    const EyeFrustum dead{0.0f, 0.0f, 0.0f, 0.0f};
    check(TheaterRectForEye(screen, dead, 2144.0f, 2292.0f).w == 0.0f);
    const ScreenTangentExtents zero{0.0f, 0.0f};
    check(TheaterRectForEye(zero, eye, 2144.0f, 2292.0f).h == 0.0f);
    check(TheaterRectForEye(screen, eye, 0.0f, 0.0f).x == 0.0f);
  }

  std::printf("stereo math: %d failures\n", failures);
  return failures ? 1 : 0;
}
