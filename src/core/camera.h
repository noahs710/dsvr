#pragma once
#include <cstdint>
#include "camera_math.h"

namespace dsvr {
// Only a real XR reference-space change invalidates the physical origin.
// Native camera/actor rebinding must preserve it across deaths and loads.
void CameraTrackingSpaceChanged(std::uint64_t epoch);

struct LabObservation;

// Game thread, after native follow-camera update. Logic-position anchor,
// calibrated six-DOF tracking with independent, intentionally turned heading.
// Native movement/action facing cannot continuously rotate this view frame.
// Returns true only after guarded camera writes; no-op at title/loading.
bool CameraApplyFirstPerson(void* follow_cam_self, float qx, float qy,
                            float qz, float qw, float px, float py, float pz,
                            float eye_height, float world_scale,
                            float forward_offset_m, float turn_yaw_rad,
                            float vfov_rad, float fov_aspect);
void CameraResetHeadlook();
// Natural camera owner only, immediately before its native update. Restore
// still-owned render overrides so native follow/collision logic sees its own
// previous output rather than feeding the HMD override back into its solver.
void CameraPrepareNativeUpdate(void* follow_cam_self);
// Game-thread IK consumes exactly the camera's anchor/calibration/body frame.
bool CameraTrackingFrame(Vec3& head_world, Vec3& right, Vec3& up, Vec3& fwd,
                         float reference_quat[4], Vec3* anchor_world = nullptr);
// Head orientation basis from the same written frame (the body frame above
// only carries heading). Lets the torso follow where the user looks.
bool CameraHeadBasis(Vec3& right, Vec3& up, Vec3& fwd);
// Game thread: publish the applied head and native movement frame for the
// render-thread pad mapper. Invalid data clears the snapshot.
void CameraPublishHeadDirectedLocomotion(std::uint64_t space_epoch);
// Fresh natural-follow title heuristic, not an in-game menu detector. No live
// native memory is read by this cross-thread getter; absent/stale means unknown.
bool CameraIsTitle();
// Fresh natural follow-camera sample with a validated local player root.
// Unknown/title/loading/stale returns false; independent of XR presentation.
bool CameraGameplayReady();
// Read-only diagnostic geometry/identity, NOT a verified gameplay/safe-scene gate.
void CameraReadLabObservation(void* follow_cam_self, LabObservation& out);

// Owning natural callback: publish the bounded title heuristic and log one
// follow-camera basis sample. Never reads/writes beyond CameraMan's container.
void CameraTraceFollowCam(void* follow_cam_self);

}  // namespace dsvr
