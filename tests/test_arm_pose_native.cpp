// Real native adapter/hook with bounded memory and recording MinHook/XR seams.
// This verifies CPU ownership/refusal, not live arm rendering or headset comfort.
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "../src/common/memory.h"
#include "../src/common/logging.h"
#include "../src/core/arm_ik.h"
#include "../src/core/arm_pose_math.h"
#include "../src/core/camera.h"
#include "../src/core/calibration.h"
#include "../src/core/combat_native.h"
#include "../src/core/cpu_frame_timer.h"
#include "../src/core/weapon_grip.h"
#include "../src/core/diagnostics.h"
#include "../src/core/tracking_frame.h"
#include "../src/core/xr.h"
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
using namespace dsvr;
constexpr std::uintptr_t base=0x140000000;
std::array<unsigned char,0x80> world{};
std::array<unsigned char,0x900> player{}, other{};
std::array<unsigned char,0x150> model{};
std::array<unsigned char,0x80> pose_output{};
std::array<unsigned char,0x30> flver{};
std::array<unsigned char,80*0x30> entries{}, rebound_entries{};
std::array<std::array<wchar_t,24>,80> names{};
std::array<ArmPoseMatrix,80> poses{}, native{};
Config cfg;
TrackingFrame latest{};
bool signature=true,latest_ok=true,camera_ok=true,menu=false;
std::uintptr_t player_pose_method=0;
bool head_basis_ok=false; Vec3 stub_head_fwd{.04822187f,0,.99883664f};
std::uint64_t camera_sequence=0;
UserCalibration calibration;
bool capture_input=false;
bool equipment_available=false;
unsigned mutate_after_pose_read=0;
unsigned originals=0,writes=0,forbidden=0,creates=0,disables=0;
std::array<unsigned,80> writes_by_bone{};
bool write_this_original=false;
unsigned equipment_reads=0;
unsigned original_scope_leaks=0,scoped_reads=0,unscoped_reads=0;
bool ReadScopeActive() {
  // Probe scope metadata only; no fake address is dereferenced.
  dsvr::GuardedReadScope::Remember(1,2);
  return dsvr::GuardedReadScope::Contains(1,2);
}
HANDLE enable_entered=nullptr,enable_continue=nullptr;
using Update=void(__fastcall*)(void*);
Update callback=nullptr;
Vec3 camera_anchor{10,20,30},camera_lean{};
template<class T> std::uintptr_t Ptr(T& v) { return reinterpret_cast<std::uintptr_t>(v.data()); }
template<class T,class V> void Put(T& v,unsigned offset,V value) { std::memcpy(v.data()+offset,&value,sizeof(value)); }
ArmPoseMatrix Pose(float x,float y,float z) { return {{1,0,0,x,0,1,0,y,0,0,1,z}}; }
void __fastcall Original(void*) {
  ++originals;if(ReadScopeActive())++original_scope_leaks;poses=native;
  writes_by_bone.fill(0);write_this_original=false;
}
void Fresh() { latest.captured_ms=GetTickCount64();camera_sequence=latest.sequence; }
bool Copy(std::uintptr_t address,void* out,std::size_t size) {
  struct Range { std::uintptr_t start; std::size_t size; };
  std::vector<Range> ranges{{Ptr(world),world.size()},{Ptr(player),player.size()},
      {Ptr(other),other.size()},{Ptr(model),model.size()},{Ptr(pose_output),pose_output.size()},
      {Ptr(flver),flver.size()},
      {Ptr(entries),entries.size()},{Ptr(rebound_entries),rebound_entries.size()},
      {Ptr(poses),sizeof(poses)}};
  for (auto& text:names) ranges.push_back({Ptr(text),sizeof(text)});
  for (const auto& r:ranges) if (address>=r.start && address-r.start<=r.size && size<=r.size-(address-r.start)) {
    std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
  }
  return false;
}
void Name(int index,const wchar_t* name,int parent) {
  wcscpy_s(names[index].data(),names[index].size(),name);
  Put(entries,index*0x30+0x28,static_cast<short>(parent));
}
}
MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
  ++creates;
  if (target!=reinterpret_cast<void*>(base+0x357a20)) ++forbidden;
  callback=reinterpret_cast<Update>(detour);*original=reinterpret_cast<void*>(&Original);return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) {
  if (enable_entered) { SetEvent(enable_entered); WaitForSingleObject(enable_continue,INFINITE); }
  return MH_OK;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID) { ++disables;return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
namespace dsvr {
void LogWrite(LogLevel,const char*,int,const char*,...) {}
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t address,const void*,std::size_t size) {
  return signature && address==base+0x320980 && size==15;
}
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size) {
  if(ReadScopeActive())++scoped_reads;else ++unscoped_reads;
  if (address==base+0x1c77e50 && size==sizeof(std::uintptr_t)) {
    const auto p=Ptr(world);std::memcpy(out,&p,size);return true;
  }
  if (address==base+0x13251f0+0xa8 && size==sizeof(std::uintptr_t)) {
    std::memcpy(out,&player_pose_method,size);return true;
  }
  const bool copied=Copy(address,out,size);
  if (copied && address==Ptr(poses) && size==sizeof(poses) && mutate_after_pose_read) {
    const auto mutation=mutate_after_pose_read;mutate_after_pose_read=0;
    if (mutation==1) Put(model,0x78,static_cast<std::uintptr_t>(1));
    if (mutation==2) Put(model,0x18,static_cast<std::uintptr_t>(0));
    if (mutation==3) Put(flver,0x18,static_cast<std::uintptr_t>(0));
    if (mutation==4) Put(pose_output,0x60,Ptr(other));
  }
  return copied;
}
bool GuardedWriteBytes(std::uintptr_t address,const void* data,std::size_t size) {
  const auto start=Ptr(poses);
  if (address<start || size!=sizeof(ArmPoseMatrix) ||
      (address-start)%sizeof(ArmPoseMatrix)!=0 || address-start>sizeof(poses)-size) {
    ++forbidden;return false;
  }
  const auto bone=static_cast<unsigned>((address-start)/sizeof(ArmPoseMatrix));
  if (!write_this_original) { ++writes;write_this_original=true; }
  ++writes_by_bone[bone];std::memcpy(reinterpret_cast<void*>(address),data,size);return true;
}
Config DiagnosticsCurrentConfig() { return cfg; }
std::uint64_t XrAppliedCameraSequence() {return camera_sequence;}
UserCalibration CalibrationCurrent() { return calibration; }
bool CalibrationCapturesInput() { return capture_input; }
bool CombatNativeReadEquipment(std::uintptr_t actor,CombatEquipment& out) {
  ++equipment_reads;
  out={};if(!equipment_available)return false;
  out.player=actor;out.weapon_id[0]=200000;out.weapon_id[1]=100000;return true;
}
void CombatNativeInvalidateEquipment() {}
bool CameraTrackingFrame(Vec3& head,Vec3& right,Vec3& up,Vec3& forward,float ref[4],Vec3* anchor) {
  if (!camera_ok) return false;
  // Run-9 model convention, matching real camera render-root calibration.
  head=VAdd(camera_anchor,VAdd(Vec3{0,1.5f,0},camera_lean));right={.99883664f,0,-.04822187f};
  up={0,1,0};forward={.04822187f,0,.99883664f};
  ref[0]=ref[1]=ref[2]=0;ref[3]=1;if(anchor)*anchor=camera_anchor;return true;
}
bool CameraHeadBasis(Vec3& right,Vec3& up,Vec3& fwd) {
  if (!head_basis_ok) return false;
  fwd=VNorm(stub_head_fwd);right=VNorm(VCross(Vec3{0,1,0},fwd));
  up=VCross(fwd,right);return true;
}
bool XrMenuPresentationActive() { return menu; }
bool XrLatestTrackingFrame(TrackingFrame& out) { if(!latest_ok)return false;out=latest;return true; }
}
int CheckMirroredGripRoll() {
  using namespace dsvr;
  const auto saved_tracking=latest;
  const float saved_roll=cfg.grip_roll_correction_deg;
  for(float degrees:{0.f,Config{}.grip_roll_correction_deg,-12.f}) {
    cfg.grip_roll_correction_deg=degrees;
    for(int rotation=0;rotation<3;++rotation) {
      for(auto& hand:latest.arm) {
        hand.quat[0]=hand.quat[1]=hand.quat[2]=0;hand.quat[3]=1;
        if(rotation) { hand.quat[rotation==1?2:0]=.70710678f;hand.quat[3]=.70710678f; }
      }
      // Losing controller tracking must not bring the obstructive head back.
  latest.arm[0].valid=latest.arm[1].valid=false;
  Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(std::memcmp(&poses[19],&native[19],sizeof(ArmPoseMatrix))==0 &&
        std::memcmp(&poses[18],&native[18],sizeof(ArmPoseMatrix))==0);
  CHECK(std::memcmp(&poses[26],&native[26],sizeof(ArmPoseMatrix))==0);
  latest.arm[0].valid=latest.arm[1].valid=true;
  Fresh();ArmIkTick(latest);callback(player.data());
  // A newer render-thread XR publication must not invalidate the coherent
  // packet already bound to this still-current natural camera update.
  const auto before_publication=writes;
  ++latest.sequence;callback(player.data());CHECK(writes==before_publication+1);
  --latest.sequence;
  const auto before=writes;
      Fresh();ArmIkTick(latest);callback(player.data());CHECK(writes==before+1);
      for(int side=0;side<2;++side) {
        const int hand=side?43:26,weapon=side?53:36;
        // Root columns are -camera right, camera up, -camera forward. With identity head
        // reference, XR -> model is diag(-1,1,1): +90 XR Z is -90 model Z,
        // whereas +90 XR X remains +90. Native wrist->weapon is -0.1 Z,
        // so positive configured roll is -Z on the left, +Z on the right.
        const float angle=(side?-1.f:1.f)*degrees*.017453292f;
        const float c=std::cos(angle),s=std::sin(angle);
        const float base[2][9]={{1,0,0,0,0,-1,0,1,0},{-1,0,0,0,0,-1,0,-1,0}};
        const float turn[3][9]={{1,0,0,0,1,0,0,0,1},{0,1,0,-1,0,0,0,0,1},{1,0,0,0,0,-1,0,1,0}};
        const float roll[9]={c,0,s,0,1,0,-s,0,c};
        float rolled[9]{},expected[9]{};
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)for(int k=0;k<3;++k)
          rolled[row*3+col]+=roll[row*3+k]*base[side][k*3+col];
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)for(int k=0;k<3;++k)
          expected[row*3+col]+=turn[rotation][row*3+k]*rolled[k*3+col];
        for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
          CHECK(std::fabs(poses[hand].m[row*4+col]-expected[row*3+col])<1e-4f);
          CHECK(std::fabs(poses[weapon].m[row*4+col]-expected[row*3+col])<1e-4f);
        }
        const Vec3 grip=ArmPoseOrigin(poses[weapon]),wrist=ArmPoseOrigin(poses[hand]);
        const Vec3 target{-latest.arm[side].pos[0],latest.arm[side].pos[1],
                          latest.arm[side].pos[2]};
        CHECK(VLen(arm_pose_detail::Sub(grip,target))<1e-5f);
        const Vec3 offset{-.1f*expected[2],-.1f*expected[5],-.1f*expected[8]};
        CHECK(VLen(arm_pose_detail::Sub(arm_pose_detail::Sub(grip,wrist),offset))<1e-5f);
        CHECK(std::fabs(VLen(arm_pose_detail::Sub(grip,wrist))-.1f)<1e-5f);

      }
    }
  }
  cfg.grip_roll_correction_deg=saved_roll;latest=saved_tracking;
  Fresh();ArmIkTick(latest);callback(player.data());
  return 0;
}

int CheckCombatBench() {
  using namespace dsvr;
  const auto saved=latest;
  const auto saved_cfg=cfg;
  cfg.combat_test_bench=true;cfg.motion_attacks=true;
  ArmIkResetCalibration();
  long long prediction=1000000000;
  const auto step=[&] {
    ++latest.sequence;prediction+=12000000;latest.predicted_time_ns=prediction;
    Fresh();ArmIkTick(latest);callback(player.data());
  };
  step();
  CombatBenchSnapshot bench;
  CHECK(ArmIkCombatBench(bench));
  CHECK(bench.tip_speed[0]==0 && bench.tip_speed[1]==0);
  // Grip-local -Z follows the camera heading exactly once, even with
  // this fixture's rotated native root. It is not a matrix row or +Z.
  const Vec3 direction=VNorm(VAdd(bench.tip[1],VScale(bench.grip[1],-1)));
  CHECK(VLen(VAdd(direction,VScale(Vec3{.04822187f,0,.99883664f},-1)))<.0001f);
  // Physical walking and native root translations cannot become attack energy.
  for(int i=0;i<3;++i) {
    latest.head_pos[0]+=.04f;
    latest.arm[0].pos[0]+=.04f;latest.arm[1].pos[0]+=.04f;
    camera_lean=VScale(Vec3{.99883664f,0,-.04822187f},.04f*(i+1));
    step();CHECK(ArmIkCombatBench(bench));
    CHECK(bench.tip_speed[0]<.001f && bench.tip_speed[1]<.001f);
  }
  latest.arm[1].pos[0]+=.06f;step();CHECK(ArmIkCombatBench(bench));
  CHECK(bench.tip_speed[1]>2 && bench.tip_speed[0]<.001f);
  // Repeated callbacks for one camera packet must not clear motion or gates.
  const float speed=bench.tip_speed[1];callback(player.data());
  CHECK(ArmIkCombatBench(bench) && bench.tip_speed[1]==speed);
  // Loss/recenter/menu/disable all start a new history at rest.
  latest.arm[1].valid=false;step();latest.arm[1].valid=true;
  latest.arm[1].pos[0]+=.06f;step();CHECK(ArmIkCombatBench(bench));
  CHECK(bench.tip_speed[1]==0);
  ++latest.space_epoch;latest.arm[1].pos[0]+=.06f;step();
  CHECK(ArmIkCombatBench(bench) && bench.tip_speed[1]==0);
  menu=true;CHECK(!ArmIkCombatBench(bench));ArmIkTick(latest);menu=false;
  latest.arm[1].pos[0]+=.06f;step();
  CHECK(ArmIkCombatBench(bench) && bench.tip_speed[1]==0);
  cfg.motion_attacks=false;step();CHECK(!ArmIkCombatBench(bench));
  cfg.motion_attacks=true;latest.arm[1].pos[0]+=.06f;step();
  CHECK(ArmIkCombatBench(bench) && bench.tip_speed[1]==0);
  // A swapped loadout/dominant hand reprimes rather than carrying old energy.
  cfg.dominant_hand_left=!cfg.dominant_hand_left;
  latest.arm[1].pos[0]+=.06f;step();
  CHECK(ArmIkCombatBench(bench) && bench.tip_speed[1]==0);
  camera_lean={};latest=saved;cfg=saved_cfg;
  ArmIkResetCalibration();Fresh();ArmIkTick(latest);callback(player.data());
  return 0;
}

int main() {
  using namespace dsvr;
  Put(world,0x68,Ptr(player));Put(player,0,base+0x13251f0);Put(player,0x60,Ptr(model));
  Put(model,0,base+0x12eaf30);Put(model,0x18,Ptr(flver));
  Put(model,0x60,base+0x12eacb8);Put(model,0x68,1u);Put(model,0x6c,80u);
  Put(model,0x70,Ptr(poses));Put(model,0x128,Ptr(model)+0x60);
  ArmPoseMatrix root{{-.99883664f,0,-.04822187f,10,0,1,0,20,.04822187f,0,-.99883664f,30}};
  std::memcpy(model.data()+0x30,&root,sizeof(root));
  Put(flver,0,base+0x12eb760);Put(flver,0x10,static_cast<short>(69));
  Put(flver,0x12,static_cast<short>(80));Put(flver,0x18,Ptr(entries));
  for (int i=0;i<80;++i) {
    swprintf_s(names[i].data(),names[i].size(),L"Bone%d",i);
    Put(entries,i*0x30+0x20,Ptr(names[i]));Put(entries,i*0x30+0x28,static_cast<short>(-1));
    native[i]=Pose(0,0,0);
  }
  Name(2,L"Pelvis",1);Name(15,L"Upper_Root",0);
  Name(3,L"L_Thigh",2);Name(4,L"L_Calf",3);Name(5,L"L_Foot",4);Name(7,L"LCalfTwist",3);
  Name(8,L"R_Thigh",2);Name(9,L"R_Calf",8);Name(10,L"R_Foot",9);Name(12,L"RCalfTwist",8);
  Name(13,L"L_Wepon_Case",2);
  Name(23,L"L_Clavicle",17);Name(40,L"R_Clavicle",17);
  Name(24,L"L_UpperArm",23);Name(25,L"L_Forearm",24);Name(26,L"L_Hand",25);
  Name(39,L"LUpArmTwist",23);Name(36,L"L_Weapon",26);
  Name(41,L"R_UpperArm",40);Name(42,L"R_Forearm",41);Name(43,L"R_Hand",42);
  Name(56,L"RUpArmTwist",40);Name(53,L"R_Weapon",43);
  Name(18,L"Neck",17);Name(19,L"Head",18);
  Name(38,L"L_ForeTwist",24);Name(55,L"R_ForeTwist",41);
  native[2]=Pose(0,.7f,0);native[13]=Pose(.2f,.7f,0);native[15]=Pose(0,.8f,0);
  native[3]=Pose(-.1f,.7f,0);native[4]=Pose(-.05f,.4f,0);native[5]=Pose(-.1f,.1f,0);native[7]=Pose(-.05f,.4f,0);
  native[8]=Pose(.1f,.7f,0);native[9]=Pose(.05f,.4f,0);native[10]=Pose(.1f,.1f,0);native[12]=Pose(.05f,.4f,0);
  native[24]=native[39]=Pose(.15f,1.3f,0);native[25]=Pose(.35f,1.15f,0);
  native[26]=Pose(.45f,.95f,0);native[36]=Pose(.45f,.95f,-.1f);
  native[41]=native[56]=Pose(-.15f,1.3f,0);native[42]=Pose(-.35f,1.15f,0);
  native[43]=Pose(-.45f,.95f,0);native[53]=Pose(-.45f,.95f,-.1f);
  native[18]=Pose(0,1.5f,0);native[19]=Pose(0,1.65f,0);
  native[79]={}; // real native palette has unused zero-basis slots outside either arm
  poses=native;
  cfg.vr_enabled=cfg.arm_ik_enabled=cfg.headlook_enabled=true;cfg.arm_ik_log_only=false;
  // Isolate tracking rotation; configured roll is exercised separately below.
  cfg.grip_roll_correction_deg=0;
  latest.focused=latest.orientation_valid=latest.position_valid=true;
  latest.sequence=1;latest.space_epoch=1;latest.head_pos[1]=1.5f;
  for (int side=0;side<2;++side) {
    latest.arm[side].valid=true;latest.arm[side].pos[0]=side?.35f:-.35f;
    latest.arm[side].pos[1]=1.1f;latest.arm[side].pos[2]=-.2f;
  }
  Put(player,0x850,Ptr(pose_output));Put(pose_output,0x60,Ptr(poses));
  player_pose_method=base+0x320980;
  CHECK(!ArmIkHookInstall() && creates==0);
  player_pose_method=base+0x357a20;
  Fresh();ArmIkTick(latest);CHECK(callback && creates==1);
  callback(player.data());CHECK(originals==1 && writes==1 && forbidden==0);
  CHECK(original_scope_leaks==0 && scoped_reads>0 && !ReadScopeActive());
  CHECK(writes_by_bone[0]==0 && writes_by_bone[79]==0);
  for(unsigned bone=0;bone<80;++bone) if(writes_by_bone[bone])
    CHECK(std::memcmp(&poses[bone],&native[bone],sizeof(ArmPoseMatrix))!=0);
  CHECK(VLen(VAdd(ArmPoseOrigin(poses[26]),{-.35f,-1.f,.2f}))<1e-5f);
  CHECK(VLen(VAdd(ArmPoseOrigin(poses[43]),{.35f,-1.f,.2f}))<1e-5f);
  // The XR target is the grip pose: the weapon mount, not the wrist joint,
  // must land on it. Native grip offset here is (0,0,-0.1) from the hand.
  CHECK(VLen(VAdd(ArmPoseOrigin(poses[36]),{-.35f,-1.1f,.2f}))<1e-5f);
  CHECK(VLen(VAdd(ArmPoseOrigin(poses[53]),{.35f,-1.1f,.2f}))<1e-5f);
  const auto after_first_commit=writes;
  Put(pose_output,0x60,Ptr(other));
  Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(writes==after_first_commit); // output buffer must alias selected ChrModel poses
  Put(pose_output,0x60,Ptr(poses));
  // Native head/neck geometry filtering is separate from skeletal retargeting.
  // Bone matrices stay nonsingular even while the head-visibility setting is on.
  for(int untouched:{18,19})
    CHECK(std::memcmp(&poses[untouched],&native[untouched],sizeof(ArmPoseMatrix))==0);
  CHECK(std::memcmp(&poses[0],&native[0],sizeof(ArmPoseMatrix))==0);
  CHECK(std::memcmp(model.data()+0x30,&root,sizeof(root))==0);
  // Root movement after the camera sample cannot drag targets away from this body.
  root.m[3]=100;root.m[7]=200;root.m[11]=300;std::memcpy(model.data()+0x30,&root,sizeof(root));
  Fresh();ArmIkTick(latest);callback(player.data());CHECK(writes==2);
  CHECK(VLen(VAdd(ArmPoseOrigin(poses[26]),{-.35f,-1.f,.2f}))<1e-5f);
  // Turning a controller in place must turn its own wrist and equipment.
  latest.arm[0].quat[2]=.70710678f;latest.arm[0].quat[3]=.70710678f;
  Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(std::fabs(poses[26].m[0])<1e-4f && poses[26].m[4]<-.999f);
  CHECK(std::fabs(poses[43].m[0]+1)<1e-4f && std::fabs(poses[43].m[4])<1e-4f);
  latest.arm[0].quat[2]=0;latest.arm[0].quat[3]=1;
  Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(CheckMirroredGripRoll() == 0);
  CHECK(CheckCombatBench() == 0);
  {
    // Exercise the real post-animation callback and real grip solver together:
    // the support hand changes the primary weapon only while grip is held.
    const auto saved=latest;
    equipment_available = true;
    g_cpu_frame_timer.Reset();

    WeaponGripProfile profile;profile.weapon_id=100000;profile.support.position_m={0,0,.2f};
    CHECK(WeaponGripPreview(profile));
    latest.arm[1].pos[0]=.3f;latest.arm[1].pos[1]=1.1f;latest.arm[1].pos[2]=-.2f;
    latest.arm[0].pos[0]=.3f;latest.arm[0].pos[1]=.9f;latest.arm[0].pos[2]=-.2f;
    latest.arm[0].squeeze_held=false;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    CHECK(g_cpu_frame_timer.Snapshot()[static_cast<unsigned>(CpuStage::EquipmentRead)].calls == 1);
    const auto free_weapon=poses[53];

    latest.arm[0].squeeze_held=true;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    latest.arm[0].pos[0]=.15f;latest.arm[0].pos[1]=1.f;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    float change=0;for(int j:{0,1,2,4,5,6,8,9,10})change+=std::fabs(poses[53].m[j]-free_weapon.m[j]);
    CHECK(change>.1f);
    const auto support_before=poses[26];const auto primary_before=poses[53];
    profile.support.rotation_deg={0,0,30};CHECK(WeaponGripPreview(profile));
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    float support_change=0;
    for(int j:{0,1,2,4,5,6,8,9,10})support_change+=std::fabs(poses[26].m[j]-support_before.m[j]);
    CHECK(support_change>.1f);
    for(int j=0;j<12;++j)CHECK(std::fabs(poses[53].m[j]-primary_before.m[j])<1e-5f);
    latest.arm[0].squeeze_held=false;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    for(int j=0;j<12;++j)CHECK(std::fabs(poses[53].m[j]-free_weapon.m[j])<1e-5f);
    WeaponGripClearPreview();equipment_available=false;latest=saved;
    Fresh();ArmIkTick(latest);callback(player.data());
  }
  ArmIkHands published{};
  CHECK(ArmIkLatestHands(published) && published.sequence==latest.sequence && published.side[0].valid);
  // Respawn/scene rebinding cannot capture another animation as neutral wrist.
  const auto stable=poses;
  for(int i=0;i<20;++i) {
    ArmIkResetCalibration();++latest.space_epoch;++latest.sequence;
    Fresh();ArmIkTick(latest);callback(player.data());
    for(int hand:{26,43})for(int j=0;j<12;++j)
      CHECK(std::fabs(poses[hand].m[j]-stable[hand].m[j])<1e-5f);
  }
  cfg.forearm_only=true;Fresh();ArmIkTick(latest);callback(player.data());
  // A visibility request must never collapse pelvis/upper-arm skinning bones.
  // Mixed elbow weights otherwise stretch an upper-arm triangle to the root.
  // Until a native mesh filter is bound, preserve the solved skinning palette.
  for(int preserved:{0,24,41})
    for(int j=0;j<12;++j)CHECK(std::fabs(poses[preserved].m[j]-stable[preserved].m[j])<1e-5f);
  for(int kept:{25,26,36,38,42,43,53,55})
    for(int j=0;j<12;++j)CHECK(std::fabs(poses[kept].m[j]-stable[kept].m[j])<1e-5f);
  cfg.forearm_only=false;Fresh();ArmIkTick(latest);callback(player.data());
  {
    // Torso follows the head: bind a spine chain (16 -> 17 -> Neck 18),
    // turn the stub head +0.5 rad off the heading, and require the solved
    // shoulders to swing with the gaze while grips stay on target and
    // non-spine bones keep the native palette.
    const auto saved_native=native;
    native[15]=Pose(0,.8f,0);native[16]=Pose(0,1.0f,0);native[17]=Pose(0,1.25f,0);
    native[23]=Pose(.15f,1.3f,0);native[40]=Pose(-.15f,1.3f,0);
    Put(entries,17*0x30+0x28,static_cast<short>(16));
    Put(entries,16*0x30+0x28,static_cast<short>(15));
    Put(entries,23*0x30+0x28,static_cast<short>(17));
    Put(entries,40*0x30+0x28,static_cast<short>(17));
    stub_head_fwd=VAdd(VScale(Vec3{.04822187f,0,.99883664f},std::cos(.5f)),
                       VScale(Vec3{.99883664f,0,-.04822187f},std::sin(.5f)));
    head_basis_ok=true;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    CHECK(VLen(VAdd(ArmPoseOrigin(poses[36]),{-.35f,-1.1f,.2f}))<.08f);
    CHECK(VLen(VAdd(ArmPoseOrigin(poses[53]),{.35f,-1.1f,.2f}))<.08f);
    // The fixture's spine chain sits on the rotation axis, so origins stay
    // put; the neck basis carrying the head must visibly rotate instead.
    CHECK(std::fabs(poses[18].m[8])>.02f || std::fabs(poses[18].m[0]-1)>.02f);
    CHECK(std::fabs(poses[19].m[8])>.02f || std::fabs(poses[19].m[0]-1)>.02f);
    // The head bone snaps to the stub camera facing, and the pelvis bone
    // carries its bounded share of the turn.
    {
      ArmPoseMatrix root_zero=root;root_zero.m[3]=root_zero.m[7]=root_zero.m[11]=0;
      const Vec3 head_col{poses[19].m[2],poses[19].m[6],poses[19].m[10]};
      const Vec3 facing=VNorm(VScale(ArmPoseTransformPoint(root_zero,head_col),-1.0f));
      CHECK(VDot(facing,stub_head_fwd)>0.999f);
      CHECK(std::fabs(poses[2].m[8])>.02f || std::fabs(poses[2].m[0]-1)>.02f);
      CHECK(std::memcmp(&poses[13],&native[13],sizeof(ArmPoseMatrix))==0);
    }
    CHECK(std::fabs(VLen(arm_pose_detail::Sub(ArmPoseOrigin(poses[25]),ArmPoseOrigin(poses[24])))-
                    VLen(arm_pose_detail::Sub(ArmPoseOrigin(native[25]),ArmPoseOrigin(native[24]))))<1e-5f);
    CHECK(std::memcmp(&poses[0],&native[0],sizeof(ArmPoseMatrix))==0);
    // Root translation is the body-motion source: solve leg chains so the
    // native feet remain stable in world space instead of sliding with root.
    const Vec3 left_foot_world=ArmPoseTransformPoint(root,ArmPoseOrigin(poses[5]));
    const Vec3 right_foot_world=ArmPoseTransformPoint(root,ArmPoseOrigin(poses[10]));
    const auto previous_capture=latest.captured_ms;
    ++latest.sequence;
    do { Fresh(); if(latest.captured_ms<=previous_capture) SwitchToThread(); }
    while(latest.captured_ms<=previous_capture);
    root.m[3]+=.01f;std::memcpy(model.data()+0x30,&root,sizeof(root));
    ArmIkTick(latest);callback(player.data());
    CHECK(VLen(arm_pose_detail::Sub(ArmPoseTransformPoint(root,ArmPoseOrigin(poses[5])),
                                    left_foot_world))<1e-3f);
    CHECK(VLen(arm_pose_detail::Sub(ArmPoseTransformPoint(root,ArmPoseOrigin(poses[10])),
                                    right_foot_world))<1e-3f);
    // An epoch transition must discard the previous damped head turn,
    // even when the recovery frame is published immediately.
    const float neck_turn=poses[18].m[8];
    const float spine_turn=poses[16].m[8];
    stub_head_fwd=VAdd(VScale(Vec3{.04822187f,0,.99883664f},std::cos(.5f)),
                       VScale(Vec3{.99883664f,0,-.04822187f},-std::sin(.5f)));
    ++latest.space_epoch;++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    CHECK(neck_turn*poses[18].m[8]<0);
    CHECK(spine_turn*poses[16].m[8]<0);

    // All lifecycle recoveries must start at the new target, not damp from a
    // previous lifetime. Native always restores `native` before the detour runs.
    const auto recovered=poses;
    // A later camera-basis publication cannot mix a new gaze into this packet.
    ArmIkResetCalibration();
    stub_head_fwd=VAdd(VScale(Vec3{.04822187f,0,.99883664f},std::cos(.5f)),
                       VScale(Vec3{.99883664f,0,-.04822187f},std::sin(.5f)));
    ++latest.sequence;Fresh();ArmIkTick(latest);
    stub_head_fwd=VAdd(VScale(Vec3{.04822187f,0,.99883664f},std::cos(.5f)),
                       VScale(Vec3{.99883664f,0,-.04822187f},-std::sin(.5f)));
    callback(player.data());
    CHECK(spine_turn*poses[16].m[8]>0);
    const auto publish_turn=[&](float angle) {
      stub_head_fwd=VAdd(VScale(Vec3{.04822187f,0,.99883664f},std::cos(angle)),
                        VScale(Vec3{.99883664f,0,-.04822187f},std::sin(angle)));
      const auto old_originals=originals,old_writes=writes;
      ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
      return originals==old_originals+1 && writes==old_writes+1;
    };
    for (int boundary=0;boundary<14;++boundary) {
      ArmIkResetCalibration();CHECK(publish_turn(.5f));
      const auto old_originals=originals;
      if (boundary==0) {
        std::thread resetter([] { ArmIkResetCalibration(); });resetter.join();
      }
      if (boundary==1) ++latest.space_epoch;
      if (boundary==2) ++calibration.revision;
      if (boundary==3) {
        latest.captured_ms=GetTickCount64()-101;ArmIkTick(latest);callback(player.data());
      }
      if (boundary==4) { latest_ok=false;callback(player.data());latest_ok=true; }
      if (boundary==5) { ++camera_sequence;callback(player.data());--camera_sequence; }
      if (boundary==6) { head_basis_ok=false;callback(player.data());head_basis_ok=true; }
      if (boundary==7) { menu=true;ArmIkTick(latest);callback(player.data());menu=false; }
      if (boundary==8) {
        capture_input=true;ArmIkTick(latest);callback(player.data());capture_input=false;
      }
      if (boundary==9) { camera_ok=false;ArmIkTick(latest);callback(player.data());camera_ok=true; }
      if (boundary==10) {
        root.m[3]+=3;std::memcpy(model.data()+0x30,&root,sizeof(root));
      }
      if (boundary==11) Put(entries,78*0x30+0x28,static_cast<short>(0));
      if (boundary==12) {
        rebound_entries=entries;Put(flver,0x18,Ptr(rebound_entries));
      }
      if (boundary==13) {
        ++latest.space_epoch;callback(player.data()); // Mismatch without publication.
      }
      CHECK(originals==old_originals+((boundary>=3&&boundary<=9)||boundary==13?1:0));
      CHECK(publish_turn(-.5f));
      for(int bone:{15,16,17,18,19})for(int j=0;j<12;++j)
        CHECK(std::fabs(poses[bone].m[j]-recovered[bone].m[j])<1e-5f);
      if (boundary==10) {
        root.m[3]-=3;std::memcpy(model.data()+0x30,&root,sizeof(root));
      }
      if (boundary==11) Put(entries,78*0x30+0x28,static_cast<short>(-1));
      if (boundary==12) Put(flver,0x18,Ptr(entries));
    }

    // Head-driven torso tracking survives controller loss without publishing
    // invented hand poses or keeping a stale two-hand attachment.
    latest.arm[0].valid=latest.arm[1].valid=false;
    ArmIkResetCalibration();CHECK(publish_turn(.5f));
    CHECK(std::fabs(poses[16].m[8])>.02f);
    ArmIkHands head_only{};CHECK(ArmIkLatestHands(head_only));
    CHECK(!head_only.side[0].valid && !head_only.side[1].valid);
    latest.arm[1].valid=true;
    CHECK(publish_turn(.5f));
    CHECK(ArmIkLatestHands(head_only) && !head_only.side[0].valid && head_only.side[1].valid);
    latest.arm[0].valid=true;

    // A refused full-body solve cannot be hidden by a subsequent valid arm
    // solve: the only visible palette must be the restored native animation.
    const auto valid_head=native[19];native[19]={};
    const auto refused_writes=writes,refused_originals=originals;
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
    CHECK(writes==refused_writes && originals==refused_originals+1);
    CHECK(std::memcmp(poses.data(),native.data(),sizeof(poses))==0);
    native[19]=valid_head;CHECK(publish_turn(-.5f));

    // Calibrated lean bends real segments without accumulating an offset when
    // the native callback restores the same synthetic animation each frame.
    const auto saved_calibration=calibration;
    calibration.valid=true;++calibration.revision;
    calibration.eye_height_m=latest.head_pos[1];
    latest.head_pos[0]+=.08f;
    camera_lean=VScale(Vec3{.99883664f,0,-.04822187f},.08f);
    ArmIkResetCalibration();CHECK(publish_turn(.5f));
    const auto leaned=poses;
    CHECK(VLen(arm_pose_detail::Sub(ArmPoseOrigin(leaned[18]),ArmPoseOrigin(native[18])))>.01f);
    for(int frame=0;frame<12;++frame) {
      CHECK(publish_turn(.5f));
      for(int bone=0;bone<80;++bone)for(int j=0;j<12;++j)
        CHECK(std::fabs(poses[bone].m[j]-leaned[bone].m[j])<1e-5f);
      for(const auto& segment:std::array<std::array<int,2>,4>{{{15,16},{16,17},{17,18},{18,19}}})
        CHECK(std::fabs(VLen(arm_pose_detail::Sub(ArmPoseOrigin(poses[segment[0]]),ArmPoseOrigin(poses[segment[1]])))-
                        VLen(arm_pose_detail::Sub(ArmPoseOrigin(native[segment[0]]),ArmPoseOrigin(native[segment[1]]))))<1e-5f);
      CHECK(std::memcmp(&poses[0],&native[0],sizeof(ArmPoseMatrix))==0);
      CHECK(std::memcmp(model.data()+0x30,&root,sizeof(root))==0);
    }
    latest.head_pos[0]-=.08f;camera_lean={};
    calibration=saved_calibration;ArmIkResetCalibration();
    native=saved_native;
    Put(entries,23*0x30+0x28,static_cast<short>(-1));
    Put(entries,40*0x30+0x28,static_cast<short>(-1));
    head_basis_ok=false;stub_head_fwd={.04822187f,0,.99883664f};
    Put(entries,17*0x30+0x28,static_cast<short>(-1));
    Put(entries,16*0x30+0x28,static_cast<short>(-1));
    ++latest.sequence;Fresh();ArmIkTick(latest);callback(player.data());
  }
  CHECK(poses[24].m[0]!=0 && ArmIkLatestHands(published));
  // Losing controllers must leave animation intact. The independent native
  // mesh filter owns visibility even when no arm can be solved.
  latest.arm[0].valid=latest.arm[1].valid=false;
  Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(std::memcmp(&poses[19],&native[19],sizeof(ArmPoseMatrix))==0);
  CHECK(std::memcmp(&poses[26],&native[26],sizeof(ArmPoseMatrix))==0);
  latest.arm[0].valid=latest.arm[1].valid=true;
  Fresh();ArmIkTick(latest);callback(player.data());
  // A newer render-thread XR publication must not invalidate the coherent
  // packet already bound to this still-current natural camera update.
  const auto before_publication=writes;
  ++latest.sequence;callback(player.data());CHECK(writes==before_publication+1);
  --latest.sequence;
  const auto before=writes;
  cfg.arm_ik_log_only=true;Fresh();ArmIkTick(latest);callback(player.data());
  CHECK(writes==before && std::memcmp(poses.data(),native.data(),sizeof(poses))==0);cfg.arm_ik_log_only=false;
  cfg.vr_enabled=false;callback(player.data());CHECK(writes==before);cfg.vr_enabled=true;
  latest_ok=false;callback(player.data());CHECK(writes==before);latest_ok=true;
  menu=true;callback(player.data());CHECK(writes==before);menu=false;
  camera_ok=false;callback(player.data());CHECK(writes==before);camera_ok=true;
  ++latest.sequence;++camera_sequence;callback(player.data());CHECK(writes==before);--latest.sequence;--camera_sequence;
  ++latest.space_epoch;callback(player.data());CHECK(writes==before);--latest.space_epoch;
  const auto reads_before_foreign=equipment_reads;
  const auto scoped_before_foreign=scoped_reads;
  const auto unscoped_before_foreign=unscoped_reads;
  callback(other.data());CHECK(writes==before);
  CHECK(equipment_reads==reads_before_foreign); // NPC animation must not query player equipment
  std::thread foreign([&]{callback(player.data());});foreign.join();CHECK(writes==before);
  CHECK(scoped_reads==scoped_before_foreign && unscoped_reads==unscoped_before_foreign);
  CHECK(original_scope_leaks==0 && !ReadScopeActive());
  // Disabled/menu callbacks still complete native animation, but cannot query
  // equipment/game memory or reuse the preceding callback's transaction.
  const auto originals_before_disabled=originals;
  cfg.arm_ik_enabled=false;callback(player.data());cfg.arm_ik_enabled=true;
  cfg.headlook_enabled=false;callback(player.data());cfg.headlook_enabled=true;
  menu=true;callback(player.data());menu=false;
  CHECK(originals==originals_before_disabled+3 && original_scope_leaks==0);
  CHECK(writes==before && equipment_reads==reads_before_foreign);
  CHECK(scoped_reads==scoped_before_foreign && unscoped_reads==unscoped_before_foreign);
  CHECK(std::memcmp(poses.data(),native.data(),sizeof(poses))==0 && !ReadScopeActive());
  Put(model,0x68,2u);callback(player.data());CHECK(writes==before);Put(model,0x68,1u);
  Put(model,0x6c,81u);callback(player.data());CHECK(writes==before);Put(model,0x6c,80u);
  Put(model,0x128,Ptr(model)+0x20);callback(player.data());CHECK(writes==before);Put(model,0x128,Ptr(model)+0x60);
  Put(model,0x78,static_cast<std::uintptr_t>(1));callback(player.data());CHECK(writes==before);Put(model,0x78,static_cast<std::uintptr_t>(0));
  // Ownership can change after the initial pose snapshot; refuse the stale write.
  for (unsigned mutation=1;mutation<=4;++mutation) {
    mutate_after_pose_read=mutation;callback(player.data());CHECK(writes==before);
    CHECK(std::memcmp(poses.data(),native.data(),sizeof(poses))==0);
    Put(model,0x78,static_cast<std::uintptr_t>(0));Put(model,0x18,Ptr(flver));Put(flver,0x18,Ptr(entries));
    Put(pose_output,0x60,Ptr(poses));
  }
  // Same-address skeleton edits must invalidate the cached hierarchy.
  Put(entries,25*0x30+0x28,static_cast<short>(-1));callback(player.data());CHECK(writes==before);
  Put(entries,25*0x30+0x28,static_cast<short>(24));
  // A detached weapon mount removes the grip point; refuse rather than
  // fall back to solving the wrist onto the controller.
  Put(entries,36*0x30+0x28,static_cast<short>(-1));callback(player.data());CHECK(writes==before);
  Put(entries,36*0x30+0x28,static_cast<short>(26));
  cfg.observer_only=true;Fresh();ArmIkTick(latest);callback(player.data());CHECK(writes==before);cfg.observer_only=false;
  Fresh();ArmIkTick(latest);
  enable_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  enable_continue=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::thread installer([]{ ArmIkHookInstall(); });
  CHECK(WaitForSingleObject(enable_entered,1000)==WAIT_OBJECT_0);
  std::thread uninstaller([&]{ ArmIkHookUninstall();SetEvent(stopped); });
  const bool serialized=WaitForSingleObject(stopped,50)==WAIT_TIMEOUT;
  SetEvent(enable_continue);installer.join();uninstaller.join();
  CloseHandle(enable_entered);CloseHandle(enable_continue);CloseHandle(stopped);
  enable_entered=enable_continue=nullptr;
  CHECK(serialized && !ArmIkHookInstall());
  callback(player.data());CHECK(writes==before && disables==1);
  CHECK(std::memcmp(poses.data(),native.data(),sizeof(poses))==0 && forbidden==0 && creates==1);
  std::puts("PASS native arm adapter ownership/refusal (memory fixture, not live rendering)");
}
