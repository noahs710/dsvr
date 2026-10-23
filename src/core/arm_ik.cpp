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
#include "pose_transfer.h"

namespace dsvr {
namespace {
constexpr std::uintptr_t kPlayerPoseUpdate=0x357a20;
constexpr std::uintptr_t kSourcePoseUpdate=0x320980;
constexpr std::uintptr_t kPlayerPoseVtable=0x13251f0;
constexpr std::uintptr_t kPlayerPoseSlot=0xa8;
constexpr int kBones=80;
using PoseUpdate=void(__fastcall*)(void*);
PoseUpdate g_original=nullptr, g_source_original=nullptr;
thread_local void* g_source_scope=nullptr;
thread_local bool g_source_seen=false;
std::atomic<bool> g_copy_verified{false};
std::uint64_t g_copy_commits=0,g_copy_refusals=0;
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
// A refused retarget is otherwise silent: commits stay 0 with no clue which
// contract broke. Name the failing predicate once per change.
std::atomic<unsigned> g_pose_gate{0},g_rig_stop{0};
constexpr const char* kPoseGateNames[]={
  "none","player vtable","player+0x60 model","model vtable","selected model",
  "override pose set","space != 1","bone count mismatch","model+0x70 data",
  "rig contract","player+0x850 output","native copy contract","assembly root mismatch"};
constexpr const char* kRigStopNames[]={
  "none","flver read/type","counts mismatch","entries read","bone missing",
  "weapon parent != hand","forearm twist parent != upper","upper parent != clavicle",
  "parent chain not rooted"};
void ReportGate() {
  const unsigned gate=g_pose_gate.load(),stop=g_rig_stop.load();
  static unsigned shown_gate=0,shown_stop=0;
  if (gate==shown_gate && stop==shown_stop) return;
  shown_gate=gate; shown_stop=stop;
  if (!gate) { DSVR_LOG("ArmIK: pose gate clear"); return; }
  DSVR_WARN("ArmIK: pose gate %s (rig stop %s); IK stays uncommitted until this clears",
            kPoseGateNames[gate<13?gate:0],kRigStopNames[stop<9?stop:0]);
}
std::atomic<std::uint64_t> g_foreign_visits{0};
struct Targets {
  std::uintptr_t player=0;
  std::uint64_t sequence=0,epoch=0,captured_ms=0;
  Vec3 relative[2]{};
  ArmPoseMatrix orientation[2]{};
  ArmPoseMatrix neutral_controller[2]{},neutral_hand[2]{};
  Vec3 head{},lean{},pole[2]{};
  Vec3 physical_head{}, physical_grip[2]{}, physical_tip[2]{}, blade_direction[2]{};
  long long predicted_time_ns=0;
  Vec3 heading_right{},heading_up{},heading_forward{};
  Vec3 head_right{},head_up{},head_forward{};
  bool head_basis_valid=false;
  UserCalibration calibration;
  float world_scale=1;
  CalibrationMapping mapping;
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
// Head-space elbow pole continuity (owner thread only). The published pole is
// recomputed from the wrist lift each frame; that vector swings ~55 deg as the
// hand crosses shoulder height, and SolveTwoBoneIk chases it: the live-grade
// fixture measured an elbow travel of 0.26 m in one frame. Damping the pole
// direction before the model inverse keeps the bend plane continuous. Reset on
// the same transitions the torso filter uses (sequence/epoch/revision change).
ArmPoleSmoother g_pole_smooth[2];
ArmBendState g_bend_state[2];
std::uint64_t g_pole_seq=~0ull,g_pole_epoch=~0ull,g_pole_revision=~0ull,g_pole_ms[2]{};
void InvalidateHands(bool reset_bench=true) {
  g_copy_verified.store(false,std::memory_order_release);
  { std::lock_guard<std::mutex> lock(g_hands_mutex);g_hands={};g_head_delta={};
    if (reset_bench) g_bench={}; }
}
struct Rig {
  std::uint64_t revision=0;
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
std::uint64_t g_rig_revision=0;
float g_native_arm_reach=0;
// Frozen native stature, measured from the same fresh native palette as the arm
// reach: the upright Head bone above the model origin, and the separation of the
// two upper-arm IK roots (the real shoulder sockets, which no controller capture
// can see). Model-space values; the root scale converts them to world units.
float g_native_head_height=0, g_native_shoulder_width=0;
std::uintptr_t g_geometry_player=0;
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
      !GuardedReadBytes(flver+0x10,counts,sizeof(counts))) { g_rig_stop=1; return false; }
  if (counts[0]!=69 || counts[1]!=kBones) { g_rig_stop=2; return false; }
  if (!GuardedRead(flver+0x18,entries) || !entries) { g_rig_stop=3; return false; }
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
      if (expected_clavicle[side] == name.data()) {
        if (rig.clavicle[side] != -1) return false;
        rig.clavicle[side] = i;
        rig.side[side].clavicle = i;
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
      if (current!=-1) { g_rig_stop=8; return false; }
    }
    for (const auto& b : rig.side) {
      if (b.upper < 0 || b.forearm < 0 || b.hand < 0 || b.upper_twist < 0 ||
          b.weapon < 0 || b.fore_twist < 0 || b.clavicle < 0) { g_rig_stop=4; return false; }
        if (rig.parents[b.weapon] != b.hand) { g_rig_stop=5; return false; }
        if (rig.parents[b.fore_twist] != b.upper) { g_rig_stop=6; return false; }
        if (rig.parents[b.upper] != b.clavicle) { g_rig_stop=7; return false; }
    }
    // Captured native c0000 animation anchors each UpperArm basis with
    // column 2 exactly along the elbow bend direction (|dot| = 1.000, 64/64
    // idle and walk frames), so the arm solve aims that hinge instead of
    // transporting roll freely with a shortest-arc rotation.
    for (auto& b : rig.side) b.hinge_aim = true;
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
  rig.revision=++g_rig_revision;
  g_native_arm_reach=0;g_native_head_height=0;g_native_shoulder_width=0;
  g_geometry_player=0;
  g_rig=rig; g_two_hand.Reset();InvalidateHands();
  // Rebinding may reuse the model address while resources/topology change.
  g_torso_smooth.Reset();
  // Force the pole filter's key check to reset it after a topology rebind.
  g_pole_epoch=~0ull; g_pole_revision=~0ull;
  return true;
}
struct NativeCopyState {
  std::uintptr_t output=0,data=0,map=0,map_data=0,resource_owner=0,resource=0,records=0;
  std::array<unsigned char,0x20> header{};
  PoseCopyLayout layout;
  bool operator==(const NativeCopyState&) const = default;
};
struct PendingNativeCopy {
  bool ready=false;
  std::uintptr_t player=0,model=0,source_data=0;
  NativeCopyState transfer;
  ArmPoseMatrix root{};
  std::array<ArmPoseMatrix,kBones> source{};
} g_pending_copy;

// This reads the contract observed at 357A2E/357A7F. It never invokes the
// copy routine or writes its destination, proportions, roots or resources.
bool ReadNativeCopyState(std::uintptr_t player,NativeCopyState& out) {
  out={};
  if (!GuardedRead(player+0x850,out.output) || !out.output ||
      !GuardedRead(out.output+0x60,out.data) || !out.data ||
      !GuardedRead(out.output+0x5c,out.layout.destination_count) ||
      !out.layout.destination_count || out.layout.destination_count>kBones ||
      !GuardedRead(player+0x858,out.map) || !out.map ||
      !GuardedRead(out.map+8,out.layout.source_count) ||
      !out.layout.source_count || out.layout.source_count>kBones ||
      !GuardedRead(out.map+0x10,out.map_data) || !out.map_data ||
      !GuardedReadBytes(out.map_data,out.layout.destination.data(),
                       out.layout.source_count*sizeof(short)) ||
      !GuardedRead(out.output+0x18,out.resource_owner) || !out.resource_owner)
    return false;
  out.resource=out.resource_owner+0xe0;
  if (out.resource<out.resource_owner ||
      !GuardedReadBytes(out.resource,out.header.data(),out.header.size())) return false;
  out.layout.proportions=(out.header[8]&4)!=0;
  if (out.layout.proportions) {
    if (!GuardedRead(out.resource+0x18,out.records) || !out.records ||
        !GuardedReadBytes(out.records,out.layout.records.data(),
                         out.layout.destination_count*4) ||
        !GuardedReadBytes(player+0x860,out.layout.values.data(),sizeof(out.layout.values)))
      return false;
  }
  if(out.data==g_rig.data)return true;  // source snapshot is validated separately
  std::array<ArmPoseMatrix,kBones> readable{};
  return GuardedReadBytes(out.data,readable.data(),
                         out.layout.destination_count*sizeof(ArmPoseMatrix));
}

void RetargetSourcePose(void* self) {
  const bool local_player_pose=GetCurrentThreadId()==g_owner_thread.load() &&
      reinterpret_cast<std::uintptr_t>(self)==g_targets.player;
  CpuStageScope mod_time(CpuStage::ArmRetarget,local_player_pose);
  if (g_stopping.load() || !g_enabled.load()) return;
  if (GetCurrentThreadId()!=g_owner_thread.load()) { ++g_foreign_visits; return; }
  ++g_pose_visits;
  if (g_reset_torso.exchange(false)) { g_torso_smooth.Reset();
    g_pole_smooth[0].Reset(); g_pole_smooth[1].Reset();
    g_bend_state[0].Reset(); g_bend_state[1].Reset(); }
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
  NativeCopyState transfer;
  unsigned gate=0;
    if (!Type(player,0x13251f0)) gate=1;
    else if (!GuardedRead(player+0x60,model) || !model) gate=2;
    else if (!Type(model,0x12eaf30) || !Type(model+0x60,0x12eacb8)) gate=3;
    else if (!GuardedRead(model+0x128,selected) || selected!=model+0x60) gate=4;
    else if (!GuardedRead(model+0x78,override_pose) || override_pose) gate=5;
    else if (!GuardedRead(model+0x68,space) || space!=1) gate=6;
    else if (!GuardedRead(model+0x6c,count) || count!=kBones) gate=7;
  else if (!GuardedRead(model+0x70,data) || !data) gate=8;
  else if (!ReadRig(model,data)) gate=9;
  else if (!GuardedRead(player+0x850,output) || !output) gate=10;
  // This is the natural source-writer return, BEFORE 140207F10. Read the
  // actual bounded remap/proportion owner; native code will perform the copy.
  // No identity-map assumption, final-body write or native replay is involved.
  else if (!ReadNativeCopyState(player,transfer) || transfer.output!=output ||
           !PoseCopyValid(transfer.layout,g_rig.owned)) gate=11;
  output_data=transfer.data;
  if (gate) { if (g_pose_gate.exchange(gate)!=gate) ReportGate(); g_torso_smooth.Reset(); return; }
  ArmPoseMatrix root{},assembly_root{};
  std::array<ArmPoseMatrix,kBones> before{},corrected{},shoulder_scratch{};
  if (!GuardedReadBytes(model+0x30,&root,sizeof(root)) ||
      !GuardedReadBytes(output+0x20,&assembly_root,sizeof(assembly_root)) ||
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
  float native_reach=0;
  for (const auto& bones:g_rig.side) {
    for (int bone:{bones.upper,bones.forearm,bones.hand})
      if (!arm_pose_detail::ValidMatrix(before[bone])) return;
    const float upper=VLen(arm_pose_detail::Sub(ArmPoseOrigin(before[bones.forearm]),ArmPoseOrigin(before[bones.upper])));
    const float forearm=VLen(arm_pose_detail::Sub(ArmPoseOrigin(before[bones.hand]),ArmPoseOrigin(before[bones.forearm])));
    if (!std::isfinite(upper) || !std::isfinite(forearm) || upper<1e-4f || forearm<1e-4f) return;
    native_reach+=(upper+forearm)*.5f;
  }
  // Qwen handoff: a shared palette pointer is NOT a shared coordinate frame.
  // FUN_140357a20 -> 140321f80/14020bec0 -> 140207f10 separately copies
  // ChrModel+0x30 into ChrAsm+0x20 before mesh/attachment propagation. Editing
  // the palette against one root while the consumer uses another detaches
  // hands/equipment even though the old alias guard passes. Compare floats,
  // not bytes: native 140209820 treats +0 and -0 as equal and may skip copying.
  // Refuse divergence; never "repair" either native root by writing it here.
  // Compare the FULL snapshot: `root` above is now translation-free solely
  // for target inversion, not the coordinate frame held by either native owner.
  // Assembly still contains the preceding native copy here. Do not require
  // its stale root to equal the newly animated source. The outer hook checks
  // complete root coherence AFTER the natural copy, before exposing a commit.
  if (g_pose_gate.exchange(0)) ReportGate();
  // Measure before any mod correction, never from the previously solved arm.
  // Anatomy is rig-bound, not animated. Freeze a valid measurement so native
  // floating-point animation noise cannot become a breathing world-scale gain.
  if (!(g_native_arm_reach>0)) g_native_arm_reach=native_reach;
  // Native stature, same rule as the reach: measured once from a fresh native
  // palette before any mod correction, and only for a grounded upright stance
  // (calibration_fit.h), so crouching, lunging or an airborne frame can never
  // become the avatar's height. Absent bones leave it 0 and the fit falls back
  // to the arm measurement instead of guessing.
  if (!(g_native_head_height>0) && g_rig.head>=0 && g_rig.pelvis>=0 &&
      g_rig.legs[0].foot>=0 && g_rig.legs[1].foot>=0 &&
      arm_pose_detail::ValidMatrix(before[g_rig.head]) &&
      arm_pose_detail::ValidMatrix(before[g_rig.pelvis]) &&
      arm_pose_detail::ValidMatrix(before[g_rig.legs[0].foot]) &&
      arm_pose_detail::ValidMatrix(before[g_rig.legs[1].foot])) {
    const float upright=CalibrationNativeHeadHeight(
        ArmPoseOrigin(before[g_rig.head]),ArmPoseOrigin(before[g_rig.pelvis]),
        ArmPoseOrigin(before[g_rig.legs[0].foot]),
        ArmPoseOrigin(before[g_rig.legs[1].foot]));
    if (upright>0) g_native_head_height=upright;
  }
  if (!(g_native_shoulder_width>0) && g_rig.side[0].upper>=0 &&
      g_rig.side[1].upper>=0 &&
      arm_pose_detail::ValidMatrix(before[g_rig.side[0].upper]) &&
      arm_pose_detail::ValidMatrix(before[g_rig.side[1].upper])) {
    const float spread=VLen(arm_pose_detail::Sub(
        ArmPoseOrigin(before[g_rig.side[1].upper]),
        ArmPoseOrigin(before[g_rig.side[0].upper])));
    if (std::isfinite(spread)&&spread>.1f&&spread<1.2f) g_native_shoulder_width=spread;
  }
  g_geometry_player=player;
  if (g_targets.mapping.fitted && g_targets.mapping.model_revision!=g_rig.revision) {
    g_torso_smooth.Reset();return;
  }
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
    // Exactly the camera's metre conversion. A shoulder-local gain here used
    // to cancel world scale and make virtual hands slide away from controllers.
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
    // Pole continuity before the solve: damp the head-space lift pole so the
    // bend plane cannot chase the per-frame lift recomputation across the
    // shoulder-height swing (raw fixture measured 0.26 m elbow travel in one
    // frame). Head space keeps the filter independent of torso yaw; the model
    // inverse below is then just a rigid rotation of the damped direction.
    if (g_pole_epoch!=g_targets.epoch ||
        g_pole_revision!=g_targets.calibration.revision ||
        g_pole_seq+1<g_targets.sequence) {
      g_pole_smooth[0].Reset(); g_pole_smooth[1].Reset();
      g_bend_state[0].Reset(); g_bend_state[1].Reset();
    }
    g_pole_epoch=g_targets.epoch; g_pole_revision=g_targets.calibration.revision;
    g_pole_seq=g_targets.sequence;
    const float pole_dt=static_cast<float>(static_cast<std::int64_t>(g_targets.captured_ms)-
                                           static_cast<std::int64_t>(g_pole_ms[side]));
    if (pole_dt<0.f || pole_dt>250.f) g_pole_smooth[side].Reset();
    // The bend rate reference is keyed by an exact input fingerprint, so a
    // zero-length step (two frames captured inside one millisecond) replays it
    // bit exactly. Only a real gap in tracking time - resume, teleport, scene
    // unload - discards it, so a steady animation replays identically whatever
    // side of a millisecond boundary the captures fall on.
    if (pole_dt<0.f || pole_dt>250.f) g_bend_state[side].Reset();
    g_pole_ms[side]=g_targets.captured_ms;
    Vec3 pole{};
    if (!ArmPoseInversePoint(root,g_pole_smooth[side].Update(g_targets.pole[side],pole_dt),pole))
      return;
    WeaponGripProfile placement;
    const bool have_profile=have_equipment && WeaponGripGet(equipment.weapon_id[side],cfg.dominant_hand_left,placement);
    if (have_profile &&
        !WeaponGripApplyPrimary(placement.primary,before[bones.hand],before[bones.weapon],
                                g_targets.world_scale,desired,grip_target)) return;
    if(side==primary) {
      Vec3 offhand_grip{};
      if(have_profile && map_grip(offhand,offhand_grip))
        support_attached=WeaponGripSolveTwoHand(placement,before[bones.hand],before[bones.weapon],
            g_targets.world_scale,primary_anchor,offhand_grip,g_targets.squeeze[offhand],
            g_targets.epoch,g_two_hand,desired,grip_target,support_grip);
      else g_two_hand.Reset();
      if(support_attached) support_rotation=placement.support.rotation_deg;
    } else if(support_attached) {
      grip_target=support_grip;
      if(!WeaponGripApplySupportRotation(support_rotation,
          corrected[g_rig.side[primary].weapon],desired)) return;
    }
    if (!ArmPoseWristForGrip(grip_target,desired,before[bones.hand],before[bones.weapon],wrist_target)) return;
    const auto arm_result = SolveModelArmPoseWithClavicle(
        corrected,g_rig.parents,bones,wrist_target,shoulder_scratch,&pole,nullptr,
        kArmBendMaxStepDeg,&g_bend_state[side]);
    if (!arm_result.valid ||
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
  ArmPoseMatrix current_root{},current_assembly_root{};
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
  NativeCopyState current_transfer;
  if (!ReadNativeCopyState(player,current_transfer) || current_transfer!=transfer ||
      !GuardedRead(player+0x850,current_output) || current_output!=output ||
      !GuardedRead(current_output+0x60,current_output_data) || current_output_data!=output_data ||
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
      !GuardedReadBytes(output+0x20,&current_assembly_root,sizeof(current_assembly_root)) ||
      std::memcmp(&current_assembly_root,&assembly_root,sizeof(assembly_root))!=0 ||
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
    g_pending_copy.ready=true;g_pending_copy.player=player;
    g_pending_copy.model=model;g_pending_copy.source_data=data;
    g_pending_copy.root=root_before;g_pending_copy.transfer=transfer;
    g_pending_copy.source=before;
    for(int bone=0;bone<kBones;++bone)
      if(written[bone])g_pending_copy.source[bone]=corrected[bone];
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
              g_bench.world_scale!=g_targets.world_scale ||
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
          g_bench.world_scale=g_targets.world_scale;
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
    // Publication is provisional until the outer hook verifies native copy.
    // Every public hand/head/bench getter checks g_copy_verified.
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

bool VerifyNaturalCopy() {
  GuardedReadScope reads;
  const auto& pending=g_pending_copy;
  NativeCopyState current;
  std::uintptr_t model=0,data=0,selected=0;
  unsigned space=0;
  ArmPoseMatrix model_root{},body_root{};
  std::array<ArmPoseMatrix,kBones> source{},body{};
  if (!pending.ready || LocalPlayer()!=pending.player ||
      !ReadNativeCopyState(pending.player,current) || current!=pending.transfer ||
      !GuardedRead(pending.player+0x60,model) || model!=pending.model ||
      !GuardedRead(model+0x70,data) || data!=pending.source_data ||
      !GuardedRead(model+0x128,selected) || selected!=model+0x60 ||
      !GuardedRead(current.output+0x58,space) || space!=1 ||
      !GuardedReadBytes(model+0x30,&model_root,sizeof model_root) ||
      std::memcmp(&model_root,&pending.root,sizeof model_root)!=0 ||
      !GuardedReadBytes(current.output+0x20,&body_root,sizeof body_root) ||
      !GuardedReadBytes(data,source.data(),sizeof source) ||
      std::memcmp(source.data(),pending.source.data(),sizeof source)!=0 ||
      !GuardedReadBytes(current.data,body.data(),current.layout.destination_count*sizeof(ArmPoseMatrix)))
    return false;
  for(unsigned i=0;i<12;++i)
    if(!std::isfinite(body_root.m[i]) || body_root.m[i]!=model_root.m[i])return false;
  return PoseCopyMatches(current.layout,source,body,g_rig.owned);
}

void __fastcall SourcePoseUpdateDetour(void* self) {
  // Shared native animation writer: foreign actors/callers always run normally.
  // Only the natural, owner-thread local PlayerIns outer update opens a scope.
  g_source_original(self);
  if (self==g_source_scope && !g_source_seen) {
    g_source_seen=true;
    RetargetSourcePose(self);
  } else if (!g_source_scope && GetCurrentThreadId()==g_owner_thread.load() &&
             reinterpret_cast<std::uintptr_t>(self)==g_targets.player) {
    InvalidateHands();  // another native source update invalidates the last copy receipt
  }
}

void __fastcall PoseUpdateDetour(void* self) {
  const bool local=GetCurrentThreadId()==g_owner_thread.load() &&
      reinterpret_cast<std::uintptr_t>(self)==g_targets.player;
  const auto previous_scope=g_source_scope;
  const bool previous_seen=g_source_seen;
  const bool own=local && !previous_scope && g_enabled.load() && !g_stopping.load();
  g_source_scope=own?self:nullptr;g_source_seen=false;
  if(own) {
    g_pending_copy.ready=false;
    g_copy_verified.store(false,std::memory_order_release);
  }
  {
    CpuStageScope native_time(CpuStage::NativePose,local);
    // Exactly one natural animation + remap/proportion copy. The inner hook
    // solves the freshly animated source before this caller copies it.
    g_original(self);
  }
  g_source_scope=previous_scope;g_source_seen=previous_seen;
  if(!own)return;
  if(g_pending_copy.ready && !g_stopping.load() && VerifyNaturalCopy()) {
    ++g_copy_commits;++g_pose_commits;g_last_commit=g_targets.sequence;
    g_copy_verified.store(true,std::memory_order_release);
    if(g_copy_commits==1 || g_copy_commits%300==0)
      DSVR_LOG("ArmIK: native body copy verified=%llu refused=%llu map=%u body=%u distinct=%d seq=%llu",
          (unsigned long long)g_copy_commits,(unsigned long long)g_copy_refusals,
          g_pending_copy.transfer.layout.source_count,g_pending_copy.transfer.layout.destination_count,
          (int)(g_pending_copy.source_data!=g_pending_copy.transfer.data),
          (unsigned long long)g_targets.sequence);
  } else {
    if(g_pending_copy.ready) {
      ++g_copy_refusals;
      if(g_copy_refusals==1 || g_copy_refusals%300==0)
        DSVR_WARN("ArmIK: natural body copy verification refused; no committed hand/head/bench publication");
    }
    InvalidateHands();g_reset_torso.store(true);
  }
  g_pending_copy.ready=false;
}
}  // namespace
void ArmIkResetCalibration() {
  g_reset_grip.store(true);
  g_reset_torso.store(true);
  InvalidateHands();
}
bool ArmIkNativeReach(float& native_reach, std::uint64_t& model_revision) {
  native_reach=0;model_revision=0;
  if (g_stopping.load() || GetCurrentThreadId()!=g_owner_thread.load() ||
      !(g_native_arm_reach>0)) return false;
  GuardedReadScope reads;
  if (LocalPlayer()!=g_geometry_player) return false;
  std::uintptr_t model=0,flver=0,entries=0,data=0;
  std::array<unsigned char,kBones*0x30> records{};
  ArmPoseMatrix root{};
  if (!GuardedRead(g_geometry_player+0x60,model) || model!=g_rig.model ||
      !GuardedRead(model+0x18,flver) || flver!=g_rig.flver ||
      !GuardedRead(model+0x70,data) || data!=g_rig.data ||
      !GuardedRead(flver+0x18,entries) || entries!=g_rig.entries ||
      !GuardedReadBytes(entries,records.data(),records.size()) || records!=g_rig.records ||
      !GuardedReadBytes(model+0x30,&root,sizeof(root)) ||
      !arm_pose_detail::ValidMatrix(root)) return false;
  const float scale=VLen(arm_pose_detail::Column(root,0));
  for (int axis=1;axis<3;++axis)
    if (std::fabs(VLen(arm_pose_detail::Column(root,axis))-scale)>1e-4f) return false;
  native_reach=g_native_arm_reach*scale;model_revision=g_rig.revision;
  return true;
}
// Native upright head height and shoulder-socket spread in world units, from the
// same guarded chain validation and root scale as the arm reach. Read-only fit
// input: it never writes a mesh scale, a bone base or a collider.
bool ArmIkNativeStature(float& head_above_origin, float& shoulder_width,
                        std::uint64_t& model_revision) {
  head_above_origin=0;shoulder_width=0;model_revision=0;
  if (!(g_native_head_height>0)||!(g_native_arm_reach>0)) return false;
  float native_reach=0;
  if (!ArmIkNativeReach(native_reach,model_revision)||!(native_reach>0)) return false;
  const float scale=native_reach/g_native_arm_reach;  // the validated root scale
  if (!std::isfinite(scale)||scale<=0.f) return false;
  head_above_origin=g_native_head_height*scale;
  shoulder_width=g_native_shoulder_width>0?g_native_shoulder_width*scale:0.f;
  return true;
}
bool ArmIkHeadWorldDelta(Vec3& delta_world, std::uint64_t& sequence,
                         std::uint64_t& epoch) {
  delta_world={};sequence=0;epoch=0;
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now=GetTickCount64();
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      !g_head_delta.sequence || g_head_delta.sequence>frame.sequence ||
      g_head_delta.epoch!=frame.space_epoch ||
      now<g_head_delta.captured_ms || now-g_head_delta.captured_ms>100) return false;
  delta_world=g_head_delta.world;sequence=g_head_delta.sequence;
  epoch=g_head_delta.epoch;return true;
}
bool ArmIkLatestHands(ArmIkHands& out) {
  out={};
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now=GetTickCount64();
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      !g_hands.sequence || g_hands.sequence>frame.sequence ||
      g_hands.sequence!=XrAppliedCameraSequence() || g_hands.epoch!=frame.space_epoch ||
      now<g_hands.captured_ms || now-g_hands.captured_ms>100) return false;
  out=g_hands;return true;
}
bool ArmIkCombatBench(CombatBenchSnapshot& out) {
  out = {};
  const auto cfg=DiagnosticsCurrentConfig();
  out.bench_on=cfg.combat_test_bench;
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      !out.bench_on || !cfg.motion_attacks || cfg.observer_only ||
      !cfg.vr_enabled || !cfg.arm_ik_enabled || cfg.arm_ik_log_only ||
      XrMenuPresentationActive() || CalibrationCapturesInput()) return false;
  TrackingFrame frame{};
  if (!XrLatestTrackingFrame(frame)) return false;
  std::lock_guard<std::mutex> lock(g_hands_mutex);
  const auto now = GetTickCount64();
  if (!g_copy_verified.load(std::memory_order_acquire) ||
      !frame.focused || !frame.orientation_valid || !frame.position_valid ||
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
  const auto source=reinterpret_cast<void*>(module+kSourcePoseUpdate);
  if (!g_original) {
    constexpr unsigned char outer_bytes[]={0x40,0x56,0x48,0x83,0xec,0x60,0x48,0x8b,0xf1,0xe8,0x52,0x8f,0xfc,0xff};
    constexpr unsigned char source_bytes[]={0x4c,0x8b,0x41,0x60,0x41,0xb1,0x01,0x41,0x83,0x88,0x84,0x00,0x00,0x00,0x01};
    if (!BytesMatch(module+kPlayerPoseUpdate,outer_bytes,sizeof outer_bytes) ||
        !BytesMatch(module+kSourcePoseUpdate,source_bytes,sizeof source_bytes)) return false;
    const auto init=MH_Initialize();
    if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(source,reinterpret_cast<void*>(&SourcePoseUpdateDetour),
                      reinterpret_cast<void**>(&g_source_original))!=MH_OK) return false;
    if (MH_CreateHook(target,reinterpret_cast<void*>(&PoseUpdateDetour),
                      reinterpret_cast<void**>(&g_original))!=MH_OK) {
      MH_RemoveHook(source);g_source_original=nullptr;return false;
    }
  }
  const auto source_status=MH_EnableHook(source);
  if (source_status!=MH_OK && source_status!=MH_ERROR_ENABLED) return false;
  const auto outer_status=MH_EnableHook(target);
  if (outer_status!=MH_OK && outer_status!=MH_ERROR_ENABLED) {
    MH_DisableHook(source);return false;
  }
  g_enabled.store(true);
  DSVR_LOG("ArmIK: scoped source-before-native-copy hooks installed; body publication requires verified native remap/proportions and roots");
  return true;
}
void ArmIkHookUninstall() {
  std::lock_guard<std::mutex> hook_lock(HookInstallMutex());
  g_stopping.store(true);
  std::lock_guard<std::mutex> install_lock(g_install_mutex);
  g_enabled.store(false);
  InvalidateHands();
  if (g_original) MH_DisableHook(reinterpret_cast<void*>(ModuleBase(nullptr)+kPlayerPoseUpdate));
  if (g_source_original) MH_DisableHook(reinterpret_cast<void*>(ModuleBase(nullptr)+kSourcePoseUpdate));
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
  CalibrationMapping mapping;
  if (!CameraTrackingFrame(head,right,up,forward,reference,&anchor,&mapping)) {
    g_reset_torso.store(true); return;
  }
  Vec3 head_right{},head_up{},head_forward{};
  const bool have_head_basis=CameraHeadBasis(head_right,head_up,head_forward);
  if (!have_head_basis) g_reset_torso.store(true);
  const auto player=LocalPlayer();
  if (!player) { g_reset_torso.store(true); return; }
  Targets targets{}; targets.player=player; targets.sequence=frame.sequence;
  targets.epoch=frame.space_epoch; targets.captured_ms=frame.captured_ms;
  targets.calibration=CalibrationCurrent();targets.mapping=mapping;
  if (!mapping.valid || mapping.calibration_revision!=targets.calibration.revision ||
      mapping.world_trim!=cfg.world_scale) {g_reset_torso.store(true);return;}
  targets.world_scale=mapping.world_scale;
  targets.heading_right=right;targets.heading_up=up;targets.heading_forward=forward;
  targets.head_right=head_right;targets.head_up=head_up;targets.head_forward=head_forward;
  targets.head_basis_valid=have_head_basis;
  const Vec3 relative{head.x-anchor.x,head.y-anchor.y,head.z-anchor.z};
  targets.head=relative;
  targets.physical_head={frame.head_pos[0],frame.head_pos[1],frame.head_pos[2]};
  targets.predicted_time_ns=frame.predicted_time_ns;
  const float eye_height=mapping.eye_height;
  // Locomotion-anchored lean: the anchor is the locomotion position, so
  // stick-walking moves anchor and head together and the difference (the
  // lean the torso bends toward) stays near zero. The torso can never
  // outrun the user while walking; it only bends when the head really
  // leaves the upright stance (a physical lean). Damped below; the raw
  // value here must already be locomotion-free.
  targets.lean=arm_pose_detail::Sub(relative,VAdd(VScale(up,eye_height),VScale(forward,cfg.forward_offset_m*targets.world_scale)));
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
    targets.physical_grip[side]={hand.pos[0],hand.pos[1],hand.pos[2]};
    const Vec3 blade=BladeDirection(hand.quat[0]*inv,hand.quat[1]*inv,
                                    hand.quat[2]*inv,hand.quat[3]*inv);
    targets.physical_tip[side]=VAdd(targets.physical_grip[side],VScale(blade,.9f));
    targets.blade_direction[side]=HeadTranslationFromReference(
        blade,reference,right,up,forward,1);
    const Vec3 delta{hand.pos[0]-frame.head_pos[0],hand.pos[1]-frame.head_pos[1],
                     hand.pos[2]-frame.head_pos[2]};
    const auto offset=VAdd(relative,HeadTranslationFromReference(
        delta,reference,right,up,forward,targets.world_scale));
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
