#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <windows.h>

namespace dsvr {

struct MenuWindowEvent {
  UINT message;
  WPARAM key;
  LPARAM data;
};

class MenuWindowEventQueue {
 public:
  static constexpr std::size_t kCapacity = 128;

  bool Push(MenuWindowEvent event) noexcept {
    if (count_ == events_.size()) return false;
    events_[(first_ + count_) % events_.size()] = event;
    ++count_;
    return true;
  }

  bool Pop(MenuWindowEvent& event) noexcept {
    if (count_ == 0) return false;
    event = events_[first_];
    first_ = (first_ + 1) % events_.size();
    --count_;
    return true;
  }

  void Clear() noexcept { first_ = count_ = 0; }

  void Swap(MenuWindowEventQueue& other) noexcept {
    events_.swap(other.events_);
    std::swap(first_, other.first_);
    std::swap(count_, other.count_);
  }

 private:
  std::array<MenuWindowEvent, kCapacity> events_{};
  std::size_t first_ = 0;
  std::size_t count_ = 0;
};

inline bool IsMenuInputEdge(UINT message) noexcept {
  switch (message) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
      return true;
    default:
      return false;
  }
}

}  // namespace dsvr
