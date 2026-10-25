// Real detour fixture: preserve the native 64-bit result and argument ABI.
#include <windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstring>
#include "../src/common/logging.h"
#include "../src/common/memory.h"
#include "../src/core/frame_rate.h"
#include "../src/core/xr.h"
#include "../src/core/cpu_frame_timer.h"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
using Fn=std::uint64_t(__fastcall*)(void*,void*,int,void*,std::uintptr_t);
Fn callback=nullptr;
void* observed[3]{};
int observed_mode=0;
std::uintptr_t observed_fifth=0;
std::uint64_t result=0;
bool session_running=false;
dsvr::EffectiveRate rate{};
std::uintptr_t live_owner=0;
unsigned setter_calls=0;
void __fastcall Setter(void* owner,int fps,unsigned char flag){
  ++setter_calls;
  auto* bytes=static_cast<unsigned char*>(owner);
  std::memcpy(bytes+0x60,&fps,sizeof(fps));
  bytes[0x68]=flag;
  std::memset(bytes+0x678,0,0x50);  // verified native setter resets epoch/history
}
std::uintptr_t TestModuleBase(){
  return reinterpret_cast<std::uintptr_t>(&Setter)-0xce2cf0;
}
std::uint64_t __fastcall Original(void* a,void* b,int c,void* d,std::uintptr_t fifth){
  observed[0]=a;observed[1]=b;observed[2]=d;observed_mode=c;
  observed_fifth=fifth;
  return result;
}
}
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID detour,LPVOID* original){
  callback=reinterpret_cast<Fn>(detour);*original=reinterpret_cast<void*>(&Original);return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
namespace dsvr {
void LogWrite(LogLevel,const char*,int,const char*,...){}
std::uintptr_t ModuleBase(const wchar_t*){return TestModuleBase();}
bool BytesMatch(std::uintptr_t address,const void*,std::size_t size){
  return size==8 && (address==TestModuleBase()+0xce2dd0 ||
                    address==TestModuleBase()+0xce2cf0);
}
bool GuardedReadBytes(std::uintptr_t address,void* out,std::size_t size){
  if(!live_owner || !((address==live_owner+0x60 && size==sizeof(int)) ||
                     (address==live_owner+0x68 && size==1)))return false;
  std::memcpy(out,reinterpret_cast<void*>(address),size);return true;
}
bool XrSessionRunning(){return session_running;}
bool XrEffectiveRate(EffectiveRate& out){out=rate;return rate.from_runtime;}
}
int main(){
  using namespace dsvr;
  CHECK(FrameRateHookInstall(true) && callback);
  int owner=0,a=0,b=0;
  for(int mode:{0,1,2,-1})for(auto value:{0ull,1ull,0xfedcba9876543210ull}){
    result=value;
    const auto fifth=static_cast<std::uintptr_t>(value ^ 0x9e3779b97f4a7c15ull);
    CHECK(callback(&owner,&a,mode,&b,fifth)==value);
    CHECK(observed_fifth==fifth);
    CHECK(observed[0]==&owner && observed[1]==&a && observed[2]==&b && observed_mode==mode);
  }
  CHECK(g_cpu_frame_timer.Snapshot()[static_cast<unsigned>(CpuStage::NativeFrameBoundary)].calls==12);
  alignas(8) unsigned char native_owner[0x6d0]{};
  int native_fps=60;
  std::memcpy(native_owner+0x60,&native_fps,sizeof(native_fps));
  live_owner=reinterpret_cast<std::uintptr_t>(native_owner);
  session_running=true;
  RateStabilizer stabilizer;
  auto frame=[&](double hz){
    rate=stabilizer.Update(90,static_cast<long long>(std::llround(1e9/hz)));
    callback(native_owner,&a,0,&b,0xa55aa55a11223344ull);

  };
  frame(90);
  CHECK(setter_calls==1 && observed_fifth==0xa55aa55a11223344ull);
  for(int cycle=0;cycle<5;++cycle) {
    // A sentinel proves none of the subsequent detour calls resets history.
    native_owner[0x688]=0xa5;
    for(double hz:{18.0,22.5,30.0,45.0,90.0})
      for(int i=0;i<5;++i)frame(hz);
    CHECK(setter_calls==1 && native_owner[0x688]==0xa5);
  }
  // Native code can reset the same owner's rate when a mode changes.
  // Cached "we applied 90" must not suppress restoration to the live target.
  native_fps=60;
  std::memcpy(native_owner+0x60,&native_fps,sizeof(native_fps));
  frame(90);
  CHECK(setter_calls==2);
  native_owner[0x688]=0xa5;
  frame(90);
  CHECK(setter_calls==2 && native_owner[0x688]==0xa5);
  FrameRateHookUninstall();
  std::puts("PASS native frame-boundary ABI and throttled-period pacing-history preservation");
}
