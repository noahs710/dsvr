// Reproduces a failed external-writer header followed by a fresh capture.
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/core/vr_trace.h"
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
static HANDLE ParseHandle(const wchar_t* value) {
  wchar_t* end = nullptr;
  const auto parsed = std::wcstoull(value, &end, 10);
  return end && end != value && !*end ? reinterpret_cast<HANDLE>(parsed) : nullptr;
}
static std::string Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int wmain(int argc, wchar_t* argv[]) {
  if (argc == 4) {
    HANDLE input = ParseHandle(argv[1]);
    HANDLE ack = ParseHandle(argv[2]);
    if (!input || !ack || !SetEvent(ack)) return 2;
    if (std::wstring(argv[3]).find(L"-flush") != std::wstring::npos) {
      std::string received;
      char buffer[4096];
      for (;;) {
        DWORD count = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &count, nullptr) || !count) break;
        received.append(buffer, count);
        if (received.find("\x1e\n") != std::string::npos) break;
      }
    }
    CloseHandle(input);
    SetEvent(ack);
    CloseHandle(ack);
    return 0;
  }
  CHECK(argc == 2);
  wchar_t local[MAX_PATH]{};
  CHECK(GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0);
  const auto directory = std::filesystem::path(local) / L"Temp" / L"opencode";
  CHECK(std::filesystem::is_directory(directory));
  const auto stem = L"dsvr-trace-sink-cleanup-" + std::to_wstring(GetCurrentProcessId());
  const auto failed_path = directory / (stem + L"-header.jsonl");
  const auto flush_path = directory / (stem + L"-flush.jsonl");
  const auto recovered_path = directory / (stem + L"-recovered.jsonl");
  const auto report_path = std::filesystem::path(recovered_path.wstring() + L".transport.json");
  std::error_code ignored;
  std::filesystem::remove(failed_path, ignored);
  std::filesystem::remove(flush_path, ignored);
  std::filesystem::remove(recovered_path, ignored);
  std::filesystem::remove(report_path, ignored);

  dsvr::VrTraceSetExternalWriter(argv[0]);
  CHECK(!dsvr::VrTraceBegin(failed_path.c_str()));
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::IoError);

  CHECK(dsvr::VrTraceBegin(flush_path.c_str()));
  CHECK(dsvr::VrTraceCheckpoint());
  dsvr::VrTraceEvent failed_event{};
  failed_event.event = "flush_failure";
  dsvr::VrTraceRecord(failed_event);
  dsvr::VrTraceRequestStop();
  dsvr::VrTracePump();
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::IoError);

  dsvr::VrTraceSetExternalWriter(argv[1]);
  CHECK(dsvr::VrTraceBegin(recovered_path.c_str()));
  dsvr::VrTraceEvent event{};
  event.event = "cleanup_recovery";
  dsvr::VrTraceRecord(event);
  dsvr::VrTraceRequestStop();
  dsvr::VrTracePump();
  CHECK(dsvr::VrTraceState() == dsvr::TraceCaptureState::Complete);
  const auto capture = Read(recovered_path);
  CHECK(capture.find("cleanup_recovery") != std::string::npos);
  CHECK(capture.find("\"io_error\":false") != std::string::npos);
  const auto report = Read(report_path);
  CHECK(report.find("\"producer_footer_seen\":true") != std::string::npos);
  CHECK(std::filesystem::remove(recovered_path));
  CHECK(std::filesystem::remove(report_path));
  std::puts("trace sink failure cleanup: PASS");
  return 0;
}
