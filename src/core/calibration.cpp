#include "calibration.h"
#include "calibration_math.h"
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
UserCalibration g_pending_profile;
CalibrationCapture g_capture;
CalibrationStatus g_status;
bool g_started=false,g_stop=false,g_loading=false,g_pending=false,g_requested=false,g_gameplay=false;
std::uint64_t g_generation=0;
std::atomic<std::uint64_t> g_recenter{0};

bool Load(const std::wstring& path,UserCalibration& profile) {
  std::ifstream in{std::filesystem::path(path)};
  in.imbue(std::locale::classic());
  std::string magic;UserCalibration p;int floor=0;
  if (!(in>>magic>>p.version) || magic!="DSVR_CALIBRATION" || p.version!=1) return false;
  if (!(in>>floor>>p.eye_height_m>>p.head_to_arm_m>>p.shoulder_half_width_m>>
        p.arm_reach_m[0]>>p.arm_reach_m[1]>>p.arm_span_m)) return false;
  for (auto& q:p.neutral_controller)for(float& v:q)if(!(in>>v))return false;
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
    for(const auto& q:p.neutral_controller) { for(float v:q)out<<v<<' ';out<<'\n'; }
    out.flush();if(!out)return false;
    out.close();if(!out)return false;
  }
  return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
void RefreshStatus() {
  g_status.profile_valid=g_profile.valid;g_status.can_cancel=g_profile.valid&&!g_status.saving;
  g_status.screen_visible=(g_loading || g_requested) && g_gameplay;
  g_status.capture_input=g_status.screen_visible;
  if(g_loading)g_status.phase=CalibrationPhase::Loading;
  else if(!g_requested)g_status.phase=g_profile.valid?CalibrationPhase::Ready:CalibrationPhase::WaitingForGameplay;
  else if(!g_gameplay)g_status.phase=CalibrationPhase::WaitingForGameplay;
  else g_status.phase=g_status.hold_progress>0?CalibrationPhase::Holding:CalibrationPhase::AwaitingPose;
  if(g_status.phase==CalibrationPhase::Loading)g_status.instruction="Loading your last calibration";
  else if(!g_status.screen_visible)g_status.instruction=g_profile.valid?"Calibration saved":"Waiting for the game";
  else if(g_status.saving)g_status.instruction="Saving your measurements";
  else if(g_status.save_failed)g_status.instruction="Could not save. Your old measurements still work. Let go of the trigger, then try again";
  else if(!g_status.pose_ready)g_status.instruction="Hold your arms out to the sides like a T. A relaxed A shape (arms down and out) works too";
  else if(!g_status.hold_progress)g_status.instruction="Good, hold still. Let go of the trigger, then squeeze and hold it for one second";
  else g_status.instruction="Measuring - keep very still, almost done";
  // Say in plain words what to move so the pose counts.
  g_status.pose_hint="";
  if(g_status.screen_visible && !g_status.saving && !g_capture.Ready() &&
     g_capture.LastHandsValid()) {
    const Vec3 left=g_capture.LastPosition(0),right=g_capture.LastPosition(1);
    const float span=g_capture.LastSpanM(),plane=g_capture.LastPlaneM();
    if(span>2.5f) g_status.pose_hint="Arms too wide - bring your hands a little closer in";
    else if(plane>.6f) g_status.pose_hint="Hands too low - lift them up to shoulder height";
    else if(left.x>-.25f || right.x<.25f || span<.7f) g_status.pose_hint="Arms too narrow - stretch both arms out wider";
    else if(std::fabs(left.y-right.y)>=.15f) g_status.pose_hint="One hand is higher - make both hands level";
    else if(plane<.08f) g_status.pose_hint="Hands a touch high - lower them just a little";
    else if(std::fabs(left.z)>=.35f || std::fabs(right.z)>=.35f) g_status.pose_hint="Hands drifted forward or back - keep them out to your sides";
    else g_status.pose_hint="Nearly there - hold steady, tiny moves only";
  } else if(g_status.screen_visible && g_capture.Ready() && !g_capture.Released()) {
    g_status.pose_hint="Let go of the trigger once, then hold it down";
  }
}
void Worker() {
  std::wstring path;
  { std::lock_guard<std::mutex> lock(g_mutex);path=g_path; }
  UserCalibration loaded;const bool have=Load(path,loaded);
  std::unique_lock<std::mutex> lock(g_mutex);
  if(have) { loaded.revision=++g_generation;g_profile=loaded; }
  // A new process always runs setup once. Loading a saved profile provides
  // recovery/cancel state; deaths and scene changes never request another capture.
  g_loading=false;g_requested=true;RefreshStatus();
  for(;;) {
    g_changed.wait(lock,[]{return g_stop||g_pending;});
    if(!g_pending && g_stop)break;
    const auto profile=g_pending_profile;path=g_path;g_pending=false;
    lock.unlock();const bool ok=Save(path,profile);lock.lock();
    g_status.saving=false;g_status.save_failed=!ok;
    if(ok) {g_profile=profile;g_requested=false;++g_recenter;}
    g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;
    RefreshStatus();
  }
}
}

void CalibrationStart(const std::wstring& path) {
  CalibrationStop();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_path=path;g_profile={};g_capture.Reset();g_status={};
  g_stop=false;g_pending=false;g_requested=false;g_gameplay=false;g_loading=true;g_started=true;
  RefreshStatus();
  g_worker=CreateThread(nullptr,0,[](LPVOID)->DWORD {Worker();return 0;},nullptr,0,nullptr);
  if(!g_worker) {g_loading=false;g_requested=true;g_status.save_failed=true;RefreshStatus();}
}
void CalibrationStop() {
  { std::lock_guard<std::mutex> lock(g_mutex);g_stop=true;g_changed.notify_one(); }
  if(g_worker) {WaitForSingleObject(g_worker,INFINITE);CloseHandle(g_worker);g_worker=nullptr;}
  std::lock_guard<std::mutex> lock(g_mutex);
  g_started=false;g_loading=false;g_requested=false;g_gameplay=false;g_capture.Reset();
  g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationRequest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started || g_stop || g_status.saving)return;
  g_requested=true;g_capture.Reset();
  g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationCancel() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_profile.valid || g_status.saving)return;
  g_requested=false;g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();
}
void CalibrationRecenter() { ++g_recenter; }
std::uint64_t CalibrationRecenterGeneration() { return g_recenter.load(); }
UserCalibration CalibrationCurrent() { std::lock_guard<std::mutex> lock(g_mutex);return g_profile; }
CalibrationStatus CalibrationGetStatus() { std::lock_guard<std::mutex> lock(g_mutex);return g_status; }
bool CalibrationCapturesInput() { std::lock_guard<std::mutex> lock(g_mutex);return g_status.capture_input; }
void CalibrationGameplayReady(bool ready) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started)return;
  g_gameplay=ready;
  if(!ready) {g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;}
  RefreshStatus();
}
void CalibrationSample(const TrackingFrame& frame,bool trigger,bool gameplay,bool floor_valid,
                       float floor_height,float fallback_height) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if(!g_started)return;
  g_gameplay=gameplay;
  // Bounded once-per-second capture state while setup is requested, so a
  // hold that never starts is diagnosable from the log (which gate: pose
  // not ready, trigger unseen, gameplay gate closed, or progress resets).
  static ULONGLONG last_state_log=0;
  if(g_requested && !g_loading) {
    const ULONGLONG now_log=GetTickCount64();
    if(now_log-last_state_log>=1000) {
      last_state_log=now_log;
      const Vec3 lp=g_capture.LastPosition(0),rp=g_capture.LastPosition(1);
      DSVR_LOG("Calibration: sample pose_ready=%d progress=%.2f trigger=%d "
               "gameplay=%d saving=%d span=%.2f plane=%.2f hands=%d "
               "L=(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f) hint=%s",
               (int)g_status.pose_ready,(double)g_status.hold_progress,
               (int)trigger,(int)gameplay,(int)g_status.saving,
               (double)g_capture.LastSpanM(),(double)g_capture.LastPlaneM(),
               (int)g_capture.LastHandsValid(),
               (double)lp.x,(double)lp.y,(double)lp.z,
               (double)rp.x,(double)rp.y,(double)rp.z,
               g_status.pose_hint);
    }
  }
  if(g_loading || !g_requested || !gameplay || g_status.saving) {
    g_capture.Reset();g_status.hold_progress=0;g_status.pose_ready=false;RefreshStatus();return;
  }
  UserCalibration completed;
  const bool complete=g_capture.Feed(frame,trigger,floor_valid,floor_height,fallback_height,completed);
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
