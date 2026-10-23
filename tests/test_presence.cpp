#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "../src/common/config.h"
#include "../src/common/logging.h"
#include "../src/core/presence.h"
namespace {
constexpr std::uintptr_t base=0x140000000;
std::array<unsigned char,0x900> player{};
std::array<unsigned char,0x200> primary{},alternate{};
std::uintptr_t world=0x1000;
dsvr::Config config;
bool theater=false,running=true;
unsigned writes=0,originals=0;
using Visibility=void(__fastcall*)(void*);Visibility callback=nullptr;
bool native_primary_visible = false;
void __fastcall Original(void*) {
  ++originals;
  primary[0x92] = native_primary_visible ? 1 : 0;
  alternate[0xaa] = 1;
}
}
namespace dsvr {
Config DiagnosticsCurrentConfig(){return config;}
bool XrMenuPresentationActive(){return theater;}
bool XrSessionRunning(){return running;}
std::uintptr_t ModuleBase(const wchar_t*){return base;}
bool BytesMatch(std::uintptr_t,const void*,std::size_t){return true;}
void LogWrite(LogLevel,const char*,int,const char*,...){}
bool GuardedReadBytes(std::uintptr_t at,void* out,std::size_t size){
  auto copy=[&](const void* value){std::memcpy(out,value,size);return true;};
  if(at==base+0x1c77e50 && size==sizeof(world))return copy(&world);
  const auto address=reinterpret_cast<std::uintptr_t>(player.data());
  if(at==world+0x68 && size==sizeof(address))return copy(&address);
  for(auto range:{std::pair{address,player.size()},std::pair{reinterpret_cast<std::uintptr_t>(primary.data()),primary.size()},std::pair{reinterpret_cast<std::uintptr_t>(alternate.data()),alternate.size()}})
    if(at>=range.first && at-range.first<=range.second && size<=range.second-(at-range.first))return copy(reinterpret_cast<void*>(at));
  return false;
}
bool GuardedWriteBytes(std::uintptr_t at,const void* data,std::size_t size){
  for(auto* model:{&primary,&alternate}) {
    const auto address=reinterpret_cast<std::uintptr_t>(model->data());
    const auto dirty=model==&primary?0x84:0x9c,visible=model==&primary?0x92:0xaa;
    if((at==address+dirty && size==4)||(at==address+visible && size==1)) {++writes;std::memcpy(reinterpret_cast<void*>(at),data,size);return true;}
  }
  return false;
}
}
MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID fn,LPVOID* original){callback=reinterpret_cast<Visibility>(fn);*original=reinterpret_cast<void*>(Original);return MH_OK;}
MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
  const auto first=reinterpret_cast<std::uintptr_t>(primary.data()),second=reinterpret_cast<std::uintptr_t>(alternate.data());
  std::memcpy(player.data()+0x60,&first,sizeof(first));std::memcpy(player.data()+0x850,&second,sizeof(second));
  config.vr_enabled=config.headlook_enabled=true;config.hide_player_model=false;config.hide_second_model=true;
  CHECK(dsvr::PresenceHookInstall(true));callback(player.data());
  CHECK(primary[0x92]==0 && alternate[0xaa]==1 && writes==0);
  native_primary_visible=true;callback(player.data());
  CHECK(primary[0x92]==1 && alternate[0xaa]==0 && writes==2);
  native_primary_visible=false;callback(player.data());
  CHECK(primary[0x92]==0 && alternate[0xaa]==1 && writes==2);
  config.forearm_only=true;native_primary_visible=true;callback(player.data());
  CHECK(primary[0x92]==1 && alternate[0xaa]==1);
  theater=true;callback(player.data());CHECK(primary[0x92]==1 && alternate[0xaa]==1);theater=false;
  running=false;callback(player.data());CHECK(primary[0x92]==1 && alternate[0xaa]==1);running=true;
  // At most one of the two render models may be hidden. The live 23 Oct run
  // had hide_player_model and hide_second_model both true and the player was
  // completely invisible for the whole session, so hiding the primary keeps the
  // body model rendering instead of hiding everything.
  config.forearm_only=false;config.hide_player_model=true;callback(player.data());
  CHECK(primary[0x92]==0 && alternate[0xaa]==1);
  config.hide_player_model=false;native_primary_visible=false;
  const std::uintptr_t absent=0;std::memcpy(player.data()+0x60,&absent,sizeof(absent));
  callback(player.data());CHECK(alternate[0xaa]==1);
  std::memcpy(player.data()+0x60,&first,sizeof(first));
  const auto before=originals;dsvr::PresenceHookUninstall();callback(player.data());
  CHECK(primary[0x92]==0 && alternate[0xaa]==1 && originals==before+1);
  std::puts("PASS actual body visibility detour: equipped body retained, duplicates hidden, native restoration");
}
