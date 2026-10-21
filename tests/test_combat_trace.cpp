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
unsigned calls=0,reads=0,mode=0;
bool unreadable=false,bad_signature=false;
std::wstring replacement;
template<class T,class V> void Put(T& buffer,unsigned offset,V value) {
  std::memcpy(buffer.data()+offset,&value,sizeof(value));
}
std::uint64_t __fastcall Original(void* data) {
  ++calls;
  if (data!=record.data()) return 0;
  Put(target,0x3e8,375);Put(target,0x3f8,80);
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
  Put(source,8,7u);Put(target,8,9u);
  Put(source,0x3e8,1000);Put(source,0x3f8,120);
  Put(target,0x3e8,400);Put(target,0x3f8,100);
  Put(record,0x130,reinterpret_cast<std::uintptr_t>(source.data()));
  Put(record,0x138,reinterpret_cast<std::uintptr_t>(target.data()));
}
std::string Read(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path),std::ios::binary);
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
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
void LogWrite(LogLevel,const char*,int,const char*,...) {}
std::wstring LogPathNextToModule(void*,const wchar_t*) { return L"unused"; }
std::uintptr_t ModuleBase(const wchar_t*) { return base; }
bool BytesMatch(std::uintptr_t address,const void* expected,std::size_t size) {
  if (address==base+0x339f30) {
    constexpr unsigned char getter[]{0xb8,1,0,0,0,0xc3};
    return size==sizeof(getter) && !std::memcmp(expected,getter,size);
  }
  constexpr unsigned char prologue[]{0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,
      0xec,0x20,0x48,0x8b,0xb1,0x30,1,0,0,0x48,0x8b,0xd9,0x48,0x8b,0xce};
  return !bad_signature && address==base+0x3c4020 && size==sizeof(prologue) &&
      !std::memcmp(expected,prologue,size);
}
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size) {
  ++reads;if (unreadable) return false;
  if (address==table+0x10 && size==8) {
    const auto getter=base+0x339f30;std::memcpy(out,&getter,size);return true;
  }
  const struct { const void* start;std::size_t size; } regions[]{
      {record.data(),record.size()},{source.data(),source.size()},{target.data(),target.size()}};
  for (const auto& region:regions) {
    const auto start=reinterpret_cast<std::uintptr_t>(region.start);
    if (address>=start && address-start<=region.size && size<=region.size-(address-start)) {
      std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
    }
  }
  return false;
}
}
int main() {
  using namespace dsvr;
  // A changed prologue never installs a speculative native hook.
  bad_signature=true;VrTraceInstall(true);CHECK(!callback);
  bad_signature=false;VrTraceInstall(true);CHECK(callback);
  Fixture();const auto idle_reads=reads;
  CHECK(callback(record.data())==result && reads==idle_reads && calls==1);
  wchar_t temp[MAX_PATH]{};CHECK(GetTempPathW(MAX_PATH,temp));
  const auto prefix=std::wstring(temp)+L"DSVR-combat-"+std::to_wstring(GetCurrentProcessId())+
      L"-"+std::to_wstring(GetTickCount64());
  for (mode=0;mode<=3;++mode) {
    Fixture();const auto path=prefix+L"-"+std::to_wstring(mode)+L".jsonl";
    replacement=path+L"-next";
    CHECK(VrTraceBegin(path.c_str()));
    const auto before=calls;CHECK(callback(record.data())==result && calls==before+1);
    if (mode==3) {
      VrTraceFinish(false);
      CHECK(Read(replacement).find("native_combat_contact")==std::string::npos);
      CHECK(Read(path).find("native_combat_contact")==std::string::npos);
      std::filesystem::remove(replacement);
    } else {
      VrTraceFinish(false);const auto text=Read(path);
      CHECK(text.find("native_combat_contact")!=std::string::npos);
      CHECK(text.find("\"native_rate\":2882400001")!=std::string::npos);
      CHECK(text.find(mode==0?"\"flags\":15":"\"flags\":7")!=std::string::npos);
      if (mode==0) CHECK(text.find(",1000,1000,120,120,400,375,100,80]")!=std::string::npos);
    }
    std::filesystem::remove(path);
  }
  mode=0;Fixture();unreadable=true;
  auto path=prefix+L"-unreadable";CHECK(VrTraceBegin(path.c_str()));
  const auto before=calls;CHECK(callback(record.data())==result && calls==before+1);
  VrTraceFinish(false);CHECK(Read(path).find("\"flags\":0")!=std::string::npos);
  std::filesystem::remove(path);unreadable=false;
  Fixture();path=prefix+L"-bounded";CHECK(VrTraceBegin(path.c_str()));
  for (unsigned i=0;i<256;++i) CHECK(callback(record.data())==result);
  const auto bounded_reads=reads;
  for (unsigned i=0;i<10;++i) CHECK(callback(record.data())==result);
  CHECK(reads==bounded_reads);VrTraceFinish(false);
  CHECK(Read(path).find("native_combat_limit")!=std::string::npos);
  CHECK(VrTraceState()==TraceCaptureState::Complete);std::filesystem::remove(path);
  std::puts("PASS native combat observation: natural call, identity, capture generation and bounds");
}
