// Actual opt-in native contact detour + capture writer, synthetic memory.
// This proves pass-through/attribution guards, not a native damage ABI.
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/common/logging.h"
#include "../src/common/memory.h"
#include "../src/core/vr_trace.h"
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
constexpr std::uintptr_t base=0x140000000,table=base+0x13251f0;
constexpr std::uint64_t result=0x12345678abcdef01ull;
using Contact=std::uint64_t(__fastcall*)(void*);
Contact callback=nullptr;
std::array<unsigned char,0x200> record{};
std::array<unsigned char,0x600> source{},target{};
// A third actor plus the world pointer chain, so the local player is a known
// identity in the fixture and a contact can be proven NOT to involve them.
std::array<unsigned char,0x200> other{};
std::array<unsigned char,0x100> world{};
unsigned calls=0,reads=0,mode=0,permission_queries=0;
bool native_permission_cache=false;
bool unreadable=false,bad_signature=false;
unsigned log_calls=0;
unsigned health_logs=0;
unsigned unrelated_signatures=0;
std::wstring replacement;
template<class T,class V> void Put(T& buffer,unsigned offset,V value) {
  std::memcpy(buffer.data()+offset,&value,sizeof(value));
}
std::uint64_t __fastcall Original(void* data) {
  ++calls;
  native_permission_cache=native_permission_cache ||
      dsvr::GuardedReadScope::Contains(reinterpret_cast<std::uintptr_t>(target.data()),
          reinterpret_cast<std::uintptr_t>(target.data())+target.size());
  if (data!=record.data()) return 0;
  Put(target,0x3e8,375);Put(target,0x3f8,80);
  Put(record,0x3c,7000); // observation must retain the pre-call selected row
  Put(record,0x140,0x99999999u);
  if (mode==1) Put(target,8,99u); // same address, different actor lifetime
  if (mode==2) Put(record,0x138,reinterpret_cast<std::uintptr_t>(source.data()));
  if (mode==3) {
    dsvr::VrTraceRequestStop();dsvr::VrTracePump();
    if (!dsvr::VrTraceBegin(replacement.c_str())) return 0;
  }
  return result;
}
void Fixture() {
  Put(source,0,table);Put(target,0,table);
  Put(source,8,0x12345678u);Put(target,8,0xfedcba98u);
  Put(source,0x3e8,1000);Put(source,0x3f8,120);
  Put(target,0x3e8,400);Put(target,0x3f8,100);
  Put(other,0,table);Put(other,8,0x2468ace1u);
  Put(other,0x3e8,500);Put(other,0x3f8,90);
  // base+0x1c77e50 -> world, world+0x68 -> the local player actor (source).
  Put(world,0x68,reinterpret_cast<std::uintptr_t>(source.data()));
  Put(record,0x130,reinterpret_cast<std::uintptr_t>(source.data()));
  Put(record,0x138,reinterpret_cast<std::uintptr_t>(target.data()));
  Put(record,0x34,64);Put(record,0x38,0x12345678u);Put(record,0x3c,6000);
  Put(record,0x40,1);Put(record,0x44,-2);Put(record,0x48,500000);
  Put(record,0xc8,static_cast<unsigned char>(0x13));
  Put(record,0xc9,static_cast<unsigned char>(1));
  Put(record,0x140,0xfedcba98u);
}
std::string Read(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path),std::ios::binary);
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
// One record's own line, so a flag value cannot be credited to a different event.
std::string MotionLine(const std::string& text) {
  const auto at=text.find("\"event\":\"native_combat_motion\"");
  if (at==std::string::npos) return {};
  const auto end=text.find('\n',at);
  return text.substr(at,end-at);
}
}
MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID address,LPVOID detour,LPVOID* original) {
  if (reinterpret_cast<std::uintptr_t>(address)!=base+0x3c4020) return MH_ERROR_UNSUPPORTED_FUNCTION;
  callback=reinterpret_cast<Contact>(detour);*original=reinterpret_cast<void*>(&Original);return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
namespace dsvr {
void LogWrite(LogLevel,const char*,int,const char* format,...) {
  ++log_calls;
  if(std::strstr(format,"TraceHealth:")) ++health_logs;
}
std::wstring LogPathNextToModule(void*,const wchar_t*) { return L"unused"; }
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t address,const void* expected,std::size_t size) {
  if (address==base+0x339f30) {
    constexpr unsigned char getter[]{0xb8,1,0,0,0,0xc3};
    return size==sizeof(getter) && !std::memcmp(expected,getter,size);
  }
  constexpr unsigned char prologue[]{0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,
      0xec,0x20,0x48,0x8b,0xb1,0x30,1,0,0,0x48,0x8b,0xd9,0x48,0x8b,0xce};
  if(address!=base+0x3c4020) ++unrelated_signatures;
  return !bad_signature && address==base+0x3c4020 && size==sizeof(prologue) &&
      !std::memcmp(expected,prologue,size);
}
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size) {
  ++reads;if (unreadable) return false;
  if(!GuardedReadScope::Contains(address,address+size)) ++permission_queries;
  if (address==table+0x10 && size==8) {
    GuardedReadScope::Remember(table,table+0x18);
    const auto getter=base+0x339f30;std::memcpy(out,&getter,size);return true;
  }
  if (address==base+0x1c77e50 && size==8) {  // the world pointer slot
    GuardedReadScope::Remember(base+0x1c77e50,base+0x1c77e58);
    const auto world_address=reinterpret_cast<std::uintptr_t>(world.data());
    std::memcpy(out,&world_address,size);return true;
  }
  const struct { const void* start;std::size_t size; } regions[]{
      {record.data(),record.size()},{source.data(),source.size()},
      {target.data(),target.size()},{other.data(),other.size()},
      {world.data(),world.size()}};
  for (const auto& region:regions) {
    const auto start=reinterpret_cast<std::uintptr_t>(region.start);
    if (address>=start && address-start<=region.size && size<=region.size-(address-start)) {
      GuardedReadScope::Remember(start,start+region.size);
      std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
    }
  }
  return false;
}
}
int main(int argc,char** argv) {
  const bool combat_only=argc==2 && std::string(argv[1])=="--combat-only";
  if(!combat_only && argc!=1 && (argc!=3 || (std::string(argv[1])!="--fixture" &&
                             std::string(argv[1])!="--profile"))) return 2;
  using namespace dsvr;
  // A changed prologue never installs a speculative native hook.
  bad_signature=true;
  if(combat_only) VrTraceInstallCombat(); else VrTraceInstall(true);
  CHECK(!callback);
  bad_signature=false;
  if(combat_only) VrTraceInstallCombat(); else VrTraceInstall(true);
  CHECK(callback);
  Fixture();const auto idle_reads=reads;
  CHECK(callback(record.data())==result && reads==idle_reads && calls==1);
  wchar_t temp[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,temp));
  const auto prefix=std::wstring(temp)+L"DSVR-combat-"+std::to_wstring(GetCurrentProcessId())+
      L"-"+std::to_wstring(GetTickCount64());
  if(combat_only) {
    CHECK(unrelated_signatures==0); // no renderer/input probes even at install
    CHECK(!VrTraceInstall(true)); // cannot add a conflicting domain later
    const auto path=prefix+L"-render-flood";CHECK(VrTraceBegin(path.c_str()));
    VrTraceEvent graphics{};graphics.event="constant_upload_enter";
    for(unsigned i=0;i<100000;++i) VrTraceRecord(graphics);
    Fixture();CHECK(callback(record.data())==result);
    VrTraceFinish(false);
    const auto text=Read(path);
    CHECK(text.find("constant_upload_enter")==std::string::npos);
    CHECK(text.find("native_combat_contact")!=std::string::npos);
    CHECK(VrTraceState()==TraceCaptureState::Complete);
    std::filesystem::remove(path);
    std::puts("PASS combat-only capture refuses unrelated render flood");
    return 0;
  }
  for (mode=0;mode<=3;++mode) {
    Fixture();const auto path=prefix+L"-"+std::to_wstring(mode)+L".jsonl";
    replacement=path+L"-next";
    CHECK(VrTraceBegin(path.c_str()));
    const auto before=calls,logs=log_calls,queries=permission_queries;
    CHECK(callback(record.data())==result && calls==before+1);
    CHECK(!native_permission_cache);
    if(mode==0) {
      // Cold-start cost of one callback: the contact pair, both actors, the
      // 10 Hz local-player identity probe (world slot, world+0x68, the vtable
      // read), the two motion fields, the context tuple/flags and event handle.
      // No GuardedReadScope is open
      // across the detour, so every guarded read is its own permission query -
      // that is the cost this budget guards. The probe is rate limited, so this
      // is the worst single callback, not a per-callback rate.
      std::printf("combat actor permission queries: %u (budget 18)\n",permission_queries-queries);
      CHECK(permission_queries-queries<=18);
    }
    if(mode!=3) CHECK(log_calls==logs); // no synchronous logging from the hook
    if (mode==3) {
      VrTraceFinish(false);
      CHECK(Read(replacement).find("native_combat_contact")==std::string::npos);
      CHECK(Read(path).find("native_combat_contact")==std::string::npos);
      CHECK(Read(path).find("native_combat_identity")==std::string::npos);
      CHECK(Read(replacement).find("native_combat_identity")==std::string::npos);
      CHECK(Read(replacement).find("native_combat_context")==std::string::npos);
      std::filesystem::remove(replacement);
    } else {
      VrTraceFinish(false);const auto text=Read(path);
      CHECK(text.find("native_combat_contact")!=std::string::npos);
      // Pair the measured outcome with independently retained actor lifetimes
      // and the complete 64-bit native return, without float32 ID truncation.
      CHECK(text.find("native_combat_identity")!=std::string::npos);
      CHECK(text.find("native_combat_diagnostics")!=std::string::npos);
      const auto context_at=text.find("\"event\":\"native_combat_context\"");
      CHECK(context_at!=std::string::npos);
      const auto context=text.substr(context_at,text.find('\n',context_at)-context_at);
      CHECK(context.find("\"flags\":7")!=std::string::npos);
      CHECK(context.find("[64,0,22136,4660,6000,0,1,0,65534,65535,41248,7,19,1,47768,65244]")!=std::string::npos);
      CHECK(text.find("\"combat_context_schema\":1")!=std::string::npos);
      // The motion record is the caller-built record's own direction/velocity
      // fields. The fixture leaves both zeroed, so both reads succeed and are
      // finite, the direction is not unit length, and the record stays bound to
      // the same callback through native_rate.
      CHECK(MotionLine(text).find("\"flags\":23")!=std::string::npos);
      CHECK(MotionLine(text).find("\"native_rate\":2882400001")!=std::string::npos);
      CHECK(text.find(",22136,4660,47768,65244,61185,43981,22136,4660]")!=std::string::npos);
      CHECK(text.find("\"native_rate\":2882400001")!=std::string::npos);
      CHECK(text.find(mode==0?"\"flags\":15":"\"flags\":7")!=std::string::npos);
      if(mode==1 || mode==2) CHECK(text.find("\"flags\":32768")!=std::string::npos);
      if (mode==0) CHECK(text.find(",1000,1000,120,120,400,375,100,80]")!=std::string::npos);
    }
    std::filesystem::remove(path);
  }
  mode=0;Fixture();unreadable=true;
  auto path=prefix+L"-unreadable";CHECK(VrTraceBegin(path.c_str()));
  const auto before=calls;CHECK(callback(record.data())==result && calls==before+1);
  VrTraceFinish(false);CHECK(Read(path).find("\"flags\":0")!=std::string::npos);
  CHECK(Read(path).find("\"flags\":30583")!=std::string::npos);
  std::filesystem::remove(path);unreadable=false;
  Fixture();path=prefix+L"-bounded";CHECK(VrTraceBegin(path.c_str()));
  for (unsigned i=0;i<256;++i) CHECK(callback(record.data())==result);
  const auto bounded_reads=reads;
  for (unsigned i=0;i<10;++i) CHECK(callback(record.data())==result);
  // Past the ceiling the hook still pays one cheap pair read per callback (it
  // must know whether a contact involves the player) plus an identity probe
  // refreshed at 10 Hz. It never pays the full actor observation again, so the
  // cost stays bounded per callback instead of zero-but-blind.
  const auto extra_reads=reads-bounded_reads;
  CHECK(extra_reads>=10 && extra_reads<=100);
  VrTraceFinish(false);
  const auto bounded=Read(path);
  unsigned detail_batches=0;
  for(auto at=bounded.find("native_combat_contact");at!=std::string::npos;
      at=bounded.find("native_combat_contact",at+1)) ++detail_batches;
  CHECK(detail_batches==256);
  CHECK(bounded.find("native_combat_limit")!=std::string::npos);
  // The ceiling is reported as a count, not as one silent marker.
  CHECK(bounded.find("\"combat_total\":266")!=std::string::npos);
  CHECK(bounded.find("\"combat_detail\":256")!=std::string::npos);
  CHECK(bounded.find("\"combat_refused\":10")!=std::string::npos);
  CHECK(bounded.find("\"combat_non_player\":0")!=std::string::npos);
  CHECK(VrTraceState()==TraceCaptureState::Complete);std::filesystem::remove(path);
  // A contact that positively does not involve the local player is excluded
  // cheaply, and the exclusion is visible in the capture rather than silent.
  Fixture();path=prefix+L"-non-player";CHECK(VrTraceBegin(path.c_str()));
  Put(record,0x130,reinterpret_cast<std::uintptr_t>(other.data()));
  for (unsigned i=0;i<3;++i) CHECK(callback(record.data())==result);
  VrTraceFinish(false);
  const auto excluded_text=Read(path);
  CHECK(excluded_text.find("native_combat_excluded")!=std::string::npos);
  CHECK(excluded_text.find("native_combat_contact")==std::string::npos);
  CHECK(excluded_text.find("\"combat_non_player\":3")!=std::string::npos);
  CHECK(excluded_text.find("\"combat_detail\":0")!=std::string::npos);
  std::filesystem::remove(path);
  // Leave only four slots: the five-part sample must drop as a whole, never
  // leave an apparently attributable contact with missing refusal diagnostics.
  Fixture();path=prefix+L"-pressure";CHECK(VrTraceBegin(path.c_str()));
  VrTraceEvent filler{};filler.event="queue_pressure";
  for(unsigned i=0;i<4092;++i) VrTraceRecord(filler);
  CHECK(callback(record.data())==result);
  VrTraceFinish(false);
  const auto pressure=Read(path);
  CHECK(pressure.find("native_combat_contact")==std::string::npos);
  CHECK(pressure.find("native_combat_identity")==std::string::npos);
  CHECK(pressure.find("native_combat_diagnostics")==std::string::npos);
  CHECK(pressure.find("native_combat_motion")==std::string::npos);
  CHECK(pressure.find("native_combat_context")==std::string::npos);
  CHECK(pressure.find("\"dropped\":5")!=std::string::npos);
  CHECK(VrTraceState()==TraceCaptureState::Dropped);std::filesystem::remove(path);
  CHECK(health_logs>=7); // writer emits bounded capture-close summaries
  if(argc==3) {
    const auto fixture_path=std::filesystem::path(argv[2]).wstring();
    CHECK(VrTraceBegin(fixture_path.c_str()));
    const unsigned count=std::string(argv[1])=="--profile" ? 256u : 1u;
    for(unsigned i=0;i<count;++i) {
      Fixture();CHECK(callback(record.data())==result);
    }
    VrTraceFinish(false);
    CHECK(VrTraceState()==TraceCaptureState::Complete);
  }
  std::puts("PASS native combat observation: natural call, identity, capture generation and bounds");
}
