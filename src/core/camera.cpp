#include "camera.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "../common/logging.h"
#include "../common/memory.h"
#include "camera_math.h"
#include "arm_ik.h"
#include "cpu_frame_timer.h"
#include "calibration.h"
#include "input.h"
#include "xr.h"
#include "vr_heading.h"
#include "vr_trace.h"
#include "lab_scenario.h"

namespace dsvr {
namespace {

bool FiniteBasis(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         VLen(v) > 0.5f && VLen(v) < 1.5f;
}
bool FinitePos(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         std::fabs(v.x) < 10000.0f && std::fabs(v.y) < 10000.0f &&
         std::fabs(v.z) < 10000.0f;
}

// Player physics pos (verified chain): WorldChrMan -> PlayerIns ->
// +0x7b8 -> +0x2e0 -> +0x120. Returns false at title/loading.
bool ReadPlayerPos(Vec3& out, std::uintptr_t* player_out = nullptr) {
  std::uintptr_t base = ModuleBase(nullptr);
  std::uintptr_t world = 0, player = 0;
  if (!GuardedRead(base + 0x1c77e50, world) || !world) return false;
  if (!GuardedRead(world + 0x68, player) || !player) return false;
  std::uintptr_t hops[]{0x7b8, 0x2e0};
  auto mid = FollowChain(player, hops, 2);
  if (!mid) return false;
  float v[4]{};
  if (!GuardedReadBytes(*mid + 0x120, v, sizeof(v))) return false;
  out = {v[0], v[1], v[2]};
  if (!FinitePos(out)) return false;
  if (player_out) *player_out = player;
  return true;
}

// ChrPosData (verified chain PlayerIns+0x18 -> +0x28): yaw at +0x4 rad.
// Its relation to the rendered model is not a camera-heading contract.
std::uintptr_t PlayerPosData() {
  std::uintptr_t base = ModuleBase(nullptr);
  std::uintptr_t world = 0, player = 0, ctrl = 0, posdata = 0;
  if (!GuardedRead(base + 0x1c77e50, world) || !world) return 0;
  if (!GuardedRead(world + 0x68, player) || !player) return 0;
  if (!GuardedRead(player + 0x18, ctrl) || !ctrl) return 0;
  if (!GuardedRead(ctrl + 0x28, posdata)) return 0;
  return posdata;
}

bool ReadPlayerYaw(float& yaw) {
  std::uintptr_t posdata = PlayerPosData();
  return posdata && GuardedRead(posdata + 0x4, yaw) && std::isfinite(yaw);
}

bool ReadPlayerRenderFacing(Vec3& forward) {
  const auto base = ModuleBase(nullptr);
  std::uintptr_t world=0, player=0, model=0, type=0, current_model=0;
  float root[12]{};
  if (!GuardedRead(base+0x1c77e50,world) || !world ||
      !GuardedRead(world+0x68,player) || !player ||
      !GuardedRead(player,type) || type!=base+0x13251f0 ||
      !GuardedRead(player+0x60,model) || !model ||
      !GuardedRead(model,type) || type!=base+0x12eaf30 ||
      !GuardedReadBytes(model+0x30,root,sizeof(root)) ||
      !GuardedRead(player+0x60,current_model) || current_model!=model) return false;
  for (float value : root) if (!std::isfinite(value)) return false;
  // Measured FLVER shoulders: native left is +model-X, right is -model-X.
  // Convert that right/up pair through the rendered model's 3x4 root. The
  // resulting facing is -model-Z, not an assumed ChrPosData Euler formula.
  Vec3 right{-root[0],-root[4],-root[8]};
  Vec3 up{root[1],root[5],root[9]};
  Vec3 back{root[2],root[6],root[10]};
  if (!FiniteBasis(right) || !FiniteBasis(up) || !FiniteBasis(back)) return false;
  right=VNorm(right); up=VNorm(up); back=VNorm(back);
  if (std::fabs(VDot(right,up))>1e-3f || std::fabs(VDot(right,back))>1e-3f ||
      std::fabs(VDot(up,back))>1e-3f || VDot(VCross(right,up),back)>-.999f)
    return false;
  const Vec3 facing=VCross(right,up);
  forward={facing.x,0,facing.z};
  return VLen(forward)>1e-4f;
}

// Logic position (ChrPosData +0x10, same chain as yaw). The current anchor
// policy prefers this; relative timing versus physics/render-model position
// during walking is still unmeasured. Physics remains the fallback.
bool ReadPlayerLogicPos(Vec3& out) {
  std::uintptr_t posdata = PlayerPosData();
  if (!posdata) return false;
  float v[3]{};
  if (!GuardedReadBytes(posdata + 0x10, v, sizeof(v))) return false;
  out = {v[0], v[1], v[2]};
  return FinitePos(out);
}

// Follow-cam committed matrix: rows at self+0x10/+0x20/+0x30, pos +0x40
// (copied from working +0x2D0.. at the end of its update -- decomp
// FUN_140236c60 tail: param_1[2..9] = param_1[0x5a..0x61]).
bool ReadFollowMatrix(void* self, Vec3& right, Vec3& up, Vec3& fwd,
                      Vec3& pos) {
  float buf[16]{};
  if (!GuardedReadBytes((std::uintptr_t)self + 0x10, buf, sizeof(buf)))
    return false;
  right = {buf[0], buf[1], buf[2]};
  up = {buf[4], buf[5], buf[6]};
  fwd = {buf[8], buf[9], buf[10]};
  pos = {buf[12], buf[13], buf[14]};
  return FiniteBasis(right) && FiniteBasis(up) && FiniteBasis(fwd) &&
         FinitePos(pos);
}

bool WriteMatrix(std::uintptr_t addr, Vec3 r, Vec3 u, Vec3 f, Vec3 p) {
  float out[16]{};
  out[0] = r.x; out[1] = r.y; out[2] = r.z; out[3] = 0;
  out[4] = u.x; out[5] = u.y; out[6] = u.z; out[7] = 0;
  out[8] = f.x; out[9] = f.y; out[10] = f.z; out[11] = 0;
  out[12] = p.x; out[13] = p.y; out[14] = p.z; out[15] = 1;
  return GuardedWriteBytes(addr, out, sizeof(out));
}

bool g_captured = false;
bool g_reference_captured = false;
std::uint64_t g_reference_epoch=0;
VrHeading g_view_heading;  // owning camera/game thread only
float g_q0[4] = {0, 0, 0, 1};
Vec3 g_p0{0, 0, 0};
Vec3 g_right0{1, 0, 0}, g_up0{0, 1, 0}, g_fwd0{0, 0, -1};
bool g_wrote = false;
float g_last_yaw = 0.0f;  // last stable head yaw (pole continuity)
Vec3 g_last_right{1, 0, 0};    // written right vector (vertical-look fallback)
Vec3 g_tracking_head{0, 0, 0};  // calibrated camera anchor shared with IK
Vec3 g_head_right{1, 0, 0}, g_head_up{0, 1, 0}, g_head_fwd{0, 0, 1};
std::uint64_t g_planar_heading_epoch = 0;
Vec3 g_last_planar_heading{0, 0, 1};
bool g_have_planar_heading = false;
Vec3 g_last_player{0, 0, 0};
bool g_have_last_player = false;
int g_turn_logs = 0;
// Follow-cam projection source (traced 2026-10-13: vfov at self+0x50,
// aspect at self+0x54; no working-copy pair near +0x310).
std::uintptr_t g_follow_projection_owner = 0;
float g_follow_original_projection[2]{};
float g_follow_written_projection[2]{};
std::uintptr_t g_follow_pose_owner=0;
unsigned g_follow_pose_mask=0;
float g_follow_native_pose[2][16]{},g_follow_written_pose[2][16]{};
unsigned long long g_frames = 0;
bool g_traced = false;
// Coherent timestamp + title heuristic from the owning natural callback.
// Present reads this value, never an unsynchronized live camera pointer.
std::atomic<unsigned long long> g_title_sample{0};
std::atomic<unsigned long long> g_gameplay_sample{0};

bool IsTitleCamera(Vec3 pos) {
  // Title/default camera sits at (0,4,-8) (observed live, twice).
  Vec3 d{pos.x - 0.0f, pos.y - 4.0f, pos.z - (-8.0f)};
  return VLen(d) < 1.0f;
}

}  // namespace

bool CameraIsTitle() {
  const auto sample = g_title_sample.load(std::memory_order_acquire);
  if (!sample) return false;  // unknown, not a verified front-end state
  const auto at = (sample >> 1) - 1;
  const auto now = GetTickCount64();
  return now >= at && now-at <= 250 && (sample & 1) != 0;
}

bool CameraGameplayReady() {
  const auto sample=g_gameplay_sample.load(std::memory_order_acquire);
  if (!sample)return false;
  const auto at=sample-1,now=GetTickCount64();
  return now>=at && now-at<=250;
}

void CameraReadLabObservation(void* self, LabObservation& out) {
  GuardedReadScope memory_reads;
  out.root_valid = ReadPlayerPos(out.root, &out.player);
  Vec3 right{}, up{}, forward{};
  out.camera_valid = self && ReadFollowMatrix(self, right, up, forward, out.camera);
  // Finite geometry and the title-camera heuristic cannot prove that menus,
  // death, loading, hazards or network state are safe for automated movement.
}

void CameraResetHeadlook() {
  GuardedReadScope memory_reads;
  InputInvalidateHeadDirectedLocomotion();
  if(g_follow_pose_owner)CameraPrepareNativeUpdate(reinterpret_cast<void*>(g_follow_pose_owner));
  // Called on the game thread after the original camera update. Restore
  // only our own values: do not overwrite a projection the game replaced.
  float current[2]{};
  if (g_follow_projection_owner &&
      GuardedReadBytes(g_follow_projection_owner + 0x50, current,
                       sizeof(current)) &&
      current[0] == g_follow_written_projection[0] &&
      current[1] == g_follow_written_projection[1])
    GuardedWriteBytes(g_follow_projection_owner + 0x50,
                      g_follow_original_projection,
                      sizeof(g_follow_original_projection));
  g_follow_projection_owner = 0;
  g_captured = false;
  // Theater, loading and temporary tracking loss only suspend the override.
  // Keep the user's physical reference and stick heading until an explicit
  // recenter or a genuinely new XR tracking space replaces them.
  g_wrote = false;
  g_last_yaw = 0.0f;
  g_have_last_player = false;
}

void CameraTrackingSpaceChanged(std::uint64_t epoch) {
  if(epoch==g_reference_epoch)return;
  g_reference_epoch=epoch;
  g_reference_captured=false;
  g_view_heading.Reset();
  CameraResetHeadlook();
}

void CameraPrepareNativeUpdate(void* owner) {
  GuardedReadScope memory_reads;
  const auto self=reinterpret_cast<std::uintptr_t>(owner);
  if(!self)return;
  if(g_follow_pose_owner==self) {
    const unsigned offsets[]={0x10,0x2d0};
    for(unsigned i=0;i<2;++i) {
      if(!(g_follow_pose_mask&(1u<<i)))continue;
      float current[16]{};
      if(GuardedReadBytes(self+offsets[i],current,sizeof(current)) &&
          std::memcmp(current,g_follow_written_pose[i],sizeof(current))==0 &&
          !GuardedWriteBytes(self+offsets[i],g_follow_native_pose[i],sizeof(current)))continue;
      g_follow_pose_mask&=~(1u<<i);
    }
    if(!g_follow_pose_mask)g_follow_pose_owner=0;
  }
  if(g_follow_projection_owner==self) {
    float current[2]{};
    if(GuardedReadBytes(self+0x50,current,sizeof(current)) &&
        std::memcmp(current,g_follow_written_projection,sizeof(current))==0 &&
        !GuardedWriteBytes(self+0x50,g_follow_original_projection,sizeof(current)))return;
    g_follow_projection_owner=0;
  }
}

bool CameraApplyFirstPerson(void* follow_cam_self, float qx, float qy,
                            float qz, float qw, float px, float py, float pz,
                            float eye_height, float world_scale,
                            float forward_offset_m, float turn_yaw_rad,
                            float vfov_rad, float fov_aspect) {
  GuardedReadScope memory_reads;
  g_wrote = false;  // every refused application invalidates the previous arm frame
  static std::uint64_t recenter_seen=0;
  const auto recenter=CalibrationRecenterGeneration();
  if (recenter_seen!=recenter) {
    recenter_seen=recenter;
    g_captured=false;
    g_reference_captured=false;
    g_view_heading.Reset();
  }
  if (!follow_cam_self) return false;
  CpuStageScope reads_time(CpuStage::CameraReads);
  Vec3 physics;
  Vec3 gr, gu, gf, gp;
  if (!ReadPlayerPos(physics) || !ReadFollowMatrix(follow_cam_self, gr, gu, gf,
                                                  gp) ||
      IsTitleCamera(gp)) {
    g_captured = false;
    g_wrote = false;
    return false;
  }
  // Preserve the current anchor policy pending same-frame trace evidence.
  // The v0.15.2 change did NOT resolve the reported walking lag; teleport
  // write-acceptance delay does not establish normal-walking physics lag.
  Vec3 logic{};
  const bool have_logic = ReadPlayerLogicPos(logic);
  Vec3 player = SelectAnchorPos(logic, have_logic, physics);

  // Rebind the native anchor after a suspend or teleport/load. Body animation
  // can turn during those transitions, so only seed heading on first capture
  // or an explicit recenter/new XR space; never replace the physical origin.
  bool teleported = g_have_last_player &&
                    VLen(Vec3{player.x - g_last_player.x,
                              player.y - g_last_player.y,
                              player.z - g_last_player.z}) > 3.0f;
  if (!g_captured || teleported) {
    Vec3 native_forward{};
    if (!ReadPlayerRenderFacing(native_forward) ||
        (!g_view_heading.IsReady() && !g_view_heading.SeedOnce(native_forward)))
      return false;
    ArmIkResetCalibration();  // transient rig binding only; saved user measurements survive
    if(!g_reference_captured) {
      const float tracked[4] = {qx, qy, qz, qw};
      CaptureYawReference(tracked, g_q0);
      g_p0 = {px, py, pz};
      g_reference_captured=true;
    }

    g_captured = true;
    DSVR_LOG("Camera: FP %s (player pos=%.2f,%.2f,%.2f src=%s; "
             "render-root facing=%.3f,%.3f; independent VR heading, models native)",
             teleported ? "anchor rebound after teleport" : "anchor bound",
             player.x, player.y, player.z,
             have_logic ? "logic" : "physics-fallback", native_forward.x, native_forward.z);
  }

  reads_time.Stop();
  // Explicit stick turns rotate VR heading only. Never interpret a native
  // follow-camera output as input, or fight native animation with yaw writes.
  if (std::isfinite(turn_yaw_rad) && turn_yaw_rad != 0.0f) {
    if (g_view_heading.ApplyTurn(turn_yaw_rad) && g_turn_logs < 3) {
      ++g_turn_logs;
      DSVR_LOG("Camera: stick turn applied to independent VR heading (delta=%.3f rad)",
               (double)turn_yaw_rad);
    }
  }
  // Locomotion anchor: the torso lean solver measures the head against
  // this same position, so stick-walking (which moves the anchor and the
  // head together) produces no lean. Only a physical lean — head leaving
  // the upright stance — bends the torso. The anchor must track every
  // frame, not just on teleport, or the torso outruns the walker.
  if (g_have_last_player && !teleported && std::isfinite(world_scale) &&
      world_scale > 0.0f) {
    RoomscaleMotion motion{};
    if (InputLatestRoomscaleMotion(motion) && motion.active &&
        motion.space_epoch != 0) {
      const Vec3 root_delta{player.x - g_last_player.x,
                            player.y - g_last_player.y,
                            player.z - g_last_player.z};
      const float speed = VLen(motion.world_velocity);
      if (speed > 0.01f && std::isfinite(speed)) {
        const Vec3 direction = VScale(motion.world_velocity, 1.0f / speed);
        const float along = VDot(root_delta, direction);
        const float follow = std::clamp(along, 0.0f, speed * 0.15f);
        if (follow > 0.0005f) {
          const Vec3 world_follow = VScale(direction, follow);
          const Vec3 local_follow{VDot(world_follow, g_right0) / world_scale,
                                 VDot(world_follow, g_up0) / world_scale,
                                -VDot(world_follow, g_fwd0) / world_scale};
          g_p0 = VAdd(g_p0, QuatRotate(local_follow, g_q0[0], g_q0[1],
                                       g_q0[2], g_q0[3]));
        }
      }
    }
  }
  g_last_player = player;
  g_have_last_player = true;

  const Basis heading = g_view_heading.BodyBasis();
  g_fwd0 = heading.f;
  g_right0 = heading.r;
  g_up0 = heading.u;
  if (VrTraceActive()) {
    VrTraceEvent event{};
    event.event = "camera_heading";
    float native_yaw = 0;
    if (ReadPlayerYaw(native_yaw)) { event.values[0] = native_yaw; event.flags = 1; }
    event.values[1] = turn_yaw_rad;
    event.values[2] = g_fwd0.x;
    event.values[3] = g_fwd0.z;
    VrTraceRecord(event);
  }

  float q[4] = {qx, qy, qz, qw};
  Basis hb = BasisFromXrDelta(g_q0, q, g_right0, g_up0, g_fwd0, g_last_right);
  g_last_right = hb.r;
  Vec3 r = hb.r, u = hb.u, f = hb.f;
  // Head-as-camera contract: the written view IS the tracked head pose in
  // the body frame, nothing else. Looking around moves the head on the
  // body; it never turns the body, the heading, or the world. Body
  // heading changes arrive solely from explicit right-stick deltas,
  // recenter, or physical roomscale turning (which moves the tracking
  // space itself). The torso solver follows the head; the head never
  // follows the torso, and no torso-follow term feeds this view.

  // Track translation directly in the calibrated heading. Filtering the
  // user's head independently of the compositor introduces vestibular lag.
  Vec3 delta{px - g_p0.x, py - g_p0.y, pz - g_p0.z};
  Vec3 anchor{player.x, player.y + eye_height, player.z};
  Vec3 head_pos = VAdd(anchor, HeadTranslationFromReference(
      delta, g_q0, g_right0, g_up0, g_fwd0, world_scale));
  head_pos = VAdd(head_pos, VScale(g_fwd0, forward_offset_m));
  // The head bone always ends at the real camera-facing position just
  // below the eyes (head_rider.h) instead of watching the torso solver
  // carry the neck away. Outrunning roots are clipped inside that
  // helper, and the rider delta never feeds back into camera position.

  CpuStageScope writes_time(CpuStage::CameraWrites);
  auto self = (std::uintptr_t)follow_cam_self;
  float native_pose[2][16]{};
  if(!GuardedReadBytes(self+0x10,native_pose[0],sizeof(native_pose[0])) ||
      !GuardedReadBytes(self+0x2d0,native_pose[1],sizeof(native_pose[1])))return false;
  if(g_follow_pose_owner!=self){g_follow_pose_owner=self;g_follow_pose_mask=0;}
  const float written[16]={r.x,r.y,r.z,0,u.x,u.y,u.z,0,f.x,f.y,f.z,0,
      head_pos.x,head_pos.y,head_pos.z,1};
  for(unsigned i=0;i<2;++i) {
    if(!(g_follow_pose_mask&(1u<<i)) ||
        std::memcmp(native_pose[i],g_follow_written_pose[i],sizeof(written))!=0)
      std::memcpy(g_follow_native_pose[i],native_pose[i],sizeof(written));
    std::memcpy(g_follow_written_pose[i],written,sizeof(written));
    g_follow_pose_mask|=1u<<i;
  }
  if (!WriteMatrix(self + 0x10, r, u, f, head_pos) ||
      !WriteMatrix(self + 0x2d0, r, u, f, head_pos)) {
    g_wrote = false;
    return false;
  }
  // Projection comes from the same tracking packet as the head pose. Only
  // edit the observed follow-camera source, never the 0x18-byte CameraMan.
  if (std::isfinite(vfov_rad) && vfov_rad > 0.5f && vfov_rad < 3.1f &&
      std::isfinite(fov_aspect) && fov_aspect > 0.1f && fov_aspect < 10.0f) {
    float current[2]{};
    if (GuardedReadBytes(self + 0x50, current, sizeof(current)) &&
        std::isfinite(current[0]) && current[0] > 0.1f && current[0] < 3.1f &&
        std::isfinite(current[1]) && current[1] > 0.1f && current[1] < 10.0f) {
      if (g_follow_projection_owner != self ||
          current[0] != g_follow_written_projection[0] ||
          current[1] != g_follow_written_projection[1]) {
        g_follow_original_projection[0] = current[0];
        g_follow_original_projection[1] = current[1];
      }
      const float proj[2] = {vfov_rad, fov_aspect};
      if (GuardedWriteBytes(self + 0x50, proj, sizeof(proj))) {
        g_follow_projection_owner = self;
        g_follow_written_projection[0] = proj[0];
        g_follow_written_projection[1] = proj[1];
      }
    }
  }

  g_tracking_head = head_pos;
  g_head_right = hb.r; g_head_up = hb.u; g_head_fwd = hb.f;
  g_wrote = true;
  if (++g_frames % 300 == 0) {
    // Head angles relative to the body frame, for log diagnosis only.
    float hyaw = std::atan2(VDot(f, g_right0), VDot(f, g_fwd0));
    float hpitch = std::asin(f.y < -1 ? -1 : (f.y > 1 ? 1 : f.y));
    DSVR_LOG("Camera: FP head yaw=%.1fdeg pitch=%.1fdeg fwd=%.2f,%.2f,%.2f "
             "head=%.2f,%.2f,%.2f",
             hyaw * 57.29578f, hpitch * 57.29578f, f.x, f.y, f.z,
             head_pos.x, head_pos.y, head_pos.z);
  }
  return true;
}

bool CameraTrackingFrame(Vec3& head_world, Vec3& right, Vec3& up, Vec3& fwd,
                         float reference_quat[4], Vec3* anchor_world) {
  if (!g_captured || !g_wrote) return false;
  head_world = g_tracking_head;
  if (anchor_world) *anchor_world = g_last_player;
  right = g_right0; up = g_up0; fwd = g_fwd0;
  for (int i = 0; i < 4; ++i) reference_quat[i] = g_q0[i];
  return true;
}

void CameraTraceFollowCam(void* follow_cam_self) {
  GuardedReadScope memory_reads;
  Vec3 right{}, up{}, fwd{}, pos{};
  if (!follow_cam_self || !ReadFollowMatrix(follow_cam_self,right,up,fwd,pos)) {
    g_title_sample.store(0,std::memory_order_release);
    g_gameplay_sample.store(0,std::memory_order_release);
    return;
  }
  const bool title = IsTitleCamera(pos);
  Vec3 player{};
  const bool gameplay=!title && ReadPlayerPos(player);
  g_gameplay_sample.store(gameplay?GetTickCount64()+1:0,std::memory_order_release);
  g_title_sample.store(((GetTickCount64()+1) << 1) | (title ? 1ull : 0ull),
                       std::memory_order_release);
  if (g_traced || title) return;
  if (!ReadPlayerPos(player)) return;
  g_traced = true;
  DSVR_LOG("CameraTrace: native follow=%p pos=%.3f,%.3f,%.3f "
           "fwd=%.3f,%.3f,%.3f handedness=%+.0f",
           follow_cam_self,pos.x,pos.y,pos.z,fwd.x,fwd.y,fwd.z,
           (double)VDot(right,VCross(up,fwd)));
}

bool CameraHeadBasis(Vec3& right, Vec3& up, Vec3& fwd) {
  if (!g_captured || !g_wrote) return false;
  right = g_head_right; up = g_head_up; fwd = g_head_fwd;
  return true;
}

void CameraPublishHeadDirectedLocomotion(std::uint64_t space_epoch) {
  GuardedReadScope memory_reads;
  Vec3 native_forward{};
  if (!space_epoch || !g_captured || !g_wrote ||
      !ReadPlayerRenderFacing(native_forward)) {
    InputInvalidateHeadDirectedLocomotion();
    return;
  }
  if (g_planar_heading_epoch != space_epoch) {
    g_planar_heading_epoch = space_epoch;
    g_have_planar_heading = false;
  }
  const float planar = std::hypot(g_head_fwd.x, g_head_fwd.z);
  if (std::isfinite(planar) && planar > 0.2f) {
    g_last_planar_heading = {g_head_fwd.x / planar, 0.0f, g_head_fwd.z / planar};
    g_have_planar_heading = true;
  }
  if (!g_have_planar_heading) {
    InputInvalidateHeadDirectedLocomotion();
    return;
  }
  InputPublishHeadDirectedLocomotion(g_last_planar_heading, native_forward, space_epoch);
  RoomscaleBasis roomscale{};
  roomscale.tracking_right = HeadTranslationFromReference(
      {1, 0, 0}, g_q0, g_right0, g_up0, g_fwd0, 1.0f);
  roomscale.tracking_up = HeadTranslationFromReference(
      {0, 1, 0}, g_q0, g_right0, g_up0, g_fwd0, 1.0f);
  roomscale.tracking_forward = HeadTranslationFromReference(
      {0, 0, -1}, g_q0, g_right0, g_up0, g_fwd0, 1.0f);
  roomscale.native_forward = native_forward;
  roomscale.space_epoch = space_epoch;
  roomscale.valid = true;
  InputPublishRoomscaleBasis(roomscale);
}

}  // namespace dsvr
