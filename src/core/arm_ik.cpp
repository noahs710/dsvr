#include "arm_ik.h"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string_view>
#include "../common/logging.h"
#include "../common/memory.h"
#include "arm_pose_math.h"
#include "body_pose_math.h"
#include "camera.h"
#include "calibration.h"
#include "calibration_math.h"
#include "combat_native.h"
#include "weapon_grip.h"
#include "diagnostics.h"
#include "hook_install.h"
#include "tracking_frame.h"
#include "xr.h"
#include "cpu_frame_timer.h"
#include "head_rider.h"
#include "combat_contact.h"
#include "hand_math.h"

namespace dsvr {
namespace {
constexpr std::uintptr_t kPlayerPoseUpdate=0x357a20;
constexpr std::uintptr_t kPlayerPoseVtable=0x13251f0;
constexpr std::uintptr_t kPlayerPoseSlot=0xa8;
constexpr int kBones=80;
using PoseUpdate=void(__fastcall*)(void*);
PoseUpdate g_original=nullptr;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stopping{false};
std::atomic<bool> g_reset_grip{true};
// External callers publish requests; only the native owner mutates smoothing.
std::atomic<bool> g_reset_torso{false};
std::mutex g_install_mutex;
std::mutex g_hands_mutex;
ArmIkHands g_hands;
struct HeadDelta {
  Vec3 world{};
  std::uint64_t sequence=0,epoch=0,captured_ms=0;
} g_head_delta;
// Combat proving-ground state (owner thread writes under g_hands_mutex at
// commit; panel reads). Blade segments come from the committed native
// pose; classification is geometry only, never native damage.
struct BenchState {
  Vec3 grip[2]{};
  Vec3 tip[2]{};
  Vec3 prev_tip[2]{};
  float tip_speed[2]{};
  bool blade_valid[2]{};
  int last_event = 0;
  int last_target = -1;
  float last_scale = 0.0f;
  bool hands_too_close = false;
  float hand_separation_m = 0.0f;
  std::uint64_t sequence = 0, epoch = 0, captured_ms = 0;
  BladeKinematics motion[2];
  std::uintptr_t player=0, model=0;
  int weapon_id[2]{-1,-1}, stance=0;
  bool equipment_bound=false, dominant_left=false;
  float world_scale=0;
  std::uint64_t calibration_revision=0;
  long long predicted_time_ns=0;
  RehitGate gate;
  long long shield_raised_ms = 0;
  bool shield_was_raised = false;
} g_bench;
WeaponTwoHandState g_two_hand;
std::atomic<DWORD> g_owner_thread{0};
bool g_attempted=false;
std::uint64_t g_pose_visits=0,g_pose_commits=0,g_sequence_refusals=0,g_last_commit=0,g_last_report=0;
std::atomic<std::uint64_t> g_foreign_visits{0};
struct Targets {
  std::uintptr_t player=0;
  std::uint64_t sequence=0,epoch=0,captured_ms=0;
  Vec3 relative[2]{};
  ArmPoseMatrix orientation[2]{};
  ArmPoseMatrix neutral_controller[2]{},neutral_hand[2]{};
  Vec3 head{},lean{},shoulder_offset[2]{},pole[2]{};
  Vec3 physical_head{}, physical_grip[2]{}, physical_tip[2]{}, blade_direction[2]{};
  long long predicted_time_ns=0;
  Vec3 heading_right{},heading_up{},heading_forward{};
  Vec3 head_right{},head_up{},head_forward{};
  bool head_basis_valid=false;
  UserCalibration calibration;
  float world_scale=1;
  bool valid[2]{};
  bool squeeze[2]{};
} g_targets;
// Damped torso state (owner thread only). Targets are recomputed from
// native poses every frame; this only smooths the approach and can never
// accumulate an offset. Reset on rig rebind, root jumps, calibration
// resets and while menu/calibration input capture owns the controllers.
struct TorsoSmooth {
  DampedAngle desired_yaw;
  DampedAngle pitch;
  DampedVec3 lean;
  std::uintptr_t model=0;
  std::uint64_t epoch=0,calibration_revision=0;
  Vec3 root_origin{};
  bool root_valid=false;
  BodyMotionEstimator root_motion;
  void Reset() { *this=TorsoSmooth{}; }
} g_torso_smooth;
void InvalidateHands(bool reset_bench=true) {
  { std::lock_guard<std::mutex> lock(g_hands_mutex);g_hands={};g_head_delta={};
    if (reset_bench) g_bench={}; }
}
struct Rig {
  std::uintptr_t model=0,flver=0,entries=0,data=0;
  std::array<unsigned char,kBones*0x30> records{};
  std::array<int,kBones> parents{};
  std::array<bool,kBones> owned{};
  ArmPoseBones side[2];
  BodyLegBones legs[2];
  BodyLegCalibration leg_calibration[2];
  std::array<int,2> clavicle{-1,-1};
  int head=-1,neck=-1,spine=-1,chest=-1,pelvis=-1;
  int upper_root=-1,torso_root=-1;
} g_rig;
std::uintptr_t LocalPlayer() {
  std::uintptr_t world=0,player=0;
  return GuardedRead(ModuleBase(nullptr)+0x1c77e50,world) && world &&
      GuardedRead(world+0x68,player) ? player : 0;
}
bool Type(std::uintptr_t object,std::uintptr_t rva) {
  std::uintptr_t vt=0;
  return object && GuardedRead(object,vt) && vt==ModuleBase(nullptr)+rva;
}
bool ReadRig(std::uintptr_t model,std::uintptr_t data) {
  std::uintptr_t flver=0,entries=0;
  short counts[2]{};
  if (!GuardedRead(model+0x18,flver) || !Type(flver,0x12eb760) ||
      !GuardedReadBytes(flver+0x10,counts,sizeof(counts)) || counts[0]!=69 ||
      counts[1]!=kBones || !GuardedRead(flver+0x18,entries) || !entries) return false;
  std::array<unsigned char,kBones*0x30> records{};
  if (!GuardedReadBytes(entries,records.data(),records.size())) return false;
  if (g_rig.model==model && g_rig.flver==flver && g_rig.entries==entries &&
      g_rig.data==data && g_rig.records==records) return true;
  Rig rig{}; rig.model=model; rig.flver=flver; rig.entries=entries; rig.data=data;
  rig.records=records;
  const std::wstring_view expected[2][6]={{L"L_UpperArm",L"L_Forearm",L"L_Hand",L"LUpArmTwist",L"L_Weapon",L"L_ForeTwist"},
      {L"R_UpperArm",L"R_Forearm",L"R_Hand",L"RUpArmTwist",L"R_Weapon",L"R_ForeTwist"}};
    const std::wstring_view expected_legs[2][4]={{L"L_Thigh",L"L_Calf",L"L_Foot",L"LCalfTwist"},
      {L"R_Thigh",L"R_Calf",L"R_Foot",L"RCalfTwist"}};
    const std::wstring_view expected_clavicle[2]={L"L_Clavicle",L"R_Clavicle"};
  for (int i=0;i<kBones;++i) {
    std::uintptr_t text=0; short parent=0;
    std::array<wchar_t,48> name{};
    std::memcpy(&text,records.data()+i*0x30+0x20,sizeof(text));
    std::memcpy(&parent,records.data()+i*0x30+0x28,sizeof(parent));
    if (!text || parent < -1 || parent>=kBones) return false;
    unsigned n=0;
    for (;n<name.size();++n) {
      if (!GuardedRead(text+n*sizeof(wchar_t),name[n])) return false;
      if (!name[n]) break;
    }
    if (n==name.size()) return false;
    rig.parents[i]=parent;
    if (std::wstring_view(name.data())==L"Head") {
      if (rig.head!=-1) return false;
      rig.head=i;
    }
    if (std::wstring_view(name.data())==L"Pelvis") {
      if (rig.pelvis!=-1) return false;
      rig.pelvis=i;
    }
    if (std::wstring_view(name.data())==L"Neck") {
      if (rig.neck!=-1) return false;
      rig.neck=i;
    }
    if (std::wstring_view(name.data())==L"Upper_Root") {
      if (rig.upper_root!=-1) return false;
      rig.upper_root=i;
    }
    for (unsigned side=0;side<2;++side) {
      if (expected_clavicle[side]==name.data()) {
        if (rig.clavicle[side]!=-1) return false;
        rig.clavicle[side]=i;
      }
      int* indices[]={&rig.side[side].upper,&rig.side[side].forearm,
                     &rig.side[side].hand,&rig.side[side].upper_twist,
                     &rig.side[side].weapon,&rig.side[side].fore_twist};
      for (unsigned part=0;part<6;++part) if (expected[side][part]==name.data()) {
        if (*indices[part]!=-1) return false;
        *indices[part]=i;
      }
        int* leg_indices[]={&rig.legs[side].thigh,&rig.legs[side].calf,
                            &rig.legs[side].foot,&rig.legs[side].calf_twist};
      for (unsigned part=0;part<4;++part) if (expected_legs[side][part]==name.data()) {
        if (*leg_indices[part]!=-1) return false;
        *leg_indices[part]=i;
      }
    }
  }
  for (int bone=0;bone<kBones;++bone) {
    int current=bone;int steps=0;
      while (current!=-1 && steps++<kBones) current=rig.parents[current];
      if (current!=-1) return false;
    }
    for (const auto& b:rig.side)
      if (b.upper<0 || b.forearm<0 || b.hand<0 || b.upper_twist<0 ||
        b.weapon<0 || rig.parents[b.weapon]!=b.hand ||
        b.fore_twist<0 || rig.parents[b.fore_twist]!=b.upper) return false;
    for (unsigned side=0;side<2;++side) {
      const auto& b=rig.legs[side];
      if (rig.clavicle[side]<0 || rig.pelvis<0 || b.thigh<0 || b.calf<0 ||
        b.foot<0 || b.calf_twist<0 || rig.parents[b.thigh]!=rig.pelvis ||
        rig.parents[b.calf]!=b.thigh || rig.parents[b.foot]!=b.calf ||
        rig.parents[b.calf_twist]!=b.thigh) return false;
    }
  if (rig.head>=0 && rig.neck>=0 && rig.parents[rig.head]==rig.neck) {
    const int chest_bone=rig.parents[rig.neck];
    const int spine_bone=chest_bone>=0?rig.parents[chest_bone]:-1;
    if (chest_bone>=0 && spine_bone>=0 && spine_bone!=chest_bone &&
        spine_bone!=rig.neck && spine_bone!=rig.head) {
      rig.chest=chest_bone; rig.spine=spine_bone;
      const int torso_root_bone=rig.parents[spine_bone];
      if (torso_root_bone>=0 && torso_root_bone!=spine_bone &&
          torso_root_bone!=chest_bone && torso_root_bone!=rig.neck &&
          torso_root_bone!=rig.head) {
          if (rig.upper_root<0 || rig.upper_root!=torso_root_bone) return false;
        rig.torso_root=torso_root_bone;
        }
    }
  }
  if (rig.chest>=0) for (unsigned side=0;side<2;++side)
    if (rig.parents[rig.clavicle[side]]!=rig.chest) return false;
  for (int bone:{rig.pelvis,rig.spine,rig.chest,rig.neck})
    if (bone>=0) rig.owned[bone]=true;
  const auto own_subtree=[&](int ancestor) {
    if (ancestor<0) return;
    for (int bone=0;bone<kBones;++bone)
      if (arm_pose_detail::InSubtree(bone,ancestor,rig.parents)) rig.owned[bone]=true;
  };
  for (unsigned side=0;side<2;++side) {
    own_subtree(rig.clavicle[side]);
    own_subtree(rig.side[side].upper);
    own_subtree(rig.side[side].upper_twist);
    own_subtree(rig.legs[side].thigh);
  }
  own_subtree(rig.head);
  g_rig=rig; g_two_hand.Reset();InvalidateHands();
  // Rebinding may reuse the model address while resources/topology change.
  g_torso_smooth.Reset(); return true;
}
void __fastcall PoseUpdateDetour(void* self) {
  const bool local_player_pose=GetCurrentThreadId()==g_owner_thread.load() &&
      reinterpret_cast<std::uintptr_t>(self)==g_targets.player;
  CpuStageScope native_time(CpuStage::NativePose,local_player_pose);
  g_original(self);  // Complete the native PlayerIns update and bone writer first.
  native_time.Stop();
  CpuStageScope mod_time(CpuStage::ArmRetarget,local_player_pose);
  if (g_stopping.load() || !g_enabled.load()) return;
  if (GetCurrentThreadId()!=g_owner_thread.load()) { ++g_foreign_visits; return; }
  ++g_pose_visits;
  if (g_reset_torso.exchange(false)) g_torso_smooth.Reset();
  if (!g_targets.player) { g_torso_smooth.Reset(); return; }
  if (reinterpret_cast<std::uintptr_t>(self)!=g_targets.player) return;
  // Native animation has returned. No native callbacks occur inside this
  // owner-thread snapshot/solve/commit transaction; data is still reread at commit.
  GuardedReadScope memory_reads;
  const auto cfg=DiagnosticsCurrentConfig();
  if (!cfg.vr_enabled || !cfg.arm_ik_enabled || !cfg.headlook_enabled || cfg.observer_only ||
      XrMenuPresentationActive() || CalibrationCapturesInput()) { g_two_hand.Reset();g_torso_smooth.Reset();InvalidateHands(); return; }
  CombatEquipment equipment{};
  const auto player=reinterpret_cast<std::uintptr_t>(self);
  CpuStageScope equipment_time(CpuStage::ArmEquipment,local_player_pose);
  const bool have_equipment=cfg.vr_enabled && !cfg.observer_only &&
      CombatNativeReadEquipment(player,equipment);
  equipment_time.Stop();
  if (g_reset_grip.exchange(false) || cfg.arm_ik_log_only) {g_two_hand.Reset();InvalidateHands();}
  CpuStageScope validation_time(CpuStage::ArmValidation,local_player_pose);
  TrackingFrame latest{};
  const auto now=GetTickCount64();
  if (!g_targets.player || now<g_targets.captured_ms || now-g_targets.captured_ms>100 ||
      !XrLatestTrackingFrame(latest) || LocalPlayer()!=g_targets.player ||
      !latest.focused || !latest.orientation_valid || !latest.position_valid ||
      now<latest.captured_ms || now-latest.captured_ms>100) {
    g_torso_smooth.Reset(); return;
  }
  // Newer XR publications do not replace the packet bound to the applied camera.
  if (latest.sequence<g_targets.sequence || latest.space_epoch!=g_targets.epoch ||
      XrAppliedCameraSequence()!=g_targets.sequence ||
      CalibrationCurrent().revision!=g_targets.calibration.revision) {
    g_torso_smooth.Reset(); ++g_sequence_refusals; return;
  }
  Vec3 head{},right{},up{},forward{}; float reference[4]{};
  if (!CameraTrackingFrame(head,right,up,forward,reference)) {
    g_torso_smooth.Reset(); return;
  }
  right=g_targets.heading_right;up=g_targets.heading_up;forward=g_targets.heading_forward;
  std::uintptr_t model=0,selected=0,override_pose=0,data=0;
  std::uintptr_t output=0,output_data=0;
  unsigned space=0,count=0;
  if (!Type(player,0x13251f0) || !GuardedRead(player+0x60,model) ||
      !Type(model,0x12eaf30) || !Type(model+0x60,0x12eacb8) || !GuardedRead(model+0x128,selected) ||
      selected!=model+0x60 || !GuardedRead(model+0x78,override_pose) || override_pose ||
      !GuardedRead(model+0x68,space) || space!=1 || !GuardedRead(model+0x6c,count) ||
      count!=kBones || !GuardedRead(model+0x70,data) || !data || !ReadRig(model,data) ||
      !GuardedRead(player+0x850,output) || !output ||
      !GuardedRead(output+0x60,output_data) || output_data!=data) {
    g_torso_smooth.Reset(); return;
  }
  ArmPoseMatrix root{};
  std::array<ArmPoseMatrix,kBones> before{},corrected{};
  if (!GuardedReadBytes(model+0x30,&root,sizeof(root)) ||
      !GuardedReadBytes(data,before.data(),sizeof(before))) {
    g_torso_smooth.Reset(); return;
  }
  for (float value:root.m) if (!std::isfinite(value)) {
    g_torso_smooth.Reset(); return;
  }
  for (unsigned side=0;side<2;++side)
    if (!g_rig.leg_calibration[side].valid &&
        !CalibrateBodyLeg(before,g_rig.parents,g_rig.legs[side],
                          g_rig.leg_calibration[side])) {
      g_torso_smooth.Reset(); return;
    }
  const auto root_before=root;
  validation_time.Stop();
  CpuStageScope solve_time(CpuStage::ArmSolve,local_player_pose);
  // Targets already exclude the camera's locomotion anchor. Invert only the
  // current root basis: adding/subtracting large world positions loses precision.
  root.m[3]=root.m[7]=root.m[11]=0;
  corrected=before;
  // Torso follows the head before either arm solves, so both shoulders
  // (the IK roots) share the wrist targets' frame. A missing spine chain
  // or a refused correction leaves the native torso untouched.
  float torso_yaw=0,torso_lean_m=0;
  bool torso_turned=false;
  TorsoPoseResult torso_result{};
  BodyMotionEstimate root_motion{};
  if (g_rig.spine>=0) {
    Vec3 head_right{},head_up{},head_forward{};
    if (g_targets.head_basis_valid && CameraHeadBasis(head_right,head_up,head_forward)) {
      head_right=g_targets.head_right;head_up=g_targets.head_up;head_forward=g_targets.head_forward;
      // Reset every lifetime boundary, including reused native models.
      // Locomotion guard: stick-walking moves the root continuously. A
      // >1 m root jump is a teleport/load (reset the torso); smaller
        // per-frame root motion is walking, and the damped lean target is
        // already locomotion-free via the camera anchor, so smoothing
        // continues without snapping. Without this the torso reset every
        // step and re-primed, which read as outrunning/lurching.
        const Vec3 root_origin{root_before.m[3],root_before.m[7],root_before.m[11]};
        if (g_torso_smooth.model!=model ||
          g_torso_smooth.epoch!=g_targets.epoch ||
          g_torso_smooth.calibration_revision!=g_targets.calibration.revision ||
          (g_torso_smooth.root_valid &&
           VLen(arm_pose_detail::Sub(root_origin,g_torso_smooth.root_origin))>1.0f))
        g_torso_smooth.Reset();
      g_torso_smooth.model=model;
      g_torso_smooth.epoch=g_targets.epoch;
      g_torso_smooth.calibration_revision=g_targets.calibration.revision;
      g_torso_smooth.root_origin=root_origin;
      g_torso_smooth.root_valid=true;
      Vec3 root_right{},root_up{},root_forward{};ArmPoseMatrix root_basis{};
      if (!torso_detail::RootFrame(root_before,root_right,root_up,root_forward,root_basis)) {
        g_torso_smooth.Reset(); return;
      }
      // Root motion is measured in fixed world axes; headset heading changes
      // alone must never become a fictitious player-body turn.
      root_motion=g_torso_smooth.root_motion.Update(root_origin,
          RelativeYaw(root_forward,{1,0,0},{0,0,1}),g_targets.captured_ms);
      const float desired=g_torso_smooth.desired_yaw.Approach(
          TorsoTargetYaw(head_forward,right,forward),
          g_targets.captured_ms);
      const float pitch=g_torso_smooth.pitch.Approach(
          TorsoPitchShare(head_forward),g_targets.captured_ms);
      if (g_rig.pelvis>=0 && g_rig.chest>=0 && g_rig.neck>=0 && g_rig.head>=0) {
        const TorsoBones torso_bones{g_rig.pelvis,g_rig.spine,g_rig.chest,
                                     g_rig.neck,g_rig.head,g_rig.torso_root};
        // Lean precedes final yaw/pitch/head alignment. A later torso refusal
        // aborts the entire palette, including this provisional correction.
        if (g_targets.calibration.valid &&
            !SolveTorsoLean(corrected,g_rig.parents,torso_bones,root,
                           g_torso_smooth.lean.Approach(g_targets.lean,
                                                      g_targets.captured_ms),
                           torso_lean_m,g_targets.world_scale)) { g_torso_smooth.Reset(); return; }
        torso_turned=SolveTorsoPose(corrected,g_rig.parents,torso_bones,root,
                                    right,forward,desired,pitch,head_right,
                                    head_up,head_forward,torso_result);
        torso_yaw=torso_result.applied_delta;
      } else {
        torso_turned=SolveTorsoYaw(corrected,g_rig.parents,g_rig.spine,root,
                                    right,forward,head_forward,torso_yaw);
      }
      // Never commit arms over a torso palette whose full solve refused.
      if (!torso_turned) { g_torso_smooth.Reset(); return; }
      // Attach the visible neck to the viewpoint: translate the solved
      // Head bone so its world origin lands at the tracked eye point,
      // bounded 0.35m. The camera itself is never moved by this.
      // Skipped while a calibrated lean is active: the lean already
      // moved the neck toward the head, and an additional rigid shift
      // of the Head origin would stretch the neck segment (the lean
      // hold-stability contract in arm_pose_native:454).
      if (g_rig.neck>=0 && g_rig.head>=0 && torso_lean_m<=1e-5f) {
        const Vec3 eye_world=VAdd(g_targets.head,Vec3{root_before.m[3],
            root_before.m[7],root_before.m[11]});
        const Vec3 head_world=ArmPoseTransformPoint(root_before,
            ArmPoseOrigin(corrected[g_rig.head]));
        float root_rot[9];
        for (int col=0;col<3;++col) {
          root_rot[col*3+0]=root_before.m[col];
          root_rot[col*3+1]=root_before.m[4+col];
          root_rot[col*3+2]=root_before.m[8+col];
        }
        const Vec3 shift=HeadRiderOffsetModel(eye_world,head_world,root_rot);
        if (VLen(shift)>1e-5f) {
          // Model-space origins live at m[3,7,11]. Move Head and its
          // descendants rigidly; Neck and above stay where torso put them.
          for (int bone=0;bone<kBones;++bone) {
              bool desc=false;
              for (int cur=bone;cur>=0;cur=g_rig.parents[cur])
                if (cur==g_rig.head) { desc=true; break; }
            if (desc) {
              corrected[bone].m[3]+=shift.x;
              corrected[bone].m[7]+=shift.y;
              corrected[bone].m[11]+=shift.z;
              }
            }
          }
      }
    } else g_torso_smooth.Reset();
  } else g_torso_smooth.Reset();
  if (root_motion.valid && (VLen(root_motion.displacement)>1e-5f ||
                            std::fabs(root_motion.yaw_delta)>1e-5f)) {
    Vec3 local_motion{};
    if (!ArmPoseInversePoint(root,root_motion.displacement,local_motion)) {
      g_torso_smooth.Reset(); return;
    }
    for (unsigned side=0;side<2;++side) {
      const auto& leg=g_rig.legs[side];
      Vec3 target{};
      if (!BodyFootTarget(ArmPoseOrigin(corrected[leg.foot]),
                          ArmPoseOrigin(corrected[g_rig.pelvis]),local_motion,
                          root_motion.yaw_delta,target) ||
          !SolveBodyLegPose(corrected,g_rig.parents,leg,
                            g_rig.leg_calibration[side],target).valid) {
        g_torso_smooth.Reset(); return;
      }
    }
  }
  static std::uint64_t torso_logs=0;
  if (torso_turned && (torso_yaw!=0 || torso_result.snap_applied!=0) && (torso_logs++%300)==0)
    DSVR_LOG("ArmIK: torso yaw correction %.3f rad toward head (spine=%d pelvis=%.3f snap=%.3f)",
             (double)torso_yaw,g_rig.spine,(double)torso_result.pelvis_applied,
             (double)torso_result.snap_applied);
  ArmIkHands published;
  published.sequence=g_targets.sequence;published.epoch=g_targets.epoch;
  published.captured_ms=g_targets.captured_ms;published.root=root_before;
  bool solved=false;
  const unsigned primary=cfg.dominant_hand_left?0u:1u,offhand=1u-primary;
  bool support_attached=false;
  Vec3 support_rotation{};
  Vec3 support_grip{};
  auto map_grip=[&](unsigned side,Vec3& grip_target)->bool {
    if(!g_targets.valid[side] || !ArmPoseInversePoint(root,g_targets.relative[side],grip_target))return false;
    if(g_targets.calibration.valid) {
      const auto& bones=g_rig.side[side];
      Vec3 head_local{},shoulder_offset{};
      if(!ArmPoseInversePoint(root,g_targets.head,head_local) ||
         
         !ArmPoseInversePoint(root,g_targets.shoulder_offset[side],shoulder_offset))return false;
      const float reach=VLen(arm_pose_detail::Sub(ArmPoseOrigin(corrected[bones.forearm]),ArmPoseOrigin(corrected[bones.upper])))+
          VLen(arm_pose_detail::Sub(ArmPoseOrigin(corrected[bones.hand]),ArmPoseOrigin(corrected[bones.forearm])));
      const float physical=CalibrationReachForScale(g_targets.calibration,(int)side)*g_targets.world_scale;
      if(!(physical>.01f) || !std::isfinite(reach))return false;
      grip_target=VAdd(ArmPoseOrigin(corrected[bones.upper]),
          VScale(arm_pose_detail::Sub(arm_pose_detail::Sub(grip_target,head_local),shoulder_offset),reach/physical));
    }
    return FiniteVec(grip_target);
  };
  for (unsigned order=0;order<2;++order) {
    const unsigned side=order?offhand:primary;
    if (!g_targets.valid[side]) continue;
    Vec3 grip_target{};
    if (!map_grip(side,grip_target)) return;
    const Vec3 primary_anchor=grip_target;
    Vec3 wrist_target=grip_target;
    ArmPoseMatrix controller{},neutral_controller{},neutral_hand{};
    for (int axis=0;axis<3;++axis) {
      Vec3 local{};
      if (!ArmPoseInversePoint(root,arm_pose_detail::Column(g_targets.orientation[side],axis),local))
        return;
      controller.m[axis]=local.x;controller.m[4+axis]=local.y;controller.m[8+axis]=local.z;
      if (!ArmPoseInversePoint(root,arm_pose_detail::Column(g_targets.neutral_controller[side],axis),local))
        return;
      neutral_controller.m[axis]=local.x;neutral_controller.m[4+axis]=local.y;neutral_controller.m[8+axis]=local.z;
      if (!ArmPoseInversePoint(root,arm_pose_detail::Column(g_targets.neutral_hand[side],axis),local))
        return;
      neutral_hand.m[axis]=local.x;neutral_hand.m[4+axis]=local.y;neutral_hand.m[8+axis]=local.z;
    }
    const auto& bones=g_rig.side[side];
    ArmPoseMatrix desired{};
    if (!ArmPoseGripTarget(controller,neutral_controller,neutral_hand,desired)) return;
    // Outward roll correction around the wrist->grip axis (mirrored per
    // hand; grip point itself does not move). Provisional 12 degrees from
    // the live inward-rotation report; grip_roll_correction_deg tunes it.
    {
      Vec3 grip_axis=arm_pose_detail::Sub(ArmPoseOrigin(before[bones.weapon]),ArmPoseOrigin(before[bones.hand]));
      // Express the native grip direction in the desired basis frame.
      Vec3 desired_axis{};
      for (int axis=0;axis<3;++axis) {
        Vec3 from_col=arm_pose_detail::Column(before[bones.hand],axis);
        Vec3 to_col=arm_pose_detail::Column(desired,axis);
        desired_axis=VAdd(desired_axis,VScale(to_col,VDot(VNorm(from_col),grip_axis)));
      }
      ArmPoseMatrix rolled{};
      const float roll=(side? -1.f:1.f)*cfg.grip_roll_correction_deg*0.017453292f;
      if (!ArmPoseRollBasis(desired,desired_axis,roll,rolled)) return;
      desired=rolled;
    }
    Vec3 pole{};
    if (!ArmPoseInversePoint(root,g_targets.pole[side],pole)) return;
    WeaponGripProfile placement;
    const bool have_profile=have_equipment && WeaponGripGet(equipment.weapon_id[side],cfg.dominant_hand_left,placement);
    if (have_profile &&
        !WeaponGripApplyPrimary(placement.primary,before[bones.hand],before[bones.weapon],
                                cfg.world_scale,desired,grip_target)) return;
    if(side==primary) {
      Vec3 offhand_grip{};
      if(have_profile && map_grip(offhand,offhand_grip))
        support_attached=WeaponGripSolveTwoHand(placement,before[bones.hand],before[bones.weapon],
            cfg.world_scale,primary_anchor,offhand_grip,g_targets.squeeze[offhand],
            g_targets.epoch,g_two_hand,desired,grip_target,support_grip);
      else g_two_hand.Reset();
      if(support_attached) support_rotation=placement.support.rotation_deg;
    } else if(support_attached) {
      grip_target=support_grip;
      if(!WeaponGripApplySupportRotation(support_rotation,
          corrected[g_rig.side[primary].weapon],desired)) return;
    }
    if (!ArmPoseWristForGrip(grip_target,desired,before[bones.hand],before[bones.weapon],wrist_target) ||
        !SolveModelArmPose(corrected,g_rig.parents,bones,wrist_target,&pole).valid ||
        !OrientModelHandPose(corrected,g_rig.parents,bones.hand,desired,bones.fore_twist)) return;
    published.side[side]={true,have_equipment?equipment.weapon_id[side]:-1,
                          corrected[bones.hand],corrected[bones.weapon]};
    solved=true;
  }
  if (!solved) g_two_hand.Reset();
  // Visibility belongs to the native mesh filter, independently of whether
  // this pose solves. Skinning bones must remain anatomical and nonsingular.
  // A head-only packet commits its torso correction alone: the palette
  // re-verification below proves only torso/lean bones differ from native.
  if ((!solved && !torso_turned) || cfg.arm_ik_log_only) return;
  solve_time.Stop();
  CpuStageScope commit_time(CpuStage::ArmCommit,local_player_pose);
  std::lock_guard<std::mutex> commit_lock(g_install_mutex);
  if (g_stopping.load() || !g_enabled.load()) return;
  TrackingFrame fresh{};
  const auto commit_now=GetTickCount64();
  if (g_reset_torso.load() || XrMenuPresentationActive() || CalibrationCapturesInput() ||
      !XrLatestTrackingFrame(fresh) || !fresh.focused || !fresh.orientation_valid ||
      !fresh.position_valid || fresh.sequence<g_targets.sequence ||
      fresh.space_epoch!=g_targets.epoch || XrAppliedCameraSequence()!=g_targets.sequence ||
      CalibrationCurrent().revision!=g_targets.calibration.revision ||
      commit_now<g_targets.captured_ms || commit_now-g_targets.captured_ms>100 ||
      commit_now<fresh.captured_ms || commit_now-fresh.captured_ms>100) {
    g_torso_smooth.Reset(); return;
  }
  std::uintptr_t current_model=0,current_data=0,current_selected=0;
  std::uintptr_t current_override=0,current_flver=0,current_entries=0;
  std::uintptr_t current_output=0,current_output_data=0;
  ArmPoseMatrix current_root{};
  unsigned current_space=0,current_count=0;
  std::array<unsigned char,kBones*0x30> current_rig{};
  std::array<ArmPoseMatrix,kBones> current{};
  CombatEquipment current_equipment{};
  if (have_equipment) {
    CpuStageScope equipment_read_time(CpuStage::EquipmentRead,local_player_pose);
    if (!CombatNativeReadEquipment(player,current_equipment) ||
      current_equipment.weapon_id[0]!=equipment.weapon_id[0] ||
      current_equipment.weapon_id[1]!=equipment.weapon_id[1] ||
      current_equipment.native_stance!=equipment.native_stance) return;
  }
  if (!GuardedRead(player+0x850,current_output) || current_output!=output ||
      !GuardedRead(current_output+0x60,current_output_data) || current_output_data!=data ||
      !GuardedRead(player+0x60,current_model) || current_model!=model ||
      !GuardedRead(model+0x70,current_data) || current_data!=data ||
      !GuardedRead(model+0x128,current_selected) || current_selected!=selected ||
      !GuardedRead(model+0x78,current_override) || current_override ||
      !GuardedRead(model+0x18,current_flver) || current_flver!=g_rig.flver ||
      !GuardedRead(current_flver+0x18,current_entries) || current_entries!=g_rig.entries ||
      !GuardedRead(model+0x68,current_space) || current_space!=space ||
      !GuardedRead(model+0x6c,current_count) || current_count!=count ||
      !GuardedReadBytes(model+0x30,&current_root,sizeof(current_root)) ||
      std::memcmp(&current_root,&root_before,sizeof(current_root))!=0 ||
      !GuardedReadBytes(g_rig.entries,current_rig.data(),current_rig.size()) || current_rig!=g_rig.records ||
      !GuardedReadBytes(data,current.data(),sizeof(current)) ||
      std::memcmp(current.data(),before.data(),sizeof(current))!=0) {
    g_torso_smooth.Reset(); return;
  }
  // This is the post-writer owner buffer. Commit only changed matrices; every
  // other animation row remains untouched. Roll back earlier rows if a guarded
  // write fails, and never write the separately validated root/physics poses.
  bool committed=true;
  std::array<bool,kBones> written{};
  for (int bone=0;bone<kBones;++bone) {
    if (!g_rig.owned[bone] ||
        std::memcmp(&before[bone],&corrected[bone],sizeof(ArmPoseMatrix))==0) continue;
    if (!GuardedWriteBytes(data+static_cast<std::uintptr_t>(bone)*sizeof(ArmPoseMatrix),
                           &corrected[bone],sizeof(ArmPoseMatrix))) {
      if (!GuardedWriteBytes(data+static_cast<std::uintptr_t>(bone)*sizeof(ArmPoseMatrix),
                             &before[bone],sizeof(ArmPoseMatrix)))
        DSVR_ERROR("ArmIK: failed to restore palette row %d after guarded write refusal",bone);
      for (int undo=0;undo<bone;++undo) if (written[undo] &&
          !GuardedWriteBytes(data+static_cast<std::uintptr_t>(undo)*sizeof(ArmPoseMatrix),
                             &before[undo],sizeof(ArmPoseMatrix)))
        DSVR_ERROR("ArmIK: failed to roll back palette row %d after guarded write refusal",undo);
      committed=false;break;
    }
    written[bone]=true;
  }
  if (committed) {
    {
      std::lock_guard<std::mutex> lock(g_hands_mutex);
      g_hands = published;
      // Combat proving ground: blade segments from the committed pose,
      // classified against a fixed sparring target. Geometry only —
      // native damage is never written. Off while the bench is off.
      if (cfg.combat_test_bench && cfg.motion_attacks && !cfg.observer_only) {
        if (g_bench.sequence!=g_targets.sequence) {
          // Motion energy comes from the bound physical packet, never native
          // root motion, stick turning, torso smoothing or IK corrections.
          if (g_bench.epoch!=g_targets.epoch || g_bench.player!=player ||
              g_bench.model!=model || g_bench.equipment_bound!=have_equipment ||
              g_bench.weapon_id[0]!=equipment.weapon_id[0] ||
              g_bench.weapon_id[1]!=equipment.weapon_id[1] ||
              g_bench.stance!=equipment.native_stance ||
              g_bench.calibration_revision!=g_targets.calibration.revision ||
              g_bench.world_scale!=cfg.world_scale ||
              g_bench.dominant_left!=cfg.dominant_hand_left ||
              g_targets.sequence<g_bench.sequence ||
              g_targets.predicted_time_ns<=g_bench.predicted_time_ns ||
              g_targets.predicted_time_ns-g_bench.predicted_time_ns>100000000)
            g_bench={};
          const float sample_dt=g_bench.predicted_time_ns>0
              ? float(g_targets.predicted_time_ns-g_bench.predicted_time_ns)*1e-9f
              : .011111111f;
          g_bench.player=player;g_bench.model=model;
          g_bench.equipment_bound=have_equipment;
          g_bench.weapon_id[0]=equipment.weapon_id[0];
          g_bench.weapon_id[1]=equipment.weapon_id[1];
          g_bench.stance=equipment.native_stance;
          g_bench.calibration_revision=g_targets.calibration.revision;
          g_bench.world_scale=cfg.world_scale;
          g_bench.dominant_left=cfg.dominant_hand_left;
          const unsigned weapon_side = cfg.dominant_hand_left ? 0u : 1u;
          const unsigned shield_side = 1u - weapon_side;
          const Vec3 anchor_w{root_before.m[3], root_before.m[7], root_before.m[11]};
          const Vec3 head_w = VAdd(g_targets.head, anchor_w);
          // Sparring target: fixed point ahead of the player at chest height.
          const Vec3 target_c = VAdd(head_w, VScale(g_targets.heading_forward,
                                                    cfg.combat_test_target_m));
          BladeContactTarget spar;
          spar.id = 7;
          spar.centre = target_c;
          spar.radius = 0.25f;
          const std::span<const BladeContactTarget> targets{&spar, 1};
          for (unsigned side = 0; side < 2; ++side) {
            if (!published.side[side].valid) {
              g_bench.blade_valid[side] = false;
              g_bench.motion[side].Reset();
              if (side==shield_side) g_bench.shield_was_raised=false;
              continue;
            }
            const Vec3 grip_w = ArmPoseTransformPoint(
                root_before, ArmPoseOrigin(published.side[side].weapon));
            // Grip-local -Z is transformed into world once in ArmIkTick.
            // Native blade axis and the temporary .9m length remain unqualified.
            const Vec3 dir=VNorm(g_targets.blade_direction[side]);
            if (!FiniteVec(dir) || VLen(dir)<.99f) {
              g_bench.blade_valid[side]=false;
              g_bench.motion[side].Reset();
              continue;
            }
            const Vec3 tip_w=VAdd(grip_w,VScale(dir,.9f));
            auto& motion=g_bench.motion[side];
            motion.FeedRelative(g_targets.physical_grip[side],
                g_targets.physical_tip[side],g_targets.physical_head,
                sample_dt,g_targets.epoch);
            BladeKinematics blade=motion;
            blade.grip=grip_w;blade.tip=tip_w;
            blade.prev_tip=g_bench.blade_valid[side] ? g_bench.tip[side] : tip_w;
            blade.valid=motion.valid && FiniteVec(grip_w) && FiniteVec(tip_w);
            g_bench.grip[side] = grip_w;
            g_bench.tip[side] = tip_w;
            g_bench.prev_tip[side] = blade.prev_tip;
            g_bench.tip_speed[side] = blade.tip_speed;
            g_bench.blade_valid[side] = blade.valid;
            if (side == weapon_side && blade.valid) {
              ContactResult contact;
              const long long now_ms = (long long)commit_now;
              if (ClassifyWeaponContact(blade, motion.FrameKind(VAdd(
                                        g_targets.physical_tip[side],VScale(g_targets.physical_grip[side],-1))), targets,
                                        cfg.contact_speed_min, cfg.swing_damage_gain,
                                        cfg.rehit_window_ms, now_ms, cfg.haptics,
                                        g_bench.gate, contact)) {
                g_bench.last_event = 1;
                g_bench.last_target = contact.hit.target_id;
                g_bench.last_scale = contact.hit.damage_scale;
                DSVR_LOG("CombatBench: HIT target=%d scale=%.2f tip=%.2f m/s",
                         contact.hit.target_id, (double)contact.hit.damage_scale,
                         (double)blade.tip_speed);
              }
            }
          }
          // Both sides must be current before defence, independent of handedness.
          const unsigned side=shield_side;
          if (g_bench.blade_valid[side]) {
            // Shield guard: off-hand held slow in front of the chest.
            const Vec3 off_w = ArmPoseTransformPoint(
                root_before, ArmPoseOrigin(published.side[side].hand));
            const bool raised = IsBlocking(
                off_w, g_bench.motion[side].grip_speed, head_w,
                g_targets.heading_forward);
            if (raised && !g_bench.shield_was_raised)
              g_bench.shield_raised_ms = (long long)commit_now;
            g_bench.shield_was_raised = raised;
            // Incoming threat proxy: the weapon blade crossing the guard.
            const bool overlaps = g_bench.blade_valid[weapon_side] &&
                SegmentPointDistance(g_bench.grip[weapon_side],
                                     g_bench.tip[weapon_side], off_w) < 0.35f;
            const ContactEvent defence = ClassifyDefence(
                raised, g_bench.shield_raised_ms, (long long)commit_now,
                overlaps);
            if (defence != ContactEvent::None) {
              g_bench.last_event =
                  defence == ContactEvent::Parry ? 3 : 2;
              g_bench.last_target = -2;
              g_bench.last_scale = 0.0f;
              DSVR_LOG("CombatBench: %s (guard held, blade reached shield)",
                       defence == ContactEvent::Parry ? "PARRY" : "BLOCK");
            }
          }
          // Report separation only; this diagnostic does not enforce collisions.
          if (published.side[0].valid && published.side[1].valid) {
            const Vec3 a = ArmPoseTransformPoint(
                root_before, ArmPoseOrigin(published.side[0].hand));
            const Vec3 b = ArmPoseTransformPoint(
                root_before, ArmPoseOrigin(published.side[1].hand));
            g_bench.hand_separation_m = VLen(arm_pose_detail::Sub(a, b));
            g_bench.hands_too_close =
                cfg.weapon_separation_min_m > 0.0f &&
                g_bench.hand_separation_m < cfg.weapon_separation_min_m;
            if (g_bench.hands_too_close)
              DSVR_LOG("CombatBench: HANDS TOO CLOSE sep=%.3f m (min %.3f)",
                       (double)g_bench.hand_separation_m,
                       (double)cfg.weapon_separation_min_m);
          } else {
            g_bench.hands_too_close = false;
          }
          g_bench.predicted_time_ns=g_targets.predicted_time_ns;
          g_bench.sequence = g_targets.sequence;
          g_bench.epoch = g_targets.epoch;
          g_bench.captured_ms = g_targets.captured_ms;
        }
      } else { g_bench={}; }
    }
    ++g_pose_commits; g_last_commit=g_targets.sequence;
    // Publish solved-vs-native head displacement for diagnostics only.
    // It must never feed the camera (that feedback loop detached the
    // viewpoint from the body in live testing).
    if (g_rig.head>=0) {
      const Vec3 solved_w=ArmPoseTransformPoint(root_before,ArmPoseOrigin(corrected[g_rig.head]));
      const Vec3 native_w=ArmPoseTransformPoint(root_before,ArmPoseOrigin(before[g_rig.head]));
      HeadDelta delta{};
      delta.world=arm_pose_detail::Sub(solved_w,native_w);
      delta.sequence=g_targets.sequence;delta.epoch=g_targets.epoch;
      delta.captured_ms=g_targets.captured_ms;
      if (FiniteVec(delta.world) && VLen(delta.world)<1.0f) {
        std::lock_guard<std::mutex> lock(g_hands_mutex);g_head_delta=delta;
      }
    }
  }
}
}  // namespace
void ArmIkResetCalibration() {
  g_reset_grip.store(true);
  g_reset_torso.store(true);
  InvalidateHands();
}
bool ArmIkHeadWorldDelta(Vec3& delta_world, std::uint64_t& sequence,
                         std::uint64_t& epoch) {
  delta_world={};sequence=0;epoch=0;
  if (XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now=GetTickCount64();
  if (!g_head_delta.sequence || g_head_delta.sequence>frame.sequence ||
      g_head_delta.epoch!=frame.space_epoch ||
      now<g_head_delta.captured_ms || now-g_head_delta.captured_ms>100) return false;
  delta_world=g_head_delta.world;sequence=g_head_delta.sequence;
  epoch=g_head_delta.epoch;return true;
}
bool ArmIkLatestHands(ArmIkHands& out) {
  out={};
  if (XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now=GetTickCount64();
  if (!g_hands.sequence || g_hands.sequence>frame.sequence ||
      g_hands.sequence!=XrAppliedCameraSequence() || g_hands.epoch!=frame.space_epoch ||
      now<g_hands.captured_ms || now-g_hands.captured_ms>100) return false;
  out=g_hands;return true;
}
bool ArmIkCombatBench(CombatBenchSnapshot& out) {
  out = {};
  const auto cfg=DiagnosticsCurrentConfig();
  out.bench_on=cfg.combat_test_bench;
  if (!out.bench_on || !cfg.motion_attacks || cfg.observer_only ||
      !cfg.vr_enabled || !cfg.arm_ik_enabled || cfg.arm_ik_log_only ||
      XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now = GetTickCount64();
  if (!frame.focused || !frame.orientation_valid || !frame.position_valid ||
      !g_bench.sequence || g_bench.sequence > frame.sequence ||
      g_bench.epoch != frame.space_epoch ||
      now < g_bench.captured_ms || now - g_bench.captured_ms > 250)
    return false;
  out.fresh = true;
  for (int i = 0; i < 2; ++i) {
    out.grip[i] = g_bench.grip[i];
    out.tip[i] = g_bench.tip[i];
    out.tip_speed[i] = g_bench.tip_speed[i];
  }
  out.last_event = g_bench.last_event;
  out.last_target = g_bench.last_target;
  out.last_scale = g_bench.last_scale;
  out.hands_too_close = g_bench.hands_too_close;
  out.hand_separation_m = g_bench.hand_separation_m;
  return true;
}
bool ArmIkHookInstall() {
  std::lock_guard<std::mutex> hook_lock(HookInstallMutex());
  std::lock_guard<std::mutex> install_lock(g_install_mutex);
  if (g_stopping.load()) return false;
  const auto module=ModuleBase(nullptr);
  const auto player=LocalPlayer();
  std::uintptr_t vtable=0,method=0;
  if (!player || !GuardedRead(player,vtable) || vtable!=module+kPlayerPoseVtable ||
      !GuardedRead(vtable+kPlayerPoseSlot,method) || method!=module+kPlayerPoseUpdate)
    return false;
  const auto target=reinterpret_cast<void*>(module+kPlayerPoseUpdate);
  if (g_original) {
    const auto status=MH_EnableHook(target);
    g_enabled.store(status==MH_OK || status==MH_ERROR_ENABLED);
    return g_enabled.load();
  }
  const auto init=MH_Initialize();
  if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
  if (MH_CreateHook(target,reinterpret_cast<void*>(&PoseUpdateDetour),
                    reinterpret_cast<void**>(&g_original))!=MH_OK) return false;
  if (MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target); g_original=nullptr; return false; }
  g_enabled.store(true);
  DSVR_LOG("ArmIK: PlayerIns +0xA8 post-writer hook installed; live headset body quality remains unverified until the guided session");
  return true;
}
void ArmIkHookUninstall() {
  std::lock_guard<std::mutex> hook_lock(HookInstallMutex());
  g_stopping.store(true);
  std::lock_guard<std::mutex> install_lock(g_install_mutex);
  g_enabled.store(false);
  InvalidateHands();
  if (g_original) MH_DisableHook(reinterpret_cast<void*>(ModuleBase(nullptr)+kPlayerPoseUpdate));
  // An already-running detour may still need its trampoline; keep it allocated.
}
void ArmIkTick(const TrackingFrame& frame) {
  if (g_stopping.load()) return;
  const auto thread=GetCurrentThreadId(); DWORD empty=0;
  g_owner_thread.compare_exchange_strong(empty,thread);
  if (g_owner_thread.load()!=thread) return;
  g_targets={};
  InvalidateHands(false);
  struct PublicationGuard {
    bool published=false;
    ~PublicationGuard() { if (!published) InvalidateHands(); }
  } publication;
  const auto cfg=DiagnosticsCurrentConfig(); const auto now=GetTickCount64();
  if (!cfg.vr_enabled || !cfg.arm_ik_enabled || !cfg.headlook_enabled || cfg.observer_only ||
      !frame.focused || !frame.orientation_valid || !frame.position_valid ||
      !frame.sequence || !frame.space_epoch || now<frame.captured_ms || now-frame.captured_ms>100 ||
      !std::isfinite(cfg.world_scale) || cfg.world_scale<=0 || XrMenuPresentationActive() ||
      CalibrationCapturesInput()) { g_reset_torso.store(true); return; }
  Vec3 head{},right{},up{},forward{},anchor{}; float reference[4]{};
  if (!CameraTrackingFrame(head,right,up,forward,reference,&anchor)) {
    g_reset_torso.store(true); return;
  }
  Vec3 head_right{},head_up{},head_forward{};
  const bool have_head_basis=CameraHeadBasis(head_right,head_up,head_forward);
  if (!have_head_basis) g_reset_torso.store(true);
  const auto player=LocalPlayer();
  if (!player) { g_reset_torso.store(true); return; }
  Targets targets{}; targets.player=player; targets.sequence=frame.sequence;
  targets.epoch=frame.space_epoch; targets.captured_ms=frame.captured_ms;
  targets.calibration=CalibrationCurrent();targets.world_scale=cfg.world_scale;
  targets.heading_right=right;targets.heading_up=up;targets.heading_forward=forward;
  targets.head_right=head_right;targets.head_up=head_up;targets.head_forward=head_forward;
  targets.head_basis_valid=have_head_basis;
  const Vec3 relative{head.x-anchor.x,head.y-anchor.y,head.z-anchor.z};
  targets.head=relative;
  targets.physical_head={frame.head_pos[0],frame.head_pos[1],frame.head_pos[2]};
  targets.predicted_time_ns=frame.predicted_time_ns;
  const float eye_height=CalibrationViewHeight(targets.calibration,cfg.eye_height,cfg.play_style=="seated");
  // Locomotion-anchored lean: the anchor is the locomotion position, so
  // stick-walking moves anchor and head together and the difference (the
  // lean the torso bends toward) stays near zero. The torso can never
  // outrun the user while walking; it only bends when the head really
  // leaves the upright stance (a physical lean). Damped below; the raw
  // value here must already be locomotion-free.
  targets.lean=arm_pose_detail::Sub(relative,VAdd(VScale(up,eye_height),VScale(forward,cfg.forward_offset_m)));
  for (unsigned side=0;side<2;++side) {
    const auto& hand=frame.arm[side]; if (!hand.valid) continue;
    targets.squeeze[side]=hand.squeeze_held;
    float norm2=0;
    for (float value:hand.quat) norm2+=value*value;
    if (!std::isfinite(norm2) || norm2<1e-6f) continue;
    const float inv=1/std::sqrt(norm2);
    const Vec3 axes[]={{1,0,0},{0,1,0},{0,0,1}};
    for (int axis=0;axis<3;++axis) {
      const auto xr=QuatRotate(axes[axis],hand.quat[0]*inv,hand.quat[1]*inv,
                               hand.quat[2]*inv,hand.quat[3]*inv);
      const auto world=HeadTranslationFromReference(xr,reference,right,up,forward,1);
      targets.orientation[side].m[axis]=world.x;
      targets.orientation[side].m[4+axis]=world.y;
      targets.orientation[side].m[8+axis]=world.z;
      const auto* neutral=targets.calibration.neutral_controller[side];
      const auto neutral_xr=QuatRotate(axes[axis],neutral[0],neutral[1],neutral[2],neutral[3]);
      const auto neutral_world=HeadTranslationWorld(neutral_xr,right,up,forward,1);
      targets.neutral_controller[side].m[axis]=neutral_world.x;
      targets.neutral_controller[side].m[4+axis]=neutral_world.y;
      targets.neutral_controller[side].m[8+axis]=neutral_world.z;
    }
    // c0000's extracted T-pose: +X wrist->fingers points outward, +Z
    // (palm normal) down. This reference is independent of native animations.
    const float sign=side?1.f:-1.f;
    const Vec3 hand_axes[]={VScale(right,sign),VScale(forward,sign),VScale(up,-1)};
    for(int axis=0;axis<3;++axis) {
      targets.neutral_hand[side].m[axis]=hand_axes[axis].x;
      targets.neutral_hand[side].m[4+axis]=hand_axes[axis].y;
      targets.neutral_hand[side].m[8+axis]=hand_axes[axis].z;
    }
    targets.shoulder_offset[side]=VAdd(VScale(right,sign*targets.calibration.shoulder_half_width_m*cfg.world_scale),
        VScale(up,-targets.calibration.head_to_arm_m*cfg.world_scale));
    targets.physical_grip[side]={hand.pos[0],hand.pos[1],hand.pos[2]};
    const Vec3 blade=BladeDirection(hand.quat[0]*inv,hand.quat[1]*inv,
                                    hand.quat[2]*inv,hand.quat[3]*inv);
    targets.physical_tip[side]=VAdd(targets.physical_grip[side],VScale(blade,.9f));
    targets.blade_direction[side]=HeadTranslationFromReference(
        blade,reference,right,up,forward,1);
    const Vec3 delta{hand.pos[0]-frame.head_pos[0],hand.pos[1]-frame.head_pos[1],
                     hand.pos[2]-frame.head_pos[2]};
    const auto offset=VAdd(relative,HeadTranslationFromReference(
        delta,reference,right,up,forward,cfg.world_scale));
    if (!FiniteVec(offset) || VLen(offset)>10) { g_reset_torso.store(true); return; }
    targets.relative[side]=offset; targets.valid[side]=true;
    const float elevation=delta.y+targets.calibration.head_to_arm_m;
    const float lift=std::clamp(elevation/.5f,0.f,1.f);
    targets.pole[side]=VAdd(VAdd(VScale(right,sign*(.55f+.35f*lift)),VScale(forward,-.4f)),
        VScale(up,-(1.f-.8f*lift)));
  }
  // Head-only packets still drive the torso: controller loss must not
  // freeze the body while the head keeps moving. Without a head basis
  // there is nothing to follow, so the packet still fails closed.
  if (!targets.valid[0] && !targets.valid[1] && !targets.head_basis_valid) { g_reset_torso.store(true); return; }
  g_targets=targets;
  publication.published=true;
  if (now-g_last_report>=1000) {
    g_last_report=now;
    DSVR_LOG("ArmIK: native CPU visits=%llu commits=%llu last_seq=%llu "
             "bound_seq=%llu sequence_refusals=%llu foreign_visits=%llu log_only=%d "
             "calibration_revision=%llu calibrated=%d "
             "(not GPU/attachment qualification)",
             (unsigned long long)g_pose_visits,(unsigned long long)g_pose_commits,
             (unsigned long long)g_last_commit,(unsigned long long)frame.sequence,
             (unsigned long long)g_sequence_refusals,
             (unsigned long long)g_foreign_visits.load(),(int)cfg.arm_ik_log_only,
             (unsigned long long)targets.calibration.revision,(int)targets.calibration.valid);
  }
  if (!g_attempted) {
    g_attempted=true;
    if (!ArmIkHookInstall()) DSVR_WARN("ArmIK: PlayerIns post-writer hook refused; animation unchanged");
  }
}
}  // namespace dsvr
