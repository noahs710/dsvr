#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace dsvr {
enum class CpuStage : unsigned { NativeCamera, CameraAndTargets, NativePose, ArmRetarget,
                                  HostFrame, NativePresent, NativeFrameBoundary,
                                  ArmEquipment, ArmValidation, ArmSolve, ArmCommit,
                                  CameraApply, ArmTargets, CameraReads, CameraWrites,
                                  EquipmentRead, Count };
inline constexpr std::array<const char*,static_cast<unsigned>(CpuStage::Count)> kCpuStageNames{
    "native camera + restore", "camera and targets", "native pose", "local-player arm retarget",
    "host submit incl XR excl poll", "DXGI Present", "native frame boundary including Present",
    "arm equipment", "arm validation", "arm solve", "arm commit",
    "camera apply (writes)", "arm target build", "camera reads",
    "camera writes", "equipment read"};
struct CpuStageStats {
  std::uint64_t calls=0;
  double total_ms=0,max_ms=0;
  double AverageMs() const {return calls?total_ms/static_cast<double>(calls):0;}
};
// All writers publish a complete sample under a short lock. No allocation,
// file IO, GPU query or native callback takes place while this lock is held.
// Boundary/host samples are inclusive: nested stages must not be added together.
// Snapshot is since explicit reset; TakeInterval drains only the log interval.
// Both use fixed storage, independent of frame count. Maxima are not subtractable.
class CpuFrameTimer {
 public:
  using SnapshotStats=std::array<CpuStageStats,static_cast<unsigned>(CpuStage::Count)>;
  std::uint64_t Generation() const {return generation_.load(std::memory_order_acquire);}
  void Record(CpuStage stage,double ms) {Record(stage,ms,Generation());}
  void Record(CpuStage stage,double ms,std::uint64_t generation) {
    if(!(ms>=0) || ms>60000 || stage>=CpuStage::Count)return;
    std::lock_guard<std::mutex> lock(mutex_);
    if(generation!=generation_.load(std::memory_order_relaxed))return;
    const auto index=static_cast<unsigned>(stage);
    Add(stats_[index],ms);Add(interval_[index],ms);
  }
  auto Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);return stats_;
  }
  auto TakeInterval() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result=interval_;interval_={};return result;
  }
  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_={};interval_={};generation_.fetch_add(1,std::memory_order_release);
  }
 private:
  static void Add(CpuStageStats& sample,double ms) {
    ++sample.calls;sample.total_ms+=ms;if(ms>sample.max_ms)sample.max_ms=ms;
  }
  std::mutex mutex_;
  std::atomic<std::uint64_t> generation_{0};
  SnapshotStats stats_{},interval_{};
};
inline CpuFrameTimer g_cpu_frame_timer;
class CpuStageScope {
 public:
  explicit CpuStageScope(CpuStage stage,bool enabled=true):stage_(stage),
      generation_(enabled?g_cpu_frame_timer.Generation():0),
      start_(enabled?Clock::now():Clock::time_point{}),stopped_(!enabled){}
  ~CpuStageScope(){Stop();}
  void Stop() {
    if(stopped_)return;
    const auto end=Clock::now();stopped_=true;
    g_cpu_frame_timer.Record(stage_,std::chrono::duration<double,std::milli>(end-start_).count(),generation_);
  }
  CpuStageScope(const CpuStageScope&)=delete;
  CpuStageScope& operator=(const CpuStageScope&)=delete;
 private:
  using Clock=std::chrono::steady_clock;
  CpuStage stage_;
  std::uint64_t generation_;
  Clock::time_point start_;
  bool stopped_=false;
};
}
