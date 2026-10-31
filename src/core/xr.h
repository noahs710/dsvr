#pragma once
#include "display_rate.h"
#include "tracking_frame.h"
#include "menu_presentation.h"

namespace dsvr {

// P2b: OpenXR probe on the game's render thread.
// Creates instance -> HMD system -> D3D11 session, logs headset name and
// recommended eye resolution, follows session state to READY/running.
// P2c adds mono frame submission (see XrSubmitMonoFrame).
void XrOnDeviceCaptured(void* d3d11_device);  // call once, render thread
void XrOnPresentFrame();                      // cheap event poll per frame
bool XrSessionRunning();
// True while the debounced menu snapshot wants the spatial canvas
// (menu_state.h); stable across menu navigation flicker.
bool XrMenuPresentationActive();
void XrToggleSanctuary();
void XrRecenterSanctuary();

// P2c: submit the game backbuffer (mono, both eyes) for this Present.
// Call BEFORE the original Present, on the render thread. No-op unless
// the session is running.
void XrSubmitMonoFrame(void* backbuffer_texture2d);

// Borrowed eye render targets, valid only until End on the same render thread.
// FOV order: left/right/up/down radians; poses use OpenXR LOCAL coordinates.
// This API schedules XR, not native scene replay or rendered-view certification.
struct NativeStereoEye {
  void* texture2d = nullptr;
  float orientation[4] = {0, 0, 0, 1};
  float position[3]{};
  float fov[4]{};
};
struct NativeStereoFrame {
  std::uint64_t token = 0, tracking_sequence = 0, space_epoch = 0;
  long long predicted_time_ns = 0;
  unsigned width = 0, height = 0;
  long long color_format = 0;  // DXGI_FORMAT; use an explicit typed RTV
  NativeStereoEye eyes[2]{};
};
// Call before native command construction, using an existing same-device target
// only for format/size setup. No scene callback is invoked and no mono image copied.
bool XrBeginNativeStereoFrame(void* reference_texture2d, NativeStereoFrame& out);
// Exactly 3 means both borrowed targets were rendered; any other mask cancels.
// Stale tokens/wrong threads cannot submit or cancel another owner's frame.
bool XrEndNativeStereoFrame(std::uint64_t token, unsigned rendered_eye_mask);

// Compatibility head getter: predicted VIEW origin/orientation in the base space.
// Uses coherent publication with focus, pose validity and freshness checks.
bool XrLatestHeadPose(float quat_xyzw[4], float pos_xyz[3]);
// Coherent head + physical left/right hands, <=100 ms local sample age.
// False on focus/pose/session loss; output is cleared on failure.
bool XrLatestTrackingFrame(TrackingFrame& out);
// Current right-grip ray projected into the anchored spatial menu texture.
// spatial_menu remains true on invalid tracking so callers suppress gamepad
// select while clearing the cursor/button state fail-closed.
MenuPointerState XrMenuPointerInput(unsigned display_width,
                                    unsigned display_height);
// Diagnostic camera-application tag only; not a rendered-view provenance tag.
void XrRecordCameraSequence(std::uint64_t sequence);
// The exact physical packet applied by the natural camera hook, including the
// actual symmetric game FOV. Mono composition uses its center-view pose rather
// than relabelling an already-rendered image with the next runtime prediction.
void XrRecordCameraFrame(const TrackingFrame& frame, float rendered_vfov_rad);
// Packet bound by the most recent natural camera update. A newer XR render
// publication does not replace that camera binding or prove a new native view.
std::uint64_t XrAppliedCameraSequence();

// Widest-eye vertical FOV/aspect from a fresh coherent tracking packet.
// Camera/IK use the whole packet to avoid a second-generation getter read.
bool XrViewFovTotals(float& vfov_rad, float& aspect);

// Observed display/application cadence only; it never configures a game cap.
// A zero rate means no live OpenXR timing sample is available. Snapshot reads
// are safe from any thread.
bool XrEffectiveRate(EffectiveRate& out);
// Last shipped FB display refresh (Hz, 0 = never reported). Powers the
// launch-time auto-sync and the launcher's last-known-rate sidecar.
double XrLastDisplayRefreshHz();

// Last actual mono submission attempt. CPU total includes the separately
// reported runtime waits; GPU values cover only copy/draw command ranges and
// arrive asynchronously from an earlier frame (gpu_frame_sequence). No GPU
// readback, flush or wait is performed to answer this snapshot query.
struct RenderMetrics {
  std::uint64_t frame_sequence = 0, gpu_frame_sequence = 0;
  std::uint64_t source_tracking_sequence = 0, target_tracking_sequence = 0;
  std::uint64_t image_timeouts = 0, held_image_skips = 0;
  double cpu_frame_ms = 0, xr_wait_ms = 0, image_wait_ms = 0;
  double source_copy_cpu_ms = 0, eye_blit_cpu_ms = 0, end_frame_cpu_ms = 0;
  double gpu_copy_ms = 0, gpu_blit_ms = 0;
  unsigned source_copies = 0, eye_draws = 0;
  unsigned eye_age_frames[2] = {0, 0};  // afr_policy freshness; mono submits both eyes (age 0)
  unsigned source_width = 0, source_height = 0, eye_width = 0, eye_height = 0;
  bool submitted = false, theater = false, gpu_valid = false, tracking_published = false;
};
bool XrRenderMetricsSnapshot(RenderMetrics& out);

}  // namespace dsvr
