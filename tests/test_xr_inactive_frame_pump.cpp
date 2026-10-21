// Runtime-controlled barriers prove WaitFrame AND EndFrame can block while
// native render callbacks keep returning and handoff remains exclusive.
#include "../src/core/xr_inactive_frame_pump.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
using Pump = dsvr::XrInactiveFramePump;
struct FakeRuntime {
  HANDLE wait_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE allow_wait=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE end_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE allow_end=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  DWORD main_thread=GetCurrentThreadId();
  Pump* pump=nullptr;
  std::atomic<unsigned> waits=0,begins=0,ends=0;
  std::atomic<int> order=0;
  std::atomic<bool> bad=false;
  XrResult wait_result=XR_SUCCESS,begin_result=XR_SUCCESS,end_result=XR_SUCCESS;
  XrTime prediction=0;
  ~FakeRuntime() {CloseHandle(wait_entered);CloseHandle(allow_wait);CloseHandle(end_entered);CloseHandle(allow_end);}
  void ResetBarriers() {ResetEvent(wait_entered);ResetEvent(allow_wait);ResetEvent(end_entered);ResetEvent(allow_end);}
  void Release() {SetEvent(allow_wait);SetEvent(allow_end);}
};
FakeRuntime* runtime=nullptr;
XrResult XRAPI_CALL Wait(XrSession session,const XrFrameWaitInfo*,XrFrameState* state) {
  auto& f=*runtime;
  if(!session || GetCurrentThreadId()==f.main_thread || f.order.load()!=0)f.bad=true;
  if(f.pump)f.pump->Snapshot(); // deadlocks if an XR call is made under its lock
  ++f.waits;SetEvent(f.wait_entered);
  if(WaitForSingleObject(f.allow_wait,5000)!=WAIT_OBJECT_0)f.bad=true;
  if(XR_FAILED(f.wait_result))return f.wait_result;
  f.order=1;
  state->predictedDisplayTime=f.prediction=1'000'000'000+f.waits.load()*11'111'111;
  state->predictedDisplayPeriod=11'111'111;state->shouldRender=XR_FALSE;
  return f.wait_result;
}
XrResult XRAPI_CALL Begin(XrSession,const XrFrameBeginInfo*) {
  auto& f=*runtime;if(f.order.load()!=1)f.bad=true;
  ++f.begins;
  f.order=XR_FAILED(f.begin_result)?0:2;
  return f.begin_result;
}
XrResult XRAPI_CALL End(XrSession,const XrFrameEndInfo* frame) {
  auto& f=*runtime;
  if(f.order.load()!=2 || frame->layerCount!=0 || frame->layers!=nullptr ||
      frame->displayTime!=f.prediction || frame->environmentBlendMode!=XR_ENVIRONMENT_BLEND_MODE_OPAQUE)f.bad=true;
  ++f.ends;SetEvent(f.end_entered);
  if(WaitForSingleObject(f.allow_end,5000)!=WAIT_OBJECT_0)f.bad=true;
  f.order=0;return f.end_result;
}
template<class F> bool Until(F condition) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(std::chrono::steady_clock::now()<deadline) {
    if(condition())return true;
    std::this_thread::yield();
  }
  return false;
}
const XrSession first=reinterpret_cast<XrSession>(1),second=reinterpret_cast<XrSession>(2);
}

int main() {
  {
    FakeRuntime fake;runtime=&fake;Pump pump({Wait,Begin,End});fake.pump=&pump;
    const auto before=std::chrono::steady_clock::now();
    CHECK(pump.Request(first));
    CHECK(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100));
    CHECK(WaitForSingleObject(fake.wait_entered,2000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<1000;++i) {
      CHECK(pump.Request(first));
      CHECK(!pump.PauseAndIdle());
    }
    CHECK(fake.waits==1 && fake.begins==0 && pump.Snapshot().stage==Pump::Stage::Wait);
    CHECK(!pump.Request(second)); // the old session is still in an XR call
    CHECK(!pump.ResetSession(second));
    SetEvent(fake.allow_wait);
    CHECK(WaitForSingleObject(fake.end_entered,2000)==WAIT_OBJECT_0);
    CHECK(!pump.PauseAndIdle() && pump.Snapshot().stage==Pump::Stage::End);
    // EndFrame blocks independently of WaitFrame. No render callback waits.
    const auto end_before=std::chrono::steady_clock::now();
    for(unsigned i=0;i<1000;++i)CHECK(!pump.PauseAndIdle());
    CHECK(std::chrono::steady_clock::now()-end_before<std::chrono::milliseconds(100));
    SetEvent(fake.allow_end);
    CHECK(Until([&]{return pump.PauseAndIdle();}));
    auto status=pump.Snapshot();
    CHECK(status.cycles==1 && status.empty_frames==1 && status.errors==0 && !status.requested);
    CHECK(status.period==11'111'111 && status.predicted_time==fake.prediction && !fake.bad);
    const auto old_waits=fake.waits.load();
    for(unsigned i=0;i<1000;++i)CHECK(pump.PauseAndIdle());
    CHECK(fake.waits==old_waits); // quiescence guarantees no late frame begins

    fake.ResetBarriers();
    CHECK(pump.Request(second)); // switching is safe only after quiescence
    CHECK(WaitForSingleObject(fake.wait_entered,2000)==WAIT_OBJECT_0);
    pump.RequestStop();
    CHECK(!pump.PauseAndIdle() && !pump.Request(first));
    fake.Release();
    CHECK(pump.StopAndWait(2000));
    CHECK(!fake.bad && fake.waits==2 && fake.begins==2 && fake.ends==2 && fake.order==0);
  }
  for(int failure=0;failure<3;++failure) {
    FakeRuntime fake;runtime=&fake;fake.Release();
    if(failure==0)fake.wait_result=XR_ERROR_RUNTIME_FAILURE;
    if(failure==1)fake.begin_result=XR_ERROR_RUNTIME_FAILURE;
    if(failure==2)fake.end_result=XR_ERROR_RUNTIME_FAILURE;
    Pump pump({Wait,Begin,End});fake.pump=&pump;
    CHECK(pump.Request(first));
    CHECK(Until([&]{return pump.Snapshot().errors==1;}));
    CHECK(pump.PauseAndIdle());
    CHECK(!pump.Request(first)); // repeated Present calls cannot spin on errors
    CHECK(fake.waits==1 && fake.begins==(failure>0?1u:0u) && fake.ends==(failure>1?1u:0u));
    CHECK(!fake.bad);
    fake.wait_result=fake.begin_result=fake.end_result=XR_SUCCESS;
    fake.ResetBarriers();
    CHECK(pump.ResetSession(first) && !pump.Snapshot().faulted);
    CHECK(pump.Request(first));
    CHECK(WaitForSingleObject(fake.wait_entered,2000)==WAIT_OBJECT_0);
    CHECK(!pump.PauseAndIdle());
    pump.RequestStop();fake.Release();
    CHECK(pump.StopAndWait(2000) && !fake.bad);
  }
  std::puts("PASS inactive XR full-frame worker: nonblocking wait/end, exclusive handoff, stop and errors");
}
