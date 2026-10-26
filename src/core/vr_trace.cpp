#include "vr_trace.h"

#include <windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

#include "../common/logging.h"
#include "../common/memory.h"
#include "../common/version.h"
#include "combat_native.h"

namespace dsvr {
namespace {

std::array<VrTraceEvent, 4096> g_events{};
std::mutex g_mutex;
std::size_t g_count = 0;
std::atomic<unsigned long> g_dropped{0};
std::atomic<unsigned long> g_contended{0};
unsigned long g_queue_peak = 0;  // guarded by g_mutex
std::atomic<unsigned long long> g_until_ms{0};
std::atomic<std::uint64_t> g_render_frames{0};
std::atomic<std::uint64_t> g_native_state_samples{0};
std::atomic<std::uint64_t> g_native_step_samples{0};
std::atomic<std::uint64_t> g_camera_consumptions{0};
FILE* g_file = nullptr;  // diagnostic thread only
unsigned g_capture = 0;
std::atomic<std::uint64_t> g_capture_id{0};
std::atomic<unsigned> g_recorders{0};
std::atomic<TraceCaptureState> g_state{TraceCaptureState::Idle};
std::uint64_t g_written = 0;  // writer thread only
bool g_io_error = false;
bool g_interrupted = false;
long long g_qpc_hz = 0;
unsigned long long g_health_until = 0;
std::uint64_t g_health_written = 0;
struct CombatTraceHealth {
  unsigned contacts = 0, unstable = 0, limited = 0;
  long long native_ticks = 0, observer_ticks = 0;
  long long native_max = 0, observer_max = 0;
};
CombatTraceHealth g_combat_health;  // writer thread only
std::atomic<int> g_hook_domain{-1}; // -1 unselected, 0 full, 1 combat; immutable

// A multi-record observation is admitted atomically or dropped in full.
// Producers never wait, format text, allocate, or perform file I/O.
void RecordBatch(const VrTraceEvent* events, std::size_t count);

using WaitFn = void(__fastcall*)(void*, unsigned);
using RenderFn = void(__fastcall*)(void*, int);
using CameraFn = void(__fastcall*)(void*);
// HGCameraEntity dispatch: pinned calls preserve RCX=self, EDX=command,
// R8=cursor address; all return paths zero-extend AL into EAX.
using CameraConsumeFn = unsigned(__fastcall*)(void*, unsigned, std::uintptr_t*);
// Pinned upload helper: RCX=buffer owner, RDX=native renderer, R8=source,
// R9D=count of 16-byte vectors. Observing its return does not prove Map success.
using ConstantUploadFn = void(__fastcall*)(void*, void*, const void*, int);
WaitFn g_wait = nullptr;
RenderFn g_render = nullptr;
CameraFn g_camera = nullptr;
CameraFn g_projection = nullptr;
CameraConsumeFn g_camera_consume = nullptr;
ConstantUploadFn g_constant_upload = nullptr;
std::atomic<std::uint64_t> g_constant_uploads{0};
#ifdef DSVR_ENABLE_LAB
// Pinned callers use the returned RAX mask; the body consumes no arguments.
// This is NOT a getter we may call: it also updates native input restrictions.
using InputBlockersFn = std::uint64_t(__fastcall*)();
InputBlockersFn g_input_blockers = nullptr;
// Pinned dispatchers consume RCX=self, XMM1=scalar float, R8=task context.
// Do not use the corpus's undefined4 second parameter as an integer ABI.
using StepFn = void(__fastcall*)(void*, float, void*);
StepFn g_stay_step = nullptr, g_game_step = nullptr, g_load_step = nullptr;
using ContactFn = std::uint64_t(__fastcall*)(void*);
ContactFn g_contact = nullptr;
using InteractionFn = std::uint64_t(__fastcall*)(void*, int, unsigned);
InteractionFn g_interaction = nullptr;
std::atomic<std::uint64_t> g_interaction_samples{0};
// Detail accounting is split three ways so a ceiling can never again hide the
// operator's own swing: every callback is counted, callbacks that positively do
// not involve the local player are excluded cheaply, and only the rest spend
// the 256-batch detail budget. All reset per capture in VrTraceBegin.
// Detail batches per capture. The counter keeps counting past the ceiling (a
// fetch_add is the only way to make the ceiling hard under re-entry), so the
// footer reports the clamped number of batches actually written.
constexpr unsigned kContactDetailBatches = 256;
std::atomic<unsigned> g_contact_samples{0};
std::atomic<unsigned long long> g_contact_total{0};
std::atomic<unsigned> g_contact_non_player{0};
std::atomic<unsigned> g_contact_self{0};
std::atomic<unsigned> g_contact_refused{0};
std::atomic<unsigned> g_contact_detail_limit{kContactDetailBatches};
std::atomic<std::uintptr_t> g_contact_player{0};
std::atomic<unsigned long long> g_contact_player_until{0};
#endif
thread_local std::uint64_t g_render_scope = 0;

void __fastcall WaitDetour(void* self, unsigned target) {
  if (!VrTraceActive()) { g_wait(self, target); return; }
  const auto capture = VrTraceCaptureId();
  const long long start = VrTraceClock();
  g_wait(self, target);  // preserve native pacing exactly
  VrTraceEvent event{};
  event.capture_id = capture;
  event.event = "native_wait";
  event.object = reinterpret_cast<std::uintptr_t>(self);
  event.frame = g_render_scope;
  event.duration_qpc = VrTraceClock() - start;
  event.flags = target;
  unsigned rate = 0;
  if (GuardedRead(event.object + 0x60, rate)) event.native_rate = rate;
  VrTraceRecord(event);
}

void __fastcall RenderDetour(void* self, int mode) {
  if (!VrTraceActive()) { g_render(self, mode); return; }
  const std::uint64_t previous = g_render_scope;
  g_render_scope = g_render_frames.fetch_add(1) + 1;
  VrTraceEvent event{};
  event.capture_id = VrTraceCaptureId();
  event.event = "render_enter";
  event.frame = g_render_scope;
  event.object = reinterpret_cast<std::uintptr_t>(self);
  event.flags = static_cast<unsigned>(mode);
  const long long start = VrTraceClock();
  VrTraceRecord(event);
  g_render(self, mode);
  event.event = "render_exit";
  event.duration_qpc = VrTraceClock() - start;
  VrTraceRecord(event);
  g_render_scope = previous;
}

void __fastcall CameraDetour(void* self) {
  if (!VrTraceActive()) { g_camera(self); return; }
  // Pinned body copies self+0x10 into command 0x3e for handle at +8.
  // Capture exactly this producer, without assuming which pass owns it.
  VrTraceEvent event{};
  event.capture_id = VrTraceCaptureId();
  event.event = "camera_command";
  event.object = reinterpret_cast<std::uintptr_t>(self);
  event.frame = g_render_scope;
  const bool handle_ok = GuardedRead(event.object + 8, event.flags);
  const bool matrix_ok = GuardedReadBytes(event.object + 0x10, event.values,
                                         sizeof(event.values));
  if (handle_ok && matrix_ok && event.flags) VrTraceRecord(event);
  g_camera(self);
}

void __fastcall ProjectionDetour(void* self) {
  if (VrTraceActive()) {
    VrTraceEvent e{};
    e.capture_id = VrTraceCaptureId();
    e.event = "camera_projection_command";
    e.object = reinterpret_cast<std::uintptr_t>(self);
    e.frame = g_render_scope;
    if (GuardedRead(e.object + 8, e.flags) && e.flags &&
        GuardedReadBytes(e.object + 0x50, e.values, 4 * sizeof(float)))
      VrTraceRecord(e);
  }
  g_projection(self);
}

void PointerLimbs(std::uintptr_t pointer, float* values) {
  for (unsigned i = 0; i < 4; ++i)
    values[i] = static_cast<float>((pointer >> (16 * i)) & 0xffff);
}

#ifdef DSVR_ENABLE_LAB
struct ContactActor {
  std::uintptr_t address=0, vtable=0;
  unsigned handle=0;
  int hp=0, stamina=0;
};

// Serialized nibble values: keep these stable; tools/vr_trace.py decodes them.
enum class ContactRead : unsigned {
  Valid, Address, Vtable, Type, Handle, Stats, Range, Record, Changed, NotAdmitted
};
ContactRead ReadContactActor(std::uintptr_t address, ContactActor& out) {
  // Type 1 is corroborated by native 3c4020/3c4140 dispatch and the
  // pinned PlayerIns vtable +0x10 getter. Never invoke that getter.
  constexpr unsigned char type_one[]{0xb8,1,0,0,0,0xc3};
  ContactActor actor{}; actor.address=address;
  std::uintptr_t getter=0;
  const auto base=ModuleBase(nullptr);
  if (!base || address<0x10000 || address>UINTPTR_MAX-0x3fc)
    return ContactRead::Address;
  if (!GuardedRead(address,actor.vtable) || actor.vtable<base ||
      actor.vtable-base>0x319b000-0x18) return ContactRead::Vtable;
  if (!GuardedRead(actor.vtable+0x10,getter) || getter!=base+0x339f30 ||
      !BytesMatch(getter,type_one,sizeof(type_one))) return ContactRead::Type;
  if (!GuardedRead(address+8,actor.handle)) return ContactRead::Handle;
  if (!GuardedRead(address+0x3e8,actor.hp) ||
      !GuardedRead(address+0x3f8,actor.stamina)) return ContactRead::Stats;
  if (actor.hp<0 || actor.hp>10000000 || actor.stamina<0 || actor.stamina>10000000)
    return ContactRead::Range;
  out=actor;
  return ContactRead::Valid;
}

// Pinned 140313470: RCX=action group, EDX=index, R8D=actor handle. Preserve
// every natural call and all return bits. No action is requested by this hook.
std::uint64_t __fastcall InteractionDetour(void* group, int index, unsigned actor) {
  if(!VrTraceActive()) return g_interaction(group,index,actor);
  VrTraceEvent e{};e.event="native_interaction_activation";
  e.capture_id=VrTraceCaptureId();e.seq=g_interaction_samples.fetch_add(1)+1;
  e.object=reinterpret_cast<std::uintptr_t>(group);
  const auto limbs=[](std::uint32_t value,float* out) {
    out[0]=static_cast<float>(value&0xffff);out[1]=static_cast<float>(value>>16);
  };
  limbs(static_cast<std::uint32_t>(index),e.values);limbs(actor,e.values+2);
  std::uintptr_t array=0,row=0,param=0;
  int count=0;unsigned handle=0,state=0;unsigned short range=0;
  const auto snapshot=[&](std::uintptr_t& a,int& n,std::uintptr_t& r,
                          std::uintptr_t& p,unsigned& h,unsigned& s) {
    return e.object>=0x10000 && e.object<=UINTPTR_MAX-0x24 && index>=0 &&
        GuardedRead(e.object+0x18,a) && a && GuardedRead(e.object+0x20,n) && index<n &&
        a<=UINTPTR_MAX-static_cast<std::uintptr_t>(index)*0xf0-0x48 &&
        (r=a+static_cast<std::uintptr_t>(index)*0xf0)!=0 &&
        GuardedRead(r+0x1c,h) && GuardedRead(r+0x20,s) && GuardedRead(r+0x40,p);
  };
  {
    GuardedReadScope permissions;
    if(snapshot(array,count,row,param,handle,state)) {
      e.flags|=1;limbs(handle,e.values+4);limbs(state,e.values+6);
      if(param && param<=UINTPTR_MAX-0xe && GuardedRead(param+0xc,range)) {
        e.flags|=8;e.values[14]=range; // raw ObjActParam validDist, not hand reach
      }
    }
  }
  const auto start=VrTraceClock();
  const auto result=g_interaction(group,index,actor);
  e.duration_qpc=VrTraceClock()-start;
  PointerLimbs(static_cast<std::uintptr_t>(result),e.values+10);
  {
    GuardedReadScope permissions;
    std::uintptr_t after_array=0,after_row=0,after_param=0;
    int after_count=0;unsigned after_handle=0,after_state=0;
    if(snapshot(after_array,after_count,after_row,after_param,after_handle,after_state)) {
      e.flags|=2;limbs(after_state,e.values+8);
      if((e.flags&1) && array==after_array && count==after_count && row==after_row &&
          param==after_param && handle==after_handle) e.flags|=4;
    }
  }
  VrTraceRecord(e); // generation/loss checks are the existing queue's responsibility
  return result;
}

std::uint64_t __fastcall ContactDetour(void* record) {
  if (!VrTraceActive()) return g_contact(record);
  const auto capture=VrTraceCaptureId();
  const auto total=g_contact_total.fetch_add(1)+1;
  const auto observe_start=VrTraceClock();
  // Cache page permissions, never actor/record contents. One scope covers the
  // pre-call snapshot and a fresh one covers the post-call snapshot. Repeating
  // cold permission queries per actor made contact bursts stall the game.
  std::optional<GuardedReadScope> permissions;
  permissions.emplace();
  const auto object=reinterpret_cast<std::uintptr_t>(record);
  std::uintptr_t actors[2]{};
  std::array<unsigned char, 0x3a> record_before{}, record_after{};
  // One bounded read retains the pair, handle and raw response bytes.
  // No permission scope crosses native dispatch.
  const bool snapshot_before=object>=0x10000 && object<=UINTPTR_MAX-0x16a &&
      GuardedReadBytes(object+0x130,record_before.data(),record_before.size());
  if (snapshot_before) std::memcpy(actors,record_before.data(),sizeof(actors));
  const bool pair_ok=snapshot_before || (object>=0x10000 && object<=UINTPTR_MAX-0x140 &&
      GuardedReadBytes(object+0x130,actors,sizeof(actors)));
  // The record's own motion fields, read while the record is still the freshly
  // remembered guarded scope, so they cost no extra permission query. The record
  // is the caller's stack object: FUN_1403c7520 initializes it at RSP+0x20 and
  // passes it to this function, and its own decompile places the actor pair at
  // +0x130/+0x138 (already proven at runtime by this hook), a normalized
  // direction whose w is forced to zero at +0xF0, and a (current-previous)*60
  // velocity at +0x150. Two consequences for how these are read. The direction
  // is zeroed or infinite when the native source delta was degenerate, so a
  // non-unit length is a question, not a refutation; each read still sets its own
  // bit and the unit-length test is reported rather than assumed. The velocity is
  // native world units per second under the game's own fixed 60 Hz assumption, so
  // the analyzer converts it with the applied world scale before comparing it
  // with our metres-per-second tip speed. These are pre-dispatch values: the
  // swing state that produced this contact. Motion is additive evidence; older
  // captures lack it entirely.
  float direction[4]{},velocity[4]{};
  unsigned motion_flags=0;
  if (object>=0x10000 && object<=UINTPTR_MAX-0x160) {
    if (GuardedReadBytes(object+0xF0,direction,sizeof(direction))) {
      motion_flags|=1u;
      bool finite=true;
      for (float value:direction) finite=finite&&std::isfinite(value);
      if (finite) {
        motion_flags|=4u;
        const float length=std::sqrt(direction[0]*direction[0]+
            direction[1]*direction[1]+direction[2]*direction[2]);
        if (length>0.95f&&length<1.05f) motion_flags|=8u;
      }
    }
    if (GuardedReadBytes(object+0x150,velocity,sizeof(velocity))) {
      motion_flags|=2u;
      bool finite=true;
      for (float value:velocity) finite=finite&&std::isfinite(value);
      if (finite) motion_flags|=16u;
    }
  }
  // Spend the detail budget on contacts the local player can be part of. A
  // blind per-capture count used to burn all 256 batches on NPC traffic, so
  // the operator's own swing arrived with no detail. Identity is refreshed at
  // 10 Hz and the pointer used is recorded, so staleness during a scene change
  // is visible rather than silent. Unknown identity admits everything:
  // exclusion needs positive knowledge, not absence of evidence.
  const auto now_ms=GetTickCount64();
  if (now_ms>=g_contact_player_until.load(std::memory_order_relaxed)) {
    std::uintptr_t fresh=0;
    if (!CombatNativeLocalPlayer(fresh)) fresh=0;
    g_contact_player.store(fresh,std::memory_order_relaxed);
    g_contact_player_until.store(now_ms+100,std::memory_order_relaxed);
  }
  const std::uintptr_t player=g_contact_player.load(std::memory_order_relaxed);
  const bool involves_player=player==0 ||
      (pair_ok && (actors[0]==player || actors[1]==player));
  if (!involves_player) {
    const auto excluded=g_contact_non_player.fetch_add(1);
    if (excluded<8) {  // bounded proof the filter ran, never silent loss
      VrTraceEvent x{};x.event="native_combat_excluded";
      x.capture_id=capture;x.seq=total;x.object=object;
      PointerLimbs(actors[0],x.values);PointerLimbs(actors[1],x.values+4);
      PointerLimbs(player,x.values+8);
      x.values[12]=static_cast<float>(total&0xffffu);
      x.values[13]=static_cast<float>((total>>16)&0xffffu);
      x.duration_qpc=VrTraceClock()-observe_start;
      VrTraceRecord(x);
    }
    permissions.reset();
    return g_contact(record);
  }
  // The pinned character dispatcher rejects local-player self contacts. Keep
  // their count explicit, preserve every natural call, and reserve detailed
  // actor snapshots for distinct actors. Recheck the live vtable: a cached
  // local-player address alone must not classify a reused allocation.
  std::uintptr_t self_vtable=0;
  if (pair_ok && player && actors[0]==player && actors[1]==player &&
      GuardedRead(player,self_vtable) && self_vtable==ModuleBase(nullptr)+0x13251f0) {
    const auto excluded=g_contact_self.fetch_add(1);
    if(excluded<8) {
      VrTraceEvent x{};x.event="native_combat_self_excluded";
      x.capture_id=capture;x.seq=total;x.object=object;
      PointerLimbs(player,x.values);
      x.duration_qpc=VrTraceClock()-observe_start;
      VrTraceRecord(x);
    }
    permissions.reset();
    return g_contact(record);
  }
  const auto sample=g_contact_samples.fetch_add(1);
  if (sample>=g_contact_detail_limit.load(std::memory_order_relaxed)) {
    const auto refused=g_contact_refused.fetch_add(1);
    if (refused<8) {  // the ceiling is reported, not merely reached once
      VrTraceEvent limit{};limit.event="native_combat_limit";
      limit.capture_id=capture;limit.seq=total;limit.object=object;
      limit.values[0]=static_cast<float>(total&0xffffu);
      limit.values[1]=static_cast<float>((total>>16)&0xffffu);
      limit.values[2]=static_cast<float>(sample);
      limit.values[3]=static_cast<float>(refused);
      limit.values[4]=static_cast<float>(
          g_contact_non_player.load(std::memory_order_relaxed));
      VrTraceRecord(limit);
    }
    permissions.reset();
    return g_contact(record);
  }
  VrTraceEvent e{};
  e.capture_id=capture;e.seq=sample+1;e.event="native_combat_contact";
  e.object=object;
  // 1405290C0 builds the attack payload, 1403C5620 copies it to event+44,
  // and 1403C7520 copies event+44 back to this record's base. These are
  // context-relative offsets, NOT serialized PARAM-row offsets. Keep signed
  // IDs as 16-bit limbs and read before native dispatch can alter the record.
  std::uint32_t context_fields[9]{}, response_kind_after=0;
  unsigned response_reads=snapshot_before?4u:0u;
  std::uint32_t event_handle=0;
  unsigned char context_flags[2]{};
  unsigned context_reads=0;
  if (object>=0x10000 && object<=UINTPTR_MAX-0xca) {
    if (GuardedReadBytes(object+0x34,context_fields,sizeof(context_fields))) {
      context_reads|=1u;
      response_reads|=1u;
    } else if (GuardedReadBytes(object+0x34,context_fields,6*sizeof(context_fields[0]))) {
      context_reads|=1u;
    }
    if (GuardedReadBytes(object+0xc8,context_flags,sizeof(context_flags)))
      context_reads|=2u;
  }
  // 1403C7520 copies the event's generated handle into record+140. The
  // record pointer itself is a reused stack address. A handle is an observed
  // identity token; pool ownership/generation still needs native validation.
  if (snapshot_before) {
    std::memcpy(&event_handle,record_before.data()+0x10,sizeof(event_handle));
    context_reads|=4u;
  } else if (object>=0x10000 && object<=UINTPTR_MAX-0x144 &&
      GuardedRead(object+0x140,event_handle)) context_reads|=4u;
  ContactActor before[2]{};
  bool admitted[2]{};
  ContactRead read_before[2]{ContactRead::Record,ContactRead::Record};
  ContactRead read_after[2]{ContactRead::Record,ContactRead::Record};
  if (pair_ok) {
    for (unsigned side=0;side<2;++side) {
      PointerLimbs(actors[side],e.values+side*4);
      read_before[side]=ReadContactActor(actors[side],before[side]);
      admitted[side]=read_before[side]==ContactRead::Valid;
      if (admitted[side]) {
        e.flags|=1u<<side;
        e.values[8+side*4]=static_cast<float>(before[side].hp);
        e.values[10+side*4]=static_cast<float>(before[side].stamina);
      }
    }
  }
  permissions.reset();  // native code may change page mappings or re-enter
  const auto start=VrTraceClock();
  const auto result=g_contact(record); // natural dispatch exactly once
  const auto native_end=VrTraceClock();
  permissions.emplace();  // revalidate mappings; no pre-call permission survives
  e.duration_qpc=native_end-start;
  // Preserve raw return bits separately from read-validity flags. Neither
  // return nor an HP delta alone certifies a physical hit or defence event.
  e.native_rate=static_cast<unsigned>(result);
  if (object>=0x10000 && object<=UINTPTR_MAX-0x58 &&
      GuardedRead(object+0x54,response_kind_after)) response_reads|=2u;
  std::uintptr_t current[2]{};
  const bool snapshot_after=object>=0x10000 && object<=UINTPTR_MAX-0x16a &&
      GuardedReadBytes(object+0x130,record_after.data(),record_after.size());
  if (snapshot_after) {
    std::memcpy(current,record_after.data(),sizeof(current));
    response_reads|=8u;
  }
  if (snapshot_after || (object>=0x10000 && object<=UINTPTR_MAX-0x140 &&
      GuardedReadBytes(object+0x130,current,sizeof(current)))) {
    for (unsigned side=0;side<2;++side) {
      ContactActor after{};
      if (!admitted[side]) { read_after[side]=ContactRead::NotAdmitted; continue; }
      if (current[side]!=actors[side]) { read_after[side]=ContactRead::Changed; continue; }
      read_after[side]=ReadContactActor(current[side],after);
      if (read_after[side]!=ContactRead::Valid) continue;
      if (after.vtable!=before[side].vtable || after.handle!=before[side].handle) {
        read_after[side]=ContactRead::Changed;
        continue;
      }
      e.flags|=1u<<(side+2);
      e.values[9+side*4]=static_cast<float>(after.hp);
      e.values[11+side*4]=static_cast<float>(after.stamina);
    }
  }
  // Preserve the identities already checked above so a later analyst can
  // distinguish actor reuse across callbacks. Float payloads carry 16-bit
  // limbs, never lossy 32/64-bit pointer, handle or return-value conversions.
  // Pair with native_combat_contact by (thread, seq, object); flags have the
  // same before/after validity bits. Neither record proves damage attribution.
  VrTraceEvent identity=e;
  identity.event="native_combat_identity";
  for (float& value:identity.values) value=0;
  for (unsigned side=0;side<2;++side) {
    if (!admitted[side]) continue;
    PointerLimbs(before[side].vtable,identity.values+side*4);
    identity.values[8+side*2]=static_cast<float>(before[side].handle&0xffff);
    identity.values[9+side*2]=static_cast<float>(before[side].handle>>16);
  }
  PointerLimbs(static_cast<std::uintptr_t>(result),identity.values+12);
  // The payload was captured above, before the native call; the record is
  // emitted after the identity record so the batch stays ordered by role.
  VrTraceEvent motion=e;
  motion.event="native_combat_motion";
  motion.flags=motion_flags;
  for (float& value:motion.values) value=0;
  if (motion_flags&4u) {
    for (unsigned i=0;i<4;++i) motion.values[i]=direction[i];
    motion.values[8]=std::sqrt(direction[0]*direction[0]+
        direction[1]*direction[1]+direction[2]*direction[2]);
  }
  if (motion_flags&16u) {
    for (unsigned i=0;i<4;++i) motion.values[4+i]=velocity[i];
    motion.values[9]=std::sqrt(velocity[0]*velocity[0]+
        velocity[1]*velocity[1]+velocity[2]*velocity[2]);
  }
  VrTraceEvent diagnostics=e;
  diagnostics.event="native_combat_diagnostics";
  diagnostics.flags=0;
  for (float& value:diagnostics.values) value=0;
  // Versioned raw fields, never a defense classification. Legacy version 0
  // remains unknown; successful reads of zero stay distinct from failed reads.
  diagnostics.values[15]=1;
  diagnostics.values[0]=static_cast<float>(response_reads);
  if (response_reads&1u) {
    diagnostics.values[1]=static_cast<float>(context_fields[8]&0xffffu);
    diagnostics.values[2]=static_cast<float>(context_fields[8]>>16);
  }
  if (response_reads&2u) {
    diagnostics.values[3]=static_cast<float>(response_kind_after&0xffffu);
    diagnostics.values[4]=static_cast<float>(response_kind_after>>16);
  }
  if (response_reads&4u) {
    diagnostics.values[5]=record_before[0x38];
    diagnostics.values[6]=record_before[0x39];
  }
  if (response_reads&8u) {
    diagnostics.values[7]=record_after[0x38];
    diagnostics.values[8]=record_after[0x39];
  }
  for(unsigned side=0;side<2;++side) {
    diagnostics.flags|=static_cast<unsigned>(read_before[side])<<(side*4);
    diagnostics.flags|=static_cast<unsigned>(read_after[side])<<(8+side*4);
  }
  // Read/assembly CPU cost only: excludes native execution and queue admission.
  // It is not a total-hook or game/VR performance measurement.
  VrTraceEvent context=e;
  context.event="native_combat_context";
  context.flags=context_reads;
  for (float& value:context.values) value=0;
  if (context_reads&1u) for (unsigned i=0;i<6;++i) {
    context.values[i*2]=static_cast<float>(context_fields[i]&0xffffu);
    context.values[i*2+1]=static_cast<float>(context_fields[i]>>16);
  }
  if (context_reads&2u) {
    context.values[12]=context_flags[0];context.values[13]=context_flags[1];
  }
  if (context_reads&4u) {
    context.values[14]=static_cast<float>(event_handle&0xffffu);
    context.values[15]=static_cast<float>(event_handle>>16);
  }
  diagnostics.duration_qpc=(start-observe_start)+(VrTraceClock()-native_end);
  const VrTraceEvent batch[]{e,identity,motion,diagnostics,context};
  RecordBatch(batch,std::size(batch));
  return result;
}
#endif

void __fastcall ConstantUploadDetour(void* owner, void* renderer,
                                    const void* source, int vectors) {
  if (!VrTraceActive()) { g_constant_upload(owner, renderer, source, vectors); return; }
  VrTraceEvent e{};
  e.capture_id = VrTraceCaptureId();
  e.seq = g_constant_uploads.fetch_add(1) + 1;
  e.frame = g_render_scope;
  e.object = reinterpret_cast<std::uintptr_t>(owner);
  e.event = "constant_upload_enter";
  const auto address = reinterpret_cast<std::uintptr_t>(source);
  PointerLimbs(address, e.values);
  std::uintptr_t context = 0, buffer = 0;
  const auto renderer_address = reinterpret_cast<std::uintptr_t>(renderer);
  if (renderer_address <= UINTPTR_MAX - 0x20 &&
      GuardedRead(renderer_address + 0x20, context)) e.flags |= 1;
  if (e.object <= UINTPTR_MAX - 0x28 &&
      GuardedRead(e.object + 0x28, buffer)) e.flags |= 2;
  PointerLimbs(context, e.values + 4);
  PointerLimbs(buffer, e.values + 8);
  const auto count = static_cast<unsigned>(vectors);
  e.values[12] = static_cast<float>(count & 0xffff);
  e.values[13] = static_cast<float>(count >> 16);
  VrTraceRecord(e);
  // Only the first three complete 64-byte blocks, BEFORE the native upload.
  // These may be matrices, but neither layout nor main-scene ownership is assumed.
  const char* names[]{"constant_upload_block0", "constant_upload_block1",
                       "constant_upload_block2"};
  for (unsigned i = 0; i < 3 && vectors >= static_cast<int>((i + 1) * 4); ++i) {
    VrTraceEvent block = e;
    block.event = names[i];
    block.flags = 0;
    for (auto& value : block.values) value = 0;
    const auto offset = static_cast<std::uintptr_t>(i * 64);
    if (address <= UINTPTR_MAX - offset &&
        GuardedReadBytes(address + offset, block.values, sizeof(block.values))) block.flags = 1;
    VrTraceRecord(block);
  }
  const auto start = VrTraceClock();
  g_constant_upload(owner, renderer, source, vectors);  // exactly once, unchanged ABI
  if (e.capture_id != VrTraceCaptureId() || !VrTraceActive()) return;
  e.event = "constant_upload_return";
  e.duration_qpc = VrTraceClock() - start;
  VrTraceRecord(e);  // CPU return only; no HRESULT, GPU completion or draw claim
}

unsigned __fastcall CameraConsumeDetour(void* self, unsigned command,
                                        std::uintptr_t* cursor) {
  if (!VrTraceActive() || (command != 0x3b && command != 0x3e))
    return g_camera_consume(self, command, cursor);
  VrTraceEvent e{};
  e.capture_id = VrTraceCaptureId();
  e.seq = g_camera_consumptions.fetch_add(1) + 1;  // native sample association
  e.frame = g_render_scope;  // thread-local dispatcher only; zero on unbound workers
  e.object = reinterpret_cast<std::uintptr_t>(self);
  std::uintptr_t before = 0;
  const bool before_ok = GuardedRead(reinterpret_cast<std::uintptr_t>(cursor), before);
  const auto alignment = command == 0x3e ? 16u : 4u;
  e.event = command == 0x3e ? "camera_payload_3e" : "camera_payload_3b";
  if (before_ok && before <= UINTPTR_MAX - (alignment - 1)) {
    const auto payload = (before + alignment - 1) & ~std::uintptr_t(alignment - 1);
    if (GuardedReadBytes(payload, e.values, command == 0x3e ? 64 : 16)) e.flags = 1;
  }
  VrTraceRecord(e);
  const auto start = VrTraceClock();
  const unsigned result = g_camera_consume(self, command, cursor);  // exactly once
  if (e.capture_id != VrTraceCaptureId() || !VrTraceActive()) return result;
  e.duration_qpc = VrTraceClock() - start;
  e.event = command == 0x3e ? "camera_consume_3e" : "camera_consume_3b";
  for (auto& value : e.values) value = 0;
  e.flags = before_ok ? 1 : 0;
  PointerLimbs(before, e.values);
  std::uintptr_t after = 0, first_word = 0;
  unsigned handle = 0;
  if (GuardedRead(reinterpret_cast<std::uintptr_t>(cursor), after)) {
    e.flags |= 2;
    PointerLimbs(after, e.values + 4);
  }
  if (GuardedRead(e.object + 0x10, handle)) {
    e.flags |= 4;
    e.values[8] = static_cast<float>(handle & 0xffff);
    e.values[9] = static_cast<float>(handle >> 16);
  }
  if (GuardedRead(e.object, first_word)) {
    e.flags |= 8;
    PointerLimbs(first_word, e.values + 10);
  }
  e.values[14] = static_cast<float>(result);  // pinned dispatcher returns 0 or 1
  VrTraceRecord(e);
  // These are CPU entity values AFTER natural consumption, not final GPU
  // constants or a certified main-scene view. No scene/space epoch is invented.
  const auto matrix = [&](const char* name, std::uintptr_t offset) {
    e.event = name;
    e.flags = 0;
    e.duration_qpc = 0;
    for (auto& value : e.values) value = 0;
    if (GuardedReadBytes(e.object + offset, e.values, sizeof(e.values))) e.flags = 1;
    VrTraceRecord(e);
  };
  matrix("camera_entity_matrix20", 0x20);
  matrix("camera_entity_matrix60", 0x60);
  matrix("camera_entity_matrixa0", 0xa0);
  return result;
}

#ifdef DSVR_ENABLE_LAB
std::uint64_t __fastcall InputBlockersDetour() {
  const auto mask = g_input_blockers();  // preserve native side effects and return
  if (VrTraceActive()) {
    std::uintptr_t owner = 0;
    if (!GuardedRead(ModuleBase(nullptr) + 0x1c88d98, owner)) owner = 0;
    VrTraceRecordInputBlockers(mask, owner);
  }
  return mask;
}

void ObserveStep(StepFn original, unsigned kind, void* self, float delta, void* task) {
  if (!VrTraceActive()) { original(self, delta, task); return; }
  VrTraceStepSample sample{};
  sample.capture_id = VrTraceCaptureId();
  sample.kind = kind;
  sample.owner = reinterpret_cast<std::uintptr_t>(self);
  sample.delta = delta;
  if (GuardedRead(sample.owner + 0x10, sample.before)) sample.state_valid |= 1;
  const auto start = VrTraceClock();
  original(self, delta, task);  // natural native call, exactly once, no new getter
  sample.duration_qpc = VrTraceClock() - start;
  if (GuardedRead(sample.owner + 0x10, sample.after)) sample.state_valid |= 2;
  VrTraceRecordStep(sample);
}

void __fastcall StayStepDetour(void* self, float delta, void* task) {
  ObserveStep(g_stay_step, 1, self, delta, task);
}
void __fastcall GameStepDetour(void* self, float delta, void* task) {
  ObserveStep(g_game_step, 2, self, delta, task);
}
void __fastcall LoadStepDetour(void* self, float delta, void* task) {
  ObserveStep(g_load_step, 3, self, delta, task);
}
#endif

bool Hook(std::uintptr_t rva, const unsigned char* bytes, std::size_t size,
          void* detour, void** original) {
  const std::uintptr_t address = ModuleBase(nullptr) + rva;
  if (!BytesMatch(address, bytes, size)) {
    DSVR_WARN("VrTrace: signature mismatch RVA=0x%llx; observation disabled",
              static_cast<unsigned long long>(rva));
    return false;
  }
  void* target = reinterpret_cast<void*>(address);
  if (MH_CreateHook(target, detour, original) != MH_OK) return false;
  if (MH_EnableHook(target) != MH_OK) {
    MH_RemoveHook(target);
    *original = nullptr;
    return false;
  }
  return true;
}

void RecordBatch(const VrTraceEvent* events, std::size_t count) {
  if(g_hook_domain.load(std::memory_order_relaxed)==1 &&
     std::strncmp(events[0].event,"native_combat_",14)!=0 &&
     std::strcmp(events[0].event,"native_interaction_activation")!=0 &&
     std::strcmp(events[0].event,"native_step_dispatch")!=0) return;
  const auto capture = events[0].capture_id ? events[0].capture_id : g_capture_id.load();
  if (!VrTraceActive()) return;
  // Closing can defer its acknowledgement until admitted producers finish.
  // This never waits on a hook thread, including on queue contention.
  struct Recorder {
    Recorder() { g_recorders.fetch_add(1); }
    ~Recorder() { g_recorders.fetch_sub(1); }
  } recorder;
  const auto until = g_until_ms.load(std::memory_order_relaxed);
  if (!until || GetTickCount64() >= until ||
      g_state.load() != TraceCaptureState::Recording ||
      g_capture_id.load() != capture) return;
  const auto qpc = VrTraceClock();
  const auto thread = GetCurrentThreadId();
  std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    g_dropped.fetch_add(static_cast<unsigned long>(count), std::memory_order_relaxed);
    g_contended.fetch_add(static_cast<unsigned long>(count), std::memory_order_relaxed);
    return;
  }
  if (g_state.load() != TraceCaptureState::Recording ||
      g_capture_id.load() != capture) return;
  if (count > g_events.size()-g_count) {
    g_dropped.fetch_add(static_cast<unsigned long>(count), std::memory_order_relaxed);
    return;
  }
  for(std::size_t i=0;i<count;++i) {
    auto event=events[i];
    event.qpc=qpc;
    event.thread=thread;
    g_events[g_count++]=event;
  }
  if(g_count>g_queue_peak) g_queue_peak=static_cast<unsigned long>(g_count);
}

}  // namespace

long long VrTraceClock() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

void VrTraceRecord(VrTraceEvent event) {
  RecordBatch(&event,1);
}

bool VrTraceActive() {
  const auto until = g_until_ms.load(std::memory_order_relaxed);
  return g_state.load() == TraceCaptureState::Recording &&
         until && GetTickCount64() < until;
}

void VrTraceRecordInputBlockers(std::uint64_t mask, std::uintptr_t owner) {
  if (!VrTraceActive()) return;
  VrTraceEvent event{};
  event.event = "native_input_blockers";
  event.seq = g_native_state_samples.fetch_add(1) + 1;
  event.object = owner;
  event.flags = owner ? 1 : 0;  // observed pointer only, not verified readiness
  for (unsigned i = 0; i < 4; ++i)
    event.values[i] = static_cast<float>((mask >> (i * 16)) & 0xffff);
  VrTraceRecord(event);
}

void VrTraceRecordStep(const VrTraceStepSample& sample) {
  if (!VrTraceActive()) return;
  VrTraceEvent event{};
  event.event = "native_step_dispatch";
  event.seq = g_native_step_samples.fetch_add(1) + 1;
  event.object = sample.owner;
  event.flags = sample.kind;
  event.duration_qpc = sample.duration_qpc;
  event.capture_id = sample.capture_id;
  // Native signed state codes can exceed float's exact integer range.
  const auto before = static_cast<std::uint32_t>(sample.before);
  const auto after = static_cast<std::uint32_t>(sample.after);
  event.values[0] = static_cast<float>(before & 0xffff);
  event.values[1] = static_cast<float>(before >> 16);
  event.values[2] = static_cast<float>(after & 0xffff);
  event.values[3] = static_cast<float>(after >> 16);
  event.values[4] = sample.delta;
  event.values[5] = static_cast<float>(sample.state_valid);
  VrTraceRecord(event);
}

TraceCaptureState VrTraceState() { return g_state.load(); }
std::uint64_t VrTraceCaptureId() { return g_capture_id.load(); }

void VrTraceRequestStop() {
  auto expected = TraceCaptureState::Recording;
  if (g_state.compare_exchange_strong(expected, TraceCaptureState::Closing))
    g_until_ms.store(0);
}

bool VrTraceBegin(const wchar_t* explicit_path, unsigned duration_ms) {
  if (g_file) {
    DSVR_LOG("VrTrace: capture already active; marker retained in ordinary log");
    return false;
  }
  if (!duration_ms || duration_ms > 600000) {
    DSVR_WARN("VrTrace: duration must be 1..600000 ms");
    return false;
  }
  const std::wstring name = L"DSVR-trace-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(GetTickCount64()) + L"-" +
      std::to_wstring(++g_capture) + L".jsonl";
  const std::wstring path = explicit_path ? explicit_path :
      LogPathNextToModule(nullptr, name.c_str());
  if (_wfopen_s(&g_file, path.c_str(), L"wx") != 0 || !g_file) {
    g_file = nullptr;
    g_state.store(TraceCaptureState::IoError);
    DSVR_WARN("VrTrace: could not create capture; no capture started");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_count = 0;
    g_dropped.store(0);
    g_contended.store(0);
    g_queue_peak=0;
  }
  g_written = 0;
  g_io_error = false;
  g_interrupted = false;
  g_combat_health={};
  g_health_written=0;
  g_health_until=GetTickCount64()+5000;
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  g_qpc_hz=frequency.QuadPart;
#ifdef DSVR_ENABLE_LAB
  const unsigned combat_schema=1,combat_hook=g_contact!=nullptr;
#else
  const unsigned combat_schema=0,combat_hook=0;
#endif
  const int header = std::fprintf(g_file,
      "{\"event\":\"header\",\"qpc\":%lld,\"schema\":2,"
      "\"pid\":%lu,\"qpc_hz\":%lld,\"duration_ms\":%u,\"mod_version\":\"%s\","
      "\"combat_detail_schema\":%u,\"combat_context_schema\":%u,"
      "\"combat_hook\":%u,\"capture_domain\":\"%s\"}\n",
      VrTraceClock(), GetCurrentProcessId(), frequency.QuadPart, duration_ms,
      kModVersion,combat_schema,combat_schema,combat_hook,
      g_hook_domain.load()==1 ? "combat" : "full");
  if (header < 0 || std::fflush(g_file) != 0) {
    std::fclose(g_file);
    g_file = nullptr;
    g_state.store(TraceCaptureState::IoError);
    return false;
  }
  g_capture_id.fetch_add(1);
#ifdef DSVR_ENABLE_LAB
  g_contact_samples.store(0);
  g_contact_total.store(0);
  g_contact_non_player.store(0);
  g_contact_self.store(0);
  g_contact_refused.store(0);
  g_contact_detail_limit.store(duration_ms>20000 ? ~0u : kContactDetailBatches);
  g_contact_player.store(0);
  g_contact_player_until.store(0);
#endif
  g_until_ms.store(GetTickCount64() + duration_ms);
  g_state.store(TraceCaptureState::Recording);
  DSVR_LOG("VrTrace: bounded %u-ms observation started: %ls", duration_ms, path.c_str());
  return true;
}

void VrTracePump() {
  if (!g_file) return;
  // Copy under lock, then perform all formatting/file writes off game hooks.
  static std::array<VrTraceEvent, 4096> batch;
  std::size_t count = 0;
  unsigned long queue_peak=0;
  const bool finished = g_state.load() == TraceCaptureState::Closing ||
                        GetTickCount64() >= g_until_ms.load();
  if (finished) {
    VrTraceRequestStop();
    if (g_recorders.load() != 0) return;  // defer close, never wait on hooks
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (finished) g_until_ms.store(0);
    count = g_count;
    for (std::size_t i = 0; i < count; ++i) batch[i] = g_events[i];
    g_count = 0;
    queue_peak=g_queue_peak;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const auto& e = batch[i];
    if(std::strcmp(e.event,"native_combat_contact")==0) {
      ++g_combat_health.contacts;
      if(e.flags!=15) ++g_combat_health.unstable;
      g_combat_health.native_ticks+=e.duration_qpc;
      if(e.duration_qpc>g_combat_health.native_max) g_combat_health.native_max=e.duration_qpc;
    } else if(std::strcmp(e.event,"native_combat_diagnostics")==0) {
      g_combat_health.observer_ticks+=e.duration_qpc;
      if(e.duration_qpc>g_combat_health.observer_max) g_combat_health.observer_max=e.duration_qpc;
    } else if(std::strcmp(e.event,"native_combat_limit")==0) ++g_combat_health.limited;
    std::fprintf(g_file, "{\"event\":\"%s\",\"qpc\":%lld,\"thread\":%lu,"
                 "\"seq\":%llu,\"epoch\":%llu,\"frame\":%llu,"
                 "\"object\":\"%llx\",\"predicted_ns\":%lld,"
                 "\"period_ns\":%lld,\"duration_qpc\":%lld,\"flags\":%u,"
                 "\"native_rate\":%u,\"values\":[",
                 e.event, e.qpc, e.thread,
                 static_cast<unsigned long long>(e.seq),
                 static_cast<unsigned long long>(e.epoch),
                 static_cast<unsigned long long>(e.frame),
                 static_cast<unsigned long long>(e.object),
                 e.predicted_ns, e.period_ns, e.duration_qpc, e.flags, e.native_rate);
    for (unsigned j = 0; j < 16; ++j) {
      if (j) std::fputc(',', g_file);
      if (std::isfinite(e.values[j])) std::fprintf(g_file, "%.9g", e.values[j]);
      else std::fputs("null", g_file);
    }
    std::fputs("]}\n", g_file);
    ++g_written;
  }
  if (std::ferror(g_file) || std::fflush(g_file) != 0) g_io_error = true;
  // The combat counters only exist in an opt-in LAB build; a non-LAB capture
  // still reports the fields so the footer schema never changes shape.
#ifdef DSVR_ENABLE_LAB
  const unsigned long long combat_total = g_contact_total.load();
  const unsigned combat_non_player = g_contact_non_player.load();
  const unsigned combat_self = g_contact_self.load();
  const unsigned combat_refused = g_contact_refused.load();
  const unsigned combat_detail_limit = g_contact_detail_limit.load();
  const unsigned written_detail = g_contact_samples.load();
  const unsigned combat_detail =
      written_detail < combat_detail_limit ? written_detail : combat_detail_limit;
#else
  const unsigned long long combat_total = 0;
  const unsigned combat_non_player = 0, combat_self = 0, combat_refused = 0, combat_detail = 0;
  const unsigned combat_detail_limit = 0;
#endif
  const auto now=GetTickCount64();
  if(finished || (now>=g_health_until && g_written!=g_health_written)) {
    DSVR_LOG("TraceHealth: capture=%llu events=%llu queue_peak=%lu/4096 dropped=%lu "
             "contended=%lu combat_total=%llu combat_non_player=%u combat_self=%u combat_refused=%u "
             "combat_samples=%u invalid_or_changed=%u detail_limit=%u "
             "native_max_us=%.3f observer_read_max_us=%.3f io_error=%d; "
             "natural observations only, combat binding UNVERIFIED",
             static_cast<unsigned long long>(g_capture_id.load()),
             static_cast<unsigned long long>(g_written),queue_peak,g_dropped.load(),
             g_contended.load(),
             combat_total,combat_non_player,combat_self,combat_refused,
             g_combat_health.contacts,g_combat_health.unstable,
             g_combat_health.limited,
             g_qpc_hz>0 ? 1e6*double(g_combat_health.native_max)/g_qpc_hz : 0,
             g_qpc_hz>0 ? 1e6*double(g_combat_health.observer_max)/g_qpc_hz : 0,g_io_error);
    g_health_until=now+5000;
    g_health_written=g_written;
  }
  if (g_io_error && !finished) {
    VrTraceRequestStop();
    return;  // next pump drains producers before acknowledging failure
  }
  if (finished) {
    const int footer = std::fprintf(g_file,
        "{\"event\":\"footer\",\"qpc\":%lld,\"dropped\":%lu,\"events\":%llu,"
        "\"interrupted\":%s,\"io_error\":%s,\"queue_peak\":%lu,\"contended\":%lu,"
        "\"combat_total\":%llu,\"combat_non_player\":%u,\"combat_self\":%u,\"combat_refused\":%u,"
        "\"combat_detail\":%u,\"combat_detail_limit\":%u}\n", VrTraceClock(), g_dropped.load(),
        static_cast<unsigned long long>(g_written),
        g_interrupted ? "true" : "false", g_io_error ? "true" : "false",
        queue_peak,g_contended.load(),
        combat_total,combat_non_player,combat_self,combat_refused,combat_detail,combat_detail_limit);
    if (footer < 0 || std::fflush(g_file) != 0) g_io_error = true;
    if (std::fclose(g_file) != 0) g_io_error = true;
    g_file = nullptr;
    g_state.store(g_io_error ? TraceCaptureState::IoError :
                  g_interrupted ? TraceCaptureState::Interrupted :
                  g_dropped.load() ? TraceCaptureState::Dropped :
                                    TraceCaptureState::Complete);
    DSVR_LOG("VrTrace: closed events=%llu dropped=%lu interrupted=%d io_error=%d "
             "(no bone/limiter writes)", static_cast<unsigned long long>(g_written),
             g_dropped.load(), g_interrupted, g_io_error);
  }
}

void VrTraceFinish(bool interrupted) {
  if (!g_file) return;
  g_interrupted = g_interrupted || interrupted;
  VrTraceRequestStop();
  VrTracePump();
}

static bool InstallTrace(bool enabled,bool combat_only) {
  if (!enabled) return false;
  int expected=-1;
  const int domain=combat_only ? 1 : 0;
  if(!g_hook_domain.compare_exchange_strong(expected,domain) && expected!=domain) {
    DSVR_WARN("VrTrace: capture domain change refused; restart required");
    return false;
  }
  const MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
  // Verified against pinned disk image on 2026-10-14; corpus provenance in
  // MODDING_PLAN.md. These are observations, NOT patch shortcuts.
  const unsigned char wait[]{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,
                              0x8b,0x41,0x60,0x8b,0xf2,0x48,0x8b,0xf9};
  const unsigned char render[]{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,
                                0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe1};
  const unsigned char camera[]{0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x60,
                                0x8b,0x69,0x08,0x48,0x8b,0xf1};
  const unsigned char projection[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
      0xec,0x30,0x8b,0x79,0x08,0x48,0x8b,0xd9,0x85,0xff};
  const unsigned char consume[]{0x40,0x53,0x48,0x83,0xec,0x20,0x8d,0x42,0xc6,
       0xb3,0x01,0x83,0xf8,0x07,0x0f,0x87,0x9d,0x00,0x00,0x00};
  const unsigned char upload[]{0x45,0x85,0xc9,0x0f,0x84,0x8e,0x00,0x00,0x00,
      0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
  const bool w = !combat_only && Hook(0xce3450, wait, sizeof(wait), reinterpret_cast<void*>(&WaitDetour),
                      reinterpret_cast<void**>(&g_wait));
  const bool r = !combat_only && Hook(0x11be710, render, sizeof(render), reinterpret_cast<void*>(&RenderDetour),
                      reinterpret_cast<void**>(&g_render));
  const bool c = !combat_only && Hook(0x245690, camera, sizeof(camera), reinterpret_cast<void*>(&CameraDetour),
                        reinterpret_cast<void**>(&g_camera));
  const bool p = !combat_only && Hook(0x2472d0, projection, sizeof(projection),
      reinterpret_cast<void*>(&ProjectionDetour), reinterpret_cast<void**>(&g_projection));
  std::uintptr_t consume_slot = 0;
  const bool consumer = !combat_only && GuardedRead(ModuleBase(nullptr) + 0x1609070 + 0x28, consume_slot) &&
      consume_slot == ModuleBase(nullptr) + 0x7b870 &&
      Hook(0x7b870, consume, sizeof(consume), reinterpret_cast<void*>(&CameraConsumeDetour),
            reinterpret_cast<void**>(&g_camera_consume));
  const bool u = !combat_only && Hook(0xcd0a10, upload, sizeof(upload),
      reinterpret_cast<void*>(&ConstantUploadDetour),
      reinterpret_cast<void**>(&g_constant_upload));
  bool input_blockers = false;
  unsigned native_steps = 0;
  bool native_contacts = false, native_interactions = false;
#ifdef DSVR_ENABLE_LAB
  // Raw pinned 3c4020 entry and its one-record-pointer calling convention
  // are corroborated by 3c7520. Observe natural calls; never fabricate a
  // record or dispatch damage. Regular builds do not install this hook.
  const unsigned char contact[]{0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,
      0xec,0x20,0x48,0x8b,0xb1,0x30,0x01,0,0,0x48,0x8b,0xd9,0x48,0x8b,0xce};
  native_contacts = g_contact || Hook(0x3c4020,contact,sizeof(contact),
      reinterpret_cast<void*>(&ContactDetour),reinterpret_cast<void**>(&g_contact));
  // The full 287-byte function and caller are pinned in the interaction
  // evidence note. The first instructions preserve all three input arguments.
  const unsigned char interaction[]{0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x50,
      0x48,0x63,0xf2,0x41,0x8b,0xe8,0x48,0x8b,0xf9,0x85,0xd2};
  native_interactions = g_interaction || Hook(0x313470,interaction,sizeof(interaction),
      reinterpret_cast<void*>(&InteractionDetour),reinterpret_cast<void**>(&g_interaction));
  // Corpus batch-000694.jsonl and pinned disk: no-argument 64-bit return;
  // prologue plus mov rax,rdi/ret corroborated. Observe only natural calls.
  const unsigned char blockers[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
      0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x33,0xdb};
  const unsigned char blockers_return[]{0x48,0x8b,0xc7,0x48,0x8b,0x6c,0x24,0x38,
      0x48,0x8b,0x74,0x24,0x40,0x48,0x83,0xc4,0x20,0x5f,0xc3};
  if (!combat_only && BytesMatch(ModuleBase(nullptr) + 0x71afd0, blockers_return,
                 sizeof(blockers_return))) {
    input_blockers = Hook(0x71ac10, blockers, sizeof(blockers),
        reinterpret_cast<void*>(&InputBlockersDetour),
        reinterpret_cast<void**>(&g_input_blockers));
  } else if(!combat_only) DSVR_WARN("VrTrace: input-blocker return ABI signature mismatch; observer disabled");
  // RTTI corroborates each class; slot +0x10 dispatches its state table at +0x20.
  // ABI bytes preserve XMM1 and R8, not the decompiler's guessed signature.
  const unsigned char step[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
      0x57,0x48,0x83,0xec,0x30,0x8b,0x41,0x10,0x49,0x8b,0xf0,0x0f,0x29,0x74,
      0x24,0x20,0x48,0x8b,0xd9,0x0f,0x28,0xf1};
  const auto hook_step = [&](std::uintptr_t rva, std::uintptr_t table_rva,
                             void* detour, StepFn& original) {
    std::uintptr_t slot = 0;
    const auto base = ModuleBase(nullptr);
    if (!GuardedRead(base + table_rva + 0x10, slot) || slot != base + rva) {
      DSVR_WARN("VrTrace: native Step vtable mismatch; observer disabled");
      return false;
    }
    return Hook(rva, step, sizeof(step), detour, reinterpret_cast<void**>(&original));
  };
  if (hook_step(0x279ee0, 0x12fe578, reinterpret_cast<void*>(&StayStepDetour),
                g_stay_step)) native_steps |= 1;
  if (hook_step(0x27bd30, 0x12fe970, reinterpret_cast<void*>(&GameStepDetour),
                g_game_step)) native_steps |= 2;
  if (hook_step(0x281020, 0x12ff8b8, reinterpret_cast<void*>(&LoadStepDetour),
                g_load_step)) native_steps |= 4;
#endif
  DSVR_LOG("VrTrace: observation hooks wait=%d render=%d camera-command=%d; "
               "native behavior unchanged; F9 captures 20 seconds", w, r, c);
  DSVR_LOG("VrTrace: perspective producer=%d HGCameraEntity consumer=%d "
            "(CPU command acceptance only, NOT final GPU/main-view proof)", p, consumer);
  DSVR_LOG("VrTrace: constant upload observer=%d (first 192 bytes maximum; "
            "CPU source/return only, NOT Map success or final draw proof)", u);
  DSVR_LOG("VrTrace: lab input-blocker observer=%d (opt-in build only; "
            "raw masks never grant gameplay/safe-scene readiness)", input_blockers);
  DSVR_LOG("VrTrace: lab native Step observers=%u (stay=1 game=2 load=4; "
           "raw states never grant scene identity or readiness)", native_steps);
  DSVR_LOG("VrTrace: lab native combat observer=%d (natural calls; 256 detail batches for short "
           "captures, streamed detail for continuous captures; local self contacts counted separately; "
           "raw before/after HP/stamina, no injected damage or feedback)", native_contacts);
  DSVR_LOG("VrTrace: lab native object-activation observer=%d (natural requests only; "
           "raw handles/state/PARAM distance; no eligibility or activation acceptance)",native_interactions);
  return combat_only ? native_contacts && native_interactions && native_steps==7 : w && r && c && p && consumer && u;
}

bool VrTraceInstall(bool enabled) { return InstallTrace(enabled,false); }
bool VrTraceInstallCombat() {
#ifdef DSVR_ENABLE_LAB
  return InstallTrace(true,true);
#else
  DSVR_WARN("VrTrace: combat-only observation requires an opt-in LAB build");
  return false;
#endif
}

}  // namespace dsvr
