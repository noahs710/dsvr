#pragma once
#include <atomic>
#include <cstddef>

namespace dsvr {

// Bounded single-producer / single-consumer queue for crossing DSVR's one
// hard thread boundary: the OpenXR render thread (Present detour, input
// polling) produces turn/gesture events; the game thread (ChrFollowCam
// hook) consumes them. Neither side may ever block the other -- a full
// queue drops the NEW event and counts it, because a stale swing applied
// late is worse than a dropped one. Lock-free, allocation-free, header-only.
//
// Capacity must be a power of two. Indices are monotonic counters (never
// masked for storage of the counters themselves), so a full queue is
// distinguishable from an empty one at any wrap count.
template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two >= 2");

 public:
  // Producer thread only. False (and dropped() increments) when full.
  bool Push(const T& value) {
    std::size_t tail = tail_.load(std::memory_order_relaxed);
    std::size_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= Capacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    buffer_[tail & kMask] = value;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer thread only. False when empty.
  bool Pop(T& out) {
    std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return false;
    out = buffer_[head & kMask];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  bool Empty() const {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }
  std::size_t Size() const {
    std::size_t tail = tail_.load(std::memory_order_acquire);
    std::size_t head = head_.load(std::memory_order_acquire);
    return tail - head;
  }
  unsigned long long Dropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }

  // Consumer thread only (or before either thread starts): discard all.
  void Clear() {
    head_.store(tail_.load(std::memory_order_acquire),
                std::memory_order_release);
  }

 private:
  static constexpr std::size_t kMask = Capacity - 1;
  T buffer_[Capacity]{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<unsigned long long> dropped_{0};
};

}  // namespace dsvr
