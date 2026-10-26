#include "camhook.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdint>

#include "../common/logging.h"
#include "../common/memory.h"
#include "camera.h"
#include "calibration.h"
#include "camera_observer.h"
#include "arm_ik.h"
#include "diagnostics.h"
#include "input.h"
#include "xr.h"
#include "vr_trace.h"
#include "lab_runner.h"
#include "cpu_frame_timer.h"

namespace dsvr {
namespace {

// ChrFollowCam update, RVA verified in the live pinned build together with
// its prologue (read from the running process 2026-10-12):
//   40 55 53 56 41 54 41 55 48 8D AC 24 ...
constexpr std::uintptr_t kFollowCamUpdateRva = 0x236c60;
constexpr unsigned char kPrologue[] = {0x40, 0x55, 0x53, 0x56,
                                       0x41, 0x54, 0x41, 0x55};

using FollowCamUpdateFn = void(__fastcall*)(void*, float, void*, float*);
FollowCamUpdateFn g_orig = nullptr;
std::uint64_t g_tracking_epoch = 0;  // game-thread only
bool g_observer_only = false;  // published before enabling the hook; never reloaded

struct LabObservationOnExit {
  void* camera;
  ~LabObservationOnExit() {
    if (g_observer_only) return;
    if (!LabWantsObservation()) return;
    LabObservation observation{};
    observation.at_ms = GetTickCount64();
    CameraReadLabObservation(camera, observation);
    // Scene identity must come from the native gameplay/load adapter. An XR
    // reference-space epoch is not a scene epoch and must not masquerade as one.
    LabObserveGame(observation);
  }
};

void __fastcall FollowCamUpdateDetour(void* self, float dt, void* arg3,
                                      float* arg4) {
  LabObservationOnExit lab_sample{self};
  VrTraceEvent trace{};
  const bool capturing = VrTraceActive();
  trace.capture_id = VrTraceCaptureId();
  trace.event = "follow_enter";
  trace.object = reinterpret_cast<std::uintptr_t>(self);
  trace.values[0] = dt;
  if (capturing) VrTraceRecord(trace);
  CpuStageScope native_time(CpuStage::NativeCamera);
  if(!g_observer_only)CameraPrepareNativeUpdate(self);
  g_orig(self, dt, arg3, arg4);
  native_time.Stop();
  CpuStageScope mod_time(CpuStage::CameraAndTargets);
  trace.event = "follow_exit";
  if (capturing) VrTraceRecord(trace);
  if (g_observer_only) {
    if (capturing) CameraObserveNative(self, trace.capture_id);
    return;  // even CameraResetHeadlook can write; do not enter it
  }
  CameraTraceFollowCam(self);
  Config cfg = DiagnosticsCurrentConfig();
  if (!cfg.headlook_enabled || XrMenuPresentationActive()) {
    CameraResetHeadlook();
    XrRecordCameraSequence(0);
    g_tracking_epoch = 0;
    return;
  }
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) {
    CameraResetHeadlook();
    XrRecordCameraSequence(0);
    g_tracking_epoch = 0;
    (void)InputTakeTurnYaw();  // never replay a turn after tracking loss
    return;
  }
  if (g_tracking_epoch != frame.space_epoch) {
    CameraTrackingSpaceChanged(frame.space_epoch);
    CameraResetHeadlook();
    g_tracking_epoch = frame.space_epoch;
    (void)InputTakeTurnYaw();
  }
  const float* q = frame.head_quat;
  const float* p = frame.head_pos;
  CpuStageScope apply_time(CpuStage::CameraApply);
  // FOV comfort scaler: narrows/widens the game frustum around the
    // headset truth (1.0 = untouched). Aspect is a headset property and
    // is never scaled; the scaled vfov stays inside the camera's accepted
    // range or the write is refused downstream.
    const float comfort_vfov = std::isfinite(cfg.fov_scale) && cfg.fov_scale > 0.0f
        ? frame.vfov_rad * cfg.fov_scale : frame.vfov_rad;
    float native_reach = 0.f, native_head = 0.f, native_shoulders = 0.f;
    std::uint64_t model_revision = 0, stature_revision = 0;
    ArmIkNativeReach(native_reach, model_revision);
    // A missing upright head measurement clears its own output revision. It
    // must not erase the valid reach revision needed by the arm-fit fallback.
    if (ArmIkNativeStature(native_head, native_shoulders, stature_revision)) {
      if (model_revision && model_revision != stature_revision) {
        XrRecordCameraSequence(0);
        InputInvalidateHeadDirectedLocomotion();
        return;
      }
      model_revision = stature_revision;
    }
    const auto mapping = ResolveCalibrationMapping(CalibrationCurrent(),
        cfg.eye_height, cfg.play_style == "seated", cfg.world_scale,
        native_reach, model_revision, native_head, native_shoulders,
        cfg.embodiment_fit != "arm", cfg.eye_height_override, cfg.avatar_scale,
        cfg.seated_height_offset_m);
    if (!mapping.valid) {
      XrRecordCameraSequence(0);
      InputInvalidateHeadDirectedLocomotion();
      return;
    }
    static CalibrationMapping reported;
    if (!reported.valid || reported.world_scale!=mapping.world_scale ||
        reported.calibration_revision!=mapping.calibration_revision ||
        reported.model_revision!=mapping.model_revision ||
        reported.fit!=mapping.fit) {
      DSVR_LOG("CalibrationFit: %.5f native units/m, eye %.3f native, trim %.3f, "
               "avatar %.3f, native head %.3f native, shoulders %.3f native, "
               "calibration=%llu model=%llu anchor=%s (independent mesh resize unverified)",
               (double)mapping.world_scale,(double)mapping.eye_height,(double)mapping.world_trim,
               (double)mapping.avatar_scale,
               (double)mapping.native_head_height,(double)mapping.native_shoulder_width,
               (unsigned long long)mapping.calibration_revision,
               (unsigned long long)mapping.model_revision,
               mapping.fit==CalibrationFit::Head?"native head bone":
               mapping.fit==CalibrationFit::Arm?"native arm geometry":"manual fallback");
      // A requested head anchor with no upright native head measurement yet is
      // a real difference in where the camera sits, so say so once instead of
      // letting the user wonder why the avatar still does not match.
      static bool head_fallback_warned=false;
      if (cfg.embodiment_fit!="arm"&&mapping.fit!=CalibrationFit::Head&&
          !head_fallback_warned) {
        head_fallback_warned=true;
        DSVR_WARN("CalibrationFit: embodiment_fit=\"head\" requested, but no upright "
                  "native Head-bone measurement is available yet; using arm fit "
                  "when available, otherwise manual scale, until an upright pose is observed");
      }
      reported=mapping;
    }
    const bool applied = CameraApplyFirstPerson(self, q[0], q[1], q[2], q[3], p[0], p[1], p[2],
                             mapping.eye_height, mapping.world_scale,
                              cfg.forward_offset_m * mapping.world_scale, InputTakeTurnYaw(),
                              comfort_vfov, frame.fov_aspect,
                              cfg.eye_clearance_m * mapping.world_scale, &mapping);
  apply_time.Stop();
  if (!applied) {
    InputInvalidateHeadDirectedLocomotion();
    XrRecordCameraSequence(0);
    return;
  }
  XrRecordCameraFrame(frame, comfort_vfov);
  CameraPublishHeadDirectedLocomotion(frame.space_epoch);
  if (VrTraceActive()) {
    trace.event = "camera_applied";
    trace.seq = frame.sequence;
    trace.epoch = frame.space_epoch;
    trace.predicted_ns = frame.predicted_time_ns;
    trace.period_ns = frame.display_period_ns;
    if (GuardedReadBytes(trace.object + 0x10, trace.values, sizeof(trace.values)))
      VrTraceRecord(trace);
  }
  // Bind this exact camera prediction for the natural post-animation arm hook.
  CpuStageScope targets_time(CpuStage::ArmTargets);
  ArmIkTick(frame);
}

}  // namespace

bool CameraHookInstall(bool vr_enabled, bool observer_only) {
  if (g_orig) return false;  // never change an installed hook's permissions
  if (!vr_enabled && !observer_only) {
    DSVR_LOG("CamHook: skipped (vr_enabled=false)");
    return false;
  }
  std::uintptr_t base = ModuleBase(nullptr);
  auto target = (void*)(base + kFollowCamUpdateRva);
  if (!BytesMatch((std::uintptr_t)target, kPrologue, sizeof(kPrologue))) {
    DSVR_ERROR("CamHook: prologue mismatch at RVA 0x%llx; refusing to hook",
               (unsigned long long)kFollowCamUpdateRva);
    return false;
  }
  MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    DSVR_ERROR("CamHook: MinHook initialize failed (%d)", (int)init);
    return false;
  }
  if (MH_CreateHook(target, (void*)&FollowCamUpdateDetour, (void**)&g_orig) !=
      MH_OK) {
    DSVR_ERROR("CamHook: MH_CreateHook failed");
    return false;
  }
  g_observer_only = observer_only;
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    g_orig = nullptr;
    DSVR_ERROR("CamHook: MH_EnableHook failed");
    return false;
  }
  DSVR_LOG("CamHook: ChrFollowCam hook installed (RVA 0x%llx, mode=%s)",
           (unsigned long long)kFollowCamUpdateRva,
           observer_only ? "observation only" : "legacy VR camera");
  return true;
}

void CameraHookUninstall() {
  ArmIkHookUninstall();
  if (g_orig) {
    std::uintptr_t base = ModuleBase(nullptr);
    MH_DisableHook((void*)(base + kFollowCamUpdateRva));
    g_orig = nullptr;
  }
}

}  // namespace dsvr
