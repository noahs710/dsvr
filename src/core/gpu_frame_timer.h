#pragma once
#include <d3d11.h>
#include <cstdint>

namespace dsvr {

// Bounded, sampled GPU timings. Queries are inspected with DONOTFLUSH once per
// frame and never waited on. A full ring skips sampling instead of stalling.
// Each eye is timed independently so XR image waits are not charged to GPU work.
class GpuFrameTimer {
 public:
  struct Sample {
    std::uint64_t sequence = 0;
    double copy_ms = 0, blit_ms = 0;
  };
  ~GpuFrameTimer() {
    for (auto& slot : slots_) {
      if (slot.disjoint) slot.disjoint->Release();
      for (auto* query : slot.stamps) if (query) query->Release();
    }
  }
  void BeginFrame(ID3D11Device* device, ID3D11DeviceContext* context,
                  std::uint64_t sequence) {
    Poll(context);
    if (active_ || sequence % 30 != 1) return;
    for (auto& slot : slots_) {
      if (slot.pending) continue;
      if (!slot.disjoint) {
        const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(device->CreateQuery(&desc, &slot.disjoint))) return;
      }
      for (auto*& query : slot.stamps) {
        if (query) continue;
        const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device->CreateQuery(&desc, &query))) return;
      }
      slot.sequence = sequence;
      slot.mask = 0;
      active_ = &slot;
      context->Begin(slot.disjoint);
      return;
    }
  }
  void BeginRange(ID3D11DeviceContext* context, unsigned range) {
    if (active_ && range < 3) context->End(active_->stamps[range * 2]);
  }
  void EndRange(ID3D11DeviceContext* context, unsigned range) {
    if (!active_ || range >= 3) return;
    context->End(active_->stamps[range * 2 + 1]);
    active_->mask |= 1u << range;
  }
  void EndFrame(ID3D11DeviceContext* context) {
    if (!active_) return;
    context->End(active_->disjoint);
    active_->pending = true;
    active_ = nullptr;
  }
  const Sample& Latest() const { return latest_; }

 private:
  struct Slot {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* stamps[6]{};
    std::uint64_t sequence = 0;
    unsigned mask = 0;
    bool pending = false;
  } slots_[4];
  Slot* active_ = nullptr;
  Sample latest_{};
  void Poll(ID3D11DeviceContext* context) {
    for (auto& slot : slots_) {
      if (!slot.pending) continue;
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
      const auto result = context->GetData(slot.disjoint, &frequency, sizeof(frequency),
                                           D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (result == S_FALSE) continue;
      if (FAILED(result) || frequency.Disjoint || !frequency.Frequency) {
        slot.pending = false;
        continue;
      }
      Sample sample{};
      sample.sequence = slot.sequence;
      bool ready = true, valid = true;
      for (unsigned range = 0; range < 3; ++range) {
        if (!(slot.mask & (1u << range))) continue;
        UINT64 begin = 0, end = 0;
        const auto a = context->GetData(slot.stamps[range * 2], &begin, sizeof(begin),
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const auto b = context->GetData(slot.stamps[range * 2 + 1], &end, sizeof(end),
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (a == S_FALSE || b == S_FALSE) { ready = false; break; }
        if (FAILED(a) || FAILED(b) || end < begin) { valid = false; break; }
        const double ms = (end - begin) * 1000.0 / frequency.Frequency;
        if (!range) sample.copy_ms = ms; else sample.blit_ms += ms;
      }
      if (!ready) continue;
      slot.pending = false;
      if (valid && slot.mask && sample.sequence > latest_.sequence) latest_ = sample;
    }
  }
};

}  // namespace dsvr
