#include "../src/core/calibration.h"
#include "../src/core/tracking_frame.h"
#include "../src/common/logging.h"
namespace dsvr {
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
// Ladder closure: InputCalibrationRefinementSafe lives in input.cpp, which
// this binary deliberately does not link (world TU). Its writer TU is absent
// here so the production flag would stay at its zero-init false; this stub
// reproduces exactly that headless value. Remove if input.cpp is ever linked.
bool InputCalibrationRefinementSafe() { return false; }
}
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iterator>
#define CHECK(x) do { if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);dsvr::CalibrationStop();return 1;} } while(0)
namespace {
bool WaitLoaded() {
  for(int i=0;i<200;++i) { if(dsvr::CalibrationGetStatus().phase!=dsvr::CalibrationPhase::Loading)return true;Sleep(5); }
  return false;
}
bool WaitSaved() {
  for(int i=0;i<200;++i) { if(!dsvr::CalibrationGetStatus().saving)return true;Sleep(5); }
  return false;
}
std::string Bytes(const std::filesystem::path& path) {
  std::ifstream in(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
bool Same(const dsvr::UserCalibration& a,const dsvr::UserCalibration& b) {
  return a.version==b.version && a.valid==b.valid && a.floor_height_measured==b.floor_height_measured &&
      a.eye_height_m==b.eye_height_m && a.head_to_arm_m==b.head_to_arm_m &&
      a.shoulder_half_width_m==b.shoulder_half_width_m && a.arm_reach_m[0]==b.arm_reach_m[0] &&
      a.arm_reach_m[1]==b.arm_reach_m[1] && a.arm_span_m==b.arm_span_m && a.revision==b.revision &&
      std::memcmp(a.neutral_controller,b.neutral_controller,sizeof(a.neutral_controller))==0;
}
}
int main() {
  using namespace dsvr;
  wchar_t temp[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,temp));
  const auto folder=std::filesystem::path(temp)/(L"DSVR-calibration-test-"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directory(folder);
  const auto path=folder/L"calibration.dat";
  CalibrationStart(path.wstring());CHECK(WaitLoaded());
  TrackingFrame f;f.sequence=1;f.space_epoch=1;f.captured_ms=1000;
  f.focused=f.orientation_valid=f.position_valid=true;f.head_pos[1]=1.65f;
  for(int s=0;s<2;++s) { f.arm[s].valid=true;f.arm[s].pos[0]=s?.72f:-.72f;f.arm[s].pos[1]=1.4f; }
  CalibrationSample(f,false,false);CHECK(!CalibrationCapturesInput());
  CalibrationPresentationReady(true);CHECK(CalibrationCapturesInput());
  CalibrationPresentationReady(false);CHECK(!CalibrationCapturesInput());
  CalibrationSample(f,false,true);CHECK(CalibrationCapturesInput());
  for(int i=0;i<22;++i) { ++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true); }
  CHECK(WaitSaved() && CalibrationCurrent().valid && !CalibrationCapturesInput());
  CHECK(!CalibrationGetStatus().save_failed && std::filesystem::exists(path));
  const auto original=CalibrationCurrent();
  // Loads/deaths, tracking epochs and recenter never rewrite user measurements.
  for(int i=0;i<100;++i) {
    CalibrationSample(f,false,false);++f.space_epoch;++f.sequence;f.captured_ms+=50;
    CalibrationSample(f,true,true);CalibrationRecenter();
  }
  auto after=CalibrationCurrent();CHECK(Same(original,after));
  CalibrationRequest();CalibrationSample(f,false,true);CHECK(CalibrationCapturesInput());
  CalibrationCancel();CHECK(!CalibrationCapturesInput());
  after=CalibrationCurrent();CHECK(Same(original,after));
  // A successful pose capture with an unwritable destination must not discard
  // the last saved/live calibration or silently dismiss recalibration.
  const auto denied_temp=std::filesystem::path(path.wstring()+L".tmp");
  std::filesystem::create_directory(denied_temp);
  CalibrationRequest();
  f.arm[0].pos[0]=-.8f;f.arm[1].pos[0]=.8f;
  ++f.sequence;f.captured_ms+=50;CalibrationSample(f,false,true);
  for(int i=0;i<22;++i) {++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true);}
  CHECK(WaitSaved());CHECK(CalibrationGetStatus().save_failed);
  CHECK(Same(original,CalibrationCurrent()));
  CHECK(CalibrationCapturesInput());CalibrationCancel();
  std::filesystem::remove(denied_temp);
  CalibrationStop();CalibrationStart(path.wstring());CHECK(WaitLoaded());
  after=CalibrationCurrent();CHECK(after.valid && after.eye_height_m==original.eye_height_m &&
      after.arm_reach_m[0]==original.arm_reach_m[0] && after.head_to_arm_m==original.head_to_arm_m);
  CalibrationSample(f,false,true);CHECK(CalibrationCapturesInput()); // once per new launch
  CalibrationCancel();CHECK(!CalibrationCapturesInput());
  const auto launch_profile=CalibrationCurrent();
  for(int i=0;i<50;++i) {CalibrationSample(f,false,false);++f.sequence;++f.space_epoch;CalibrationSample(f,false,true);}
  CHECK(!CalibrationCapturesInput() && Same(launch_profile,CalibrationCurrent()));
  // Lock the destination rather than the staging path: failed atomic replacement
  // must retain both the exact saved bytes and the active profile/recenter state.
  const auto saved_bytes=Bytes(path);const auto recenter=CalibrationRecenterGeneration();
  HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  CHECK(locked!=INVALID_HANDLE_VALUE);
  CalibrationRequest();++f.sequence;f.captured_ms+=50;CalibrationSample(f,false,true);
  for(int i=0;i<22;++i) {++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true,false,0,1.72f);}
  CHECK(WaitSaved() && CalibrationGetStatus().save_failed);
  CHECK(Same(launch_profile,CalibrationCurrent()) && Bytes(path)==saved_bytes);
  CHECK(CalibrationRecenterGeneration()==recenter);
  CloseHandle(locked);
  ++f.sequence;f.captured_ms+=50;CalibrationSample(f,false,true);
  for(int i=0;i<22;++i) {++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true,false,0,1.72f);}
  CHECK(WaitSaved() && !CalibrationGetStatus().save_failed);
  CHECK(CalibrationCurrent().eye_height_m==1.72f && Bytes(path)!=saved_bytes);
  CHECK(CalibrationRecenterGeneration()==recenter+1);
  CalibrationStop();CalibrationStart(path.wstring());CHECK(WaitLoaded());
  CHECK(CalibrationCurrent().eye_height_m==1.72f);
  CalibrationPresentationReady(true);CalibrationCancel();CalibrationRequest();
  ++f.sequence;f.captured_ms+=50;CalibrationSample(f,false,true);
  for(int i=0;i<16;++i) {++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true,false,0,1.8f);}
  // A readiness interruption must reset the hold even without a Sample callback.
  CalibrationPresentationReady(false);CalibrationPresentationReady(true);
  for(int i=0;i<7;++i) {++f.sequence;f.captured_ms+=50;CalibrationSample(f,true,true,false,0,1.8f);}
  CHECK(WaitSaved() && CalibrationCurrent().eye_height_m==1.72f);
  CalibrationStop();CHECK(!CalibrationCapturesInput());
  CalibrationRequest();CHECK(!CalibrationCapturesInput());
  // Malformed/version-mismatched files cannot become a silent user profile.
  { std::ofstream bad(path);bad<<"DSVR_CALIBRATION 99\n"; }
  CalibrationStart(path.wstring());CHECK(WaitLoaded());CHECK(!CalibrationCurrent().valid);
  CalibrationSample(f,false,true);CHECK(CalibrationCapturesInput());CalibrationStop();
  CHECK(!CalibrationCapturesInput());
  std::filesystem::remove(path);std::filesystem::remove(folder);
  std::puts("PASS calibration worker atomic persistence, repeated scene recovery and explicit replacement");
}
