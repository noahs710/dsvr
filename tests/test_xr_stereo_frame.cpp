// Real frame/image-order implementation with a fake XR runtime, not native stereo proof.
#include "../src/core/xr_stereo_frame.h"
#include <cstdio>
#include <limits>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
XrSession session = reinterpret_cast<XrSession>(1);
XrSpace space = reinterpret_cast<XrSpace>(2);
XrSpace head_space = reinterpret_cast<XrSpace>(3);
XrSwapchain chains[2]{reinterpret_cast<XrSwapchain>(11), reinterpret_cast<XrSwapchain>(12)};
bool acquired[2]{}, ready[2]{}, begun = false, bad = false;
bool should_render = true, bad_pose = false;
uint32_t view_count = 2, submitted = 0, ends = 0, waits = 0, acquires = 0;
uint32_t head_locates = 0;
XrViewStateFlags view_flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
XrSpaceLocationFlags head_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                XR_SPACE_LOCATION_POSITION_VALID_BIT;
XrResult wait_result = XR_SUCCESS, begin_result = XR_SUCCESS, end_result = XR_SUCCESS;
XrResult head_result = XR_SUCCESS;
XrPosef head_pose{{0, .38268343f, 0, .92387953f}, {1, 1.5f, 2}};
XrResult image_wait_result = XR_SUCCESS;
XrSwapchain acquire_failure = XR_NULL_HANDLE, release_failure = XR_NULL_HANDLE;
int Side(XrSwapchain chain) { return chain == chains[0] ? 0 : 1; }
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession s, const XrFrameWaitInfo*, XrFrameState* state) {
  ++waits;
  if (s != session || begun) bad = true;
  state->predictedDisplayTime = 123456;
  state->predictedDisplayPeriod = 11111111;
  state->shouldRender = should_render;
  return wait_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*) {
  if (begun) bad = true;
  begun = XR_SUCCEEDED(begin_result);
  return begin_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo* info,
    XrViewState* state, uint32_t capacity, uint32_t* count, XrView* views) {
  if (!begun || info->displayTime != 123456 || info->space != space || capacity != 2) bad = true;
  state->viewStateFlags = view_flags;
  *count = view_count;
  for (unsigned eye = 0; eye < 2; ++eye) {
    // Canted eyes deliberately differ from the primary viewer's heading.
    views[eye].pose.orientation.y = bad_pose ? 0.0f : (eye ? .17364818f : -.17364818f);
    views[eye].pose.orientation.w = bad_pose ? 0.0f : .98480775f;
    views[eye].pose.position.x = eye ? .032f : -.032f;
    views[eye].fov = {-1.0f, 1.0f, 1.0f, -1.0f};
  }
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace target, XrSpace base,
    XrTime time, XrSpaceLocation* location) {
  ++head_locates;
  if (target != head_space || base != space || time != 123456) bad = true;
  location->locationFlags = head_flags;
  location->pose = head_pose;
  return head_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageAcquireInfo*, uint32_t* index) {
  ++acquires;
  if (chain == acquire_failure) return XR_ERROR_RUNTIME_FAILURE;
  const auto i = Side(chain);
  if (acquired[i]) bad = true;
  acquired[i] = true;
  *index = i + 1;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageWaitInfo* info) {
  const auto i = Side(chain);
  if (!acquired[i] || info->timeout > 5'000'000) bad = true;
  if (image_wait_result == XR_SUCCESS) ready[i] = true;
  return image_wait_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain chain,
    const XrSwapchainImageReleaseInfo*) {
  const auto i = Side(chain);
  if (!acquired[i] || !ready[i]) bad = true;
  if (chain == release_failure) return XR_ERROR_RUNTIME_FAILURE;
  acquired[i] = ready[i] = false;
  return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo* info) {
  if (!begun || info->displayTime != 123456) bad = true;
  begun = false;
  ++ends;
  submitted = info->layerCount;
  if (submitted) {
    const auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
    if (layer->viewCount != 2 || layer->space != space || acquired[0] || acquired[1] ||
        layer->views[0].pose.position.x >= 0 || layer->views[1].pose.position.x <= 0 ||
        layer->views[0].subImage.swapchain != chains[0] ||
        layer->views[1].subImage.swapchain != chains[1] ||
        layer->views[0].pose.orientation.y != -.17364818f ||
        layer->views[1].pose.orientation.y != .17364818f) bad = true;
  }
  return end_result;
}

int main() {
  dsvr::XrStereoFrame frame;
  CHECK(!frame.Begin(XR_NULL_HANDLE, space, head_space) && waits == 0);
  CHECK(!frame.Begin(session, space, XR_NULL_HANDLE) && waits == 0);
  CHECK(frame.Begin(session, space, head_space));
  CHECK(head_locates == 1);
  CHECK(frame.HeadPose().orientation.y == head_pose.orientation.y &&
        frame.HeadPose().position.x == 1 && frame.HeadPose().position.y == 1.5f &&
        frame.HeadPose().position.z == 2);
  CHECK(frame.HeadPose().orientation.y != frame.View(0).pose.orientation.y &&
        frame.HeadPose().position.x !=
            (frame.View(0).pose.position.x + frame.View(1).pose.position.x) * .5f);
  const auto first = frame.Token();
  CHECK(!frame.Begin(session, space, head_space) && waits == 1);
  bool foreign = true;
  std::thread other([&] { foreign = frame.Acquire(chains[0], chains[1]); frame.Cancel(); });
  other.join();
  CHECK(!foreign && frame.Active() && acquires == 0);
  CHECK(!frame.Acquire(chains[0], chains[0]));
  CHECK(frame.Acquire(chains[0], chains[1]));
  CHECK(!frame.Acquire(chains[0], chains[1]));
  CHECK(frame.ImageIndex(0) == 1 && frame.ImageIndex(1) == 2);
  CHECK(frame.Submit(1000, 1000) && submitted == 1 && !frame.Busy());
  CHECK(!frame.Submit(1000, 1000));
  frame.Cancel(); CHECK(ends == 1);

  wait_result = XR_ERROR_RUNTIME_FAILURE;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy() && ends == 1);
  wait_result = XR_SESSION_LOSS_PENDING;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy() && ends == 2 && submitted == 0);
  wait_result = XR_SUCCESS;

  should_render = false;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy() && submitted == 0);
  should_render = true;
  begin_result = XR_FRAME_DISCARDED;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  begin_result = XR_SUCCESS;
  view_flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  view_flags |= XR_VIEW_STATE_POSITION_VALID_BIT;
  view_count = 1;
  CHECK(!frame.Begin(session, space, head_space));
  view_count = 2;
  bad_pose = true;
  CHECK(!frame.Begin(session, space, head_space));
  bad_pose = false;

  // Valid eyes never excuse an invalid primary-viewer location. The begun
  // frame is cancelled before borrowing either swapchain image.
  const auto before_head_ends = ends, before_head_acquires = acquires;
  head_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  CHECK(ends == before_head_ends + 1 && acquires == before_head_acquires && submitted == 0);
  head_flags = XR_SPACE_LOCATION_POSITION_VALID_BIT;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  head_flags |= XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  head_result = XR_ERROR_RUNTIME_FAILURE;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  head_result = XR_SESSION_LOSS_PENDING;
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  head_result = XR_SUCCESS;
  const auto valid_head = head_pose;
  head_pose.position.x = std::numeric_limits<float>::infinity();
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  head_pose = valid_head;
  head_pose.orientation = {};
  CHECK(!frame.Begin(session, space, head_space) && !frame.Busy());
  head_pose = valid_head;
  CHECK(acquires == before_head_acquires && submitted == 0);

  // The mono publisher uses this same location/refusal operation. Failed
  // locations clear a previously valid output instead of exposing stale pose.
  XrPosef located{};
  CHECK(dsvr::XrStereoFrame::LocateHeadPose(head_space, space, 123456, located));
  CHECK(located.orientation.y == valid_head.orientation.y && located.position.x == 1);
  head_flags = 0;
  CHECK(!dsvr::XrStereoFrame::LocateHeadPose(head_space, space, 123456, located));
  CHECK(located.orientation.w == 0 && located.position.x == 0);
  head_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
  const auto before_head_locates = head_locates;
  CHECK(!dsvr::XrStereoFrame::LocateHeadPose(XR_NULL_HANDLE, space, 123456, located));
  CHECK(!dsvr::XrStereoFrame::LocateHeadPose(head_space, XR_NULL_HANDLE, 123456, located));
  CHECK(!dsvr::XrStereoFrame::LocateHeadPose(head_space, space, 0, located));
  CHECK(head_locates == before_head_locates);

  CHECK(frame.Begin(session, space, head_space) && frame.Token() > first);
  acquire_failure = chains[1];
  CHECK(!frame.Acquire(chains[0], chains[1]));
  frame.Cancel();
  CHECK(!frame.Busy() && !acquired[0] && submitted == 0);
  acquire_failure = XR_NULL_HANDLE;

  CHECK(frame.Begin(session, space, head_space));
  image_wait_result = XR_TIMEOUT_EXPIRED;
  CHECK(!frame.Acquire(chains[0], chains[1]));
  frame.Cancel();
  CHECK(frame.Busy() && acquired[0] && !ready[0] && submitted == 0);
  const auto before = waits;
  CHECK(!frame.Begin(session, space, head_space) && waits == before);
  image_wait_result = XR_SUCCESS;
  CHECK(frame.Begin(session, space, head_space) && !acquired[0]);
  CHECK(frame.Acquire(chains[0], chains[1]));
  release_failure = chains[1];
  CHECK(!frame.Submit(1000, 1000) && submitted == 0 && frame.Busy());
  release_failure = XR_NULL_HANDLE;
  CHECK(frame.Drain() && !frame.Busy());
  CHECK(frame.Begin(session, space, head_space) && frame.Acquire(chains[0], chains[1]));
  CHECK(!frame.Submit(0, 1000) && submitted == 0 && !frame.Busy());

  CHECK(frame.Begin(session, space, head_space));
  end_result = XR_ERROR_RUNTIME_FAILURE;
  frame.Cancel();
  CHECK(frame.Busy() && !frame.Begin(session, space, head_space));
  frame.SwapchainsDestroyed();
  CHECK(!frame.Begin(session, space, head_space));  // a new image cannot repair an ambiguous frame end
  frame.SessionEnded();
  end_result = XR_SUCCESS;
  CHECK(frame.Begin(session, space, head_space));
  frame.Cancel();
  begin_result = XR_ERROR_RUNTIME_FAILURE;
  CHECK(!frame.Begin(session, space, head_space) && frame.Busy());
  frame.SwapchainsDestroyed();
  CHECK(!frame.Begin(session, space, head_space));
  frame.SessionEnded();
  begin_result = XR_SUCCESS;
  CHECK(frame.Begin(session, space, head_space));
  frame.Cancel();
  // Stopping a session does not destroy held swapchain images. Preserve
  // ownership until the actual chain is drained or successfully destroyed.
  CHECK(frame.Begin(session, space, head_space));
  image_wait_result = XR_TIMEOUT_EXPIRED;
  CHECK(!frame.Acquire(chains[0], chains[1]));
  frame.Cancel();
  frame.SessionEnded();
  CHECK(frame.Busy() && acquired[0]);
  frame.SwapchainDestroyed(chains[1]);
  CHECK(frame.Busy());
  acquired[0] = ready[0] = false;  // Fake successful destruction of this owner.
  frame.SwapchainDestroyed(chains[0]);
  CHECK(!frame.Busy());
  image_wait_result = XR_SUCCESS;
  CHECK(!bad && !begun && !frame.Busy());
  std::puts("PASS XR stereo frame ordering/refusal/recovery (fake runtime, not native rendering)");
}
