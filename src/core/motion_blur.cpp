#include "motion_blur.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include "../common/memory.h"
#include "hook_install.h"
namespace dsvr {
namespace {
constexpr std::uintptr_t kGet=0x1b6970,kSet=0x1b69a0,kOwner=0x1c6aec8;
constexpr unsigned char kGetBytes[]={0x83,0xfa,0x35,0x77,0x20,0x48,0x63,0xc2,0x45,0x84,0xc0,0x74,0x0c};
constexpr unsigned char kSetBytes[]={0x83,0xfa,0x35,0x77,0x0f,0x48,0x63,0xc2,0x48,0x83,0xc0,0x39,
    0x48,0x8d,0x04,0x40,0x44,0x89,0x04,0xc1,0xc3};
using Get=int(__fastcall*)(void*,unsigned,unsigned char);
using Set=void(__fastcall*)(void*,unsigned,int);
Get g_get=nullptr;Set g_set=nullptr;
std::atomic<bool> g_enabled{false};
// Once an enable is attempted, a callback may still hold a trampoline. Keep
// owned disabled trampolines alive through rollback, just as on uninstall.
bool g_callbacks_possible=false;
std::atomic<std::uint64_t> g_reads{0},g_blocked{0};
bool IsBlur(void* owner,unsigned index) {
  // Pinned static option entry #11 is [DisplaySettingFilter] Blur, default 1.
  // The load loop at 1b69c0 reads section/key/value at +548/+550/+558 + 24*i.
  // Get/Set use (i+0x39)*24. Verify the live metadata and singleton, never
  // assume that a numeric option in another object means motion blur.
  if(!g_enabled.load() || !owner || index!=11)return false;
  std::uintptr_t singleton=0,key=0;unsigned section=0;
  const auto address=reinterpret_cast<std::uintptr_t>(owner);
  constexpr wchar_t name[]=L"Blur";
  return GuardedRead(ModuleBase(nullptr)+kOwner,singleton) && singleton==address &&
      GuardedRead(address+0x650,section) && section==3 &&
      GuardedRead(address+0x658,key) && key && BytesMatch(key,name,sizeof(name));
}
int __fastcall ReadOption(void* owner,unsigned index,unsigned char defaults) {
  const int value=g_get(owner,index,defaults);
  if(!IsBlur(owner,index))return value;
  ++g_reads;return 0;
}
void __fastcall WriteOption(void* owner,unsigned index,int value) {
  if(IsBlur(owner,index)) {if(value)++g_blocked;value=0;}
  g_set(owner,index,value);
}
}
bool MotionBlurHookInstall(bool enabled) {
  if(!enabled)return false;
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  if(g_enabled.load())return true;
  const auto base=ModuleBase(nullptr);
  if(!BytesMatch(base+kGet,kGetBytes,sizeof(kGetBytes)) ||
      !BytesMatch(base+kSet,kSetBytes,sizeof(kSetBytes)))return false;
  const auto initialized=MH_Initialize();
  if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
  const auto get_target=reinterpret_cast<void*>(base+kGet);
  const auto set_target=reinterpret_cast<void*>(base+kSet);
  const auto rollback=[&]() {
    g_enabled.store(false);
    if(g_get)MH_DisableHook(get_target);
    if(g_set)MH_DisableHook(set_target);
    if(!g_callbacks_possible) {
      if(g_get) {MH_RemoveHook(get_target);g_get=nullptr;}
      if(g_set) {MH_RemoveHook(set_target);g_set=nullptr;}
    }
    return false;
  };
  if(!g_get && MH_CreateHook(get_target,reinterpret_cast<void*>(ReadOption),
      reinterpret_cast<void**>(&g_get))!=MH_OK)return rollback();
  if(!g_set && MH_CreateHook(set_target,reinterpret_cast<void*>(WriteOption),
      reinterpret_cast<void**>(&g_set))!=MH_OK)return rollback();
  g_callbacks_possible=true;
  const auto get=MH_EnableHook(get_target);
  if(get!=MH_OK && get!=MH_ERROR_ENABLED)return rollback();
  const auto set=MH_EnableHook(set_target);
  if(set!=MH_OK && set!=MH_ERROR_ENABLED)return rollback();
  g_enabled.store(true);
  return true;
}
void MotionBlurHookUninstall() {
  std::lock_guard<std::mutex> lock(HookInstallMutex());
  g_enabled.store(false);
  const auto base=ModuleBase(nullptr);
  if(g_get)MH_DisableHook(reinterpret_cast<void*>(base+kGet));
  if(g_set)MH_DisableHook(reinterpret_cast<void*>(base+kSet));
}
MotionBlurStatus MotionBlurGetStatus() {return {g_enabled.load(),g_reads.load(),g_blocked.load()};}
}
