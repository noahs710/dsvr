#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>
#include "../src/common/blur_files.h"
#include "../src/common/blur_state.h"
#define CHECK(x) do { if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static std::string Read(const std::wstring& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Write(const std::wstring& p,const std::string& s) {std::ofstream f(p,std::ios::binary);f<<s;}
int main() {
  wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
  const auto root=std::wstring(temp)+L"DSVR-blur-files-"+std::to_wstring(GetCurrentProcessId());
  CHECK(CreateDirectoryW(root.c_str(),nullptr));
  const auto ini=root+L"\\game.ini",config=root+L"\\DSVR.toml";
  const std::string original="[DisplaySettingFilter]\r\nBlur = 1\r\nDOF=1\r\n";
  Write(ini,original);dsvr::Config settings;settings.force_disable_motion_blur=false;std::string status;
  CHECK(!dsvr::UninstallBlurPreference(ini,config,status) && Read(ini)==original);
  const std::string sentinel="unowned temporary file: preserve exactly\r\n";
  Write(config+L".dsvr.tmp",sentinel);
  CHECK(!dsvr::InstallBlurPreference(ini,config,settings,status));
  CHECK(Read(config+L".dsvr.tmp")==sentinel && Read(ini)==original);
  CHECK(settings.motion_blur_original.empty() && !settings.force_disable_motion_blur);
  CHECK(DeleteFileW((config+L".dsvr.tmp").c_str()));
  CHECK(CreateDirectoryW((config+L".dsvr.tmp").c_str(),nullptr));
  CHECK(!dsvr::InstallBlurPreference(ini,config,settings,status));
  CHECK(Read(ini)==original && settings.motion_blur_original.empty());
  CHECK(RemoveDirectoryW((config+L".dsvr.tmp").c_str()));
  CHECK(CreateDirectoryW((ini+L".dsvr.tmp").c_str(),nullptr));
  CHECK(!dsvr::InstallBlurPreference(ini,config,settings,status));
  dsvr::Config saved;CHECK(dsvr::ConfigLoad(config.c_str(),saved));
  CHECK(saved.motion_blur_original=="1" && Read(ini)==original);
  CHECK(RemoveDirectoryW((ini+L".dsvr.tmp").c_str()));
  Write(ini+L".dsvr.tmp",sentinel);
  CHECK(!dsvr::InstallBlurPreference(ini,config,settings,status));
  CHECK(Read(ini+L".dsvr.tmp")==sentinel && Read(ini)==original);
  CHECK(dsvr::ConfigLoad(config.c_str(),saved) && saved.motion_blur_original=="1");
  CHECK(DeleteFileW((ini+L".dsvr.tmp").c_str()));
  CHECK(dsvr::InstallBlurPreference(ini,config,settings,status));
  CHECK(settings.force_disable_motion_blur && settings.motion_blur_original=="1");
  CHECK(dsvr::FindBlurPreference(Read(ini)).value=="0");
  CHECK(dsvr::InstallBlurPreference(ini,config,settings,status));
  CHECK(settings.motion_blur_original=="1");
  dsvr::Config stale;stale.force_disable_motion_blur=false;
  CHECK(dsvr::InstallBlurPreference(ini,config,stale,status));
  CHECK(stale.motion_blur_original=="1");
  CHECK(dsvr::ConfigLoad(config.c_str(),saved) && saved.motion_blur_original=="1");
  Write(ini,"[DisplaySettingFilter]\r\nBlur = 0\r\nDOF=0\r\n");
  CHECK(CreateDirectoryW((ini+L".dsvr.tmp").c_str(),nullptr));
  CHECK(!dsvr::UninstallBlurPreference(ini,config,status));
  CHECK(dsvr::ConfigLoad(config.c_str(),saved) && saved.motion_blur_original=="1");
  CHECK(RemoveDirectoryW((ini+L".dsvr.tmp").c_str()));
  Write(config+L".dsvr.tmp",sentinel);
  CHECK(!dsvr::UninstallBlurPreference(ini,config,status));
  CHECK(dsvr::FindBlurPreference(Read(ini)).value=="1");
  CHECK(Read(config+L".dsvr.tmp")==sentinel);
  CHECK(dsvr::ConfigLoad(config.c_str(),saved) && saved.motion_blur_original=="1");
  CHECK(DeleteFileW((config+L".dsvr.tmp").c_str()));
  CHECK(dsvr::UninstallBlurPreference(ini,config,status));
  CHECK(dsvr::FindBlurPreference(Read(ini)).value=="1" && Read(ini).find("DOF=0")!=std::string::npos);
  CHECK(dsvr::ConfigLoad(config.c_str(),saved) && saved.motion_blur_original.empty());
  Write(ini,"[DisplaySettingFilter]\nBlur=0\n");settings=saved;
  CHECK(dsvr::InstallBlurPreference(ini,config,settings,status) && settings.motion_blur_original=="0");
  Write(ini,original);
  CHECK(dsvr::UninstallBlurPreference(ini,config,status));
  CHECK(dsvr::FindBlurPreference(Read(ini)).value=="0");
  CHECK(GetFileAttributesW((ini+L".dsvr.tmp").c_str())==INVALID_FILE_ATTRIBUTES);
  CHECK(GetFileAttributesW((config+L".dsvr.tmp").c_str())==INVALID_FILE_ATTRIBUTES);
  CHECK(DeleteFileW(ini.c_str()));CHECK(DeleteFileW(config.c_str()));CHECK(RemoveDirectoryW(root.c_str()));
  std::puts("PASS durable blur receipt, duplicate/stale install, preserved temp files and reversible partial failures");
}
