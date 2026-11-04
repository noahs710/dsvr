#pragma once

namespace dsvr {

// Byte-gated natural ChrFollowCam update. Observer mode only samples raw
// camera identity/transform after the original; it cannot enter legacy camera
// writes, restoration, IK, XR or input paths, regardless of live config.
// Both permissions are startup-only. The legacy VR camera layout is disputed;
// do not use it as a stereo integration contract.
bool CameraHookInstall(bool vr_enabled, bool observer_only = false);
void CameraHookUninstall();

}  // namespace dsvr
