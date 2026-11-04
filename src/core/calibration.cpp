#include "calibration.h"
#include "calibration_math.h"
#include "input.h"
#include "../common/logging.h"

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>

namespace dsvr {
namespace {
std::mutex g_mutex;
std::condition_variable g_changed;
// The DLL is process-lifetime. A joinable static std::thread would terminate
// the process during CRT shutdown, when the loader cannot safely join workers.
HANDLE g_worker=nullptr;
std::wstring g_path;
UserCalibration g_profile;
UserCalibration g_live_profile;
UserCalibration g_pending_profile;
CalibrationCapture g_capture;
CalibrationStatus g_status;
bool g_started=false,g_stop=false,g_loading=false,g_pending=false,g_requested=false,g_presentation_ready=false;
std::string g_play_style="standing";
std::uint64_t g_generation=0;
std::atomic<std::uint64_t> g_recenter{0};
float g_session_yaw[4]={0,0,0,1};
std::uint64_t g_session_epoch=0;
bool g_session_yaw_valid=false;

struct RefinementWindow {
  std::uint64_t epoch=0,last_sequence=0,started_ms=0,last_ms=0;
  unsigned samples=0;
  Vec3 head{},hand[2]{};
  float head_yaw[4]{},baseline_quat[2][4]{},sum_quat[2][4]{};
  void Reset() { *this=RefinementWindow{}; }
} g_refinement;

float QDot(const float a[4],const float b[4]) {
  float dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];return dot;
}
void QBlend(const float from[4],const float to[4],float amount,float out[4]) {
  float b[4];for(int i=0;i<4;++i)b[i]=to[i];
  float dot=QDot(from,b);
  if(dot<0) {dot=-dot;for(float& v:b)v=-v;}
  dot=std::clamp(dot,-1.f,1.f);
  if(dot>.9995f) {
    for(int i=0;i<4;++i)out[i]=from[i]+(b[i]-from[i])*amount;
  } else {
    const float theta=std::acos(dot),sin_theta=std::sin(theta);
    const float a=std::sin((1-amount)*theta)/sin_theta;
    const float c=std::sin(amount*theta)/sin_theta;
    for(int i=0;i<4;++i)out[i]=from[i]*a+b[i]*c;
  }
  float normalized[4]{};
  if(calibration_detail::UnitQuaternion(out,normalized))
    for(int i=0;i<4;++i)out[i]=normalized[i];
}
void InverseMultiply(const float a[4],const float b[4],float out[4]) {
  const float inverse[4]={-a[0],-a[1],-a[2],a[3]};
  calibration_detail::Multiply(inverse,b,out);
}
bool InNeutralRegion(const Vec3 positions[2]) {
  const float span=VLen(VAdd(positions[1],VScale(positions[0],-1)));
  for(int side=0;side<2;++side) {
    const auto& p=positions[side];
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z) ||
       std::fabs(p.x)>.8f || p.y < -1.2f || p.y > .25f || std::fabs(p.z)>.9f)
      return false;
  }
  return span>.12f && span<1.5f;
}
void SetPreview(const TrackingFrame& frame) {
  g_status.hand_preview_valid[0]=g_status.hand_preview_valid[1]=false;
  float yaw[4]{};
  if(!g_session_yaw_valid || !calibration_detail::UnitQuaternion(g_session_yaw,yaw))return;
  const float inv[4]={-yaw[0],-yaw[1],-yaw[2],yaw[3]};
  for(int side=0;side<2;++side) {
    const auto& hand=frame.arm[side];float q[4]{},relative[4]{};
    if(!hand.valid || !hand.tracking_flags_available || !hand.position_tracked ||
       !hand.orientation_tracked || !calibration_detail::UnitQuaternion(hand.quat,q))continue;
    calibration_detail::Multiply(inv,q,relative);
    const auto up=QuatRotate({0,1,0},relative[0],relative[1],relative[2],relative[3]);
    const float angle=std::atan2(up.x,up.y);
    if(!std::isfinite(angle))continue;
    const Vec3 delta{hand.pos[0]-frame.head_pos[0],hand.pos[1]-frame.head_pos[1],
                     hand.pos[2]-frame.head_pos[2]};
    const auto position=QuatRotate(delta,inv[0],inv[1],inv[2],inv[3]);
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))continue;
    g_status.hand_preview_position[side][0]=std::clamp(position.x/.8f,-1.f,1.f);
    g_status.hand_preview_position[side][1]=std::clamp((-position.y-.12f)/1.05f,0.f,1.f);
    g_status.hand_preview_angle[side]=angle;
    g_status.hand_preview_valid[side]=true;
  }
}

void RefineFromStableWindow(const TrackingFrame& frame,bool safe) {
  if(!safe || g_requested || g_loading || !g_profile.valid ||
     g_status.refinement_review_needed) {
    g_refinement.Reset();return;
  }
  if(!frame.focused || !frame.orientation_valid || !frame.position_valid ||
     (frame.head_tracking_flags_available &&
      (!frame.head_orientation_tracked || !frame.head_position_tracked))) {
    g_refinement.Reset();return;
  }
  if(!g_session_yaw_valid || frame.space_epoch!=g_session_epoch) {
    g_refinement.Reset();return;
  }
  float session_yaw[4]{};
  if(!calibration_detail::UnitQuaternion(g_session_yaw,session_yaw)) {
    g_refinement.Reset();return;
  }
  const float inverse_session[4]={-session_yaw[0],-session_yaw[1],-session_yaw[2],session_yaw[3]};
  float headq[4]{},head_yaw[4]{};
  if(!calibration_detail::UnitQuaternion(frame.head_quat,headq)) {g_refinement.Reset();return;}
  CaptureYawReference(headq,head_yaw);
  const float inverse_head_yaw[4]={-head_yaw[0],-head_yaw[1],-head_yaw[2],head_yaw[3]};
  Vec3 positions[2];float quats[2][4]{};
  for(int side=0;side<2;++side) {
    const auto& hand=frame.arm[side];float q[4]{};
    if(!hand.valid || !hand.tracking_flags_available || !hand.position_tracked ||
       !hand.orientation_tracked || !calibration_detail::UnitQuaternion(hand.quat,q)) {
      g_refinement.Reset();return;
    }
    Vec3 delta{hand.pos[0]-frame.head_pos[0],hand.pos[1]-frame.head_pos[1],hand.pos[2]-frame.head_pos[2]};
    positions[side]=QuatRotate(delta,inverse_head_yaw[0],inverse_head_yaw[1],inverse_head_yaw[2],inverse_head_yaw[3]);
    calibration_detail::Multiply(inverse_session,q,quats[side]);
  }
  if(!InNeutralRegion(positions)) {g_refinement.Reset();return;}
  auto& state=g_refinement;
  if(!state.last_sequence || state.epoch!=frame.space_epoch || frame.sequence<=state.last_sequence ||
     frame.captured_ms<state.last_ms || frame.captured_ms-state.last_ms>150) {
    state.Reset();state.epoch=frame.space_epoch;state.last_sequence=frame.sequence;
    state.started_ms=state.last_ms=frame.captured_ms;
    state.head={frame.head_pos[0],frame.head_pos[1],frame.head_pos[2]};
    for(int i=0;i<4;++i)state.head_yaw[i]=head_yaw[i];
    for(int side=0;side<2;++side) {
      state.hand[side]=positions[side];
      for(int i=0;i<4;++i)state.baseline_quat[side][i]=quats[side][i];
    }
    return;
  }
  state.last_sequence=frame.sequence;
  if(VLen(VAdd(Vec3{frame.head_pos[0],frame.head_pos[1],frame.head_pos[2]},VScale(state.head,-1)))>.08f ||
     std::fabs(QDot(state.head_yaw,head_yaw))<.9763f) {state.Reset();return;}
  for(int side=0;side<2;++side) {
    if(VLen(VAdd(positions[side],VScale(state.hand[side],-1)))>.08f ||
       std::fabs(QDot(state.baseline_quat[side],quats[side]))<.9763f) {state.Reset();return;}
  }
  state.last_ms=frame.captured_ms;
  ++state.samples;
  for(int side=0;side<2;++side) {
    const float sign=QDot(state.baseline_quat[side],quats[side])<0?-1.f:1.f;
    for(int i=0;i<4;++i)state.sum_quat[side][i]+=quats[side][i]*sign;
  }
  if(frame.captured_ms-state.started_ms<1000 || state.samples<20)return;
  float target[2][4]{};bool complete=true;
  for(int side=0;side<2;++side) {
    float average[4]{};
    for(int i=0;i<4;++i)average[i]=state.sum_quat[side][i]/static_cast<float>(state.samples);
    complete=complete&&calibration_detail::UnitQuaternion(average,target[side]);
  }
  if(!complete) {state.Reset();return;}
  float existing[2][4]{},angle[2]{};
  for(int side=0;side<2;++side) {
    float absolute[4]{};
    const float* old_yaw=g_live_profile.hand_alignment_valid?
        g_live_profile.alignment_reference_yaw:nullptr;
    if(old_yaw)calibration_detail::Multiply(old_yaw,g_live_profile.neutral_controller[side],absolute);
    else for(int i=0;i<4;++i)absolute[i]=(i==3)?1.f:0.f;
    InverseMultiply(session_yaw,absolute,existing[side]);
    const float dot=std::clamp(std::fabs(QDot(existing[side],target[side])),0.f,1.f);
    angle[side]=2.f*std::acos(dot)*57.2957795f;
    if(g_live_profile.hand_alignment_valid && angle[side]>45.f) {
      g_status.refinement_review_needed=true;
      g_refinement.Reset();return;
    }
  }
  for(int side=0;side<2;++side) {
    const float max_step=g_live_profile.hand_alignment_valid?2.f:5.f;
    const float amount=angle[side]>1e-3f?std::min(.08f,max_step/angle[side]):0.f;
    QBlend(existing[side],target[side],amount,g_live_profile.neutral_controller[side]);
  }
  for(int i=0;i<4;++i)g_live_profile.alignment_reference_yaw[i]=session_yaw[i];
  g_live_profile.hand_alignment_valid=true;
  g_refinement.Reset();
}

bool Load(const std::wstring& path,UserCalibration& profile) {
  std::ifstream in{std::filesystem::path(path)};
  in.imbue(std::locale::classic());
  std::string magic;UserCalibration p;int floor=0;
  if (!(in>>magic>>p.version) || magic!="DSVR_CALIBRATION" ||
      (p.version!=1 && p.version!=2)) return false;
  if (!(in>>floor>>p.eye_height_m>>p.head_to_arm_m>>p.shoulder_half_width_m>>
        p.arm_reach_m[0]>>p.arm_reach_m[1]>>p.arm_span_m)) return false;
  if(p.version==2) {
    int alignment=0;
    if(!(in>>alignment) || (alignment!=0&&alignment!=1))return false;
    p.hand_alignment_valid=alignment==1;
    for(float& v:p.alignment_reference_yaw)if(!(in>>v))return false;
    for (auto& q:p.neutral_controller)for(float& v:q)if(!(in>>v))return false;
  } else {
    // Version 1 did not persist the yaw frame needed to interpret controller
    // alignment after a later gameplay camera bind. Keep anatomy and recapture
    // wrist alignment from a known reference.
    float legacy[2][4]{};
    for(auto& q:legacy)for(float& v:q)if(!(in>>v))return false;
    p.version=2;p.hand_alignment_valid=false;
  }
  std::string trailing;if(in>>trailing)return false;
  p.floor_height_measured=floor==1;p.valid=true;
  if ((floor!=0 && floor!=1) || !CalibrationValidProfile(p)) return false;
  profile=p;return true;
}
bool Save(const std::wstring& path,const UserCalibration& p) {
  if (!CalibrationValidProfile(p) || path.empty()) return false;
  const std::wstring temp=path+L".tmp";
  {
    std::ofstream out{std::filesystem::path(temp),std::ios::trunc};
    out.imbue(std::locale::classic());
    out<<std::setprecision(9)<<"DSVR_CALIBRATION "<<p.version<<'\n'<<
        (p.floor_height_measured?1:0)<<' '<<p.eye_height_m<<' '<<p.head_to_arm_m<<' '<<
        p.shoulder_half_width_m<<' '<<p.arm_reach_m[0]<<' '<<p.arm_reach_m[1]<<' '<<p.arm_span_m<<'\n';
    out<<(p.hand_alignment_valid?1:0)<<' ';
    for(float v:p.alignment_reference_yaw)out<<v<<' ';
    out<<'\n';
    for(const auto& q:p.neutral_controller) { for(float v:q)out<<v<<' ';out<<'\n'; }
    out.flush();if(!out)return false;
    out.close();if(!out)return false;
  }
  return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
void RefreshStatus() {
  g_status.profile_valid=g_profile.valid;g_status.hand_alignment_valid=g_live_profile.hand_alignment_valid;
  g_status.hand_alignment_saved=g_profile.hand_alignment_valid;
  g_status.can_cancel=g_profile.valid&&!g_status.saving;
  g_status.screen_visible=(g_loading || g_requested) && g_presentation_ready;
  g_status.capture_input=g_status.screen_visible;
  if(g_loading)g_status.phase=CalibrationPhase::Loading;
  else if(!g_requested)g_status.phase=g_profile.valid?CalibrationPhase::Ready:CalibrationPhase::WaitingForTracking;
  else if(!g_presentation_ready)g_status.phase=CalibrationPhase::WaitingForTracking;
  else g_status.phase=g_status.hold_progress>0?CalibrationPhase::Holding:CalibrationPhase::AwaitingPose;
  const bool seated=g_play_style=="seated";
  if(g_status.phase==CalibrationPhase::Loading)g_status.instruction="Preparing your calibration";
  else if(!g_status.screen_visible)g_status.instruction=g_profile.valid?"Calibration saved":"Waiting for tracking";
  else if(g_status.saving)g_status.instruction="Saving your measurements";
  else if(g_status.save_failed)g_status.instruction="Could not save. Your old measurements still work. Release both triggers, then try again";
  else if(!g_status.pose_ready)g_status.instruction="Bring both controllers into view, then hold both triggers for one second";
  else if(!g_status.hold_progress)g_status.instruction="Ready. Hold both triggers for one second";
  else g_status.instruction="Measuring your reach and hand angle";
  // Offer a useful playstyle-specific pose without making it a capture gate.
  g_status.pose_hint="";
  if(g_status.screen_visible && !g_status.saving && g_capture.LastHandsValid()) {
    if(seated)g_status.pose_hint="Recommended: rest your hands comfortably on your lap";
    else if(g_play_style=="roomscale")g_status.pose_hint="Recommended: keep your hands relaxed and slightly out from your sides";
    else g_status.pose_hint="Recommended: a relaxed A shape gives the clearest reach estimate";
  } else if(g_status.screen_visible && !g_status.pose_ready && !g_status.saving) {
    g_status.pose_hint="Keep both controllers visible; any comfortable pose works";
  }
  if(g_status.screen_visible && g_status.refinement_review_needed)
    g_status.pose_hint="Hand alignment needs a fresh one-second hold; the saved fit is unchanged";
  else if(g_status.screen_visible && !g_status.hand_alignment_valid && !g_status.saving &&
          g_status.pose_ready)
    g_status.pose_hint="Live fit is settling gently; hold both triggers to save a precise alignment";
}
void Worker() {
  std::wstring path;
  { std::lock_guard<std::mutex> lock(g_mutex);path=g_path; }
  UserCalibration loaded;const bool have=Load(path,loaded);
  std::unique_lock<std::mutex> lock(g_mutex);
  if(have) { loaded.revision=++g_generation;g_profile=loaded;g_live_profile=loaded; }
  // A new process always runs setup once. Loading a saved profile provides
  // recovery/cancel state; deaths and scene changes never request another capture.
  g_loading=false;g_requested=true;RefreshStatus();
  for(;;) {
    g_changed.wait(lock,[]{return g_stop||g_pending;});
    if(!g_pending && g_stop)break;
    const auto profile=g_pending_profile;path=g_path;g_pending=false;
    lock.unlock();const bool ok=Save(path,profile);lock.lock();
    g_status.saving=false;g_status.save_failed=!ok;
    if(ok) {g_profile=profile;g_live_profile=profile;g_requested=false;
      g_status.refinement_review_needed=false;g_refinement.Reset();++g_recenter;}
    g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;
    RefreshStatus();
  }
}
}

void CalibrationStart(const std::wstring& path) {
  CalibrationStop();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_path=path;g_profile={};g_live_profile={};g_capture.Reset();g_status={};
  g_session_yaw[0]=g_session_yaw[1]=g_session_yaw[2]=0;g_session_yaw[3]=1;
  g_session_epoch=0;g_session_yaw_valid=false;g_refinement.Reset();
  g_stop=false;g_pending=false;g_requested=false;g_presentation_ready=false;g_loading=true;g_started=true;
  RefreshStatus();
  g_worker=CreateThread(nullptr,0,[](LPVOID)->DWORD {Worker();return 0;},nullptr,0,nullptr);
  if(!g_worker) {g_loading=false;g_requested=true;g_status.save_failed=true;RefreshStatus();}
}
void CalibrationStop() {
  { std::lock_guard<std::mutex> lock(g_mutex);g_stop=true;g_changed.notify_one(); }
  if(g_worker) {WaitForSingleObject(g_worker,INFINITE);CloseHandle(g_worker);g_worker=nullptr;}
  std::lock_guard<std::mutex> lock(g_mutex);
  g_started=false;g_loading=false;g_requested=false;g_presentation_ready=false;g_capture.Reset();
  g_refinement.Reset();g_session_yaw_valid=false;
  g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationRequest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started || g_stop || g_status.saving)return;
  g_requested=true;g_capture.Reset();
  g_refinement.Reset();g_status.refinement_review_needed=false;
  g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationCancel() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_profile.valid || g_status.saving)return;
  g_requested=false;g_live_profile=g_profile;g_capture.Reset();g_refinement.Reset();
  g_status.refinement_review_needed=false;g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationRecenter() { ++g_recenter; }
std::uint64_t CalibrationRecenterGeneration() { return g_recenter.load(); }
UserCalibration CalibrationCurrent() { std::lock_guard<std::mutex> lock(g_mutex);return g_live_profile; }
CalibrationStatus CalibrationGetStatus() { std::lock_guard<std::mutex> lock(g_mutex);return g_status; }
bool CalibrationCapturesInput() { std::lock_guard<std::mutex> lock(g_mutex);return g_status.capture_input; }
void CalibrationPresentationReady(bool ready) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started)return;
  g_presentation_ready=ready;
  if(!ready) {g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;}
  RefreshStatus();
}
void CalibrationSample(const TrackingFrame& frame,bool both_triggers_held,bool presentation_ready,bool floor_valid,
                       float floor_height,float fallback_height,const char* play_style) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started)return;
  if(play_style && g_play_style!=play_style) g_play_style=play_style;
  g_presentation_ready=presentation_ready;
  if(frame.space_epoch && frame.focused && frame.orientation_valid) {
    if(g_session_epoch!=frame.space_epoch) {
      g_session_epoch=frame.space_epoch;g_session_yaw_valid=false;g_refinement.Reset();
    }
    if(!g_session_yaw_valid &&
       (!frame.head_tracking_flags_available || frame.head_orientation_tracked)) {
      float head[4]{};
      if(calibration_detail::UnitQuaternion(frame.head_quat,head)) {
        CaptureYawReference(head,g_session_yaw);g_session_yaw_valid=true;
      }
    }
  }
  SetPreview(frame);
  // Bounded once-per-second capture state while setup is requested, so a
  // hold that never starts is diagnosable from the log (tracking readiness,
  // simultaneous trigger state, room presentation, or progress resets).
  static ULONGLONG last_state_log=0;
  if(g_requested && !g_loading) {
    const ULONGLONG now_log=GetTickCount64();
    if(now_log-last_state_log>=1000) {
      last_state_log=now_log;
      const Vec3 lp=g_capture.LastPosition(0),rp=g_capture.LastPosition(1);
      DSVR_LOG("Calibration: sample pose_ready=%d progress=%.2f both_triggers=%d "
               "presentation=%d saving=%d span=%.2f plane=%.2f hands=%d "
               "L=(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f) hint=%s",
               (int)g_status.pose_ready,(double)g_status.hold_progress,
                (int)both_triggers_held,(int)presentation_ready,(int)g_status.saving,
               (double)g_capture.LastSpanM(),(double)g_capture.LastPlaneM(),
               (int)g_capture.LastHandsValid(),
               (double)lp.x,(double)lp.y,(double)lp.z,
               (double)rp.x,(double)rp.y,(double)rp.z,
               g_status.pose_hint);
    }
  }
  if(!g_requested) {
    RefineFromStableWindow(frame,InputCalibrationRefinementSafe() && presentation_ready);
    RefreshStatus();
    return;
  }
  if(g_loading || !presentation_ready || g_status.saving) {
    g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();return;
  }
  UserCalibration completed;
  const bool complete=g_capture.Feed(frame,both_triggers_held,floor_valid,floor_height,fallback_height,
                                      completed,g_session_yaw_valid?g_session_yaw:nullptr);
  g_status.pose_ready=g_capture.Ready();g_status.hold_progress=g_capture.Progress();
  if(complete) {
    if(!g_worker) {g_status.save_failed=true;RefreshStatus();return;}
    completed.revision=++g_generation;g_pending_profile=completed;
    g_status.hold_progress=0;g_status.saving=true;g_status.save_failed=false;
    g_pending=true;g_changed.notify_one();
  }
  RefreshStatus();
}
}  // namespace dsvr
