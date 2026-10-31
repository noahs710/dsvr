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
using HealthSetter=std::uint64_t(__fastcall*)(void*,int);
HealthSetter health_callback=nullptr;
using Interaction=std::uint64_t(__fastcall*)(void*,int,unsigned);
Interaction interaction_callback=nullptr;
std::array<unsigned char,0x58> action_group{};
std::array<unsigned char,0xf0> action_row{};
std::array<unsigned char,0x20> action_param{};
unsigned interaction_calls=0;
bool interaction_changed=false,interaction_bad_args=false;
std::array<unsigned char,0x200> record{};
std::array<unsigned char,0x600> source{},target{};
// A third actor plus the world pointer chain, so the local player is a known
// identity in the fixture and a contact can be proven NOT to involve them.
std::array<unsigned char,0x200> other{};
std::array<unsigned char,0x100> world{};
unsigned calls=0,reads=0,mode=0,permission_queries=0;
unsigned health_calls=0;
unsigned health_emit_count=1;
bool health_bad_args=false;
constexpr std::uint64_t health_result=0x9876543210abcdefull;
bool native_permission_cache=false;
bool unreadable=false,bad_signature=false,response_unreadable=false;
unsigned log_calls=0;
unsigned health_logs=0;
unsigned unrelated_signatures=0;
std::wstring replacement;
template<class T,class V> void Put(T& buffer,unsigned offset,V value) {
  std::memcpy(buffer.data()+offset,&value,sizeof(value));
}
std::uint64_t __fastcall OriginalInteraction(void* group,int index,unsigned actor) {
  ++interaction_calls;
  interaction_bad_args=interaction_bad_args || group!=action_group.data() || index!=0 || actor!=0xffffffffu ||
      dsvr::GuardedReadScope::Contains(reinterpret_cast<std::uintptr_t>(action_row.data()),
          reinterpret_cast<std::uintptr_t>(action_row.data())+action_row.size());
  Put(action_row,0x20,3u);
  if(interaction_changed) Put(action_row,0x1c,0xdeadbeefu);
  return result;
}
std::uint64_t __fastcall OriginalHealthSetter(void* actor,int requested) {
  ++health_calls;
  health_bad_args=health_bad_args || actor!=target.data() || requested!=375 ||
      dsvr::GuardedReadScope::Contains(reinterpret_cast<std::uintptr_t>(target.data()),
          reinterpret_cast<std::uintptr_t>(target.data())+target.size());
  Put(target,0x3e8,requested);
  return health_result;
}
std::uint64_t __fastcall Original(void* data) {
  ++calls;
  native_permission_cache=native_permission_cache ||
      dsvr::GuardedReadScope::Contains(reinterpret_cast<std::uintptr_t>(target.data()),
          reinterpret_cast<std::uintptr_t>(target.data())+target.size());
  if (data!=record.data()) return 0;
  for(unsigned i=0;i<health_emit_count;++i)
    if(health_callback && health_callback(target.data(),375)!=health_result)
      health_bad_args=true;
  Put(target,0x3f8,80);
  Put(record,0x3c,7000); // observation must retain the pre-call selected row
  Put(record,0x140,0x99999999u);
  Put(record,0x54,0xfedcba98u);
  Put(record,0x168,std::uint16_t{1});
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
  Put(record,0x54,2u);
  Put(record,0x168,std::uint16_t{0x5aa5});
  // 140529670 store fields: weight +0x50, paired IDs +0xac/+0xb0, tag +0xb4.
  Put(record,0x50,2.5f);
  Put(record,0xac,0x11223344u);
  Put(record,0xb0,0x55667788u);
  Put(record,0xb4,static_cast<unsigned char>(0x7e));
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
  if(reinterpret_cast<std::uintptr_t>(address)==base+0x322910) {
    health_callback=reinterpret_cast<HealthSetter>(detour);
    *original=reinterpret_cast<void*>(&OriginalHealthSetter);return MH_OK;
  }
  if(reinterpret_cast<std::uintptr_t>(address)==base+0x313470) {
    interaction_callback=reinterpret_cast<Interaction>(detour);
    *original=reinterpret_cast<void*>(&OriginalInteraction);return MH_OK;
  }
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
  if(address==base+0x322910) {
    constexpr unsigned char setter[]{0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,
        0x20,0xf6,0x81,0x24,0x05,0,0,0x40,0x48,0x8b,0xd9};
    return !bad_signature && size==sizeof(setter) && !std::memcmp(expected,setter,size);
  }
  if(address==base+0x313470) {
    constexpr unsigned char prologue[]{0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x50,
        0x48,0x63,0xf2,0x41,0x8b,0xe8,0x48,0x8b,0xf9,0x85,0xd2};
    return !bad_signature && size==sizeof(prologue) && !std::memcmp(expected,prologue,size);
  }
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
  const auto object=reinterpret_cast<std::uintptr_t>(record.data());
  if (response_unreadable && ((address==object+0x130 && size==0x3a) ||
      (address==object+0x34 && size==36) || (address==object+0x54 && size==4) ||
      (address==object+0x50 && size==4) || (address==object+0xac && size==4) ||
      (address==object+0xb0 && size==4) || (address==object+0xb4 && size==1))) return false;
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
      {world.data(),world.size()},{action_group.data(),action_group.size()},
      {action_row.data(),action_row.size()},{action_param.data(),action_param.size()}};
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
  const bool session_only=argc==2 && std::string(argv[1])=="--session-only";
  if(!combat_only && !session_only && argc!=1 && (argc!=3 || (std::string(argv[1])!="--fixture" &&
                              std::string(argv[1])!="--profile"))) return 2;
  using namespace dsvr;
  // A changed prologue never installs a speculative native hook.
  bad_signature=true;
  if(combat_only) VrTraceInstallCombat();
  else if(session_only) VrTraceInstallSession();
  else VrTraceInstall(true);
  CHECK(!callback);
  unrelated_signatures=0;
  bad_signature=false;
  if(combat_only) VrTraceInstallCombat();
  else if(session_only) VrTraceInstallSession();
  else VrTraceInstall(true);
  CHECK(callback);
  CHECK(health_callback);
  CHECK(interaction_callback);
  Fixture();const auto idle_reads=reads;
  const auto idle_health=health_calls;
  CHECK(callback(record.data())==result && reads==idle_reads && calls==1);
  CHECK(health_calls==idle_health+1 && !health_bad_args);
  CHECK(health_callback(target.data(),375)==health_result); // inactive scope still preserves raw RAX
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
    Put(action_group,0x18,reinterpret_cast<std::uintptr_t>(action_row.data()));
    Put(action_group,0x20,1);
    Put(action_row,0x40,reinterpret_cast<std::uintptr_t>(action_param.data()));
    Put(action_param,0xc,std::uint16_t{90});
    CHECK(interaction_callback(action_group.data(),0,0xffffffffu)==result);
    VrTraceFinish(false);
    const auto text=Read(path);
    CHECK(text.find("constant_upload_enter")==std::string::npos);
    CHECK(text.find("native_combat_contact")!=std::string::npos);
    CHECK(text.find("native_interaction_activation")!=std::string::npos);
    CHECK(VrTraceState()==TraceCaptureState::Complete);
    std::filesystem::remove(path);
    std::puts("PASS combat-only capture refuses unrelated render flood");
    return 0;
  }
  if(session_only) {
    CHECK(unrelated_signatures==1); // only the optional input-blocker signature
    CHECK(!VrTraceInstallCombat()); // a process cannot switch domains
    const auto path=prefix+L"-session";CHECK(VrTraceBegin(path.c_str(),600000));
    VrTraceEvent graphics{};graphics.event="constant_upload_enter";
    VrTraceRecord(graphics);
    VrTraceRecordInputBlockers(0x12,42);
    Fixture();
    CHECK(callback(record.data())==result);
    Put(action_group,0x18,reinterpret_cast<std::uintptr_t>(action_row.data()));
    Put(action_group,0x20,1);
    Put(action_row,0x40,reinterpret_cast<std::uintptr_t>(action_param.data()));
    CHECK(interaction_callback(action_group.data(),0,0xffffffffu)==result);
    VrTraceRecordStep({2,42,1,2,3,.01f,0});
    VrTraceFinish(false);
    const auto text=Read(path);
    CHECK(text.find("constant_upload_enter")==std::string::npos);
    CHECK(text.find("native_input_blockers")!=std::string::npos);
    CHECK(text.find("native_combat_contact")!=std::string::npos);
    CHECK(text.find("native_interaction_activation")!=std::string::npos);
    CHECK(text.find("native_step_dispatch")!=std::string::npos);
    CHECK(VrTraceState()==TraceCaptureState::Complete);
    std::filesystem::remove(path);
    std::puts("PASS autonomous session domain excludes unrelated render telemetry");
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
      // Reuse permissions within each read phase, independently of actor data.
      // Native dispatch must see no cached permissions (checked by Original).
      // This cold callback also includes the rate-limited local-player probe.
      std::printf("combat actor permission queries: %u (budget 10)\n",permission_queries-queries);
      CHECK(permission_queries-queries<=10);
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
      CHECK(text.find("native_combat_health_write")!=std::string::npos);
      CHECK(text.find("\"combat_health_schema\":1,\"combat_health_hook\":1")!=std::string::npos);
      CHECK(text.find("\"combat_module_base\":\"0000000140000000\"")!=std::string::npos);
      const auto health_at=text.find("\"event\":\"native_combat_health_write\"");
      CHECK(health_at!=std::string::npos);
      const auto health_line=text.substr(health_at,text.find('\n',health_at)-health_at);
      CHECK(health_line.find(",375,0,0,1,0,0,0,1]")!=std::string::npos);
            const auto diagnostic_at=text.find("\"event\":\"native_combat_diagnostics\"");
            const auto diagnostic=text.substr(diagnostic_at,text.find('\n',diagnostic_at)-diagnostic_at);
            CHECK(diagnostic.find("[31,2,0,47768,65244,165,90,1,0,2.5,13124,4386,30600,21862,126,2]")!=std::string::npos);
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
  mode=0;Fixture();response_unreadable=true;
  {
    const auto partial_path=prefix+L"-response-unreadable";
    CHECK(VrTraceBegin(partial_path.c_str()));
    const auto before_partial=calls;
    CHECK(callback(record.data())==result && calls==before_partial+1);
    VrTraceFinish(false);
    const auto text=Read(partial_path);
    const auto at=text.find("\"event\":\"native_combat_diagnostics\"");
    CHECK(at!=std::string::npos);
    const auto line=text.substr(at,text.find('\n',at)-at);
    CHECK(line.find("[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2]")!=std::string::npos);
    CHECK(text.find(",1000,1000,120,120,400,375,100,80]")!=std::string::npos);
    CHECK(text.find("[64,0,22136,4660,6000,0,1,0,65534,65535,41248,7,19,1,47768,65244]")!=std::string::npos);
    std::filesystem::remove(partial_path);
  }
  response_unreadable=false;
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
  Fixture();health_emit_count=6;path=prefix+L"-health-overflow";
  CHECK(VrTraceBegin(path.c_str()));
  CHECK(callback(record.data())==result);
  VrTraceFinish(false);health_emit_count=1;
  const auto overflow=Read(path);
  unsigned health_records=0;
  for(auto at=overflow.find("native_combat_health_write");at!=std::string::npos;
      at=overflow.find("native_combat_health_write",at+1)) ++health_records;
  CHECK(health_records==4);
  CHECK(overflow.find(",375,0,0,6,0,1,0,1]")!=std::string::npos);
  std::filesystem::remove(path);
  // Continuous combat recording must retain real contacts after a burst of
  // local-player self contacts and after the short-capture detail ceiling.
  Fixture();path=prefix+L"-continuous";
  CHECK(!VrTraceBegin(path.c_str(),600001));
  CHECK(VrTraceBegin(path.c_str(),600000));
  const auto continuous_calls=calls;
  for(unsigned i=0;i<600;++i) {
    Fixture();Put(record,0x138,reinterpret_cast<std::uintptr_t>(source.data()));
    CHECK(callback(record.data())==result);
  }
  for(unsigned i=0;i<300;++i) {
    Fixture();CHECK(callback(record.data())==result);
    if(i%16==0) VrTracePump();
  }
  CHECK(calls==continuous_calls+900 && !native_permission_cache);
  VrTraceFinish(false);
  const auto continuous=Read(path);
  unsigned continuous_details=0;
  for(auto at=continuous.find("native_combat_contact");at!=std::string::npos;
      at=continuous.find("native_combat_contact",at+1)) ++continuous_details;
  CHECK(continuous_details==300);
  CHECK(continuous.find("\"combat_self\":600")!=std::string::npos);
  CHECK(continuous.find("\"combat_detail\":300")!=std::string::npos);
  CHECK(continuous.find("native_combat_limit")==std::string::npos);
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
  // Leave only four slots: the expanded contact batch must drop as a whole, never
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
  CHECK(pressure.find("native_combat_health_write")==std::string::npos);
  CHECK(pressure.find("\"dropped\":6")!=std::string::npos);
  CHECK(VrTraceState()==TraceCaptureState::Dropped);std::filesystem::remove(path);
  CHECK(health_logs>=7); // writer emits bounded capture-close summaries
  Put(action_group,0x18,reinterpret_cast<std::uintptr_t>(action_row.data()));
  Put(action_group,0x20,1);
  Put(action_row,0x40,reinterpret_cast<std::uintptr_t>(action_param.data()));
  Put(action_param,0xc,std::uint16_t{90});
  for(unsigned changed=0;changed<2;++changed) {
    interaction_changed=changed!=0;
    Put(action_row,0x1c,0x12345678u);Put(action_row,0x20,0u);
    path=prefix+L"-interaction-"+std::to_wstring(changed);
    CHECK(VrTraceBegin(path.c_str()));
    const auto before_interaction=interaction_calls;
    CHECK(interaction_callback(action_group.data(),0,0xffffffffu)==result);
    CHECK(interaction_calls==before_interaction+1 && !interaction_bad_args);
    VrTraceFinish(false);
    const auto text=Read(path);
    const auto at=text.find("\"event\":\"native_interaction_activation\"");CHECK(at!=std::string::npos);
    const auto line=text.substr(at,text.find('\n',at)-at);
    CHECK(line.find(changed?"\"flags\":11":"\"flags\":15")!=std::string::npos);
    CHECK(line.find("[0,0,65535,65535,22136,4660,0,0,3,0,61185,43981,22136,4660,90,0]")!=std::string::npos);
    std::filesystem::remove(path);
  }
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
