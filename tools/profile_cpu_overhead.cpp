// Offline instrumentation-only harness; no game, GPU, XR, hooks or frame pacing.
// Build in a VS developer prompt: cl /nologo /std:c++17 /O2 /DNDEBUG /EHsc /I src
//   tools\profile_cpu_overhead.cpp /Fe:build\n2-overhead.exe /Fo:build\n2-overhead.obj
#include "core/cpu_frame_timer.h"
#include <cstdio>
#include <thread>
#include <vector>

using namespace dsvr;
namespace {
constexpr unsigned kFrames=5000;
constexpr unsigned kStages=static_cast<unsigned>(CpuStage::Count);
volatile std::uint64_t sinks[4]{};
__declspec(noinline) void Work(bool enabled,unsigned worker) {
  std::uint64_t value=worker+1;
  for(unsigned frame=0;frame<kFrames;++frame)
    for(unsigned stage=0;stage<kStages;++stage) {
      CpuStageScope scope(static_cast<CpuStage>(stage),enabled);
      value=value*1664525+1013904223;
    }
  sinks[worker]=value;
}
double Run(bool enabled,unsigned workers) {
  g_cpu_frame_timer.Reset();
  const auto begin=std::chrono::steady_clock::now();
  if(workers==1)Work(enabled,0);
  else {
    std::vector<std::thread> threads;
    for(unsigned i=0;i<workers;++i)threads.emplace_back(Work,enabled,i);
    for(auto& thread:threads)thread.join();
  }
  const auto end=std::chrono::steady_clock::now();
  return std::chrono::duration<double,std::nano>(end-begin).count()/
      (double(kFrames)*kStages*workers);
}
}
int main() {
  std::puts("threads,repeat,disabled_ns_per_scope,enabled_ns_per_scope,delta_ns_per_scope");
  for(unsigned workers:{1u,4u}) {
    Run(false,workers);Run(true,workers); // warm up before paired samples
    for(unsigned repeat=0;repeat<21;++repeat) {
      double disabled=0,enabled=0;
      if(repeat%2){enabled=Run(true,workers);disabled=Run(false,workers);}
      else {disabled=Run(false,workers);enabled=Run(true,workers);}
      std::printf("%u,%u,%.3f,%.3f,%.3f\n",workers,repeat,disabled,enabled,enabled-disabled);
    }
  }
  return 0;
}
