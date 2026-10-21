#include "blur_files.h"
#include "blur_state.h"
#include <windows.h>
#include <cstdio>
namespace dsvr {
namespace {
bool Read(const std::wstring& path,std::string& out) {
  FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"rb") || !file)return false;
  char buffer[4096];std::size_t count=0;out.clear();
  while((count=fread(buffer,1,sizeof(buffer),file))!=0 && out.size()<65536)out.append(buffer,count);
  const bool ok=ferror(file)==0 && out.size()<65536 && out.find('\0')==std::string::npos;
  fclose(file);return ok;
}
bool Write(const std::wstring& path,const std::string& text) {
  const auto temp=path+L".dsvr.tmp";
  HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false; // never truncate/delete a preexisting temp
  DWORD written=0;
  bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE && written==text.size();
  if(ok)ok=FlushFileBuffers(file)!=FALSE;
  ok=CloseHandle(file)!=FALSE && ok;
  if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!ok)DeleteFileW(temp.c_str());return ok;
}
bool Save(const std::wstring& path,const Config& settings) {
  const auto temp=path+L".dsvr.tmp";
  HANDLE reserved=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(reserved==INVALID_HANDLE_VALUE)return false;
  bool ok=CloseHandle(reserved)!=FALSE && ConfigSave(temp.c_str(),settings);
  if(ok) {
    HANDLE saved=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    ok=saved!=INVALID_HANDLE_VALUE;
    if(ok) {ok=FlushFileBuffers(saved)!=FALSE;ok=CloseHandle(saved)!=FALSE && ok;}
  }
  if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!ok)DeleteFileW(temp.c_str());return ok;
}
bool BinaryValue(const std::string& value) {return value=="0" || value=="1";}
}
bool InstallBlurPreference(const std::wstring& ini,const std::wstring& config,Config& settings,std::string& status) {
  std::string text;
  if(!Read(ini,text)) {status="Run the game once to create DarkSouls.ini, then retry installation. No graphics preference was changed.";return false;}
  const auto preference=FindBlurPreference(text);
  if(!preference.found || !BinaryValue(preference.value)) {status="The native Blur preference is missing or unsupported. No graphics preference was changed.";return false;}
  Config recorded=settings;recorded.force_disable_motion_blur=true;
  Config durable;
  if(GetFileAttributesW(config.c_str())!=INVALID_FILE_ATTRIBUTES) {
    if(!ConfigLoad(config.c_str(),durable)) {
      status="Cannot read the existing recovery record. No graphics preference was changed.";return false;
    }
    // A fresh/stale caller must not replace a durable original with forced zero.
    if(!durable.motion_blur_original.empty())recorded.motion_blur_original=durable.motion_blur_original;
  }
  if(recorded.motion_blur_original.empty())recorded.motion_blur_original=preference.value;
  if(!BinaryValue(recorded.motion_blur_original) || !Save(config,recorded)) {
    status="Cannot record the original motion-blur preference. The game INI was left unchanged.";return false;
  }
  settings=recorded; // the recovery record is durable, even if forcing off fails
  std::string forced;const auto result=ForceBlurOff(text,forced);
  if(result==BlurForceResult::Forced && !Write(ini,forced)) {
    status="Cannot disable motion blur. Original preference remains recorded; retry installation.";return false;
  }
  status="Motion blur disabled; original preference recorded for uninstall.";return true;
}
bool UninstallBlurPreference(const std::wstring& ini,const std::wstring& config,std::string& status) {
  Config settings;
  if(!ConfigLoad(config.c_str(),settings)) {
    status="Cannot read the motion-blur recovery record. Preference unchanged; retry uninstall.";return false;
  }
  if(settings.motion_blur_original.empty()) {
    status="No recorded motion-blur preference to restore.";return true;
  }
  std::string text;
  if(!BinaryValue(settings.motion_blur_original) || !Read(ini,text)) {
    status="Cannot read the native preference or its recovery record. Record retained; retry uninstall.";return false;
  }
  std::string restored;const auto result=RestoreBlurPreference(text,settings.motion_blur_original,restored);
  if(result==BlurRestoreResult::NotFound || (result==BlurRestoreResult::Restored && !Write(ini,restored))) {
    status="Cannot restore motion blur. Recovery record retained; retry uninstall.";return false;
  }
  settings.motion_blur_original.clear();
  if(!Save(config,settings)) {status="Native motion-blur preference restored. Recovery record could not be cleared; retry uninstall.";return false;}
  status="Original motion-blur preference restored.";return true;
}
}
