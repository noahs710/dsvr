#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "../src/common/config.h"
#include "../src/common/memory.h"
#include "../src/core/native_ui.h"
#include "../src/core/motion_blur.h"
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
constexpr std::uintptr_t base=0x140000000;
std::array<unsigned char,0x680> owner{};
const wchar_t blur_name[]=L"Blur",other_name[]=L"DOF";
bool signature=true,theater=false,menu=false,gameplay=true,xr_running=true,fail_set_enable=false;
bool fail_get_create=false,fail_set_create=false,fail_get_enable=false;
unsigned blur_creates=0,blur_removes=0;
bool get_active=false,set_active=false;
dsvr::Config config;
using Draw=void(__fastcall*)(void*,std::uintptr_t,void*,void*);
using Get=int(__fastcall*)(void*,unsigned,unsigned char);
using Set=void(__fastcall*)(void*,unsigned,int);
Draw ui=nullptr;Get get=nullptr;Set set=nullptr;
unsigned original_ui=0,original_get=0,original_set=0;
void* last_commands=nullptr;
void __fastcall OriginalUi(void* commands,std::uintptr_t,void*,void*) {++original_ui;last_commands=commands;}
int __fastcall OriginalGet(void* object,unsigned index,unsigned char) {
  ++original_get;int value=42;
  if(object==owner.data() && index==11)std::memcpy(&value,owner.data()+0x660,4);
  return value;
}
void __fastcall OriginalSet(void* object,unsigned index,int value) {
  ++original_set;
  if(object==owner.data() && index==11)std::memcpy(owner.data()+0x660,&value,4);
}
}
MH_STATUS WINAPI MH_Initialize() {return MH_OK;}
MH_STATUS WINAPI MH_CreateHook(LPVOID address,LPVOID detour,LPVOID* original) {
  if(address==reinterpret_cast<void*>(base+0x1e5b70)) {ui=reinterpret_cast<Draw>(detour);*original=reinterpret_cast<void*>(OriginalUi);return MH_OK;}
  if(address==reinterpret_cast<void*>(base+0x1b6970)) {++blur_creates;if(fail_get_create)return MH_ERROR_MEMORY_ALLOC;get=reinterpret_cast<Get>(detour);*original=reinterpret_cast<void*>(OriginalGet);return MH_OK;}
  if(address==reinterpret_cast<void*>(base+0x1b69a0)) {++blur_creates;if(fail_set_create)return MH_ERROR_MEMORY_ALLOC;set=reinterpret_cast<Set>(detour);*original=reinterpret_cast<void*>(OriginalSet);return MH_OK;}
  return MH_ERROR_NOT_EXECUTABLE;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID address) {
  if(address==reinterpret_cast<void*>(base+0x1b6970)) {if(fail_get_enable)return MH_ERROR_MEMORY_PROTECT;get_active=true;}
  if(address==reinterpret_cast<void*>(base+0x1b69a0)) {if(fail_set_enable)return MH_ERROR_MEMORY_PROTECT;set_active=true;}
  return MH_OK;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID address) {
  if(address==reinterpret_cast<void*>(base+0x1b6970))get_active=false;
  if(address==reinterpret_cast<void*>(base+0x1b69a0))set_active=false;
  return MH_OK;
}
MH_STATUS WINAPI MH_RemoveHook(LPVOID address) {
  ++blur_removes;
  if(address==reinterpret_cast<void*>(base+0x1b6970))get=nullptr;
  if(address==reinterpret_cast<void*>(base+0x1b69a0))set=nullptr;
  return MH_OK;
}
namespace dsvr {
Config DiagnosticsCurrentConfig() {return config;}
bool CameraGameplayReady() {return gameplay;}
bool ModMenuVisible() {return menu;}
bool XrSessionRunning() { return xr_running; }
bool XrMenuPresentationActive() {return theater;}
std::uintptr_t ModuleBase(const wchar_t*) {return base;}
bool BytesMatch(std::uintptr_t address,const void* bytes,std::size_t size) {
  if(address==reinterpret_cast<std::uintptr_t>(blur_name))return size==sizeof(blur_name) && std::memcmp(bytes,blur_name,size)==0;
  return signature && (address==base+0x1e5b70 || address==base+0x1b6970 || address==base+0x1b69a0);
}
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size) {
  if(address==base+0x1c6aec8 && size==sizeof(std::uintptr_t)) {
    auto value=reinterpret_cast<std::uintptr_t>(owner.data());std::memcpy(out,&value,size);return true;
  }
  const auto start=reinterpret_cast<std::uintptr_t>(owner.data());
  if(address>=start && address-start<=owner.size() && size<=owner.size()-(address-start)) {
    std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;
  }
  return false;
}
}
int main() {
  using namespace dsvr;
  signature=false;CHECK(!NativeUiHookInstall(true) && !MotionBlurHookInstall(true));CHECK(!ui && !get && !set);
  signature=true;CHECK(NativeUiHookInstall(true));
  config.vr_enabled=true;config.hide_ui_in_vr=true;
  ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==0 && NativeUiGetStatus().hidden==1);
  theater=true;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==1 && last_commands==owner.data());
  theater=false;menu=true;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==2);
  menu=false;gameplay=false;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==3);
  gameplay=true;config.vr_enabled=false;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==4);
  config.vr_enabled=true;config.observer_only=true;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==5);
  config.observer_only=false;xr_running=false;ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==6);
  xr_running=true;NativeUiHookUninstall();ui(owner.data(),7,nullptr,nullptr);CHECK(original_ui==7);
  unsigned section=3;auto key=reinterpret_cast<std::uintptr_t>(blur_name);int enabled=1;
  std::memcpy(owner.data()+0x650,&section,4);std::memcpy(owner.data()+0x658,&key,sizeof(key));std::memcpy(owner.data()+0x660,&enabled,4);
  fail_get_create=true;CHECK(!MotionBlurHookInstall(true));
  CHECK(!get_active && !set_active && blur_removes==0);
  fail_get_create=false;fail_set_create=true;CHECK(!MotionBlurHookInstall(true));
  CHECK(!get && !set && !get_active && !set_active && blur_removes==1);
  fail_set_create=false;fail_get_enable=true;CHECK(!MotionBlurHookInstall(true));
  CHECK(get && set && !get_active && !set_active && blur_removes==1);
  CHECK(get(owner.data(),11,0)==1); // late callback keeps a live original trampoline
  fail_get_enable=false;fail_set_enable=true;CHECK(!MotionBlurHookInstall(true));
  CHECK(get && set && !get_active && !set_active && blur_removes==1);
  const auto retained_get=get;const auto retained_set=set;
  CHECK(retained_get(owner.data(),11,0)==1);
  retained_set(owner.data(),11,1);
  int late_value=0;std::memcpy(&late_value,owner.data()+0x660,4);
  CHECK(late_value==1 && original_set==1);
  fail_set_enable=false;CHECK(MotionBlurHookInstall(true));
  const auto creates_once=blur_creates;
  signature=false;CHECK(MotionBlurHookInstall(true));signature=true;
  CHECK(blur_creates==creates_once && get_active && set_active);
  CHECK(get==retained_get && set==retained_set);
  CHECK(get(owner.data(),11,0)==0 && get(owner.data(),11,1)==0 && original_get==4);
  CHECK(get(owner.data(),12,0)==42);
  set(owner.data(),11,1);CHECK(original_set==2 && OriginalGet(owner.data(),11,0)==0);
  CHECK(MotionBlurGetStatus().blocked_enables==1);
  key=reinterpret_cast<std::uintptr_t>(other_name);std::memcpy(owner.data()+0x658,&key,sizeof(key));
  set(owner.data(),11,1);CHECK(get(owner.data(),11,0)==1); // wrong metadata is never touched
  key=reinterpret_cast<std::uintptr_t>(blur_name);std::memcpy(owner.data()+0x658,&key,sizeof(key));
  MotionBlurHookUninstall();CHECK(!get_active && !set_active && get(owner.data(),11,0)==1);
  CHECK(MotionBlurHookInstall(true) && blur_creates==creates_once);
  MotionBlurHookUninstall();
  std::puts("PASS actual native UI/blur detours, refusal, theater restoration and late-callback pass-through");return 0;
}
