#include "crash.h"

#include <windows.h>

#include <DbgHelp.h>

#include <cstdio>

#include "../common/logging.h"

namespace dsvr {
namespace {

LONG WINAPI Filter(EXCEPTION_POINTERS* ex) {
  DSVR_ERROR("Crash: code=0x%08lx at=%p",
             ex && ex->ExceptionRecord ? (unsigned long)ex->ExceptionRecord->ExceptionCode : 0,
             ex && ex->ExceptionRecord ? ex->ExceptionRecord->ExceptionAddress : nullptr);

  wchar_t dir[32768]{};
  DWORD n = GetModuleFileNameW(nullptr, dir, 32767);
  std::wstring path;
  if (n > 0 && n < 32767) {
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) {
      slash[1] = L'\0';
      path = dir;
    }
  }
  if (path.empty()) path = L".\\";
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t name[128]{};
  swprintf_s(name, L"DSVR-crash-%lu-%04u%02u%02u-%02u%02u%02u.dmp",
             GetCurrentProcessId(), st.wYear, st.wMonth, st.wDay, st.wHour,
             st.wMinute, st.wSecond);
  path += name;

  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ex;
    mei.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      MiniDumpWithIndirectlyReferencedMemory, &mei, nullptr,
                      nullptr);
    CloseHandle(f);
    DSVR_ERROR("Crash: minidump written %ls", path.c_str());
  } else {
    DSVR_ERROR("Crash: minidump failed (%lu)", GetLastError());
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void CrashHandlerInstall() {
  SetUnhandledExceptionFilter(Filter);
}

}  // namespace dsvr
