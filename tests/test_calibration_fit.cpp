#include "../src/core/calibration_fit.h"
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main() {
  using namespace dsvr;
  auto near=[](float a,float b) {return std::fabs(a-b)<1e-5f;};
  UserCalibration p;p.valid=true;p.revision=7;
  p.eye_height_m=1.8f;
  CHECK(near(CalibrationViewHeight(p,1.65f,true),1.8f));
  p.eye_height_m=1.65f;
  p.arm_reach_m[0]=p.arm_reach_m[1]=.6f;
  const auto fit=ResolveCalibrationMapping(p,1.65f,false,1.f,.54f,3);
  CHECK(fit.valid && fit.fitted && near(fit.world_scale,.9f));
  CHECK(near(fit.eye_height,1.85f*.9f) && fit.calibration_revision==7 && fit.model_revision==3);
  // The +0.2 m height trim is an applied offset, never a profile write: the
  // saved profile still reports the height that was actually captured.
  CHECK(near(p.eye_height_m,1.65f));
  CHECK(near(fit.eye_height/fit.world_scale,1.65f+kCalibrationHeightOffsetM));
  // The same conversion fits the ENTIRE native avatar/world, not an arm-only
  // controller gain. One physical metre, including head motion, is .9 native.
  const auto moved=HeadTranslationWorld({1,0,0},{1,0,0},{0,1,0},{0,0,1},fit.world_scale);
  CHECK(near(moved.x,.9f));
  for(float height:{1.2f,1.65f,2.1f}) {
    p.eye_height_m=height;
    const auto stature=ResolveCalibrationMapping(p,1.65f,false,1.f,.54f,3);
    CHECK(stature.valid && near(stature.eye_height/ stature.world_scale,height+kCalibrationHeightOffsetM));
    CHECK(near(stature.world_scale,fit.world_scale)); // height is not arm reach
    const auto seated=ResolveCalibrationMapping(p,1.65f,true,1.f,.54f,3);
    CHECK(seated.valid && near(seated.eye_height,(height+kCalibrationHeightOffsetM+.5f)*.9f));
  }
  p.eye_height_m=1.65f;
  const auto trim=ResolveCalibrationMapping(p,1.65f,false,1.5f,.54f,3);
  CHECK(near(trim.world_scale,1.35f) && near(trim.eye_height,1.85f*1.35f));
  const auto model=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4);
  CHECK(near(model.world_scale,1.2f) && model.model_revision==4);
  p.arm_reach_m[0]=p.arm_reach_m[1]=.72f;++p.revision;
  const auto recapture=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4);
  CHECK(near(recapture.world_scale,1.f) && recapture.calibration_revision==8);
  CHECK(near(fit.world_scale,.9f) && fit.calibration_revision==7); // snapshot
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.54f,0).valid);
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,NAN,3).valid);
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,NAN,.54f,3).valid);
  CHECK(!ResolveCalibrationMapping(p,NAN,false,1.f,.54f,3).valid);
  // --- Head-anchored fit: the same single ratio, anchored on the native Head
  // bone, so the camera lands on the avatar's own head instead of hovering
  // above or inside the torso. The arm fit stays available as the alternative.
  const float head_native=1.83f, shoulders_native=.42f;
  const auto head_fit=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,head_native,shoulders_native);
  CHECK(head_fit.valid && head_fit.fitted && head_fit.fit==CalibrationFit::Head);
  CHECK(near(head_fit.world_scale,head_native/(1.65f+kCalibrationHeightOffsetM)));
  CHECK(near(head_fit.eye_height,head_native));  // camera sits on the head bone
  // The trim raises the avatar and the world together: the head anchor still
  // holds, so the camera never floats above the shell, and the ratio between
  // native units and the wearer's metres falls by exactly the trim fraction.
  CHECK(head_fit.world_scale<1.83f/1.65f);  // fewer native units per wearer metre
  CHECK(near(head_fit.native_head_height,head_native));
  CHECK(near(head_fit.native_shoulder_width,shoulders_native));
  CHECK(head_fit.model_revision==4);
  // The manual trim still moves the world and the head anchor together.
  const auto head_trim=ResolveCalibrationMapping(p,1.65f,false,1.5f,.72f,4,head_native,shoulders_native);
  CHECK(near(head_trim.world_scale,head_native/(1.65f+kCalibrationHeightOffsetM)*1.5f));
  CHECK(near(head_trim.eye_height,head_native*1.5f));
  // embodiment_fit="arm" keeps the exact-hand fit even with a head measurement.
  const auto arm_pref=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,head_native,shoulders_native,false);
  CHECK(arm_pref.fit==CalibrationFit::Arm && near(arm_pref.world_scale,1.f));
  CHECK(near(arm_pref.eye_height,1.65f+kCalibrationHeightOffsetM));
  // No upright measurement yet (0) falls back to the arm fit, never a guess.
  const auto no_head=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,0.f,shoulders_native);
  CHECK(no_head.fit==CalibrationFit::Arm && near(no_head.world_scale,1.f));
  // A head measurement outside the plausible band is a stale or deformed
  // palette, not a short player: keep the arm fit rather than making the world
  // enormous, but still report the measurement honestly.
  const auto absurd=ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,12.f,shoulders_native);
  CHECK(absurd.fit==CalibrationFit::Arm && near(absurd.world_scale,1.f));
  CHECK(near(absurd.native_head_height,12.f));
  // Seated floor height is posture: reach (not seated eye height) owns scale.
  const auto seated_head=ResolveCalibrationMapping(p,1.65f,true,1.f,.72f,4,head_native,shoulders_native);
  CHECK(seated_head.fit==CalibrationFit::Arm && near(seated_head.world_scale,1.f));
  p.eye_height_m=1.1f;
  const auto sitting=ResolveCalibrationMapping(p,1.65f,true,1.f,.72f,4,head_native,shoulders_native);
  CHECK(sitting.eye_height/sitting.world_scale>=1.65f); // seated capture must not leave the standing avatar at chair height
  CHECK(near(sitting.world_scale,1.f) && near(sitting.eye_height,1.1f+kCalibrationHeightOffsetM+.5f));
  const auto higher=ResolveCalibrationMapping(p,1.65f,true,1.f,.72f,4,head_native,shoulders_native,true,false,1.f,.75f);
  CHECK(near(higher.eye_height-sitting.eye_height,.25f));
  CHECK(near(higher.world_scale,sitting.world_scale) && near(p.eye_height_m,1.1f));
  CHECK(near(higher.seated_height_offset_m,.75f));
  CHECK(!ResolveCalibrationMapping(p,1.65f,true,1.f,.72f,4,head_native,shoulders_native,true,false,1.f,NAN).valid);
  p.eye_height_m=1.65f;
  const auto manual=ResolveCalibrationMapping(p,1.9f,false,1.f,.72f,4,head_native,shoulders_native,true,true);
  CHECK(near(manual.world_scale,head_fit.world_scale));
  CHECK(near(manual.eye_height/manual.world_scale,1.9f+kCalibrationHeightOffsetM));
  const auto automatic=ResolveCalibrationMapping(p,1.9f,false,1.f,.72f,4,head_native,shoulders_native);
  CHECK(near(automatic.eye_height,head_native));
  CHECK(near(CalibrationViewHeight(p,1.9f,false,true),1.9f));
  // A fitted write still needs the model revision it was measured against.
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,0,head_native,shoulders_native).valid);
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,NAN,shoulders_native).valid);
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,head_native,NAN).valid);
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.72f,4,-1.f,shoulders_native).valid);

  // --- The upright-stance gate for measuring native stature. Only a grounded,
  // standing palette may define the avatar's height; crouch, lunge, kneel and
  // airborne palettes return 0 so the frozen measurement stays authoritative.
  CHECK(near(CalibrationNativeHeadHeight({0,1.83f,0},{0,.95f,0},{0,.02f,.1f},{0,.03f,-.1f}),1.83f));
  CHECK(CalibrationNativeHeadHeight({0,1.83f,0},{0,.95f,0},{0,.6f,0},{0,.62f,0})==0.f);   // airborne
  CHECK(CalibrationNativeHeadHeight({0,1.83f,0},{0,.95f,0},{0,-.2f,0},{0,0.f,0})==0.f);   // below the floor
  CHECK(CalibrationNativeHeadHeight({0,1.83f,0},{0,.95f,0},{0,0.f,0},{0,.9f,0})==0.f);    // one knee down
  CHECK(CalibrationNativeHeadHeight({0,1.83f,0},{0,.4f,0},{0,0.f,0},{0,.02f,0})==0.f);    // crouched pelvis
  CHECK(CalibrationNativeHeadHeight({0,1.15f,0},{0,.95f,0},{0,0.f,0},{0,.02f,0})==0.f);    // head not high enough
  CHECK(CalibrationNativeHeadHeight({0,NAN,0},{0,.95f,0},{0,0.f,0},{0,.02f,0})==0.f);     // non-finite
  CHECK(CalibrationNativeHeadHeight({0,1.83f,0},{0,.95f,0},{0,INFINITY,0},{0,.02f,0})==0.f);

  p.arm_reach_m[0]=0;
  CHECK(!ResolveCalibrationMapping(p,1.65f,false,1.f,.54f,3).valid);
  p={};
  const auto pending=ResolveCalibrationMapping(p,1.65f,false,1.5f);
  CHECK(pending.valid && !pending.fitted && near(pending.eye_height,(1.65f+kCalibrationHeightOffsetM)*1.5f));
  std::puts("PASS coherent automatic native-world/avatar fit (head and arm anchors), stature, seated, trim, lifetimes and refusals");
}
