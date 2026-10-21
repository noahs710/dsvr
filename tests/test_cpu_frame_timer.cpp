#include "../src/core/cpu_frame_timer.h"
#include <cstdio>
#include <limits>
#include <limits>
#include <thread>
#include <vector>
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
  using namespace dsvr;
  CpuFrameTimer timer;
  std::vector<std::thread> writers;
  for(int i=0;i<4;++i)writers.emplace_back([&]{for(int j=0;j<10000;++j)timer.Record(CpuStage::ArmRetarget,.25);});
  for(auto& writer:writers)writer.join();
  auto sample=timer.Snapshot();
  CHECK(sample[3].calls==40000 && sample[3].total_ms==10000 && sample[3].max_ms==.25);
  timer.Record(CpuStage::ArmRetarget,std::numeric_limits<double>::quiet_NaN());
  timer.Record(CpuStage::ArmRetarget,-1);timer.Record(CpuStage::Count,1);
  CHECK(timer.Snapshot()[3].calls==40000);
  timer.Reset();CHECK(timer.Snapshot()[3].calls==0);
  g_cpu_frame_timer.Reset();
  {CpuStageScope outer(CpuStage::HostFrame);{CpuStageScope inner(CpuStage::NativePresent);inner.Stop();}}
  sample=g_cpu_frame_timer.Snapshot();
  CHECK(sample[4].calls==1 && sample[5].calls==1 && sample[4].total_ms>=sample[5].total_ms);
  // A scope that began before a scene/reset boundary must not poison its new interval.
  g_cpu_frame_timer.Reset();
  CpuStageScope old_scene(CpuStage::HostFrame);
  g_cpu_frame_timer.Reset();
  old_scene.Stop();
  CHECK(g_cpu_frame_timer.Snapshot()[4].calls==0);
  // Draining log intervals must not erase the panel's reset-scoped totals.
  CpuFrameTimer interval;
  interval.Record(CpuStage::CameraAndTargets,2.0);
  interval.Record(CpuStage::CameraAndTargets,6.0);
  auto first=interval.TakeInterval();
  CHECK(first[1].calls==2 && first[1].AverageMs()==4.0 && first[1].max_ms==6.0);
  CHECK(interval.TakeInterval()[1].calls==0);
  interval.Record(CpuStage::CameraAndTargets,1.0);
  auto second=interval.TakeInterval();
  CHECK(second[1].calls==1 && second[1].max_ms==1.0);
  CHECK(interval.Snapshot()[1].calls==3 && interval.Snapshot()[1].max_ms==6.0);
  interval.Record(CpuStage::Count,1.0);
  interval.Record(CpuStage::CameraAndTargets,-1.0);
  interval.Record(CpuStage::CameraAndTargets,std::numeric_limits<double>::infinity());
  interval.Record(CpuStage::CameraAndTargets,std::numeric_limits<double>::quiet_NaN());
  interval.Record(CpuStage::CameraAndTargets,60001.0);
  CHECK(interval.TakeInterval()[1].calls==0);
  interval.Reset();
  CHECK(interval.Snapshot()[1].calls==0 && interval.TakeInterval()[1].calls==0);

  // Stop is idempotent; reset rejects stale generations, including delayed writers.
  g_cpu_frame_timer.Reset();
  const auto generation=g_cpu_frame_timer.Generation();
  {CpuStageScope once(CpuStage::NativePose);once.Stop();once.Stop();}
  CHECK(g_cpu_frame_timer.Snapshot()[2].calls==1);
  g_cpu_frame_timer.Reset();
  g_cpu_frame_timer.Record(CpuStage::NativePose,1.0,generation);
  CHECK(g_cpu_frame_timer.TakeInterval()[2].calls==0);
  std::puts("PASS concurrent CPU timing, reset generations, bounded intervals and inclusive nested scopes");
}
