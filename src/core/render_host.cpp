#include "render_host.h"

#include <limits>

namespace dsvr {
namespace {
class TryLane {
 public:
  explicit TryLane(std::atomic_flag& flag)
      : flag_(flag), owned_(!flag.test_and_set(std::memory_order_acquire)) {}
  ~TryLane() { if (owned_) flag_.clear(std::memory_order_release); }
  bool Owned() const { return owned_; }
 private:
  std::atomic_flag& flag_;
  bool owned_;
};
}  // namespace

bool RenderHost::Configure(bool enabled, RenderHostKind selected,
                           HostOperations ops) {
  TryLane lock(busy_);
  if (!lock.Owned()) return false;
  if (configured_) return false;
  configured_ = true;
  selected_ = selected;
  ops_ = ops;
  enabled_ = enabled && ops.capture_device && ops.poll && ops.submit_mono;
  return enabled_.load();
}

HostDispatch RenderHost::Dispatch(const HostSource& source) {
  TryLane lock(busy_);
  if (!lock.Owned()) return HostDispatch::Busy;
  if (!enabled_ || faulted_.load() || source.host != selected_ ||
      !source.device || !source.swapchain || !source.image || !source.thread)
    return HostDispatch::Refused;
  if (thread_ && thread_ != source.thread) return HostDispatch::Refused;
  if (swapchain_ && swapchain_ != source.swapchain)
    return HostDispatch::Refused;
  if (device_ && device_ != source.device) {
    faulted_.store(true);  // no migration of a live XR D3D11 binding
    return HostDispatch::Refused;
  }
  if (ordinal_ == std::numeric_limits<std::uint64_t>::max()) {
    faulted_.store(true);
    return HostDispatch::Refused;
  }
  swapchain_ = source.swapchain;
  thread_ = source.thread;
  if (!device_) {
    device_ = source.device;
    ops_.capture_device(ops_.context, device_);
  }
  if (faulted_.load()) return HostDispatch::Refused;
  ops_.poll(ops_.context);
  if (faulted_.load()) return HostDispatch::Refused;
  ops_.submit_mono(ops_.context, source, {generation_, ++ordinal_});
  return HostDispatch::Dispatched;
}

void RenderHost::Invalidate(RenderHostKind host, std::uintptr_t swapchain) {
  if (!enabled_.load() || host != selected_.load() || !swapchain) return;
  TryLane lock(busy_);
  if (!lock.Owned()) {
    // Teardown during a frame cannot safely recycle its resource lifetime.
    faulted_.store(true);
    return;
  }
  if (!enabled_ || host != selected_ || !swapchain || swapchain_ != swapchain)
    return;
  swapchain_ = 0;
  if (generation_ == std::numeric_limits<std::uint64_t>::max())
    faulted_.store(true);
  else
    ++generation_;
}

}  // namespace dsvr
