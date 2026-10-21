#pragma once

#include <openxr/openxr.h>
#include <cmath>
#include <cstdint>
#include <thread>

namespace dsvr {

// Owns the real XR frame/image call order, not native render provenance.
// A timed-out acquisition stays owned until a later wait succeeds; never
// release an unwaited image or acquire another over it.
class XrStereoFrame {
 public:
  XrStereoFrame() = default;
  XrStereoFrame(const XrStereoFrame&) = delete;
  XrStereoFrame& operator=(const XrStereoFrame&) = delete;
  bool Begin(XrSession session, XrSpace space, XrSpace head_space) {
    if (!session || !space || !head_space || active_ || faulted_ ||
        !OnOwner() || !Drain()) return false;
    owner_ = std::this_thread::get_id();
    session_ = session;
    space_ = space;
    head_pose_ = {};
    state_ = {XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    const auto waited = xrWaitFrame(session_, &wait, &state_);
    if (XR_FAILED(waited)) return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    const auto result = xrBeginFrame(session_, &begin);
    if (XR_FAILED(result)) { faulted_ = true; return false; }
    active_ = true;
    acquired_ = false;
    ++token_;
    if (waited != XR_SUCCESS || result != XR_SUCCESS || !state_.shouldRender ||
        state_.predictedDisplayTime <= 0) { Cancel(); return false; }
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = state_.predictedDisplayTime;
    locate.space = space_;
    XrViewState validity{XR_TYPE_VIEW_STATE};
    views_[0] = views_[1] = {XR_TYPE_VIEW};
    uint32_t count = 0;
    constexpr auto required = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                              XR_VIEW_STATE_POSITION_VALID_BIT;
    if (xrLocateViews(session_, &locate, &validity, 2, &count, views_) != XR_SUCCESS ||
        count != 2 || (validity.viewStateFlags & required) != required ||
        !ValidView(views_[0]) || !ValidView(views_[1]) ||
        !LocateHeadPose(head_space, space_, state_.predictedDisplayTime, head_pose_)) {
      Cancel();
      return false;
    }
    return true;
  }

  bool Acquire(XrSwapchain left, XrSwapchain right) {
    if (!active_ || !OnOwner() || acquired_ || !left || !right || left == right)
      return false;
    acquired_ = true;
    chains_[0] = left;
    chains_[1] = right;
    for (unsigned eye = 0; eye < 2; ++eye) {
      XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      auto result = xrAcquireSwapchainImage(chains_[eye], &acquire, &indices_[eye]);
      if (XR_FAILED(result)) return false;
      leases_[eye].chain = chains_[eye];
      if (result != XR_SUCCESS) return false;
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wait.timeout = 5'000'000;  // bounded, at most 10 ms for the pair
      result = xrWaitSwapchainImage(chains_[eye], &wait);
      leases_[eye].ready = XR_SUCCEEDED(result) && result != XR_TIMEOUT_EXPIRED;
      if (result != XR_SUCCESS) return false;
    }
    return true;
  }

  bool Submit(uint32_t width, uint32_t height) {
    if (!active_ || !OnOwner()) return false;
    const bool complete = acquired_ && leases_[0].ready && leases_[1].ready &&
                          width && height && width <= INT32_MAX && height <= INT32_MAX;
    const bool released = Drain();
    XrCompositionLayerProjectionView images[2]{};
    for (unsigned eye = 0; eye < 2; ++eye) {
      images[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
      images[eye].pose = views_[eye].pose;
      images[eye].fov = views_[eye].fov;
      images[eye].subImage.swapchain = chains_[eye];
      images[eye].subImage.imageRect.extent = {
          static_cast<int32_t>(width), static_cast<int32_t>(height)};
    }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = space_;
    layer.viewCount = 2;
    layer.views = images;
    const bool render = complete && released;
    const bool ended = End(render ? &layer : nullptr);
    return render && ended;
  }

  void Cancel() {
    if (!OnOwner()) return;
    Drain();
    if (active_) End(nullptr);
  }

  bool Drain() {
    if (!OnOwner()) return false;
    bool drained = true;
    for (auto& lease : leases_) {
      if (!lease.chain) continue;
      if (!lease.ready) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 0;
        const auto result = xrWaitSwapchainImage(lease.chain, &wait);
        lease.ready = XR_SUCCEEDED(result) && result != XR_TIMEOUT_EXPIRED;
      }
      if (lease.ready) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_SUCCEEDED(xrReleaseSwapchainImage(lease.chain, &release))) lease = {};
      }
      if (lease.chain) drained = false;
    }
    return drained;
  }

  bool Busy() const { return active_ || faulted_ || leases_[0].chain || leases_[1].chain; }
  bool Active() const { return active_; }
  std::uint64_t Token() const { return token_; }
  const XrFrameState& State() const { return state_; }
  const XrView& View(unsigned eye) const { return views_[eye]; }
  const XrPosef& HeadPose() const { return head_pose_; }
  uint32_t ImageIndex(unsigned eye) const { return indices_[eye]; }

  // VIEW is the primary viewer's origin; individual eye orientations can be
  // canted. Locate it in the same base space and prediction as the eye views.
  static bool LocateHeadPose(XrSpace head_space, XrSpace base_space,
                             XrTime predicted_time, XrPosef& out) {
    out = {};
    if (!head_space || !base_space || predicted_time <= 0) return false;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    constexpr auto required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                              XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if (xrLocateSpace(head_space, base_space, predicted_time, &location) != XR_SUCCESS ||
        (location.locationFlags & required) != required || !ValidPose(location.pose))
      return false;
    out = location.pose;
    return true;
  }

  // A partial teardown must forget only the owner that was actually destroyed.
  void SwapchainDestroyed(XrSwapchain chain) {
    if (!OnOwner() || !chain) return;
    for (auto& lease : leases_) if (lease.chain == chain) lease = {};
  }

  // Only after the owning swapchains have actually been destroyed.
  void SwapchainsDestroyed() {
    if (!OnOwner()) return;
    leases_[0] = leases_[1] = {};
    active_ = acquired_ = false;
    session_ = XR_NULL_HANDLE;
    space_ = XR_NULL_HANDLE;
    head_pose_ = {};
    owner_ = {};
  }

  // Ambiguous frame-end failure needs a session stop/restart, not just new images.
  void SessionEnded() {
    if (!OnOwner()) return;
    // Session stop resolves frame ambiguity, not ownership of live images.
    active_ = acquired_ = false;
    faulted_ = false;
  }

 private:
  bool OnOwner() const {
    return owner_ == std::thread::id{} || owner_ == std::this_thread::get_id();
  }
  bool End(const XrCompositionLayerProjection* layer) {
    const auto* header = reinterpret_cast<const XrCompositionLayerBaseHeader*>(layer);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state_.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = layer ? 1 : 0;
    end.layers = layer ? &header : nullptr;
    active_ = false;  // never retry an ambiguous xrEndFrame
    const bool ended = xrEndFrame(session_, &end) == XR_SUCCESS;
    if (!ended) faulted_ = true;
    return ended;
  }
  static bool ValidPose(const XrPosef& pose) {
    const auto& p = pose.position;
    const auto& q = pose.orientation;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(q.x) || !std::isfinite(q.y) ||
        !std::isfinite(q.z) || !std::isfinite(q.w)) return false;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return norm > 0.5f && norm < 1.5f;
  }
  static bool ValidView(const XrView& view) {
    const auto& f = view.fov;
    return ValidPose(view.pose) && std::isfinite(f.angleLeft) &&
           std::isfinite(f.angleRight) && std::isfinite(f.angleUp) &&
           std::isfinite(f.angleDown) && f.angleLeft < f.angleRight &&
           f.angleDown < f.angleUp && f.angleLeft > -1.5707963f &&
           f.angleRight < 1.5707963f && f.angleDown > -1.5707963f &&
           f.angleUp < 1.5707963f;
  }
  struct Lease { XrSwapchain chain = XR_NULL_HANDLE; bool ready = false; } leases_[2];
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace space_ = XR_NULL_HANDLE;
  XrSwapchain chains_[2]{};
  uint32_t indices_[2]{};
  XrFrameState state_{XR_TYPE_FRAME_STATE};
  XrView views_[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  XrPosef head_pose_{};
  std::thread::id owner_{};
  std::uint64_t token_ = 0;
  bool active_ = false, acquired_ = false, faulted_ = false;
};

}  // namespace dsvr
