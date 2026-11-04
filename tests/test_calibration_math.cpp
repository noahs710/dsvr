#include "../src/core/calibration_math.h"
#include <cstdio>
#include <limits>
#include <utility>
#define CHECK(x) do { if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
namespace {
dsvr::TrackingFrame Pose() {
  dsvr::TrackingFrame f;f.sequence=1;f.space_epoch=1;f.captured_ms=1000;
  f.focused=f.orientation_valid=f.position_valid=true;f.head_pos[1]=1.65f;
  for(int s=0;s<2;++s) {
    f.arm[s].valid=true;f.arm[s].pos[0]=s?.72f:-.72f;f.arm[s].pos[1]=1.4f;
  }
  return f;
}
void Step(dsvr::TrackingFrame& f) { ++f.sequence;f.captured_ms+=50; }
bool CaptureAtHeight(float height,dsvr::UserCalibration& result) {
  dsvr::CalibrationCapture capture;auto frame=Pose();
  frame.head_pos[1]=height;
  for(int side=0;side<2;++side) {
    frame.arm[side].pos[0]=side?.7364f:-.7364f;
    frame.arm[side].pos[1]=height-.2185f;
    frame.arm[side].quat[1]=.19866933f;
    frame.arm[side].quat[3]=.98006658f;
  }
  capture.Feed(frame,false,true,height,1.65f,result);Step(frame);
  for(int sample=0;sample<22;++sample) {
    if(capture.Feed(frame,true,true,height,1.65f,result))return true;
    Step(frame);
  }
  return false;
}
}
int main() {
  using namespace dsvr;
  // Identical head-relative poses represent the same arms when sitting or
  // standing. Floor-to-eye height must not change the shoulder estimate.
  UserCalibration seated,standing;
  CHECK(CaptureAtHeight(1.04f,seated) && CaptureAtHeight(1.65f,standing));
  CHECK(seated.floor_height_measured && standing.floor_height_measured);
  CHECK(std::fabs(seated.eye_height_m-1.04f)<1e-5f &&
        std::fabs(standing.eye_height_m-1.65f)<1e-5f);
  CHECK(std::fabs(seated.shoulder_half_width_m-standing.shoulder_half_width_m)<1e-5f);
  CHECK(std::fabs(seated.shoulder_half_width_m-.16706434f)<1e-5f);
  CHECK(std::fabs(seated.arm_span_m-standing.arm_span_m)<1e-5f);
  CHECK(std::fabs(seated.head_to_arm_m-standing.head_to_arm_m)<1e-5f);
  for(int side=0;side<2;++side) {
    CHECK(std::fabs(seated.arm_reach_m[side]-standing.arm_reach_m[side])<1e-5f);
    for(int axis=0;axis<4;++axis)
      CHECK(std::fabs(seated.neutral_controller[side][axis]-standing.neutral_controller[side][axis])<1e-5f);
  }
  CalibrationCapture capture;UserCalibration result;auto f=Pose();
  // A combined trigger release never advances the capture timer.
  for(int i=0;i<30;++i) { CHECK(!capture.Feed(f,false,false,0,1.65f,result));Step(f); }
  CHECK(!result.valid && capture.Progress()==0);
  Step(f);
  bool done=false;
  for(int i=0;i<22 && !done;++i) {
    // q and -q represent the same rotation; averaging must not cancel them.
    f.arm[0].quat[3]=(i%2)?-1.f:1.f;
    done=capture.Feed(f,true,false,0,1.65f,result);Step(f);
  }
  CHECK(done && CalibrationValidProfile(result));
  CHECK(!result.floor_height_measured && std::fabs(result.eye_height_m-1.65f)<1e-5f);
  CHECK(std::fabs(result.head_to_arm_m-.24002975f)<1e-5f && std::fabs(result.arm_span_m-1.4401785f)<1e-5f);
  CHECK(std::fabs(result.shoulder_half_width_m-.16322023f)<1e-5f);
  CHECK(std::fabs(result.arm_reach_m[0]-.55686903f)<1e-5f);
  CHECK(std::fabs(result.neutral_controller[0][3]-1)<1e-5f);

  capture.Reset();f=Pose();capture.Feed(f,false,true,1.2f,1.65f,result);Step(f);
  for(int i=0;i<12;++i) { CHECK(!capture.Feed(f,true,true,1.2f,1.65f,result));Step(f); }
  ++f.space_epoch;  // Never aggregate a pose across a tracking-space change.
  CHECK(!capture.Feed(f,true,true,1.2f,1.65f,result) && capture.Progress()==0);Step(f);
  done=false;
  for(int i=0;i<21 && !done;++i) { done=capture.Feed(f,true,true,1.2f,1.65f,result);Step(f); }
  CHECK(done && result.floor_height_measured && std::fabs(result.eye_height_m-1.2f)<1e-5f);
  // GetTickCount64 can repeat across distinct 90/120 Hz frames. Equal clock
  // ticks are coherent new samples, not a backwards-clock discontinuity.
  capture.Reset();f=Pose();capture.Feed(f,false,false,0,1.65f,result);
  done=false;
  for(int i=1;i<150 && !done;++i) {
    ++f.sequence;f.captured_ms=1000+static_cast<unsigned>(i*8/16)*16;
    done=capture.Feed(f,true,false,0,1.65f,result);
  }
  CHECK(done && CalibrationValidProfile(result));
  auto invalid=result;invalid.version=3;CHECK(!CalibrationValidProfile(invalid)); // v2 is current; unknown versions rejected
  invalid=result;invalid.neutral_controller[0][0]=std::numeric_limits<float>::quiet_NaN();
  CHECK(!CalibrationValidProfile(invalid));
  capture.Reset();f=Pose();f.arm[1].valid=false;
  capture.Feed(f,false,false,0,1.65f,result);Step(f);
  for(int i=0;i<25;++i) { CHECK(!capture.Feed(f,true,false,0,1.65f,result));Step(f); }
  CHECK(!capture.Ready());
  // Non-unit head orientation must not silently distort yaw-local geometry.
  capture.Reset();f=Pose();f.head_quat[3]=2;
  capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
  for(int i=0;i<22 && !done;++i) {done=capture.Feed(f,true,false,0,1.65f,result);Step(f);}
  CHECK(!done);
  // Accepted near-unit tracking quaternions describe rotations, not scale.
  capture.Reset();f=Pose();f.arm[0].quat[3]=.8f;
  capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
  for(int i=0;i<22 && !done;++i) {done=capture.Feed(f,true,false,0,1.65f,result);Step(f);}
  CHECK(done && std::fabs(result.neutral_controller[0][3]-1.f)<1e-5f);
  capture.Reset();f=Pose();f.head_quat[1]=f.head_quat[3]=std::sqrt(.5f)*.9f;
  for(auto& arm:f.arm) {const float x=arm.pos[0];arm.pos[0]=0;arm.pos[2]=-x;}
  capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
  for(int i=0;i<22 && !done;++i) {done=capture.Feed(f,true,false,0,1.65f,result);Step(f);}
  CHECK(done && std::fabs(result.arm_span_m-1.4401785f)<1e-5f);
  // Bounds accepted by individual samples must survive averaging and validation.
  for(float height:{.7f,2.3f}) {
    UserCalibration edge;CHECK(CaptureAtHeight(height,edge));
    CHECK(CalibrationValidProfile(edge) && edge.eye_height_m==height);
  }
  // Preserve the measured left/right lateral extensions instead of mirroring
  // the total span about the head. Shoulder sockets remain explicit estimates.
  capture.Reset();f=Pose();f.arm[0].pos[0]=-.65f;f.arm[1].pos[0]=.85f;
  capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
  for(int i=0;i<22 && !done;++i) {done=capture.Feed(f,true,false,0,1.65f,result);Step(f);}
  CHECK(done && CalibrationValidProfile(result));
  CHECK(std::fabs(result.arm_reach_m[0]-.47806084f)<1e-5f);
  CHECK(std::fabs(result.arm_reach_m[1]-.67805831f)<1e-5f);
  auto rejected=[](TrackingFrame frame) {
    CalibrationCapture c;UserCalibration out;c.Feed(frame,false,false,0,1.65f,out);Step(frame);
    for(int i=0;i<24;++i) {if(c.Feed(frame,true,false,0,1.65f,out))return false;Step(frame);}
    return true;
  };
  f=Pose();f.arm[0].quat[3]=0;CHECK(rejected(f));
  f=Pose();f.arm[1].quat[0]=NAN;CHECK(rejected(f));
  f=Pose();f.head_pos[0]=INFINITY;CHECK(rejected(f));
  f=Pose();f.arm[0].pos[1]=NAN;CHECK(rejected(f));
  // Backwards clocks and tracking gaps restart elapsed hold time.
  capture.Reset();f=Pose();capture.Feed(f,false,false,0,1.65f,result);Step(f);
  for(int i=0;i<10;++i) {capture.Feed(f,true,false,0,1.65f,result);Step(f);}
  f.captured_ms-=200;++f.sequence;CHECK(!capture.Feed(f,true,false,0,1.65f,result));
  CHECK(capture.Progress()==0);
  f.captured_ms+=151;++f.sequence;CHECK(!capture.Feed(f,true,false,0,1.65f,result));
  CHECK(capture.Progress()==0);
  // Slow tracking drift must not spend the whole stillness budget. The
  // operator's 31 Oct headset run restarted the hold near its end (progress
  // stuck at 0.42) because breathing alone drifts the head further than a
  // fixed 4 cm window measured from the first sample. 12 cm of drift spread
  // over the hold is tracking noise, not a moved pose, so it must complete.
  capture.Reset();f=Pose();done=false;
  capture.Feed(f,false,false,0,1.65f,result);Step(f);
  for(int i=0;i<22 && !done;++i) {
    f.head_pos[0]+=.005f;f.arm[0].pos[0]+=.005f;f.arm[1].pos[0]+=.005f;
    done=capture.Feed(f,true,false,0,1.65f,result);Step(f);
  }
  CHECK(done);
  // A deliberate move still trips the stability check: one step past the budget restarts
  // the hold and clears the bar.
  capture.Reset();f=Pose();
  capture.Feed(f,false,false,0,1.65f,result);Step(f);
    for(int i=0;i<8;++i) { capture.Feed(f,true,false,0,1.65f,result);Step(f); }
    CHECK(capture.Progress()>0.f);
    f.head_pos[0]+=.20f;
  CHECK(!capture.Feed(f,true,false,0,1.65f,result));
  CHECK(capture.Progress()==0);
  // A duplicate frame must not clear published progress: the render loop reads
  // the same tracking frame more than once per frame, and the on-screen bar
  // flickered back to zero ("progress bar glitching near the very end").
  capture.Reset();f=Pose();
  capture.Feed(f,false,false,0,1.65f,result);Step(f);
  for(int i=0;i<8;++i) { capture.Feed(f,true,false,0,1.65f,result);Step(f); }
  const float held=capture.Progress();
  CHECK(held>0.f && held<1.f);
  TrackingFrame dup=f;--dup.sequence;
  CHECK(!capture.Feed(dup,true,false,0,1.65f,result));
  CHECK(capture.Progress()==held);
  // Releasing either trigger clears the combined hold and its visible bar.
  CHECK(!capture.Feed(f,false,false,0,1.65f,result));
  CHECK(capture.Progress()==0);
  // Excessive grip jitter or a wrist swing beyond the 25 degree tolerance
  // restarts the hold without demanding a frozen, unnatural pose.
  for(bool angular:{false,true}) {
    capture.Reset();f=Pose();capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
    for(int i=0;i<24 && !done;++i) {
      if(angular) {f.arm[0].quat[0]=std::sin((i&1)?.25f:-.25f);f.arm[0].quat[3]=std::cos(.25f);}
      else f.arm[0].pos[0]=-.72f+(i&1)*.16f;
      done=capture.Feed(f,true,false,0,1.65f,result);Step(f);
    }
    CHECK(!done);
  }
  // Quaternion sign is only a representation change, and 2mm noise is stable.
  capture.Reset();f=Pose();capture.Feed(f,false,false,0,1.65f,result);Step(f);done=false;
  for(int i=0;i<24 && !done;++i) {
    f.arm[0].quat[3]=(i&1)?-1.f:1.f;f.arm[0].pos[0]=-.72f+(i&1)*.002f;
    done=capture.Feed(f,true,false,0,1.65f,result);Step(f);
  }
  CHECK(done && CalibrationValidProfile(result));
  UserCalibration bounds;bounds.valid=true;bounds.eye_height_m=.7f;
  bounds.head_to_arm_m=.08f;bounds.shoulder_half_width_m=.1f;bounds.arm_span_m=.7f;
  bounds.arm_reach_m[0]=.2f;bounds.arm_reach_m[1]=1.05f;CHECK(CalibrationValidProfile(bounds));
  bounds.eye_height_m=2.3f;bounds.head_to_arm_m=.6f;
  bounds.shoulder_half_width_m=.3f;bounds.arm_span_m=2.5f;CHECK(CalibrationValidProfile(bounds));
  bounds.arm_reach_m[1]=1.051f;CHECK(!CalibrationValidProfile(bounds));bounds.arm_reach_m[1]=1.05f;
  bounds.shoulder_half_width_m=.301f;CHECK(!CalibrationValidProfile(bounds));bounds.shoulder_half_width_m=.3f;
  bounds.arm_reach_m[0]=.199f;CHECK(!CalibrationValidProfile(bounds));
  bounds.arm_reach_m[0]=.2f;bounds.neutral_controller[1][3]=0;CHECK(!CalibrationValidProfile(bounds));
  // IK reach scaling guards unmeasurable asymmetry; the stored profile
  // keeps the raw measured reaches untouched.
  UserCalibration asym;asym.arm_reach_m[0]=.50f;asym.arm_reach_m[1]=.62f;
  CHECK(std::fabs(CalibrationReachForScale(asym,0)-.53f)<1e-5f);
  CHECK(std::fabs(CalibrationReachForScale(asym,1)-.59f)<1e-5f);
  CHECK(asym.arm_reach_m[0]==.50f && asym.arm_reach_m[1]==.62f);
  UserCalibration sym;sym.arm_reach_m[0]=sym.arm_reach_m[1]=.55f;
  CHECK(CalibrationReachForScale(sym,0)==.55f && CalibrationReachForScale(sym,1)==.55f);
  UserCalibration mild;mild.arm_reach_m[0]=.54f;mild.arm_reach_m[1]=.56f;
  CHECK(CalibrationReachForScale(mild,0)==.54f && CalibrationReachForScale(mild,1)==.56f);
  CHECK(CalibrationReachForScale(mild,7)==.55f);
  // Diagnostics expose the measured pose geometry behind Ready().
  { CalibrationCapture diag;UserCalibration out;auto pose=Pose();
    diag.Feed(pose,false,false,0,1.65f,out);
    CHECK(std::fabs(diag.LastSpanM()-1.44f)<1e-3f);
    CHECK(std::fabs(diag.LastPlaneM()-.25f)<1e-3f);
    CHECK(diag.LastHandsValid() && diag.Ready()); }
  // A relaxed A pose is recommended, but the exact hand spread and height do
  // not gate the one-second capture.
  {
    CalibrationCapture a;UserCalibration out;auto pose=Pose();
    pose.arm[0].pos[0]=-.55f;pose.arm[1].pos[0]=.55f;
    pose.arm[0].pos[1]=1.20f;pose.arm[1].pos[1]=1.20f; // head 1.65, plane .45
    a.Feed(pose,false,false,0,1.65f,out);Step(pose);
    bool relaxed_done=false;
    for(int i=0;i<22 && !relaxed_done;++i){relaxed_done=a.Feed(pose,true,false,0,1.65f,out);Step(pose);}
    CHECK(relaxed_done && CalibrationValidProfile(out));
    CHECK(std::fabs(out.arm_span_m-1.23446025f)<1e-5f);
    CalibrationCapture hips;auto hip=Pose();
    hip.arm[0].pos[0]=-.20f;hip.arm[1].pos[0]=.20f;
    hip.arm[0].pos[1]=.95f;hip.arm[1].pos[1]=.95f; // plane .70, span .40
    hips.Feed(hip,false,false,0,1.65f,out);Step(hip);
    bool hips_done=false;
    for(int i=0;i<22 && !hips_done;++i) {
      hips_done=hips.Feed(hip,true,false,0,1.65f,out);Step(hip);
    }
    CHECK(hips_done && CalibrationValidProfile(out));
  }
  // Seated guidance is useful, but the same tracked lap pose also remains
  // available to people who selected a different playstyle.
  {
    const auto lap_pose=[](){
      CalibrationCapture c;UserCalibration out;auto pose=Pose();
      pose.arm[0].pos[0]=-.175f;pose.arm[1].pos[0]=.175f;  // span .35
      pose.arm[0].pos[1]=1.20f;pose.arm[1].pos[1]=1.20f;   // head 1.65, plane .45
      pose.arm[0].pos[2]=.40f;pose.arm[1].pos[2]=.40f;     // hands forward
      c.Feed(pose,false,false,0,1.65f,out);Step(pose);
      bool lap_done=false;
      for(int i=0;i<22 && !lap_done;++i)
        {lap_done=c.Feed(pose,true,false,0,1.65f,out);Step(pose);}
      return std::pair{lap_done,out};
    };
    const auto lap_result=lap_pose();
    CHECK(lap_result.first && CalibrationValidProfile(lap_result.second));
    CHECK(std::fabs(lap_result.second.eye_height_m-1.65f)<1e-5f);
    const auto lap_again=lap_pose();
    CHECK(lap_again.first && CalibrationValidProfile(lap_again.second));
  }
  // Wrist pose never gates or normalizes the capture: a strongly bent wrist
  // completes the hold and its orientation is recorded verbatim in the neutral
  // grip, so the IK drives the game hand in exactly that bent pose (the hand
  // is 1:1 with the controller at any wrist angle, not just a flat grip).
  {
    CalibrationCapture bent;UserCalibration out;auto pose=Pose();
    for(auto& arm:pose.arm) {arm.quat[0]=.5f;arm.quat[3]=std::sqrt(.75f);}  // 60 deg wrist pitch
    bent.Feed(pose,false,false,0,1.65f,out);Step(pose);
    bool bent_done=false;
    for(int i=0;i<22 && !bent_done;++i){bent_done=bent.Feed(pose,true,false,0,1.65f,out);Step(pose);}
    CHECK(bent_done && CalibrationValidProfile(out));
    for(int side=0;side<2;++side) {
      CHECK(std::fabs(out.neutral_controller[side][0]-.5f)<1e-5f &&
            std::fabs(out.neutral_controller[side][3]-std::sqrt(.75f))<1e-5f);
      CHECK(std::fabs(out.neutral_controller[side][1])<1e-5f &&
            std::fabs(out.neutral_controller[side][2])<1e-5f);
    }
  }
  // The same idealized anatomy in T and A must produce the same scale inputs.
  // This asserts an independently constructed geometry, not capture's own math.
  for(float body_scale:{.8f,1.f,1.2f}) {
    for(float angle:{0.f,.4f,.785398163f}) {
      const float x=(.17f+.58f*std::cos(angle))*body_scale;
      const float y=-(.25f+.58f*std::sin(angle))*body_scale;
      const Vec3 controllers[2]{{-x,y,0},{x,y,0}};
      UserCalibration estimated;
      CHECK(CalibrationEstimateAnatomy(controllers,estimated));
      CHECK(std::fabs(estimated.shoulder_half_width_m-.17f*body_scale)<1e-5f);
      CHECK(std::fabs(estimated.head_to_arm_m-.25f*body_scale)<1e-5f);
      CHECK(std::fabs(estimated.arm_reach_m[0]-.58f*body_scale)<1e-5f);
      CHECK(std::fabs(estimated.arm_span_m-1.5f*body_scale)<1e-5f);
    }
  }
  std::puts("PASS stable T/A-pose capture, pose-independent anatomy estimates, timing/jitter/quaternion contracts and asymmetric reaches");
}
