// Exercise the real camera adapter with bounded native-memory fixtures.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "../src/common/memory.h"
#include "../src/common/logging.h"
#include "../src/core/camera.h"
#include "../src/core/input.h"
#include "../src/core/arm_ik.h"
#include "../src/core/calibration.h"
#include "../src/core/vr_trace.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
constexpr std::uintptr_t base=0x140000000;
std::array<unsigned char,0x100> world{}, container{};
std::array<unsigned char,0x300> physics_a{};
std::array<unsigned char,0x900> player{};
std::array<unsigned char,0x150> model{};
std::array<unsigned char,0x150> physics_b{}, control{}, position{};
std::array<unsigned char,0x320> follow{};
unsigned container_accesses=0, rejected_writes=0, unscoped_reads=0;
unsigned locomotion_publishes=0, locomotion_invalidations=0;
dsvr::Vec3 locomotion_head{}, locomotion_native{};
std::uint64_t locomotion_epoch=0;
dsvr::UserCalibration calibration;
dsvr::RoomscaleBasis published_roomscale;
std::uint64_t recenter_generation=0;
template<class T> std::uintptr_t Ptr(T& v) { return reinterpret_cast<std::uintptr_t>(v.data()); }
template<class T,class V> void Put(T& v,unsigned offset,V value) { std::memcpy(v.data()+offset,&value,sizeof(value)); }
void Matrix(unsigned offset,float x,float y,float z) {
  const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};
  std::memcpy(follow.data()+offset,m,sizeof(m));
}
float Float(unsigned offset) { float f; std::memcpy(&f,follow.data()+offset,4); return f; }
bool Copy(std::uintptr_t address,void* out,std::size_t size) {
  struct Range { std::uintptr_t begin; std::size_t size; };
  const Range ranges[]={{Ptr(world),world.size()},{Ptr(player),player.size()},
      {Ptr(physics_a),physics_a.size()},{Ptr(physics_b),physics_b.size()},
      {Ptr(model),model.size()},
      {Ptr(control),control.size()},{Ptr(position),position.size()},
      {Ptr(follow),follow.size()},{Ptr(container),container.size()}};
  for (const auto& r:ranges) {
    if (address>=r.begin && address-r.begin<=r.size && size<=r.size-(address-r.begin)) {
      std::memcpy(out,reinterpret_cast<void*>(address),size); return true;
    }
  }
  return false;
}
}
namespace dsvr {
void ArmIkResetCalibration() {}

bool ArmIkCombatBench(CombatBenchSnapshot& out) { out = {}; return false; }
void InputPublishRoomscaleBasis(const RoomscaleBasis& basis) {published_roomscale=basis;}
bool InputLatestRoomscaleMotion(RoomscaleMotion&) { return false; }
void InputInvalidateRoomscaleBasis() {}
void InputPublishHeadDirectedLocomotion(Vec3 head, Vec3 native, std::uint64_t epoch) {
  ++locomotion_publishes;
  locomotion_head = head;
  locomotion_native = native;
  locomotion_epoch = epoch;
}
void InputInvalidateHeadDirectedLocomotion() { ++locomotion_invalidations; }
UserCalibration CalibrationCurrent() { return calibration; }
std::uint64_t CalibrationRecenterGeneration() { return recenter_generation; }
void LogWrite(LogLevel,const char*,int,const char*,...) {}
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size) {
  // Observe the real adapter's transaction boundaries, not cached game data.
  GuardedReadScope::Remember(address,address+size);
  if (!GuardedReadScope::Contains(address,address+size)) ++unscoped_reads;
  if (address==base+0x1c6e188) {
    ++container_accesses; const auto p=Ptr(container);
    if (size!=sizeof(p)) return false;
    std::memcpy(out,&p,size); return true;
  }
  if (address>=Ptr(container) && address-Ptr(container)<container.size()) ++container_accesses;
  if (address==base+0x1c77e50 && size==sizeof(std::uintptr_t)) {
    const auto p=Ptr(world); std::memcpy(out,&p,size); return true;
  }
  return Copy(address,out,size);
}
bool GuardedWriteBytes(std::uintptr_t address,const void* data,std::size_t size) {
  if (address>=Ptr(container) && address-Ptr(container)<container.size()) ++container_accesses;
  const auto p=Ptr(follow);
  if (!((address==p+0x10 && size==64) || (address==p+0x2d0 && size==64) ||
        (address==p+0x50 && size==8))) { ++rejected_writes; return false; }
  std::memcpy(reinterpret_cast<void*>(address),data,size); return true;
}
std::optional<std::uintptr_t> FollowChain(std::uintptr_t p,const std::uintptr_t* offsets,std::size_t count) {
  for (std::size_t i=0;i<count;++i) if (!GuardedRead(p+offsets[i],p) || !p) return {};
  return p;
}
bool VrTraceActive() { return false; }
void VrTraceRecord(VrTraceEvent) {}
}
int main() {
  using namespace dsvr;
  Put(world,0x68,Ptr(player)); Put(player,0x7b8,Ptr(physics_a));
  Put(player,0,base+0x13251f0); Put(player,0x60,Ptr(model));
  Put(model,0,base+0x12eaf30);
  // Measured run-9 root: native left is +model-X, right is -model-X.
  // Its near-180-degree world rotation puts the right shoulder at world +X.
  const float root[12]={-.99883664f,0,-.04822187f,1,0,1,0,2,.04822187f,0,-.99883664f,3};
  std::memcpy(model.data()+0x30,root,sizeof(root));
  Put(physics_a,0x2e0,Ptr(physics_b));
  Put(player,0x18,Ptr(control)); Put(control,0x28,Ptr(position));
  const float xyz[4]={1,2,3,1};
  std::memcpy(physics_b.data()+0x120,xyz,sizeof(xyz));
  std::memcpy(position.data()+0x10,xyz,12);
  Put(position,4,3.14159265f);
  Matrix(0x10,1,4,3); Matrix(0x2d0,1,4,3);
  Put(follow,0x50,.75f); Put(follow,0x54,16.0f/9);
  // Deliberately plausible bytes beyond the real 0x18-byte container: old
  // code must not be allowed to turn unrelated adjacent memory into a camera.
  std::memcpy(container.data()+0x30,follow.data()+0x10,64);
  Put(container,0x70,.75f); Put(container,0x74,16.0f/9);
  const auto before_container=container;
  const auto before_follow=follow;
  CameraTraceFollowCam(follow.data());
  CHECK(container_accesses==0);
  CHECK(!CameraIsTitle());
  CHECK(CameraGameplayReady());
  std::printf("camera trace reads outside permission scope: %u (expected 0)\n",unscoped_reads);
  CHECK(unscoped_reads==0);
  CHECK(!GuardedReadScope::Contains(Ptr(follow)+0x10,Ptr(follow)+0x50));
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  CHECK(Float(0x50)==1.4f && Float(0x54)==1.1f);
  CHECK(std::memcmp(follow.data()+0x10,follow.data()+0x2d0,64)==0);
  CHECK(Float(0x40)==1 && std::fabs(Float(0x44)-3.6f)<.00001f && Float(0x48)==3);
  CameraPrepareNativeUpdate(follow.data());
  CHECK(std::memcmp(follow.data()+0x10,before_follow.data()+0x10,64)==0);
  CHECK(std::memcmp(follow.data()+0x2d0,before_follow.data()+0x2d0,64)==0);
  CHECK(Float(0x50)==.75f && Float(0x54)==16.f/9);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  Vec3 head,r,u,f,anchor; float reference[4];
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  CHECK(anchor.x==1 && anchor.y==2 && anchor.z==3);
  // Calibrate against the measured body, independent of the logic-yaw formula.
  const Vec3 native_right{.99883664f,0,-.04822187f};
  CHECK(VDot(r,native_right)>.999f && VDot(f,{.04822187f,0,.99883664f})>.999f);
  // The actual writer must preserve physical pitch/roll, including vertical
  // gaze, without inheriting native animation bob or roll. Quarter turns have
  // independent integer-axis expectations, not recomputed production math.
  const Vec3 native_forward{.04822187f,0,.99883664f},world_up{0,1,0};
  const float quarter_turn_component=std::sqrt(.5f);
  struct Tilt { float x,z; Vec3 right,up,forward; };
  const Tilt tilts[]={
      {quarter_turn_component,0,native_right,VScale(native_forward,-1),world_up},
      {-quarter_turn_component,0,native_right,native_forward,VScale(world_up,-1)},
      {0,quarter_turn_component,world_up,VScale(native_right,-1),native_forward},
      {0,-quarter_turn_component,VScale(world_up,-1),native_right,native_forward}};
  for(const auto& tilt:tilts) {
    const float native_bob[16]={0,1,0,0,-1,0,0,0,0,0,1,0,7,9,11,1};
    std::memcpy(follow.data()+0x10,native_bob,sizeof(native_bob));
    std::memcpy(follow.data()+0x2d0,native_bob,sizeof(native_bob));
    CHECK(CameraApplyFirstPerson(follow.data(),tilt.x,0,tilt.z,quarter_turn_component,
        0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
    Vec3 head_right{},head_up{},head_forward{};
    CHECK(CameraHeadBasis(head_right,head_up,head_forward));
    CHECK(VDot(head_right,tilt.right)>.99999f);
    CHECK(VDot(head_up,tilt.up)>.99999f);
    CHECK(VDot(head_forward,tilt.forward)>.99999f);
    CHECK(VDot(Vec3{Float(0x10),Float(0x14),Float(0x18)},tilt.right)>.99999f);
    CHECK(VDot(Vec3{Float(0x20),Float(0x24),Float(0x28)},tilt.up)>.99999f);
    CHECK(VDot(Vec3{Float(0x30),Float(0x34),Float(0x38)},tilt.forward)>.99999f);
    CHECK(std::memcmp(follow.data()+0x10,follow.data()+0x2d0,64)==0);
    CHECK(Float(0x40)==1 && std::fabs(Float(0x44)-3.6f)<1e-5f && Float(0x48)==3);

  }
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  // Home/theater and temporary tracking loss suspend the camera override.
  // Native animation may face another direction while suspended; resuming
  // must preserve both the intentional stick heading and physical origin.
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,.12f,1.35f,.04f,
      1.6f,1,0,.4f,1.4f,1.1f));
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  const auto turned_forward=f;
  const auto held_head_offset=VAdd(head,VScale(anchor,-1));
  CameraPublishHeadDirectedLocomotion(9);
  CHECK(locomotion_publishes == 1 && locomotion_epoch == 9);
  CHECK(VDot(locomotion_head,turned_forward) > .999f);
  // Movement basis contract (23 Oct): native is asked to walk in the heading of
  // the camera the mod actually wrote, never the rendered model's root facing.
  // Pre-rotating the stick into the model facing double-counted every right-
  // stick turn, which is the reported "walking the wrong way, worse after
  // turning the camera" failure. The model facing stays a read-only gate.
  CHECK(VDot(locomotion_native,locomotion_head) > .999f);
  CHECK(VDot(locomotion_native,{.04822187f,0,.99883664f}) < .99f);
  CHECK(CameraApplyFirstPerson(follow.data(),.70710678f,0,0,.70710678f,.12f,1.35f,.04f,
      1.6f,1,0,0,1.4f,1.1f));
  CameraPublishHeadDirectedLocomotion(9);
  CHECK(locomotion_publishes == 2 && VDot(locomotion_head,turned_forward) > .999f);
  const auto invalid_before_bad_native = locomotion_invalidations;
  Put(model,0x38,std::numeric_limits<float>::quiet_NaN());
  CameraPublishHeadDirectedLocomotion(9);
  CHECK(locomotion_invalidations == invalid_before_bad_native + 1);
  Put(model,0x38,root[2]);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,.12f,1.35f,.04f,
      1.6f,1,0,0,1.4f,1.1f));
  CameraPublishHeadDirectedLocomotion(9);
  CHECK(locomotion_publishes == 3);
  const auto locomotion_invalidations_before = locomotion_invalidations;

  const float suspended_root[12]={1,0,0,1,0,1,0,2,0,0,1,3};
  std::memcpy(model.data()+0x30,suspended_root,sizeof(suspended_root));
  CameraResetHeadlook();
  CHECK(locomotion_invalidations == locomotion_invalidations_before + 1);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,.12f,1.35f,.04f,
      1.6f,1,0,0,1.4f,1.1f));
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  CHECK(VDot(f,turned_forward)>.99999f);
  CHECK(VLen(VAdd(VAdd(head,VScale(anchor,-1)),VScale(held_head_offset,-1)))<1e-5f);
  CHECK(reference[0]==0 && reference[1]==0 && reference[2]==0 && reference[3]==1);
  for(int load=0;load<20;++load) {
    Put(position,0x10,10.f+load*10.f);
    std::memcpy(model.data()+0x30,load%2?root:suspended_root,sizeof(root));
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,.12f,1.35f,.04f,
        1.6f,1,0,0,1.4f,1.1f));
    CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
    CHECK(VDot(f,turned_forward)>.99999f);
    CHECK(VLen(VAdd(VAdd(head,VScale(anchor,-1)),VScale(held_head_offset,-1)))<2e-5f);
  }
  Put(position,0x10,1.f);
  std::memcpy(model.data()+0x30,root,sizeof(root));
  // Loading/respawning while the user has lowered their head must preserve
  // physical lean/height relative to the launch reference, rather than
  // silently treating the crouched pose as a new neutral body.
  CameraResetHeadlook();
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.35f,0,1.6f,1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.35f)<1e-5f);
  for(int death=0;death<20;++death){
    Put(position,0x10,10.f+death*10.f);
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.35f,0,1.6f,1,0,0,1.4f,1.1f));
    CHECK(std::fabs(Float(0x44)-3.35f)<1e-5f);
  }
  Put(position,0x10,1.f);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  // A refused teleport recapture cannot keep a previous successful arm frame.
  Put(position,0x10,10.0f); Put(model,0x38,std::numeric_limits<float>::quiet_NaN());
  CHECK(!CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  CHECK(!CameraTrackingFrame(head,r,u,f,reference));
  Put(position,0x10,1.0f); Put(model,0x38,root[2]);
  CameraResetHeadlook();
  CHECK(Float(0x50)==.75f && Float(0x54)==16.0f/9);
  CHECK(!CameraTrackingFrame(head,r,u,f,reference));
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,0,1.6f,0,1.6f,1,0,0,1.4f,1.1f));
  Put(follow,0x50,.9f); // newer native edit wins over our saved projection
  CameraResetHeadlook(); CHECK(Float(0x50)==.9f);
  // Recenter changes only the view origin. It must not edit saved body/wrist values.
  calibration.valid=true;calibration.eye_height_m=1.8f;calibration.revision=9;
  ++recenter_generation;
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.6f,0,CalibrationViewHeight(calibration,1.6f,false),1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.8f)<1e-5f && calibration.revision==9);
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  CHECK(VDot(f,{.04822187f,0,.99883664f})>.999f); // explicit recenter adopts native facing
  calibration.eye_height_m=1.04f; // actual seated floor-to-eye sample
  ++recenter_generation; // successful new calibration explicitly replaces the origin
  CameraResetHeadlook();
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.04f,0,
      CalibrationViewHeight(calibration,1.65f,true),1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.04f)<1e-5f && calibration.eye_height_m==1.04f);
  // The former seated default requires an explicit manual override now.
  // It changes placement only, never the measured profile or anatomy.
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.04f,0,
      CalibrationViewHeight(calibration,1.65f,true,true),1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.65f)<1e-5f && calibration.eye_height_m==1.04f);
  // A new XR space establishes a new origin once. Repeating its epoch does
  // not rebase a subsequent crouch, and no profile measurement changes.
  std::memcpy(model.data()+0x30,suspended_root,sizeof(suspended_root));
  CameraTrackingSpaceChanged(7);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.04f,0,1.65f,1,0,0,1.4f,1.1f));
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  CHECK(VDot(f,{0,0,-1})>.999f);
  CameraTrackingSpaceChanged(7);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,.84f,0,1.65f,1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.45f)<1e-5f && calibration.revision==9);
  std::memcpy(model.data()+0x30,root,sizeof(root));
  CameraTrackingSpaceChanged(8);
  CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,.84f,0,1.65f,1,0,0,1.4f,1.1f));
  CHECK(std::fabs(Float(0x44)-3.65f)<1e-5f && calibration.eye_height_m==1.04f);
  CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor));
  CHECK(VDot(f,{.04822187f,0,.99883664f})>.999f);
  // eye_clearance_m height band: physically kneeling must not push the real
  // eye inside the standing native model, and the knob at 0 restores exact
  // physical tracking. X/Z are never touched by the band.
  {
    const float raw_x = Float(0x40), raw_z = Float(0x48);
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,.09f,0,1.65f,1,0,0,1.4f,1.1f,0.45f));
    CHECK(std::fabs(Float(0x44)-3.20f)<1e-5f);        // floor: 3.65 - 0.45
    CHECK(Float(0x40)==raw_x && Float(0x48)==raw_z);  // horizontal tracking intact
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.59f,0,1.65f,1,0,0,1.4f,1.1f,0.45f));
    CHECK(std::fabs(Float(0x44)-4.10f)<1e-5f);        // ceiling: 3.65 + 0.45
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,.09f,0,1.65f,1,0,0,1.4f,1.1f,0.0f));
    CHECK(std::fabs(Float(0x44)-2.90f)<1e-5f);        // knob off = fully physical
    CHECK(std::fabs(Float(0x44)-2.90f)<1e-5f);        // knob off = fully physical
  }
  {
    // Scale/height/revisions travel as one applied-camera packet, all the way
    // to roomscale publication. Changing a profile cannot rewrite that packet.
    const auto mapping=ResolveCalibrationMapping(calibration,1.65f,true,1.f,.495f,42);
    CHECK(mapping.valid && mapping.fitted);
    CameraTrackingSpaceChanged(9);
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.04f,0,
        mapping.eye_height,mapping.world_scale,0,0,1.4f,1.1f,0,&mapping));
    CHECK(CameraApplyFirstPerson(follow.data(),0,0,0,1,1.2f,.94f,0,
        mapping.eye_height,mapping.world_scale,0,0,1.4f,1.1f,0,&mapping));
    CalibrationMapping applied;
    CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor,&applied));
    const Vec3 offset=VAdd(head,VScale(anchor,-1));
    CHECK(std::fabs(VDot(offset,r)-.2f*mapping.world_scale)<1e-5f);
    CHECK(std::fabs(offset.y-(mapping.eye_height-.1f*mapping.world_scale))<1e-5f);
    CHECK(applied.world_scale==mapping.world_scale && applied.model_revision==42);
    CameraPublishHeadDirectedLocomotion(9);
    CHECK(published_roomscale.world_scale==mapping.world_scale &&
          published_roomscale.calibration_revision==mapping.calibration_revision);
    ++calibration.revision;
    CHECK(CameraTrackingFrame(head,r,u,f,reference,&anchor,&applied));
    CHECK(applied.calibration_revision==mapping.calibration_revision);
    auto invalid=mapping;invalid.valid=false;
    CHECK(!CameraApplyFirstPerson(follow.data(),0,0,0,1,1,1.04f,0,
        mapping.eye_height,mapping.world_scale,0,0,1.4f,1.1f,0,&invalid));
    CHECK(!CameraTrackingFrame(head,r,u,f,reference,&anchor,&applied));
  }
  Matrix(0x10,0,4,-8); CameraTraceFollowCam(follow.data()); CHECK(CameraIsTitle());
  CHECK(!CameraGameplayReady());
  Sleep(270); CHECK(!CameraIsTitle()); // stale observation is unknown
  Matrix(0x10,0,4,-8); CameraTraceFollowCam(follow.data()); CHECK(CameraIsTitle());
  CameraTraceFollowCam(nullptr); CHECK(!CameraIsTitle());
  CHECK(!CameraGameplayReady());
  CHECK(container==before_container && container_accesses==0 && rejected_writes==0);
  CHECK(unscoped_reads==0);
  CHECK(!GuardedReadScope::Contains(Ptr(follow)+0x10,Ptr(follow)+0x50));
  std::puts("camera writes: no CameraMan container accesses, owned follow projection preserved");
}
