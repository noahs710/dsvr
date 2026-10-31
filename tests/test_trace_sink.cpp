// Exercises the external writer lifecycle; no game, hooks or save files.
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/core/trace_sink.h"
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
static std::string Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
int wmain(int argc, wchar_t** argv) {
  CHECK(argc == 2);
  const std::filesystem::path writer = argv[1];
  CHECK(std::filesystem::exists(writer));
  wchar_t local[MAX_PATH]{};
  CHECK(GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0);
  const auto directory = std::filesystem::path(local) / L"Temp" / L"opencode";
  CHECK(std::filesystem::is_directory(directory));
  const auto path = directory / (L"dsvr-sink-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
  std::error_code code;
  std::filesystem::remove(path, code);
  std::filesystem::remove(std::filesystem::path(path.wstring() + L".transport.json"), code);
  std::wstring command = L"\"" + writer.wstring() + L"\" --self-test \"" + path.wstring() + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child));
  CHECK(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0);
  DWORD result = 1;
  CHECK(GetExitCodeProcess(child.hProcess, &result) && result == 0);
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  const auto text = Read(path);
  CHECK(text.find("{\"event\":\"header\"") != std::string::npos);
  CHECK(text.find("{\"event\":\"footer\"") != std::string::npos);
  const auto report = Read(std::filesystem::path(path.wstring() + L".transport.json"));
  CHECK(report.find("\"producer_footer_seen\":true") != std::string::npos);
  CHECK(report.find("\"complete_lines\":3") != std::string::npos);
  CHECK(report.find("\"trailing_bytes\":0") != std::string::npos);
  CHECK(report.find("\"io_error\":false") != std::string::npos);
  CHECK(report.find("\"bytes\":" + std::to_string(text.size())) != std::string::npos);
  CHECK(report.find("\"future_extension\"") != std::string::npos);
  CHECK(std::filesystem::remove(path, code));
  CHECK(std::filesystem::remove(std::filesystem::path(path.wstring() + L".transport.json"), code));
  const auto client_path = directory / (L"dsvr-sink-client-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
  dsvr::TraceSink sink;
  FILE* stream = sink.Open(writer.wstring(), client_path.wstring());
  CHECK(stream != nullptr);
  CHECK(std::fputs("{\"event\":\"header\"}\n", stream) >= 0);
  CHECK(sink.Flush(stream));
  CHECK(std::fputs("{\"event\":\"footer\"}\n", stream) >= 0);
  CHECK(sink.Close(stream));
  CHECK(Read(client_path).find("footer") != std::string::npos);
  CHECK(std::filesystem::remove(client_path, code));
  CHECK(std::filesystem::remove(std::filesystem::path(client_path.wstring() + L".transport.json"), code));
  return 0;
}
