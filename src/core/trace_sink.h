#pragma once
#include <windows.h>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

namespace dsvr {
// Owned only by the diagnostics writer; never called under loader lock or on hooks.
class TraceSink {
 public:
  FILE* Open(const std::wstring& executable, const std::wstring& path) {
    if (process_ || path.find(L'"') != std::wstring::npos) return nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE input = nullptr, output = nullptr;
    if (!CreatePipe(&input, &output, &security, 65536)) return nullptr;
    ack_ = CreateEventW(&security, FALSE, FALSE, nullptr);
    if (!ack_ || !SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) {
      CloseHandle(input); CloseHandle(output); Reset(); return nullptr;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    HANDLE handles[]{input, ack_};
    bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size) != FALSE;
    bool ready = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
    std::wstring command = L"\"" + executable + L"\" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(input)) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(ack_)) + L" \"" + path + L"\"";
    PROCESS_INFORMATION child{};
    const bool started = ready && CreateProcessW(executable.c_str(), command.data(),
        nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &child);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(input);
    SetHandleInformation(ack_, HANDLE_FLAG_INHERIT, 0);
    if (!started) { CloseHandle(output); Reset(); return nullptr; }
    CloseHandle(child.hThread);
    process_ = child.hProcess;
    if (!WaitAck()) { CloseHandle(output); Reset(); return nullptr; }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(output), _O_WRONLY | _O_BINARY);
    if (fd == -1) { CloseHandle(output); Reset(); return nullptr; }
    FILE* file = _fdopen(fd, "wb");
    if (!file) { _close(fd); Reset(); }
    return file;
  }
  bool Flush(FILE* file) {
    if (std::fputs("\x1e\n", file) < 0 || std::fflush(file) != 0) return false; // checkpoint marker, not an event
    return WaitAck();
  }
  bool Close(FILE* file) {
    bool ok = std::fclose(file) == 0;
    DWORD result = 1;
    if (!process_ || WaitForSingleObject(process_, 5000) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process_, &result) || result != 0) ok = false;
    Reset();
    return ok;
  }
 private:
  bool WaitAck() {
    HANDLE handles[]{ack_, process_};
    return WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0;
  }
  void Reset() {
    if (process_) CloseHandle(process_);
    if (ack_) CloseHandle(ack_);
    process_ = ack_ = nullptr;
  }
  HANDLE process_ = nullptr, ack_ = nullptr;
};
}
