#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "calibration.h"
#include "tracking_frame.h"
#include "camera_math.h"

namespace dsvr {

inline bool CalibrationValidProfile(const UserCalibration& p) {
  if (p.version != 1 || !p.valid || !std::isfinite(p.eye_height_m) ||
      p.eye_height_m < .7f || p.eye_height_m > 2.3f ||
      !std::isfinite(p.head_to_arm_m) || p.head_to_arm_m < .08f || p.head_to_arm_m > .6f ||
      !std::isfinite(p.shoulder_half_width_m) || p.shoulder_half_width_m < .1f ||
      p.shoulder_half_width_m > .3f || !std::isfinite(p.arm_span_m) ||
      p.arm_span_m < .7f || p.arm_span_m > 2.5f) return false;
  for (int side=0;side<2;++side) {
    if (!std::isfinite(p.arm_reach_m[side]) || p.arm_reach_m[side] < .2f ||
        p.arm_reach_m[side] > 1.05f) return false;
    float norm=0;
    for (float v:p.neutral_controller[side]) {
      if (!std::isfinite(v)) return false;
      norm+=v*v;
    }
    if (std::fabs(norm-1)>1e-3f) return false;
  }
  return true;
}

// A single arms-out capture cannot separate real arm-length asymmetry
// from pose sloppiness (one arm held a few centimetres lower or further
// forward reads as a longer reach, and the arms then feel uneven in IK).
// The profile preserves the raw measured reaches; only the IK reach
// scaling consumes this guarded value, with per-side deviation from the
// mean clamped to a plausible anatomical bound.
inline constexpr float kArmReachAsymmetryLimitM = 0.03f;
inline float CalibrationReachForScale(const UserCalibration& p, int side) {
  const float mean = (p.arm_reach_m[0] + p.arm_reach_m[1]) * .5f;
  if (!std::isfinite(mean)) return 0.0f;
  if (side < 0 || side > 1 || !std::isfinite(p.arm_reach_m[side])) return mean;
  const float deviation = std::clamp(p.arm_reach_m[side] - mean,
                                     -kArmReachAsymmetryLimitM,
                                     kArmReachAsymmetryLimitM);
  return mean + deviation;
}

namespace calibration_detail {
inline bool UnitQuaternion(const float in[4], float out[4]) {
  float norm=0;
  for (int i=0;i<4;++i) { if (!std::isfinite(in[i])) return false; norm+=in[i]*in[i]; }
  if (norm < .5f || norm > 1.5f) return false;
  const float inv=1/std::sqrt(norm);
  for (int i=0;i<4;++i) out[i]=in[i]*inv;
  return true;
}
inline void Multiply(const float a[4],const float b[4],float out[4]) {
  out[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
  out[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
  out[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
  out[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
}
}

// One capture cannot locate shoulder joints. Fit the explicit anthropometric
// proportions already used by setup to the full head-relative controller
// vectors, not just their horizontal spread. In an A pose a lowered hand is
// not a shorter arm, nor is the hand plane the shoulder plane. Floor height
// never enters this estimate, so sitting cannot shrink the user's anatomy.
inline bool CalibrationEstimateAnatomy(const Vec3 positions[2], UserCalibration& out) {
  if (!positions || !std::isfinite(positions[0].x) || !std::isfinite(positions[0].y) ||
      !std::isfinite(positions[0].z) || !std::isfinite(positions[1].x) ||
      !std::isfinite(positions[1].y) || !std::isfinite(positions[1].z)) return false;
  constexpr float shoulder=.17f, head_to_shoulder=.25f, reach=.58f;
  const float lateral=(positions[1].x-positions[0].x)*.5f;
  const float down=-(positions[0].y+positions[1].y)*.5f;
  const float radius2=(VDot(positions[0],positions[0])+VDot(positions[1],positions[1]))*.5f;
  const float a=reach*reach-shoulder*shoulder-head_to_shoulder*head_to_shoulder;
  const float b=lateral*shoulder+down*head_to_shoulder;
  // a*k^2 + 2*b*k - radius2 = 0. This form avoids subtractive cancellation.
  const float divisor=std::sqrt(b*b+a*radius2)+b;
  if (!(divisor>1e-6f) || !std::isfinite(divisor)) return false;
  const float k=std::clamp(radius2/divisor,.12f/shoulder,.25f/shoulder);
  UserCalibration candidate=out;
  candidate.shoulder_half_width_m=shoulder*k;
  candidate.head_to_arm_m=head_to_shoulder*k;
  candidate.arm_span_m=2*candidate.shoulder_half_width_m;
  for (int side=0;side<2;++side) {
    const Vec3 delta{positions[side].x-(side?1.f:-1.f)*candidate.shoulder_half_width_m,
                     positions[side].y+candidate.head_to_arm_m,positions[side].z};
    candidate.arm_reach_m[side]=VLen(delta);
    if (!std::isfinite(candidate.arm_reach_m[side]) || candidate.arm_reach_m[side]<.2f ||
        candidate.arm_reach_m[side]>1.05f) return false;
    candidate.arm_span_m+=candidate.arm_reach_m[side];
  }
  out=candidate;return true;
}

// A one-second stable T/A pose observes controller geometry and estimates
// anatomy. Shoulder sockets and upper/forearm split are unobservable from one
// pose; the IK retains native segment proportions, not invented bone scales.
class CalibrationCapture {
 public:
  void Reset() { *this=CalibrationCapture{}; }
  bool Feed(const TrackingFrame& f,bool trigger,bool floor_valid,float floor_height,
            float fallback_height,UserCalibration& completed) {
    ready_=false; progress_=0; last_hands_valid_=false;
    if (!f.sequence || !f.space_epoch || !f.focused || !f.orientation_valid ||
        !f.position_valid || !f.captured_ms || !std::isfinite(fallback_height)) {
      RestartHold(); return false;
    }
    if (last_sequence_ && f.sequence<=last_sequence_ && f.space_epoch==epoch_) return false;
    if (epoch_!=f.space_epoch || (last_time_ &&
        (f.captured_ms<last_time_ || f.captured_ms-last_time_>150))) RestartHold();
    last_sequence_=f.sequence; epoch_=f.space_epoch; last_time_=f.captured_ms;
    float headq[4]{},yaw[4]{};
    if (!calibration_detail::UnitQuaternion(f.head_quat,headq)) { RestartHold();return false; }
    CaptureYawReference(headq,yaw);
    const float inverse_yaw[4]={-yaw[0],-yaw[1],-yaw[2],yaw[3]};
    Vec3 positions[2];float quats[2][4]{};
    for (int side=0;side<2;++side) {
      const auto& hand=f.arm[side];float q[4]{};
      if (!hand.valid || !calibration_detail::UnitQuaternion(hand.quat,q)) { RestartHold();return false; }
      Vec3 delta{hand.pos[0]-f.head_pos[0],hand.pos[1]-f.head_pos[1],hand.pos[2]-f.head_pos[2]};
      if (!std::isfinite(delta.x)||!std::isfinite(delta.y)||!std::isfinite(delta.z)) { RestartHold();return false; }
      positions[side]=QuatRotate(delta,inverse_yaw[0],inverse_yaw[1],inverse_yaw[2],inverse_yaw[3]);
      calibration_detail::Multiply(inverse_yaw,q,quats[side]);
    }
    const float plane=-(positions[0].y+positions[1].y)*.5f;
    const float span=positions[1].x-positions[0].x;
    last_span_=span; last_plane_=plane; last_hands_valid_=true;
    last_positions_[0]=positions[0]; last_positions_[1]=positions[1];
    // Two accepted poses: a full T (arms sideways) and a relaxed A
    // (arms down-out at roughly 45 degrees). The A rejects hands held
    // down by the hips (plane too large / span too small) and hands
    // raised into a T (that is the T branch). Bounds stay strict enough
    // that a random stance cannot silently capture bad measurements.
    // Wider gates than the first pass: users hold controllers, not bare
      // hands, so exact symmetry is harder than the old bounds assumed.
      const bool t_pose=positions[0].x<-.28f && positions[1].x>.28f &&
          std::fabs(positions[0].y-positions[1].y)<.16f && plane>=.05f &&
        std::fabs(positions[0].z)<.35f && std::fabs(positions[1].z)<.35f;
      const bool a_pose=positions[0].x<-.22f && positions[1].x>.22f &&
          std::fabs(positions[0].y-positions[1].y)<.18f && plane>.22f &&
        std::fabs(positions[0].z)<.40f && std::fabs(positions[1].z)<.40f;
    ready_=span>=.6f && span<=2.5f && plane<=.65f && (t_pose || a_pose);
    if (!trigger) { released_=true; RestartHold();return false; }
    if (!ready_ || !released_) { RestartHold();return false; }
    if (!started_) {
      started_=f.captured_ms; samples_=0;
      for(auto& position:sum_position_) position={};
      baseline_head_={f.head_pos[0],f.head_pos[1],f.head_pos[2]};
      for(int i=0;i<4;++i)baseline_head_q_[i]=headq[i];
      for (int s=0;s<2;++s) {
        baseline_[s]=positions[s];
        for (int i=0;i<4;++i) { baseline_q_[s][i]=quats[s][i]; sum_q_[s][i]=0; }
      }
      floor_sum_=0;floor_samples_=0;
    }
    const Vec3 head{f.head_pos[0],f.head_pos[1],f.head_pos[2]};
    float head_dot=0;for(int i=0;i<4;++i)head_dot+=baseline_head_q_[i]*headq[i];
    if(VLen(VAdd(head,VScale(baseline_head_,-1)))>.04f || std::fabs(head_dot)<.9914f) {
      RestartHold();return false;
    }
    for (int s=0;s<2;++s) {
      if (VLen(VAdd(positions[s],VScale(baseline_[s],-1)))>.04f) { RestartHold();return false; }
      float dot=0;for(int i=0;i<4;++i)dot+=baseline_q_[s][i]*quats[s][i];
      if (std::fabs(dot)<.9914f) { RestartHold();return false; }  // 15 degrees
      for (int i=0;i<4;++i) sum_q_[s][i]+=quats[s][i]*(dot<0?-1.f:1.f);
    }
    ++samples_;
    for(int side=0;side<2;++side) {
      sum_position_[side][0]+=positions[side].x;
      sum_position_[side][1]+=positions[side].y;
      sum_position_[side][2]+=positions[side].z;
    }
    if (floor_valid && std::isfinite(floor_height) && floor_height>=.7f && floor_height<=2.3f) {
      floor_sum_+=floor_height;++floor_samples_;
    }
    progress_=std::min(1.f,static_cast<float>(f.captured_ms-started_)/1000.f);
    if (progress_<1 || samples_<20) return false;
    UserCalibration result;
    result.valid=true;result.floor_height_measured=floor_samples_==samples_;
    result.eye_height_m=result.floor_height_measured?static_cast<float>(floor_sum_/floor_samples_):fallback_height;
    Vec3 average_position[2];
    for(int side=0;side<2;++side)
      average_position[side]={static_cast<float>(sum_position_[side][0]/samples_),
                              static_cast<float>(sum_position_[side][1]/samples_),
                              static_cast<float>(sum_position_[side][2]/samples_)};
    if (!CalibrationEstimateAnatomy(average_position,result)) {RestartHold();return false;}
    for (int s=0;s<2;++s) {
      float average[4];for(int i=0;i<4;++i)average[i]=sum_q_[s][i]/static_cast<float>(samples_);
      if (!calibration_detail::UnitQuaternion(average,result.neutral_controller[s])) { RestartHold();return false; }
    }
    if (!CalibrationValidProfile(result)) { RestartHold();return false; }
    completed=result;released_=false;RestartHold();return true;
  }
  bool Ready() const { return ready_; }
  float Progress() const { return progress_; }
  bool Released() const { return released_; }
  // Last measured T-pose geometry, for diagnostics when a hold will not
  // start (which acceptance bound is failing is otherwise invisible).
  float LastSpanM() const { return last_span_; }
  float LastPlaneM() const { return last_plane_; }
  bool LastHandsValid() const { return last_hands_valid_; }
  Vec3 LastPosition(int side) const { return last_positions_[side ? 1 : 0]; }
 private:
  void RestartHold() { started_=0;samples_=0;progress_=0; }
  std::uint64_t last_sequence_=0,epoch_=0,last_time_=0,started_=0;
  bool ready_=false,released_=false;
  float last_span_=0,last_plane_=0; bool last_hands_valid_=false;
  Vec3 last_positions_[2]{};
  unsigned samples_=0,floor_samples_=0;
  float progress_=0;
  double floor_sum_=0;
  std::array<double,3> sum_position_[2]{};
  Vec3 baseline_[2]{};
  Vec3 baseline_head_{};
  float baseline_head_q_[4]{};
  float baseline_q_[2][4]{},sum_q_[2][4]{};
};

}  // namespace dsvr
